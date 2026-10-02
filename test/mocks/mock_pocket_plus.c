#include "compression.h"
#include <string.h>

uint_fast16_t startPocket(uint32_t *referencePacket, uint_fast16_t referencePacketSize, uint_fast16_t positiveUpdateRate,
                          uint_fast8_t maximumProtectionLevel, uint_fast8_t anchorUpdateFrequency, int_fast16_t npOffset) {
    (void)referencePacket;
    (void)positiveUpdateRate;
    (void)maximumProtectionLevel;
    (void)anchorUpdateFrequency;
    (void)npOffset;
    return referencePacketSize;
}

int_fast16_t compressPacket(uint8_t * compressedPacket, uint32_t * uncompressedPaket) {
    memcpy(compressedPacket, uncompressedPaket, 20);
    return 20; 
}

void stopPocket(void) {
}

