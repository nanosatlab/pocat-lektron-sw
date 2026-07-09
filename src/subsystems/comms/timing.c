#include "timing.h"
#include "lora_cfg.h"

/* All intermediate values in microseconds using integer arithmetic.
 * Reads from the active runtime LoRa config (§9) so timeouts track
 * SF / BW / CR / preamble changes after a TC_LORA_CONFIG commit. */
uint32_t lora_toa_ms(uint16_t payload_bytes)
{
    const uint32_t sf       = lora_cfg_active_sf();
    const uint32_t bw_hz    = lora_cfg_active_bw_hz();
    const uint32_t cr       = lora_cfg_active_cr();
    const uint32_t pre_sym  = lora_cfg_active_preamble();
    const uint32_t crc_en   = lora_cfg_active_crc();
    const uint32_t he       = 0u;
    const uint32_t n        = payload_bytes;

    uint32_t sym_time_us = ((1u << sf) * 1000000u) / bw_hz;

    /* preamble_time_us = (preamble_symbols + 4.25) * symbol_time_us
     * = (pre_sym*4 + 17) * sym_time_us / 4  (avoid floating point) */
    uint32_t preamble_time_us = (pre_sym * 4u + 17u) * sym_time_us / 4u;

    uint32_t de = (sf >= 11u) ? 1u : 0u;

    /* num = 8*N - 4*SF + 28 + 16*CRC - 20*HE */
    int32_t num = (int32_t)(8u * n) - (int32_t)(4u * sf) + 28
                  + (int32_t)(16u * crc_en) - (int32_t)(20u * he);

    uint32_t den = 4u * (sf - 2u * de);

    /* ceil(num/den) — handle negative/zero num (no payload symbols) */
    int32_t ceil_val = 0;
    if (num > 0) {
        ceil_val = ((int32_t)num + (int32_t)den - 1) / (int32_t)den;
    }

    int32_t data_term = ceil_val * (int32_t)(cr + 4u);
    uint32_t n_data_sym = (data_term > 0) ? (uint32_t)data_term : 0u;

    uint32_t n_symb = 8u + n_data_sym;
    uint32_t toa_us = preamble_time_us + n_symb * sym_time_us;

    return (toa_us + 999u) / 1000u;
}

uint32_t ack_timeout_ms(uint8_t tc_frame_len)
{
    return 2u * lora_toa_ms(tc_frame_len) + lora_toa_ms(8u) + 200u;
}
