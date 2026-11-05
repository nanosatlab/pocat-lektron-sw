/**
 * @file crc32.c
 * @brief CRC-32/ISO-HDLC, bit by bit (no table, so no flash or RAM cost).
 * @date 2026-10-27
 * @note Moved out of arq.c.
 */

#include "crc32.h"

uint32_t crc32_update(uint32_t crc, const uint8_t *data, uint32_t len)
{
    for (uint32_t i = 0u; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1u) {
                crc = (crc >> 1) ^ 0xEDB88320u;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

uint32_t crc32_finish(uint32_t crc) { return crc ^ 0xFFFFFFFFu; }
