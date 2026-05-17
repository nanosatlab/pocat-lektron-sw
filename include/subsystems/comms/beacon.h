#pragma once

#include <stdint.h>

#define BEACON_PERIOD_MS      30000u
#define BEACON_HEALTH_KICK_MS  4000u  /* must be < health check period (5 s) */

extern uint8_t  g_last_tc_id;
extern uint8_t  g_last_tc_rc;
extern uint8_t  g_uptime_m;
extern uint32_t g_beacon_period_ms;

void beacon_set_period(uint32_t period_ms);
void beacon_task(void *pv_parameters);
