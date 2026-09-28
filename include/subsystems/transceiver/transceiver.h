/**
 * @file transceiver.h
 * @brief Interrupt-driven RF transceiver task with link-layer ACK and ARQ.
 *
 * Handles all RadioLib hardware operations (RX/TX) driven by DIO1 hardware
 * interrupts, plus link-layer reliability:
 *
 * - On RX_DONE: reads packet, deinterleaves. If it is an ACK, consumes it
 *   internally (ARQ matching). Otherwise, auto-sends a link-layer ACK and
 *   forwards the data packet to comms_task via rx_queue.
 * - On TX_READY: drains tx_queue, interleaves and transmits each packet.
 *   For packets with needs_ack set, performs inline ARQ (send → wait for
 *   ACK → retry on timeout, up to ARQ_MAX_RETRIES).
 * - Always returns to RX mode when idle.
 */

#pragma once

/**
 * @brief Transceiver FreeRTOS task entry point.
 * @param pv_parameters Task parameter provided by xTaskCreate(); currently unused.
 */
void transceiver_task(void *pv_parameters);
