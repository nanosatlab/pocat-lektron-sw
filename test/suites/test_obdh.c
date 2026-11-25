/**
 * @file test_obdh.c
 * @brief Host tests for OBDH-mediated flash access: the client request functions and the OBDH task that serves them.
 * @date 2026-10-23
 *
 * @details
 * **What is under test**
 * - src/subsystems/obdh/obdh_requests.c (client side). The functions
 *   `obdh_read_request()`, `obdh_write_request()`, `obdh_program_request()`
 *   and `obdh_erase_program_request()` check their arguments, put a request
 *   on `obdh_queue_handle`, and then block on notification index 1 until OBDH
 *   replies.
 * - src/subsystems/obdh/obdh.c (server side). `process_obdh()`, one loop
 *   iteration of the OBDH task, does the following:
 *   1. handles PAUSE/RESUME;
 *   2. takes one request from the queue, waiting up to 1 s;
 *   3. calls the flash driver;
 *   4. replies to the requesting task on index 1 with the HAL status.
 *
 * Together they implement the rule "all flash access goes through the OBDH
 * task".
 *
 * **One request, end to end, in this single-threaded test**
 * @verbatim
 * test task                          fake RTOS                      OBDH (run by the hook)
 * obdh_write_request() ──xQueueSend──► queue
 *   waits on index 1 (portMAX_DELAY)
 *                                     would block → block hook ───► process_obdh()
 *                                                                     xQueueReceive → flash_write()
 *                                     index 1 = HAL status  ◄──────── xTaskNotifyIndexed(client)
 *   wait completes, returns status
 * @endverbatim
 *
 * **How it is built** (see test/CMakeLists.txt)
 * - `#include`d below:
 *   - `obdh.c` and `obdh_requests.c`, the code under test;
 *   - `task_management.c` and `health.c`, because `process_obdh()` uses the
 *     real `tm_check_pause()`.
 * - SOURCES:
 *   - `notifications.c`;
 *   - `stub_task_entries.c`, whose weak `obdh_task` stub is replaced by the
 *     real one from obdh.c;
 *   - `fake_flash_driver.c`, a RAM copy of the internal flash with the same
 *     rules as flash.c.
 * - Not `fake_obdh_requests.c`: here the real request functions are tested.
 *
 * **Fake setup**
 * - All 7 tasks are created with `tm_create_tasks()`, so OBDH has a real
 *   handle and task number.
 * - The test task plays the client. The block hook `run_obdh_once()` makes
 *   OBDH the current task and runs one `process_obdh()`.
 *
 * **How to run**
 * @code
 * docker run --rm -v "${PWD}:/app" pocat-tests test_obdh
 * docker run --rm -v "${PWD}:/app" pocat-tests test_obdh -f paused
 * @endcode
 */
#include "unity.h"
#include "fake_rtos.h"
#include "fake_hal.h"
#include "fake_flash_driver.h"
#include "pocat_test_main.h"

#include "obdh.c"
#include "obdh_requests.c"
#include "task_management.c"
#include "health.c"

/** @brief Length of the OBDH request queue, the same value obc.c uses when it creates the queue. */
#define TEST_OBDH_QUEUE_LEN 10

/** @brief A page-aligned address inside internal flash used as scratch space (start of the 2 KB page at 0x08040000). */
#define SCRATCH_ADDR 0x08040000u

/** @brief Position of OBDH in `task_table[]` (bit 4 of the `TM_TASK_*` flags). */
#define IDX_OBDH 4

/** @brief Handle of the OBDH task created by tm_create_tasks() in setUp(). */
static TaskHandle_t obdh;

/** @brief Number of times the block hook ran process_obdh(). Lets a test prove OBDH was never involved. */
static int obdh_runs;

/**
 * @brief Block hook: runs one iteration of the OBDH task as the OBDH task.
 * @details Called by the fake RTOS when the client is about to block waiting
 * for the reply. It switches the current task to OBDH, so that
 * `tm_check_pause()` and `wait_for_notification()` act on OBDH, runs
 * `process_obdh()` once, and switches back.
 */
static void run_obdh_once(void)
{
    TaskHandle_t previous = fake_rtos_current_task();
    fake_rtos_set_current_task(obdh);
    process_obdh();
    fake_rtos_set_current_task(previous);
    obdh_runs++;
}

/**
 * @brief Helper: runs one OBDH iteration directly, outside any client call.
 * @details Used by tests that put requests on the queue by hand, or that
 * check how OBDH reacts to PAUSE/RESUME. The block hook is removed during
 * the step. Otherwise, if the queue were empty, OBDH's own `xQueueReceive`
 * would trigger the hook and run a second, nested OBDH iteration.
 */
static void obdh_step(void)
{
    fake_rtos_set_block_hook(NULL);
    run_obdh_once();
    fake_rtos_set_block_hook(run_obdh_once);
}

/**
 * @brief Helper: puts a request on the OBDH queue by hand, bypassing the client functions.
 * @param op     Flash operation.
 * @param addr   Flash address.
 * @param src    Data buffer (for READ it is also the destination).
 * @param len    Number of bytes.
 * @param client Task to notify on completion, or NULL for no notification.
 */
static void enqueue_raw(obdh_flash_op op, uint32_t addr, uint8_t *src, size_t len, TaskHandle_t client)
{
    obdh_request r = { .op = op, .addr = addr, .len = len, .client = client };
    r.buf.dst = src;
    TEST_ASSERT_EQUAL_INT(pdTRUE, xQueueSend(obdh_queue_handle, &r, 0));
}

/**
 * @brief Runs before every test: fresh fakes, erased flash, the OBDH queue, all 7 tasks, and the OBDH block hook.
 * @details
 * 1. Resets the fake RTOS, the fake HAL and the fake flash driver. The flash
 *    is all 0xFF.
 * 2. Resets the statics of the included modules:
 *    - the task table and its event group;
 *    - the health module;
 *    - OBDH's own `deferred_notifications`.
 * 3. Creates the OBDH queue, as `setup_obc()` does on the satellite, and all
 *    7 tasks.
 * 4. Installs `run_obdh_once()` as the block hook.
 */
void setUp(void)
{
    fake_rtos_reset();
    fake_hal_reset();
    fake_flash_driver_reset();

    for (size_t i = 0; i < TASK_TABLE_SIZE; i++) {
        task_table[i].handle = NULL;
        task_table[i].paused = false;
    }
    task_event_group_handle = NULL;
    health_eg = NULL;
    expected_bits = 0;
    period_ticks = 0;
    next_deadline = 0;
    iwdg_handle = NULL;
    health_init();
    deferred_notifications = 0;

    obdh_queue_handle = xQueueCreate(TEST_OBDH_QUEUE_LEN, sizeof(obdh_request));
    TEST_ASSERT_NOT_NULL(obdh_queue_handle);
    TEST_ASSERT_EQUAL_INT(pdPASS, tm_create_tasks(TM_TASK_ALL, 0));
    obdh = tm_get_task_handle(TM_TASK_OBDH);
    TEST_ASSERT_NOT_NULL(obdh);

    obdh_runs = 0;
    fake_rtos_set_block_hook(run_obdh_once);
}

/** @brief Runs after every test: checks that the code left the RTOS balanced. */
void tearDown(void)
{
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, fake_rtos_critical_depth(), "unbalanced critical section");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, fake_rtos_suspend_all_depth(), "unbalanced scheduler suspend");
}

/* =========================================================================
 * 1. Round trips: client function → OBDH → flash driver → reply
 * ========================================================================= */

/**
 * @brief `obdh_write_request()` writes the bytes to flash through OBDH and returns `HAL_OK`.
 * @details Expected:
 * - the bytes are in the fake flash;
 * - OBDH ran exactly once and called `flash_write()` once;
 * - the queue is empty again;
 * - the reply on index 1 has been read, so nothing is left pending for the
 *   next request.
 */
void test_write_request_round_trip(void)
{
    const uint8_t data[4] = { 0xDE, 0xAD, 0xBE, 0xEF };

    TEST_ASSERT_EQUAL_INT(HAL_OK, obdh_write_request(SCRATCH_ADDR, data, sizeof(data)));

    TEST_ASSERT_EQUAL_HEX8_ARRAY(data, fake_flash_driver_mem(SCRATCH_ADDR), sizeof(data));
    TEST_ASSERT_EQUAL_INT(1, obdh_runs);
    TEST_ASSERT_EQUAL_INT(1, fake_flash_driver_counts().write);
    TEST_ASSERT_EQUAL_UINT(0, fake_rtos_queue_items(obdh_queue_handle));
    TEST_ASSERT_FALSE(fake_rtos_notify_pending(fake_rtos_test_task(), OBDH_NOTIFY_IDX));
}

/**
 * @brief `obdh_read_request()` returns the bytes stored in flash.
 * @details 6 bytes are preset in the fake flash, then read into a buffer
 * through OBDH.
 */
void test_read_request_returns_flash_contents(void)
{
    const uint8_t stored[6] = { 1, 2, 3, 4, 5, 6 };
    uint8_t out[6] = { 0 };
    memcpy(fake_flash_driver_mem(SCRATCH_ADDR), stored, sizeof(stored));

    TEST_ASSERT_EQUAL_INT(HAL_OK, obdh_read_request(SCRATCH_ADDR, out, sizeof(out)));

    TEST_ASSERT_EQUAL_HEX8_ARRAY(stored, out, sizeof(out));
    TEST_ASSERT_EQUAL_INT(1, fake_flash_driver_counts().read);
}

/**
 * @brief `obdh_program_request()` on erased flash programs the bytes and returns `HAL_OK`.
 * @details Programming without an erase is how ht_handling fills the slots of
 * a page that is already erased. 8 bytes at an 8-byte-aligned address.
 */
void test_program_request_on_erased_flash(void)
{
    const uint8_t data[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };

    TEST_ASSERT_EQUAL_INT(HAL_OK, obdh_program_request(SCRATCH_ADDR, data, sizeof(data)));

    TEST_ASSERT_EQUAL_HEX8_ARRAY(data, fake_flash_driver_mem(SCRATCH_ADDR), sizeof(data));
    TEST_ASSERT_EQUAL_INT(0, fake_flash_driver_counts().erase);
}

/**
 * @brief An error from the flash driver reaches the client: programming over data that isn't erased returns `HAL_ERROR`.
 * @details The first byte of the target is preset to 0x00 (not erased). The
 * driver refuses, and OBDH must pass `HAL_ERROR` back to the caller instead
 * of reporting success. ht_handling relies on this error to skip to the next
 * page.
 */
void test_driver_error_is_forwarded_to_client(void)
{
    const uint8_t data[8] = { 0 };
    fake_flash_driver_mem(SCRATCH_ADDR)[0] = 0x00;

    TEST_ASSERT_EQUAL_INT(HAL_ERROR, obdh_program_request(SCRATCH_ADDR, data, sizeof(data)));
}

/**
 * @brief `obdh_erase_program_request()` erases the whole 2 KB page and then programs the new bytes.
 * @details
 * - Arrange: a byte elsewhere in the same page (offset 1000) holds 0x12.
 * - Expected:
 *   - the new 8 bytes are at the start of the page;
 *   - the byte at offset 1000 is 0xFF, because the whole page was erased;
 *   - exactly 1 erase and 1 program happened.
 * This is why only the module that owns a whole page may use this request.
 */
void test_erase_program_erases_whole_page_then_programs(void)
{
    const uint8_t data[8] = { 9, 9, 9, 9, 9, 9, 9, 9 };
    fake_flash_driver_mem(SCRATCH_ADDR + 1000u)[0] = 0x12;

    TEST_ASSERT_EQUAL_INT(HAL_OK, obdh_erase_program_request(SCRATCH_ADDR, data, sizeof(data)));

    TEST_ASSERT_EQUAL_HEX8_ARRAY(data, fake_flash_driver_mem(SCRATCH_ADDR), sizeof(data));
    TEST_ASSERT_EQUAL_HEX8(0xFF, fake_flash_driver_mem(SCRATCH_ADDR + 1000u)[0]);
    TEST_ASSERT_EQUAL_INT(1, fake_flash_driver_counts().erase);
    TEST_ASSERT_EQUAL_INT(1, fake_flash_driver_counts().program);
}

/**
 * @brief If the erase fails, OBDH doesn't program and reports `HAL_ERROR`.
 * @details Programming after a failed erase could write over old data and
 * produce corrupted bytes. Expected: 1 erase attempt, 0 programs, `HAL_ERROR`.
 */
void test_erase_failure_skips_program(void)
{
    const uint8_t data[8] = { 0 };
    fake_flash_driver_inject_error(HAL_ERROR);

    TEST_ASSERT_EQUAL_INT(HAL_ERROR, obdh_erase_program_request(SCRATCH_ADDR, data, sizeof(data)));

    TEST_ASSERT_EQUAL_INT(1, fake_flash_driver_counts().erase);
    TEST_ASSERT_EQUAL_INT(0, fake_flash_driver_counts().program);
}

/* =========================================================================
 * 2. Client-side checks and the reply channel
 * ========================================================================= */

/**
 * @brief A NULL buffer or a zero length is rejected by the client function, before anything is queued.
 * @details All 4 request functions are checked. Expected: `HAL_ERROR`, an
 * empty queue, and OBDH never ran.
 */
void test_invalid_arguments_rejected_without_enqueue(void)
{
    uint8_t buf[8] = { 0 };

    TEST_ASSERT_EQUAL_INT(HAL_ERROR, obdh_write_request(SCRATCH_ADDR, NULL, 4));
    TEST_ASSERT_EQUAL_INT(HAL_ERROR, obdh_read_request(SCRATCH_ADDR, NULL, 4));
    TEST_ASSERT_EQUAL_INT(HAL_ERROR, obdh_program_request(SCRATCH_ADDR, NULL, 8));
    TEST_ASSERT_EQUAL_INT(HAL_ERROR, obdh_erase_program_request(SCRATCH_ADDR, NULL, 8));
    TEST_ASSERT_EQUAL_INT(HAL_ERROR, obdh_write_request(SCRATCH_ADDR, buf, 0));
    TEST_ASSERT_EQUAL_INT(HAL_ERROR, obdh_read_request(SCRATCH_ADDR, buf, 0));

    TEST_ASSERT_EQUAL_UINT(0, fake_rtos_queue_items(obdh_queue_handle));
    TEST_ASSERT_EQUAL_INT(0, obdh_runs);
}

/**
 * @brief When the OBDH queue stays full, the client gives up after `FLASH_QUEUE_SEND_TIMEOUT_MS` and returns `HAL_BUSY`.
 * @details
 * - Arrange: no block hook (OBDH never runs), and the queue is filled with
 *   10 requests by hand.
 * - Expected: `HAL_BUSY`, and the fake clock advanced by exactly the 100 ms
 *   send timeout. The caller isn't blocked forever and can retry or report.
 */
void test_full_queue_returns_busy_after_timeout(void)
{
    uint8_t buf[4] = { 0 };
    const uint8_t data[4] = { 1, 2, 3, 4 };
    fake_rtos_set_block_hook(NULL);
    for (int i = 0; i < TEST_OBDH_QUEUE_LEN; i++) {
        enqueue_raw(FLASH_READ, SCRATCH_ADDR, buf, sizeof(buf), NULL);
    }
    TickType_t before = xTaskGetTickCount();

    TEST_ASSERT_EQUAL_INT(HAL_BUSY, obdh_write_request(SCRATCH_ADDR, data, sizeof(data)));

    TEST_ASSERT_EQUAL_UINT32(before + FLASH_QUEUE_SEND_TIMEOUT_MS, xTaskGetTickCount());
    TEST_ASSERT_EQUAL_UINT(TEST_OBDH_QUEUE_LEN, fake_rtos_queue_items(obdh_queue_handle));
}

/**
 * @brief OBDH replies on notification index 1 only, with overwrite, so it never mixes with the client's own index-0 command bits.
 * @details After one write request, the notify log must contain exactly one
 * entry from OBDH: target = the client, index = 1, action =
 * `eSetValueWithOverwrite`, value = `HAL_OK`.
 * test_notifications (`test_obdh_index1_does_not_leak_into_index0`) checks
 * the same guarantee from the receiving side: an index-1 value never shows
 * up in `wait_for_notification()`.
 */
void test_reply_uses_index1_only(void)
{
    const uint8_t data[4] = { 1, 2, 3, 4 };
    int log_before = fake_rtos_notify_log_count();

    TEST_ASSERT_EQUAL_INT(HAL_OK, obdh_write_request(SCRATCH_ADDR, data, sizeof(data)));

    TEST_ASSERT_EQUAL_INT(log_before + 1, fake_rtos_notify_log_count());
    fake_notify_rec_t rec = fake_rtos_notify_log_at(log_before);
    TEST_ASSERT_EQUAL_PTR(fake_rtos_test_task(), rec.target);
    TEST_ASSERT_EQUAL_UINT(OBDH_NOTIFY_IDX, rec.index);
    TEST_ASSERT_EQUAL_INT(eSetValueWithOverwrite, rec.action);
    TEST_ASSERT_EQUAL_UINT32(HAL_OK, rec.value);
}

/**
 * @brief OBDH sends a flash-write error from the driver back to the client.
 * @details With an error injected into the driver, `obdh_write_request()`
 * returns `HAL_ERROR` after exactly one `flash_write()` attempt. This adds to
 * test_driver_error_is_forwarded_to_client, which covers the program path.
 */
void test_write_error_is_forwarded_to_client(void)
{
    const uint8_t data[4] = { 1, 2, 3, 4 };
    fake_flash_driver_inject_error(HAL_ERROR);

    TEST_ASSERT_EQUAL_INT(HAL_ERROR, obdh_write_request(SCRATCH_ADDR, data, sizeof(data)));
    TEST_ASSERT_EQUAL_INT(1, fake_flash_driver_counts().write);
}

/**
 * @brief A program request whose address or length isn't a multiple of 8 fails and leaves the flash untouched.
 * @details The STM32L4 programs flash 8 bytes (a double word) at a time.
 * Both cases must come back as `HAL_ERROR`, and the target stays erased:
 * - 8 bytes at an address 4 bytes past an aligned one;
 * - 5 bytes at an aligned address.
 * ht_handling relies on this rule: its slots are 128 bytes, a multiple of 8.
 */
void test_unaligned_program_is_rejected(void)
{
    const uint8_t data[8] = { 0 };

    TEST_ASSERT_EQUAL_INT(HAL_ERROR, obdh_program_request(SCRATCH_ADDR + 4u, data, sizeof(data)));
    TEST_ASSERT_EQUAL_INT(HAL_ERROR, obdh_program_request(SCRATCH_ADDR, data, 5));
    TEST_ASSERT_EQUAL_HEX8(0xFF, fake_flash_driver_mem(SCRATCH_ADDR)[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, fake_flash_driver_mem(SCRATCH_ADDR + 4u)[0]);
}

/**
 * @brief A request with a NULL buffer that reaches the queue anyway gets a `HAL_ERROR` reply, and the driver isn't called.
 * @details The client functions already reject NULL (see
 * test_invalid_arguments_rejected_without_enqueue), but any task can put a
 * raw request on the queue. OBDH's own `buf.src != NULL` check is the last
 * line of defence. Expected: the client gets `HAL_ERROR` on index 1, and
 * there are no driver calls.
 */
void test_null_buffer_in_queue_replies_error_without_driver_call(void)
{
    enqueue_raw(FLASH_WRITE, SCRATCH_ADDR, NULL, 4, fake_rtos_test_task());

    obdh_step();

    TEST_ASSERT_TRUE(fake_rtos_notify_pending(fake_rtos_test_task(), OBDH_NOTIFY_IDX));
    TEST_ASSERT_EQUAL_UINT32(HAL_ERROR, fake_rtos_notify_value(fake_rtos_test_task(), OBDH_NOTIFY_IDX));
    fake_flash_driver_counts_t c = fake_flash_driver_counts();
    TEST_ASSERT_EQUAL_INT(0, c.read + c.write + c.program + c.erase);
}

/**
 * @brief A request with an unknown operation code gets a `HAL_ERROR` reply, and the driver isn't called.
 * @details This is the `default:` branch of the switch in `process_obdh()`.
 * A corrupted request must not reach the flash, and the client must still be
 * answered so it doesn't wait forever.
 */
void test_unknown_operation_replies_error_without_driver_call(void)
{
    uint8_t buf[4] = { 0 };
    enqueue_raw((obdh_flash_op)99, SCRATCH_ADDR, buf, sizeof(buf), fake_rtos_test_task());

    obdh_step();

    TEST_ASSERT_EQUAL_UINT32(HAL_ERROR, fake_rtos_notify_value(fake_rtos_test_task(), OBDH_NOTIFY_IDX));
    fake_flash_driver_counts_t c = fake_flash_driver_counts();
    TEST_ASSERT_EQUAL_INT(0, c.read + c.write + c.program + c.erase);
}

/**
 * @brief Requests from two tasks are served in arrival order, and each reply goes to the task that asked.
 * @details The test task queues a write first and EPS queues one second.
 * - After the first OBDH iteration: only the test task has its reply, and
 *   only its bytes are written.
 * - After the second iteration: EPS has its reply too.
 * A mix-up would hand one task's status to another, and it would believe
 * its write had succeeded or failed when it hadn't.
 */
void test_requests_served_in_order_each_reply_to_its_client(void)
{
    uint8_t a[4] = { 0xA1, 0xA2, 0xA3, 0xA4 };
    uint8_t b[4] = { 0xB1, 0xB2, 0xB3, 0xB4 };
    TaskHandle_t eps = tm_get_task_handle(TM_TASK_EPS);
    enqueue_raw(FLASH_WRITE, SCRATCH_ADDR,       a, sizeof(a), fake_rtos_test_task());
    enqueue_raw(FLASH_WRITE, SCRATCH_ADDR + 16u, b, sizeof(b), eps);

    obdh_step();
    TEST_ASSERT_TRUE(fake_rtos_notify_pending(fake_rtos_test_task(), OBDH_NOTIFY_IDX));
    TEST_ASSERT_FALSE(fake_rtos_notify_pending(eps, OBDH_NOTIFY_IDX));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(a, fake_flash_driver_mem(SCRATCH_ADDR), sizeof(a));
    TEST_ASSERT_EQUAL_HEX8(0xFF, fake_flash_driver_mem(SCRATCH_ADDR + 16u)[0]);

    obdh_step();
    TEST_ASSERT_TRUE(fake_rtos_notify_pending(eps, OBDH_NOTIFY_IDX));
    TEST_ASSERT_EQUAL_UINT32(HAL_OK, fake_rtos_notify_value(eps, OBDH_NOTIFY_IDX));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(b, fake_flash_driver_mem(SCRATCH_ADDR + 16u), sizeof(b));
}

/**
 * @brief A request without a client (`client = NULL`) is still served, and nobody is notified.
 * @details OBDH checks `client != NULL` before notifying. Expected: the flash
 * is written and the notify log doesn't grow.
 */
void test_request_without_client_is_served_silently(void)
{
    uint8_t data[4] = { 7, 7, 7, 7 };
    int log_before = fake_rtos_notify_log_count();
    enqueue_raw(FLASH_WRITE, SCRATCH_ADDR, data, sizeof(data), NULL);

    obdh_step();

    TEST_ASSERT_EQUAL_HEX8_ARRAY(data, fake_flash_driver_mem(SCRATCH_ADDR), sizeof(data));
    TEST_ASSERT_EQUAL_INT(log_before, fake_rtos_notify_log_count());
}

/* =========================================================================
 * 3. The OBDH loop: idle pacing and pause/resume
 * ========================================================================= */

/**
 * @brief With nothing to do, one OBDH iteration waits on the queue for 1 s, then returns without touching flash.
 * @details This 1 s wait paces the OBDH loop, which kicks the health monitor
 * once per iteration. Expected: the clock advanced by 1000 ticks, and there
 * were no driver calls.
 */
void test_idle_obdh_waits_one_second_on_queue(void)
{
    TickType_t before = xTaskGetTickCount();

    obdh_step();

    TEST_ASSERT_EQUAL_UINT32(before + 1000u, xTaskGetTickCount());
    fake_flash_driver_counts_t c = fake_flash_driver_counts();
    TEST_ASSERT_EQUAL_INT(0, c.read + c.write + c.program + c.erase);
}

/**
 * @brief A paused OBDH leaves requests in the queue, and serves them once it is resumed.
 * @details
 * 1. OBDH gets `N_TASK_PAUSE` and runs one iteration. It marks itself
 *    paused, sets its ACK bit for the OBC, and leaves health monitoring.
 * 2. A write request is queued and OBDH runs again. The request stays
 *    queued and the flash is untouched.
 * 3. OBDH gets `N_TASK_RESUME` and runs again. It resumes, is monitored
 *    again, and serves the request in the same iteration.
 */
void test_paused_obdh_leaves_requests_queued_until_resume(void)
{
    uint8_t data[4] = { 5, 5, 5, 5 };

    xTaskNotify(obdh, N_TASK_PAUSE, eSetBits);
    obdh_step();
    TEST_ASSERT_TRUE(task_table[IDX_OBDH].paused);
    TEST_ASSERT_BITS_HIGH(EV_OBDH_ACK, xEventGroupGetBits(task_event_group_handle));   /* ACK sent to the OBC */
    TEST_ASSERT_BITS_LOW(HEALTH_BIT_OBDH, health_get_expected());                      /* no longer monitored */

    enqueue_raw(FLASH_WRITE, SCRATCH_ADDR, data, sizeof(data), NULL);
    obdh_step();
    TEST_ASSERT_EQUAL_UINT(1, fake_rtos_queue_items(obdh_queue_handle));
    TEST_ASSERT_EQUAL_HEX8(0xFF, fake_flash_driver_mem(SCRATCH_ADDR)[0]);

    xTaskNotify(obdh, N_TASK_RESUME, eSetBits);
    obdh_step();
    TEST_ASSERT_FALSE(task_table[IDX_OBDH].paused);
    TEST_ASSERT_BITS_HIGH(HEALTH_BIT_OBDH, health_get_expected());                     /* monitored again */
    TEST_ASSERT_EQUAL_UINT(0, fake_rtos_queue_items(obdh_queue_handle));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(data, fake_flash_driver_mem(SCRATCH_ADDR), sizeof(data));
}

/**
 * @brief While OBDH is paused, a client that asks for flash blocks forever.
 * @details This test documents the current behaviour; it is not a bug report.
 * `obdh_submit_request()` waits with `portMAX_DELAY`, so while OBDH is paused
 * the caller hangs. The fake detects this as a deadlock. In practice OBDH is
 * in the task set of every state (see `tasks_for_state()`), so the state
 * machine never pauses it. If that ever changes, this test shows what would
 * happen. The `@todo` in obdh_requests.c ("consider other options other than
 * simply indefinitely blocking the caller") is the same concern.
 */
void test_client_blocks_forever_while_obdh_paused(void)
{
    const uint8_t data[4] = { 1, 2, 3, 4 };
    xTaskNotify(obdh, N_TASK_PAUSE, eSetBits);
    obdh_step();

    fake_rtos_expect_deadlock(true);
    (void)obdh_write_request(SCRATCH_ADDR, data, sizeof(data));

    TEST_ASSERT_TRUE(fake_rtos_deadlock_detected());
    TEST_ASSERT_EQUAL_UINT(1, fake_rtos_queue_items(obdh_queue_handle));
}

/* =========================================================================
 * 4. Known bugs (ignored until the production code is fixed)
 * ========================================================================= */

/**
 * @brief A paused OBDH should block, waiting for RESUME, instead of returning straight away.
 * @details
 * - Expected: one iteration of a paused OBDH takes time; it blocks on its
 *   notification, so the tick advances.
 * - Actual: `process_obdh()` reads its notifications with timeout 0 and
 *   returns as soon as `tm_check_pause()` says "paused", so the tick doesn't
 *   move. On the satellite the `for (;;)` loop in `obdh_task()` would spin
 *   without ever blocking. OBDH runs at priority 5, so this would starve the
 *   lower-priority tasks, which then couldn't ACK their own pause requests.
 *   Impact today is low because OBDH is never paused (see
 *   test_client_blocks_forever_while_obdh_paused).
 * @bug KI-5 (test/README.md). Confirmed on the host: the paused iteration
 * takes 0 ticks.
 */
void test_paused_obdh_blocks_instead_of_spinning(void)
{
    TEST_IGNORE_MESSAGE("KNOWN BUG KI-5: paused OBDH returns immediately (0 ticks), so obdh_task busy-loops at priority 5");
    xTaskNotify(obdh, N_TASK_PAUSE, eSetBits);
    obdh_step();
    TickType_t before = xTaskGetTickCount();

    obdh_step();

    TEST_ASSERT_GREATER_THAN_UINT32(before, xTaskGetTickCount());
}

/**
 * @brief Test runner. Lists every test of this suite, in execution order.
 * @details A test function that isn't listed here never runs. See
 * test/support/pocat_test_main.h for the `-l` / `-f` / `-n` / `-x` options.
 */
int main(int argc, char **argv)
{
    if (pocat_parse_args(argc, argv) != 0) return 1;
    POCAT_BEGIN();
    POCAT_RUN_TEST(test_write_request_round_trip);
    POCAT_RUN_TEST(test_read_request_returns_flash_contents);
    POCAT_RUN_TEST(test_program_request_on_erased_flash);
    POCAT_RUN_TEST(test_driver_error_is_forwarded_to_client);
    POCAT_RUN_TEST(test_erase_program_erases_whole_page_then_programs);
    POCAT_RUN_TEST(test_erase_failure_skips_program);
    POCAT_RUN_TEST(test_invalid_arguments_rejected_without_enqueue);
    POCAT_RUN_TEST(test_full_queue_returns_busy_after_timeout);
    POCAT_RUN_TEST(test_reply_uses_index1_only);
    POCAT_RUN_TEST(test_request_without_client_is_served_silently);
    POCAT_RUN_TEST(test_write_error_is_forwarded_to_client);
    POCAT_RUN_TEST(test_unaligned_program_is_rejected);
    POCAT_RUN_TEST(test_null_buffer_in_queue_replies_error_without_driver_call);
    POCAT_RUN_TEST(test_unknown_operation_replies_error_without_driver_call);
    POCAT_RUN_TEST(test_requests_served_in_order_each_reply_to_its_client);
    POCAT_RUN_TEST(test_idle_obdh_waits_one_second_on_queue);
    POCAT_RUN_TEST(test_paused_obdh_leaves_requests_queued_until_resume);
    POCAT_RUN_TEST(test_client_blocks_forever_while_obdh_paused);
    POCAT_RUN_TEST(test_paused_obdh_blocks_instead_of_spinning);
    return POCAT_END();
}
