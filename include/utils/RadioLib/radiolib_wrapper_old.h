#if 0
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

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Callback struct for radio events. Assign your C functions before calling RadioLib_Init(). */
typedef struct {
    /** @brief Transmission finished */
    void (*TxDone)(void);
    /**
     * @brief Reception finished.
     * @param payload Received data buffer.
     * @param size Length in bytes.
     * @param rssi Received signal strength (dBm).
     * @param snr  Signal-to-noise ratio (dB).
     */
    void (*RxDone)(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr);
    /** @brief Transmission timeout */
    void (*TxTimeout)(void);
    /** @brief Reception timeout */
    void (*RxTimeout)(void);
    /** @brief Reception error (e.g. CRC) */
    void (*RxError)(void);
    /**
     * @brief Channel Activity Detection (CAD) finished.
     * @param channelActivityDetected 0 = channel free (safe to transmit), non-zero = LoRa activity detected (channel busy).
     */
    void (*CadDone)(int channelActivityDetected);
} RadioEvents_t;

/**
 * @brief Initialise the radio and assign the callbacks.
 * @param events Pointer to RadioEvents_t with your callback functions (can be NULL for unused).
 */
void RadioLib_Init(RadioEvents_t *events);

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
 * @brief Send the data.
 * @param buf Pointer to the data to transmit.
 * @param len Length in bytes.
 */
void RadioLib_Send(uint8_t *buf, uint16_t len);

/**
 * @brief Enter receive mode and wait for a packet.
 * @param timeoutMs Timeout in milliseconds; 0 = wait forever.
 * @return RadioLib error code (e.g. RADIOLIB_ERR_NONE on success).
 */
int16_t RadioLib_Rx(uint32_t timeoutMs);

/** @brief Enter sleep mode */
int16_t RadioLib_Sleep(void);

/** @brief Enter standby mode */
int16_t RadioLib_Standby(void);

/** @brief Start channel detection. 
  * @todo Do we really need this function?
*/
int16_t RadioLib_StartCad(void);

/** @brief Process radio interrupts. 
 * @note No-op function. RadioLib already handles it but it kept so that call sites need no changes. */
void RadioLib_IrqProcess(void);

#ifdef __cplusplus
}
#endif

#endif /* INC_WRAPPER_H */
#endif