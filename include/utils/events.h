/**
 * @file events.h
 * @brief FreeRTOS event-group bit definitions.
 */

#pragma once

#include "FreeRTOS.h"
#include "event_groups.h"

/** @name Task pause/resume ACK event-group bits
 *  Used by task management to track pause/resume acknowledgements.
 * @{ */
#define EV_PAYLOAD_ACK     (1u << 0)
#define EV_EPS_ACK         (1u << 1)
#define EV_COMMS_ACK       (1u << 2)
#define EV_ADCS_ACK        (1u << 3)
#define EV_OBDH_ACK        (1u << 4)
#define EV_TRANSCEIVER_ACK (1u << 5)
#define EV_BEACON_ACK      (1u << 6)
/** @} */
