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

/** @name Battery Status Event-Group Bits
 * Used by EPS to broadcast the power mode, and by other tasks to scale operations.
 * @{ */
#define EV_BAT_NOMINAL     (1u << 0) 
#define EV_BAT_CONTINGENCY (1u << 1)
#define EV_BAT_SUNSAFE     (1u << 2)
#define EV_BAT_SURVIVAL    (1u << 3)

#define ALL_BATTERY_STATES (EV_BAT_NOMINAL | EV_BAT_CONTINGENCY | EV_BAT_SUNSAFE | EV_BAT_SURVIVAL)
/** @} */

/** @brief Global handle for the battery state event group.
 * Initialized in main.c or eps.c */
extern EventGroupHandle_t batteryStatus;
