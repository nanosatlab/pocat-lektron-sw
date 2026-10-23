# Design: per-subsystem "dummy tests" and a command interface for PoCAT Lektron

> Written by the architect agent on 2026-10-22. Status: **Layer 1 (MVP) IMPLEMENTED and reviewed on 2026-10-23.**
> Part A is the overview (about 10 minutes to read). Part B is the spec that was implemented.
> For day-to-day use (commands, conventions, current known-issues table) read `test/README.md`, which is kept up to date. This document records the design decisions.
>
> **Decisions taken:** MVP approved as designed. Production bugs are only documented as `KNOWN BUG` tests, never fixed from test work. Sanitizers are on. The ctest name changed to `test_ht_handling`. No `.gitattributes` was added. `build_test/` was removed from git tracking.
>
> **Outcome:**
> - The 4 new suites pass, with 4 `KNOWN BUG` tests ignored: KB-1, KB-3 ×2 and KI-4. With their IGNORE line removed, each one fails for the stated reason. ASan catches the KB-1 overflow.
> - `test_ht_handling`: the A6 "pre-existing" row is outdated. `test_eviction_jumps_page` was a test mistake (it didn't set `current_ht_count`) and now passes. The other 2 failures are production limitations, KI-10 and KI-11 in `test/README.md`.
> - Additions beyond the spec: KI-9 (return code of `tm_create_tasks`), one extra event-group-failure test, and a NACK test.

---

## Part A: Overview

### A1. Short answer
- **Where it belongs:** in `test/`. Keep extending the host test module you already have. Logic tests should run in Docker on Windows in seconds, with no board attached.
- **What the "commands" are:** at first, each named test is a command. For example, `run_tests.sh state_machine` runs one subsystem's tests, and `run_tests.sh test_state_machine -f NM_to_SM` runs one scenario.
- **Later steps:**
  - A host "command catalogue" suite where every telecommand (TC) is a scenario.
  - Optionally, an interactive host simulator.
  - Finally, an on-target debug console on the Nucleo board.

### A2. Options compared

| Option | What it is | Pros | Cons | Verdict |
|---|---|---|---|---|
| (a) Host unit/integration tests in `test/` (Unity + fakes) | Compile real `.c` files with gcc and fake FreeRTOS/HAL | No hardware, fast, deterministic, can inject faults, runs in Docker. Infrastructure already exists | Can't check timing, preemption, or real peripherals | **Build first (Layers 1 and 2)** |
| (b) On-target test/command mode (UART console or debug TCs) | Firmware option that takes typed commands on the Nucleo and injects them into the real tasks | Exercises the real RTOS, clocks and flash | Needs the board. The `RADIO_MOCK` path looks stale (KI-7). Code must never fly | **Later (Layer 4)** |
| (c) Host command runner or simulator | A host executable that reads commands (`tc TRANSIT_TO_SM`, `state`, `tick 5000`) against the host build | "Try out the satellite" interactively | Needs (a)'s fakes first | **Later (Layer 3)** |

### A3. The layers and the order to build them
1. **Layer 1, unit tests per module (MVP, now).** Shared fakes, one suite per module, and a one-line CMake registration per suite.
2. **Layer 2, host scenarios: the command catalogue (next).** A `test_tc_handler` suite with one test per TC_ID. Each test feeds a TC frame to `tc_process()` and checks what happened: which task was notified with which bit, what was written to flash, and what TM/NACK was queued. This is the "set of commands to see how the satellite works", and it needs no hardware.
3. **Layer 3, interactive host simulator (optional).** `test/sim/pocat_sim.c` links the same fakes as Layer 2 and reads commands from stdin or a script file.
4. **Layer 4, on-target debug console (hardware phase).** A `POCAT_DEBUG_CONSOLE` CMake option, compiled out of flight builds like `POCAT_TEST_INSTRUMENTATION`. It reads UART lines like `tc 05` and posts an `AirFrame_t` into `comms_get_rx_queue()`, so the real `comms_task → tc_process` path runs on the board. The `TC_OBC_DEBUG_MODE` (0x73) TODO in `tc_handler.c` is the natural hook.

### A4. The commands in the MVP (from PowerShell, at the repo root)
```
docker build -t pocat-tests -f Dockerfile.test .                      # once / when Dockerfile changes
docker run --rm -v ${PWD}:/app pocat-tests                            # all suites
docker run --rm -v ${PWD}:/app pocat-tests --list                     # list suites
docker run --rm -v ${PWD}:/app pocat-tests --label obc                # one subsystem (ctest label)
docker run --rm -v ${PWD}:/app pocat-tests state_machine              # suites matching a regex
docker run --rm -v ${PWD}:/app pocat-tests test_state_machine -l      # list scenarios in a suite
docker run --rm -v ${PWD}:/app pocat-tests test_state_machine -f NM_to_SM   # run matching scenarios only
```
This short form works because `Dockerfile.test` changes to `ENTRYPOINT ["bash","run_tests.sh"]`. Running with no arguments behaves exactly as it does today.

### A5. How a host test fakes the satellite
- **Single-threaded.** Nothing runs concurrently. `xTaskCreate` only records the task and never starts it. The test decides who is "running" with `fake_rtos_set_current_task()`.
- **Blocking calls.** When code would block (waiting for an ACK, a queue item, or a notification), the fake calls a **block hook** first. The hook lets the test "run the other tasks", for example letting every task answer a PAUSE.
  - If the wait still can't be satisfied, the fake advances time by the timeout and returns "timeout".
  - If the timeout was `portMAX_DELAY`, the test **fails with "deadlock"**, so deadlocks are caught automatically.
- **Inspection.** Fakes record what they did: notifications sent (target, index, bits), flash writes (address, bytes), clock switches. Tests assert on those records.
- **Fault injection.** Make the Nth `xTaskCreate` fail, make OBDH return `HAL_ERROR`, or make a task unresponsive.
- **Sanitizers.** Suites build with AddressSanitizer and UBSan, so memory bugs make tests fail loudly.

### A6. Problems found while designing
The MVP writes tests for the first four and marks them `IGNORED: KNOWN BUG`, so the build stays green and the problems stay visible. No production code is changed.

| ID | Where | Problem |
|---|---|---|
| KB-1 | `src/subsystems/comms/frame.c` `decode_raw()` | Nothing checks `len <= AIR_PAYLOAD_MAX`, and `(uint8_t)(AIR_FRAME_HDR + len)` wraps for len ≥ 251. A received frame with LEN 243..255 makes `memcpy` overflow `AirFrame_t.payload[242]` on the transceiver stack. This happens **before authentication**, so anyone on the air can trigger it. |
| KB-2 | `src/subsystems/transceiver/transceiver.c:127` | `uint8_t ack_buf[8];` but `air_encode_ack()` writes 16 bytes (5 header + 3 body + 8 tag). This is a stack overflow on every ACK sent. **(Verified by the coordinator.)** |
| KB-3 | `src/subsystems/obc/state_machine.c` `change_state()` | It writes `sizeof(obc_state_t)` (4 bytes) at `PREVIOUS_STATE_ADDR` (0x08030001), then at `CURRENT_STATE_ADDR` (0x08030000). The writes overlap. As a result PREVIOUS_STATE always reads 0 (BOOT), and `DEPLOYMENT_STATE_ADDR` / `DEPLOYMENTRF_STATE_ADDR` are zeroed on every state change. **(Verified by the coordinator.)** |
| KI-4 | `src/subsystems/obc/task_management.c` `tm_check_pause()` | If PAUSE and RESUME are both pending in one notification value, PAUSE wins and RESUME is dropped. The task stays paused forever and its health bit is removed, so the watchdog won't recover it either. |
| KI-5 | `src/subsystems/obdh/obdh.c` `process_obdh()` | When paused, it returns without blocking (`wait_for_notification(0)`), so OBDH at priority 5 likely busy-loops and starves lower-priority tasks. This is a hypothesis to confirm on target. |
| KI-6 | `src/subsystems/adcs/adcs.c` | It uses private `ADCS_DETUMBLING_MODE` (bit 0) and `ADCS_NADIR_POINTING_MODE` (bit 1) instead of `N_ADCS_DESIRED_STATE_*` (bits 1 and 2) from `notifications.h`. An IDLE request would therefore start detumbling. |
| KI-7 | `src/utils/RadioLib/radiolib_wrapper_mock.c` | It implements the old API, not `radiolib_wrapper.h`, so `build.sh --mock` very likely fails to build. Not verified by building. |
| KI-8 | `tc_handler.c` `TC_UPLOAD_EPS_TH` / `TC_UPLOAD_PL_CONFIG` | `frame->len` isn't checked before persisting 3 or 8 bytes. |
| Pre-existing | `test/suites/test_ht_handling.c` | The last run shows 11 passing and 3 failing: `test_init_seq_counter_wrap`, `test_eviction_jumps_page`, `test_bad_sector_deadlock`. |

### A7. What can be tested on host, per subsystem

| Subsystem | On host? | First dummy tests | Not feasible on host |
|---|---|---|---|
| notifications | Yes (MVP) | wait returns 0 / drains bits / set bits accumulate / index 1 doesn't leak into index 0 / global bits don't collide / TM bits equal ACK bits | — |
| task_management | Yes (MVP) | create running/paused sets; task number = table index; health only for running tasks; create failure → pdFAIL; pause/resume handshake; deferral while paused; ACK timeout; health fault resets the right task; KI-4 | Real preemption and priorities |
| OBC state_machine | Yes (MVP) | freq per state; boot sets per state; NM→SM clock switch once; CM keeps 80 MHz; state persisted; pause old / resume new; no exit bit → no-op; NM priority; KB-3 ×2 | Real clock/peripheral reconfiguration |
| OBC task (`obc.c`) | Phase 2 | forwards exit bits to the state machine; setup persists boot time | IWDG reset itself |
| health | Phase 2 | no fault before period; missing kick reported; grace for newly expected tasks; IWDG refreshed only after a clean period; tick wraparound | — |
| OBDH | Phase 2 | read/write round trip; NULL buffer error; queue full → BUSY; client notified on index 1 only; paused OBDH doesn't serve; KI-5 | Real HAL_FLASH errors, erase timing, power cut |
| comms frame/auth/dedup | Yes (MVP) | SHA-256 and HMAC known-answer tests; roundtrip; tamper/version/flag/truncation rejects; RETX flag; 242/243 limit; ACK = 16 bytes; dedup; KB-1 | — |
| lora_cfg + timing | Phase 2 | parameter range rejects; busy while pending; commit at time; auto-revert; time-on-air golden value | Over-the-air verification |
| tc_handler + tm_builder (command catalogue) | Phase 2 | one test per TC_ID, unknown TC → NACK, short frames | — |
| beacon | Phase 2 | 19-byte layout; uptime; timer callback; transmit feeds HT | Real timer accuracy |
| ht_handling / compression | Existing; Phase 3 adds real `compression.c` | keep existing tests; real POCKET+ block fits slot | Ground decompressor compatibility |
| arq / saw | Phase 3 (scripted fake radio) | UL complete writes TLE; CRC mismatch discarded; SAW ACK/RETX/timeout | Air timing, CAD, IRQ races |
| EPS / ADCS / PAYLOAD | Phase 2 | shared "task contract" (pause → ACK, defer, resume); EPS thresholds; KI-6 | Sensors and actuators |
| transceiver | Hard (infinite loop; needs a `transceiver_process_once()` refactor) | ACK and forward; duplicate resends cached ACK (would expose KB-2) | SX1262 driver, SPI, DIO1 |
| core/HAL utilities | No (low value) | — | Belongs to on-target AIV |

General host limits: the host is 64-bit and the target is 32-bit. Host tests also say nothing about stack sizes, priorities, starvation or timing.

### A8. Open questions
1. **Bugs found:** fix KB-1/KB-2/KB-3/KI-4 in separate small changes, or only document them (MVP default)?
2. **The 3 failing ht tests:** leave them failing (default) or mark them `IGNORE`?
3. **ctest name change:** `HT_Handling_Tests` becomes `test_ht_handling`. OK?
4. **Sanitizers on by default:** recommended. OK?
5. **Layer 4:** it needs KI-7 fixed first, or the real radio. When?
6. **Line endings:** add `.gitattributes` with `*.sh text eol=lf`? Your git has `core.autocrlf=true`.

---

## Part B: Executor specification

### B0. Ground rules
- Do not modify `src/`, `include/` or `external/`. Do not commit or push.
- Allowed changes: `test/**`, `run_tests.sh`, `Dockerfile.test`, and optionally the testing section of `CLAUDE.md`.
- `run_tests.sh` and all new files must use **LF** line endings.
- Never build the default target in `build_test/`. It's the ARM firmware compiled with native gcc and won't build. Always build `pocat_tests`.

### B1. Target layout (new = ★, changed = ✎)
```
Dockerfile.test                     ✎ ENTRYPOINT ["bash","run_tests.sh"] (drop CMD)
run_tests.sh                        ✎ argument dispatch (B3)
test/
  CMakeLists.txt                    ✎ pocat_add_suite() helper + registrations (B2)
  README.md                         ✎ how to run, add a suite, conventions, known-issues table
  framework/unity/                  (unchanged)
  mocks/
    stubs_hal/stm32l4xx_hal.h       ✎ + handle typedefs, HAL_IWDG_Refresh
    stubs_hal/stm32l476xx.h         ★ empty (included by main.h, log.h)
    stubs_rtos/FreeRTOS.h task.h queue.h semphr.h event_groups.h   ✎ real declarations
    stubs_rtos/timers.h             ★
    stubs_project/flash.h           (unchanged; used ONLY by test_ht_handling)
    mock_obdh_requests.c            (unchanged; ht only)
    mock_pocket_plus.c              (unchanged)
    fake_rtos.c / fake_rtos.h       ★ FreeRTOS fake + control API (B4)
    fake_hal.c / fake_hal.h         ★ handle instances (hiwdg, huart4, htim2, htim5, hspi2, hrtc, hadc1) + IWDG refresh counter
    fake_clock.c / fake_clock.h     ★ clock_get_current, clock_switch_to_freq, systemclock_init_for_freq, periph_reconfigure_for_freq, periph_init_for_freq + counters
    fake_obdh_requests.c / .h       ★ 4 obdh_*_request over a 1 MB RAM flash image + call log
    stub_task_entries.c             ★ empty payload_task, eps_task, comms_task, adcs_task, obdh_task, transceiver_task, beacon_task
  support/
    fake_flash_memory.c/.h          (unchanged)
    pocat_test_main.h               ★ -l / -f handling + POCAT_RUN_TEST macro (B5)
    sim_tasks.c / sim_tasks.h       ★ "let every task run its pause protocol once" (B4.3)
  suites/
    test_ht_handling.c              ✎ only main(): argc/argv + POCAT_RUN_TEST (assertions untouched)
    test_notifications.c            ★
    test_task_management.c          ★
    test_state_machine.c            ★
    test_comms_frame.c              ★
```
Naming:
- `fake_*`: a working, simplified implementation with a control/inspection API.
- `stub_*`: does nothing.
- `mock_*`: existing files; keep their names.

### B2. `test/CMakeLists.txt`
```cmake
project(pocat_tests C)
set(TEST_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(SRC_DIR  ${CMAKE_SOURCE_DIR}/src)
option(POCAT_TEST_SANITIZERS "Build host tests with ASan+UBSan" ON)

add_library(unity STATIC framework/unity/unity.c)
target_include_directories(unity PUBLIC framework/unity)
target_compile_definitions(unity PUBLIC UNITY_USE_COMMAND_LINE_ARGS)

# Test-only PSK (NOT the flight key). psk.h.in uses @PSK_BYTES@.
set(PSK_BYTES "0x00u, 0x01u, 0x02u, 0x03u, 0x04u, 0x05u, 0x06u, 0x07u, 0x08u, 0x09u, 0x0Au, 0x0Bu, 0x0Cu, 0x0Du, 0x0Eu, 0x0Fu")
configure_file(${CMAKE_SOURCE_DIR}/include/subsystems/comms/psk.h.in
               ${CMAKE_CURRENT_BINARY_DIR}/generated/psk.h @ONLY)

set(POCAT_TEST_COMMON_INCLUDES
    ${CMAKE_CURRENT_BINARY_DIR}/generated      # test psk.h wins over any dev psk.h
    ${TEST_DIR}/support ${TEST_DIR}/mocks
    ${TEST_DIR}/mocks/stubs_hal ${TEST_DIR}/mocks/stubs_rtos
    ${PROJECT_INCLUDE_DIRECTORIES}             # real headers, from root CMakeLists
    ${SRC_DIR}/utils ${SRC_DIR}/subsystems/obc ${SRC_DIR}/subsystems/obdh
    ${SRC_DIR}/subsystems/comms ${SRC_DIR}/subsystems/eps ${SRC_DIR}/subsystems/adcs
    ${SRC_DIR}/subsystems/payloads ${SRC_DIR}/subsystems/transceiver)  # so suites can #include "x.c"

add_custom_target(pocat_tests)   # run_tests.sh builds only this

# pocat_add_suite(<name> [LABEL l] [NO_FAKES] [NO_SANITIZE] [SOURCES ...] [INCLUDES dirs-searched-first ...])
function(pocat_add_suite NAME)
  cmake_parse_arguments(S "NO_FAKES;NO_SANITIZE" "LABEL" "SOURCES;INCLUDES" ${ARGN})
  set(srcs ${TEST_DIR}/suites/${NAME}.c ${S_SOURCES})
  if(NOT S_NO_FAKES)
    list(APPEND srcs ${TEST_DIR}/mocks/fake_rtos.c ${TEST_DIR}/mocks/fake_hal.c)
  endif()
  add_executable(${NAME} ${srcs})
  target_include_directories(${NAME} PRIVATE ${S_INCLUDES} ${POCAT_TEST_COMMON_INCLUDES})
  target_link_libraries(${NAME} PRIVATE unity)
  target_compile_options(${NAME} PRIVATE -Wall -Wextra -Wno-unused-parameter -g -O0)
  if(POCAT_TEST_SANITIZERS AND NOT S_NO_SANITIZE)
    target_compile_options(${NAME} PRIVATE -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer)
    target_link_options(${NAME} PRIVATE -fsanitize=address,undefined)
  endif()
  add_test(NAME ${NAME} COMMAND ${NAME})
  if(S_LABEL)
    set_tests_properties(${NAME} PROPERTIES LABELS "${S_LABEL}")
  endif()
  add_dependencies(pocat_tests ${NAME})
endfunction()

pocat_add_suite(test_ht_handling LABEL comms NO_FAKES
  INCLUDES ${TEST_DIR}/mocks/stubs_project
  SOURCES support/fake_flash_memory.c mocks/mock_obdh_requests.c mocks/mock_pocket_plus.c)
pocat_add_suite(test_notifications LABEL infra
  SOURCES ${SRC_DIR}/utils/notifications.c)
pocat_add_suite(test_task_management LABEL obc            # suite #includes task_management.c + health.c
  SOURCES ${SRC_DIR}/utils/notifications.c mocks/stub_task_entries.c support/sim_tasks.c)
pocat_add_suite(test_state_machine LABEL obc              # suite #includes state_machine.c + task_management.c + health.c
  SOURCES ${SRC_DIR}/utils/notifications.c mocks/stub_task_entries.c support/sim_tasks.c
          mocks/fake_clock.c mocks/fake_obdh_requests.c)
pocat_add_suite(test_comms_frame LABEL comms              # suite #includes auth.c (static sha256/hmac)
  SOURCES ${SRC_DIR}/subsystems/comms/frame.c ${SRC_DIR}/subsystems/comms/dedup.c)
```
- The fake sources are compiled into each suite, not into a shared library. This avoids mixing sanitized and unsanitized objects.
- If ASan or UBSan makes `test_ht_handling` behave differently from today, add `NO_SANITIZE` to it with a comment, and report what the sanitizer said. It may be a real finding.
- `test_ht_handling` gets `stubs_project` via INCLUDES because the stub `flash.h` must shadow the real one only for that suite. New suites use the **real** `include/utils/flash.h`.

### B3. `run_tests.sh`
```bash
#!/bin/bash
# bash run_tests.sh                    -> all suites
# bash run_tests.sh --list             -> list suites
# bash run_tests.sh --label <label>    -> suites with that ctest label (obc, comms, infra)
# bash run_tests.sh <regex>            -> ctest -R <regex>
# bash run_tests.sh <suite> -f <text>  -> run one suite binary, only tests whose name contains <text>
# bash run_tests.sh <suite> -l         -> list test names in a suite
set -e
mkdir -p build_test && cd build_test && rm -f CMakeCache.txt
cmake -DBUILD_TESTING=ON .. > /dev/null
make -j"$(nproc)" pocat_tests
case "$1" in
  "")      ctest --output-on-failure ;;
  --list)  ctest -N ;;
  --label) ctest --output-on-failure -L "$2" ;;
  *) if [ $# -ge 2 ] && [ -x "test/$1" ]; then s="$1"; shift; "./test/$s" "$@";
     else ctest --output-on-failure -R "$1"; fi ;;
esac
```
In `Dockerfile.test`, replace `CMD ["bash", "run_tests.sh"]` with `ENTRYPOINT ["bash", "run_tests.sh"]`.

### B4. Fakes

#### B4.1 Stub headers
These must compile the real `task_management.h`, `obdh.h` (→ `main.h` → `log.h`/`periph.h`), `flash.h` (→ `obc.h`, `definitions.h`), `comms.h` and `health.h`.
- `FreeRTOS.h`:
  - `TickType_t` (uint32_t), `BaseType_t` (long), `UBaseType_t` (unsigned long).
  - `pdTRUE/pdFALSE/pdPASS/pdFAIL`, `errQUEUE_FULL` (0).
  - `portMAX_DELAY` (0xFFFFFFFFu).
  - `pdMS_TO_TICKS(ms) ((TickType_t)(ms))` (target is 1 kHz).
  - `configASSERT(x)` → `fake_rtos_assert_failed(__FILE__, __LINE__)`.
- `task.h`:
  - `typedef struct fake_task *TaskHandle_t;`, `TaskFunction_t`.
  - `eNotifyAction {eNoAction, eSetBits, eIncrement, eSetValueWithOverwrite, eSetValueWithoutOverwrite}`.
  - `taskSCHEDULER_SUSPENDED/NOT_STARTED/RUNNING`.
  - `taskENTER_CRITICAL()`/`taskEXIT_CRITICAL()` → `fake_rtos_enter_critical/exit_critical`.
  - Prototypes: `xTaskCreate`, `vTaskDelete`, `vTaskSuspend`, `vTaskSetTaskNumber`, `uxTaskGetTaskNumber`, `xTaskGetCurrentTaskHandle`, `xTaskGetTickCount`, `vTaskDelay`, `vTaskSuspendAll`, `xTaskResumeAll`, `xTaskGetSchedulerState`, `xTaskNotify`, `xTaskNotifyIndexed`, `xTaskNotifyWait`, `xTaskNotifyWaitIndexed`, `xTaskNotifyStateClear`. These are plain functions, not FreeRTOS's macros.
- `queue.h`: `QueueHandle_t`, `xQueueCreate`, `xQueueSend`, `xQueueReceive`.
- `event_groups.h`: `EventBits_t` (uint32_t), `EventGroupHandle_t`, `xEventGroupCreate/SetBits/ClearBits/GetBits/WaitBits`.
- `semphr.h`: `SemaphoreHandle_t`, `xSemaphoreCreateMutex/Take/Give`.
- `timers.h`: `TimerHandle_t`, `TimerCallbackFunction_t`, `xTimerCreate/Start/ChangePeriod`.
- `stm32l4xx_hal.h` additions:
  - `typedef struct { int dummy; }` for each of `TIM_HandleTypeDef`, `SPI_HandleTypeDef`, `UART_HandleTypeDef`, `IWDG_HandleTypeDef`, `RTC_HandleTypeDef`, `ADC_HandleTypeDef`.
  - `HAL_StatusTypeDef HAL_IWDG_Refresh(IWDG_HandleTypeDef *)`.
  - Keep the existing enum and `FLASH_PAGE_SIZE`, which ht relies on.

#### B4.2 `mocks/fake_rtos.h` control API (minimum)
```c
void fake_rtos_reset(void);                                   // frees all objects, tick=0, current=test task, hook=NULL
TaskHandle_t fake_rtos_test_task(void);                       // the task that represents the test
void fake_rtos_set_current_task(TaskHandle_t t);              // NULL -> test task
typedef void (*fake_rtos_block_hook_t)(void);
void fake_rtos_set_block_hook(fake_rtos_block_hook_t hook);
void fake_rtos_set_tick(TickType_t t); void fake_rtos_advance_ticks(TickType_t dt);
void fake_rtos_fail_task_create_at(int nth);                  // nth upcoming xTaskCreate fails (1 = next), 0 = off
void fake_rtos_fail_next_queue_create(void); void fake_rtos_fail_next_event_group_create(void);
TaskHandle_t fake_rtos_find_task(const char *name);           // newest alive task with that name
bool fake_rtos_task_alive(TaskHandle_t t); int fake_rtos_tasks_created(void);
uint32_t fake_rtos_notify_value(TaskHandle_t t, UBaseType_t idx);
bool fake_rtos_notify_pending(TaskHandle_t t, UBaseType_t idx);
typedef struct { TaskHandle_t target; UBaseType_t index; uint32_t value; eNotifyAction action; } fake_notify_rec_t;
int fake_rtos_notify_log_count(void); fake_notify_rec_t fake_rtos_notify_log_at(int i);
uint32_t fake_rtos_bits_sent_to(TaskHandle_t t);              // OR of all index-0 values since reset
UBaseType_t fake_rtos_queue_items(QueueHandle_t q);
int fake_rtos_critical_depth(void); int fake_rtos_suspend_all_depth(void);
void fake_rtos_fire_timer(TimerHandle_t t);
```
Behaviour rules:
- **Single-threaded.** `xTaskCreate` stores name, fn, prio, depth and a number (default 0). It never runs the function.
- **Blocking.** A call would block when its condition is unmet and `ticks > 0`. When that happens:
  1. Call the hook once, never re-entrantly (use a guard flag).
  2. Re-check the condition.
  3. If still unmet and `ticks == portMAX_DELAY`, call `TEST_FAIL_MESSAGE("fake_rtos: deadlock in <api>")`. Otherwise advance the tick by `ticks` and return the timeout result.
- **Notifications** follow FreeRTOS per-index semantics:
  - `eSetBits` ORs; `eSetValueWithOverwrite` replaces; `eIncrement` adds one; `eSetValueWithoutOverwrite` fails if a notification is pending.
  - Clear-on-entry applies only when nothing is pending.
  - On success, `*v` gets the value, then clear-on-exit applies and pending is cleared.
  - On timeout, `*v` gets the current value and the call returns pdFALSE.
  - Every notify is logged.
- **Queues:** FIFO with byte copies. Send on a full queue goes through the blocking rule, then returns `errQUEUE_FULL`.
- **Event groups:** `WaitBits` returns the bits value from before clearing, and clears only if satisfied and `clearOnExit`. Calling any API with a NULL handle triggers `configASSERT`.
- **Critical sections:** count nesting depth; `tearDown` asserts it's 0.

#### B4.3 `support/sim_tasks.h`
```c
void sim_tasks_reset(void);
void sim_tasks_run_pending(void);                 // block hook: each created, alive, responsive task with a pending idx-0
                                                  // notification becomes current, n = wait_for_notification(0),
                                                  // tm_check_pause(n, &deferred[i]); then restore current task
void sim_tasks_set_unresponsive(uint32_t tm_mask);// these tasks never "run" (stuck task)
uint32_t sim_tasks_deferred(uint32_t tm_bit);
uint32_t sim_tasks_last_notification(uint32_t tm_bit);
```
It finds tasks through `tm_get_task_handle(1u << i)` for i in 0..6, which needs the real `task_management.c` in the same suite.

#### B4.4 `mocks/fake_obdh_requests.h`
- The flash image covers `0x08000000..0x080FFFFF` and starts as 0xFF.
- Function semantics:
  - `write` = memcpy.
  - `program` = AND.
  - `erase_program` = 0xFF over the containing 2 KB page, then AND.
  - NULL data or len 0 returns `HAL_ERROR`.
  - Out of range returns `HAL_ERROR`.
  - `fake_obdh_inject_error(err)`: every later call returns `err` without touching the image.
- API: `fake_obdh_reset()`, `uint8_t *fake_obdh_flash(uint32_t addr)`, `int fake_obdh_call_count(void)`, `fake_obdh_call_t fake_obdh_call_at(int i)` with `{op, addr, len}`.

### B5. Suite conventions
- **R1. Under test → `#include` the `.c`.** `#include` every production `.c` that is under test or holds static state. Link stateless helpers through `SOURCES`. Never do both for the same file (duplicate symbols).
- **R2. Reset everything in `setUp()`.** Call `fake_rtos_reset()`, `sim_tasks_reset()` and the module fakes' resets. Also reset the included modules' statics:
  - `task_table[i].handle = NULL; .paused = false; task_event_group_handle = NULL;`
  - `health_eg = NULL; expected_bits = 0; period_ticks = 0; next_deadline = 0; iwdg_handle = NULL;`
  - Then call `health_init();`.
- **R3. `tearDown()` checks balance.** Assert `fake_rtos_critical_depth()==0` and `fake_rtos_suspend_all_depth()==0`.
- **R4. Known-bug policy.** The first line of the test body is `TEST_IGNORE_MESSAGE("KNOWN BUG KB-n: <one line>");`. Keep the real assertions below it, so fixing the bug means deleting one line. Every KB/KI is listed in `test/README.md`.
- **R5. Comments and names.** Put a one-line `/* [Intent] ... */` above each test. Test names read as commands: `test_<action>_<expected>`.
- **R6. `main()` template:**
```c
// support/pocat_test_main.h
#include "unity.h"
#include <stdio.h>
#include <string.h>
static int pocat_list_only = 0;
static inline int pocat_parse_args(int argc, char **argv) {
    for (int i = 1; i < argc; i++) if (strcmp(argv[i], "-l") == 0) { pocat_list_only = 1; return 0; }
    return UnityParseOptions(argc, argv);   /* -f <substr>, -n <exact>, -x <exclude> */
}
#define POCAT_RUN_TEST(fn) do { if (pocat_list_only) { printf("%s\n", #fn); break; } \
    Unity.CurrentTestName = #fn; if (UnityTestMatches()) { RUN_TEST(fn); } } while (0)
// in each suite:
int main(int argc, char **argv) { if (pocat_parse_args(argc, argv) != 0) return 1;
    UNITY_BEGIN(); POCAT_RUN_TEST(test_x); /*...*/ return UNITY_END(); }
```

### B6. MVP suite contents

**`test_notifications.c`** (real `notifications.c`):
- `test_wait_returns_zero_when_nothing_pending`
- `test_wait_returns_pending_bits_and_drains_them`: the second call returns 0.
- `test_set_bits_notifications_accumulate`
- `test_obdh_index1_does_not_leak_into_index0`
- `test_global_pause_resume_bits_do_not_collide_with_task_bits`
- `test_exit_state_group_mask_is_four_bits`
- `test_tm_task_bits_equal_ack_event_bits`
- Fake self-tests:
  - `test_fake_queue_is_fifo_and_reports_full`
  - `test_fake_event_wait_returns_bits_and_clears`
  - `test_fake_detects_forever_block`: use a `fake_rtos_expect_deadlock(bool)` switch that turns the fail into a recorded flag.

**`test_task_management.c`** (`#include "task_management.c"` + `"health.c"`, block hook = `sim_tasks_run_pending`):
- `test_create_tasks_creates_all_seven_named_tasks`
- `test_task_number_equals_table_index`
- `test_table_ack_bits_match_tm_bits`
- `test_health_expected_only_for_running_tasks`
- `test_create_failure_returns_pdFAIL`
- `test_pause_notifies_only_selected_tasks`
- `test_pause_handshake_marks_paused_and_drops_health_bit`
- `test_resume_handshake_restores_health_bit`
- `test_paused_task_defers_other_notifications`
- `test_unresponsive_task_times_out_and_pause_returns`
- `test_health_fault_on_obdh_resets_obdh_not_eps`
- `test_get_task_handle_null_before_creation`
- `test_pause_and_resume_pending_together_leaves_task_running`: IGNORE, KI-4.

**`test_state_machine.c`** (`#include` `state_machine.c`, `task_management.c`, `health.c`; `fake_clock` starts at `CLK_FREQ_80MHZ`):
- `test_freq_for_state_mapping`
- `test_boot_NM_runs_all_tasks`
- `test_boot_CM_pauses_only_payload`
- `test_boot_SM_pauses_payload_adcs_beacon`
- `test_NM_to_SM_switches_clock_to_2MHz_once`
- `test_NM_to_CM_does_not_switch_clock`
- `test_transition_persists_current_state`
- `test_transition_pauses_old_and_resumes_new_tasks`
- `test_non_exit_notification_changes_nothing`
- `test_multiple_exit_bits_NM_has_priority`
- `test_transition_preserves_previous_state`: IGNORE, KB-3.
- `test_transition_does_not_touch_deployment_flags`: IGNORE, KB-3.

**`test_comms_frame.c`** (`#include "auth.c"`; `frame.c` and `dedup.c` linked):
- `test_sha256_abc_known_answer`: expected `ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad`.
- `test_hmac_rfc4231_case2_first8`: key `"Jefe"` zero-padded to 16 bytes, msg `"what do ya want for nothing?"` (28 bytes), expected `5b dc c1 46 bf 60 75 4e`.
- `test_encode_decode_roundtrip`
- `test_encode_sets_auth_flag_and_total_length`: 5 + len + 8.
- `test_decode_rejects_tampered_payload`
- `test_decode_rejects_wrong_version`
- `test_decode_rejects_missing_auth_flag`
- `test_decode_rejects_truncated_frame`
- `test_retx_flag_does_not_change_tag`
- `test_encode_accepts_242_rejects_243`
- `test_ack_frame_is_16_bytes`: documents KB-2.
- `test_decode_rejects_len_above_payload_max`: IGNORE, KB-1.
- `test_dedup_finds_added_entry`
- `test_dedup_evicts_oldest_after_16`
- `test_dedup_truncates_ack_to_16_bytes`

### B7. Steps and acceptance criteria
Each step leaves the build working.
1. Extend the HAL/RTOS stub headers, add `stm32l476xx.h` and `timers.h`, and write `fake_rtos.c/h` and `fake_hal.c/h`.
2. Rewrite `test/CMakeLists.txt` (B2) with only `test_ht_handling` registered, update its `main()` (R6), and rewrite `run_tests.sh` (B3) and `Dockerfile.test`. Verify that ht gives the same 11/3 result.
3. Add `test_notifications`.
4. Add `stub_task_entries.c`, `sim_tasks.c/h` and `test_task_management`.
5. Add `fake_clock.c/h`, `fake_obdh_requests.c/h` and `test_state_machine`.
6. Add `test_comms_frame`.
7. Update `test/README.md`: commands, how to add a suite in 2 steps, conventions R1–R6, fakes overview, and the known-issues table from A6.

If you're blocked at a step, stop and report. Don't weaken assertions.

**Acceptance criteria (for the reviewer)**
- **AC1. Clean build.** The Docker build and run commands build all 5 suites with no errors and no warnings from new test files.
- **AC2. Results.**
  - `ctest` lists 5 tests. The 4 new suites pass; their IGNOREs are only `KNOWN BUG/KI` tests.
  - `test_ht_handling` gives exactly 11 PASS and 3 FAIL. Its assertions are unchanged; diff shows only `main()` changed.
  - Because of those 3 failures, the overall exit code is non-zero, as it is today.
- **AC3. Commands.** These all work: `--list`, `--label obc`, `state_machine`, `test_state_machine -l`, `test_state_machine -f NM_to_SM`.
- **AC4. One-line registration.** Adding a suite needs no change to `run_tests.sh`.
- **AC5. Scope of changes.** `git status` shows changes only in `test/`, `run_tests.sh`, `Dockerfile.test` (and optionally `CLAUDE.md` or `.gitattributes`). Nothing changed in `src/`, `include/` or `external/`.
- **AC6. Sanitizers.** Sanitizers are active on the new suites. Any `NO_SANITIZE` has a justifying comment.
- **AC7. Fake behaviour.** A forever-blocking wait is detected. Each `KNOWN BUG` test, with its IGNORE line removed, fails for the stated reason.
- **AC8. Line endings.** `run_tests.sh` and new files use LF.

### B8. Pitfalls
- `include/utils/time.h` can shadow `<time.h>` for angle-bracket system includes. If you see `time_t` or `timespec` errors, it's this. Report it instead of using `-isystem`.
- 64-bit host: `uxTaskGetTaskNumber` returns `UBaseType_t` (unsigned long). Pointer↔`uint32_t` casts will warn; there's no `-Werror` on production files.
- `obdh_submit_request()` initialises `value = 0` (`HAL_OK`), so without the deadlock rule an unserviced request would look successful. Keep the rule.
- `state_machine.c` and `task_management.c` print with `printf`; that's expected in the test output.
- The block hook must not run re-entrantly.
