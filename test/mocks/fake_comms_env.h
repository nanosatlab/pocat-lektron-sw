/**
 * @file fake_comms_env.h
 * @brief Fakes for everything the telecommand handler calls outside itself: comms queues, RTC time, beacon, ARQ, MCU sensors and radio.
 * @date 2026-10-23
 *
 * @details
 * `tc_process()` (src/subsystems/comms/tc_handler.c) is the entry point of
 * every telecommand. Its real collaborators either need hardware (RTC, ADC,
 * SX1262 radio) or run their own task loop (comms, beacon, ARQ). This file
 * replaces them with small recorders, so test_tc_handler can check what each
 * TC triggered:
 *
 * | Real module (not linked)          | Replaced functions / data                                   | What the fake records                       |
 * |-----------------------------------|-------------------------------------------------------------|---------------------------------------------|
 * | src/core/main.c                   | `main_get_obc_handle()`                                     | returns a fake "OBC" task                   |
 * | src/subsystems/comms/comms.c      | `comms_next_seq()`, `comms_get_tx_queue()`, `comms_get_rx_queue()` | a real (fake-RTOS) TX queue of `COMMS_QUEUE_LEN` |
 * | src/utils/time.c                  | `time_get_unix()`, `time_set_unix()`                        | settable clock, last value set, call count  |
 * | src/subsystems/comms/beacon.c     | `beacon_set_period()`, `g_last_tc_id`, `g_last_tc_rc`, `g_beacon_period_ms` | last period, call count        |
 * | src/subsystems/comms/arq.c        | `arq_begin_dl()`, `arq_arm_ul()`                            | session id, type, params, call counts       |
 * | src/utils/temperature.c           | `mcu_get_temperature()`, `mcu_get_vdda_mv()`                | returns settable values                     |
 * | RadioLib wrapper                  | `RadioLib_Standby/SetChannel/SetTxConfig/SetRxConfig()`     | number of radio reconfigurations            |
 *
 * **Weak symbols.** `g_last_tc_id`, `g_last_tc_rc`, `g_beacon_period_ms`,
 * `beacon_set_period()`, `arq_begin_dl()` and `arq_arm_ul()` are defined
 * `__attribute__((weak))`. A suite that compiles the real `beacon.c`
 * (test_beacon) or `arq.c` (test_arq) therefore gets the real definitions at
 * link time and these fakes are ignored. When the real module is linked, the
 * `fake_beacon_*` / `fake_arq_*` recorders stay at 0.
 *
 * The radio functions exist so `lora_cfg.c` links; they also record the last
 * configuration (see fake_radio_last_config()). `tc_process()`
 * never applies a configuration itself: that happens later in
 * `lora_cfg_tick()`, which the transceiver task calls.
 */
#ifndef FAKE_COMMS_ENV_H
#define FAKE_COMMS_ENV_H
#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "types.h"

/** @brief Most `params` bytes kept from one `arq_begin_dl()` call. Real calls pass 0, 4 or 5. */
#define FAKE_ARQ_PARAMS_MAX 16

/**
 * @brief Resets every recorder, creates the fake OBC task and the TX/RX queues, and sets the clock to 1 700 000 000 (Nov 2023).
 * @details Call it after `fake_rtos_reset()` in `setUp()`, because it creates
 * fake-RTOS objects. It also resets `g_last_tc_id` to 0xFF and
 * `g_last_tc_rc` to 0, the same values beacon.c starts with.
 */
void fake_comms_env_reset(void);

/** @brief Returns the fake OBC task: the handle `main_get_obc_handle()` gives to `tc_process()`. */
TaskHandle_t fake_comms_env_obc_task(void);

/** @brief Sets what `time_get_unix()` returns (seconds since 1970). */
void     fake_time_set_now(uint32_t unix_s);
/** @brief Returns the number of `time_set_unix()` calls since the last reset. */
int      fake_time_set_count(void);
/** @brief Returns the value passed to the last `time_set_unix()` call. */
uint32_t fake_time_last_set(void);

/** @brief Returns the number of `beacon_set_period()` calls since the last reset. */
int      fake_beacon_set_period_count(void);
/** @brief Returns the period, in ms, passed to the last `beacon_set_period()` call. */
uint32_t fake_beacon_last_period_ms(void);

/** @brief Record of the last `arq_begin_dl()` call. */
typedef struct {
    uint8_t         session_id;                    /**< Session id chosen by tc_handler. */
    transfer_type_t type;                          /**< TRANSFER_* code. */
    uint8_t         params_len;                    /**< Length passed (0 if params was NULL). */
    uint8_t         params[FAKE_ARQ_PARAMS_MAX];   /**< Copy of the params bytes. */
    int             params_was_null;               /**< 1 if the params pointer was NULL. */
} fake_arq_dl_call_t;

/**
 * @brief Sets what the fake `arq_begin_dl()` returns: 0 = session started (default after reset), -1 = a session is already active.
 * @details The real arq_begin_dl() returns -1 while another transfer runs (arq.c:390).
 */
void               fake_arq_set_begin_dl_result(int rc);
/** @brief Returns the number of `arq_begin_dl()` calls since the last reset. */
int                fake_arq_begin_dl_count(void);
/** @brief Returns the record of the last `arq_begin_dl()` call. */
fake_arq_dl_call_t fake_arq_last_dl(void);
/** @brief Returns the number of `arq_arm_ul()` calls since the last reset. */
int                fake_arq_arm_ul_count(void);
/** @brief Returns the transfer type passed to the last `arq_arm_ul()` call. */
transfer_type_t    fake_arq_last_ul_type(void);

/** @brief Sets what `mcu_get_temperature()` (°C) and `mcu_get_vdda_mv()` (mV) return. */
void fake_sensors_set(int8_t temp_c, uint16_t vdda_mv);

/** @brief Returns the number of times the radio was reconfigured (`RadioLib_SetChannel` calls) since the last reset. */
int  fake_radio_config_count(void);

/** @brief Last values passed to the RadioLib configuration functions (all zero after `fake_comms_env_reset()`). */
typedef struct {
    int      standby_calls;                                      /**< Number of `RadioLib_Standby` calls. */
    uint32_t freq_hz;                                            /**< `RadioLib_SetChannel` frequency. */
    uint8_t  tx_sf, tx_cr, tx_bw;                                /**< `RadioLib_SetTxConfig`: SF, CR (1..4), BW code (0..2). */
    int8_t   tx_power;                                           /**< `RadioLib_SetTxConfig` power in dBm. */
    int      tx_iq, tx_crc;                                      /**< `RadioLib_SetTxConfig` IQ inversion and CRC flags. */
    uint16_t tx_preamble;                                        /**< `RadioLib_SetTxConfig` preamble length. */
    uint8_t  rx_sf, rx_cr, rx_bw;                                /**< `RadioLib_SetRxConfig`: SF, CR, BW code. */
    int      rx_iq, rx_crc;                                      /**< `RadioLib_SetRxConfig` IQ inversion and CRC flags. */
    uint16_t rx_preamble;                                        /**< `RadioLib_SetRxConfig` preamble length. */
} fake_radio_cfg_t;

/** @brief Returns a copy of the last radio configuration recorded (used by test_lora_cfg). */
fake_radio_cfg_t fake_radio_last_config(void);

#endif
