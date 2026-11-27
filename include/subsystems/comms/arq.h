/**
 * @file arq.h
 * @brief TT&C v2 ARQ session engine (§7.3).
 *
 * Implements Selective-Repeat ARQ with bitmap for both directions:
 *   DL (OBC → GS): satellite sends DATA frames from RAM (the HK history copy
 *       of hk_history.h, or the OBC log test pattern), so nothing is read
 *       from flash while a session runs; driven from transceiver_task
 *       context when N_TRANSCEIVER_ARQ_DL_BIT fires.
 *   UL (GS → OBC): satellite receives DATA frames and accumulates them;
 *       driven from comms_task context via arq_rx().
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "frame.h"
#include "types.h"
#include "FreeRTOS.h"
#include "queue.h"

/* ---- tunables ---- */
#define ARQ_BLOCK_SIZE    200u   /* bytes per DATA block payload (§6.8) */
#define ARQ_WINDOW_SIZE    16u   /* max outstanding blocks per window */
#define ARQ_UL_BUF_MAX    512u   /* max UL reassembly buffer */
#define ARQ_MAX_RETX        3u   /* window retransmit attempts before abort */

/** What a request to begin a DL session got. */
typedef enum {
    ARQ_DL_STARTED = 0,      /**< The session starts: the transceiver task sends it. */
    ARQ_DL_SESSION_ACTIVE,   /**< Another session is active or armed: nothing changed. */
    ARQ_DL_NO_DATA,          /**< Nothing is stored to send. */
    ARQ_DL_STORE_BUSY,       /**< The store did not answer in time: nothing changed; ask again. */
} arq_dl_result_t;

/**
 * Begin an HK history DL session (OBC → GS, TRANSFER_HK_HISTORY).
 *
 * Copies the stored HT blocks numbered from_seq or later to RAM
 * (hk_history_request(), at most OBDH_REQUEST_WAIT), then notifies the
 * transceiver task to send that copy with arq_run_dl(). Can only be called
 * from comms_task (tc_handler) context.
 *
 * @param session_id  1-byte id chosen by OBC.
 * @param from_seq    First HT block wanted (TC_REQUEST_HK_HISTORY's FROM_SEQ).
 * @return ARQ_DL_STARTED; ARQ_DL_SESSION_ACTIVE; ARQ_DL_NO_DATA if no block
 *         numbered from_seq or later is stored; ARQ_DL_STORE_BUSY if the HT
 *         store did not answer in time.
 */
arq_dl_result_t arq_begin_hk_history(uint8_t session_id, uint32_t from_seq);

/**
 * Begin an OBC log DL session (OBC → GS, TRANSFER_OBC_LOG).
 *
 * The OBC log store does not exist yet: the session sends 8 KB of a test
 * pattern, byte i = i mod 251, for the ARQ throughput tests. Can only be
 * called from comms_task (tc_handler) context.
 *
 * @param session_id  1-byte id chosen by OBC.
 * @return ARQ_DL_STARTED, or ARQ_DL_SESSION_ACTIVE.
 */
arq_dl_result_t arq_begin_obc_log(uint8_t session_id);

/**
 * Arm the UL receiver for the next incoming DATA_BEGIN from GS.
 * Called from tc_handler when TC_UPLOAD_TLE_BEGIN / TC_UPLOAD_ADCS_CAL_BEGIN
 * is received. A subsequent DATA_BEGIN frame will activate the UL session.
 *
 * @param type  Expected TRANSFER_TYPE from GS.
 */
void arq_arm_ul(transfer_type_t type);

/**
 * Process an incoming ARQ frame (DATA_BEGIN / DATA / DATA_END) for a UL
 * session. Called from comms_task after routing from the rx_queue.
 *
 * @param frame  Decoded air-layer frame.
 * @return 1 when the UL session completes (data written to flash); 0 otherwise.
 */
int arq_rx(const AirFrame_t *frame);

/**
 * Run the DL session loop. Must be called from transceiver_task context
 * (it drives the radio directly, like saw_send). Blocks until the session
 * completes, exhausts retransmit attempts, or times out.
 *
 * @param rx_q  comms rx_queue handle (non-matching RX frames are forwarded).
 */
void arq_run_dl(QueueHandle_t rx_q);

/** True if any ARQ session is currently active or armed. */
bool arq_active(void);
