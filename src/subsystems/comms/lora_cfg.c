#include "lora_cfg.h"
#include "radiolib_wrapper.h"
#include "time.h"
#include <stdbool.h>

/* §9.4 safety constraints. */
#define LORA_FREQ_MIN_HZ   860000000ul
#define LORA_FREQ_MAX_HZ   928000000ul
#define LORA_TX_POWER_MAX  22

typedef enum {
    SLOT_STATE_IDLE,                  /* no candidate */
    SLOT_STATE_PENDING_COMMIT,        /* candidate staged, waiting for commit_at_unix */
    SLOT_STATE_PENDING_REVERT,        /* candidate applied, watching for first valid RX */
} slot_state_t;

static LoraConfig_t  s_committed;
static LoraConfig_t  s_candidate;
static LoraConfig_t  s_previous;
static slot_state_t  s_state = SLOT_STATE_IDLE;
static uint32_t      s_revert_deadline_unix;

static uint32_t bw_code_to_hz(uint8_t bw_code)
{
    switch (bw_code) {
    case 0: return 125000ul;
    case 1: return 250000ul;
    case 2: return 500000ul;
    default: return 125000ul;
    }
}

static bool validate(const LoraConfig_t *c)
{
    if (c->frequency_hz < LORA_FREQ_MIN_HZ || c->frequency_hz > LORA_FREQ_MAX_HZ) {
        return false;
    }
    if (c->tx_power_dbm > LORA_TX_POWER_MAX) {
        return false;
    }
    if (c->sf < 7u || c->sf > 12u) {
        return false;
    }
    if (c->bw > 2u) {
        return false;
    }
    if (c->cr < 1u || c->cr > 4u) {
        return false;
    }
    return true;
}

static void apply_to_radio(const LoraConfig_t *c)
{
    RadioLib_Standby();
    RadioLib_SetChannel(c->frequency_hz);
    RadioLib_SetTxConfig(c->sf, c->cr, c->tx_power_dbm, c->bw,
                         c->iq_inverted, c->crc_on, c->preamble_symbols);
    RadioLib_SetRxConfig(c->sf, c->cr, c->bw,
                         c->iq_inverted, c->crc_on, c->preamble_symbols);
}

void lora_cfg_init(void)
{
    /* Defaults match the hard-coded values that transceiver.c installs at
     * boot (RF_FREQUENCY, LORA_*_DEFAULT, TX_OUTPUT_POWER). CFG_ID=1 is the
     * first committed config.
     * SF10 (not the original SF8) per the bench test campaign: SF11/12
     * showed a sharp, unexplained reliability cliff (see ir-report/
     * test-campaign.md) — SF9/SF10 were consistently clean, SF10 chosen for
     * the larger link margin. */
    LoraConfig_t boot = {
        .cfg_id           = 0x01u,
        .target           = 0x01u,
        .frequency_hz     = 868000000ul,
        .sf               = 10u,
        .bw               = 0u,
        .cr               = 1u,
        .tx_power_dbm     = 18,
        .preamble_symbols = 64u,
        .crc_on           = 1u,
        .iq_inverted      = 0u,
        .sync_word        = 0x12u,
        .commit_at_unix   = 0u,
        .revert_after_s   = 0u,
    };
    s_committed = boot;
    s_previous  = boot;
    s_state     = SLOT_STATE_IDLE;
}

lora_cfg_result_t lora_cfg_parse(const uint8_t *body, LoraConfig_t *out)
{
    /* body is 21 bytes: §9.2 fields 1..21 (CFG_ID..REVERT_AFTER_S). */
    out->cfg_id           = body[0];
    out->target           = body[1];
    out->frequency_hz     = ((uint32_t)body[2] << 24) | ((uint32_t)body[3] << 16) |
                            ((uint32_t)body[4] << 8)  |  (uint32_t)body[5];
    out->sf               = body[6];
    out->bw               = body[7];
    out->cr               = body[8];
    out->tx_power_dbm     = (int8_t)body[9];
    out->preamble_symbols = (uint16_t)(((uint16_t)body[10] << 8) | body[11]);
    out->crc_on           = body[12];
    out->iq_inverted      = body[13];
    out->sync_word        = body[14];
    out->commit_at_unix   = ((uint32_t)body[15] << 24) | ((uint32_t)body[16] << 16) |
                            ((uint32_t)body[17] << 8)  |  (uint32_t)body[18];
    out->revert_after_s   = (uint16_t)(((uint16_t)body[19] << 8) | body[20]);

    return validate(out) ? LORA_CFG_OK : LORA_CFG_REJECTED;
}

lora_cfg_result_t lora_cfg_request(const LoraConfig_t *cfg)
{
    if (!validate(cfg)) {
        return LORA_CFG_REJECTED;
    }
    if (s_state != SLOT_STATE_IDLE) {
        return LORA_CFG_BUSY;
    }
    s_candidate = *cfg;
    s_state     = SLOT_STATE_PENDING_COMMIT;
    return LORA_CFG_OK;
}

bool lora_cfg_tick(void)
{
    if (s_state == SLOT_STATE_IDLE) {
        return false;
    }

    uint32_t now = time_get_unix();

    if (s_state == SLOT_STATE_PENDING_COMMIT) {
        /* commit_at_unix == 0 means apply immediately. */
        if (s_candidate.commit_at_unix != 0u && now < s_candidate.commit_at_unix) {
            return false;
        }

        s_previous  = s_committed;
        s_committed = s_candidate;
        apply_to_radio(&s_committed);

        if (s_committed.revert_after_s > 0u) {
            s_revert_deadline_unix = now + s_committed.revert_after_s;
            s_state = SLOT_STATE_PENDING_REVERT;
        } else {
            s_state = SLOT_STATE_IDLE;
        }
        printf("LoRa config committed: ID=%d\r\n", s_committed.cfg_id);
        return true;
    }

    if (s_state == SLOT_STATE_PENDING_REVERT) {
        if (now >= s_revert_deadline_unix) {
            /* No valid RX at new params — fall back. */
            LoraConfig_t tmp = s_committed;
            s_committed = s_previous;
            s_previous  = tmp;
            apply_to_radio(&s_committed);
            s_state = SLOT_STATE_IDLE;
            printf("LoRa config auto-reverted: ID=%d\r\n", s_committed.cfg_id);
            return true;
        }
    }

    return false;
}

void lora_cfg_notify_rx(void)
{
    if (s_state == SLOT_STATE_PENDING_REVERT) {
        s_state = SLOT_STATE_IDLE;
    }
}

uint8_t  lora_cfg_current_id(void)      { return s_committed.cfg_id; }
uint8_t  lora_cfg_active_sf(void)       { return s_committed.sf; }
uint32_t lora_cfg_active_bw_hz(void)    { return bw_code_to_hz(s_committed.bw); }
uint8_t  lora_cfg_active_cr(void)       { return s_committed.cr; }
uint16_t lora_cfg_active_preamble(void) { return s_committed.preamble_symbols; }
uint8_t  lora_cfg_active_crc(void)      { return s_committed.crc_on; }

void lora_cfg_active_snapshot(LoraConfig_t *out)
{
    *out = s_committed;
}
