/*
--- POCKET+ compression ---
Author: FISCHER BENJAMIN
Contact: benjamin.fischer@esa.int, ben.fischer@ymail.com
*/

#ifndef pocket_header
#define pocket_header

#include <stdlib.h> //malloc, calloc...
#include <stdint.h> //datatypes (like uint32_t)
#include <string.h> //memcpy and string operations

#define P_P_PROTECTION_LEVEL        1
#define P_P_MAX_PACKET_SIZE         2048
#define P_P_WORDS_MAX_PACKET_SIZE   (P_P_MAX_PACKET_SIZE/sizeof(uint32_t))

#define safeStartPocket(p, size, period) startPocket(p, size, period, 1, 0 ,0)

/**
 * Initializes Pocket+ compression motor with a reference packet and the amount of data to be written
 *
 * @param[in/out]  referencePacket    Pointer to the reference packet as uint32_t popinter
 * @param[in]  referencePacketSize    Size in words of the reference packet
 * @param[in]  positiveUpdateRate     Packet periodicity
 * @param[in]  maximumProtectionLevel Compression protection level when using lossless pocket+
 * @param[in]  anchorUpdateFrequency  Use of anchor deltas when compression
 * @param[in]  npOffset               Non-predictable bits offset
 * @return                            referencePacketSize when OK. 0 on error
 */
uint_fast16_t startPocket(uint32_t *referencePacket, uint_fast16_t referencePacketSize, uint_fast16_t positiveUpdateRate,
                          uint_fast8_t maximumProtectionLevel, uint_fast8_t anchorUpdateFrequency, int_fast16_t npOffset);

/**
 * Compresses a raw packet in uint32_t pointer format to a byte array. The output array appends the compressed chunks,
 * compressPacket shall be called as many times as configured in positiveUpdateRate from startPocket initialization call.
 * @param  compressedPacket  Pointer to the compressed packet where the compression is appended
 * @param  uncompressedPaket Pointer to the raw data packet to be compressed and appended to compressedPacket
 * @return                   Total size of the compressed packet, including the previoisly compressed data.
 *                           -1 in case of error
 */
int_fast16_t compressPacket(uint8_t * compressedPacket, uint32_t * uncompressedPaket);

/**
 * Stops pocket plus current compression
 */
void stopPocket(void);

#endif /* pocket_header */
