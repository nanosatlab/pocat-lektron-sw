/**
 * @file fake_link.c
 * @brief Scripted SX1262 link fake. See fake_link.h.
 * @date 2026-10-29
 */
#include "fake_link.h"
#include "fake_rtos.h"
#include "notifications.h"
#include "radiolib_wrapper.h"
#include "unity.h"
#include <string.h>

/** @brief One stored frame. */
typedef struct { uint8_t data[255]; uint8_t len; } frame_t;

static frame_t  tx_log[FAKE_LINK_MAX_TX];
static int      tx_n;
static frame_t  inj[FAKE_LINK_MAX_INJ];
static int      inj_n;
static frame_t  rx_buf;
static bool     rx_loaded;
static uint32_t irq;
static int      tx_serial, answered_serial;
static int      scans, start_rx;
static int16_t  scan_result;
static bool     tx_done_fires;
static fake_link_responder_t responder;

/** @brief Raises the DIO1 "interrupt": notifies the current task. */
static void notify_irq(void)
{
    xTaskNotify(xTaskGetCurrentTaskHandle(), N_TRANSCEIVER_RADIO_IRQ_BIT, eSetBits);
}

void fake_link_reset(void)
{
    memset(tx_log, 0, sizeof(tx_log));
    memset(inj, 0, sizeof(inj));
    memset(&rx_buf, 0, sizeof(rx_buf));
    tx_n = inj_n = 0;
    rx_loaded = false;
    irq = 0;
    tx_serial = answered_serial = 0;
    scans = start_rx = 0;
    scan_result = RADIOLIB_CHANNEL_FREE;
    tx_done_fires = true;
    responder = NULL;
}

void fake_link_set_responder(fake_link_responder_t fn) { responder = fn; }
void fake_link_set_scan_result(int16_t r) { scan_result = r; }
void fake_link_set_tx_done(bool fires) { tx_done_fires = fires; }

void fake_link_inject_rx(const uint8_t *frame, uint8_t len)
{
    TEST_ASSERT_TRUE_MESSAGE(inj_n < FAKE_LINK_MAX_INJ, "fake_link: too many injected frames");
    memcpy(inj[inj_n].data, frame, len);
    inj[inj_n].len = len;
    inj_n++;
}

int fake_link_tx_count(void) { return tx_serial; }

uint8_t fake_link_tx_frame(int i, uint8_t *out)
{
    TEST_ASSERT_TRUE_MESSAGE(i >= 0 && i < tx_n, "fake_link: tx frame index out of range");
    memcpy(out, tx_log[i].data, tx_log[i].len);
    return tx_log[i].len;
}

int fake_link_scan_count(void) { return scans; }
int fake_link_start_receive_count(void) { return start_rx; }

int16_t RadioLib_StartTransmit(uint8_t *buf, uint16_t len)
{
    if (tx_n < FAKE_LINK_MAX_TX) {
        memcpy(tx_log[tx_n].data, buf, len);
        tx_log[tx_n].len = (uint8_t)len;
        tx_n++;
    }
    tx_serial++;
    if (tx_done_fires) {
        irq |= RADIOLIB_SX126X_IRQ_TX_DONE;
        notify_irq();
    }
    return RADIOLIB_ERR_NONE;
}

int16_t RadioLib_StartReceive(uint16_t preambleLen)
{
    (void)preambleLen;
    start_rx++;
    if (inj_n > 0) {
        rx_buf = inj[0];
        for (int i = 1; i < inj_n; i++) inj[i - 1] = inj[i];
        inj_n--;
        rx_loaded = true;
    } else if (responder != NULL && tx_serial != answered_serial) {
        answered_serial = tx_serial;
        /* the last transmitted frame (the log keeps the first FAKE_LINK_MAX_TX only) */
        int last = (tx_n < FAKE_LINK_MAX_TX) ? tx_n - 1 : FAKE_LINK_MAX_TX - 1;
        uint8_t reply[255], rlen = 0;
        if (last >= 0 && responder(tx_log[last].data, tx_log[last].len, reply, &rlen)) {
            memcpy(rx_buf.data, reply, rlen);
            rx_buf.len = rlen;
            rx_loaded = true;
        }
    }
    if (rx_loaded) {
        irq |= RADIOLIB_SX126X_IRQ_RX_DONE;
        notify_irq();
    }
    return RADIOLIB_ERR_NONE;
}

uint32_t RadioLib_GetIrqFlags(void) { return irq; }
void RadioLib_ClearIrqFlags(uint32_t mask) { irq &= ~mask; }

int16_t RadioLib_ReadRxData(uint8_t *outBuf, uint16_t bufSize, uint16_t *outLen, int16_t *outRssi, int8_t *outSnr)
{
    uint16_t n = 0;
    if (rx_loaded) {
        n = rx_buf.len < bufSize ? rx_buf.len : bufSize;
        memcpy(outBuf, rx_buf.data, n);
    }
    if (outLen) *outLen = n;
    if (outRssi) *outRssi = -80;
    if (outSnr) *outSnr = 5;
    rx_loaded = false;
    return RADIOLIB_ERR_NONE;
}

int16_t RadioLib_ScanChannel(void)
{
    scans++;
    return scan_result;
}
