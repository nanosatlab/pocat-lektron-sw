# Test design, Phase 2/3 batch: OBC task, LoRa config + timing, beacon, subsystem task contract, SAW/ARQ, real POCKET+

> Written by the architect agent on 2026-10-29. Builds on `docs/testing/test-design.md` (Layers 1 and 2 are done) and `test/README.md` (conventions R1–R6, fakes, KB/KI table). Status (2026-11-12): **implemented except the `test_arq` downlink tests (B3.6 tests 20–24, P3, so KI-23 has no test yet).** All P1 and P2 suites exist and are registered: `test_lora_cfg`, `test_obc`, `test_subsystem_tasks`, `test_beacon`, `test_saw`, `test_arq` (uplink, tests 1–19) and `test_ht_compression`. `ctest -N` lists 15 suites. The README update (B4.9) is done. AC3 (each IGNORE fails without it) was checked for KI-20, KI-21 and KI-22; it hasn't been recorded for KI-6, KI-15 (test_beacon), KI-17, KI-18, KI-19, KI-24 and KI-26.
>
> Deviations from this spec in `test_ht_compression` (B3.7): `test_random_beacons_never_overflow_slot` feeds 100 random beacons, not 200 (200 wrap the queue, so evicted blocks make the "nothing lost" count impossible); and two tests were added, `test_slot_payload_is_reference_plus_compressed_packets` (replays the compressor per slot, the closest check to a round trip without the ground decompressor) and `test_positive_mask_update_follows_update_rate` (pins the `POCKET_PLUS_UPDATE_RATE` = 12 behaviour of compression.c). `test_identical_beacons_compress_to_minimum` asserts the exact maximum (97 beacons in 116 bytes) instead of "> 20".
>
> Deviations in `test_arq` (B3.6): the KI-20 test sends the DATA frame *before* asserting the state, so without the IGNORE line it reproduces the actual modulo by zero (UBSan abort at arq.c:539) instead of stopping at the first assertion; the KI-22 test uploads 500 B (not 300) so the overwrite reaches `ANTENNA_DEPLOYED_ADDR` and `BOOT_TIME_ADDR`, and presets the neighbours with non-erased values.

## Part A: Overview

### A1. What this batch covers (taken from the A7 table)

| A7 row | New suite | Already covered elsewhere | Added here |
|---|---|---|---|
| OBC task (`obc.c`) | `test_obc` | transitions themselves (`test_state_machine`) | `setup_obc()`: OBDH queue of 10, IWDG + 5 s health period, boot task sets, **boot time persisted**. `process_obc()` forwards exit bits, a 2 s idle wait, non-exit bits ignored |
| lora_cfg + timing | `test_lora_cfg` | range rejects and BUSY while PENDING_COMMIT (`test_tc_handler`: `TC_LORA_CONFIG_*`) | parse byte order, **commit at time**, immediate commit, **auto-revert**, RX cancels revert, BUSY during PENDING_REVERT, the values applied to the radio, BW mapping, **time-on-air golden values**, `ack_timeout_ms` formula, KI-18, KI-19 |
| beacon | `test_beacon` | `TC_SET_BEACON_PERIOD`, `TC_REQUEST_BEACON_NOW` (tc side only) | **19-byte layout**, **uptime**, **timer callback**, timer period change, **transmit feeds HT**, HT is fed even when the TX queue is full, KI-17, KI-15 at module level |
| EPS / ADCS / PAYLOAD | `test_subsystem_tasks` | pause protocol via `sim_tasks` (which only calls `tm_check_pause`) | the shared contract run through the **real** `process_eps/adcs/payload()`: PAUSE → ACK, deferral, RESUME runs deferred bits, 1 s idle. EPS thresholds read. **KI-6 as a KNOWN BUG test** |
| arq / saw | `test_saw`, `test_arq` | `arq_begin_dl`/`arq_arm_ul` calls from tc_handler (faked) | SAW ACK/NACK/RETX/timeout/CAD give-up/forwarding. Uplink: **TLE complete writes flash + notifies ADCS**, **CRC mismatch discarded**, reordering, duplicates, window ACKs. Downlink (stretch): happy path, lost block retransmitted, abort |
| ht_handling + real compression | `test_ht_compression` (new suite; `test_ht_handling` unchanged) | queue logic with the fake 20-byte compressor | **a real POCKET+ block fits a slot**: realistic, constant and random beacons, raw reference stored, `ht_init()` recovers after real blocks |

### A2. Dropped and why
- **`transceiver.c`**: `transceiver_task` is an endless loop with no `process_once()`, so it can't be tested without a production refactor. KB-2 stays documented only.
- **`comms.c` `comms_task`**: also an endless loop. The routing to `tc_process`/`arq_rx` is one line; both targets are tested directly.
- **`obc_task()` loop, `beacon_task()` loop, `xQueueCreate` failure in `setup_obc()`**: endless loops or `while(1)`. The tests drive the static `setup_*`/`process_*` functions instead (they are reachable because the `.c` is `#include`d). The `while(1)` on queue failure ends in an IWDG reset, which is acceptable fail-safe behaviour and not a KI.
- **Ground decompressor round-trip for POCKET+**: `decompression.cpp` isn't in this repo.
- **`time.c`, `clock.c`, `periph.c`, `main.c`**: HAL only, low value on host.
- **KI-7 (mock radio build)**: firmware build, not host.
- **Layer 3 (stdin simulator) and Layer 4 (on-target console)**: out of scope; listed under future work (A5).

### A3. New known-issue candidates (verify by removing the IGNORE line)

| ID | Where | Problem | Test |
|---|---|---|---|
| KI-17 | `beacon.c:43-46` + `obc.c:90-92` | Uptime = RTC now − boot time taken from the RTC. A `TC_UPLOAD_UNIX_TIME` after boot (e.g. RTC reset to 2000-01-01 on power loss, then synced to 2023) makes the uptime jump by decades. A backwards sync gives 0. Uptime should be monotonic (tick based). | `test_beacon: test_uptime_survives_rtc_resync` (ignored) |
| KI-18 | `lora_cfg.c:33-51` `validate()` + `timing.c:278-282` | `preamble_symbols` is never range-checked (and there's no lower bound on TX power). At SF12 with preamble > 32763, `(pre_sym*4+17)*sym_time_us` overflows uint32, so `lora_toa_ms` and every ACK/window timeout derived from it wrap to a small value. | `test_lora_cfg: test_toa_does_not_wrap_for_long_preamble` (ignored) |
| KI-19 | `timing.c:309` (also `arq.c:609-610`) | `ack_timeout_ms` uses the ToA of an 8-byte ACK, but an air ACK is 16 bytes (`air_encode_ack`: 5 + 3 + 8 tag). The window timeout likewise uses payload sizes (3+bsize, 12) instead of frame sizes (16+bsize, 25). Timeouts are underestimated; today the margins absorb it. Low. | `test_lora_cfg: test_ack_timeout_uses_real_ack_frame_size` (ignored) |
| KI-20 | `arq.c:460` + `arq.c:538-539` | `DATA_BEGIN` with `WINDOW_SIZE = 0` is accepted. The next `DATA` frame computes `ul_blocks_received % 0`: undefined behaviour (SIGFPE on host, silent garbage on target). The frame is authenticated, so this is a ground-mistake robustness issue. | `test_arq: test_UL_window_size_zero_is_rejected` (ignored; the IGNORE line must stay first or the binary crashes) |
| KI-21 | `arq.c:453-457` | `DATA_BEGIN` whose TRANSFER_TYPE differs from the armed one is only logged and the session proceeds, so e.g. calibration bytes are accepted on a TLE arm. | `test_arq: test_UL_type_mismatch_is_rejected` (ignored) |
| KI-22 | `arq.c:129-146`, `arq.c:567-568`, `flash.h:62-104` | The uplink length is bounded only by `ARQ_UL_BUF_MAX` (512), not by the destination region. A TLE (region 138 B at 0x08030020) or calibration upload longer than its region overwrites the neighbouring config, up to 0x08030220: calibration, telemetry, `ANTENNA_DEPLOYED_ADDR` 0x08030205 and `BOOT_TIME_ADDR` 0x08030206. | `test_arq: test_UL_TLE_longer_than_region_does_not_overwrite_neighbours` (ignored) |
| KI-23 | `arq.c:87`, `arq.c:118-119` | `TRANSFER_HK_HISTORY` downlinks 2 KB from `TELEMETRY_ADDR` (legacy area), never from the HT queue at `HT_BASE_ADDR`. The compressed historic telemetry built by `ht_handling.c` can't be downlinked. | `test_arq: test_DL_HK_history_reads_ht_queue` (ignored, stretch) |
| KI-24 | `saw.c:20`, `saw.c:92-99` | `saw_send` ignores `own_type` (`(void)own_type`) and matches ACK/NACK on REF_SEQ only. `arq_saw_frame` (arq.c:326-332) checks both. An ACK for another frame type with the same seq is accepted. Low. | `test_saw: test_ack_with_wrong_ref_type_is_not_accepted` (ignored) |
| KI-25 | `obc.c:112-121` | `N_OBC_HARD_REBOOT` / `N_OBC_SOFT_REBOOT` / `N_OBC_UPDATE_TIME` are empty `if` blocks, and `N_OBC_PERIPHERALS_REBOOT` isn't handled at all. The reboot TCs are ACKed but do nothing. This is unimplemented, not a crash. | documented by `test_obc: test_process_non_exit_bits_do_not_change_state` (passes) |
| KI-26 (optional) | `state_machine.c:105-126` `change_state()` | A transition to the current state (e.g. `TC_TRANSIT_TO_NM` while in NM) pauses and resumes every task and does 2 flash writes. Low. | `test_obc: test_exit_to_current_state_is_a_no_op` (ignored, optional) |

Also update existing rows:
- **KI-6**, Test column: `test_subsystem_tasks: test_adcs_private_mode_bits_match_notification_map` (ignored). Add a note: ADCS also never handles `N_ADCS_NEW_TLE` / `N_ADCS_NEW_CALIBRATION`.
- **KI-15**: add `test_beacon: test_set_period_zero_keeps_previous_period` (ignored).
- **KI-16**: add `test_arq: test_begin_dl_refused_while_ul_armed`, which confirms the -1 premise (passes).

### A4. Priorities (one implementation pass)
- **P1 (must):** shared-fake changes, `test_lora_cfg`, `test_obc`, `test_subsystem_tasks`, `test_beacon`, `fake_link` + `test_arq` uplink part.
- **P2 (should):** `test_saw`, `test_ht_compression`.
- **P3 (stretch, optional):** `test_arq` downlink tests (`test_DL_*`), KI-26.

### A5. Future work (out of scope)
- Layer 3: `test/sim/pocat_sim.c`, a stdin simulator over the Layer-2 fakes.
- Layer 4: `POCAT_DEBUG_CONSOLE` on target (needs KI-7 fixed first).
- Production refactor `transceiver_process_once()`, which would make KB-2 testable.

---

## Part B: Executor specification

### B0. Ground rules
- Don't modify `src/`, `include/` or `external/`. No commits or pushes.
- New files use LF line endings and the R5 Doxygen style (`test/suites/test_task_management.c` is the model):
  - file header with `@file`/`@brief`/`@date 2026-10-29`/`@details`, covering what is under test, how it is built (`#include` vs SOURCES), which fakes, and how to run it;
  - every test gets `@brief` + `@details` (scenario, and why it matters on the satellite);
  - KNOWN BUG tests also get `@bug KI-n (test/README.md)` with the expected and the actual behaviour.
- Known bugs: the first statement of the test body is `TEST_IGNORE_MESSAGE("KNOWN BUG KI-n: ...");`, and the real assertions stay below it.
- One `pocat_add_suite(...)` line per new suite. No `run_tests.sh` change. Never build the default target in `build_test/`.
- If an assertion fails for a reason not listed here, report it. Don't weaken the assertion.

### B1. Changes to shared fakes (additive only; existing suites must stay green)

**B1.1 `test/mocks/fake_rtos.h/.c`**: add
```c
/** @brief Returns the current period (ticks) of software timer @p t (last xTimerCreate / xTimerChangePeriod value). */
TickType_t fake_rtos_timer_period(TimerHandle_t t);
/** @brief Returns true if xTimerStart or xTimerChangePeriod was called on @p t. */
bool       fake_rtos_timer_active(TimerHandle_t t);
```
Implementation: `configASSERT(t != NULL); return t->period;` and `return t->active;`.

**B1.2 `test/mocks/fake_comms_env.c/.h`**
1. Mark these definitions weak with `__attribute__((weak))`, using the same `POCAT_WEAK` pattern as `stub_task_entries.c`: `g_last_tc_id`, `g_last_tc_rc`, `g_beacon_period_ms`, `beacon_set_period`, `arq_begin_dl`, `arq_arm_ul`. Reason: `test_beacon` compiles the real `beacon.c` and `test_arq` the real `arq.c`; their strong definitions then win at link time. Document this in the header table, plus: "when the real module is linked, `fake_beacon_*` / `fake_arq_*` recorders stay at 0". `fake_comms_env_reset()` keeps writing the 3 globals, which works on whichever definition won.
2. Record the radio configuration (only for `test_lora_cfg`):
   ```c
   /** @brief Last values passed to the RadioLib configuration functions. */
   typedef struct {
       int      standby_calls;
       uint32_t freq_hz;                                   /* RadioLib_SetChannel */
       uint8_t  tx_sf, tx_cr, tx_bw; int8_t tx_power; int tx_iq, tx_crc; uint16_t tx_preamble;  /* SetTxConfig */
       uint8_t  rx_sf, rx_cr, rx_bw;                  int rx_iq, rx_crc; uint16_t rx_preamble;  /* SetRxConfig */
   } fake_radio_cfg_t;
   fake_radio_cfg_t fake_radio_last_config(void);
   ```
   Reset to all zeros in `fake_comms_env_reset()`. `fake_radio_config_count()` is unchanged.
3. Acceptance check after B1: `test_tc_handler` and all other existing suites give exactly the same results.

**B1.3 New `test/mocks/fake_link.h/.c`**: a scripted SX1262 for the asynchronous TX/RX/IRQ/CAD API. It implements only `RadioLib_StartTransmit`, `RadioLib_StartReceive`, `RadioLib_GetIrqFlags`, `RadioLib_ClearIrqFlags`, `RadioLib_ReadRxData`, `RadioLib_ScanChannel`. `Standby/SetChannel/SetTx/SetRxConfig` stay in `fake_comms_env.c`, so always link both.
```c
#define FAKE_LINK_MAX_TX   64      /* transmitted frames kept */
#define FAKE_LINK_MAX_INJ   4      /* injected (unsolicited) RX frames */
/** Fake ground station: called with the last transmitted frame; fill reply and return true to answer. */
typedef bool (*fake_link_responder_t)(const uint8_t *tx, uint8_t tx_len, uint8_t *reply, uint8_t *reply_len);
void    fake_link_reset(void);                    /* no responder, scan = RADIOLIB_CHANNEL_FREE, tx_done on, logs cleared, irq 0 */
void    fake_link_set_responder(fake_link_responder_t fn);
void    fake_link_set_scan_result(int16_t r);     /* RADIOLIB_CHANNEL_FREE (-15) or RADIOLIB_LORA_DETECTED (-702) */
void    fake_link_set_tx_done(bool fires);        /* false: TX_DONE never fires (TX wait times out) */
void    fake_link_inject_rx(const uint8_t *frame, uint8_t len);   /* FIFO, delivered before the responder */
int     fake_link_tx_count(void);
uint8_t fake_link_tx_frame(int i, uint8_t *out);  /* copies frame i (0 = first) into out[255], returns its length; bad i fails the test */
int     fake_link_scan_count(void);
int     fake_link_start_receive_count(void);
```
Behaviour (deterministic, single-threaded; "notify" means `xTaskNotify(xTaskGetCurrentTaskHandle(), N_TRANSCEIVER_RADIO_IRQ_BIT, eSetBits)`):
- **StartTransmit(buf, len):** log a copy, `tx_serial++`. If tx_done: `irq |= RADIOLIB_SX126X_IRQ_TX_DONE` and notify. Return `RADIOLIB_ERR_NONE`. The production code calls `xTaskNotifyStateClear(NULL)` before this, so the post must happen inside StartTransmit.
- **StartReceive(pre):** count it. If an injected frame is queued, pop it into `rx_buf`. Otherwise, if a responder is set and `tx_serial != answered_serial`, set `answered_serial = tx_serial` and call the responder with the last TX frame; if it returns true, copy the reply into `rx_buf`. If an `rx_buf` was loaded: `irq |= RX_DONE` and notify. At most **one** RX per call. Each TX is answered **at most once**, so repeated StartReceive calls without a new TX deliver nothing.
- **GetIrqFlags** returns `irq`. **ClearIrqFlags(m)** does `irq &= ~m`.
- **ReadRxData:** copy `rx_buf` (up to bufSize), `*outLen = len`, rssi = -80, snr = 5 (each only if the pointer is non-NULL), clear rx_buf, return `RADIOLIB_ERR_NONE`. If nothing is loaded: `*outLen = 0`.
- **ScanChannel:** count it, return `scan_result`.

Header doc: explain that this replaces the SX1262 + DIO1 IRQ, that the responder plays the ground station, and give a usage example (ACK responder built with `air_encode_ack`).

### B2. `test/CMakeLists.txt` lines (append after `test_tc_handler`)
```cmake
pocat_add_suite(test_lora_cfg LABEL comms                 # #includes lora_cfg.c; timing.c linked
  INCLUDES ${CMAKE_SOURCE_DIR}/external/RadioLib/src
  SOURCES ${SRC_DIR}/subsystems/comms/timing.c mocks/fake_comms_env.c)
pocat_add_suite(test_obc LABEL obc                        # #includes obc.c, state_machine.c, task_management.c, health.c
  SOURCES ${SRC_DIR}/utils/notifications.c mocks/stub_task_entries.c support/sim_tasks.c
          mocks/fake_clock.c mocks/fake_obdh_requests.c)
pocat_add_suite(test_subsystem_tasks LABEL subsys         # #includes eps.c, adcs.c, payload.c, task_management.c, health.c
  SOURCES ${SRC_DIR}/utils/notifications.c mocks/stub_task_entries.c mocks/fake_obdh_requests.c)
pocat_add_suite(test_beacon LABEL comms                   # #includes beacon.c, ht_handling.c, lora_cfg.c, task_management.c, health.c
  INCLUDES ${CMAKE_SOURCE_DIR}/external/RadioLib/src
  SOURCES ${SRC_DIR}/utils/notifications.c ${SRC_DIR}/subsystems/comms/frame.c ${SRC_DIR}/subsystems/comms/auth.c
          mocks/stub_task_entries.c mocks/fake_obdh_requests.c mocks/fake_comms_env.c mocks/mock_pocket_plus.c)
pocat_add_suite(test_saw LABEL comms                      # #includes saw.c, lora_cfg.c
  INCLUDES ${CMAKE_SOURCE_DIR}/external/RadioLib/src
  SOURCES ${SRC_DIR}/subsystems/comms/timing.c ${SRC_DIR}/subsystems/comms/cad.c
          ${SRC_DIR}/subsystems/comms/frame.c ${SRC_DIR}/subsystems/comms/auth.c
          mocks/fake_comms_env.c mocks/fake_link.c)
pocat_add_suite(test_arq LABEL comms                      # #includes arq.c, lora_cfg.c, task_management.c, health.c
  INCLUDES ${CMAKE_SOURCE_DIR}/external/RadioLib/src
  SOURCES ${SRC_DIR}/utils/notifications.c ${SRC_DIR}/subsystems/comms/timing.c ${SRC_DIR}/subsystems/comms/cad.c
          ${SRC_DIR}/subsystems/comms/frame.c ${SRC_DIR}/subsystems/comms/auth.c
          mocks/stub_task_entries.c mocks/fake_obdh_requests.c mocks/fake_comms_env.c mocks/fake_link.c)
pocat_add_suite(test_ht_compression LABEL comms           # #includes ht_handling.c; real POCKET+ (compression.c) linked
  SOURCES ${SRC_DIR}/utils/compression.c mocks/fake_obdh_requests.c)
```
Notes:
- `timing.c`, `cad.c`, `frame.c` and `auth.c` are stateless helpers, so they go in SOURCES (R1).
- `compression.c` goes in SOURCES even though it has statics. Justify in the file header: it is third-party ESA code tested only through its public API, and `startPocket()` resets its state.
- `test_ht_compression` uses the **real** `include/utils/flash.h` (no `stubs_project`). Its HT region is the same: 0x080FE000, 8 KB, 64 slots.
- New ctest label `subsys`: add it to the README label list.

### B3. Suites

#### B3.1 `test/suites/test_lora_cfg.c` (P1)
- `#include "lora_cfg.c"`. `timing.c` is linked.
- Fakes: `fake_comms_env` (clock via `fake_time_set_now`, radio recorder).
- **setUp:** `fake_rtos_reset(); fake_hal_reset(); fake_comms_env_reset(); lora_cfg_init();`. `s_revert_deadline_unix` doesn't need a reset because the state is IDLE.
- **tearDown:** R3.
- **Helpers:**
  - `make_cfg(LoraConfig_t*)`: valid config. id 2, 869 MHz, SF9, bw 0, cr 1, 14 dBm, preamble 16, crc 1, iq 0, sync 0x12, commit 0, revert 0.
  - `activate(sf, bw, cr, pre, crc)`: request with commit 0, revert 0, then tick; asserts both return OK/true.
- **Tests:**
  1. `test_init_sets_boot_defaults`: id 1, SF10, 125 kHz, CR1, preamble 64, CRC 1; `lora_cfg_tick()` returns false and the radio isn't touched.
  2. `test_parse_decodes_big_endian_fields`: 21-byte body with distinct values (freq 869 000 000, preamble 0x0102, commit 0x0A0B0C0D, revert 0x0E0F, power byte 0xFD → -3); every field decoded; returns OK.
  3. `test_request_stages_without_touching_radio`: PENDING_COMMIT, `fake_radio_config_count()==0`.
  4. `test_tick_commits_immediately_when_commit_at_is_zero`: tick returns true. The radio gets exactly the candidate (check `fake_radio_last_config()`: freq, tx/rx SF/CR/BW/power/preamble/crc/iq). `lora_cfg_current_id()==2`, state IDLE (revert 0).
  5. `test_tick_waits_until_commit_time`: commit_at = now+60. Ticks at now and now+59 return false with no radio change; now+60 returns true.
  6. `test_auto_revert_after_deadline_without_rx`: revert 30, commit now → PENDING_REVERT. now+29 → false. now+30 → true; id back to 1, radio re-applied with SF10/preamble 64; IDLE.
  7. `test_rx_after_commit_cancels_revert`: `lora_cfg_notify_rx()` → IDLE; tick at the deadline is false; id stays 2.
  8. `test_notify_rx_while_pending_commit_does_not_cancel_commit`: still PENDING_COMMIT; later commit works.
  9. `test_request_busy_during_revert_window`: in PENDING_REVERT, a second request returns `LORA_CFG_BUSY` (tc_handler only covers PENDING_COMMIT).
  10. `test_request_accepted_again_after_commit_completes`.
  11. `test_bw_code_maps_to_hz`: codes 0/1/2 give 125000/250000/500000 via `lora_cfg_active_bw_hz()`.
  12. `test_snapshot_returns_committed_config`: snapshot equals the committed config and not the pending candidate.
  13. `test_toa_golden_default_sf10`: at boot defaults (SF10, BW125, CR1, preamble 64, CRC on, explicit header) `lora_toa_ms(8)==707` and `lora_toa_ms(16)==789`.
  14. `test_toa_golden_sf7_matches_semtech_calculator`: activate SF7/bw0/cr1/pre 8/crc 1; `lora_toa_ms(10)==42` (Semtech: 41.22 ms, rounded up).
  15. `test_toa_golden_sf12_uses_low_data_rate_optimisation`: SF12/bw0/cr1/pre 8/crc 1; `lora_toa_ms(10)==992` (Semtech: 991.23 ms, DE=1).
  16. `test_ack_timeout_formula_at_defaults`: `ack_timeout_ms(16) == 2*789 + 707 + 200 == 2485`.
  17. KNOWN BUG `test_ack_timeout_uses_real_ack_frame_size` (KI-19): expect `ack_timeout_ms(16) == 2*lora_toa_ms(16) + lora_toa_ms(16) + 200` (2567); the actual value is 2485.
  18. KNOWN BUG `test_toa_does_not_wrap_for_long_preamble` (KI-18): activate SF12/bw0/cr1/**pre 40000**/crc 1 (validate accepts it); expect `lora_toa_ms(16) > 1300000` (preamble alone ≈ 1 310 859 ms); the actual value is ≈ 237 000 because of uint32 wrap.

Hand-calculation for the reviewer: SF10 symbol = 8192 µs; preamble = (64·4+17)·8192/4 = 559 104 µs; n=8: ceil(68/40)=2 → 10 + 8 = 18 symbols → 706 560 µs → 707 ms; n=16: ceil(132/40)=4 → 20 + 8 = 28 symbols → 788 480 µs → 789 ms.

#### B3.2 `test/suites/test_obc.c` (P1)
- `#include "obc.c"`, `"state_machine.c"`, `"task_management.c"`, `"health.c"`.
- **Suite-local definitions** (no production file defines them in this build):
  - `QueueHandle_t obdh_queue_handle;` (normally in obdh.c);
  - `static uint32_t fake_now; uint32_t time_get_unix(void) { return fake_now; }`.
- **setUp:** as `test_state_machine` (fake_rtos/hal/clock/obdh resets, `sim_tasks_reset`, task table + health statics reset, `health_init()`, block hook `sim_tasks_run_pending`), plus `obdh_queue_handle = NULL; fake_now = 1700000000u;`.
- **Helper** `obc_task_handle()`: creates `xTaskCreate(obc_task, "OBC", OBC_STACK_SIZE, NULL, OBC_PRIORITY, &h)` (never runs) and makes it current with `fake_rtos_set_current_task(h)`.
- **Tests:**
  1. `test_setup_creates_obdh_queue_for_10_requests`: after `setup_obc(OBC_STATE_NM)`, 10 `xQueueSend(obdh_queue_handle, &(obdh_request){0}, 0)` return pdTRUE and the 11th returns errQUEUE_FULL.
  2. `test_setup_registers_iwdg_and_5s_health_period`: `iwdg_handle == &hiwdg`, `period_ticks == 5000`.
  3. `test_setup_NM_creates_all_tasks_running`: 7 tasks created, none paused, `health_get_expected()` has all 7 HEALTH bits.
  4. `test_setup_SM_creates_payload_adcs_beacon_paused`: paused set = PAYLOAD|ADCS|BEACON; those health bits are absent.
  5. `test_setup_persists_boot_time`: the image at `BOOT_TIME_ADDR` (memcpy 4 B) equals `fake_now`; the log has one `FAKE_OBDH_WRITE` with addr `BOOT_TIME_ADDR`, len 4. Matters because beacon uptime depends on it.
  6. `test_setup_persists_boot_time_even_if_task_creation_fails`: `fake_rtos_fail_task_create_at(3)`; setup returns (no hang) and the boot time is still written. Documents degraded boot.
  7. `test_process_forwards_exit_bit_to_state_machine`: `setup_obc(NM)`, make OBC current, `xTaskNotify(obc, N_OBC_EXIT_STATE_TO_OBC_STATE_SM, eSetBits)`, `obc_state_t s = OBC_STATE_NM; process_obc(&s);` → `s == OBC_STATE_SM`, the flash byte at `CURRENT_STATE_ADDR` is SM, the fake clock switched to 2 MHz once.
  8. `test_process_without_notification_waits_2s_and_keeps_state`: tick increases by 2000, `s` unchanged, no new flash calls.
  9. `test_process_non_exit_bits_do_not_change_state`: UPDATE_TIME|HARD_REBOOT|SOFT_REBOOT|PERIPHERALS_REBOOT → no state change, no flash access, no clock change. Add `@note KI-25`: these are unimplemented.
  10. `test_process_drains_notification_in_one_cycle`: send SM|UPDATE_TIME; process (→ SM); a second process returns after 2000 ticks with no further change.
  11. (optional, KI-26) KNOWN BUG `test_exit_to_current_state_is_a_no_op`: in NM, send the NM exit bit; expect no PAUSE sent to any task (`fake_rtos_bits_sent_to(handle) & N_TASK_PAUSE == 0`) and no flash write.

#### B3.3 `test/suites/test_subsystem_tasks.c` (P1)
- **Build:** the three task files are included in one translation unit. Each defines `static uint32_t deferred_notifications;`, so rename it per include:
  ```c
  #define deferred_notifications eps_deferred
  #include "eps.c"
  #undef  deferred_notifications
  #define deferred_notifications adcs_deferred
  #include "adcs.c"
  #undef  deferred_notifications
  #define deferred_notifications payload_deferred
  #include "payload.c"
  #undef  deferred_notifications
  #include "task_management.c"
  #include "health.c"
  ```
  Explain the trick in the file header. The real `eps_task/adcs_task/payload_task` override the weak stubs.
- **setUp:** fake resets, `fake_obdh_reset()`, task table/health reset + `health_init()`, then `setup_eps(); setup_adcs(); setup_payload();` (they zero the deferred words) and `tm_create_tasks(TM_TASK_ALL, 0)`.
- **Contract table** (file-static):
  ```c
  typedef struct { const char *name; uint32_t tm_bit; EventBits_t health_bit; void (*process)(void); uint32_t *deferred; uint32_t own_bit; } contract_t;
  /* EPS: N_EPS_ENABLE_AUTO_HEAT; ADCS: N_ADCS_NEW_TLE; PAYLOAD: N_PAYLOAD_ACTIVATE (any task bit with no flash side effect) */
  ```
- **Helpers:**
  - `run_as(const contract_t *c)`: current = the task handle, call `c->process()`, restore the test task.
  - Block hook `run_real_tasks()`: for each of the 3 contracts whose handle has `fake_rtos_notify_pending(h, 0)`, call `run_as`. It must check pending first, otherwise `process_*` would wait 1000 ticks.
- **Tests:** each contract test loops over the 3 entries and uses `TEST_ASSERT_*_MESSAGE(..., c->name)`.
  1. `test_contract_pause_acks_and_marks_paused`: notify PAUSE, `run_as` → `task_table[idx].paused`, ACK bit set in `task_event_group_handle`, health bit removed from expected.
  2. `test_contract_paused_task_defers_own_bits`: after pause, notify `own_bit`, `run_as` → `*c->deferred & own_bit`, still paused.
  3. `test_contract_resume_clears_deferred_and_restores_health`: then RESUME, `run_as` → not paused, `*deferred == 0`, health bit expected again, ACK set.
  4. `test_contract_idle_cycle_returns_after_1s`: no notification → tick +1000 (this sets the health-kick cadence, < 5 s).
  5. `test_real_tasks_complete_pause_handshake`: hook = `run_real_tasks`; `tm_pause_tasks(TM_TASK_EPS|TM_TASK_ADCS|TM_TASK_PAYLOAD)` returns without timeout (tick < 10000) and all 3 are paused; then `tm_resume_tasks` of the same mask unpauses them all.
  6. `test_eps_new_thresholds_reads_3_bytes_from_flash`: notify `N_EPS_NEW_THRESHOLDS`, `run_as(EPS)` → one `FAKE_OBDH_READ` at `EPS_THRESHOLDS_ADDR`, len 3.
  7. `test_eps_threshold_request_while_paused_is_served_on_resume`: pause; thresholds bit (no flash call yet); resume → the read happens in the resume cycle.
  8. `test_eps_heater_bits_touch_no_flash`: ENABLE/DISABLE_AUTO_HEAT → `fake_obdh_call_count()==0`.
  9. `test_eps_threshold_read_failure_does_not_crash`: `fake_obdh_inject_error(HAL_ERROR)`; the cycle completes.
  10. KNOWN BUG `test_adcs_private_mode_bits_match_notification_map` (KI-6): `TEST_ASSERT_EQUAL_HEX32(N_ADCS_DESIRED_STATE_DETUMBLING, ADCS_DETUMBLING_MODE); TEST_ASSERT_EQUAL_HEX32(N_ADCS_DESIRED_STATE_NADIR, ADCS_NADIR_POINTING_MODE);`. The private macros are visible because adcs.c is included. `@bug`: IDLE (bit 0) triggers detumble.

#### B3.4 `test/suites/test_beacon.c` (P1)
- `#include "beacon.c"`, `"ht_handling.c"`, `"lora_cfg.c"`, `"task_management.c"`, `"health.c"`.
- Linked: frame.c, auth.c, notifications.c, `fake_comms_env` (weak beacon globals are overridden by beacon.c), `fake_obdh_requests`, `mock_pocket_plus` (deterministic 20 B per compressed beacon, so 5 beacons per block; the 6th flushes).
- **setUp:**
  1. fake resets and task/health statics, `health_init()`;
  2. `fake_comms_env_reset()` (resets `g_last_tc_*`, `g_beacon_period_ms`);
  3. `beacon_timer = NULL;`;
  4. `lora_cfg_init();`;
  5. `fake_obdh_reset(); ht_init(); fake_obdh_reset();`: `ht_init` zeroes the HT statics on erased flash; the second reset clears its 64-read log;
  6. `tm_create_tasks(TM_TASK_ALL, 0)`.
- **Helpers:**
  - `preset_flash(state, boot_time)`: `fake_obdh_flash(CURRENT_STATE_ADDR)[0] = state; memcpy(fake_obdh_flash(BOOT_TIME_ADDR), &boot, 4)`;
  - `be32(const uint8_t*)`.
- **Tests:**
  1. `test_body_is_19_bytes_with_spec_layout`: now 1700000000; state CM; boot = now−3600; sensors (25 °C, 3300 mV); `g_last_tc_id=0x05`, `g_last_tc_rc=0x02`. `build_beacon_body` returns 19. Bytes: [0]=AIR_BODY_VER, [1..4]=BE epoch, [5]=OBC_STATE_CM, [6]=0x3F, [7]=25, [8]=0, [9]=33, [10]=0, [11]=0, [12]=0x05, [13]=0x02, [14]=1 (CFG_ID), [15..18]=BE 3600.
  2. `test_uptime_is_zero_when_boot_time_not_written`: erased BOOT_TIME (0xFFFFFFFF) → uptime 0.
  3. `test_negative_temperature_is_twos_complement`: −10 °C → body[7] == 0xF6.
  4. `test_vdda_is_truncated_to_100mv`: 3399 mV → 33.
  5. `test_cfg_id_follows_committed_lora_config`: request + tick a config with id 7 → body[14] == 7.
  6. KNOWN BUG `test_uptime_survives_rtc_resync` (KI-17): boot = 946684800 (RTC reset to 2000-01-01); `fake_rtos_advance_ticks(100000)`; `fake_time_set_now(1700000000)` (the sync TC). Expect uptime == 100 (= ticks/1000); the actual value is 753 315 200.
  7. `test_timer_callback_requests_beacon_from_beacon_task`: `beacon_timer_cb(NULL)` → `fake_rtos_bits_sent_to(BEACON handle) == N_COMMS_TRANSMIT_BEACON`, and nobody else is notified.
  8. `test_timer_callback_before_task_exists_is_safe`: clear the task table first → no notification, no crash.
  9. `test_set_period_changes_running_timer`: create the timer as `beacon_task` does (`beacon_timer = xTimerCreate("beacon", pdMS_TO_TICKS(g_beacon_period_ms), pdTRUE, NULL, beacon_timer_cb)`). `beacon_set_period(60000)` → `fake_rtos_timer_period == 60000`, active, `g_beacon_period_ms == 60000`. `fake_rtos_fire_timer` → beacon task notified.
  10. `test_set_period_before_timer_only_stores_value`: `beacon_timer == NULL` → only the global changes.
  11. KNOWN BUG `test_set_period_zero_keeps_previous_period` (KI-15): expect the period to stay 30000; the actual value is 0.
  12. `test_transmit_queues_unacked_beacon_frame`: `transmit_beacon()` → 1 queue item; `air_decode` succeeds: type AIR_BEACON, len 19, payload == the `build_beacon_body` output; `needs_ack == 0`; transceiver got `N_TRANSCEIVER_TX_READY_BIT`.
  13. `test_transmit_feeds_historic_telemetry`: `ht_block_count == 1`, `ht_block_used == 20`, `ht_block[12..30]` == body, `ht_block[31] == 0` (pad).
  14. `test_sixth_beacon_flushes_block_to_ht_slot0`: 6 transmits; drain the TX queue with `xQueueReceive` after each, because `COMMS_QUEUE_LEN` is only 2. Exactly one `FAKE_OBDH_ERASE_PROGRAM` at `HT_BASE_ADDR`, len 128; slot header format 0x03, count 5.
  15. `test_beacon_archived_even_when_tx_queue_full`: fill the TX queue with 2 entries; transmit → the queue still has 2, the tick advanced 100, no TX_READY, but `ht_block_count == 1`. History must not depend on the downlink.

#### B3.5 `test/suites/test_saw.c` (P2)
- `#include "saw.c"`, `"lora_cfg.c"`. Linked: timing, cad, frame, auth, fake_comms_env, fake_link.
- **setUp:** `fake_rtos_reset(); fake_hal_reset(); fake_comms_env_reset(); fake_link_reset(); lora_cfg_init(); srand(1); rx_q = xQueueCreate(COMMS_QUEUE_LEN, sizeof(RxAirFrame_t));`. The test task is current: saw waits on the current task's notifications, and fake_link notifies the current task.
- **Helpers:**
  - `make_tm(seq, buf)`: `air_encode(buf, AIR_TM, AIR_FLAG_REQUIRES_ACK, seq, payload8, 8)`;
  - responders `ack_responder` (decodes TX with `air_decode`, replies `air_encode_ack(reply, 0x90, f.type, f.seq)`), `nack_responder`, `ack_second_try_responder` (static counter);
  - `encode_ack_raw(ref_type, ref_seq, buf)`.
- **Tests:**
  1. `test_ack_on_first_attempt_returns_ok`: `SAW_OK`, tx_count 1, `tx[2] & AIR_FLAG_IS_RETX == 0`.
  2. `test_nack_returns_nacked`.
  3. `test_no_reply_retransmits_3_times_then_times_out`: `SAW_TIMEOUT`, tx_count 4, frames 1..3 (0-based) have IS_RETX and frame 0 doesn't. Tick ≥ 4 × `ack_timeout_ms(len)`.
  4. `test_ack_on_second_attempt_returns_ok`: tx_count 2.
  5. `test_retransmitted_frame_still_authenticates`: every logged TX decodes with `air_decode` == 0.
  6. `test_foreign_frame_during_wait_is_forwarded_to_comms`: inject an authenticated AIR_TC, then the ACK responder. → `SAW_OK`, rx_q has 1 item with type AIR_TC, rssi −80, snr 5.
  7. `test_ack_for_other_seq_is_forwarded_not_accepted`: inject an ACK with the wrong seq, then the right one → OK, rx_q has 1 item.
  8. `test_corrupted_reply_is_dropped`: inject a tampered frame (flip a payload byte) → not forwarded; then the ACK → OK.
  9. `test_cad_busy_gives_up_without_transmitting`: scan result `RADIOLIB_LORA_DETECTED` → `SAW_CAD_FAIL`, tx_count 0, scan_count 4.
  10. `test_missing_tx_done_still_waits_for_ack`: `fake_link_set_tx_done(false)` + ACK responder → OK, tick ≥ 5000.
  11. KNOWN BUG `test_ack_with_wrong_ref_type_is_not_accepted` (KI-24): inject an ACK with the right seq but ref_type AIR_BEACON, no responder. Expect `SAW_TIMEOUT`; the actual result is `SAW_OK`.

#### B3.6 `test/suites/test_arq.c` (P1 uplink, P3 downlink)
- `#include "arq.c"`, `"lora_cfg.c"`, `"task_management.c"`, `"health.c"`. Linked as in B2. The real `arq_begin_dl/arq_arm_ul` override fake_comms_env's weak ones.
- **setUp:** fake resets (rtos, hal, obdh, comms_env, link), task/health statics + `health_init()`, `memset(&g_sess, 0, sizeof g_sess)`, `lora_cfg_init()`, `srand(1)`, `tm_create_tasks(TM_TASK_ALL, 0)`, `rx_q = xQueueCreate(...)`.
- **Frame builders** (fill an `AirFrame_t` directly; `arq_rx` takes decoded frames). Layouts follow the code, not the spec:
  - `ul_begin(sid, type, total_blocks, bsize, wsz, total_bytes)`: payload `[AIR_BODY_VER, sid, type, tb_hi, tb_lo, bsize, wsz, tbytes BE32]`, len 11;
  - `ul_data(sid, idx, data, n)`: `[sid, idx_hi, idx_lo, data...]`, len 3+n;
  - `ul_end(sid, status, crc)`: `[sid, status, crc BE32]`, len 6.
- **Other helpers:**
  - `ref_crc32(buf, n)`: an independent bitwise CRC-32/ISO-HDLC in the test file;
  - `pop_data_ack(&f)`: `xQueueReceive` the TX queue + `air_decode`, assert type AIR_DATA_ACK, len 12. The queue holds only 2: pop after every step that queues.
- **Uplink tests (P1):**
  1. `test_crc32_known_answer`: `crc32_finish(crc32_update(0xFFFFFFFF, "123456789", 9)) == 0xCBF43926`.
  2. `test_data_begin_without_arm_is_ignored`: returns 0, IDLE, nothing queued.
  3. `test_data_begin_after_arm_activates_and_sends_data_ack`: ARQ_UL_ACTIVE, DATA_ACK `[sid,0,0,8,0×8]`, needs_ack 0, transceiver TX_READY.
  4. `test_data_begin_too_short_stays_armed`: len 10.
  5. `test_UL_TLE_complete_writes_tle_and_notifies_adcs`: 138 B pattern in 2 blocks (bsize 100: 100 + 38), wsz 16; END with the correct CRC returns 1. Flash at `TLE_ADDR`[0..137] == pattern; ADCS received `N_ADCS_NEW_TLE`; `arq_active()` false.
  6. `test_UL_crc_mismatch_discards_upload`: CRC^1 → returns 0, no `FAKE_OBDH_WRITE`, ADCS not notified, IDLE.
  7. `test_UL_out_of_order_blocks_are_reassembled`: 3 blocks sent 2, 1, 0 → `ul_rx_base == 3`; CRC OK; data in order.
  8. `test_UL_duplicate_block_is_counted_once`: block 1 twice → `ul_blocks_received == 1`.
  9. `test_UL_data_for_other_session_is_ignored`.
  10. `test_UL_block_index_beyond_total_is_ignored`.
  11. `test_UL_abort_by_sender_returns_idle_without_writing`.
  12. `test_UL_data_ack_sent_after_each_window`: wsz 2, 4 blocks → a DATA_ACK after blocks 2 and 4 (pop each), the last with base 4.
  13. `test_UL_ADCS_CAL_writes_calibration_and_notifies_adcs`: `CALIBRATION_ADDR`, `N_ADCS_NEW_CALIBRATION`.
  14. `test_UL_flight_params_truncated_to_8_bytes`: arm `TRANSFER_FLIGHT_PARAMS`, 20 B → one write at `RFI_CONFIG_ADDR` with len 8.
  15. `test_begin_dl_refused_while_ul_armed`: returns −1 (supports KI-16).
  16. `test_arm_ul_refused_while_dl_active`: state stays `ARQ_DL_ACTIVE`, session id unchanged.
  17. KNOWN BUG `test_UL_window_size_zero_is_rejected` (KI-20): IGNORE line first. BEGIN with wsz 0 → expect `g_sess.state == ARQ_UL_ARMED` (rejected); then a DATA must not crash.
  18. KNOWN BUG `test_UL_type_mismatch_is_rejected` (KI-21): arm TLE, BEGIN type ADCS_CAL → expect still ARMED and no DATA_ACK queued.
  19. KNOWN BUG `test_UL_TLE_longer_than_region_does_not_overwrite_neighbours` (KI-22): TLE of 300 B (2 × 150) with a valid CRC → expect `fake_obdh_flash(CALIBRATION_ADDR)[0] == 0xFF` and `BOOT_TIME_ADDR` untouched (preset a known value).
- **Downlink tests (P3, optional).** The fake ground-station responder (static state) does this per decoded TX:
  - DATA_BEGIN or DATA_END: reply `air_encode_ack(.., f.type, f.seq)`;
  - DATA: store the block by index (optionally drop index `drop_idx` once), then reply DATA_ACK `[sid, base_hi, base_lo, 8, bitmap...]`. Base is the lowest missing index; bitmap bit k = block base+1+k received.

  It replies to the last DATA of a window because production calls StartReceive only after the window.
  20. `test_DL_HK_history_happy_path`: preset 2048 B pattern at `TELEMETRY_ADDR`; `arq_begin_dl(7, TRANSFER_HK_HISTORY, NULL, 0)` → transceiver got `N_TRANSCEIVER_ARQ_DL_BIT`; `arq_run_dl(rx_q)` → 11 DATA frames (last 48 B), reassembled == pattern, the DATA_END CRC == `ref_crc32(pattern, 2048)`, status COMPLETE_OK, IDLE.
  21. `test_DL_lost_block_is_retransmitted_with_retx_flag`: `drop_idx = 3` → block 3 sent twice, the second with IS_RETX; final CRC OK.
  22. `test_DL_missing_data_ack_aborts_after_max_retx`: the responder never answers DATA → 4 × 11 DATA TX, then DATA_END with status `DATA_END_ABORTED_BY_SENDER`; IDLE.
  23. `test_DL_unacked_data_begin_aborts_without_data`: no responder → DATA_BEGIN sent 4 times, 0 DATA, IDLE.
  24. KNOWN BUG `test_DL_HK_history_reads_ht_queue` (KI-23): the first `FAKE_OBDH_READ` address is expected in `[HT_BASE_ADDR, HT_BASE_ADDR+HT_REGION_SIZE)`; the actual address is `TELEMETRY_ADDR`.

#### B3.7 `test/suites/test_ht_compression.c` (P2)
- `#include "ht_handling.c"`. Linked: the real `compression.c` and `fake_obdh_requests`. Real `flash.h`.
- **setUp:** `fake_obdh_reset(); ht_init(); fake_obdh_reset();`.
- **Helpers:**
  - `make_beacon(i, out19)`: same layout as `build_beacon_body`. epoch = 1700000000 + 30·i; state 1; 0x3F; temp 20 + (i % 6); vdda 33; tc id/rc constant; cfg 1; uptime 30·i;
  - `slot_hdr(i)`: memcpy the header from `fake_obdh_flash(HT_BASE_ADDR + i*128)`;
  - deterministic LCG `rnd()` for random beacons.
- **Tests:**
  1. `test_real_pocket_block_fits_slot`: 300 realistic beacons. Every stored slot (0..`ht_queue.current_ht_count`−1 from the reading pointer) has format 0x03, `20 ≤ data_len ≤ 116`, `1 ≤ count ≤ HT_SLOT_MAX_BEACONS`. No ASan/UBSan report.
  2. `test_real_pocket_packs_several_beacons_per_block`: same feed → every stored block has count ≥ 2. Print the counts with printf. If this fails, report it as a finding; don't lower the threshold.
  3. `test_reference_beacon_is_stored_raw`: slot 0 payload[0..18] == beacon 0, payload[19] == 0.
  4. `test_identical_beacons_compress_to_minimum`: 200 identical beacons → the first block count is > 20 and ≤ `HT_SLOT_MAX_BEACONS`, data_len ≤ 116.
  5. `test_random_beacons_never_overflow_slot`: 200 random bodies → all headers valid, data_len ≤ 116 (compression may expand; the code must start a new block instead).
  6. `test_ht_init_recovers_queue_after_real_blocks`: after test-1 style feeding, save `ht_queue`, call `ht_init()` → same count, reading_pointer, writing_pointer, next_seq.
  7. (optional) `test_queue_wraps_with_real_blocks`: feed until > 64 blocks are flushed → count == 64; slot headers keep strictly increasing seq from the reading pointer.

### B4. Implementation steps (each step leaves the build green)
0. Baseline: `docker run --rm -v "${PWD}:/app" pocat-tests` and record the results (`test_ht_handling` has 2 expected failures).
1. B1.1 + B1.2 (timer accessors, weak defs, radio recorder). Re-run: identical results.
2. `test_lora_cfg` + CMake line.
3. `test_obc` + line.
4. `test_subsystem_tasks` + line.
5. `test_beacon` + line.
6. B1.3 `fake_link`, then `test_saw` + line.
7. `test_arq` uplink tests + line; then downlink tests if time permits.
8. `test_ht_compression` + line.
9. `test/README.md`:
   - Suites table: 7 rows;
   - Directory Structure: add `fake_link`, mention the weak symbols in `fake_comms_env`;
   - label list: add `subsys`;
   - Fakes section: `fake_rtos_timer_period/active`;
   - KI table: KI-17..KI-26 + KI-6/15/16 updates.

   In `docs/testing/test-design.md`, add one status line pointing to this file. Optionally add the new suites and the `subsys` label to the CLAUDE.md test section.

### B5. Acceptance criteria (tester)
- **AC1.** A clean build of `pocat_tests`; no warnings from new test files (warnings inside `src/` files such as compression.c are acceptable and should be listed).
- **AC2.** `ctest -N` lists 15 suites. All new suites pass with only these IGNOREs:
  - `test_lora_cfg`: 2 (KI-18, KI-19);
  - `test_obc`: 0, or 1 if KI-26 is done;
  - `test_subsystem_tasks`: 1 (KI-6);
  - `test_beacon`: 2 (KI-17, KI-15);
  - `test_saw`: 1 (KI-24);
  - `test_arq`: 3 (KI-20, KI-21, KI-22), or 4 with downlink (KI-23);
  - `test_ht_compression`: 0.

  Existing suites are unchanged; `test_ht_handling` still has its 2 known failures.
- **AC3.** Removing each IGNORE line makes that test fail for the stated reason. For KI-20 the failure is a crash or UBSan report; restore the IGNORE afterwards.
- **AC4.** `git status`: changes only under `test/` and `docs/testing/` (plus optionally `CLAUDE.md`). Nothing in `src/`, `include/` or `external/`.
- **AC5.** Sanitizers are on for every new suite (no `NO_SANITIZE`).
- **AC6.** LF endings; R5 Doxygen on every file, test, helper, `setUp`/`tearDown` and `main`.
- **AC7.** `test_tc_handler` passes with the weak fake definitions (its `fake_beacon_*` and `fake_arq_*` checks still work).

### B6. Pitfalls
- **Weak symbols:**
  - only the 6 listed definitions become weak;
  - the strong production definition must be in the suite TU (`#include`d) or in SOURCES;
  - with the real module linked, the `fake_beacon_*` / `fake_arq_*` recorders read 0.
- **`COMMS_QUEUE_LEN` is 2.** Drain the TX queue between steps in the beacon and ARQ tests, or later frames are silently dropped (`xQueueSend(..., 0)`).
- **Current task.** `saw`/`arq` wait on the *current* task's notifications and `fake_link` notifies the current task. Keep the test task current, or set both consistently.
- **`process_*` functions wait 1000 ticks** when nothing is pending. In block hooks, only run a task that has `fake_rtos_notify_pending(h, 0)`, otherwise time jumps and handshakes look like timeouts.
- **`sim_tasks` doesn't call the real `process_*`.** The subsystem suite needs its own hook (`run_real_tasks`).
- **The `#define deferred_notifications` rename** must wrap each include exactly and be `#undef`'d afterwards.
- **`cad.c` uses `rand()`.** Call `srand(1)` in `setUp`. A CAD give-up costs 4 random delays of 1..200 ticks.
- **Frame layouts in `arq_rx`:** DATA_BEGIN has BODY_VER at `p[0]`, but DATA and DATA_END start with the session id. Build frames as the code reads them.
- **Unity `TEST_IGNORE_MESSAGE`** must be the first statement (KI-20 would otherwise crash the suite binary).
- **Log limits:** the fake_obdh call log keeps 256 entries and the notify log 1024. Reset fake_obdh after `ht_init()`, and keep downlink tests to one 2 KB session.
- **Host is little-endian like the target.** Use memcpy for `BOOT_TIME_ADDR` / the HT header, not hand-assembled bytes.
- **Header shadowing:** `include/utils/time.h` shadows `<time.h>` (known; report, don't work around).
- **The obc suite** must define `obdh_queue_handle` and `time_get_unix` itself. Don't link `fake_comms_env` there.
- **Sanitizer findings in `compression.c`:** if ASan/UBSan fires there, don't add `NO_SANITIZE`. Record it as a new KI and report it.
- **`printf` noise** from lora_cfg/arq/ht/state_machine is expected.
