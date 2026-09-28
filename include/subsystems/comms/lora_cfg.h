/**
 * @file lora_cfg.h
 * @brief LoRa runtime reconfiguration (TT&C v2 §9).
 *
 * Two-phase commit with auto-revert. Three slots: committed, candidate,
 * previous. On revert (timer expiry without a valid RX at new params),
 * swap committed ↔ previous so we fall back to the working config.
 *
 * Apply is driven by lora_cfg_tick() from the transceiver task between
 * RX/TX cycles so SPI access to the SX1262 is single-owner.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

/* Active LoRa parameters and pending-commit metadata. */
typedef struct {
    uint8_t  cfg_id;            /* §9.2 CFG_ID (rolling 1 B counter) */
    uint8_t  target;            /* §9.2 TARGET (0x01=sat, 0x02=fw, 0x03=both) */
    uint32_t frequency_hz;
    uint8_t  sf;                /* 7..12 */
    uint8_t  bw;                /* 0=125 kHz, 1=250, 2=500 */
    uint8_t  cr;                /* 1..4 → RadioLib 5..8 */
    int8_t   tx_power_dbm;
    uint16_t preamble_symbols;
    uint8_t  crc_on;            /* 0/1 */
    uint8_t  iq_inverted;       /* 0/1 */
    uint8_t  sync_word;
    uint32_t commit_at_unix;    /* 0 = apply immediately */
    uint16_t revert_after_s;
} LoraConfig_t;

typedef enum {
    LORA_CFG_OK       = 0,
    LORA_CFG_REJECTED = 1,  /* range check failed (NACK CONFIG_REJECTED) */
    LORA_CFG_BUSY     = 2,  /* a candidate is already pending (NACK BUSY) */
} lora_cfg_result_t;

/* Initialise lora_cfg with hard-coded defaults (the first "committed" slot).
 * Must be called once before any other lora_cfg_* call. */
void lora_cfg_init(void);

/* Parse a 21-byte §9.2 config struct (without BODY_VER — that lives in the
 * outer TC body header). Returns LORA_CFG_REJECTED on range violation. */
lora_cfg_result_t lora_cfg_parse(const uint8_t *body21, LoraConfig_t *out);

/* Stage a candidate config. Range-checks again defensively. Returns
 * LORA_CFG_BUSY if another candidate is already pending. */
lora_cfg_result_t lora_cfg_request(const LoraConfig_t *cfg);

/* Called from the transceiver task once per main-loop iteration. Performs
 * the SPI swap when commit_at_unix has arrived and triggers the auto-revert
 * when the receive grace window expires. Returns true if the radio config
 * changed during this tick (transceiver should re-arm RX after). */
bool lora_cfg_tick(void);

/* Called from the transceiver on every successfully decoded air frame.
 * Disarms the pending revert timer once we know the new config is alive. */
void lora_cfg_notify_rx(void);

/* Read-only accessors used by beacon (§6.6 CFG_ID) and timing. */
uint8_t  lora_cfg_current_id(void);
uint8_t  lora_cfg_active_sf(void);
uint32_t lora_cfg_active_bw_hz(void);
uint8_t  lora_cfg_active_cr(void);
uint16_t lora_cfg_active_preamble(void);
uint8_t  lora_cfg_active_crc(void);

/* Copy the active (committed) config into *out. Used by tm_builder to
 * serialise TM_DOWNLINK_CONFIG (§11). */
void lora_cfg_active_snapshot(LoraConfig_t *out);
