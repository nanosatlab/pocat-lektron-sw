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
| `--label obc` | run the suites with that ctest label (`obc`, `comms`, `obdh`, `infra`) |
| `state_machine` | run the suites whose name matches the regex |
| `test_state_machine -l` | list the test names inside one suite |
| `test_state_machine -f NOMINAL_to_SURVIVAL` | run only the tests whose name contains the text |

Suite binaries also accept `-n <exact name>` and `-x <text to exclude>` (Unity options).

The overall exit code is non-zero if any suite fails. Every suite passes: the two tests that failed on purpose (KI-10 and KI-11 in the known-issues table) went with `test_ht_handling` when step 4b of the flash plan moved HT onto the ring module, which fixes both.

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
    stubs_hal_flash/        stm32l4xx_hal.h for test_flash_driver: the FLASH registers and SYSCFG_MEMRMP as plain memory,
                            with the register bits of the real stm32l476xx.h (generated/flash_reg_bits.h, made by
                            CMakeLists.txt); every write through WRITE_REG(), SET_BIT(), CLEAR_BIT() or MODIFY_REG() goes
                            to the suite's fake flash controller. HAL_FLASH_Unlock(), HAL_FLASH_Lock() and
                            FLASH_PageErase() are only declared: the suite defines them as copies of ST's code
    stubs_rtos/             FreeRTOS.h task.h queue.h semphr.h event_groups.h timers.h (declarations only)
    fake_rtos.c/.h          single-threaded FreeRTOS fake + control/inspection API; counts tick overflows like the kernel;
                            a timer reports its period, whether it runs and its reload mode, and a period of 0 fails
                            configASSERT as in the kernel; a settable scheduler state (running after a reset), so a
                            test can be main() before the scheduler starts
    fake_hal.c/.h           peripheral handle instances (hiwdg, huart4, ...) + IWDG refresh counter
    fake_clock.c/.h         clock_* / periph_* fakes with call counters
    fake_obdh_requests.c/.h obdh_param_write_request() served at once by the real param_store_write(), run as the OBDH task
                            of the suite's task table; call log (ID, length, block time) and a hook run at every request
                            (test_state_machine, test_beacon_period). The flash image and the region and old-map requests
                            went with flash plan step 5a
    fake_flash_driver.c/.h  flash_read/program/erase_page over the data area, with power cuts, ECC errors, doublewords
                            programmed wrong without an ECC error and pages that fail to erase; counts reads and programs
                            (test_param_store, test_state_machine, test_ring, test_ht_store, test_beacon_period)
    stub_task_entries.c/.h  empty payload_task, eps_task, ... so task_management.c links; a settable OBC handle for tm_current_index()
  support/
    pocat_test_main.h       -l / -f handling + POCAT_RUN_TEST macro
    sim_tasks.c/.h          "let every task run its pause protocol once" (block hook)
    map_at.c/.h             RAM mapped at a fixed host address (VirtualAlloc or mmap), for code that uses absolute addresses
  suites/                   one test_<module>.c per module
```

Naming: `fake_*` is a working, simplified implementation with a control/inspection API. `stub_*` does nothing.

### Suites

| Suite | Label | Covers |
|---|---|---|
| `test_notifications` | infra | `wait_for_notification`, notification bit map, self-tests of the fake RTOS |
| `test_task_management` | obc | task table, pause/resume handshake (separate pause and resume acknowledgements), health-driven reset, the index of each OBDH client; only the OBC task creates, pauses, resumes or resets tasks or runs the health check (`obc_is_caller()`) |
| `test_state_machine` | obc | state to frequency; Init: every boot starts in it, the activation flags skip their steps (only a value of 1), the standby lasts 45 minutes and is stored, a reboot with both flags set waits at first contact for `TC_PING`, the PING ends Init in `NOMINAL`, other state requests do not end it, and a lower-power request made during Init follows the PING at once; transitions pause the old and resume the new tasks and store nothing, none for the current state; no clock switch without OBDH's pause acknowledgement, and the retry of the request the state machine keeps; only the OBC task boots, ends Init or changes the state (`obc_is_caller()`). Runs the real parameter store over the fake flash driver |
| `test_comms_frame` | comms | SHA-256/HMAC known answers, air frame encode/decode, dedup ring |
| `test_hk_history` | comms | HK history copy for the downlink (`hk_history.c`) over a fake `ht_read_request()`: the format byte, then SEQ, LEN and block per record; oldest first from FROM_SEQ, numbers missing in the store; a copy filled to its last byte and never beyond; the next copy going on from the last SEQ; nothing stored or the first read refused; one block time for all the reads |
| `test_beacon_period` | comms | beacon period (`beacon_period.c`) over the real parameter store: a period outside 10–3,600 s refused with nothing stored, both limits accepted; stored with the caller's block time, and only then the beacon task told; a period the store did not take changes nothing; the same period again applied again; the stored period read back, and the default when it is outside the limits; one repeating timer, started with the stored period at the first apply, also after a reboot, and kept by a restarted beacon task (KI-13); the timer tells the beacon task to transmit |
| `test_param_store` | obdh | parameter store (`param_store.c`): defaults, two copies, compaction, a new half header that reads back wrong (the old half kept), wear, a power cut at every step, reload, a stored record of an ID no longer in the table skipped (as boards that stored `PARAM_OBC_MODE` still hold); callers (FLS-REQ-110): another task's load and write refused with nothing read, programmed or changed, `main()` before the scheduler starts served, a restarted OBDH served |
| `test_ring` | obdh | ring module (`ring.c`) of the HT, payload and OBC-log stores: slot layout, the header check (a header damaged without an ECC error, a zeroed slot), one erase per page per lap, a page that fails to erase or a slot that fails to program skipped, a header, data or trailer that reads back with an ECC error or wrong without one written again on the next page, the wrap of the sequence number, a power cut at every step of an append |
| `test_ht_store` | obdh | telemetry-history store (`ht_store.c`) on the real ring and POCKET+: the 30 s credit (0 at boot, every window bounded across resets, kept by an OBDH restart), blocks byte for byte as POCKET+ makes them, the open block lost at a restart or reset, old-format slots blank, dropped posts counted; another task's load, add and read refused with nothing read, programmed or changed, the credit and the open block included (FLS-REQ-110) |
| `test_obdh_requests` | obdh | OBDH request protocol (`obdh_requests.c` + `obdh.c`), through its two requests since step 5a, the parameter write and the HT block read: one deadline per request, `HAL_BUSY` / `HAL_TIMEOUT`, data by copy (a read carries none: length 0), a block longer than the caller's buffer refused with nothing written, answers by number and client, OBDH paused (it sleeps until the resume) or restarted; posts never answered and never taking the clients' entries (V14) |
| `test_flash_driver` | obdh | the real flash driver (`flash.c`) over a fake flash controller that acts as RM0351 says the chip does and fails the test where the chip would stall or lock up (FLASH_CR written while busy or locked, a wrong key sequence, a cache reset while the cache is on), with RAM mapped at the data area's address (tests that need it are ignored if the host has that address in use). The three HAL functions the driver calls (`HAL_FLASH_Unlock()`, `HAL_FLASH_Lock()`, `FLASH_PageErase()`) are copies of ST's code in the suite, so their writes reach the fake controller. Program and erase step by step against RM0351 §3.3.6–3.3.7 and the HAL: each doubleword first word first, a program that is not whole doublewords refused, a target that is not erased refused before anything is written, all-zero data included (FLS-DRV-120), a doubleword the chip refuses (WRPERR) stopping the program there; the erase's page and bank, also with the banks swapped (`get_bank()`), an error the chip reports returned and cleared, error flags left by an earlier operation cleared first, an erase that cannot unlock FLASH_CR; FLASH_CR locked again, the data cache off while an operation runs, the caches flushed as the HAL does; the erase wait sleeps, the program wait does not. An operation that stays busy returns `HAL_TIMEOUT` at its limit without writing FLASH_CR, and the next one clears what it left (V13 on the driver); one that lasts exactly its limit succeeds. The interface reset after a task deleted during an erase or a refused program, when idle (no key written), and with the flash stuck busy. The ECC correction interrupt (enabled at boot above the kernel's mask, a correction left since reset counted, the address decoded in bank 1, bank 2 and system memory, ECCC cleared and ECCD left to the NMI, interrupts off from the read of FLASH_ECCR to the clear and PRIMASK restored, nothing done without a flag); the clear before each read; a double error in a read reported whether its NMI ran before the check (the count shows it) or has not run yet (ECCD shows it, and the driver leaves it to the NMI), and an earlier one not failing a later read (FLS-INT-040); the NMI counting a double error outside the program image, recording its address and clearing ECCD alone (the suite sets the linker symbols that end the image); an NMI that is not a double error left alone; program and erase leaving the interrupt on; unit data read-only; ranges outside their region refused |
| `test_uptime` | infra | uptime from the tick counter (`uptime.c`), across the 32-bit tick wrap |

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
- **R2. Reset everything in `setUp()`.** Call `fake_rtos_reset()`, `sim_tasks_reset()` and the module fakes' resets. Also reset the statics of the included modules (task table handles and `paused`, `task_event_group_handle`, the health module statics) and then call `health_init()`. A suite that runs the OBC's functions (state changes, task management, the health check) makes the test task the OBC task with `stub_set_obc_handle(fake_rtos_test_task())`: they do nothing for any other caller.
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

- **Single-threaded.** `xTaskCreate` only records a task; it never runs it. The test decides who is "running" with `fake_rtos_set_current_task()`, and whether the scheduler has started with `fake_rtos_set_scheduler_state()`.
- **Blocking calls.** When a call would block (event-group wait, queue, notification, mutex), the fake first runs the **block hook** (once, never re-entrantly). `sim_tasks_run_pending` is such a hook: every created task with a pending notification runs `tm_check_pause`. If the wait is still unmet the fake advances time by the timeout and returns "timeout". With `portMAX_DELAY` the test **fails with "deadlock"** (or only records it after `fake_rtos_expect_deadlock(true)`).
- **Inspection.** The fakes record notifications (`fake_rtos_notify_log_*`, `fake_rtos_bits_sent_to`), flash calls (`fake_obdh_call_at`), clock switches (`fake_clock_*`); timers answer the kernel's own queries (`xTimerGetPeriod`, `xTimerIsTimerActive`, `uxTimerGetReloadMode`).
- **Fault injection.** `fake_rtos_fail_task_create_at(n)`, `fake_rtos_fail_next_event_group_create()`, `sim_tasks_set_unresponsive(mask)`, `fake_obdh_inject_error(err)`; on the flash driver, `fake_flashdrv_cut_power_after(n)`, `fake_flashdrv_inject_ecc()`, `fake_flashdrv_inject_wrong_program()`, `fake_flashdrv_fail_erase()`.
- `configASSERT` fails the running test with file and line. Like real FreeRTOS, it also fires for `xEventGroupWaitBits` with an empty bit mask, for `xQueueCreate` with length 0, and for `xQueueSend` / `xQueueReceive` with a block time while the scheduler is suspended.
- A notification rejected by `eSetValueWithoutOverwrite` (one is already pending) delivers nothing and is not recorded in the notify log, as in FreeRTOS.
- The comms suite is built against a **test-only PSK** (`00 01 ... 0F`) generated into the build directory. It is not the flight key.

---

## Known issues found while building the infrastructure

No production code was changed while the infrastructure was built; later fixes are marked in the table. Tests that expose a bug are `IGNORED` with a `KNOWN BUG` message so the build stays green and the problem stays visible.

| ID | Where | Problem | Test |
|---|---|---|---|
| KB-1 | `src/subsystems/comms/frame.c` `decode_raw()` | No check `len <= AIR_PAYLOAD_MAX`, and `(uint8_t)(AIR_FRAME_HDR + len)` wraps for len >= 251. A received frame with LEN 243..255 overflows `AirFrame_t.payload[242]` on the transceiver stack, before authentication. | `test_comms_frame: test_decode_rejects_len_above_payload_max` (ignored) |
| KB-2 | `src/subsystems/transceiver/transceiver.c:127` | `uint8_t ack_buf[8];` but `air_encode_ack()` writes 16 bytes. Stack overflow on every ACK sent. | `test_ack_frame_is_16_bytes` documents the 16 bytes (passes) |
| KB-3 | `src/subsystems/obc/state_machine.c` `change_state()` | Writes 4 bytes at `PREVIOUS_STATE_ADDR` (0x08030001) then 4 bytes at `CURRENT_STATE_ADDR` (0x08030000). The writes overlap: PREVIOUS_STATE always reads 0 (BOOT) and `DEPLOYMENT_STATE_ADDR` / `DEPLOYMENTRF_STATE_ADDR` are zeroed on every state change. **Fixed on 2026-11-21** (flash plan step 3): the state is one parameter, `PARAM_OBC_MODE`, written once per change; the deployment flags are parameters of their own. The previous state, which nothing read, was dropped on 2026-11-25, and on 2026-11-26 the state stopped being stored: every boot starts in Init. | `test_state_machine: test_transition_stores_nothing` (passes) |
| KI-4 | `src/subsystems/obc/task_management.c` `tm_check_pause()` | If PAUSE and RESUME are pending in one notification value, PAUSE wins and RESUME is dropped. The task stays paused forever and loses its health bit, so the watchdog will not recover it either. **Fixed on 2026-11-12** (flash plan step 2d): the resume wins and is acknowledged on its own bit. | `test_task_management: test_pause_and_resume_pending_together_leaves_task_running` (passes) |
| KI-5 | `src/subsystems/obdh/obdh.c` `process_obdh()` | When paused it returns without blocking, so OBDH (priority 5) likely busy-loops and starves lower-priority tasks. Hypothesis, to confirm on target. Code reading on 2026-11-12: `process_obdh()` polls with `wait_for_notification(0)` and returns while paused, so `obdh_task()` never blocks. It lasts only for a mode change: every mode resumes OBDH, and the OBC (priority 6) runs above it. During that time, a lower-priority task that has not yet acknowledged its own pause cannot run, so the OBC waits the full 10 s (`TM_PAUSE_ACK_TIMEOUT_MS`) and switches without that task's acknowledgement. **Fixed on 2026-11-12:** while paused, OBDH sleeps in `wait_for_notification(portMAX_DELAY)` until the resume. | `test_obdh_requests: test_paused_obdh_sleeps_until_the_resume`, `test_other_notification_puts_a_paused_obdh_back_to_sleep` (pass) |
| KI-6 | `src/subsystems/adcs/adcs.c` | Private `ADCS_DETUMBLING_MODE` (bit 0) / `ADCS_NADIR_POINTING_MODE` (bit 1) instead of `N_ADCS_DESIRED_STATE_*` (bits 1, 2) from `notifications.h`: an IDLE request would start detumbling. | none yet |
| KI-7 | `src/utils/RadioLib/radiolib_wrapper_mock.c` | Implements the old API, not `radiolib_wrapper.h`, so `build.sh --mock` very likely does not build. Not verified. | none |
| KI-8 | `tc_handler.c` `TC_UPLOAD_EPS_TH` / `TC_UPLOAD_PL_CONFIG` | `frame->len` is not checked before persisting 3 or 8 bytes. **Fixed on 2026-11-21** (flash plan step 3): a frame too short for the value is refused with `NACK_BAD_PARAMS`, and a value the parameter store did not take with `NACK_BUSY`; EPS is told about new thresholds only once they are stored. | none: `tc_handler.c` has no host suite yet; checked by compilation |
| KI-9 | `src/subsystems/obc/task_management.c` `tm_create_tasks()` | Doc says it returns `pdFAIL`, but it forwards `xTaskCreate`'s code, which in real FreeRTOS is `errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY` (-1). Harmless today because callers compare with `pdPASS`. | `test_create_failure_returns_pdFAIL` asserts `!= pdPASS` |
| KI-10 | `src/subsystems/comms/ht_handling.c` `ht_init()` | Oldest and newest blocks are picked with a plain `<` / `>` on `seq`, so after the 32-bit sequence counter wraps, `ht_init()` picks the wrong oldest slot. Very unlikely in a real mission (2^32 blocks), but real. **Fixed in the ring module on 2026-11-21** (flash plan step 4a): sequence numbers are compared through their difference. HT moved onto the ring the same day (step 4b), and `ht_handling.c` was deleted. | `test_ring: test_sequence_numbers_keep_their_order_across_the_wrap` (passes); `test_ht_handling: test_init_seq_counter_wrap`, which failed on purpose, went with `ht_handling.c` |
| KI-11 | `src/subsystems/comms/ht_handling.c` `ht_store_block()` | If the erase+program at a page boundary fails, the function returns without moving `writing_pointer`, so every later flush retries the same bad page and history stops being stored. There's no bad-page skip. Note: the test injects an error on every flash call, not only on page 0. **Fixed in the ring module on 2026-11-21** (flash plan step 4a): the writer goes on at the next page, and a page that no longer erases is skipped at each lap. HT moved onto the ring the same day (step 4b), and `ht_handling.c` was deleted. | `test_ring: test_a_page_that_fails_to_erase_is_skipped` (passes); `test_ht_handling: test_bad_sector_deadlock`, which failed on purpose, went with `ht_handling.c` |
| KI-12 | `src/subsystems/comms/comms.c` `comms_task()` | KI-5 one priority lower, found on 2026-11-12 by code reading: COMMS polls its notifications with a zero timeout and `continue`s while paused, which skips the `xQueueReceive` that paces its loop. A paused COMMS spins at priority 4: during a mode change EPS (3), ADCS (2) and Payload (1) cannot run, and one that has not yet acknowledged its own pause makes the OBC wait the full 10 s and switch without it. Proposed fix, as for OBDH: while paused, wait for the next notification without a time limit. Not fixed yet. | none yet |
| KI-13 | `src/subsystems/comms/beacon.c` `beacon_task()` | Found on 2026-11-24 by code reading: every start of the beacon task created a new repeating timer and never deleted the old one, which kept firing. Each restart by the health monitor added a timer, and the heap it took: after n restarts, up to n + 1 beacons per period, and a new period reached only the newest timer. **Fixed on 2026-11-24** (flash plan step 5b): the timer is in `beacon_period.c`, created once; a restarted beacon task only sets its period. | `test_beacon_period: test_a_restarted_beacon_task_keeps_the_one_timer` (passes) |

Fixed in the test environment (not production bugs): `test_eviction_jumps_page` and `test_rp_in_another_page` set the queue pointers by hand without setting `current_ht_count`, so the queue looked empty. As a result the first test failed and the second passed without testing anything. `test_rp_in_another_page` also fed only 6 beacons, which never reached the wrap onto page 0. Both now set the count, and the second feeds 12 beacons.

---

## Roadmap

1. **Layer 1 (done):** unit tests per module with shared fakes.
2. **Layer 2:** a `test_tc_handler` suite with one test per TC_ID (the "command catalogue"): feed a TC frame to `tc_process()` and check the notification, the flash write and the TM/NACK queued.
3. **Layer 3 (optional):** interactive host simulator reading commands from stdin.
4. **Layer 4:** an on-target debug console, compiled out of flight builds.
