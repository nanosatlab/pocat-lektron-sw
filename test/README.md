# POCAT Lektron SW - Testing Infrastructure

This directory contains the **Host-Based Unit Testing** infrastructure for the flight software.

The goal of this architecture is to allow the logical validation of the control code (circular queues, state machines, frame authentication, etc.) by compiling and executing the code natively on the developer's computer (or inside a Docker container), isolating it completely from the physical hardware (STM32) and the RTOS (FreeRTOS).

---

## Architecture Philosophy: The Dual Workflow

When developing embedded software for space, there are two distinct types of testing that must coexist:

1. **Hardware Verification (STM32):** You compile the code using the `arm-none-eabi-gcc` cross-compiler and flash it to the physical STM32 board. This proves that electrical signals (SPI, I2C) and peripherals work.
2. **Logic Verification (Host-Based / Docker):** You compile the logic with a native C compiler on your computer. This allows you to run hundreds of tests in milliseconds and easily simulate impossible hardware failures to verify that your mathematics and algorithms are robust.

This directory (`test/`) is **strictly ignored** when compiling the flight software for the STM32. It only activates when the CMake flag `-DBUILD_TESTING=ON` is provided.

Host tests say nothing about timing, task priorities, stack sizes, starvation, or real peripherals. The host is also 64-bit while the target is 32-bit.

---

## How to Run the Tests

Make sure **Docker Desktop** is running. Open a terminal at the **root** of the repository and run:

```bash
docker build -t pocat-tests -f Dockerfile.test .      # once, or when Dockerfile.test changes
docker run --rm -v "${PWD}:/app" pocat-tests          # all suites
```

Keep the quotes around `${PWD}:/app`: the repository path contains spaces (`2nd Repo`), and without quotes Docker rejects the mount.

`Dockerfile.test` uses `ENTRYPOINT ["bash","run_tests.sh"]`, so anything after the image name is passed to `run_tests.sh`:

| Command (after `docker run --rm -v "${PWD}:/app" pocat-tests`) | What it does |
|---|---|
| *(nothing)* | build and run all suites (`ctest --output-on-failure`) |
| `--list` | list the registered suites |
| `--label obc` | run the suites with that ctest label (`obc`, `obdh`, `comms`, `subsys`, `infra`) |
| `state_machine` | run the suites whose name matches the regex |
| `test_state_machine -l` | list the test names inside one suite |
| `test_state_machine -f NM_to_SM` | run only the tests whose name contains the text |

Suite binaries also accept `-n <exact name>` and `-x <text to exclude>` (Unity options).

The overall exit code is non-zero if any suite fails. `test_ht_handling` currently has 2 failing tests that expose production limitations (KI-10 and KI-11 in the known-issues table), so a full run exits non-zero.

Every suite has a ctest timeout of 60 s (`POCAT_TEST_TIMEOUT` in `test/CMakeLists.txt`). A whole suite normally runs in well under a second, so a timeout means the code under test hangs.

`build_test/` is generated output and is ignored by git.

On Windows, always run the Docker commands from the repository root. In Git Bash use `-v "$(pwd -W)":/app` and set `MSYS_NO_PATHCONV=1`.

---

## Directory Structure

```
test/
  CMakeLists.txt            pocat_add_suite() helper + one line per suite
  framework/unity/          Unity test framework (third party, unchanged)
  mocks/
    stubs_hal/              stm32l4xx_hal.h (handle typedefs, HAL_IWDG_Refresh), empty stm32l476xx.h
    stubs_rtos/             FreeRTOS.h task.h queue.h semphr.h event_groups.h timers.h (declarations only)
    stubs_project/flash.h   minimal flash map, shadows the real flash.h ONLY for test_ht_handling
    fake_rtos.c/.h          single-threaded FreeRTOS fake + control/inspection API
    fake_hal.c/.h           peripheral handle instances (hiwdg, huart4, ...) + IWDG refresh counter
    fake_clock.c/.h         clock_* / periph_* fakes with call counters
    fake_obdh_requests.c/.h obdh_*_request over a 1 MB RAM flash image + call log (replaces the OBDH requests)
    fake_flash_driver.c/.h  flash_read/write/program/erase_page over a 1 MB RAM image, same rules as flash.c
                            (used by test_obdh, where the real OBDH task and requests are under test)
    fake_comms_env.c/.h     what tc_handler.c calls outside itself: comms TX/RX queues, RTC time, beacon period,
                            ARQ begin/arm, MCU sensors, radio configuration (+ fake_radio_last_config()).
                            The beacon globals, beacon_set_period(), arq_begin_dl() and arq_arm_ul() are
                            weak: a suite that compiles the real beacon.c / arq.c gets the real ones, and
                            the fake_beacon_* / fake_arq_* recorders then stay at 0
    fake_link.c/.h          scripted SX1262 + DIO1 IRQ (async TX/RX/IRQ/CAD calls of the RadioLib wrapper):
                            TX log, injected RX frames, a "ground station" responder callback, CAD result
    stub_task_entries.c     empty, weak payload_task, eps_task, ... so task_management.c links;
                            a real task compiled into the suite (e.g. obdh.c) replaces its stub
    mock_obdh_requests.c    OBDH fake restricted to the HT region (used only by test_ht_handling)
    mock_pocket_plus.c      fake POCKET+ compressor, 20 bytes per beacon (test_ht_handling, test_beacon);
                            test_ht_compression uses the real compression.c instead
  support/
    fake_flash_memory.c/.h  RAM-backed HT flash region with power-cut injection (test_ht_handling)
    pocat_test_main.h       -l / -f handling + POCAT_RUN_TEST macro
    sim_tasks.c/.h          "let every task run its pause protocol once" (block hook)
  suites/                   one test_<module>.c per module
```

Naming: `fake_*` is a working, simplified implementation with a control/inspection API. `stub_*` does nothing. `mock_*` are the original files from the first suite; their names are kept.

### Suites

`R` = `fake_rtos` + `fake_hal`, linked into every suite except `test_ht_handling`. "Production code" lists the files `#include`d into the suite (under test or holding statics) and, after `+`, the stateless ones linked through `SOURCES`.

| Suite | Label | Covers | Production code | Fakes |
|---|---|---|---|---|
| `test_ht_handling` | comms | historic-telemetry circular queue (`ht_handling.c`): slots, eviction, page erase per lap, `ht_init` rebuild, power cut, bad sector | ht_handling.c | stubs_project/flash.h, fake_flash_memory, mock_obdh_requests, mock_pocket_plus (no R) |
| `test_notifications` | infra | `wait_for_notification`, notification bit map, self-tests of the fake RTOS | + notifications.c | R |
| `test_task_management` | obc | task table, pause/resume handshake, health-driven reset | task_management.c, health.c + notifications.c | R, stub_task_entries, sim_tasks |
| `test_state_machine` | obc | state to frequency, boot task sets, transitions | state_machine.c, task_management.c, health.c + notifications.c | R, stub_task_entries, sim_tasks, fake_clock, fake_obdh_requests |
| `test_comms_frame` | comms | SHA-256/HMAC known answers, air frame encode/decode, dedup ring | auth.c + frame.c, dedup.c | R |
| `test_health` | obc | software watchdog: period timing (incl. tick wraparound), missing kicks, grace period, IWDG refresh | health.c | R |
| `test_obdh` | obdh | OBDH-mediated flash: client requests → OBDH task → flash driver → reply on index 1; queue full, pause/resume | obdh.c, obdh_requests.c, task_management.c, health.c + notifications.c | R, stub_task_entries, fake_flash_driver |
| `test_tc_handler` | comms | **Telecommand catalogue**: one `test_TC_<NAME>_...` per TC_ID. Checks which task is notified, what is written to flash, which TM/NACK is queued, and ARQ/beacon/RTC effects. Also checks that all 256 ids are either handled or NACKed | tc_handler.c, lora_cfg.c, tm_builder.c, task_management.c, health.c + notifications.c, frame.c, auth.c | R, stub_task_entries, fake_obdh_requests, fake_comms_env |
| `test_lora_cfg` | comms | LoRa configuration: boot defaults, parse, staged commit, commit at time, auto-revert, BUSY windows, BW map, time-on-air golden values, `ack_timeout_ms` | lora_cfg.c + timing.c | R, fake_comms_env |
| `test_obc` | obc | `setup_obc()` (OBDH queue, IWDG + health period, boot task sets, boot time persisted) and `process_obc()` (exit bits, 2 s idle, other bits ignored) | obc.c, state_machine.c, task_management.c, health.c + notifications.c | R, stub_task_entries, sim_tasks, fake_clock, fake_obdh_requests (+ suite-local `obdh_queue_handle`, `time_get_unix`) |
| `test_subsystem_tasks` | subsys | the shared task contract on the real `process_eps/adcs/payload()` (PAUSE → ACK, deferral, RESUME, 1 s idle); EPS threshold read | eps.c, adcs.c, payload.c, task_management.c, health.c + notifications.c | R, stub_task_entries, fake_obdh_requests |
| `test_beacon` | comms | 19-byte beacon body, uptime, timer callback and period, TX frame queued, HT fed (even when the TX queue is full) | beacon.c, ht_handling.c, lora_cfg.c, task_management.c, health.c + notifications.c, frame.c, auth.c | R, stub_task_entries, fake_obdh_requests, fake_comms_env (weak beacon symbols overridden), mock_pocket_plus |
| `test_saw` | comms | stop-and-wait sender: ACK, NACK, 3 retransmits then timeout, RETX frames authenticate, foreign frames forwarded, corrupted replies dropped, CAD give-up, missing TX_DONE | saw.c, lora_cfg.c + timing.c, cad.c, frame.c, auth.c | R, fake_comms_env, fake_link |
| `test_arq` | comms | ARQ **uplink**: arm, DATA_BEGIN, out-of-order and duplicate blocks, window DATA_ACKs, DATA_END CRC-32, TLE / calibration / flight-params flash write + ADCS notification, one session at a time. The downlink (`arq_run_dl`) isn't covered yet | arq.c, lora_cfg.c, task_management.c, health.c + notifications.c, timing.c, cad.c, frame.c, auth.c | R, stub_task_entries, fake_obdh_requests, fake_comms_env (weak arq symbols overridden), fake_link |
| `test_ht_compression` | comms | historic telemetry with the **real** POCKET+ compressor: real blocks fit the slot format, slot = reference + packets back to back, identical / random beacons, `ht_init` after real blocks, queue wrap, positive-mask update at `POCKET_PLUS_UPDATE_RATE`. No round trip: the ground decompressor isn't in this repo | ht_handling.c + compression.c | R, fake_obdh_requests (real flash.h) |

### The telecommand catalogue (Layer 2)

`test_tc_handler` is the "set of commands to try the satellite". Each TC can be run on its own:

```bash
docker run --rm -v "${PWD}:/app" pocat-tests test_tc_handler -l                 # list every command
docker run --rm -v "${PWD}:/app" pocat-tests test_tc_handler -f TC_REQUEST_HK   # just one
docker run --rm -v "${PWD}:/app" pocat-tests test_tc_handler -f LORA            # all LoRa-config cases
```

When you add a TC to `include/subsystems/comms/types.h`:
1. add a `case` in `tc_process()`;
2. add the id to `known[]` in `test_every_tc_id_is_either_handled_or_nacked`. `known[]` is a hand-written copy of the catalogue: the test catches a `case` without a `known[]` entry and the other way round, but not an id forgotten in both places;
3. write a `test_TC_<NAME>_<effect>` test.

---

## How to add a suite (2 steps)

1. Write `test/suites/test_<name>.c` (see the conventions below).
2. Add one line to `test/CMakeLists.txt`:
   ```cmake
   pocat_add_suite(test_<name> LABEL <label> SOURCES <extra .c files> ...)
   ```
   Options: `NO_FAKES` (do not link fake_rtos/fake_hal), `NO_SANITIZE` (needs a justifying comment), `INCLUDES <dirs searched first>`.

`run_tests.sh` needs no change: it builds the umbrella target `pocat_tests`, and every suite is a dependency of it.

Never build the default target in `build_test/`: it is the ARM firmware and will not build with the native compiler.

Sanitizers (AddressSanitizer + UBSan) are on by default; disable them for a whole build with `-DPOCAT_TEST_SANITIZERS=OFF`.

---

## Conventions (R1-R6)

- **R1. Under test means `#include` the `.c`.** Include every production `.c` that is under test or holds static state, so tests can inspect `static` variables. Link stateless helpers through `SOURCES`. Never do both for the same file (duplicate symbols).
- **R2. Reset everything in `setUp()`.** Call `fake_rtos_reset()`, `sim_tasks_reset()` and the module fakes' resets. Also reset the statics of the included modules (task table handles and `paused`, `task_event_group_handle`, the health module statics) and then call `health_init()`.
- **R3. `tearDown()` checks balance.** Assert `fake_rtos_critical_depth() == 0` and `fake_rtos_suspend_all_depth() == 0`.
- **R4. Known-bug policy.** A test for a known production bug starts with `TEST_IGNORE_MESSAGE("KNOWN BUG KB-n: <one line>");` and keeps the real assertions below it. Fixing the bug means deleting that one line. Every KB/KI is listed in the table below.
- **R5. Documentation and names.** Use the same Doxygen style as `src/`. Test names read as commands: `test_<action>_<expected>`.
  - **File header** (`@file`, `@brief`, `@date`, `@details`):
    - what is under test;
    - how the suite is built (which `.c` files are `#include`d and which are in `SOURCES`);
    - which fakes it uses and how they are set up;
    - how to run it.
  - **Every test** gets a `/** @brief ... @details ... */` block. `@brief` says the expected behaviour in one sentence. `@details` explains the scenario (arrange, act, expected) and *why it matters on the satellite*.
  - **Known-bug tests** add `@bug <ID> (test/README.md)` with the expected and the actual behaviour.
  - **Helpers, `setUp`/`tearDown` and `main`** get a short `@brief` too. `setUp` should say which statics it resets and why.
  - `test/suites/test_task_management.c` is a complete example.
- **R6. `main()` template** (from `support/pocat_test_main.h`):
  ```c
  int main(int argc, char **argv) {
      if (pocat_parse_args(argc, argv) != 0) return 1;
      POCAT_BEGIN();
      POCAT_RUN_TEST(test_x);
      return POCAT_END();
  }
  ```

---

## The fakes in short

- **Single-threaded.** `xTaskCreate` only records a task; it never runs it. The test decides who is "running" with `fake_rtos_set_current_task()`.
- **Blocking calls.** When a call would block (event-group wait, queue, notification, mutex), the fake first runs the **block hook** (once, never re-entrantly). `sim_tasks_run_pending` is such a hook: every created task with a pending notification runs `tm_check_pause`. If the wait is still unmet the fake advances time by the timeout and returns "timeout". With `portMAX_DELAY` the test **fails with "deadlock"** (or only records it after `fake_rtos_expect_deadlock(true)`).
- **Inspection.** The fakes record notifications (`fake_rtos_notify_log_*`, `fake_rtos_bits_sent_to`), flash calls (`fake_obdh_call_at`), clock switches (`fake_clock_*`), software timers (`fake_rtos_timer_period`, `fake_rtos_timer_active`; timers only fire through `fake_rtos_fire_timer`), radio configuration (`fake_radio_last_config`) and radio traffic (`fake_link_tx_frame`, `fake_link_scan_count`, ...).
- **Fault injection.** `fake_rtos_fail_task_create_at(n)`, `fake_rtos_fail_next_event_group_create()`, `sim_tasks_set_unresponsive(mask)`, `fake_obdh_inject_error(err)`.
- `configASSERT` fails the running test with file and line. Like real FreeRTOS, it also fires for `xEventGroupWaitBits` with an empty bit mask and for `xQueueCreate` with length 0.
- A notification rejected by `eSetValueWithoutOverwrite` (one is already pending) delivers nothing and is not recorded in the notify log, as in FreeRTOS.
- `test_ht_handling` uses its own older fakes (`mock_obdh_requests.c`, `fake_flash_memory.c`, the stub `flash.h`). When a test sets `ht_queue` pointers by hand, it must also set `ht_queue.current_ht_count`, because the eviction logic relies on the count, not on the pointers alone.
- The comms suite is built against a **test-only PSK** (`00 01 ... 0F`) generated into the build directory. It is not the flight key.

---

## Known issues found while building the infrastructure

No production code was changed. Tests that expose a bug are `IGNORED` with a `KNOWN BUG` message so the problem stays visible without failing the run. The exceptions are KI-10 and KI-11, whose `test_ht_handling` tests are left failing on purpose (see "How to Run the Tests").

| ID | Where | Problem | Test |
|---|---|---|---|
| KB-1 | `src/subsystems/comms/frame.c` `decode_raw()` | No check `len <= AIR_PAYLOAD_MAX`, and `(uint8_t)(AIR_FRAME_HDR + len)` wraps for len >= 251. A received frame with LEN 243..255 overflows `AirFrame_t.payload[242]` on the transceiver stack, before authentication. | `test_comms_frame: test_decode_rejects_len_above_payload_max` (ignored) |
| KB-2 | `src/subsystems/transceiver/transceiver.c:127` | `uint8_t ack_buf[8];` but `air_encode_ack()` writes 16 bytes. Stack overflow on every ACK sent. | `test_ack_frame_is_16_bytes` documents the 16 bytes (passes) |
| KB-3 | `src/subsystems/obc/state_machine.c` `change_state()` | Writes 4 bytes at `PREVIOUS_STATE_ADDR` (0x08030001) then 4 bytes at `CURRENT_STATE_ADDR` (0x08030000). The writes overlap: PREVIOUS_STATE always reads 0 (BOOT) and `DEPLOYMENT_STATE_ADDR` / `DEPLOYMENTRF_STATE_ADDR` are zeroed on every state change. | `test_state_machine: test_transition_preserves_previous_state`, `test_transition_does_not_touch_deployment_flags` (ignored) |
| KI-4 | `src/subsystems/obc/task_management.c` `tm_check_pause()` | If PAUSE and RESUME are pending in one notification value, PAUSE wins and RESUME is dropped. The task stays paused forever and loses its health bit, so the watchdog will not recover it either. | `test_task_management: test_pause_and_resume_pending_together_leaves_task_running` (ignored) |
| KI-5 | `src/subsystems/obdh/obdh.c` `process_obdh()` | When paused it returns without blocking: confirmed on host, a paused iteration takes 0 ticks. On target `obdh_task` would busy-loop at priority 5 and starve lower-priority tasks. Low impact today, because OBDH is in every state's task set and is never paused. | `test_obdh: test_paused_obdh_blocks_instead_of_spinning` (ignored) |
| KI-6 | `src/subsystems/adcs/adcs.c` | Private `ADCS_DETUMBLING_MODE` (bit 0) / `ADCS_NADIR_POINTING_MODE` (bit 1) instead of `N_ADCS_DESIRED_STATE_*` (bits 1, 2) from `notifications.h`: an IDLE request would start detumbling. ADCS also never handles `N_ADCS_NEW_TLE` / `N_ADCS_NEW_CALIBRATION`, which `arq.c` sends after an upload. | `test_subsystem_tasks: test_adcs_private_mode_bits_match_notification_map` (ignored) |
| KI-7 | `src/utils/RadioLib/radiolib_wrapper_mock.c` | Implements the old API, not `radiolib_wrapper.h`, so `build.sh --mock` very likely does not build. Not verified. | none |
| KI-8 | `tc_handler.c` `TC_UPLOAD_EPS_TH` / `TC_UPLOAD_PL_CONFIG` | `frame->len` is not checked before persisting 3 or 8 bytes, so a short TC stores stale bytes as EPS thresholds or payload config (and EPS is told to use them). | `test_tc_handler: test_TC_UPLOAD_EPS_TH_short_is_rejected`, `test_TC_UPLOAD_PL_CONFIG_short_is_rejected` (ignored) |
| KI-15 | `tc_handler.c` `TC_SET_BEACON_PERIOD` → `beacon.c` `beacon_set_period()` | A period of 0 s is accepted and becomes `xTimerChangePeriod(timer, 0)`. FreeRTOS's timer task checks `configASSERT(xTimerPeriodInTicks > 0)`, and this project's `configASSERT` disables interrupts and loops forever, so **one authenticated TC hangs the whole OBC** until the IWDG resets it. Verified by reading the code (FreeRTOS timers.c, FreeRTOSConfig.h:164). | `test_tc_handler: test_TC_SET_BEACON_PERIOD_zero_is_rejected`, `test_beacon: test_set_period_zero_keeps_previous_period` (both ignored) |
| KI-16 | `tc_handler.c` `TC_REQUEST_HK_HISTORY` / `_PAYLOAD_DATA` / `_OBC_LOG`, `TC_UPLOAD_TLE_BEGIN` / `_ADCS_CAL_BEGIN` | While an ARQ transfer is running, `arq_begin_dl()` returns -1 (arq.c:390) and `arq_arm_ul()` refuses (arq.c:420), but `tc_process()` ignores this: no NACK_BUSY, and `g_last_tc_rc` stays 0. The ground was ACKed and waits for a transfer that never starts. The -1 / refusal premise is confirmed on the real arq.c. | `test_tc_handler: test_TC_REQUEST_HK_HISTORY_while_arq_busy_nacks_busy` (ignored); premise: `test_arq: test_begin_dl_refused_while_ul_armed`, `test_arm_ul_refused_while_dl_active` (pass) |
| KI-9 | `src/subsystems/obc/task_management.c` `tm_create_tasks()` | Doc says it returns `pdFAIL`, but it forwards `xTaskCreate`'s code, which in real FreeRTOS is `errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY` (-1). Harmless today because callers compare with `pdPASS`. | `test_create_failure_returns_pdFAIL` asserts `!= pdPASS` |
| KI-10 | `src/subsystems/comms/ht_handling.c` `ht_init()` | Oldest and newest blocks are picked with a plain `<` / `>` on `seq`, so after the 32-bit sequence counter wraps, `ht_init()` picks the wrong oldest slot. Very unlikely in a real mission (2^32 blocks), but real. | `test_ht_handling: test_init_seq_counter_wrap` (left failing on purpose) |
| KI-11 | `src/subsystems/comms/ht_handling.c` `ht_store_block()` | If the erase+program at a page boundary fails, the function returns without moving `writing_pointer`, so every later flush retries the same bad page and history stops being stored. There's no bad-page skip. Note: the test injects an error on every flash call, not only on page 0. | `test_ht_handling: test_bad_sector_deadlock` (left failing on purpose) |
| KI-12 | `src/subsystems/obdh/obdh.c:65-66` `process_obdh()` | `notifications \|= deferred_notifications; deferred_notifications = 0;` but `notifications` is never used afterwards. Any non-PAUSE/RESUME notification sent to OBDH (e.g. `N_OBDH_CLEAR_FLASH`, `N_OBDH_CLEAR_HT`) is silently dropped. Verified by reading the code. | none yet (those commands aren't implemented); `test_tc_handler: test_TC_CLEAR_*_notifies_obdh` carry a `@warning` |
| KI-13 | `src/utils/health.c:175` `system_health()` | Calls `xEventGroupGetBits(health_eg)` without the NULL check that `health_kick()` has. A `health_check()` before `health_init()` would dereference NULL. It can't happen with today's boot order in `setup_obc()`. Verified by reading the code. | none (the fake RTOS would only assert) |
| KI-14 | `obdh_requests.c` → `obdh.c` → `flash.c` | The `size_t len` of a request is passed to the flash driver's `uint16_t n_bytes`, so a length ≥ 65536 is silently truncated. No client does that today. Verified by reading the code. | none |
| KI-17 | `beacon.c:43-46` + `obc.c:90-92` | Uptime = RTC now − boot time taken from the RTC. A `TC_UPLOAD_UNIX_TIME` after boot (e.g. RTC reset to 2000-01-01 on power loss, then synced) makes the uptime jump by decades; a backwards sync gives 0. Uptime should be tick based. | `test_beacon: test_uptime_survives_rtc_resync` (ignored) |
| KI-18 | `lora_cfg.c` `validate()` + `timing.c:278-282` | `preamble_symbols` is never range-checked (and TX power has no lower bound). At SF12 with a preamble > 32763 the ToA computation overflows uint32, so `lora_toa_ms` and every ACK/window timeout derived from it wrap to a small value. | `test_lora_cfg: test_toa_does_not_wrap_for_long_preamble` (ignored) |
| KI-19 | `timing.c:309` (also `arq.c:609-610`) | `ack_timeout_ms` uses the ToA of an 8-byte ACK, but an air ACK is 16 bytes. The ARQ window timeout likewise uses payload sizes instead of frame sizes. Timeouts are underestimated; today the margins absorb it. Low. | `test_lora_cfg: test_ack_timeout_uses_real_ack_frame_size` (ignored) |
| KI-20 | `arq.c:460` + `arq.c:538-539` | A `DATA_BEGIN` with `WINDOW_SIZE = 0` is accepted; the next `DATA` computes `ul_blocks_received % 0`. Confirmed on host: UBSan "division by zero" at arq.c:539 aborts the binary. On the target the DATA_ACK logic silently misbehaves. Authenticated frame, so a ground-mistake robustness issue. | `test_arq: test_UL_window_size_zero_is_rejected` (ignored; the IGNORE line must stay first or the suite binary aborts) |
| KI-21 | `arq.c:453-457` | A `DATA_BEGIN` whose TRANSFER_TYPE differs from the armed one is only logged; the session goes active and a DATA_ACK is sent. `g_sess.transfer_type` keeps the armed type, so e.g. calibration bytes are written to `TLE_ADDR` and ADCS is told it has a new TLE. | `test_arq: test_UL_type_mismatch_is_rejected` (ignored) |
| KI-22 | `arq.c:129-146`, `arq.c:567-568`, `flash.h` | The uplink length is bounded only by `ARQ_UL_BUF_MAX` (512), not by the destination region. A TLE (region 138 B at 0x08030020) or calibration upload longer than its region overwrites the neighbouring config; 500 B reach 0x08030213, past `ANTENNA_DEPLOYED_ADDR` (0x08030205) and `BOOT_TIME_ADDR` (0x08030206). Confirmed on host (calibration bytes overwritten). Only `TRANSFER_FLIGHT_PARAMS` is truncated to its 8 bytes. | `test_arq: test_UL_TLE_longer_than_region_does_not_overwrite_neighbours` (ignored) |
| KI-23 | `arq.c:87`, `arq.c:118-119` | `TRANSFER_HK_HISTORY` downlinks 2 KB from `TELEMETRY_ADDR` (legacy area), never from the HT queue at `HT_BASE_ADDR`, so the compressed historic telemetry built by `ht_handling.c` can't be downlinked. Verified by reading the code. | none yet (planned `test_arq: test_DL_HK_history_reads_ht_queue`, downlink part not written) |
| KI-24 | `saw.c:20`, `saw.c:92-99` | `saw_send` ignores `own_type` and matches ACK/NACK on REF_SEQ only (`arq_saw_frame` checks both), so an ACK for another frame type with the same seq is accepted. Low. | `test_saw: test_ack_with_wrong_ref_type_is_not_accepted` (ignored) |
| KI-25 | `obc.c:112-121` | `N_OBC_HARD_REBOOT` / `N_OBC_SOFT_REBOOT` / `N_OBC_UPDATE_TIME` are empty `if` blocks and `N_OBC_PERIPHERALS_REBOOT` isn't handled: the reboot TCs are ACKed but do nothing. Unimplemented, not a crash. | documented by a `@note` in `test_obc: test_process_non_exit_bits_do_not_change_state` (passes) |
| KI-26 | `state_machine.c` `change_state()` | A transition to the current state (e.g. `TC_TRANSIT_TO_NM` while in NM) pauses and resumes every task and does 2 flash writes. Low. | `test_obc: test_exit_to_current_state_is_a_no_op` (ignored) |

Fixed in the test environment (not production bugs): `test_eviction_jumps_page` and `test_rp_in_another_page` set the queue pointers by hand without setting `current_ht_count`, so the queue looked empty. As a result the first test failed and the second passed without testing anything. `test_rp_in_another_page` also fed only 6 beacons, which never reached the wrap onto page 0. Both now set the count, and the second feeds 12 beacons.

---

## Roadmap

1. **Layer 1 (done):** unit tests per module with shared fakes.
2. **Layer 2 (done):** `test_tc_handler`, the command catalogue. One test per TC_ID feeds a TC frame to `tc_process()` and checks the notification, the flash write and the TM/NACK queued.
3. **Layer 3 (optional):** interactive host simulator reading commands from stdin.
4. **Layer 4:** an on-target debug console, compiled out of flight builds.
