/**
 * @file eps.h
 * @brief EPS task: battery state machine, telemetry scheduling, autonomous
 *        thermal control and telecommand handling.
 *
 * Hardware access goes exclusively through eps_hw.h; the ground-telemetry
 * frame layout lives in eps_frame.h. This header exposes only the task
 * entry point, the pure logic functions (unit-testable) and the ISR
 * dispatch hooks.
 */

#ifndef INC_EPS_H_
#define INC_EPS_H_

#include <stdint.h>
#include <stdbool.h>
#include "eps_hw.h"
#include "eps_frame.h"

/**
 * @brief EPS FreeRTOS task entry point.
 * @param pv_parameters Task parameter provided by xTaskCreate(); currently unused.
 */
void eps_task(void *pv_parameters);

/* --- Pure logic, exposed for unit testing and reuse --- */
bool EPS_Update_System_State(uint16_t vbat_mv);   /* returns true if the state changed */
void EPS_Heater_Control(int16_t temp_c);

/* --- Hardware interrupt dispatch (called from stm32l4xx_it.c) --- */
void EPS_Fault_IRQHandler(void);   /**< PC4 !FAULT falling edge — disables charger immediately */
void EPS_PFO_IRQHandler(void);     /**< PB5 !PFO both edges — power-fail assert/clear (input power lost/restored) */

#ifdef UNIT_TEST
/* Test-only seams, compiled exclusively in the EPS_TESTS build */
void EPS_Test_Set_Thresholds_mV(const uint16_t mv[3]);
void EPS_Test_Set_Auto_Heat(bool enable);
extern volatile uint32_t eps_cycle_count;   /* ++ at end of each process_eps */
#endif

#endif /* INC_EPS_H_ */
