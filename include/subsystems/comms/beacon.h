#pragma once

#include <stdint.h>

#define BEACON_HEALTH_KICK_MS  4000u  /* must be < health check period (5 s) */

extern uint8_t  g_last_tc_id;
extern uint8_t  g_last_tc_rc;

/* The beacon period and its timer are in beacon_period.h (flash plan step 5b) */
void beacon_task(void *pv_parameters);
