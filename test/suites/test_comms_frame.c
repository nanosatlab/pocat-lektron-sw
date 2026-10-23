/**
 * @file test_comms_frame.c
 * @brief Host tests for the radio link layer: frame authentication (SHA-256 / HMAC), encode/decode, and the duplicate-detection ring.
 * @date 2026-10-23
 *
 * @details
 * **What is under test**
 * - src/subsystems/comms/auth.c: SHA-256 and the truncated HMAC-SHA256 tag
 *   (8 bytes) that authenticates every frame (§4.7).
 * - src/subsystems/comms/frame.c:
 *   - `air_encode()` / `air_decode()`
 *   - `air_encode_ack()` / `air_encode_nack()`
 * - src/subsystems/comms/dedup.c: the 16-entry ring of recently received
 *   (type, seq) pairs. It lets a retransmitted frame get its cached ACK
 *   instead of running twice.
 *
 * **Air frame layout** (what `air_encode()` produces)
 * @verbatim
 *  byte 0   1     2      3    4     5 ... 5+LEN-1   5+LEN ... 5+LEN+7
 *  [VER][TYPE][FLAGS][SEQ][LEN] [ payload (LEN) ]  [ HMAC tag, 8 bytes ]
 *  max total 255 bytes -> max payload 242 bytes
 * @endverbatim
 * The tag covers the 5 header bytes (with the IS_RETX flag masked out) and
 * the payload.
 *
 * **How it is built** (see test/CMakeLists.txt)
 * - `auth.c` is `#include`d below, so the tests can call its `static` SHA-256
 *   and HMAC helpers directly with the official test vectors.
 * - `frame.c` and `dedup.c` are linked through SOURCES.
 * - The PSK comes from a `psk.h` that CMake generates in the build folder with
 *   the **test-only key** 00 01 02 ... 0F. It is never the flight key;
 *   `test_psk_is_test_key` checks that.
 * - Nothing here uses the RTOS. The fake RTOS is linked only because every
 *   suite links it.
 *
 * **How to run**
 * @code
 * docker run --rm -v "${PWD}:/app" pocat-tests test_comms_frame
 * docker run --rm -v "${PWD}:/app" pocat-tests test_comms_frame -f decode
 * @endcode
 */
#include "unity.h"
#include "fake_rtos.h"
#include "frame.h"
#include "dedup.h"
#include "pocat_test_main.h"
#include <string.h>

#include "auth.c"

/** @brief Test PSK that CMake writes into the generated psk.h (00 01 ... 0F). Never the flight key. */
static const uint8_t TEST_PSK[AUTH_PSK_LEN] = { 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15 };

/**
 * @brief Runs before every test: resets the fake RTOS.
 * @details The comms code under test doesn't use the RTOS. The reset is here
 * only so every suite starts from the same state.
 */
void setUp(void)
{
    fake_rtos_reset();
}

/**
 * @brief Runs after every test. Nothing to check.
 * @details frame, auth and dedup never enter critical sections or suspend the
 * scheduler, so the balance checks of the other suites (rule R3) don't apply.
 */
void tearDown(void)
{
}

/* =========================================================================
 * 1. Cryptography: official test vectors
 *    If these fail, no frame can be authenticated, whatever the other tests say.
 * ========================================================================= */

/**
 * @brief SHA-256("abc") matches the official FIPS 180-4 test vector.
 * @details Expected digest:
 * `ba7816bf 8f01cfea 414140de 5dae2223 b00361a3 96177a9c b410ff61 f20015ad`.
 * The test feeds the 3 bytes through `sha256_init`, then `sha256_update`,
 * then `sha256_final`.
 */
void test_sha256_abc_known_answer(void)
{
    static const uint8_t expected[32] = {
        0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,
        0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad };
    sha256_ctx_t ctx;
    uint8_t digest[32];

    sha256_init(&ctx);
    sha256_update(&ctx, (const uint8_t *)"abc", 3);
    sha256_final(&ctx, digest);

    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, digest, 32);
}

/**
 * @brief HMAC-SHA256 matches RFC 4231 test case 2 (first 8 bytes, the length of our tag).
 * @details RFC 4231 uses the key "Jefe" (4 bytes). Our HMAC always takes a
 * 16-byte PSK, so the key is "Jefe" followed by 12 zero bytes. HMAC pads
 * short keys with zeros anyway, so the result is identical.
 * Message: "what do ya want for nothing?" (28 bytes). Expected tag:
 * `5b dc c1 46 bf 60 75 4e`.
 */
void test_hmac_rfc4231_case2_first8(void)
{
    static const uint8_t expected[8] = { 0x5b,0xdc,0xc1,0x46,0xbf,0x60,0x75,0x4e };
    uint8_t key[AUTH_PSK_LEN] = { 'J','e','f','e' };   /* rest is zero */
    const char *msg = "what do ya want for nothing?";
    uint8_t tag[AUTH_TAG_LEN];

    hmac_sha256_8(key, (const uint8_t *)msg, 28, NULL, 0, tag);

    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, tag, 8);
}

/**
 * @brief Splitting the message into two parts gives the same tag as passing it whole.
 * @details `hmac_sha256_8()` takes the frame header and the payload as two
 * separate buffers, so the payload doesn't have to be copied after the header.
 * Both parts must be hashed as one continuous stream. Here the same 28-byte
 * message is passed whole, and then as 5 + 23 bytes.
 */
void test_hmac_two_part_input_equals_single_part(void)
{
    const char *msg = "what do ya want for nothing?";
    uint8_t key[AUTH_PSK_LEN] = { 'J','e','f','e' };
    uint8_t whole[AUTH_TAG_LEN], split[AUTH_TAG_LEN];

    hmac_sha256_8(key, (const uint8_t *)msg, 28, NULL, 0, whole);
    hmac_sha256_8(key, (const uint8_t *)msg, 5, (const uint8_t *)msg + 5, 23, split);

    TEST_ASSERT_EQUAL_HEX8_ARRAY(whole, split, AUTH_TAG_LEN);
}

/**
 * @brief The PSK compiled into this test is the test key, not the flight key.
 * @details This is a safety check on the build setup. If a developer's real
 * `include/subsystems/comms/psk.h` were picked up instead of the generated
 * one, this test would fail.
 */
void test_psk_is_test_key(void)
{
    TEST_ASSERT_EQUAL_HEX8_ARRAY(TEST_PSK, COMMS_PSK, AUTH_PSK_LEN);
}

/* =========================================================================
 * 2. Encode / decode
 * ========================================================================= */

/**
 * @brief A frame encoded with `air_encode()` decodes back to the same contents.
 * @details Encodes a TC with the REQUIRES_ACK flag, seq 42 and a 10-byte
 * payload. Expected after `air_decode()`:
 * - the result is 0 (accepted);
 * - the version, type and seq are unchanged;
 * - the flags are REQUIRES_ACK plus AUTH_TAG, which the encoder adds;
 * - the length and payload bytes are unchanged.
 */
void test_encode_decode_roundtrip(void)
{
    uint8_t payload[10] = { 1,2,3,4,5,6,7,8,9,10 };
    uint8_t buf[AIR_FRAME_MAX];
    AirFrame_t out;
    memset(&out, 0, sizeof(out));

    uint8_t n = air_encode(buf, AIR_TC, AIR_FLAG_REQUIRES_ACK, 42, payload, sizeof(payload));
    TEST_ASSERT_GREATER_THAN_UINT8(0, n);
    TEST_ASSERT_EQUAL_INT(0, air_decode(buf, n, &out));

    TEST_ASSERT_EQUAL_UINT8(AIR_PROTOCOL_VER, out.ver);
    TEST_ASSERT_EQUAL_UINT8(AIR_TC, out.type);
    TEST_ASSERT_EQUAL_UINT8(AIR_FLAG_REQUIRES_ACK | AIR_FLAG_AUTH_TAG, out.flags);
    TEST_ASSERT_EQUAL_UINT8(42, out.seq);
    TEST_ASSERT_EQUAL_UINT8(sizeof(payload), out.len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(payload, out.payload, sizeof(payload));
}

/**
 * @brief `air_encode()` sets the AUTH_TAG flag and returns header + payload + tag as the total length.
 * @details For a 7-byte payload: the total is 5 + 7 + 8 = 20, the FLAGS byte
 * (byte 2) has AUTH_TAG set, and the LEN byte (byte 4) is 7.
 */
void test_encode_sets_auth_flag_and_total_length(void)
{
    uint8_t payload[7] = { 0 };
    uint8_t buf[AIR_FRAME_MAX];

    uint8_t n = air_encode(buf, AIR_TM, 0x00, 1, payload, sizeof(payload));

    TEST_ASSERT_EQUAL_UINT8(AIR_FRAME_HDR + sizeof(payload) + AUTH_TAG_LEN, n);
    TEST_ASSERT_BITS_HIGH(AIR_FLAG_AUTH_TAG, buf[2]);
    TEST_ASSERT_EQUAL_UINT8(sizeof(payload), buf[4]);
}

/**
 * @brief A single flipped bit in the payload makes `air_decode()` reject the frame.
 * @details This is the purpose of authentication: a corrupted or forged
 * telecommand must never run. Flips one bit of payload byte 3 and expects -1.
 */
void test_decode_rejects_tampered_payload(void)
{
    uint8_t payload[10] = { 1,2,3,4,5,6,7,8,9,10 };
    uint8_t buf[AIR_FRAME_MAX];
    AirFrame_t out;

    uint8_t n = air_encode(buf, AIR_TC, 0, 1, payload, sizeof(payload));
    buf[AIR_FRAME_HDR + 3] ^= 0x01;

    TEST_ASSERT_EQUAL_INT(-1, air_decode(buf, n, &out));
}

/**
 * @brief A modified header (here the SEQ byte) is also rejected, because the tag covers the header.
 * @details If only the payload were authenticated, an attacker could replay
 * a valid frame with a new sequence number.
 */
void test_decode_rejects_tampered_header(void)
{
    uint8_t payload[4] = { 1,2,3,4 };
    uint8_t buf[AIR_FRAME_MAX];
    AirFrame_t out;

    uint8_t n = air_encode(buf, AIR_TC, 0, 1, payload, sizeof(payload));
    buf[3] ^= 0x01;

    TEST_ASSERT_EQUAL_INT(-1, air_decode(buf, n, &out));
}

/**
 * @brief A frame with a different protocol version (byte 0) is rejected.
 */
void test_decode_rejects_wrong_version(void)
{
    uint8_t payload[4] = { 1,2,3,4 };
    uint8_t buf[AIR_FRAME_MAX];
    AirFrame_t out;

    uint8_t n = air_encode(buf, AIR_TC, 0, 1, payload, sizeof(payload));
    buf[0] = AIR_PROTOCOL_VER + 1u;

    TEST_ASSERT_EQUAL_INT(-1, air_decode(buf, n, &out));
}

/**
 * @brief A frame without the AUTH_TAG flag is dropped, even if everything else is intact.
 * @details The OBC accepts only authenticated frames (§4.7). Clearing the
 * flag must not let a frame skip verification.
 */
void test_decode_rejects_missing_auth_flag(void)
{
    uint8_t payload[4] = { 1,2,3,4 };
    uint8_t buf[AIR_FRAME_MAX];
    AirFrame_t out;

    uint8_t n = air_encode(buf, AIR_TC, 0, 1, payload, sizeof(payload));
    buf[2] &= (uint8_t)~AIR_FLAG_AUTH_TAG;

    TEST_ASSERT_EQUAL_INT(-1, air_decode(buf, n, &out));
}

/**
 * @brief A truncated frame is rejected however short it is.
 * @details Four lengths are tried, all expected to fail:
 * - tag one byte short;
 * - payload cut after 3 bytes;
 * - shorter than the 5-byte header;
 * - empty.
 * Radio reception can end early, so none of these may be accepted or read
 * past the end of the buffer.
 */
void test_decode_rejects_truncated_frame(void)
{
    uint8_t payload[10] = { 1,2,3,4,5,6,7,8,9,10 };
    uint8_t buf[AIR_FRAME_MAX];
    AirFrame_t out;

    uint8_t n = air_encode(buf, AIR_TC, 0, 1, payload, sizeof(payload));

    TEST_ASSERT_EQUAL_INT(-1, air_decode(buf, (uint8_t)(n - 1), &out));                      /* tag one byte short */
    TEST_ASSERT_EQUAL_INT(-1, air_decode(buf, (uint8_t)(AIR_FRAME_HDR + 3), &out));          /* payload cut */
    TEST_ASSERT_EQUAL_INT(-1, air_decode(buf, (uint8_t)(AIR_FRAME_HDR - 1), &out));          /* shorter than header */
    TEST_ASSERT_EQUAL_INT(-1, air_decode(buf, 0, &out));
}

/**
 * @brief The IS_RETX flag isn't covered by the tag, so a retransmission can be marked without re-signing it.
 * @details The stop-and-wait and ARQ code (saw.c, arq.c) resends a frame by
 * setting IS_RETX on the bytes it already has (`tmp[2] |= AIR_FLAG_IS_RETX`),
 * without computing a new tag. `auth_tag()` masks that flag out before
 * hashing. The test checks that:
 * - a frame encoded with IS_RETX has different flags but the same tag as
 *   one encoded without it, and it decodes with IS_RETX still visible;
 * - setting IS_RETX by hand on an already-encoded frame still decodes.
 */
void test_retx_flag_does_not_change_tag(void)
{
    uint8_t payload[6] = { 9,8,7,6,5,4 };
    uint8_t a[AIR_FRAME_MAX], b[AIR_FRAME_MAX];
    AirFrame_t out;

    uint8_t na = air_encode(a, AIR_TC, 0, 3, payload, sizeof(payload));
    uint8_t nb = air_encode(b, AIR_TC, AIR_FLAG_IS_RETX, 3, payload, sizeof(payload));

    TEST_ASSERT_EQUAL_UINT8(na, nb);
    TEST_ASSERT_NOT_EQUAL_UINT8(a[2], b[2]);                                    /* flags differ ... */
    TEST_ASSERT_EQUAL_HEX8_ARRAY(&a[na - AUTH_TAG_LEN], &b[nb - AUTH_TAG_LEN], AUTH_TAG_LEN);   /* ... tags do not */
    TEST_ASSERT_EQUAL_INT(0, air_decode(b, nb, &out));
    TEST_ASSERT_BITS_HIGH(AIR_FLAG_IS_RETX, out.flags);

    /* Setting IS_RETX on an already-encoded frame must also keep it valid. */
    a[2] |= AIR_FLAG_IS_RETX;
    TEST_ASSERT_EQUAL_INT(0, air_decode(a, na, &out));
}

/**
 * @brief The largest payload is 242 bytes, a 255-byte frame; 243 bytes is refused.
 * @details 255 = 5 (header) + 242 (payload) + 8 (tag) is the largest value
 * that fits the 1-byte length used for the whole frame. A 242-byte payload
 * must encode and decode. A 243-byte payload must make `air_encode()` return
 * 0 (error) instead of producing a frame that wraps around.
 */
void test_encode_accepts_242_rejects_243(void)
{
    uint8_t payload[243];
    uint8_t buf[AIR_FRAME_MAX + 1];
    AirFrame_t out;
    memset(payload, 0xA5, sizeof(payload));

    uint8_t n = air_encode(buf, AIR_DATA, 0, 1, payload, 242);
    TEST_ASSERT_EQUAL_UINT8(255, n);
    TEST_ASSERT_EQUAL_INT(0, air_decode(buf, n, &out));
    TEST_ASSERT_EQUAL_UINT8(242, out.len);

    TEST_ASSERT_EQUAL_UINT8(0, air_encode(buf, AIR_DATA, 0, 1, payload, 243));
}

/**
 * @brief An ACK frame is 16 bytes (5 header + 3 body + 8 tag) and carries the type and seq it acknowledges.
 * @details The body is [ref_type, ref_seq, 0]. This test passes, but it also
 * documents a bug elsewhere.
 * @bug KB-2 (test/README.md): src/subsystems/transceiver/transceiver.c:127
 * declares `uint8_t ack_buf[8]` and encodes an ACK into it, so every ACK
 * the satellite sends writes 8 bytes past the end of that stack buffer. The
 * transceiver can't be tested on the host yet, so this size check is the
 * evidence.
 */
void test_ack_frame_is_16_bytes(void)
{
    uint8_t buf[AIR_FRAME_MAX];
    AirFrame_t out;

    uint8_t n = air_encode_ack(buf, 9, AIR_TC, 4);

    TEST_ASSERT_EQUAL_UINT8(16, n);
    TEST_ASSERT_EQUAL_UINT8(AIR_FRAME_HDR + 3u + AUTH_TAG_LEN, n);
    TEST_ASSERT_EQUAL_INT(0, air_decode(buf, n, &out));
    TEST_ASSERT_EQUAL_UINT8(AIR_ACK, out.type);
    TEST_ASSERT_EQUAL_UINT8(AIR_TC, out.payload[0]);
    TEST_ASSERT_EQUAL_UINT8(4, out.payload[1]);
}

/**
 * @brief A NACK frame is 16 bytes, verifies, and carries the rejection reason in its third body byte.
 * @details The ground station reads that byte to learn why a TC was refused,
 * here `NACK_UNKNOWN_TC_ID`.
 */
void test_nack_frame_carries_reason(void)
{
    uint8_t buf[AIR_FRAME_MAX];
    AirFrame_t out;

    uint8_t n = air_encode_nack(buf, 9, AIR_TC, 4, NACK_UNKNOWN_TC_ID);

    TEST_ASSERT_EQUAL_UINT8(16, n);
    TEST_ASSERT_EQUAL_INT(0, air_decode(buf, n, &out));
    TEST_ASSERT_EQUAL_UINT8(AIR_NACK, out.type);
    TEST_ASSERT_EQUAL_UINT8(NACK_UNKNOWN_TC_ID, out.payload[2]);
}

/**
 * @brief A received frame whose LEN field is larger than the payload buffer is rejected before anything is copied.
 * @details
 * - Arrange: a 255-byte buffer with a valid-looking header and LEN = 250.
 *   5 + 250 = 255 fits the received buffer, but `AirFrame_t.payload` holds
 *   only 242 bytes.
 * - Expected: `air_decode()` returns -1.
 * - Actual: `decode_raw()` copies 250 bytes into the 242-byte array, a stack
 *   overflow that AddressSanitizer reports at frame.c:38. This happens
 *   **before** the tag is checked, so anyone transmitting on the frequency
 *   could trigger it.
 * @bug KB-1 (test/README.md). Delete the TEST_IGNORE_MESSAGE line once
 * `decode_raw()` checks `len <= AIR_PAYLOAD_MAX`.
 */
void test_decode_rejects_len_above_payload_max(void)
{
    TEST_IGNORE_MESSAGE("KNOWN BUG KB-1: decode_raw() has no len <= AIR_PAYLOAD_MAX check; stack overflow before auth");
    uint8_t buf[AIR_FRAME_MAX];
    AirFrame_t out;
    memset(buf, 0, sizeof(buf));
    buf[0] = AIR_PROTOCOL_VER;
    buf[1] = AIR_DATA;
    buf[2] = AIR_FLAG_AUTH_TAG;
    buf[3] = 1;
    buf[4] = 250;   /* 5 + 250 = 255 fits in the 255-byte buffer, but payload[] holds only 242 */

    TEST_ASSERT_EQUAL_INT(-1, air_decode(buf, sizeof(buf), &out));
}

/* =========================================================================
 * 3. Duplicate detection ring (dedup.c)
 * ========================================================================= */

/**
 * @brief A (type, seq) entry added to the ring is found again and returns its cached ACK.
 * @details When the ground resends a TC because our ACK was lost, the
 * satellite must resend the same ACK without running the TC again. The
 * test checks:
 * - an empty ring finds nothing;
 * - after `dedup_add(TC, 5, ack)`, `dedup_check(TC, 5)` finds the entry and
 *   returns the 3 ACK bytes;
 * - the same seq with another type, or the same type with another seq,
 *   isn't treated as a duplicate.
 */
void test_dedup_finds_added_entry(void)
{
    DedupRing_t r;
    const uint8_t ack[3] = { 0xA1, 0xA2, 0xA3 };
    const uint8_t *got = NULL;
    uint8_t len = 0;

    dedup_init(&r);
    TEST_ASSERT_FALSE(dedup_check(&r, AIR_TC, 5, &got, &len));

    dedup_add(&r, AIR_TC, 5, ack, sizeof(ack));

    TEST_ASSERT_TRUE(dedup_check(&r, AIR_TC, 5, &got, &len));
    TEST_ASSERT_EQUAL_UINT8(3, len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ack, got, 3);
    TEST_ASSERT_FALSE(dedup_check(&r, AIR_TC, 6, NULL, NULL));      /* other seq */
    TEST_ASSERT_FALSE(dedup_check(&r, AIR_TM, 5, NULL, NULL));      /* other type */
}

/**
 * @brief The ring keeps the last 16 entries; adding a 17th evicts the oldest.
 * @details After adding seq 0 to 16 (17 entries), the ring holds 16 entries,
 * seq 0 is gone, and seq 1 to 16 are all still found.
 */
void test_dedup_evicts_oldest_after_16(void)
{
    DedupRing_t r;
    const uint8_t ack[1] = { 0 };
    dedup_init(&r);

    for (uint8_t seq = 0; seq < 17; seq++) {
        dedup_add(&r, AIR_TC, seq, ack, sizeof(ack));
    }

    TEST_ASSERT_EQUAL_UINT8(DEDUP_SIZE, r.n);
    TEST_ASSERT_FALSE_MESSAGE(dedup_check(&r, AIR_TC, 0, NULL, NULL), "oldest entry should have been evicted");
    for (uint8_t seq = 1; seq < 17; seq++) {
        TEST_ASSERT_TRUE(dedup_check(&r, AIR_TC, seq, NULL, NULL));
    }
}

/**
 * @brief A cached ACK longer than `DEDUP_ACK_MAX` (16 bytes) is cut to 16 bytes instead of overflowing the slot.
 * @details Adds a 24-byte "ACK" and expects to read back 16 bytes, equal to
 * the first 16 bytes added.
 */
void test_dedup_truncates_ack_to_16_bytes(void)
{
    DedupRing_t r;
    uint8_t ack[24];
    const uint8_t *got = NULL;
    uint8_t len = 0;
    for (uint8_t i = 0; i < sizeof(ack); i++) ack[i] = i;

    dedup_init(&r);
    dedup_add(&r, AIR_TC, 1, ack, sizeof(ack));

    TEST_ASSERT_TRUE(dedup_check(&r, AIR_TC, 1, &got, &len));
    TEST_ASSERT_EQUAL_UINT8(DEDUP_ACK_MAX, len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ack, got, DEDUP_ACK_MAX);
}

/**
 * @brief Test runner. Lists every test of this suite, in execution order.
 * @details A test function that isn't listed here never runs. See
 * test/support/pocat_test_main.h for the `-l` / `-f` / `-n` / `-x` options.
 */
int main(int argc, char **argv)
{
    if (pocat_parse_args(argc, argv) != 0) return 1;
    POCAT_BEGIN();
    POCAT_RUN_TEST(test_sha256_abc_known_answer);
    POCAT_RUN_TEST(test_hmac_rfc4231_case2_first8);
    POCAT_RUN_TEST(test_hmac_two_part_input_equals_single_part);
    POCAT_RUN_TEST(test_psk_is_test_key);
    POCAT_RUN_TEST(test_encode_decode_roundtrip);
    POCAT_RUN_TEST(test_encode_sets_auth_flag_and_total_length);
    POCAT_RUN_TEST(test_decode_rejects_tampered_payload);
    POCAT_RUN_TEST(test_decode_rejects_tampered_header);
    POCAT_RUN_TEST(test_decode_rejects_wrong_version);
    POCAT_RUN_TEST(test_decode_rejects_missing_auth_flag);
    POCAT_RUN_TEST(test_decode_rejects_truncated_frame);
    POCAT_RUN_TEST(test_retx_flag_does_not_change_tag);
    POCAT_RUN_TEST(test_encode_accepts_242_rejects_243);
    POCAT_RUN_TEST(test_ack_frame_is_16_bytes);
    POCAT_RUN_TEST(test_nack_frame_carries_reason);
    POCAT_RUN_TEST(test_decode_rejects_len_above_payload_max);
    POCAT_RUN_TEST(test_dedup_finds_added_entry);
    POCAT_RUN_TEST(test_dedup_evicts_oldest_after_16);
    POCAT_RUN_TEST(test_dedup_truncates_ack_to_16_bytes);
    return POCAT_END();
}
