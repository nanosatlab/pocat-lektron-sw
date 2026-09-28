#ifdef RADIO_MOCK

/**
 * @file radiolib_wrapper_mock.c
 * @brief Mock implementation of the RadioLib C wrapper.
 *
 * Enabled when RADIO_MOCK is defined (CMake option: -DRADIO_MOCK=ON).
 * Prints radio configuration and outgoing packets to the debug console.
 * RX always times out immediately — no packet is ever returned.
 */

#include "radiolib_wrapper.h"
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"

static RadioEvents_t *radio_events = NULL;

static const char *bw_str(uint8_t bw_code)
{
    switch (bw_code) {
        case 0: return "125 kHz";
        case 1: return "250 kHz";
        case 2: return "500 kHz";
        default: return "?";
    }
}

void RadioLib_Init(RadioEvents_t *events)
{
    radio_events = events;
    printf("[RADIO MOCK] Initialized\r\n");
}

void RadioLib_SetChannel(uint32_t freq_hz)
{
    printf("[RADIO MOCK] Channel: %lu Hz (%.3f MHz)\r\n",
           (unsigned long)freq_hz, (double)freq_hz / 1000000.0);
}

void RadioLib_SetTxConfig(uint8_t sf, uint8_t cr, int8_t power,
                          uint8_t bw_code, int iqInverted,
                          int crcOn, uint16_t preambleLen)
{
    printf("[RADIO MOCK] TX config: SF=%u CR=4/%u Power=%d dBm BW=%s IQ=%d CRC=%d Preamble=%u\r\n",
           sf, cr + 4, (int)power, bw_str(bw_code), iqInverted, crcOn, preambleLen);
}

void RadioLib_SetRxConfig(uint8_t sf, uint8_t cr, uint8_t bw_code,
                          int iqInverted, int crcOn, uint16_t preambleLen)
{
    printf("[RADIO MOCK] RX config: SF=%u CR=4/%u BW=%s IQ=%d CRC=%d Preamble=%u\r\n",
           sf, cr + 4, bw_str(bw_code), iqInverted, crcOn, preambleLen);
}

void RadioLib_Send(uint8_t *buf, uint16_t len)
{
    printf("[RADIO MOCK] TX %u byte(s):", len);
    for (uint16_t i = 0; i < len; i++) {
        printf(" %02X", buf[i]);
    }
    printf("\r\n");

    if (radio_events && radio_events->TxDone) {
        radio_events->TxDone();
    }
}

int16_t RadioLib_Rx(uint32_t timeoutMs)
{
    (void)timeoutMs;
    if (radio_events && radio_events->RxTimeout) {
        radio_events->RxTimeout();
    }
    return -1; /* always timeout — mock provides no incoming packets */
}

int16_t RadioLib_Sleep(void)
{
    printf("[RADIO MOCK] Sleep\r\n");
    return 0;
}

int16_t RadioLib_Standby(void)
{
    printf("[RADIO MOCK] Standby\r\n");
    return 0;
}

int16_t RadioLib_StartCad(void)
{
    printf("[RADIO MOCK] CAD: channel free\r\n");
    if (radio_events && radio_events->CadDone) {
        radio_events->CadDone(0);
    }
    return 0;
}

int16_t RadioLib_DutyCycleReceive(uint32_t listenMs, uint16_t preambleLen,
                                  uint8_t *outBuf, uint16_t bufSize,
                                  uint16_t *outLen, int16_t *outRssi, int8_t *outSnr)
{
    printf("[RADIO MOCK] DutyCycleReceive: listen=%lums preamble=%u (timeout)\r\n",
           (unsigned long)listenMs, preambleLen);
    vTaskDelay(pdMS_TO_TICKS(listenMs));
    return -1;
}

void RadioLib_IrqProcess(void)
{
    /* no-op */
}

#endif /* RADIO_MOCK */
