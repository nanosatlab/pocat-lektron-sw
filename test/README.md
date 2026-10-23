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
| `--label obc` | run the suites with that ctest label (`obc`, `comms`, `infra`) |
| `state_machine` | run the suites whose name matches the regex |
| `test_state_machine -l` | list the test names inside one suite |
| `test_state_machine -f NM_to_SM` | run only the tests whose name contains the text |

Suite binaries also accept `-n <exact name>` and `-x <text to exclude>` (Unity options).

The overall exit code is non-zero if any suite fails. `test_ht_handling` currently has 2 failing tests that expose production limitations (KI-10 and KI-11 in the known-issues table), so a full run exits non-zero.

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
    fake_obdh_requests.c/.h obdh_*_request over a 1 MB RAM flash image + call log
    stub_task_entries.c     empty payload_task, eps_task, ... so task_management.c links
    mock_obdh_requests.c    OBDH fake restricted to the HT region (used only by test_ht_handling)
    mock_pocket_plus.c      fake POCKET+ compressor (used only by test_ht_handling)
  support/
    fake_flash_memory.c/.h  RAM-backed HT flash region with power-cut injection (test_ht_handling)
    pocat_test_main.h       -l / -f handling + POCAT_RUN_TEST macro
    sim_tasks.c/.h          "let every task run its pause protocol once" (block hook)
  suites/                   one test_<module>.c per module
```

Naming: `fake_*` is a working, simplified implementation with a control/inspection API. `stub_*` does nothing. `mock_*` are the original files from the first suite; their names are kept.

### Suites

| Suite | Label | Covers |
|---|---|---|
| `test_ht_handling` | comms | historic-telemetry circular queue (`ht_handling.c`) |
| `test_notifications` | infra | `wait_for_notification`, notification bit map, self-tests of the fake RTOS |
| `test_task_management` | obc | task table, pause/resume handshake, health-driven reset |
| `test_state_machine` | obc | state to frequency, boot task sets, transitions |
| `test_comms_frame` | comms | SHA-256/HMAC known answers, air frame encode/decode, dedup ring |

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
- **Inspection.** The fakes record notifications (`fake_rtos_notify_log_*`, `fake_rtos_bits_sent_to`), flash calls (`fake_obdh_call_at`), clock switches (`fake_clock_*`).
- **Fault injection.** `fake_rtos_fail_task_create_at(n)`, `fake_rtos_fail_next_event_group_create()`, `sim_tasks_set_unresponsive(mask)`, `fake_obdh_inject_error(err)`.
- `configASSERT` fails the running test with file and line. Like real FreeRTOS, it also fires for `xEventGroupWaitBits` with an empty bit mask and for `xQueueCreate` with length 0.
- A notification rejected by `eSetValueWithoutOverwrite` (one is already pending) delivers nothing and is not recorded in the notify log, as in FreeRTOS.
- `test_ht_handling` uses its own older fakes (`mock_obdh_requests.c`, `fake_flash_memory.c`, the stub `flash.h`). When a test sets `ht_queue` pointers by hand, it must also set `ht_queue.current_ht_count`, because the eviction logic relies on the count, not on the pointers alone.
- The comms suite is built against a **test-only PSK** (`00 01 ... 0F`) generated into the build directory. It is not the flight key.

---

## Known issues found while building the infrastructure

No production code was changed. Tests that expose a bug are `IGNORED` with a `KNOWN BUG` message so the build stays green and the problem stays visible.

| ID | Where | Problem | Test |
|---|---|---|---|
| KB-1 | `src/subsystems/comms/frame.c` `decode_raw()` | No check `len <= AIR_PAYLOAD_MAX`, and `(uint8_t)(AIR_FRAME_HDR + len)` wraps for len >= 251. A received frame with LEN 243..255 overflows `AirFrame_t.payload[242]` on the transceiver stack, before authentication. | `test_comms_frame: test_decode_rejects_len_above_payload_max` (ignored) |
| KB-2 | `src/subsystems/transceiver/transceiver.c:127` | `uint8_t ack_buf[8];` but `air_encode_ack()` writes 16 bytes. Stack overflow on every ACK sent. | `test_ack_frame_is_16_bytes` documents the 16 bytes (passes) |
| KB-3 | `src/subsystems/obc/state_machine.c` `change_state()` | Writes 4 bytes at `PREVIOUS_STATE_ADDR` (0x08030001) then 4 bytes at `CURRENT_STATE_ADDR` (0x08030000). The writes overlap: PREVIOUS_STATE always reads 0 (BOOT) and `DEPLOYMENT_STATE_ADDR` / `DEPLOYMENTRF_STATE_ADDR` are zeroed on every state change. | `test_state_machine: test_transition_preserves_previous_state`, `test_transition_does_not_touch_deployment_flags` (ignored) |
| KI-4 | `src/subsystems/obc/task_management.c` `tm_check_pause()` | If PAUSE and RESUME are pending in one notification value, PAUSE wins and RESUME is dropped. The task stays paused forever and loses its health bit, so the watchdog will not recover it either. | `test_task_management: test_pause_and_resume_pending_together_leaves_task_running` (ignored) |
| KI-5 | `src/subsystems/obdh/obdh.c` `process_obdh()` | When paused it returns without blocking, so OBDH (priority 5) likely busy-loops and starves lower-priority tasks. Hypothesis, to confirm on target. | none yet |
| KI-6 | `src/subsystems/adcs/adcs.c` | Private `ADCS_DETUMBLING_MODE` (bit 0) / `ADCS_NADIR_POINTING_MODE` (bit 1) instead of `N_ADCS_DESIRED_STATE_*` (bits 1, 2) from `notifications.h`: an IDLE request would start detumbling. | none yet |
| KI-7 | `src/utils/RadioLib/radiolib_wrapper_mock.c` | Implements the old API, not `radiolib_wrapper.h`, so `build.sh --mock` very likely does not build. Not verified. | none |
| KI-8 | `tc_handler.c` `TC_UPLOAD_EPS_TH` / `TC_UPLOAD_PL_CONFIG` | `frame->len` is not checked before persisting 3 or 8 bytes. | none yet |
| KI-9 | `src/subsystems/obc/task_management.c` `tm_create_tasks()` | Doc says it returns `pdFAIL`, but it forwards `xTaskCreate`'s code, which in real FreeRTOS is `errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY` (-1). Harmless today because callers compare with `pdPASS`. | `test_create_failure_returns_pdFAIL` asserts `!= pdPASS` |
| KI-10 | `src/subsystems/comms/ht_handling.c` `ht_init()` | Oldest and newest blocks are picked with a plain `<` / `>` on `seq`, so after the 32-bit sequence counter wraps, `ht_init()` picks the wrong oldest slot. Very unlikely in a real mission (2^32 blocks), but real. | `test_ht_handling: test_init_seq_counter_wrap` (left failing on purpose) |
| KI-11 | `src/subsystems/comms/ht_handling.c` `ht_store_block()` | If the erase+program at a page boundary fails, the function returns without moving `writing_pointer`, so every later flush retries the same bad page and history stops being stored. There's no bad-page skip. Note: the test injects an error on every flash call, not only on page 0. | `test_ht_handling: test_bad_sector_deadlock` (left failing on purpose) |

Fixed in the test environment (not production bugs): `test_eviction_jumps_page` and `test_rp_in_another_page` set the queue pointers by hand without setting `current_ht_count`, so the queue looked empty. As a result the first test failed and the second passed without testing anything. `test_rp_in_another_page` also fed only 6 beacons, which never reached the wrap onto page 0. Both now set the count, and the second feeds 12 beacons.

---

## Roadmap

1. **Layer 1 (done):** unit tests per module with shared fakes.
2. **Layer 2:** a `test_tc_handler` suite with one test per TC_ID (the "command catalogue"): feed a TC frame to `tc_process()` and check the notification, the flash write and the TM/NACK queued.
3. **Layer 3 (optional):** interactive host simulator reading commands from stdin.
4. **Layer 4:** an on-target debug console, compiled out of flight builds.
