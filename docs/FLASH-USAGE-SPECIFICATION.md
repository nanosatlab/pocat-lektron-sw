# Flash Usage Specification — PoCat-Lektron OBSW

| | |
|---|---|
| Status | DRAFT 0.16 — memory map redesigned and agreed on 2026-10-26/27 (§5.3). Steps 1 and 2 of §10 implemented on 2026-10-27: the map, the driver's region guard, the 256 KB code limit, HT in its 64 KB region, the parameter store (§5.4) and the flash ECC NMI handler. On 2026-10-28 OBDH was confirmed as the storage system and the request standard agreed (§5.2); the plan was revised to match (§10). Steps 2b, 2c and 2d implemented on 2026-11-12: the parameter semantics, the request protocol and OBDH's recovery (driver on the flash registers, stores reloaded at each OBDH start, clock switch only after OBDH acknowledges the pause). Step 3 implemented on 2026-11-21: the mode, the EPS thresholds, the RFI configuration, the TLE and the ADCS calibration are parameters, the boot time is no longer stored, and nothing writes the old map. Step 4 split in two the same day (§10), and both halves implemented: 4a, the ring module that the HT, payload and OBC-log stores share (§5.4), and 4b, the HT store on the ring inside OBDH, fed by posts from the beacon task, with its credit starting at 0 at boot (O21 closed). Step 5 split in two the same day (§10). 5a implemented: the HK history downlink sends a RAM copy of the stored HT blocks, chosen by sequence number (§5.4); the OBC-log downlink sends a test pattern and the payload downlink answers "no data" until their stores exist; the old map and the old-map and low-level requests are deleted, and the driver and the map moved under `subsystems/obdh/`. 5b implemented on 2026-11-24: a beacon period outside its limits is refused rather than clamped (FLS-RATE-010 reworded), a new period takes effect only once stored as `PARAM_BEACON_PERIOD`, and the beacon starts with the stored period. The same day `FLASH_MAP_VERSION` was removed. The map is frozen before flight acceptance (§5.3). Also on 2026-11-24 the flash ECC correction interrupt was enabled at boot. Its handler clears the correction flag at once and counts each correction event the hardware flags, with its address; until then a correction anywhere but in OBDH's reads left the flag set, and so blocked the NMI for a double error, until OBDH's next read (FLS-INT-040, FLS-INT-070). Host tests: `test_flash_driver`, the real driver over fake registers (the handler, the clear before each read, the region guard, the read-only unit data). Whether the handler's access to FLASH_ECCR waits during a program or erase is still to be measured (V15). On 2026-11-25 the driver's program and erase were checked step by step against RM0351 and the HAL functions they replace, and simplified (FLS-DRV-090): the flash control register is never written while the flash is busy, so an operation that times out leaves it as it is and the next one clears what was left (FLS-DRV-110, new). The driver's check that the target of a program is erased before anything is programmed is kept and now stated as FLS-DRV-120 (new): the chip's own check stops a program only part way and lets all-zero data overwrite stored data. `test_flash_driver` now runs program and erase on a fake flash controller that fails the test where the chip would stall or lock up (V13 on the driver). The driver then called `HAL_FLASH_Unlock()`, `FLASH_PageErase()` and `HAL_FLASH_Lock()` instead of copies of them; the HAL's program and erase functions and their wait are still not used (FLS-DRV-090). The flash interface reset then stopped unlocking FLASH_CR only to lock it again: it finishes the operation a deleted task left, clearing PG, PER and PNB and locking FLASH_CR only if that task left it unlocked, so a restart with the flash idle writes no key (FLS-REQ-070). Also on 2026-11-25 a read was made to check the double-error flag ECCD as well as the NMI's count: nothing specifies how soon the NMI arrives after the read that finds the error, so the read reads ECCD first and the count second, and reports the error whether or not the NMI has run (FLS-INT-040). `test_flash_driver` runs the real NMI handler before the check and leaves it unrun. Later on 2026-11-25 the ring's slot header was changed (§5.4): a 2-byte check of the length and the sequence number replaced the store's tag and format, so the scan at loading passes over a header damaged without an ECC error, whose number could otherwise send the writer back into the history. Parameter copies are identified by their fixed addresses. Their half headers now hold the sequence number and its complemented CRC in one doubleword (§5.4, FLS-INT-090). The same day `PARAM_OBC_MODE` became one byte, the mode to boot in: the previous mode it also held, carried over from the old map, had no reader (§6). Also on 2026-11-25 the rule that only OBDH runs the stores became a check in the code: the store functions that run in OBDH refuse any other caller except `main()` before the scheduler starts, with `HAL_ERROR` and nothing done (§5.2.1 rule 9, FLS-REQ-110, V16). On 2026-11-26 `PARAM_OBC_MODE` was removed: every boot starts in Init (SPF §1.6.1), whose completed steps the flags `PARAM_STANDBY_DONE` and `PARAM_DEPLOY_COMMS` skip and which only the ground's `TC_PING` ends, so the mode is no longer stored (FLS-RATE-030 withdrawn, ID `0x01` left unused, §6). Init runs at 2 MHz and writes flash, so O19 now concerns every boot; O22 and O23 are new (§11) |
| Date | 2026-10-27 |
| Applies to | On-board software, STM32L476RG internal flash (branch `obdh-compression` and later) |
| Sources | Satellite Project File `PLK_SPF_2025-1504_v1_3` (SPF), RM0351 reference manual, STM32L476xx datasheet DS10198 (Rev 2), OBC hardware design document, code at commit `86aab76` |

Values marked **TBC** (to be confirmed) are proposed defaults the team may change; open points are listed in §11.

---

## 1. Summary

**Verdict on the current design.** Flash wear is a real mission risk today, and not only in fault cases:

| Case (current code) | Erases on the worst page | Reaches 10,000 cycles in |
|---|---|---|
| Config page, normal operations (≈3 boots, 1 TLE upload, 2 mode changes per day) | ≈8 / day | 3.4 years (58 % used after 2 years) |
| Config page, once automatic EPS mode changes follow eclipse (≈30 changes/day) | ≈64 / day | 5 months |
| Config page, watchdog reboot loop (33 s period) | ≈2,600 / day | 4 days |
| Historic telemetry, 8 KB region, measured compression (flight-like to noisy sensors, §8) | 2.9–4.5 / day | 6–9.5 years (52–82 % used after 5 years) |

The SPF makes resets a normal event (periodic reboots, 72 h command-less reboot, MCU power cut at low battery, "we will heavily rely on resetting the satellite"), so the reboot-loop row is a credible failure, not a corner case.

Two further problems were found that are not wear but can end the mission through the flash:

- A power cut during a flash write can leave a doubleword that fails ECC. Reading it raises an NMI, the NMI handler is `while(1)`, and `main()` reads the state byte from flash before anything else. The result is a permanent boot loop.
- A power cut during any config write loses the whole 2 KB page (state, deployment flags, TLE, calibration together), because every write erases the page and rewrites it.

**What to do.** Eleven measures, implemented in the steps of §10:

1. Take every write off the boot path. Boot time moves to RAM and uptime comes from the tick counter.
2. Stop the page-rewrite primitive (`flash_write`); all stores become append-only.
3. Move all runtime data to the top 256 KB of flash bank 2; bank 1 holds only code and space reserved for a backup image.
4. One map header (`flash_map.h`) with compile-time checks; every region has one owner and one store type; no absolute addresses elsewhere.
5. One parameter store for all persistent variables: addressed by ID, two copies, CRC on every record.
6. Fixed-slot circular queues for payload data and telemetry history.
7. An erase governor in the driver, and no erase in the first 5 minutes of uptime.
8. Bound the inputs: refuse a beacon period outside its limits and rate-limit telemetry ingestion.
9. Grow the HT region from 8 KB to 64 KB.
10. Make the NMI handler recover from flash ECC errors; read-back verify; guard the driver's address range.
11. Report wear in telemetry and prove the budget with tests (soak, boot-loop, power-cut).

**Result if implemented.** Every page stays at or below about 10 % of rated endurance over 5 years in expected use, the parameter store below 3 % even under heavy ground use, and every page below 37 % even if a fault drives a region at the governor limit for the entire 5 years (§8).

---

## 2. Inputs

### 2.1 Mission (from the SPF)

| Fact | Value | SPF |
|---|---|---|
| Operational phase | 1–2 years | §1.3 |
| Maximum orbital lifetime | 5 years (debris rule) | §1.4, §7.6.3 |
| Orbit | 450–500 km, period ≈94.5 min (≈15.2 orbits/day) | §1.4, Annex G |
| Passes | ≈2.5 uplink and ≈1.6 downlink per day; 1 per day in the adverse case | Annex G.2 |
| Downlink capacity | ≈12.5 KB per pass, ≈22 KB per day | Table G.6/G.7 |
| Payload data | K-band 2.7 KB per measurement, L-band 0.7 KB | §1.2 |
| Housekeeping | < 50 bytes per stored telemetry record | §3.6.6 |
| Resets | Periodic reboot (configurable), reboot after 72 h without telecommand, MCU power cut at low battery, reset as main recovery | §1.6.3, §3.1.5, §7.6.3 |
| Modes | Init, Nominal, Contingency, Sunsafe, Survival; automatic downward transitions on battery level | §1.5 |
| Init flags | "standby done" and "deploy comms done" must survive a reboot | §1.6.1 |
| Redundancy | "Critical configurations are saved across multiple pages"; redundant storage with periodic scrubbing | §2.2 ST[06], §7.5 |
| Clear commands | Clear HT, clear experiments, clear all, hard reboot (clears stored data) | §2.4.1, Table 3.16 |
| Code size | ≤ 300 KB expected (79 KB today) | §3.6.6 |
| In-orbit software patch | Not planned; ESA recommends keeping the option | §2.1, §3.6.4 |

**Design life for wear: 5 years (1,826 days).** The operational phase is 2 years, but the software keeps running until re-entry.

### 2.2 Hardware (STM32L476RG)

| Fact | Value | Source |
|---|---|---|
| Flash | 1 MB, two 512 KB banks, 2 KB pages (256 per bank) | RM0351 |
| Bank 1 / Bank 2 | `0x08000000–0x0807FFFF` / `0x08080000–0x080FFFFF` | RM0351 |
| Endurance | 10,000 erase cycles per page minimum, valid from −40 °C to +105 °C | DS10198 Table 51 |
| Data retention | 30 years at 85 °C after up to 1,000 cycles; 15 years at 85 °C after 10,000 cycles | DS10198 Table 51 |
| Wear unit | The page erase. Programming erased cells does not add a cycle. | — |
| Page erase time | 22.0 ms typical, 24.5 ms maximum | DS10198 Table 50 |
| Doubleword program time | 82 µs typical, 91 µs maximum (≈21 ms for a full page) | DS10198 Table 50 |
| Program unit | 64-bit doubleword + 8 ECC bits. A programmed doubleword cannot be programmed again, except with all zeros. | RM0351 "Flash main memory programming sequences" |
| ECC | 1-bit errors corrected (ECCC flag). 2-bit errors set ECCD and **raise an NMI**. | RM0351 "Error code correction" |
| Read while write | Erasing or programming one bank stalls every read of the **same** bank; the other bank stays readable. | RM0351 "Read-while-write" |
| RTC backup registers | 32 × 32 bits. Kept across every system reset; lost on backup-domain power loss. | RM0351 RTC |
| Write protection | 2 WRP areas per bank, set in option bytes | RM0351 |
| SRAM2 | 32 KB at `0x10000000`. Kept across system resets when option bit `SRAM2_RST` = 1 (factory default); lost at power-off. | RM0351 |
| Dual-bank boot (`BFB2`) | When set, the boot loader can start the image in bank 2 and swaps the two bank addresses. | RM0351 |

VBAT (pin 1) is connected to the general 3.3 V rail (OBC hardware design document, MCU pin table). Backup registers and RTC time therefore survive watchdog, software and pin resets, and are lost when the EPS cuts MCU power.

The datasheet figures are from DS10198 Rev 2 (2015), read from a mirror; they should be re-checked once against the current revision on st.com. Staying under the 1,000-cycle design target (§4) also keeps every page in the best retention class.

---

## 3. Findings on the current design

Evidence is from the code at `86aab76`; F14 and F15 were found on 2026-10-28, F16–F19 on 2026-11-11 in a review of the plan (code at `7314944`).

| ID | Finding | Evidence | Consequence |
|---|---|---|---|
| F1 | Every non-HT write erases a whole page, even for one byte. | `flash_write` in `src/utils/flash.c` (read page → erase → reprogram 2 KB) | 1 erase per write |
| F2 | One page (`0x08030000`) holds mode, boot time, deployment flags, config, TLE and calibration. | `include/utils/flash.h` | All writers add up on one page |
| F3 | Boot time is written on every boot. | `obc.c:92` | 1 erase per reset; a reboot loop wears the page out in days |
| F4 | A mode change costs 2 erases and is not skipped when the new mode equals the current one. | `state_machine.c:107,125` | Repeated TC or eclipse-driven transitions multiply wear |
| F5 | Config writes are not power-fail safe: the page is erased before the new content is written. | `flash_write` | A power cut loses state, flags, TLE and calibration at once; no CRC, no second copy (contradicts SPF §2.2 ST[06]) |
| F6 | An ECC double error in flash locks the satellite in a boot loop. | `NMI_Handler` is `while(1)` (`stm32l4xx_it.c:71`); `main.c:50` reads flash first | Mission loss after one torn write |
| F7 | Config data is in bank 1 with the code. | address map | Each erase stalls all execution, including interrupts, for ≈22 ms (plus ≈20 ms to reprogram the page) |
| F8 | The driver accepts any address. | `flash_write`, `flash_erase_page` | A bad address erases code |
| F9 | Beacon period is unclamped and each beacon feeds HT. | `tc_handler.c:271`, `beacon.c` | A 1 s period multiplies HT wear by 30; period 0 is passed to `xTimerChangePeriod` |
| F10 | HT stops for good when a page fails to erase. | `ht_store_block` returns without advancing | Storage halts; the bad page takes one erase per block |
| F11 | Flash requests block the caller forever. | `obdh_submit_request` uses `portMAX_DELAY` | A stuck flash operation freezes OBC, beacon and COMMS tasks |
| F12 | The 8 KB HT region is small for both wear and history. | `HT_REGION_SIZE` | 21–33 % of endurance in 2 years and 52–82 % in 5; 5–8 h of history |
| F13 | No wear telemetry. | — | Ground cannot see a problem developing |
| F14 | An HT slot is written header first and has no CRC. | `ht_store_block` programs the 128-byte slot from its start; `ht_slot_header_valid` checks only header fields | A power cut partway through a slot leaves a valid-looking block over a damaged payload; `ht_init` counts it and the ground decodes garbage. Contradicts FLS-INT-010 |
| F15 | An OBDH task restarted in the middle of a flash operation leaves the HAL flash lock taken. | The health monitor deletes a task that misses its 5 s report (`tm_handle_health_faults`); the HAL holds `pFlash.Lock` during an operation and waits up to 50 s (`FLASH_TIMEOUT_VALUE`) | Every later program and erase returns `HAL_BUSY` until the MCU resets |
| F16 | OBDH answers a request by notifying the task handle stored in it. | `obdh_submit_request` stores `xTaskGetCurrentTaskHandle()`; `process_obdh` calls `xTaskNotifyIndexed(request.client, …)`; the health monitor deletes and recreates tasks (`tm_reset_task`) | An answer to a deleted task writes into freed memory, or into the task that reused it; a restarted task can take the answer to its predecessor's request as its own |
| F17 | The OBC switches the clock even when a task did not acknowledge the pause, and a task that gets the pause and the resume together stays paused. | `notify_and_wait` only prints the missing acknowledgements and `change_state` goes on to `clock_switch_to_freq`; `tm_check_pause` handles the pause and drops the resume | The clock can switch while OBDH is in the middle of a flash operation, which the pause exists to prevent; a late task can stay paused, outside health monitoring |
| F18 | A copy whose compaction keeps failing erases its waiting half at every write. | `compact()` erases a target half that is not blank before each attempt | While the fault lasts, every parameter write costs 4 erases on the same pages |
| F19 | During an erase the OBDH task busy-waits at priority 5, above COMMS, beacon and EPS. | `HAL_FLASHEx_Erase` polls the busy flag until the erase ends (22–25 ms per page) | A parameter write that compacts both copies stops every lower-priority task for up to about 0.4 s |

What is already right and should be kept: the HT queue design (one erase per page per lap, scan at boot, no persisted pointers, sequence numbers independent of the RTC), the single gatekeeper task for flash access, and the ARQ upload path (buffer in RAM, check CRC, write once).

---

## 4. Wear policy

| Limit | Per page, over 5 years | Per page, per day | Meaning |
|---|---|---|---|
| Rated | 10,000 | 5.5 | Datasheet guarantee; never to be reached |
| Governor (hard) | 3,652 (36.5 %) | 2.0 average over a region | Enforced in software; no fault or command can exceed it |
| Design target | 1,000 (10 %) | 0.55 | Expected use must stay below this |

The factor 10 between target and rating covers temperature cycling, radiation, cell-to-cell spread and the uncertainty in the usage estimates.

**Formulas.** For a circular log with one erase per page per lap:

```
erases per page per day = bytes written per day / region size in bytes
```

For the parameter store (append records; when a half fills, copy the live records to the other half and erase the full one, so each page is erased once every two compactions):

```
erases per page per day = bytes written per day / (half size − header − live set) / 2
```

---

## 5. Storage architecture

### 5.1 Where data lives

| Place | Survives | Wear | Use for |
|---|---|---|---|
| Flash, parameter store (PARAM A and B) | Reset and power cut | 1 erase per many writes | Everything that must persist: mode, flags, configuration, TLE, calibration, and every EPS and ADCS value |
| Flash, rings (HT, PL, OBC event log) | Reset and power cut | 1 erase per page per lap | Telemetry history, payload measurements, OBC events |
| Flash, unit data | Everything; written once on the ground | None in flight | Satellite ID, hardware revision, ground calibration |
| SRAM2, reset record | Reset; not a power cut | None | Boot counter, reset history, crash record, erase-governor credit |
| RAM | Nothing | None | Live readings, error flags, protocol buffers, uptime, current mode |
| RTC | Reset; not a power cut | None | Current time |

How to decide where a variable goes:

1. Can the satellite get it back by itself after a restart (re-read a sensor, recompute, safe default)? Then RAM.
2. Does only the ground know it (uploaded)? Then PARAM.
3. Would losing it cause something harmful or unrepeatable? Then PARAM.
4. Does it change by itself more than a few times a day (state of charge, filter states, counters)? Then never PARAM: RAM, or SRAM2 if it should survive a reset.

Every persistent variable listed in §6 today changes only on a telecommand, an upload or the end of Init's standby. The mode is not stored: every boot starts in Init (SPF §1.6.1), so a mode change writes nothing. The EPS and ADCS parameters, including any their software saves by itself, have been requested from both teams (O14).

**Not in the baseline:** RTC backup registers (SRAM2 covers the same need; VBAT is on the 3.3 V rail, so they would not survive a power cut either); a separate region for the most critical flags (O12).

### 5.2 Software layers

OBDH is the storage system (decided 2026-10-28): it is the only task that touches flash, and every store runs inside it. This is the gatekeeper pattern of the FreeRTOS book (*Mastering the FreeRTOS Real Time Kernel*, §8.4): one task owns a resource, and the others use it only through that task's services, which gives mutual exclusion without the priority inversion and deadlock risks of a mutex (§8.3). A mutex has one more risk here: the health monitor deletes and recreates a task that stops reporting, and a task deleted while holding the mutex would leave storage locked for good.

| Layer | Module | Responsibility |
|---|---|---|
| Driver | `obdh/flash.c` (in `utils/` until step 5a) | `flash_program`, `flash_erase_page`, `flash_read`, each given a region of `flash_map.h` and an offset inside it, refusing anything outside it; ECC handling; later the erase governor, counters and read-back verify. Called only by the stores, in the OBDH task. `flash_write` was removed in step 3, `flash_read_old` in step 5a. |
| Stores | `obdh/param_store.c`; `obdh/ht_store.c`, the HT store (step 4b), which runs POCKET+ from `utils/compression.c`; the payload store and the OBC event log (new); the SRAM2 reset record. The HT, payload and OBC-log stores keep their blocks with the ring module `obdh/ring.c` (step 4a, §5.4) | Record and slot formats, CRC, copies, page rotation, scan at boot, rate limits on what clients send. Each store function that runs in OBDH refuses any other caller (§5.2.1 rule 9) |
| Store functions | One set per store, in the store's module | Requests and posts that send one message to OBDH (§5.2.1), and plain functions such as `param_get()` that never involve the OBDH task |
| Clients | Subsystem code | Call store functions only: never the driver or a flash address. Since step 5a OBDH has no other requests: the low-level ones were deleted |

The owner of a region in §5.3 is whose data it holds; OBDH runs every store and is the only writer.

#### 5.2.1 Requests and posts

Agreed on 2026-10-28 and corrected on 2026-11-11 after a review of the plan; implemented in steps 2c and 2d (§10). It follows the FreeRTOS conventions for waiting and for passing data between tasks (book chapter 5).

| Kind | Name | The caller | Used when |
|---|---|---|---|
| Request | `<store>_<action>_request(…, ticks_to_wait)` | Waits for OBDH's answer, at most `ticks_to_wait` | The caller needs the result: a new beacon period must be stored before it takes effect, a TLE before ARQ confirms it to the ground |
| Post | `<store>_<action>_post(…)` | Continues at once and gets no answer | Nothing depends on the result: a beacon into HT, an event into the OBC log |
| Plain | Any other name, such as `param_get()` | Never involves the OBDH task | Reading what a store keeps in RAM |

1. **Block time.** A request takes a block time chosen by the caller, like every FreeRTOS call that can block (`xTicksToWait`, book §5.2.3–5.2.4). It is one deadline for the whole request: waiting for room in the queue, then for the answer (`vTaskSetTimeOutState`, `xTaskCheckForTimeOut`). A request returns OBDH's status; `HAL_BUSY` if the queue stayed full for the whole block time, so nothing was sent; or `HAL_TIMEOUT` if it was sent and no answer came in time, which means "result unknown": OBDH may still carry it out later. Callers normally pass the standard block time `OBDH_REQUEST_WAIT`.
2. **Data by copy.** A request or post carries a copy of its data inside the queue entry (queue by copy, book §5.2.1). Nothing in the queue points into a caller's memory, so a caller that gave up leaves nothing behind. A queue entry holds up to `OBDH_DATA_MAX` bytes (256): the largest parameter value (138 bytes today) and an HT block (112 bytes) fit. Payload measurements are too large to copy; they will be passed by reference with explicit buffer ownership, defined with the payload store.
3. **Answers.** OBDH answers through reply queues, never through a task handle (F16). Each client, a task of the task table or the OBC task, has a one-entry reply queue, created with the request queue at boot, never deleted, and found by the task's index in the task table, which a restarted task keeps. OBDH answers with `xQueueOverwrite`, so it never waits for a client. Every request carries a number and the answer carries it back. A client keeps its numbers in static memory, so a restarted task continues the sequence. A client ignores an answer with another number, which belongs to an earlier request that timed out, and keeps waiting within the same deadline. A request that returns data, such as the read of an HT block, carries none itself (its length is 0) and gets the data back by copy in the answer. The client copies it into the caller's buffer only if it fits; a longer block returns `HAL_ERROR` with nothing written.
4. **Posts never wait.** A post goes into the queue only while more than `OBDH_CLIENTS` entries are free (8 of the 10): posts leave one entry for each client's request, so they can never keep a request out (FLS-REQ-050, V14). A post that finds no such room is dropped and counted; the count is not in telemetry yet. OBDH serves a post like a request but answers nothing. The first post is the HT store's `ht_beacon_post()` (step 4b).
5. **Restarts.** The health monitor deletes and recreates a task that stops reporting (`tm_handle_health_faults`). When OBDH starts, before it serves a request, it puts the flash interface in a known state (an operation cut short leaves it busy or unlocked) and rebuilds every store's state from flash, because the task may have stopped between writing flash and updating RAM. Parameter values are published under the lock that `param_get()` uses. OBDH does not answer the request it was serving: its caller gets `HAL_TIMEOUT` from its own block time, and "unknown" is the truth, since the operation may have completed. Requests and posts still in the queue are served normally. What a store keeps only in RAM is lost, such as the HT block being filled; HT's credit is kept, since it counts time since boot (§5.4).
6. **Waits and the health period.** A task that goes longer than the health period without reporting is restarted, whatever it was waiting for. The rule is per task: its longest stretch between two health reports, waits included, must be shorter than the health period. The build checks that the standard block time is shorter than the health period, both named constants (FLS-REQ-060), but that is only a sanity check. Inside OBDH, every flash wait is bounded by the datasheet's maximum time with a margin (FLS-DRV-090), so the driver runs program and erase itself, calling only the HAL functions that neither wait nor lock: the HAL's program and erase functions wait up to 50 s and hold a lock that a deleted task would leave taken (F15). While an erase runs, the driver sleeps between checks, so lower-priority tasks keep running (F19).
7. **Pauses.** OBDH is paused while the OBC changes state, so that no flash operation runs during the clock switch. The pause reports which tasks acknowledged it. Without OBDH's acknowledgement the OBC does not switch the clock: it resumes the tasks, keeps the current mode, counts the failure and tries again (F17). A resume that reaches a task together with a pause wins, and the task acknowledges it; a running task acknowledges a resume too. A task acknowledges a pause and a resume on separate event bits, so a late acknowledgement of a resume is never taken for the acknowledgement of a pause. While paused, OBDH sleeps until its next notification instead of polling, so lower-priority tasks keep running, including those that still have to acknowledge their own pause. A request sent during a pause waits for the resume, within its block time. The OBC never sends a request while it holds OBDH paused: it would be waiting for itself.
8. **Boot.** The only flash access outside the OBDH task is the read-only scan of the parameter store in `main()`, before the scheduler starts, so that every value is in RAM before any task reads it: the OBC reads the activation flags, which decide where Init starts, before OBDH exists. `main()` also updates the SRAM2 reset record there. When OBDH starts it scans every store, the parameter store again included (rule 5).
9. **Callers.** The store functions that run in OBDH check their caller first: today `param_store_load()`, `param_store_write()`, `ht_store_load()`, `ht_store_add()` and `ht_store_read()`. Only the OBDH task may call them, and `main()` before the scheduler starts (rule 8). Any other caller gets `HAL_ERROR` before anything is read, programmed or changed, so a task that calls one by mistake leaves the store as it was (FLS-REQ-110). The check, `obdh_is_caller()` in `obdh.h`, compares the running task with OBDH's row of the task table. A restarted OBDH is in that row before it can run, because `xTaskCreate()` stores the new handle before the task is ready; while OBDH is deleted the row is NULL, which no running task matches. A wrong caller is refused, not caught with `configASSERT()`, which in this build disables interrupts and waits for the watchdog to reset the board. The ring module and the driver have no check of their own: only the stores call them, and OBDH's start (the flash interface reset). Requests, posts and plain functions such as `param_get()` are not checked: any task may call them.

### 5.3 Memory map

Agreed on 2026-10-26 and 2026-10-27; it replaces the map of DRAFT 0.4. It is defined in `include/subsystems/obdh/flash_map.h` (in `include/utils/` until step 5a), the only file allowed to contain a flash address (FLS-GEN-040, FLS-GEN-050). Pages are 2 KB; each bank has 256 pages, numbered 0–255 within the bank.

**Top level**

| Address range | Size | Bank | Content |
|---|---|---|---|
| `0x08000000–0x0803FFFF` | 256 KB | 1 | Code. The linker limits the program image to this (82 KB on 2026-10-27; SPF estimate ≤ 300 KB). A bootloader, if one is adopted, takes the first 16 KB. |
| `0x08040000–0x0807FFFF` | 256 KB | 1 | Reserved for a backup program image (§11). Kept erased. |
| `0x08080000–0x080BFFFF` | 256 KB | 2 | Reserved for staging an uploaded image (§11). Kept erased; becomes free data space if no update scheme is adopted. |
| `0x080C0000–0x080FFFFF` | 256 KB | 2 | Data area |

**Data area**

| Address | Size | Pages (bank 2) | Region | Owner | Store | Size status |
|---|---|---|---|---|---|---|
| `0x080C0000` | 64 KB | 128–159 | HT: telemetry history | COMMS | Ring, 128-byte slots | Set by the wear analysis (§8) |
| `0x080D0000` | 64 KB | 160–191 | PL: payload results | Payload | Ring of fixed slots | Placeholder, TBC by Payload (O5) |
| `0x080E0000` | 64 KB | 192–223 | Free | — | — | What the packed regions leave |
| `0x080F0000` | 16 KB | 224–231 | PARAM B: parameter store, copy B | OBDH, for every subsystem | Parameter store | Fixed |
| `0x080F4000` | 16 KB | 232–239 | OBC event log | OBC | Ring | Placeholder, TBC by OBC (O15); may grow into the spare |
| `0x080F8000` | 8 KB | 240–243 | Spare | — | — | — |
| `0x080FA000` | 16 KB | 244–251 | PARAM A: parameter store, copy A | OBDH, for every subsystem | Parameter store | Fixed |
| `0x080FE000` | 8 KB | 252–255 | Unit data | Ground | Written once, write-protected | Fixed |

HT, PL and Free form the **packed part**: their sizes are placeholders, and each base is computed from the region before it. From `0x080F0000` to the end of flash is the **fixed block**: its addresses never move. Free and spare space are not regions; nothing may write there.

EPS and ADCS have no region of their own: they keep only parameters, in the parameter store (decision of 2026-10-26). FSS is undefined and has no region; its parameter IDs are reserved (§6).

**Rules**

1. Code and data are in different banks. An erase in bank 2 does not stall code running from bank 1 (F7).
2. Every byte of data belongs to one region, with one owner and one store type. The driver is given a region and an offset and refuses anything outside the region (FLS-DRV-010).
3. There are three store types: parameter store, ring, and written once.
4. The memory map identifies each store and each parameter copy by address. The firmware defines how to read the bytes in that region (§5.4). Erase affected regions before running firmware that moves them or changes their stored layout incompatibly (FLS-INT-090).
5. What must never move (unit data, the parameter copies) sits at fixed addresses at the top. Uncertain sizes are packed below, ordered by certainty, so a size that is still a guess never moves a region whose size is settled.
6. Space for a backup image and for an image upload is reserved now. Adding it after launch would mean moving all the data.
7. The two parameter copies are not adjacent, so one wrong erase cannot hit both.
8. The chip's dual-bank boot (`BFB2`) is never used: it swaps the bank addresses, which would move every data region. A backup image, if adopted, needs a small bootloader that does not swap banks.

**Changing a size before the freeze.** Edit the region's `*_SIZE` line in `flash_map.h`. The compile-time checks stop the build if regions overlap, split a page, leave the data area or run into the fixed block. The regions after the changed one move into Free; on development boards, erase the regions that moved before the new firmware runs, since a ring cannot tell another store's slots from its own (FLS-INT-090). The map is frozen before flight acceptance; after that, nothing moves.

**Sizing a ring.** Take the larger of the history needed and the wear limit, rounded up to whole pages, and at least 4 pages:

```
size ≥ max(bytes stored per day × days to keep, 1.83 × bytes stored per day)
```

The factor 1.83 is 1,826 days ÷ 1,000 erases (§4). At the wear limit a 16 KB ring takes about 9 KB of writes per day, a 64 KB ring about 35 KB.

**Parameter store.** Each copy is 16 KB, split into two halves of 4 pages (§5.4). At the 1,000-erase target, with a 2 KB live set, it takes about 280 small writes per day for the whole satellite; one value saved every hour uses about a tenth of that.

**Unit data.** Per-satellite values measured or assigned on the ground: satellite ID (K-band or L-band unit), hardware revision and ground calibration, in two copies with a CRC. They are programmed with the ST-Link during integration, separately from the firmware, then write-protected, so both satellites run the same firmware. A parameter takes its value from, in increasing priority: the default in the program image, unit data, the newest value written in flight. Two consequences for flashing: the firmware `.bin` never includes the data area (a `.bin` that did would span the whole 1 MB and overwrite it), and flight units are never mass-erased (FLS-GEN-090).

**SRAM2.** The first 1 KB of SRAM2 (`0x10000000`) holds a reset record with a magic number, version and CRC: boot counter, quick-reset counter (boot-loop detection), the last 16 reset causes with uptime and mode, the last crash record, and the erase-governor credit (FLS-DRV-030). It is re-initialised after a power-on (RCC `BORRSTF`) or a bad CRC. It is one of OBDH's storage modules; `main()` updates it at boot, before the scheduler starts (§5.2.1 rule 8). The remaining 31 KB are free for large buffers.

**Option bytes on flight units:** WRP on the code area and on unit data; `BFB2` = 0; `SRAM2_RST` = 1.

**Linker.** `cmake/STM32L476RGTX_FLASH.ld` limits `FLASH` to `FLASH_CODE_SIZE`, 256 KB (FLS-GEN-080): a larger image fails to link instead of spilling into the reserved and data areas.

**Driver guard.** `flash_program`, `flash_erase_page` and `flash_read` take a region of `flash_map.h` and an offset, and return `HAL_ERROR` for any range that is not inside the region; program and erase also refuse unit data. A refused program or erase prints a line on the debug UART. Only the stores call the driver. Until step 5a, OBDH requests also carried a region and an offset (`obdh_read_request`, `obdh_program_request`, `obdh_erase_program_request`); they were deleted, and no request names a flash location now.

**Old addresses.** The addresses of the map before step 1, in the old `include/utils/flash.h` (`0x08030000–0x0803FFFF` and `PHOTO_ADDR`), were deleted in step 5a, with `flash_read_old` and `obdh_read_old_request`, the last code that read them. Since step 3 the parameter store had held their contents, and nothing wrote them. Their three pages, `0x08030000–0x080317FF`, are ordinary code area again: the image may grow to the 256 KB limit (85 KB on 2026-11-21).

No downlink reads flash while it runs (step 5a). The HK history downlink sends a copy of stored HT blocks, taken in RAM when its telecommand arrives (§5.4, HT). Until their stores exist, the OBC-log downlink sends a test pattern and the payload downlink is answered "no data" (§6). Before step 5a, ARQ read these downlinks from the old addresses `TELEMETRY_ADDR` and `PHOTO_ADDR`, as placeholders.

HT moved from its old 8 KB at `0x080FE000` to its 64 KB region in step 1. On boards that ran older firmware, the old HT blocks stay at `0x080FE000` (unit data, which nothing reads yet) until unit data is programmed, and HT history starts empty after the update.

### 5.4 Formats

Rules for every store:

- A record or slot is a whole number of doublewords and carries a CRC, programmed last. Without a valid CRC it does not exist.
- Nothing is modified in place. A new value is a new record; the newest valid record wins.
- A record may be cancelled by programming zeros over it (the one overwrite the hardware allows). The parameter store does not need this: an invalid record is skipped, and the next compaction leaves it behind.
- Each store finds its state by scanning at boot. No pointers are persisted.
- Sequence numbers only increase and are never reset.

**PARAM.** Implemented in step 2 (`src/subsystems/obdh/param_store.c`). Each copy has two halves of 4 pages: one active, one erased and waiting. Records are appended to the active half; nothing is modified in place.

| Part | Size | Content |
|---|---|---|
| Half header | 8 bytes | Half sequence number (4), then the bitwise complement of its CRC-32/ISO-HDLC (4). An erased sequence has CRC `0xFFFFFFFF`, so its check would have to be zero and a fully erased header fails. Compaction programs this doubleword after copying the records. The first record starts at offset 8 within the half. |
| Record header | 8 bytes | ID (1), data length (1; always the parameter's size, so the scan can step over IDs it does not know), check `~(ID \| length << 8)` (2), sequence number (4) |
| Record data | Length, padded with `0xFF` to a multiple of 8 | The value |
| Record trailer | 8 bytes | CRC-32 of the header and the data (4), then its complement (4). Programmed last. |

A 1-byte value takes 24 bytes of flash; a 138-byte TLE takes 160; the newest record of all 13 IDs, 544 (the 2 KB live-set budget is checked at compile time). The sequence number grows by one with every write and is the same in both copies. The half sequence number grows by one with every compaction of that copy; half of it is the copy's erase count.

**Writing** (`param_set_request`, run by the OBDH task). A value equal to the current one, stored or default, is not written (FLS-DRV-070). Otherwise the record goes to copy A and is read back, then to copy B. A copy that lacks newer records (a power cut between the two copies, or a failed write) gets them appended first, at this next write and never at boot. `param_set_request` returns `HAL_OK` when at least one copy holds the new value, so the value survives a reset; a copy that failed is counted, and callers do not retry. `HAL_ERROR` means that neither copy took the record as far as OBDH could check; a record whose read-back failed may still be valid in flash, so after `HAL_ERROR`, as after `HAL_TIMEOUT`, the stored value is the old one or the new one.

**Reset** (`param_reset_request`). It writes the default value from `PARAM_TABLE` like any other value: there is no special record, and no argument has a hidden meaning. It writes nothing when the value already equals the default. A parameter that has never been written has no record and follows the default of the running software, including a different default in a later version; once written, it keeps the recorded value, even one equal to a default, until the next write. Per-unit defaults from the unit data region (§5.3) will replace `PARAM_TABLE`'s defaults for a reset once that store exists.

**Reading** (`param_get`, any task, from RAM). It returns `HAL_OK` and the value, or `HAL_ERROR` for an unknown ID or a wrong size. It does not say where a value came from: the owner checks that a value makes sense (ADCS checks the TLE, for example).

**Compaction.** When the active half has no room, the newest record of every ID is copied to the waiting half (erased first only if it is not blank), its half header is programmed, and the old half is erased. Until that header is programmed the old half is still the active one, so a power cut at any point loses nothing. One compaction erases one half, about every 300 small writes.

**Loading** (`param_store_load`, in `main()` before the scheduler starts, and again each time the OBDH task starts; it reads only). In each copy, the valid half with the higher half sequence number is active, and its records are read in order up to erased flash. A record with a bad CRC is skipped. A damaged record header ends the scan of that copy, because its length cannot be trusted; the copy is rebuilt at the next write. Per ID, the valid record with the highest sequence number in either copy wins. The values are built apart and then copied to RAM in one step, under the lock `param_get` uses. A copy without a readable, CRC-valid half header contributes no values. The next write rebuilds it, erasing the destination half first if needed.

An ID is never renumbered or reused once it has flown. When updating a board that used the previous 16-byte half header, save any required settings, erase **PARAM A and PARAM B** before first boot, and restore those settings through the normal parameter writes. The new loader cannot recover values stored with the previous header.

**Ring.** Implemented in step 4a (`src/subsystems/obdh/ring.c`, `include/subsystems/obdh/ring.h`). The HT store uses it since step 4b; the payload store and the OBC event log will use it too. A ring fills its region with slots of one fixed size that divides the page (32 bytes to 2 KB), and overwrites its oldest blocks page by page.

| Part | Size | Content |
|---|---|---|
| Header | 8 bytes | Check (2): the low 16 bits of the CRC-32 of the length and the sequence number. Data length (2), sequence number (4) |
| Data | Length, padded with `0xFF` to a multiple of 8 | The block. The slot stays erased from there to the trailer |
| Trailer | 8 bytes, at the end of the slot | CRC-32 of the header and the data (4), then its complement (4). Programmed last |

A slot holds a block only if its header passes its check and has a length that fits the slot, and its trailer matches. Anything else counts as blank, including a slot cut short by a power cut (F14). The check lets the scan at loading, which reads only headers, pass over a header damaged without an ECC error, whose number could otherwise send the writer back into the history; such a header still passes about 1 time in 65,536, and an erased or zeroed header never passes. A slot does not name its store or the format of its blocks: the region and the store's code do (FLS-INT-090).

**Writing.** Blocks go to consecutive slots. The writer erases each page as it enters it, which drops the oldest blocks a full ring keeps there: one erase per page per lap. Each slot is read back once programmed. If the erase, the programming or the read-back fails, the writer moves to the start of the next page and tries once more there; if that fails too, the block is dropped and counted. A lasting fault therefore costs at most two erases per block, and a page that no longer erases is skipped at each lap (FLS-INT-050, F10). Each attempt to program a slot takes the next sequence number, so no number is given twice, even when a power cut left a slot half programmed.

**Loading** (each time the OBDH task starts; it reads only). The ring reads every slot header, 8 bytes per slot, and the newest one that passes its check decides where the next block goes and which number it takes. Sequence numbers are compared through their difference, so the order holds when they wrap from `0xFFFFFFFF` to 0 (KI-10). Trailers are not checked at loading (decided on 2026-11-21): checking all of them in the 64 KB HT region would keep the OBDH task busy for about 3 s at the 2 MHz of SURVIVAL.

**Reading.** A read asks for the oldest block numbered from a given number on. Numbers that are missing (blocks dropped, damaged, or erased with their page) are passed over, so a reader that continues from the number it got plus one sees every stored block in order. The trailer is checked here: a block whose trailer does not match is never returned, and the read moves on to the next younger block.

Host tests: `test_ring` (a power cut at every step of an append, with and without ECC errors; a page that fails to erase; a slot that fails to program, or whose header, data or trailer reads back with an ECC error or wrong without one; a header damaged without an ECC error, and a zeroed slot; the wrap of the sequence number). Slots larger than a page, for K-band, come with the payload store once O5 is settled.

**PL.** A ring (above), one measurement per slot.

| Satellite | Measurement (SPF §1.2, Table G.4) | Slot size | Slots |
|---|---|---|---|
| K-band | about 2,831 bytes | 4,096 bytes (2 pages) | 16 |
| L-band | 708 bytes | 1,024 bytes (half a page) | 64 |

The ring's header and trailer carry the sequence number, the data length, the header check and the CRC; the timestamp goes in the data. The length is kept per slot because a change of `RFI_CONFIG` in orbit can change the measurement size while older measurements are still stored. The slot sizes are defaults until the payload owner confirms the largest measurement; K-band's two-page slots need the ring extension above.

**HT.** Implemented in step 4b (`src/subsystems/obdh/ht_store.c`, `include/subsystems/obdh/ht_store.h`, host tests `test_ht_store`). A ring (above) of 128-byte slots, 16 per page and 512 in the region. The block format is `HT_FORMAT` (1) in `ht_store.h`: the HK history downlink sends it, and flash does not store it. Slots written before 2026-11-25 have the tag `0x48` and format 1 where the check now is, so their headers mostly fail it and that history reads as blank; about 1 slot in 65,536 passes and reads as the valid block it is, since the block format did not change. Erase the HT region to start empty. The CRC trailer is programmed last (FLS-INT-010; O18 closed), so a slot cut short by a power cut counts as blank (F14). A block holds up to 112 bytes:

| Bytes | Content |
|---|---|
| 0–19 | The block's first beacon, raw: the 19-byte beacon body, then a zero byte |
| 20 to the end | Each later beacon, compressed by POCKET+, back to back |

POCKET+ (`utils/compression.c`) restarts at each block, with the first beacon as its reference, an update rate of 12, protection level 1 and no anchor deltas, so every block decodes on its own. A beacon is appended while it fits; otherwise the block is stored and the beacon starts the next one. The block has no beacon count and no epoch: each beacon carries its Unix time (bytes 1–4) and its uptime (bytes 15–18), and the decoder stops at the end of the data. The block being filled stays in RAM until it is full, so a reset, a power cut or an OBDH restart loses it: at most one block, about 8 minutes of flight-like beacons at 30 s.

OBDH stores at most one beacon per 30 s on average (FLS-RATE-020), with a credit: a stored beacon costs 30 s, the credit grows with uptime up to 60 s, and a beacon that finds less than 30 s is skipped and counted. The credit is 0 at boot and counts from the tick counter's start, not from OBDH's (O21). Every beacon of a 30 s timer is therefore stored, since the timer never fires early, and no boot loop stores more than one beacon per 30 s. An OBDH restart keeps the credit.

The beacon task posts each beacon body with `ht_beacon_post()`, which never waits (§5.2.1). The downlink reads with `ht_read_request(from_seq, …)`: it returns the oldest stored block numbered `from_seq` or later, with its length and number; the block being filled cannot be read. Slots in the format before step 4b (V3 of `comms/ht_handling.c`: a 12-byte header, 116 bytes of data, no CRC) count as blank, so HT history starts empty after the update. The ground decoder changes with the format.

**HK history downlink.** Implemented in step 5a (`src/subsystems/comms/hk_history.c`, `include/subsystems/comms/hk_history.h`, host tests `test_hk_history`). `TC_REQUEST_HK_HISTORY` carries FROM_SEQ, the sequence number of the first block wanted (4 bytes, big-endian), in parameter bytes 2–5, where it carried SINCE_UNIX before; a shorter telecommand is refused with `NACK_BAD_PARAMS`. When the telecommand arrives, the COMMS task copies the stored blocks numbered FROM_SEQ or later, oldest first, into a 2,048-byte buffer in RAM, with one block time (`OBDH_REQUEST_WAIT`) for all its reads. ARQ then sends the copy, resends from it and computes its CRC over it, so the session reads no flash; blocks stored in the meantime wait for the next request. The copy is:

| Bytes | Content |
|---|---|
| 0 | HT format (`HT_FORMAT`, 1) |
| Then, per block | SEQ, the block's sequence number (4 bytes, big-endian); LEN, its length (1 byte); the block (LEN bytes, as above) |

The copy takes blocks while one more of the largest size still fits, so it holds about 17 flight-like blocks, 2.2 hours of beacons at 30 s; a read that does not complete in time ends it early. The ground asks for the rest with FROM_SEQ = the last SEQ received + 1; FROM_SEQ 0 asks for the oldest block stored. A jump in the numbers means blocks that can no longer be read: overwritten by the ring, or skipped because their CRC failed. With flight-like beacons a day of history is about 187 blocks, about 21 KB with the record headers (O4).

A request that finds nothing stored is answered `NACK_NO_DATA` (`0x0C`, added in step 5a). A request that arrives during another downlink session, or whose first read does not complete in time, is answered `NACK_BUSY`, and the ground asks again later; before step 5a a request during a session was dropped without an answer. These are changes to the TT&C protocol: the protocol document (`TTC.md`) and the ground software, including its copy of `types.h` (`gs-server/domain/air/types/types.go`), must follow, with the agreement of the COMMS owners.

### 5.5 Parameter store: own module, not ST's EEPROM emulation

ST publishes a store of this kind (X-CUBE-EEPROM, AN4894; current source `github.com/STMicroelectronics/stm32-util-eeprom-emulation`, v2.1.0). It was reviewed by reading the source, not by building or running it. Every value there is one 8-byte flash element with 4 bytes of data, a 16-bit variable number and a 16-bit CRC.

| Point | ST library | Own store |
|---|---|---|
| Power-cut recovery logic | Written and maintained by ST | Ours to write and test |
| Items over 4 bytes (TLE, calibration, COMMS config) | Split into several elements; a torn update is detectable with an added CRC, not recoverable | One record; the old value is kept |
| Second copy of critical values (SPF §2.2) | No: single instance | Yes |
| Erase at start-up | None in normal reboots with `EE_CONDITIONAL_ERASE`; still completes an interrupted erase during init. `EE_FORCED_ERASE` re-erases spare pages on every boot and must not be used. | None |
| STM32L476 support | This version lists only the STM32C5. The older package on st.com has an L4 port; its source was not read. | Native |
| Wear counters, erase governor | To be added in the flash interface | Built in |

**Decision (team, 2026-10-08): write `param_store`.** The deciding points are whole-item updates and the second copy. ST's library remains a workable fallback if those two limits are accepted.

Ideas taken from ST's design: state markers at the start of each erase unit (here the half header) and erasing only in a separate step after the new data is committed. ST's cancelling of corrupted elements with zeros was not needed (§5.4).

FlashDB and littlefs were not evaluated.

---

## 6. Allocation by subsystem

The row-by-row mapping from the team's "OBDH VARIABLES - MEMORY ALLOCATION" sheet, including the address overlaps found in it, is in `docs/flash-variable-allocation.csv`.

### Parameter IDs (PARAM region)

Each subsystem has a range of 32 IDs and picks its own IDs inside it:

| Range | Owner |
|---|---|
| `0x01–0x1F` | OBC |
| `0x20–0x3F` | EPS |
| `0x40–0x5F` | COMMS |
| `0x60–0x7F` | ADCS |
| `0x80–0x9F` | Payload |
| `0xA0–0xBF` | FSS (reserved; FSS is undefined) |
| `0xC0–0xDF` | OBDH |
| `0xE0–0xFE` | Reserved |
| `0x00`, `0xFF` | Never valid (`0xFF` is erased flash) |

| ID | Name | Owner | Size | Default | Written when | Old sheet row |
|---|---|---|---|---|---|---|
| `0x01` | None. `PARAM_OBC_MODE`, the mode to boot in, was removed on 2026-11-26: every boot starts in Init (SPF §1.6.1). Left unused: boards flashed before still hold records of it, which the load skips | OBC | — | — | Never | `CURRENT_STATE_ADDR` (`PREVIOUS_STATE_ADDR` was dropped on 2026-11-25: nothing read it) |
| `0x02` | `PARAM_DEPLOY_COMMS` | OBC | 1 | not deployed | The ground's confirmation of the deployment (SPF §1.6.1); no telecommand sets it yet (O22) | `DEPLOYMENT_STATE_ADDR` |
| `0x03` | `PARAM_DEPLOY_PAYLOAD` | OBC | 1 | not deployed | Flag changes | `DEPLOYMENTRF_STATE_ADDR` |
| `0x04` | `PARAM_POL_MASK` | OBC | 1 | all enabled | Telecommand | `POLMASK_ADDR` |
| `0x05` | `PARAM_STANDBY_DONE` | OBC | 1 | not done | After the 45-minute wait (SPF §1.6.1) | not in the sheet |
| `0x20` | `PARAM_EPS_THRESHOLDS` | EPS | 3 (TBC) | built-in | Telecommand | the four `*_TH_ADDR` rows |
| `0x21` | `PARAM_HEATER_AUTO` | EPS | 1 | built-in | `TC_EPS_HEATER_ENABLE` / `_DISABLE` | not in the sheet |
| `0x40` | `PARAM_COMMS_CONFIG` | COMMS | 21 (TBC) | built-in LoRa settings | Telecommand, after the new settings are confirmed by a received TC | `COMMS_CONFIG_ADDR` |
| `0x41` | `PARAM_TX_INHIBIT` | COMMS | 1 | off | `TC_COMMS_STOP_TX` / `_RESUME_TX` | not in the sheet |
| `0x42` | `PARAM_BEACON_PERIOD` | COMMS | 2 | 30 s | `TC_SET_BEACON_PERIOD` | not in the sheet |
| `0x60` | `PARAM_ADCS_CALIBRATION` | ADCS | 81 (TBC) | identity / zero | Upload complete, CRC checked | `ADCS_CONFIG_ADDR` |
| `0x61` | `PARAM_TLE` | ADCS (uploaded through COMMS) | 138 (TBC) | none | Upload complete, CRC checked | `TLE_ADDR` |
| `0x80` | `PARAM_RFI_CONFIG` | Payload | 8 | built-in | Telecommand | `RFI_CONFIG_ADDR` |
| `0x81` | `PARAM_PL_SCHEDULE` | Payload | 8 | none | Telecommand | `PL_TIME_ADDR` |

The code holds this table as `PARAM_TABLE` in `include/subsystems/obdh/param_store.h`, with the size and default of each ID; the two must agree. Defaults marked TBD there are still to be set with each owner (O14); until then they are zero.

Reserved for later: `0x06` periodic reboot period, `0x43` COMMS parameters (`TC_COMMS_PARAMS`). Neither is defined yet. The IDs were renumbered into the ranges on 2026-10-27 (they were 1–16); nothing has flown, so no old number is in use.

The sizes marked TBC are disputed between sources: the TLE is 102 bytes in the sheet, 103 by its address range, 138 in `flash.h` and 141 in the old ADCS code; the calibration is 81 bytes in the sheet and 87 by its range; the COMMS configuration is 7 bytes in the sheet and a 21-byte structure in the current telecommand; the EPS thresholds are four bytes in the sheet and three in the code (`eps.c`, `tc_handler.c`). The values shown are the working assumption; for the EPS thresholds it follows the code since step 3 (nominal, contingency, sunsafe). Since step 3 an upload or telecommand whose value does not have the size in `PARAM_TABLE` is refused, so a size change must reach the ground software and `PARAM_TABLE` together.

### OBC

| Where | Items |
|---|---|
| PARAM | IDs `0x02`–`0x05` |
| OBC event log region | Events to be returned by `TC_REQUEST_OBC_LOG`; format and rate to be defined (O15). Until the log exists, the telecommand downlinks an 8 KB test pattern (byte *i* = *i* mod 251), kept for the COMMS throughput tests (step 5a) |
| SRAM2 | Reset record (§5.3) |
| RAM | Uptime (from the kernel tick counter and its overflow count, `uptime.h`; the boot time is now minus the uptime), the current mode (`state_machine_current_state()`) and Init's step, reset cause read from the chip at boot, "time not valid" flag, OBC and peripheral error flags, time of last telecommand |
| RTC | Current time. `RTC_TIME_ADDR` and `SET_RTC_TIME_ADDR` are dropped: the set-time telecommand writes the RTC directly. |

### EPS

| Where | Items |
|---|---|
| PARAM | IDs `0x20`, `0x21`; further parameters requested from the EPS team (O14) |
| RAM | Battery voltage, current, capacity, battery temperature, EPS error flags |

EPS keeps no logs or bulk data in flash; readings needed as history go through HT.

### Payload

| Where | Items |
|---|---|
| PARAM | IDs `0x80`, `0x81` |
| PL region | Measurements, one per slot, written once when a measurement completes |

Until the payload store exists, `TC_REQUEST_PAYLOAD_DATA` is answered `NACK_NO_DATA` (step 5a).

### COMMS

| Where | Items |
|---|---|
| PARAM | IDs `0x40`–`0x42`; writes `0x60` and `0x61` when their upload completes |
| HT region | Compressed beacon blocks. Since step 4b the beacon task posts each beacon to OBDH, which compresses and stores it: at most one stored sample per 30 s on average, whatever the beacon rate (FLS-RATE-020) |
| RAM | Current telemetry packet fields, frame sequence numbers, dedup ring, upload buffer |

The beacon period (`0x42`) is set by `TC_SET_BEACON_PERIOD` within the limits of FLS-RATE-010 and applied once stored; the beacon task starts with the stored period (step 5b, `beacon_period.h`).

Uploads (TLE, calibration) are assembled in RAM and written once after the transfer CRC passes. Default LoRa settings stay in the program image as the final fallback.

### ADCS

| Where | Items |
|---|---|
| PARAM | IDs `0x60`, `0x61`; further parameters requested from the ADCS team (O14) |
| RAM | Gyro, magnetometer, sun vector, magnetorquer level, panel temperatures, ADCS mode |

ADCS keeps no logs or bulk data in flash; readings needed as history go through HT. The ADCS mode starts in IDLE after every restart (SPF §3.3.3) and follows the stored OBC mode from there.

### FSS

Undefined. No region; parameter IDs `0xA0`–`0xBF` are reserved. Any future storage comes out of Free and must be added to this section first.

### Detecting a power outage

The RTC loses its time in a power outage and restarts from 1 January 2000. `periph_rtc_init` already detects this through the marker in backup register 0. The software sets a "time not valid" flag in RAM at that point, reports it in the beacon, and clears it when the set-time telecommand arrives. Nothing is stored in flash for this.

### Clear commands

`TC_CLEAR_HT` and `TC_CLEAR_PL_DATA` erase their region and reset the queue. `TC_CLEAR_FLASH` does both and writes the default value for every parameter ID. Each is a request to OBDH, which applies the once-a-day limit (FLS-RATE-080). No extra IDs are needed.

---

## 7. Requirements

### General

| ID | Requirement |
|---|---|
| FLS-GEN-010 | No page of the internal flash shall accumulate more than 1,000 erases in 5 years of expected use. |
| FLS-GEN-020 | No page shall accumulate more than 3,652 erases in 5 years under any software fault, telecommand sequence or reset pattern. |
| FLS-GEN-030 | The flash shall be accessed only by the OBDH task, through the flash driver. The one exception is the read-only scan of the parameter store in `main()` before the scheduler starts. |
| FLS-GEN-040 | The address map shall be defined in one header (`include/subsystems/obdh/flash_map.h`), with compile-time checks that regions are page-aligned, do not overlap and lie in the data area of bank 2. |
| FLS-GEN-050 | No module other than the driver and the stores shall use an absolute flash address. |
| FLS-GEN-060 | Flight software shall not erase a page in order to modify data in it (no copy–erase–rewrite). |
| FLS-GEN-070 | Every new use of flash shall be added to §6 with its region and maximum rate before it is merged. |
| FLS-GEN-080 | The program image shall not exceed 256 KB (`FLASH_CODE_SIZE`); the linker script shall enforce it. |
| FLS-GEN-090 | Flashing the firmware shall not write the reserved areas or the data area, and flight units shall not be mass-erased. |
| FLS-GEN-100 | The dual-bank boot option (`BFB2`) shall stay disabled. |
| FLS-GEN-110 | Every store shall run in the OBDH task. Other tasks shall use only the store functions of §5.2.1: requests, posts and plain functions. |

### Boot and reset

| ID | Requirement |
|---|---|
| FLS-BOOT-010 | The boot path shall not program or erase flash. |
| FLS-BOOT-020 | No erase shall be issued during the first 5 minutes (TBC) of uptime. |
| FLS-BOOT-030 | An unbroken series of 10,000 resets at any interval shorter than 5 minutes shall cause zero erases. |
| FLS-BOOT-040 | Values that change on every boot (boot time, reset cause) shall be kept in RAM, not in flash. |
| FLS-BOOT-050 | The software shall boot and reach beacon transmission with every data region erased, corrupted or unreadable, using defaults from the program image. |

### Driver

| ID | Requirement |
|---|---|
| FLS-DRV-010 | The driver shall reject any program or erase outside the data regions of §5.3. |
| FLS-DRV-020 | The driver shall limit erases per region to an average of 2 per page per day of uptime (token bucket; capacity `max(2, pages/4)`). |
| FLS-DRV-030 | Erase credit shall not be restored by a reset: the bucket starts empty after a cold start and keeps its level across warm resets (or starts empty at every boot). |
| FLS-DRV-040 | A refused erase shall return a distinct status, increment a counter and never block the caller. |
| FLS-DRV-050 | The driver shall read back every programmed range and report a mismatch. |
| FLS-DRV-060 | The driver shall count, per region since boot: erases, program failures, erase failures, verify failures and governor refusals. |
| FLS-DRV-070 | A write that would not change the stored value shall not be issued. |
| FLS-DRV-080 | An erase should not be started while the EPS reports power-fail or the battery is below the Survival threshold. |
| FLS-DRV-090 | Every wait inside the driver shall be bounded by the datasheet's maximum program or erase time with a margin. The driver shall not use the HAL's program and erase functions (`HAL_FLASH_Program()`, `HAL_FLASHEx_Erase()`), whose 50 s limit (`FLASH_TIMEOUT_VALUE`) and lock outlive a deleted task (F15), nor their wait, `FLASH_WaitForLastOperation()`, which never sleeps (FLS-DRV-100). It may call the HAL functions that do one step on the flash registers without waiting or taking the HAL's lock: `HAL_FLASH_Unlock()`, `FLASH_PageErase()` and `HAL_FLASH_Lock()`. It shall follow the sequences of RM0351 (§3.3.6 for an erase, §3.3.7 for a program) and do around each operation what `HAL_FLASH_Program()` and `HAL_FLASHEx_Erase()` do: clear the error flags an earlier operation left, keep the data cache off while the operation runs, then reset the data cache after a program and both caches after an erase, and lock the flash control register again. It shall write the two unlock keys with interrupts masked, because a wrong key sequence locks the flash control register until the next reset (RM0351 §3.3.5). |
| FLS-DRV-100 | While an erase runs, the driver shall sleep between status checks, so that lower-priority tasks keep running (F19). |
| FLS-DRV-110 | The driver shall not write the flash control register while the flash is busy: the write stalls the bus until the operation ends (RM0351 §3.3.5), which never happens if the flash stays busy. An operation that does not end within its limit shall return `HAL_TIMEOUT` and leave the control register and the data cache as they are; the next operation shall wait for the flash, at most for the erase limit, then clear the operation bits that were left before it starts. |
| FLS-DRV-120 | The driver shall check that the whole target of a program is erased before it programs any of it, and refuse the program otherwise. The chip checks each doubleword only as it programs it, and accepts all-zero data over anything (PROGERR, RM0351 §3.3.7), so a refused program would otherwise leave part of it written, and zeros could overwrite stored data. |

### Requests

| ID | Requirement |
|---|---|
| FLS-REQ-010 | A store function whose caller needs OBDH's answer shall be a request (`_request`) taking a block time; one whose caller does not shall be a post (`_post`) that never waits. A function with neither suffix shall never involve the OBDH task. |
| FLS-REQ-020 | A request's block time shall be one deadline for queueing and answer. When it runs out, the request shall return `HAL_BUSY` if it could not be queued (nothing sent), or `HAL_TIMEOUT` if it was sent (result unknown). |
| FLS-REQ-030 | Requests and posts shall carry their data by copy: no queue entry shall point into a caller's memory. Payload measurements are the one exception, passed by reference with explicit buffer ownership. |
| FLS-REQ-040 | Every answer shall carry the number of its request, and a caller shall ignore an answer to an earlier request without extending its deadline. Request numbers shall survive a restart of the client task. |
| FLS-REQ-050 | A post shall leave free the queue entries kept for requests, one per client; a post that finds no other entry free shall be dropped and counted. |
| FLS-REQ-060 | Each task's longest stretch between two health reports, waits included, shall be shorter than the health period. The standard block time and the health period shall be named constants, and the build shall check that the block time is the shorter. |
| FLS-REQ-070 | When the OBDH task starts, it shall put the flash interface in a known state and rebuild every store's state from flash before it serves a request. |
| FLS-REQ-080 | The OBC shall not send a request while it holds the OBDH task paused. |
| FLS-REQ-090 | OBDH shall answer through reply queues created at boot and never deleted, found by the client's index in the task table. It shall never use a task handle, and never wait to deliver an answer. |
| FLS-REQ-100 | The OBC shall switch the clock only after the OBDH task has acknowledged the pause; otherwise it shall keep the current mode, count the failure and try again. The acknowledgement of a pause shall be distinct from that of a resume. |
| FLS-REQ-110 | A store function that runs in the OBDH task shall check its caller before anything else. It shall return `HAL_ERROR`, having read, programmed and changed nothing, to any caller other than the OBDH task, or `main()` before the scheduler starts. |

### Data integrity

| ID | Requirement |
|---|---|
| FLS-INT-010 | Every record shall carry a CRC, programmed last. |
| FLS-INT-020 | Every PARAM record shall exist as two copies on different pages; copy B is written after copy A is verified. |
| FLS-INT-030 | A power cut at any instant shall leave each parameter at either its old or its new value. |
| FLS-INT-040 | The NMI handler shall handle flash ECC double errors: if the failing address is inside the program image, reset; anywhere else, count the event, record the address, clear the flag and return without writing flash (the reader's CRC rejects the data). While the ECC correction flag is set a double error raises no NMI (RM0351 §3.3.2), so the correction interrupt shall be enabled at boot, before the driver's first read, and its handler shall clear the flag at once, count the event and record its address. The driver shall still clear the flag before each read, as a backup. A read shall report a double error in the bytes read whether or not the NMI has run by the end of the copy: nothing specifies how soon the NMI arrives after the read that finds the error (Arm AN321, *Cortex-M Programming Guide to Memory Barrier Instructions*, §3.6), so after the copy the driver reads ECCD first and the NMI's count second, and leaves ECCD to the NMI. |
| FLS-INT-050 | A page whose erase or verify fails shall be skipped; the store shall continue on the next page. |
| FLS-INT-060 | The software shall remain operable (beacon and telecommand handling) if every program and erase operation fails. |
| FLS-INT-070 | The program image should be checked against a CRC at boot and once per day, and each ECC correction event the hardware flags (ECCC) should be counted, keeping the address of the last one; the results go to telemetry. |
| FLS-INT-080 | The program pages should be write-protected (WRP option bytes) on flight units. |
| FLS-INT-090 | A store shall ignore unrecognized records and data that fails its integrity checks. Before running firmware that moves a store or introduces an incompatible on-flash layout, erase the affected regions. For an incompatible change to the parameter half or record header, erase both PARAM A and PARAM B. |

### Rate limits on inputs

| ID | Requirement |
|---|---|
| FLS-RATE-010 | A beacon period set by telecommand outside 10 s – 3,600 s (TBC) shall be refused (`NACK_PARAM_OUT_OF_RANGE`) and change nothing. A new period shall take effect only once stored, and the beacon shall start with the stored period (the default, 30 s, if the stored one is outside the limits). |
| FLS-RATE-020 | Historic telemetry shall store at most one sample per 30 s (TBC) on average, independent of beacon period and of beacon-on-request: in any T seconds, resets included, at most 2 + T/30 samples. |
| FLS-RATE-030 | Withdrawn on 2026-11-26: the mode is not stored, since every boot starts in Init (SPF §1.6.1). |
| FLS-RATE-040 | A transition request to the current mode shall cause no write. |
| FLS-RATE-050 | The OBC event log shall stay within the wear limit of its region (§5.3, sizing a ring); its event rate limit is set when the log is designed (O15). |
| FLS-RATE-060 | Payload results shall be written once per measurement, after it completes. |
| FLS-RATE-070 | Uploaded files shall be written once, after the whole transfer has passed its CRC. |
| FLS-RATE-080 | A clear command shall erase its region at most once per 24 hours (TBC); a repeat within that time is refused. |

### Observability

| ID | Requirement |
|---|---|
| FLS-OBS-010 | The software shall report on request, per region: lifetime erase count of the most-worn page, erases since boot, failures and governor refusals. |
| FLS-OBS-020 | A flash error, a governor refusal and an ECC event shall each set a bit in the beacon health flags. |
| FLS-OBS-030 | Ground shall raise a warning at 1,000 erases on any page and an alarm at 3,000. |

---

## 8. Wear budget of the proposed design

Per page, over 5 years. Limits: target 1,000, governor 3,652, rated 10,000.

| Region | Case | Erases per page per day | In 5 years | Share of rated |
|---|---|---|---|---|
| PARAM | Expected: 1 TLE upload and 8 small writes per day | 0.02 | 42 | 0.4 % |
| PARAM | Heavy ground use: 10 TLE uploads and 20 small writes per day | 0.14 | 249 | 2.5 % |
| PL | Data budget: 1 measurement per pass | 0.10 | 186 | 1.9 % |
| PL | Downlink-limited: about 8 K-band measurements per day | 0.49 | 897 | 9.0 % |
| HT | Static fields, as the bench firmware sends today: 3.6 bytes per beacon, 26 per block | 0.22 | 395 | 4.0 % |
| HT | Flight-like sensors: 6.4 bytes per beacon, 15.4 per block | 0.37 | 682 | 6.8 % |
| HT | Noisy sensors: 10.2 bytes per beacon, 9.5 per block | 0.59 | 1,071 | 10.7 % |
| HT | Incompressible input (bound): 49 bytes per beacon, 2.2 per block | 2.00 (governor) | 3,652 | 36.5 % |
| Any | Fault holding a region at the governor limit for 5 years | 2.00 | 3,652 | 36.5 % |
| Any | Reboot loop faster than 5 minutes | 0 | 0 | 0 % |

Each 8 KB PARAM half has 7,640 bytes for new records after the 8-byte header and the 544-byte live set (the 13 IDs at the sizes of §6). Each page is erased once per two compactions. The other estimates use 4,096-byte payload slots and 30 s HT sampling. The governor allows 32 K-band measurements per day.

History kept by the 64 KB HT region: 108 h with static fields, 62 h with flight-like sensors, 40 h with noisy sensors. All three cover the adverse case of one pass per day.

The HT figures were measured on the host by running the flight `compression.c` and `ht_handling.c` on 30 days of synthetic beacons at a 30 s period, with the 116 bytes of data per slot of the format before step 4b. The beacon model is an estimate: orbit-periodic temperatures and battery current with noise, a daily reboot, occasional mode and telecommand changes. Captured FlatSat beacons should replace it when available (§11). The ring's slots of step 4b hold 112 bytes of data, so a block holds fewer beacons. Run through `compression.c` with both sizes on 2026-11-21, the same synthetic beacons need 4 % more blocks at the flight-like point, 5 % more at the static and noisy points, and 16 % more for pure noise; the HT rows and the history above are the measurements scaled by these factors.

POCKET+ expands incompressible input: a 20-byte beacon of pure noise becomes about 49 bytes, so a block then holds 2.2 beacons instead of the 5 that raw storage would give. The block format has no raw fallback. Real beacons cannot be pure noise (version, time and uptime are deterministic), and the governor bounds the wear in any case, but HT then drops history.

The rows "heavy ground use" and "incompressible input" are stress cases that need a fault or deliberate misuse. Among the cases that can occur in normal operation, the highest is HT with noisy sensors, at 1,071 erases: 7 % above the 1,000 target, against 2 % before the ring's header and trailer took 16 bytes of each 128-byte slot in step 4b. The sampling period (O4) and FlatSat beacons (O3) will settle it.

---

## 9. Verification

| ID | Test | Pass criterion |
|---|---|---|
| V1 | Host soak: 5 simulated years of expected use on the fake flash driver (`test/mocks/fake_flash_driver`) with an erase counter per page | Max erases per page ≤ 1,000 |
| V2 | Host worst case: every rate-limited input driven at its maximum for 5 simulated years | Max ≤ 3,652 |
| V3 | Boot loop on hardware: 10,000 resets (watchdog and power) at 1 s, 30 s and 4 min intervals | Erase counter unchanged |
| V4 | Power cut on hardware: ≥ 1,000 cuts at random instants during parameter writes, log writes and erases. On the host, `test_param_store` already cuts at every program and erase step of a write and of a compaction, and `test_ring` at every step of an append, one that erases a page included | Always boots; every parameter is old or new; no NMI lock |
| V5 | Telecommand flood: each flash-affecting TC repeated at the maximum uplink rate for 24 h | Erases ≤ governor limit; refusals reported |
| V6 | Read-only flash: fault injection makes every program and erase fail | Beacons continue, TCs are answered, no task blocks |
| V7 | Blank and corrupted flash: boot with regions erased, random and half-written | Boots with defaults |
| V8 | ECC injection (hardware, by interrupted programming) in a data region | NMI handler recovers; event counted; the read that hit the error returns `HAL_ERROR`. A single error is counted by the correction interrupt, with its address |
| V9 | Static check in CI: no call to `HAL_FLASH*` outside `flash.c`; no `0x080…` literal outside the map header. Run by hand on `src/` and `include/` after step 5a (2026-11-21): clean. The CI check is not built yet | Clean |
| V10 | OBDH restarted in the middle of a program, an erase and a compaction (the health monitor's path, forced on hardware) | The interrupted request's caller gets `HAL_TIMEOUT`; later writes succeed; every value is old or new, and the same before and after a reboot |
| V11 | Request protocol on the host, through the queue (`test_obdh_requests`): late answer, requester restarted while waiting, full queue, answers to several clients. Implemented on 2026-11-12, with one deadline per request, data by copy and OBDH paused or restarted; the restarted task's index is checked in `test_task_management`. On 2026-11-25: a read sends length 0, and a block longer than the caller's buffer | A late answer is ignored and the next request gets its own; a restarted task continues its numbering; a full queue returns `HAL_BUSY` within the block time and sends nothing; a block longer than the caller's buffer returns `HAL_ERROR` with nothing written |
| V12 | Missing pause acknowledgement on the host (`test_state_machine`); pause and resume reaching a task together (`test_task_management`). Implemented on 2026-11-12, with a late resume acknowledgement that must not count as a pause acknowledgement | No clock switch, the mode is kept and the retry succeeds; the task ends running and acknowledges |
| V13 | Flash waits with the busy flag stuck (host harness on the driver). The driver's half implemented on 2026-11-25 (`test_flash_driver`, on a fake flash controller that fails the test on a write to FLASH_CR while the flash is busy): a program and an erase that stay busy, each followed by an operation once the flash is free, and the flash interface reset with the flash stuck busy. OBDH's half is not built yet | Program and erase return `HAL_TIMEOUT` within their limits without writing FLASH_CR, and the next operation succeeds (FLS-DRV-110); OBDH keeps answering |
| V14 | Flood of posts on the host (`test_obdh_requests`; the count in `test_ht_store`). Implemented on 2026-11-21 (step 4b): OBDH stuck, posts until one is refused, then a request from every client | A post that finds only the clients' entries free is refused at once and counted; every client can still queue a request; OBDH answers no post |
| V15 | The correction handler's whole path, `flash_ecc_irq()` (the FLASH_ECCR read and the write that clears ECCC), timed with the cycle counter (`DWT->CYCCNT`) while a bank-2 page erase runs, and while a doubleword programs; BSY in FLASH_SR is read before and after the call, to confirm the flash was busy throughout (hardware). RM0351 §3.7.7 promises FLASH_ECCR no wait state only while no operation runs (FLS-INT-040) | The call returns in a bounded time with the flash still busy: a few wait states are acceptable, waiting for the operation to end is not. If it waits, the handler must not touch FLASH_ECCR while the flash is busy; one candidate fix is to check BSY first (FLASH_SR never waits) and, while busy, turn the interrupt off until the driver sees BSY clear |
| V16 | Wrong caller on the host (`test_param_store`, `test_ht_store`). Implemented on 2026-11-25, checked by 12 mutations of the checks and of `obdh_is_caller()`, all caught: each store function that runs in OBDH called from another task (COMMS, the beacon task), the HT store's in the middle of a block with a sample's credit available; `main()` before the scheduler starts; OBDH deleted and created again | Another task gets `HAL_ERROR`, with no flash read or programmed and nothing changed: the caller's buffer, the parameter values and sequence numbers, and the HT credit, counters and open block are as before, so the writes and blocks that follow are exactly those without the call. `main()` and the restarted OBDH are served |

---

## 10. Implementation plan

Order agreed on 2026-10-26: the parameter store comes first, because most of the other fixes build on it. Revised on 2026-10-28, when OBDH was confirmed as the storage system (§5.2): step 2b added, and HT moves into OBDH before the old map is deleted, because the telemetry-history downlink must read HT through its store first. The work goes one step at a time, each reviewed before the next.

| Step | Change | Fixes | Status |
|---|---|---|---|
| 1 | Memory map (§5.3): `flash_map.h` with compile-time checks; the driver takes a region and an offset and refuses anything outside it; linker limit of 256 KB for code; HT moved into its new region (the guard would refuse its old address) | F7, F8, F12 | Done on 2026-10-27. Old-map access stayed until step 5a, limited to its three pages (§5.3) |
| 2 | `param_store`: two copies, CRC, two halves per copy, scan at boot; `param_get` / `param_set` by ID; a reset writes the default value as an ordinary record. NMI handler for flash ECC errors (FLS-INT-040), which the scan at boot relies on | F1, F2, F5, F6 | Done on 2026-10-27, with host tests (`test/suites/test_param_store.c`). No subsystem uses the store yet |
| 2b | Parameter semantics (§5.4): `param_get` returns a status and no longer classifies the value; the reset rule stated precisely; the load publishes the values in one step | — | Done on 2026-11-12, with host tests (`test_param_store`) |
| 2c | Request protocol (§5.2.1): one deadline per request, `HAL_BUSY` or `HAL_TIMEOUT`; data by copy in the queue entry; reply queues by task-table index, created once, answered with `xQueueOverwrite`; request numbers that survive a restart; build-time check of the block time against the health period. `param_set` and `param_reset` become `param_set_request` and `param_reset_request`. Posts come with their first user in step 4 | F11, F16 | Done on 2026-11-12, with host tests (`test_obdh_requests`, V11). Notification index 1 removed: OBDH answers only through the reply queues. On 2026-11-25 the store functions that run in OBDH were made to refuse any other caller (§5.2.1 rule 9, FLS-REQ-110), with host tests (V16) checked by 12 mutations, all caught |
| 2d | OBDH recovery: program and erase through the flash registers with limits from the datasheet, sleeping during erases; the flash interface reset and every store reloaded when OBDH starts; the clock switch only after OBDH acknowledges the pause, a resume that arrives with a pause wins; pause and resume acknowledged on separate bits; a paused OBDH sleeps until the resume instead of polling (KI-5 in `test/README.md`) | F15, F17, F19 | Done on 2026-11-12. Host tests for the pause protocol (V12) and OBDH's restart (`test_obdh_requests`). On 2026-11-25 the driver's program and erase were checked step by step against RM0351 and the HAL and simplified (FLS-DRV-090, FLS-DRV-110, FLS-DRV-120), calling `HAL_FLASH_Unlock()`, `FLASH_PageErase()` and `HAL_FLASH_Lock()` instead of copies of them, and the flash interface reset finishing the operation a deleted task left, writing FLASH_CR only if that task left it unlocked; with host tests on a fake flash controller (`test_flash_driver`, V13 on the driver), checked by 75 mutations of `flash.c`, all caught, the read's ECCD check (FLS-INT-040) included; a 76th, which swaps that check's two reads, cannot be caught on the host, where nothing runs between two plain reads. The driver compiles but has not run on hardware yet (V10, O19, O20) |
| 3 | Move the parameters of §6 to the store (the 8 callers of `obdh_write_request` and the readers of the same addresses); set the defaults marked TBD in `PARAM_TABLE` with each owner; boot time to RAM and uptime from the tick counter; ignore a transition to the current mode; the OBC writes the mode outside the OBDH pause (FLS-REQ-080) | F3, F4 | Done on 2026-11-21, with host tests (`test_state_machine`, `test_uptime`), except the TBD defaults, which wait for the owners (O14). The mode is one write of `PARAM_OBC_MODE` after the resume, and telemetry reads the live mode from the state machine. Until 2026-11-25 the parameter also held the previous mode, carried over from the old map; nothing read it, so that byte was dropped. On 2026-11-26 the mode stopped being stored and `PARAM_OBC_MODE` was removed: every boot starts in Init. Uploads and telecommands of the wrong size are refused, and ADCS and EPS are notified only once the value is stored. `flash_write` and `obdh_write_request` deleted. The telecommand and upload paths are checked by compilation only |
| 4a | Ring module (§5.4) for the HT store, the payload store and the OBC event log: fixed slots of up to one page; a page erased as the writer enters it; a page that fails to erase, or a slot that fails to program or to read back, skipped; a CRC trailer programmed last; sequence numbers compared across the wrap; a scan of the headers when OBDH starts; reads by sequence number | F10, F14 for every store built on it | Done on 2026-11-21, with host tests (`test_ring`, checked by 18 mutations of `ring.c`, all caught). The HT store uses it since 4b. On 2026-11-25 the slot header's tag and format were replaced by a check of the length and the sequence number (§5.4), checked by 22 mutations of `ring.c`, all caught. The same day, tests of a header, data or trailer that reads back with an ECC error, or wrong without one, raised that to 30 mutations, all caught |
| 4b | HT store on the ring, inside OBDH: POCKET+ compression; the beacon task posts each beacon, the first post (FLS-REQ-050, V14); at most one stored sample per 30 s on average (FLS-RATE-020), as a credit: each stored sample costs 30 s, and the credit grows with time up to 60 s, so beacon jitter drops nothing (start value: O21); block reads for the downlink (`ht_read_request`); the scan when OBDH starts; `comms/ht_handling.c` deleted, and with it KI-10 and KI-11. New slot format and ground decoder (§5.4) | F9 (wear), F10, F14 | Done on 2026-11-21, with host tests (`test_ht_store`, and V14 in `test_obdh_requests`), checked by 39 mutations of `ht_store.c`, `obdh_requests.c` and `obdh.c`, all caught. O21 closed: the credit starts at 0 at boot. Not in 4b: `TC_CLEAR_HT` (OBDH ignores it until the clear commands are built), the counters in telemetry, smaller POCKET+ buffers (20.5 KB of RAM) and the beacon period limits (step 5b). The ground decoder must follow the new format (§5.4) |
| 5a | Downlinks that read no flash while they run: HK history sends a RAM copy of the stored HT blocks, chosen by FROM_SEQ (§5.4, HT); until their stores exist, the OBC-log downlink sends a test pattern and the payload downlink is answered `NACK_NO_DATA`; `NACK_NO_DATA` added, and a downlink request during a session answered `NACK_BUSY`. Then `flash_read_old`, the old addresses in `flash.h`, `obdh_read_old_request` and the low-level requests (`obdh_read_request`, `obdh_program_request`, `obdh_erase_program_request`) deleted; the driver and the map moved under `subsystems/obdh/` | F2, F8 (last parts) | Done on 2026-11-21, with host tests (`test_hk_history`; `test_obdh_requests` reworked onto the parameter write and the HT read), checked by 36 mutations of `hk_history.c`, `obdh_requests.c` and `obdh.c`, all caught. ARQ and the telecommand handler are checked by compilation only. The TT&C changes (FROM_SEQ, the copy format, `NACK_NO_DATA`, `NACK_BUSY` during a session) wait for the ground software and the COMMS owners (§5.4, HT) |
| 5b | Beacon period (`comms/beacon_period.c`): a `TC_SET_BEACON_PERIOD` outside 10–3,600 s (TBC) refused with `NACK_PARAM_OUT_OF_RANGE` instead of clamped (FLS-RATE-010 reworded); the period stored as `PARAM_BEACON_PERIOD` and applied only once stored, a period not stored within the block time answered `NACK_BUSY`; the beacon task starts with the stored value. Until 5b any period was applied, period 0 included, and none was stored | F9 | Done on 2026-11-24, with host tests (`test_beacon_period`), checked by 31 mutations of `beacon_period.c` and `beacon_period.h`, all caught. One beacon timer for the life of the software: until 5b each start of the beacon task created another, so every restart by the health monitor added a timer and more beacons. A period stored while the beacon task is paused (SURVIVAL) applies at the resume; beacons that fell due during the pause are still not sent late. A frame too short to carry a period is answered `NACK_BAD_PARAMS` (it was ignored). The telecommand handler and the beacon task's loop are checked by compilation only. The ground software must expect the three refusals |

After these steps (order to be agreed): driver protection inside OBDH (an erase governor that counts per page, for example at most one erase of a page every 12 h, so that one faulty page cannot spend a whole region's credit (F18); lifetime erase estimates derived from the sequence numbers already stored; quiet period after boot, counters, read-back verify; FLS-DRV). Until the governor exists, FLS-GEN-020 is not met. Then the payload store with fixed slots on the ring module (§5.4), the OBC event log, the SRAM2 reset record (an OBDH module, updated by `main()` at boot), clear commands as requests that OBDH rate-limits, "time not valid" and the reset cause in the beacon, flash health telemetry and beacon health bits, tests V1–V14 and the CI check, WRP and the image CRC check in the flight procedure.

Steps 1–3 should be complete before the EPS automatic mode transitions are merged.

Not in the baseline: RTC backup registers (§5.1), a separate region for critical flags (O12), the backup image itself (space reserved only, §11).

---

## 11. Open points

| # | Question | Status | Affects |
|---|---|---|---|
| O1 | Endurance and retention figures | **Closed.** 10,000 cycles from −40 °C to +105 °C (DS10198 Table 51). Re-check once against the current datasheet revision. | §4 limits |
| O2 | Do backup registers survive the EPS power cut? | **Closed.** No: VBAT is on the 3.3 V rail. They survive resets only. Not used in the baseline. | §5.1 |
| O3 | Real POCKET+ compressed size of flight beacons | **Estimated** by host measurement on synthetic beacons: 3.6–10.2 bytes (§8). Confirm with captured FlatSat beacons. Not blocking. | HT budget and history depth |
| O4 | HT sampling period: every beacon (30 s) or slower? With flight-like sensors a day of history is about 187 blocks: about 21 KB in the HK history downlink (block data and record headers, step 5a), before ARQ and frame overhead, against a daily downlink of 22 KB. | Open — operations | HT downlink, not wear |
| O5 | Largest payload measurement per satellite, and whether the size is fixed in flight | **Default taken:** 4,096-byte slots (K-band), 1,024-byte (L-band), from SPF §1.2 and Table G.4. Payload owner to confirm. | PL slot size |
| O6 | Periodic reboot period and flight watchdog period | Open — OBC design | ID `0x06` |
| O7 | Sizes of the TLE, ADCS calibration, COMMS configuration and EPS thresholds | Open — ADCS, COMMS and EPS owners. Working values 138, 81, 21 and 3 bytes. Since step 3 an upload of another size is refused | §6 |
| O8 | Does a hard reboot clear the deployment flags (SPF §2.1)? If so it re-arms the antenna burn. | Open — operations | IDs `0x02`, `0x03`, `0x05` |
| O9 | FSS storage needs | FSS is undefined: no region; IDs `0xA0`–`0xBF` reserved | Free |
| O10 | Quiet period (5 min), beacon period limits (10 s to 3,600 s), clear-command limit (1 per 24 h) | Defaults proposed; team may change | §7 values |
| O11 | Should HT refuse to append a compressed beacon larger than the raw 20 bytes (start a new block instead)? | **Closed** on 2026-11-21: no. A beacon that POCKET+ expands is appended like any other: starting a new block for it would cost more wear and history than the expansion | HT worst case |
| O12 | Keep the critical flags (IDs `0x02`–`0x05`) in a separate region from the TLE traffic? | Baseline: one region. Must be decided before launch, since IDs cannot move afterwards. | §5.3 |
| O13 | Definition of COMMS parameters (`TC_COMMS_PARAMS`) | Open — COMMS | ID `0x43` |
| O14 | EPS and ADCS parameters: name, size, default, who changes each one and how often, especially values their software saves by itself | Request to both teams prepared on 2026-10-26 | §6, PARAM live set and write budget |
| O15 | OBC event log: content, record size, events per day | Open — OBC. 16 KB placeholder | §5.3, FLS-RATE-050 |
| O16 | Keep the two 256 KB reservations for a backup image and an image upload? | Kept until the team decides on in-orbit updates (TODO below) | §5.3 top level |
| O17 | Standard block time for requests and the health period | Open. Working values 1 s and 5 s; the health period is not final. The build checks only their order (FLS-REQ-060); the real rule is each task's longest stretch between two health reports | §5.2.1 |
| O18 | HT slot format in step 4: write the header last only, or also add a CRC (which changes the slot format and the ground decoder)? | **Closed** on 2026-11-11: a CRC trailer, programmed last, as FLS-INT-010 already requires. The ground decoder changes with the slot format | §5.4, F14 |
| O19 | Are flash program and erase possible in low-power run mode? Init and SURVIVAL run at 2 MHz with the regulator in low-power mode. Since 2026-11-26 every boot starts in Init, which writes flash: `PARAM_STANDBY_DONE` at the end of the standby, HT blocks once the beacon runs, and the parameters that telecommands change | Open. RM0351 Rev 10 (checked 2026-11-21) states no restriction and refers to the datasheet; DS10198 still to be checked, then a test on hardware. If not possible, the driver must leave low-power run around each operation, or nothing may write flash in Init and SURVIVAL | §6 OBC, Init |
| O20 | Program and erase limits for the driver (FLS-DRV-090) | Working values from DS10198: doubleword program 90.76 µs max, page erase 24.47 ms max; the driver allows 2 ms and 100 ms. The tick counts whole milliseconds, so a 1 ms limit could expire a few microseconds after the start; 2 ms always leaves at least 1 ms. Confirm against the current datasheet revision, like O1 | §7 Driver |
| O21 | HT credit after a reset (step 4b): start at 30 s or at 0? The beacon timer first fires one period after it starts (`beacon.c`; `beacon_period.c` since step 5b). At 30 s, the first beacon after a reset is always stored, but a boot loop with a beacon period under 30 s (10 s at the lower limit of FLS-RATE-010) stores one sample per boot: up to three times the HT rate, so flight-like HT wear (§8) goes from 6.8 % to about 20 % if such a loop lasted 5 years. At 0, a boot loop never stores more than one sample per 30 s, and the first beacon after a reset is lost only if OBDH started after the beacon task | **Closed** on 2026-11-21: 0, counted from boot (the tick counter's start), not from OBDH's start, so OBDH's start time does not matter. FLS-RATE-020 then holds across resets. The first beacon of a 30 s timer is still stored, since the timer never fires early, and an OBDH restart keeps the credit (`test_ht_store`) | FLS-RATE-020, HT wear |
| O22 | Init's antenna deployment (2026-11-26): which telecommand confirms the deployment and sets `PARAM_DEPLOY_COMMS` (none in the catalogue yet), and what does a reset before then do? Until the flag is set, every boot runs the deployment step again, and with it the burn once that is written. The reboot after 72 h without a telecommand (SPF §1.6.3) waits for the same decision | Open — OBC and the deployment owner | ID `0x02`, Init |
| O23 | How are the activation flags cleared on the flight unit before integration? The SPF starts the activation sequence with both flags clear (§1.6.1). Since 2026-11-26 any run of Init past 45 minutes stores `PARAM_STANDBY_DONE`, and `flash.sh` (`st-flash write`) leaves the PARAM region as it is. A unit run on the bench would therefore skip the standby after deployment and transmit within the 45 minutes the deployer forbids (SPF §1.5). `TC_CLEAR_FLASH` is not implemented yet: OBDH ignores its notification | Open — operations. Until decided, erase the whole flash before loading the flight image | IDs `0x02`, `0x03`, `0x05`, Init |

None of the open points blocks steps 1–5b. O19 must be settled before flight, since every boot runs Init at 2 MHz and Init writes flash. O7 and O14 must be settled before the ground software sends uploads and threshold telecommands to the store.

### TODO: dual boot / backup program image

Status: **not decided, not implemented.** The SPF (§3.6.4) plans no failsafe image; the ESA software recommendations ask for a way to recover from corrupted software, for example a bootloader with a backup image. The team has to decide and record the justification before the memory map is frozen.

Interim steps that keep the option open:

- [x] Reserve the space: `0x08040000–0x0807FFFF` for a backup image and `0x08080000–0x080BFFFF` for staging an upload, both kept erased (§5.3). The image is about 85 KB today.
- [ ] Check the program image CRC at boot and once per day, and report the result in telemetry (FLS-INT-070).
- [ ] Write-protect bank 1 with the WRP option bytes on flight units (FLS-INT-080).

Points to settle if dual boot is adopted:

- [ ] The chip's built-in dual-bank boot (BFB2) is understood to check only the start of the image, not a CRC. To be confirmed in AN2606; a small custom bootloader is the likely alternative.
- [x] Booting from bank 2 swaps the bank addresses, which would move the data regions of §5.3. Settled: `BFB2` is never used (§5.3 rule 8, FLS-GEN-100); a backup image needs a bootloader that does not swap banks.
- [ ] A bootloader is a new single point of failure: it must be minimal, write-protected and never updated.
- [ ] A backup image protects against a corrupted image, not against a software bug present in both copies.

### SPF text to update when this is adopted

- §3.6.4 "Data overwriting": the copy–erase–rewrite method is replaced by append-only stores.
- §3.6.4 Figure 3.98: new memory map (§5.3).
- §3.1.5: states that EDAC and CRC are not implemented, while §7.5 lists redundant storage with ECC and scrubbing. This specification adds CRC and two copies.
- §3.6.6: memory budget can now be stated (§5.3, §8).
- Table 2.2, ST[06]: "critical configurations are saved across multiple pages" is met by the two parameter copies (§5.3).
