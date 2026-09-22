/**
 * @file arq.h
 * @brief TT&C v2 ARQ session engine (§7.3).
 *
 * Implements Selective-Repeat ARQ with bitmap for both directions:
 *   DL (OBC → GS): satellite reads flash and sends DATA frames; driven from
 *       transceiver_task context when N_TRANSCEIVER_ARQ_DL_BIT fires.
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

/**
 * Begin a DL session (OBC → GS).
 *
 * Sets up session state and notifies the transceiver task to start
 * arq_run_dl(). Can only be called from comms_task (tc_handler) context.
 *
 * @param session_id  1-byte id chosen by OBC.
 * @param type        TRANSFER_TYPE code from §6.7.1.
 * @param params      Per-TC params (payload bytes after BODY_VER+TC_ID).
 * @param params_len  Length of params.
 * @return 0 on success; -1 if a session is already active.
 */
int arq_begin_dl(uint8_t session_id, transfer_type_t type,
                 const uint8_t *params, uint8_t params_len);

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
