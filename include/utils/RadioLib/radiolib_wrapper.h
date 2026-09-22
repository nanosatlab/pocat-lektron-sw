/**
 * @file radiolib_wrapper.h
 * @author Jan Pruneda Marcet
 * @brief C wrapper interface for RadioLib
 * @date 2026-01-29
 * 
 * @details This header provides a C-compatible interface to the RadioLib C++ library,
 * allowing the radio functionality to be used from C code.
 */

#ifndef INC_WRAPPER_H
#define INC_WRAPPER_H

#include <stdint.h>
#include "TypeDef.h"

#ifndef __cplusplus
/* SX126x IRQ flag constants for C users */
#define RADIOLIB_SX126X_IRQ_TX_DONE             (0x0001)
#define RADIOLIB_SX126X_IRQ_RX_DONE             (0x0002)
#define RADIOLIB_SX126X_IRQ_PREAMBLE_DETECTED   (0x0004)
#define RADIOLIB_SX126X_IRQ_SYNC_WORD_VALID     (0x0008)
#define RADIOLIB_SX126X_IRQ_HEADER_VALID        (0x0010)
#define RADIOLIB_SX126X_IRQ_HEADER_ERR          (0x0020)
#define RADIOLIB_SX126X_IRQ_CRC_ERR             (0x0040)
#define RADIOLIB_SX126X_IRQ_CAD_DONE            (0x0080)
#define RADIOLIB_SX126X_IRQ_CAD_DETECTED        (0x0100)
#define RADIOLIB_SX126X_IRQ_TIMEOUT             (0x0200)
#define RADIOLIB_SX126X_IRQ_ALL                 (0x43FF)
#define RADIOLIB_SX126X_IRQ_NONE                (0x0000)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise the radio hardware.
 * @returns 0 on success, negative RadioLib error code on failure.
 */
int16_t RadioLib_Init(void);

/**
 * @brief Set the frequency.
 * @param freq Operating frequency in Hz.
 */
void RadioLib_SetChannel(uint32_t freq);

/**
 * @brief Configure TX parameters.
 * @note RadioLib CR is different from SX1262Config: we pass 1–4 and add 4 internally (RadioLib uses 5–8).
 * @param sf         Spreading factor; controls sensitivity and range.
 * @param cr         Coding rate; controls error correction.
 * @param power      Transmission power (dBm).
 * @param bw_code    Bandwidth; controls channel bandwidth (0=125kHz, 1=250kHz, 2=500kHz).
 * @param iqInverted IQ signal inversion (to avoid collisions).
 * @param crcOn      Enable CRC (checksum for errors).
 * @param preambleLen Preamble length (synchronisation sequence).
 */
void RadioLib_SetTxConfig(
    uint8_t sf,
    uint8_t cr,
    int8_t power,
    uint8_t bw_code,
    int iqInverted,
    int crcOn,
    uint16_t preambleLen);

/** @brief Configure RX parameters
 * @param sf         Spreading factor; controls sensitivity and range.
 * @param cr         Coding rate; controls error correction.
 * @param bw_code    Bandwidth; controls channel bandwidth (0=125kHz, 1=250kHz, 2=500kHz).
 * @param iqInverted IQ signal inversion (to avoid collisions).
 * @param crcOn      Enable CRC (checksum for errors).
 * @param preambleLen Preamble length (synchronisation sequence).
 */
void RadioLib_SetRxConfig(
    uint8_t sf,
    uint8_t cr,
    uint8_t bw_code,
    int iqInverted,
    int crcOn,
    uint16_t preambleLen);

/**
 * @brief Send data (blocking).
 * @param buf Pointer to the data to transmit.
 * @param len Length in bytes.
 * @return 0 on success, negative RadioLib error code on failure.
 */
int16_t RadioLib_Transmit(uint8_t *buf, uint16_t len);

/**
 * @brief Receive a packet (blocking).
 * @param timeoutMs  Timeout in milliseconds; 0 = wait forever.
 * @param outBuf     Buffer to write received data into.
 * @param bufSize    Size of outBuf.
 * @param outLen     [out] Actual number of bytes received (may be NULL).
 * @param outRssi    [out] RSSI in dBm (may be NULL).
 * @param outSnr     [out] SNR in dB (may be NULL).
 * @return 0 on success, negative RadioLib error code on failure.
 */
int16_t RadioLib_Receive(uint32_t timeoutMs,
                    uint8_t *outBuf, uint16_t bufSize,
                    uint16_t *outLen, int16_t *outRssi, int8_t *outSnr);

/** @brief Enter sleep mode */
int16_t RadioLib_Sleep(void);

/** @brief Enter standby mode */
int16_t RadioLib_Standby(void);

/**
 * @brief Perform blocking channel-activity detection.
 * @return RADIOLIB_LORA_DETECTED if activity found,
 *         RADIOLIB_CHANNEL_FREE  if channel is clear,
 *         or negative error code on failure.
 */
int16_t RadioLib_ScanChannel(void);

/**
 * @brief Continuously scan for LoRa activity (CAD) and receive when detected.
 *
 * Back-to-back CAD scanning loop for up to @p cadTimeoutMs.
 * Uses the SX1262's CAD_GOTO_RX exit mode so that the radio transitions from
 * CAD to RX in hardware with zero software gap, avoiding the timing issues
 * that caused the previous single-shot CAD approach to miss most packets.
 *
 * When LoRa activity is detected the radio automatically enters RX mode and
 * waits up to @p rxTimeoutMs for the full packet.  If the RX phase times out
 * (possible false CAD detection), scanning resumes until @p cadTimeoutMs
 * expires.
 *
 * @param cadTimeoutMs Overall time budget (ms) for the CAD scanning loop.
 * @param rxTimeoutMs  Maximum time (ms) to wait for a packet after CAD detects activity.
 * @param outBuf       Buffer to write received data into.
 * @param bufSize      Size of outBuf.
 * @param outLen       [out] Actual number of bytes received (may be NULL).
 * @param outRssi      [out] RSSI in dBm (may be NULL).
 * @param outSnr       [out] SNR in dB (may be NULL).
 * @return 0 on success,
 *         RADIOLIB_CHANNEL_FREE if no activity detected within cadTimeoutMs,
 *         or negative RadioLib error code on failure.
 */
int16_t RadioLib_CadReceive(uint32_t cadTimeoutMs, uint32_t rxTimeoutMs,
                       uint8_t *outBuf, uint16_t bufSize,
                       uint16_t *outLen, int16_t *outRssi, int8_t *outSnr);

/**
 * @brief Listen for packets using SX1262 hardware RX Duty Cycle mode.
 *
 * The radio autonomously alternates between sleep and RX. The MCU blocks
 * on a semaphore until DIO1 fires (RX_DONE) or listenMs expires.
 *
 * @param listenMs      Overall listen window (ms). MCU semaphore timeout.
 * @param preambleLen   Sender preamble length (symbols). Used to compute duty cycle timing.
 * @param outBuf        Buffer for received data.
 * @param bufSize       Size of outBuf.
 * @param outLen        [out] Bytes received (may be NULL).
 * @param outRssi       [out] RSSI in dBm (may be NULL).
 * @param outSnr        [out] SNR in dB (may be NULL).
 * @return 0 on success, RADIOLIB_ERR_RX_TIMEOUT if no packet, or negative error code.
 */
int16_t RadioLib_DutyCycleReceive(uint32_t listenMs, uint16_t preambleLen,
                                  uint8_t *outBuf, uint16_t bufSize,
                                  uint16_t *outLen, int16_t *outRssi, int8_t *outSnr);

/** @brief Process radio interrupts.
 * @note No-op function. RadioLib already handles it but it kept so that call sites need no changes. */
void RadioLib_IrqProcess(void);

/**
 * @brief Register task to receive DIO1 interrupt notifications.
 * @param handle Task handle for xTaskNotifyFromISR on DIO1 events.
 * @note Must be called before StartReceive or StartTransmit.
 */
void RadioLib_SetIrqTask(void *handle);

/**
 * @brief Start async duty-cycle RX (non-blocking, returns immediately).
 * @param preambleLen Preamble length in symbols; DIO1 fires on RX_DONE.
 * @return 0 on success, negative RadioLib error code on failure.
 */
int16_t RadioLib_StartReceive(uint16_t preambleLen);

/**
 * @brief Start async TX (non-blocking, returns immediately).
 * @param buf Pointer to packet data to transmit.
 * @param len Length in bytes.
 * @return 0 on success, negative RadioLib error code on failure.
 * @note DIO1 fires on TX_DONE. Caller must wait for notification before StartTransmit again.
 */
int16_t RadioLib_StartTransmit(uint8_t *buf, uint16_t len);

/**
 * @brief Read the SX1262 IRQ status register.
 * @return IRQ flags (RADIOLIB_SX126X_IRQ_*).
 * @note Call this after DIO1 fires to determine what happened (RX_DONE, TX_DONE, etc).
 */
uint32_t RadioLib_GetIrqFlags(void);

/**
 * @brief Clear IRQ flags in the SX1262.
 * @param mask IRQ flags to clear (e.g., RADIOLIB_SX126X_IRQ_ALL).
 */
void RadioLib_ClearIrqFlags(uint32_t mask);

/**
 * @brief Read received packet data after RX_DONE IRQ.
 * @param outBuf     Buffer to write received data into.
 * @param bufSize    Size of outBuf.
 * @param outLen     [out] Actual number of bytes received (may be NULL).
 * @param outRssi    [out] RSSI in dBm (may be NULL).
 * @param outSnr     [out] SNR in dB (may be NULL).
 * @return 0 on success, negative RadioLib error code on failure.
 * @note Call this only after RX_DONE IRQ has fired.
 */
int16_t RadioLib_ReadRxData(uint8_t *outBuf, uint16_t bufSize,
                            uint16_t *outLen, int16_t *outRssi, int8_t *outSnr);

#ifdef __cplusplus
}
#endif

#endif /* INC_WRAPPER_H */
