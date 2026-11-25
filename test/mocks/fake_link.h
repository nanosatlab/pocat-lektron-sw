/**
 * @file fake_link.h
 * @brief Scripted fake of the SX1262 radio link (asynchronous TX / RX / IRQ / CAD API) with a programmable "ground station".
 * @date 2026-10-29
 *
 * @details
 * On the satellite, `saw.c` and `arq.c` transmit with `RadioLib_StartTransmit()`,
 * wait for the DIO1 interrupt (a task notification with
 * `N_TRANSCEIVER_RADIO_IRQ_BIT`), read the IRQ flags, and receive with
 * `RadioLib_StartReceive()` / `RadioLib_ReadRxData()`. This fake replaces the
 * SX1262 and the DIO1 line, and plays the ground station:
 *
 * - **Responder**: a callback that receives the last transmitted frame and may
 *   fill in a reply (for example an ACK built with `air_encode_ack()`).
 * - **Injected frames**: unsolicited frames delivered before the responder.
 * - **CAD**: `RadioLib_ScanChannel()` returns a settable result.
 * - **TX_DONE**: can be disabled to test the 5 s TX timeout.
 *
 * Only these functions are implemented here: `RadioLib_StartTransmit`,
 * `RadioLib_StartReceive`, `RadioLib_GetIrqFlags`, `RadioLib_ClearIrqFlags`,
 * `RadioLib_ReadRxData`, `RadioLib_ScanChannel`. `Standby`, `SetChannel`,
 * `SetTxConfig`, `SetRxConfig` stay in fake_comms_env.c, so always link both.
 *
 * Everything is deterministic and single-threaded. "Notify" means
 * `xTaskNotify(xTaskGetCurrentTaskHandle(), N_TRANSCEIVER_RADIO_IRQ_BIT, eSetBits)`:
 * keep the task that runs `saw_send()` / `arq_*()` as the current task.
 *
 * **Behaviour**
 * - `StartTransmit`: logs a copy of the frame; if TX_DONE is enabled, sets the
 *   TX_DONE flag and notifies (inside the call, because production clears the
 *   notification state just before it).
 * - `StartReceive`: pops one injected frame; otherwise, if a responder is set
 *   and there is a new TX not yet answered, calls it once for that TX. If a
 *   frame is loaded, sets RX_DONE and notifies. At most one frame per call; each
 *   TX is answered at most once.
 * - `ReadRxData`: copies the loaded frame out (RSSI -80, SNR 5) and clears it.
 *
 * @code
 * static bool ack_responder(const uint8_t *tx, uint8_t tx_len, uint8_t *reply, uint8_t *reply_len)
 * {
 *     AirFrame_t f;
 *     if (air_decode(tx, tx_len, &f) != 0) return false;
 *     *reply_len = air_encode_ack(reply, 0x90, f.type, f.seq);
 *     return true;
 * }
 * // in setUp():
 * fake_link_reset();
 * fake_link_set_responder(ack_responder);
 * @endcode
 */
#ifndef FAKE_LINK_H
#define FAKE_LINK_H
#include <stdbool.h>
#include <stdint.h>

#define FAKE_LINK_MAX_TX   64   /**< Transmitted frames kept in the log. */
#define FAKE_LINK_MAX_INJ   4   /**< Injected (unsolicited) RX frames kept. */

/**
 * @brief Fake ground station: called with the last transmitted frame; fill @p reply and return true to answer.
 * @param tx        Last transmitted frame.
 * @param tx_len    Its length.
 * @param reply     Buffer (255 bytes) for the reply frame.
 * @param reply_len Output: reply length.
 * @return true to deliver @p reply, false for silence.
 */
typedef bool (*fake_link_responder_t)(const uint8_t *tx, uint8_t tx_len, uint8_t *reply, uint8_t *reply_len);

/** @brief Resets the link: no responder, scan result CHANNEL_FREE, TX_DONE enabled, logs cleared, IRQ flags 0. Call it in `setUp()`. */
void    fake_link_reset(void);
/** @brief Installs the ground-station responder (NULL = silence). */
void    fake_link_set_responder(fake_link_responder_t fn);
/** @brief Sets the CAD result: `RADIOLIB_CHANNEL_FREE` (-15) or `RADIOLIB_LORA_DETECTED` (-702). */
void    fake_link_set_scan_result(int16_t r);
/** @brief Chooses whether TX_DONE fires after a transmit (false makes the TX wait time out). */
void    fake_link_set_tx_done(bool fires);
/** @brief Queues an unsolicited frame; delivered by a later `StartReceive` before the responder is asked. */
void    fake_link_inject_rx(const uint8_t *frame, uint8_t len);
/** @brief Returns the number of frames transmitted since reset. */
int     fake_link_tx_count(void);
/**
 * @brief Copies transmitted frame @p i (0 = first) into @p out (255 bytes) and returns its length. A bad @p i fails the test.
 */
uint8_t fake_link_tx_frame(int i, uint8_t *out);
/** @brief Returns the number of `RadioLib_ScanChannel()` calls. */
int     fake_link_scan_count(void);
/** @brief Returns the number of `RadioLib_StartReceive()` calls. */
int     fake_link_start_receive_count(void);

#endif
