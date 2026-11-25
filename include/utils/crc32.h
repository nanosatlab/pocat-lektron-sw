/**
 * @file crc32.h
 * @brief CRC-32/ISO-HDLC (polynomial 0x04C11DB7 reflected, the zlib and Ethernet CRC).
 * @details
 * Shared by the ARQ transfers and the parameter store. Start from CRC32_INIT,
 * feed the data with crc32_update() in one or more calls, then apply
 * crc32_finish().
 * @date 2026-10-27
 * @note Moved out of arq.c.
 */

#ifndef INC_CRC32_H_
#define INC_CRC32_H_

#include <stdint.h>

#define CRC32_INIT 0xFFFFFFFFu   /**< Initial value for crc32_update(). */

/**
 * @brief Adds bytes to a running CRC.
 * @param crc CRC32_INIT for the first call, then the value returned by the previous call.
 * @param data Bytes to add.
 * @param len Number of bytes.
 * @return The running CRC, before crc32_finish().
 */
uint32_t crc32_update(uint32_t crc, const uint8_t *data, uint32_t len);

/**
 * @brief Turns a running CRC into the final CRC-32.
 * @param crc Value returned by the last crc32_update().
 * @return The CRC-32 (for "123456789": 0xCBF43926).
 */
uint32_t crc32_finish(uint32_t crc);

#endif /* INC_CRC32_H_ */
