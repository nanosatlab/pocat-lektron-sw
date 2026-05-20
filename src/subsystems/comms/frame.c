#include "frame.h"
#include "auth.h"
#include <string.h>

/* AIR_FRAME_MAX_AUTH: maximum total LoRa frame size when AUTH_TAG is present.
 * LEN field counts payload only; tag is not counted toward LEN (§4.7). */
#define AIR_PAYLOAD_MAX_AUTH  242u   /* 255 - 5 (hdr) - 8 (tag) */

/* ---- Raw helpers (header+payload, no auth tag) ---- */

static uint8_t encode_raw(uint8_t *buf, uint8_t type, uint8_t flags,
                           uint8_t seq, const uint8_t *payload, uint8_t len)
{
    buf[0] = AIR_PROTOCOL_VER;
    buf[1] = type;
    buf[2] = flags;
    buf[3] = seq;
    buf[4] = len;
    if (len > 0u && payload != NULL) {
        memcpy(&buf[5], payload, len);
    }
    return (uint8_t)(AIR_FRAME_HDR + len);
}

static int decode_raw(const uint8_t *buf, uint8_t buf_len, AirFrame_t *out)
{
    if (buf_len < AIR_FRAME_HDR) { return -1; }
    if (buf[0] != AIR_PROTOCOL_VER)   { return -1; }
    uint8_t len = buf[4];
    if ((uint8_t)(AIR_FRAME_HDR + len) > buf_len) { return -1; }
    out->ver   = buf[0];
    out->type  = buf[1];
    out->flags = buf[2];
    out->seq   = buf[3];
    out->len   = len;
    if (len > 0u) {
        memcpy(out->payload, &buf[5], len);
    }
    return 0;
}

/* ---- Public API ---- */

/**
 * Encode an authenticated air-frame (§4.7).
 * Sets AUTH_TAG_PRESENT in FLAGS and appends 8-byte HMAC-SHA256/8 tag.
 * Returns total byte count (5 + len + 8), or 0 on error.
 */
uint8_t air_encode(uint8_t *buf, uint8_t type, uint8_t flags,
                   uint8_t seq, const uint8_t *payload, uint8_t len)
{
    if (len > AIR_PAYLOAD_MAX_AUTH) {
        return 0;
    }
    flags |= AIR_FLAG_AUTH_TAG;
    uint8_t hdr_len = encode_raw(buf, type, flags, seq, payload, len);
    /* hdr_len = 5 + len; auth_tag hashes header (buf[0..4]) then payload. */
    auth_tag(buf, payload, len, &buf[hdr_len]);
    return (uint8_t)(hdr_len + AUTH_TAG_LEN);
}

/**
 * Decode and authenticate an air-frame (§4.7).
 * Returns 0 on success; -1 if the frame is too short, has wrong VER, is
 * missing AUTH_TAG_PRESENT, or the HMAC-SHA256/8 tag does not match.
 * Caller silently drops the frame on -1 (no NACK emitted, §4.7).
 */
int air_decode(const uint8_t *buf, uint8_t buf_len, AirFrame_t *out)
{
    if (decode_raw(buf, buf_len, out) != 0) {
        return -1;
    }
    /* Require AUTH_TAG_PRESENT (§4.7): OBC drops all unauthenticated frames. */
    if ((out->flags & AIR_FLAG_AUTH_TAG) == 0u) {
        return -1;
    }
    uint8_t tag_off = (uint8_t)(AIR_FRAME_HDR + out->len);
    if ((uint8_t)(tag_off + AUTH_TAG_LEN) > buf_len) {
        return -1;
    }
    if (!auth_verify(buf, out->payload, out->len, &buf[tag_off])) {
        return -1;
    }
    return 0;
}

uint8_t air_encode_ack(uint8_t *buf, uint8_t own_seq,
                       uint8_t ref_type, uint8_t ref_seq)
{
    uint8_t body[3] = { ref_type, ref_seq, 0x00 };
    return air_encode(buf, AIR_ACK, 0x00, own_seq, body, 3);
}

uint8_t air_encode_nack(uint8_t *buf, uint8_t own_seq,
                        uint8_t ref_type, uint8_t ref_seq, uint8_t reason)
{
    uint8_t body[3] = { ref_type, ref_seq, reason };
    return air_encode(buf, AIR_NACK, 0x00, own_seq, body, 3);
}
