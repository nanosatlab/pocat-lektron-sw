#pragma once
/**
 * @file auth.h
 * @brief TT&C v2 frame authentication — HMAC-SHA256/8 (§4.7).
 *
 * Every air-layer frame carries an 8-byte tag:
 *   AUTH_TAG = HMAC-SHA256(COMMS_PSK, [VER|TYPE|FLAGS|SEQ|LEN|PAYLOAD])[0:8]
 *
 * FLAGS is hashed with AUTH_TAG_PRESENT (bit 7) already set.  The PSK is
 * supplied by the build-generated psk.h; the build system reads POCAT_PSK
 * (32 hex chars) from the host environment and generates the file.
 *
 * STM32L476 does not include the HASH peripheral (HAL_HASH), so the
 * implementation uses a compact software SHA-256 (FIPS 180-4).
 */

#include <stdint.h>
#include <stdbool.h>

/** Length of the pre-shared key in bytes (128 bits). */
#define AUTH_PSK_LEN   16u

/** Truncated tag length appended after PAYLOAD (§4.7). */
#define AUTH_TAG_LEN   8u

/**
 * @brief Compute the 8-byte HMAC-SHA256/8 auth tag.
 *
 * @param header    Points to the 5-byte frame header [VER|TYPE|FLAGS|SEQ|LEN]
 *                  with AUTH_TAG_PRESENT already set in FLAGS.
 * @param payload   Payload bytes (LEN bytes).
 * @param payload_len  Number of payload bytes.
 * @param out_tag   Output buffer — must be at least AUTH_TAG_LEN bytes.
 */
void auth_tag(const uint8_t *header, const uint8_t *payload,
              uint8_t payload_len, uint8_t out_tag[AUTH_TAG_LEN]);

/**
 * @brief Verify an 8-byte auth tag with a constant-time comparison.
 *
 * @param header      5-byte frame header (FLAGS has AUTH_TAG_PRESENT set).
 * @param payload     Payload bytes.
 * @param payload_len Payload length.
 * @param tag         The 8-byte tag received in the frame.
 * @return true  if the tag matches; false otherwise (caller silently drops).
 */
bool auth_verify(const uint8_t *header, const uint8_t *payload,
                 uint8_t payload_len, const uint8_t tag[AUTH_TAG_LEN]);
