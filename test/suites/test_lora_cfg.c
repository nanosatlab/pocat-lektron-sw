/**
 * @file test_lora_cfg.c
 * @brief Host tests for the LoRa runtime reconfiguration (two-phase commit with auto-revert) and for the time-on-air / ACK timeout maths.
 * @date 2026-10-29
 *
 * @details
 * **What is under test**
 * - src/subsystems/comms/lora_cfg.c:
 *   - `lora_cfg_init()`, `lora_cfg_parse()`, `lora_cfg_request()`;
 *   - `lora_cfg_tick()`: commit at the requested time, immediate commit,
 *     auto-revert;
 *   - `lora_cfg_notify_rx()`: a valid RX cancels the pending revert;
 *   - the read-only accessors (`lora_cfg_current_id()`, `lora_cfg_active_*()`).
 * - src/subsystems/comms/timing.c: `lora_toa_ms()` against hand-calculated
 *   and Semtech-calculator values, and `ack_timeout_ms()`.
 *
 * The range rejects and the BUSY answer while PENDING_COMMIT are already
 * covered by test_tc_handler (`TC_LORA_CONFIG_*`). This suite adds what that
 * one does not reach: byte order of the parser, the commit time, the
 * auto-revert, BUSY during the revert window, and the values that really
 * reach the radio.
 *
 * **State machine of the slot** (`slot_state_t` in lora_cfg.c)
 * - IDLE: no candidate.
 * - PENDING_COMMIT: candidate staged, waiting for `commit_at_unix`.
 * - PENDING_REVERT: candidate applied, waiting for the first valid RX; if none
 *   arrives before the deadline the previous config is restored.
 *
 * **How it is built** (see test/CMakeLists.txt)
 * - `#include`d below: `lora_cfg.c`, so tests can read the static state.
 * - SOURCES: `timing.c` (stateless, reads the config through the public
 *   accessors) and `fake_comms_env.c`.
 *
 * **Fake setup**
 * - `fake_comms_env` provides the RTC (`fake_time_set_now()`, 1 700 000 000 after
 *   reset) and records the last radio configuration (`fake_radio_last_config()`).
 *
 * **How to run**
 * @code
 * docker run --rm -v "${PWD}:/app" pocat-tests test_lora_cfg
 * docker run --rm -v "${PWD}:/app" pocat-tests test_lora_cfg -f toa
 * @endcode
 */
#include "unity.h"
#include "fake_rtos.h"
#include "fake_hal.h"
#include "fake_comms_env.h"
#include "timing.h"
#include "pocat_test_main.h"

#include "lora_cfg.c"

/** @brief Fixed "now" used as the RTC time of every test (matches the value `fake_comms_env_reset()` installs). */
#define T0 1700000000u

/**
 * @brief Runs before every test: fresh fakes and the boot-default LoRa configuration.
 * @details Resets the fake RTOS, the fake HAL and the comms environment (RTC
 * back to T0, radio recorder cleared), then calls `lora_cfg_init()`, which
 * resets `s_committed`, `s_previous` and `s_state` to IDLE. `s_candidate` and
 * `s_revert_deadline_unix` need no reset: they are only read while the state
 * is PENDING_*, and both are rewritten on entering those states.
 */
void setUp(void)
{
    fake_rtos_reset();
    fake_hal_reset();
    fake_comms_env_reset();
    lora_cfg_init();
}

/**
 * @brief Runs after every test: checks the RTOS fake is balanced.
 * @details An unclosed critical section or scheduler suspend would freeze the satellite.
 */
void tearDown(void)
{
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, fake_rtos_critical_depth(), "unbalanced critical section");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, fake_rtos_suspend_all_depth(), "unbalanced scheduler suspend");
}

/**
 * @brief Helper: fills @p c with a valid, non-default configuration (id 2, 869 MHz, SF9, BW125, CR1, 14 dBm, preamble 16, CRC on).
 * @details The commit time and revert time are 0, so the config is applied
 * immediately and never reverted unless the test changes them.
 * @param c Output structure.
 */
static void make_cfg(LoraConfig_t *c)
{
    memset(c, 0, sizeof(*c));
    c->cfg_id           = 2u;
    c->target           = 1u;
    c->frequency_hz     = 869000000ul;
    c->sf               = 9u;
    c->bw               = 0u;
    c->cr               = 1u;
    c->tx_power_dbm     = 14;
    c->preamble_symbols = 16u;
    c->crc_on           = 1u;
    c->iq_inverted      = 0u;
    c->sync_word        = 0x12u;
    c->commit_at_unix   = 0u;
    c->revert_after_s   = 0u;
}

/**
 * @brief Helper: makes (sf, bw, cr, preamble, crc) the active configuration, through the real request + tick path.
 * @param sf  Spreading factor 7..12.
 * @param bw  Bandwidth code 0..2.
 * @param cr  Coding rate 1..4.
 * @param pre Preamble length in symbols.
 * @param crc CRC flag.
 */
static void activate(uint8_t sf, uint8_t bw, uint8_t cr, uint16_t pre, uint8_t crc)
{
    LoraConfig_t c;
    make_cfg(&c);
    c.sf = sf;
    c.bw = bw;
    c.cr = cr;
    c.preamble_symbols = pre;
    c.crc_on = crc;
    TEST_ASSERT_EQUAL_INT(LORA_CFG_OK, lora_cfg_request(&c));
    TEST_ASSERT_TRUE(lora_cfg_tick());
}

/* =========================================================================
 * 1. Defaults and parsing
 * ========================================================================= */

/**
 * @brief After `lora_cfg_init()` the committed config is the documented boot default and the radio is left alone.
 * @details Expected: cfg id 1, SF10, 125 kHz, CR1, preamble 64, CRC on.
 * `lora_cfg_tick()` returns false and the radio recorder is empty. Matters
 * because the ground station's first contact depends on these values
 * matching what transceiver.c installs at boot.
 */
void test_init_sets_boot_defaults(void)
{
    TEST_ASSERT_EQUAL_UINT8(1, lora_cfg_current_id());
    TEST_ASSERT_EQUAL_UINT8(10, lora_cfg_active_sf());
    TEST_ASSERT_EQUAL_UINT32(125000u, lora_cfg_active_bw_hz());
    TEST_ASSERT_EQUAL_UINT8(1, lora_cfg_active_cr());
    TEST_ASSERT_EQUAL_UINT16(64, lora_cfg_active_preamble());
    TEST_ASSERT_EQUAL_UINT8(1, lora_cfg_active_crc());
    TEST_ASSERT_FALSE(lora_cfg_tick());
    TEST_ASSERT_EQUAL_INT(0, fake_radio_config_count());
    TEST_ASSERT_EQUAL_INT(0, fake_radio_last_config().standby_calls);
}

/**
 * @brief `lora_cfg_parse()` reads every multi-byte field big-endian and sign-extends the power.
 * @details Arrange a 21-byte body with distinct values in every field
 * (frequency 869 000 000, preamble 0x0102, commit 0x0A0B0C0D, revert 0x0E0F,
 * power byte 0xFD). Expected: every field decoded to the matching value,
 * power -3, result OK. A byte-order mistake here would send the radio to a
 * wrong frequency or schedule the commit decades away.
 */
void test_parse_decodes_big_endian_fields(void)
{
    uint8_t b[21] = {0};
    b[0] = 0x07; b[1] = 0x03;
    b[2] = (uint8_t)(869000000ul >> 24); b[3] = (uint8_t)(869000000ul >> 16);
    b[4] = (uint8_t)(869000000ul >> 8);  b[5] = (uint8_t)(869000000ul);
    b[6] = 11; b[7] = 2; b[8] = 3;
    b[9] = 0xFD;
    b[10] = 0x01; b[11] = 0x02;
    b[12] = 1; b[13] = 1; b[14] = 0x34;
    b[15] = 0x0A; b[16] = 0x0B; b[17] = 0x0C; b[18] = 0x0D;
    b[19] = 0x0E; b[20] = 0x0F;

    LoraConfig_t c;
    TEST_ASSERT_EQUAL_INT(LORA_CFG_OK, lora_cfg_parse(b, &c));
    TEST_ASSERT_EQUAL_UINT8(7, c.cfg_id);
    TEST_ASSERT_EQUAL_UINT8(3, c.target);
    TEST_ASSERT_EQUAL_UINT32(869000000ul, c.frequency_hz);
    TEST_ASSERT_EQUAL_UINT8(11, c.sf);
    TEST_ASSERT_EQUAL_UINT8(2, c.bw);
    TEST_ASSERT_EQUAL_UINT8(3, c.cr);
    TEST_ASSERT_EQUAL_INT8(-3, c.tx_power_dbm);
    TEST_ASSERT_EQUAL_UINT16(0x0102, c.preamble_symbols);
    TEST_ASSERT_EQUAL_UINT8(1, c.crc_on);
    TEST_ASSERT_EQUAL_UINT8(1, c.iq_inverted);
    TEST_ASSERT_EQUAL_UINT8(0x34, c.sync_word);
    TEST_ASSERT_EQUAL_UINT32(0x0A0B0C0Du, c.commit_at_unix);
    TEST_ASSERT_EQUAL_UINT16(0x0E0F, c.revert_after_s);
}

/* =========================================================================
 * 2. Two-phase commit
 * ========================================================================= */

/**
 * @brief Requesting a config only stages it: the radio is not touched and the active values do not change.
 * @details Expected: the request returns OK, the slot is PENDING_COMMIT, the
 * radio recorder is empty and the committed id is still 1. Matters because
 * the SPI to the SX1262 must be touched only by the transceiver task (via
 * `lora_cfg_tick()`), never by the telecommand handler.
 */
void test_request_stages_without_touching_radio(void)
{
    LoraConfig_t c;
    make_cfg(&c);
    TEST_ASSERT_EQUAL_INT(LORA_CFG_OK, lora_cfg_request(&c));
    TEST_ASSERT_EQUAL_INT(SLOT_STATE_PENDING_COMMIT, s_state);
    TEST_ASSERT_EQUAL_INT(0, fake_radio_config_count());
    TEST_ASSERT_EQUAL_INT(0, fake_radio_last_config().standby_calls);
    TEST_ASSERT_EQUAL_UINT8(1, lora_cfg_current_id());
}

/**
 * @brief With `commit_at_unix == 0` the next tick commits and applies exactly the staged values to the radio.
 * @details Expected: tick returns true (so the transceiver re-arms RX), the
 * radio recorder holds the candidate's frequency, SF, CR, BW, power,
 * preamble, CRC and IQ for both TX and RX, the id becomes 2 and the slot is
 * IDLE (no revert requested). Matters because a field mixed up between TX and
 * RX config would silently break one direction of the link.
 */
void test_tick_commits_immediately_when_commit_at_is_zero(void)
{
    LoraConfig_t c;
    make_cfg(&c);
    c.iq_inverted = 1u;
    TEST_ASSERT_EQUAL_INT(LORA_CFG_OK, lora_cfg_request(&c));
    TEST_ASSERT_TRUE(lora_cfg_tick());

    fake_radio_cfg_t r = fake_radio_last_config();
    TEST_ASSERT_EQUAL_INT(1, r.standby_calls);
    TEST_ASSERT_EQUAL_UINT32(869000000ul, r.freq_hz);
    TEST_ASSERT_EQUAL_UINT8(9, r.tx_sf);
    TEST_ASSERT_EQUAL_UINT8(1, r.tx_cr);
    TEST_ASSERT_EQUAL_UINT8(0, r.tx_bw);
    TEST_ASSERT_EQUAL_INT8(14, r.tx_power);
    TEST_ASSERT_EQUAL_INT(1, r.tx_iq);
    TEST_ASSERT_EQUAL_INT(1, r.tx_crc);
    TEST_ASSERT_EQUAL_UINT16(16, r.tx_preamble);
    TEST_ASSERT_EQUAL_UINT8(9, r.rx_sf);
    TEST_ASSERT_EQUAL_UINT8(1, r.rx_cr);
    TEST_ASSERT_EQUAL_UINT8(0, r.rx_bw);
    TEST_ASSERT_EQUAL_INT(1, r.rx_iq);
    TEST_ASSERT_EQUAL_INT(1, r.rx_crc);
    TEST_ASSERT_EQUAL_UINT16(16, r.rx_preamble);
    TEST_ASSERT_EQUAL_UINT8(2, lora_cfg_current_id());
    TEST_ASSERT_EQUAL_INT(SLOT_STATE_IDLE, s_state);
}

/**
 * @brief A config with a future commit time waits: no change at now and now+59, commit at now+60.
 * @details Arrange `commit_at_unix = T0 + 60`. Expected: ticks at T0 and
 * T0+59 return false and leave the radio alone; the tick at T0+60 returns
 * true. Matters because the ground schedules the switch so both ends change
 * parameters at the same instant.
 */
void test_tick_waits_until_commit_time(void)
{
    LoraConfig_t c;
    make_cfg(&c);
    c.commit_at_unix = T0 + 60u;
    TEST_ASSERT_EQUAL_INT(LORA_CFG_OK, lora_cfg_request(&c));

    fake_time_set_now(T0);
    TEST_ASSERT_FALSE(lora_cfg_tick());
    fake_time_set_now(T0 + 59u);
    TEST_ASSERT_FALSE(lora_cfg_tick());
    TEST_ASSERT_EQUAL_INT(0, fake_radio_config_count());
    TEST_ASSERT_EQUAL_UINT8(1, lora_cfg_current_id());

    fake_time_set_now(T0 + 60u);
    TEST_ASSERT_TRUE(lora_cfg_tick());
    TEST_ASSERT_EQUAL_UINT8(2, lora_cfg_current_id());
}

/**
 * @brief With no valid RX after a commit, the previous config is restored at the revert deadline.
 * @details Arrange `revert_after_s = 30`, commit at T0. Expected: the slot
 * is PENDING_REVERT; a tick at T0+29 returns false; at T0+30 it returns true,
 * the id is back to 1, the radio is re-applied with the boot values
 * (SF10, preamble 64, 868 MHz, 18 dBm) and the slot is IDLE. This is the
 * safety net that stops a bad configuration from losing the satellite.
 */
void test_auto_revert_after_deadline_without_rx(void)
{
    LoraConfig_t c;
    make_cfg(&c);
    c.revert_after_s = 30u;
    TEST_ASSERT_EQUAL_INT(LORA_CFG_OK, lora_cfg_request(&c));
    TEST_ASSERT_TRUE(lora_cfg_tick());
    TEST_ASSERT_EQUAL_INT(SLOT_STATE_PENDING_REVERT, s_state);
    TEST_ASSERT_EQUAL_UINT8(2, lora_cfg_current_id());

    fake_time_set_now(T0 + 29u);
    TEST_ASSERT_FALSE(lora_cfg_tick());
    TEST_ASSERT_EQUAL_UINT8(2, lora_cfg_current_id());

    fake_time_set_now(T0 + 30u);
    TEST_ASSERT_TRUE(lora_cfg_tick());
    TEST_ASSERT_EQUAL_UINT8(1, lora_cfg_current_id());
    fake_radio_cfg_t r = fake_radio_last_config();
    TEST_ASSERT_EQUAL_UINT8(10, r.tx_sf);
    TEST_ASSERT_EQUAL_UINT16(64, r.tx_preamble);
    TEST_ASSERT_EQUAL_UINT16(64, r.rx_preamble);
    TEST_ASSERT_EQUAL_UINT32(868000000ul, r.freq_hz);
    TEST_ASSERT_EQUAL_INT8(18, r.tx_power);
    TEST_ASSERT_EQUAL_INT(SLOT_STATE_IDLE, s_state);
}

/**
 * @brief A received frame after the commit cancels the pending revert and the new config stays.
 * @details Commit with `revert_after_s = 30`, then `lora_cfg_notify_rx()`.
 * Expected: slot IDLE; a tick at the deadline returns false; id stays 2.
 * Matters because a received frame proves the ground can hear us at the new
 * parameters.
 */
void test_rx_after_commit_cancels_revert(void)
{
    LoraConfig_t c;
    make_cfg(&c);
    c.revert_after_s = 30u;
    TEST_ASSERT_EQUAL_INT(LORA_CFG_OK, lora_cfg_request(&c));
    TEST_ASSERT_TRUE(lora_cfg_tick());

    lora_cfg_notify_rx();
    TEST_ASSERT_EQUAL_INT(SLOT_STATE_IDLE, s_state);

    fake_time_set_now(T0 + 30u);
    TEST_ASSERT_FALSE(lora_cfg_tick());
    TEST_ASSERT_EQUAL_UINT8(2, lora_cfg_current_id());
}

/**
 * @brief A received frame while the commit is still pending does not cancel the commit.
 * @details Expected: after `lora_cfg_notify_rx()` the slot is still
 * PENDING_COMMIT and the later tick still commits. Matters because normal
 * telecommands keep arriving between the request and the scheduled commit;
 * they must not drop the staged config.
 */
void test_notify_rx_while_pending_commit_does_not_cancel_commit(void)
{
    LoraConfig_t c;
    make_cfg(&c);
    c.commit_at_unix = T0 + 10u;
    TEST_ASSERT_EQUAL_INT(LORA_CFG_OK, lora_cfg_request(&c));

    lora_cfg_notify_rx();
    TEST_ASSERT_EQUAL_INT(SLOT_STATE_PENDING_COMMIT, s_state);

    fake_time_set_now(T0 + 10u);
    TEST_ASSERT_TRUE(lora_cfg_tick());
    TEST_ASSERT_EQUAL_UINT8(2, lora_cfg_current_id());
}

/**
 * @brief While the revert window is open a new request is refused with BUSY.
 * @details test_tc_handler only covers BUSY during PENDING_COMMIT. Here the
 * slot is PENDING_REVERT after a commit with `revert_after_s`. Expected:
 * `lora_cfg_request()` returns `LORA_CFG_BUSY`. Matters because accepting a
 * second config now would overwrite `s_previous`'s safe fallback logic.
 */
void test_request_busy_during_revert_window(void)
{
    LoraConfig_t c;
    make_cfg(&c);
    c.revert_after_s = 30u;
    TEST_ASSERT_EQUAL_INT(LORA_CFG_OK, lora_cfg_request(&c));
    TEST_ASSERT_TRUE(lora_cfg_tick());
    TEST_ASSERT_EQUAL_INT(SLOT_STATE_PENDING_REVERT, s_state);

    c.cfg_id = 3u;
    TEST_ASSERT_EQUAL_INT(LORA_CFG_BUSY, lora_cfg_request(&c));
    TEST_ASSERT_EQUAL_UINT8(2, lora_cfg_current_id());
}

/**
 * @brief Once a commit has completed (IDLE), a new request is accepted again.
 * @details Commit config 2, then request config 3. Expected: OK and, after a
 * tick, id 3. Matters because a stuck slot would block every later
 * reconfiguration until reboot.
 */
void test_request_accepted_again_after_commit_completes(void)
{
    activate(9, 0, 1, 16, 1);
    LoraConfig_t c;
    make_cfg(&c);
    c.cfg_id = 3u;
    TEST_ASSERT_EQUAL_INT(LORA_CFG_OK, lora_cfg_request(&c));
    TEST_ASSERT_TRUE(lora_cfg_tick());
    TEST_ASSERT_EQUAL_UINT8(3, lora_cfg_current_id());
}

/* =========================================================================
 * 3. Accessors
 * ========================================================================= */

/**
 * @brief The bandwidth code maps to 125, 250 and 500 kHz.
 * @details Activates BW code 0, 1 and 2 in turn and reads
 * `lora_cfg_active_bw_hz()`. Matters because the time-on-air maths and every
 * timeout use this value.
 */
void test_bw_code_maps_to_hz(void)
{
    activate(9, 0, 1, 16, 1);
    TEST_ASSERT_EQUAL_UINT32(125000u, lora_cfg_active_bw_hz());
    activate(9, 1, 1, 16, 1);
    TEST_ASSERT_EQUAL_UINT32(250000u, lora_cfg_active_bw_hz());
    activate(9, 2, 1, 16, 1);
    TEST_ASSERT_EQUAL_UINT32(500000u, lora_cfg_active_bw_hz());
}

/**
 * @brief The snapshot returns the committed config, not the pending candidate.
 * @details Commit id 2, stage id 3 (not yet committed), take a snapshot.
 * Expected: id 2, SF9, 869 MHz. Matters because TM_DOWNLINK_CONFIG must
 * report what the radio is really using.
 */
void test_snapshot_returns_committed_config(void)
{
    activate(9, 0, 1, 16, 1);

    LoraConfig_t c;
    make_cfg(&c);
    c.cfg_id = 3u;
    c.sf = 12u;
    c.commit_at_unix = T0 + 1000u;
    TEST_ASSERT_EQUAL_INT(LORA_CFG_OK, lora_cfg_request(&c));

    LoraConfig_t s;
    lora_cfg_active_snapshot(&s);
    TEST_ASSERT_EQUAL_UINT8(2, s.cfg_id);
    TEST_ASSERT_EQUAL_UINT8(9, s.sf);
    TEST_ASSERT_EQUAL_UINT32(869000000ul, s.frequency_hz);
}

/* =========================================================================
 * 4. Time on air and ACK timeout
 * ========================================================================= */

/**
 * @brief Time on air at the boot defaults: 8 bytes take 707 ms, 16 bytes take 789 ms.
 * @details SF10, BW125, CR1, preamble 64, CRC on, explicit header. Hand
 * calculation: symbol 8192 us; preamble (64*4+17)*8192/4 = 559 104 us; n=8:
 * ceil(68/40)=2 so 10+8 = 18 symbols = 706 560 us -> 707 ms; n=16:
 * ceil(132/40)=4 so 20+8 = 28 symbols = 788 480 us -> 789 ms. Matters because
 * every ARQ timeout is built from these numbers.
 */
void test_toa_golden_default_sf10(void)
{
    TEST_ASSERT_EQUAL_UINT32(707, lora_toa_ms(8));
    TEST_ASSERT_EQUAL_UINT32(789, lora_toa_ms(16));
}

/**
 * @brief SF7 time on air matches the Semtech calculator (41.22 ms rounded up to 42).
 * @details SF7, BW125, CR1, preamble 8, CRC on, 10-byte payload.
 */
void test_toa_golden_sf7_matches_semtech_calculator(void)
{
    activate(7, 0, 1, 8, 1);
    TEST_ASSERT_EQUAL_UINT32(42, lora_toa_ms(10));
}

/**
 * @brief SF12 time on air uses low-data-rate optimisation (Semtech 991.23 ms, rounded up to 992).
 * @details SF12, BW125, CR1, preamble 8, CRC on, 10 bytes. The DE=1 branch
 * (SF >= 11) changes the symbol count; getting it wrong underestimates the
 * time on air of the slowest, most robust profile.
 */
void test_toa_golden_sf12_uses_low_data_rate_optimisation(void)
{
    activate(12, 0, 1, 8, 1);
    TEST_ASSERT_EQUAL_UINT32(992, lora_toa_ms(10));
}

/**
 * @brief `ack_timeout_ms(16)` at the defaults equals 2 * ToA(16) + ToA(8) + 200 = 2485 ms.
 * @details Documents the current formula (see KI-19 below for why it may be
 * too short).
 */
void test_ack_timeout_formula_at_defaults(void)
{
    TEST_ASSERT_EQUAL_UINT32(2u * 789u + 707u + 200u, ack_timeout_ms(16));
    TEST_ASSERT_EQUAL_UINT32(2485, ack_timeout_ms(16));
}

/**
 * @brief The ACK timeout should be based on the real 16-byte air ACK, not on an 8-byte one.
 * @details Expected: `ack_timeout_ms(16) == 2*ToA(16) + ToA(16) + 200` (2567 ms).
 * Actual: 2485 ms, because timing.c:309 uses ToA(8) for the ACK. On the
 * satellite this underestimates every ACK wait by 82 ms at SF10, and more at
 * SF11/12.
 * @bug KI-19 (test/README.md) expected 2567, actual 2485.
 */
void test_ack_timeout_uses_real_ack_frame_size(void)
{
    TEST_IGNORE_MESSAGE("KNOWN BUG KI-19: ack_timeout_ms uses the ToA of an 8-byte ACK, but an air ACK is 16 bytes");
    TEST_ASSERT_EQUAL_UINT32(2u * lora_toa_ms(16) + lora_toa_ms(16) + 200u, ack_timeout_ms(16));
}

/**
 * @brief Time on air must not wrap for a very long preamble.
 * @details Activate SF12, BW125, CR1, preamble 40000 (`validate()` accepts it),
 * CRC on. The preamble alone lasts about 1 310 859 ms, so the ToA of 16 bytes
 * must exceed 1 300 000 ms. Actual: about 237 000 ms, because
 * `(pre_sym*4+17)*sym_time_us` overflows uint32. Every timeout derived from
 * it would then be far too short.
 * @bug KI-18 (test/README.md) expected > 1 300 000 ms, actual ~237 000 ms.
 */
void test_toa_does_not_wrap_for_long_preamble(void)
{
    TEST_IGNORE_MESSAGE("KNOWN BUG KI-18: preamble_symbols is never range-checked and the ToA maths overflows uint32");
    activate(12, 0, 1, 40000, 1);
    TEST_ASSERT_GREATER_THAN_UINT32(1300000u, lora_toa_ms(16));
}

/**
 * @brief Test runner: registers and runs every test of the suite.
 * @param argc Argument count (the `-l`, `-f`, `-n`, `-x` options are handled by pocat_parse_args()).
 * @param argv Arguments.
 * @return Number of failed tests (0 = all pass).
 */
int main(int argc, char **argv)
{
    if (pocat_parse_args(argc, argv) != 0) return 1;
    POCAT_BEGIN();
    POCAT_RUN_TEST(test_init_sets_boot_defaults);
    POCAT_RUN_TEST(test_parse_decodes_big_endian_fields);
    POCAT_RUN_TEST(test_request_stages_without_touching_radio);
    POCAT_RUN_TEST(test_tick_commits_immediately_when_commit_at_is_zero);
    POCAT_RUN_TEST(test_tick_waits_until_commit_time);
    POCAT_RUN_TEST(test_auto_revert_after_deadline_without_rx);
    POCAT_RUN_TEST(test_rx_after_commit_cancels_revert);
    POCAT_RUN_TEST(test_notify_rx_while_pending_commit_does_not_cancel_commit);
    POCAT_RUN_TEST(test_request_busy_during_revert_window);
    POCAT_RUN_TEST(test_request_accepted_again_after_commit_completes);
    POCAT_RUN_TEST(test_bw_code_maps_to_hz);
    POCAT_RUN_TEST(test_snapshot_returns_committed_config);
    POCAT_RUN_TEST(test_toa_golden_default_sf10);
    POCAT_RUN_TEST(test_toa_golden_sf7_matches_semtech_calculator);
    POCAT_RUN_TEST(test_toa_golden_sf12_uses_low_data_rate_optimisation);
    POCAT_RUN_TEST(test_ack_timeout_formula_at_defaults);
    POCAT_RUN_TEST(test_ack_timeout_uses_real_ack_frame_size);
    POCAT_RUN_TEST(test_toa_does_not_wrap_for_long_preamble);
    return POCAT_END();
}
