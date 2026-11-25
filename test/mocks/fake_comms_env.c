/**
 * @file fake_comms_env.c
 * @brief Recording fakes for the collaborators of tc_handler.c. See fake_comms_env.h.
 * @date 2026-10-23
 */
#include "fake_comms_env.h"
#include "comms.h"
#include "beacon.h"
#include "arq.h"
#include "time.h"
#include "temperature.h"
#include "radiolib_wrapper.h"
#include "unity.h"
#include <string.h>

/** @brief Marks a definition weak: a suite that also compiles the real module (beacon.c, arq.c) gets the real symbol, the fake is ignored. */
#define POCAT_WEAK __attribute__((weak))

/* ---- globals normally defined in beacon.c (weak, see POCAT_WEAK) ---- */
POCAT_WEAK uint8_t  g_last_tc_id       = 0xFFu;
POCAT_WEAK uint8_t  g_last_tc_rc       = 0x00u;
POCAT_WEAK uint32_t g_beacon_period_ms = BEACON_PERIOD_MS;

/** @brief Body of the fake OBC task. It is never run, because the fake RTOS only records tasks. */
static void fake_obc_task(void *p) { (void)p; }

static TaskHandle_t  obc_task;
static QueueHandle_t tx_queue, rx_queue;
static uint8_t       next_seq;

static uint32_t now_unix, last_set_unix;
static int      time_set_calls;

static int      beacon_period_calls;
static uint32_t beacon_last_period;

static int                arq_dl_calls, arq_ul_calls, arq_dl_result;
static fake_arq_dl_call_t arq_last_dl;
static transfer_type_t    arq_last_ul;

static int8_t   temp_c;
static uint16_t vdda_mv;
static int      radio_configs;
static fake_radio_cfg_t radio_last;

void fake_comms_env_reset(void)
{
    TEST_ASSERT_EQUAL_INT(pdPASS, xTaskCreate(fake_obc_task, "OBC", 256, NULL, 6, &obc_task));
    tx_queue = xQueueCreate(COMMS_QUEUE_LEN, sizeof(TxQueueEntry_t));
    rx_queue = xQueueCreate(COMMS_QUEUE_LEN, sizeof(RxAirFrame_t));
    next_seq = 0;

    now_unix = 1700000000u;
    last_set_unix = 0;
    time_set_calls = 0;

    g_last_tc_id = 0xFFu;
    g_last_tc_rc = 0x00u;
    g_beacon_period_ms = BEACON_PERIOD_MS;
    beacon_period_calls = 0;
    beacon_last_period = 0;

    arq_dl_calls = arq_ul_calls = 0;
    arq_dl_result = 0;
    memset(&arq_last_dl, 0, sizeof(arq_last_dl));
    arq_last_ul = (transfer_type_t)0;

    temp_c = 0;
    vdda_mv = 0;
    radio_configs = 0;
    memset(&radio_last, 0, sizeof(radio_last));
}

TaskHandle_t fake_comms_env_obc_task(void) { return obc_task; }

/* ---- main.c ---- */
TaskHandle_t main_get_obc_handle(void) { return obc_task; }

/* ---- comms.c ---- */
uint8_t       comms_next_seq(void)     { return next_seq++; }
QueueHandle_t comms_get_tx_queue(void) { return tx_queue; }
QueueHandle_t comms_get_rx_queue(void) { return rx_queue; }

/* ---- time.c ---- */
uint32_t time_get_unix(void) { return now_unix; }
void time_set_unix(uint32_t epoch) { last_set_unix = epoch; now_unix = epoch; time_set_calls++; }
void     fake_time_set_now(uint32_t unix_s) { now_unix = unix_s; }
int      fake_time_set_count(void) { return time_set_calls; }
uint32_t fake_time_last_set(void) { return last_set_unix; }

/* ---- beacon.c ---- */
POCAT_WEAK void beacon_set_period(uint32_t period_ms)
{
    g_beacon_period_ms = period_ms;
    beacon_last_period = period_ms;
    beacon_period_calls++;
}
int      fake_beacon_set_period_count(void) { return beacon_period_calls; }
uint32_t fake_beacon_last_period_ms(void) { return beacon_last_period; }

/* ---- arq.c ---- */
POCAT_WEAK int arq_begin_dl(uint8_t session_id, transfer_type_t type, const uint8_t *params, uint8_t params_len)
{
    arq_dl_calls++;
    memset(&arq_last_dl, 0, sizeof(arq_last_dl));
    arq_last_dl.session_id = session_id;
    arq_last_dl.type = type;
    arq_last_dl.params_len = params_len;
    arq_last_dl.params_was_null = (params == NULL);
    if (params != NULL) {
        memcpy(arq_last_dl.params, params, params_len < FAKE_ARQ_PARAMS_MAX ? params_len : FAKE_ARQ_PARAMS_MAX);
    }
    return arq_dl_result;
}
void fake_arq_set_begin_dl_result(int rc) { arq_dl_result = rc; }
POCAT_WEAK void arq_arm_ul(transfer_type_t type) { arq_ul_calls++; arq_last_ul = type; }
int                fake_arq_begin_dl_count(void) { return arq_dl_calls; }
fake_arq_dl_call_t fake_arq_last_dl(void) { return arq_last_dl; }
int                fake_arq_arm_ul_count(void) { return arq_ul_calls; }
transfer_type_t    fake_arq_last_ul_type(void) { return arq_last_ul; }

/* ---- temperature.c ---- */
int8_t   mcu_get_temperature(void) { return temp_c; }
uint16_t mcu_get_vdda_mv(void) { return vdda_mv; }
void fake_sensors_set(int8_t t, uint16_t v) { temp_c = t; vdda_mv = v; }

/* ---- RadioLib wrapper (configuration functions; recorded for test_lora_cfg) ---- */
int16_t RadioLib_Standby(void) { radio_last.standby_calls++; return 0; }
void RadioLib_SetChannel(uint32_t freq) { radio_last.freq_hz = freq; radio_configs++; }
void RadioLib_SetTxConfig(uint8_t sf, uint8_t cr, int8_t power, uint8_t bw_code,
                          int iqInverted, int crcOn, uint16_t preambleLen)
{
    radio_last.tx_sf = sf;
    radio_last.tx_cr = cr;
    radio_last.tx_bw = bw_code;
    radio_last.tx_power = power;
    radio_last.tx_iq = iqInverted;
    radio_last.tx_crc = crcOn;
    radio_last.tx_preamble = preambleLen;
}
void RadioLib_SetRxConfig(uint8_t sf, uint8_t cr, uint8_t bw_code,
                          int iqInverted, int crcOn, uint16_t preambleLen)
{
    radio_last.rx_sf = sf;
    radio_last.rx_cr = cr;
    radio_last.rx_bw = bw_code;
    radio_last.rx_iq = iqInverted;
    radio_last.rx_crc = crcOn;
    radio_last.rx_preamble = preambleLen;
}
int fake_radio_config_count(void) { return radio_configs; }
fake_radio_cfg_t fake_radio_last_config(void) { return radio_last; }
