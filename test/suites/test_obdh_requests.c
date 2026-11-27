/**
 * @file test_obdh_requests.c
 * @brief Host tests for the OBDH request protocol: block time, data by copy, reply queues, request numbers, restarts and posts (spec §5.2.1, tests V11 and V14).
 * @date 2026-11-11
 * @note Modified on 2026-11-21 (flash plan step 5a): the protocol is driven
 * through the requests that remain, the parameter write and the HT block read.
 *
 * @details
 * **What is under test**
 * - src/subsystems/obdh/obdh_requests.c, the client side: every request
 *   function, prepare(), prepare_with_data(), send(), fetch_block(), post()
 *   and round_trip().
 * - src/subsystems/obdh/obdh.c, the OBDH side: obdh_queues_create(),
 *   setup_obdh(), process_obdh() and serve().
 *
 * **How it is built** (see test/CMakeLists.txt)
 * - Both files are `#include`d below, so the tests reach their statics
 *   (the client slots, setup_obdh(), process_obdh()).
 * - Fakes defined below replace the flash interface reset, the parameter
 *   store, the telemetry-history store, `tm_current_index()` (the calling
 *   client is whatever the test sets), `tm_check_pause()`,
 *   `wait_for_notification()` and `health_kick()`. Each fake logs its call as
 *   one letter in `events`.
 * - obdh_param_write_request() stands for the requests that carry data
 *   (send()), obdh_ht_read_request() for those that return data
 *   (fetch_block()).
 * - The fake RTOS runs one thing at a time. When a client is about to block
 *   for its answer, the block hook plays the OBDH task: it runs process_obdh()
 *   until the queue is empty, and fails the test if a cycle takes no request
 *   (the real task would spin). With `obdh_stuck` set it does nothing, so the
 *   wait runs out its block time. Time only moves when a wait runs out, or
 *   when a test's own hook moves it.
 * - OBDH's notifications are modelled too. A test sends bits to OBDH in
 *   `notified`; the fake tm_check_pause() follows the rules of the real one;
 *   when OBDH sleeps in wait_for_notification(portMAX_DELAY), the hook
 *   `while_obdh_sleeps` plays the OBC meanwhile and must notify OBDH.
 *
 * **How to run**
 * @code
 * docker run --rm -v "${PWD}:/app" pocat-tests test_obdh_requests
 * docker run --rm -v "${PWD}:/app" pocat-tests test_obdh_requests -f deadline
 * @endcode
 */
#include "unity.h"
#include "fake_rtos.h"
#include "fake_hal.h"
#include "pocat_test_main.h"

#include "obdh.c"
#include "obdh_requests.c"

#include <string.h>

#define CLIENT_EPS    1   /**< Task-table index of EPS. */
#define CLIENT_COMMS  2   /**< Task-table index of COMMS. */
#define CLIENT_BEACON 6   /**< Task-table index of the beacon task, which posts the beacons. */
#define HT_TEST_LEN   37u /**< Length of the block the fake HT store returns, unless a test sets block_len. */

/* ---- Fakes of what the request functions and OBDH call ---- */

static int  current_client;             /**< What tm_current_index() returns: the calling task. */
static bool obdh_stuck;                 /**< The block hook serves nothing. */
static HAL_StatusTypeDef store_result;  /**< What every fake store call returns. */
static uint32_t block_len;              /**< Length of the block the fake HT store returns. */

static bool obdh_paused;                /**< OBDH's paused flag in the task table, kept by the fake tm_check_pause(). */
static uint32_t notified;               /**< Bits sent to OBDH that it has not read yet. */
static int  obdh_sleeps;                /**< Times OBDH waited for a notification without a time limit. */
static void (*while_obdh_sleeps)(void); /**< What the OBC does while OBDH sleeps; it must notify OBDH. */

static char events[64];                 /**< One letter per call: R reset, L parameter load, H HT load, P param write, B beacon added, h HT read. */
static int  n_events;

static uint32_t last_len;
static uint8_t  last_param_id;
static uint8_t  last_data[OBDH_DATA_MAX];

static void log_event(char c)
{
    if (n_events < (int)sizeof(events) - 1) {
        events[n_events++] = c;
        events[n_events] = '\0';
    }
}

static void keep(const uint8_t *data, uint32_t len)
{
    last_len = len;
    memcpy(last_data, data, len);
}

/** @brief What the fake HT store holds at byte i of the block it returns for from_seq: every read can be told apart. */
static uint8_t pattern(uint32_t from_seq, uint32_t i)
{
    return (uint8_t)(from_seq + i);
}

int tm_current_index(void) { return current_client; }
void health_kick(EventBits_t bit) { (void)bit; }

/** @brief The rules of the real tm_check_pause() (test_task_management): a pause holds until a resume, a resume wins, other bits wait while paused. */
bool tm_check_pause(uint32_t notif, uint32_t *deferred)
{
    if ((notif & N_TASK_RESUME) != 0u) {
        obdh_paused = false;
    } else if ((notif & N_TASK_PAUSE) != 0u) {
        obdh_paused = true;
    }
    if (obdh_paused) {
        *deferred |= notif & ~(N_TASK_PAUSE | N_TASK_RESUME);
    }
    return obdh_paused;
}

/**
 * @brief OBDH's notification wait: returns the bits sent to OBDH and clears them.
 * @details With nothing pending, a poll (timeout 0) returns 0 at once. A wait
 * without a time limit is OBDH going to sleep: `while_obdh_sleeps` plays the
 * OBC meanwhile, and must wake OBDH with a notification.
 */
uint32_t wait_for_notification(TickType_t timeout)
{
    if ((notified == 0u) && (timeout != 0u)) {
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(portMAX_DELAY, timeout, "OBDH waits for a notification with a time limit");
        obdh_sleeps++;
        TEST_ASSERT_TRUE_MESSAGE(while_obdh_sleeps != NULL, "OBDH sleeps and nothing wakes it");
        while_obdh_sleeps();
        TEST_ASSERT_TRUE_MESSAGE(notified != 0u, "the OBC left OBDH asleep");
    }
    uint32_t bits = notified;
    notified = 0u;
    return bits;
}

void flash_interface_reset(void) { log_event('R'); }
HAL_StatusTypeDef param_store_load(void) { log_event('L'); return HAL_OK; }

HAL_StatusTypeDef param_store_write(uint8_t id, const uint8_t *value, size_t size)
{
    log_event('P');
    last_param_id = id;
    keep(value, (uint32_t)size);
    return store_result;
}

HAL_StatusTypeDef ht_store_load(void) { log_event('H'); return HAL_OK; }

HAL_StatusTypeDef ht_store_add(const uint8_t beacon[HT_BEACON_SIZE])
{
    log_event('B');
    keep(beacon, HT_BEACON_SIZE);
    return store_result;
}

/**
 * @brief The fake HT store holds, for any from_seq, a block_len-byte block numbered from_seq + 5.
 * @details OBDH reads into the answer's OBDH_DATA_MAX bytes, so a test can make
 *          the block longer than any HT block, as a broken store would.
 */
HAL_StatusTypeDef ht_store_read(uint32_t from_seq, uint8_t block[HT_BLOCK_MAX], uint32_t *len, uint32_t *seq)
{
    log_event('h');
    TEST_ASSERT_LESS_OR_EQUAL_UINT32_MESSAGE(OBDH_DATA_MAX, block_len, "the fake block must fit an answer");
    for (uint32_t i = 0; i < block_len; i++) block[i] = pattern(from_seq, i);
    *len = block_len;
    *seq = from_seq + 5u;
    return store_result;
}

/* ---- The requests the tests send ---- */

static uint8_t  got[HT_BLOCK_MAX];   /**< Where read_ht() puts the block. */
static uint32_t got_len;             /**< Its length. */
static uint32_t got_seq;             /**< Its number. */

/** @brief The calling client reads the HT block from from_seq: a request that returns data. */
static HAL_StatusTypeDef read_ht(uint32_t from_seq, TickType_t ticks_to_wait)
{
    return obdh_ht_read_request(from_seq, got, &got_len, &got_seq, ticks_to_wait);
}

/** @brief The calling client writes 8 bytes to a parameter: a request that carries data. */
static HAL_StatusTypeDef write_param(TickType_t ticks_to_wait)
{
    static const uint8_t value[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    return obdh_param_write_request(0x42, value, sizeof(value), ticks_to_wait);
}

/* ---- Block hooks: what OBDH does while a client waits ---- */

/**
 * @brief The OBDH task: runs its cycle until the queue is empty; stuck, it does nothing.
 * @details While the queue holds a request, each cycle must take one. A cycle
 * that returns without one runs again at once: the real task would spin at its
 * priority and starve every lower-priority task (KI-5).
 */
static void run_obdh(void)
{
    if (obdh_stuck) return;
    while (fake_rtos_queue_items(obdh_queue_handle) > 0u) {
        uint32_t before = (uint32_t)fake_rtos_queue_items(obdh_queue_handle);
        process_obdh();
        TEST_ASSERT_LESS_THAN_UINT32_MESSAGE(before, (uint32_t)fake_rtos_queue_items(obdh_queue_handle), "an OBDH cycle took no request: the task would spin");
    }
}

static int hook_calls;

/** @brief A slow OBDH: the first time a client blocks, 60 ticks pass and OBDH serves one request; then it serves nothing. */
static void slow_obdh_once(void)
{
    if (hook_calls++ > 0) return;
    fake_rtos_advance_ticks(60);
    process_obdh();
}

/** @brief The first time a client blocks, 60 ticks pass and an answer to EPS's previous request arrives. */
static void stale_answer_once(void)
{
    if (hook_calls++ > 0) return;
    fake_rtos_advance_ticks(60);
    obdh_answer stale = { .number = slots[CLIENT_EPS].request.header.number - 1u, .status = HAL_OK };
    (void)xQueueOverwrite(obdh_answer_queues[CLIENT_EPS], &stale);
}

/** @brief OBDH takes the request from the queue, then is deleted before it answers. */
static void obdh_dies_while_serving(void)
{
    if (hook_calls++ > 0) return;
    obdh_request taken;
    (void)xQueueReceive(obdh_queue_handle, &taken, 0);
}

/* ---- Sleep hooks: what the OBC does while a paused OBDH sleeps ---- */

/** @brief The OBC switches the clock (30 ticks) and resumes OBDH, which must not have taken the request meanwhile. */
static void obc_resumes_after_30_ticks(void)
{
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, fake_rtos_queue_items(obdh_queue_handle), "a paused OBDH took a request");
    fake_rtos_advance_ticks(30);
    notified |= N_TASK_RESUME;
}

/** @brief The first time OBDH sleeps, a telecommand asks it to clear the HT; the second time, the OBC resumes it. */
static void clear_ht_then_resume(void)
{
    if (obdh_sleeps == 1) {
        notified |= N_OBDH_CLEAR_HT;
        return;
    }
    obc_resumes_after_30_ticks();
}

/* ---- Setup ---- */

/**
 * @brief Runs before every test: fresh queues, fresh client slots, the calling task is EPS, OBDH works.
 * @details The client slots live in static memory on purpose (a restarted
 * task keeps its request numbers), so the suite clears them by hand.
 */
void setUp(void)
{
    fake_rtos_reset();
    fake_hal_reset();
    memset(slots, 0, sizeof(slots));
    deferred_notifications = 0;
    TEST_ASSERT_TRUE(obdh_queues_create());

    current_client = CLIENT_EPS;
    obdh_stuck = false;
    obdh_paused = false;
    notified = 0u;
    obdh_sleeps = 0;
    while_obdh_sleeps = NULL;
    store_result = HAL_OK;
    block_len = HT_TEST_LEN;
    n_events = 0;
    events[0] = '\0';
    hook_calls = 0;
    memset(got, 0xEE, sizeof(got));
    got_len = 0u;
    got_seq = 0u;
    fake_rtos_set_block_hook(run_obdh);
}

/** @brief Runs after every test: no critical section left open, the scheduler not left suspended. */
void tearDown(void)
{
    TEST_ASSERT_EQUAL_INT(0, fake_rtos_critical_depth());
    TEST_ASSERT_EQUAL_INT(0, fake_rtos_suspend_all_depth());
}

/* =========================================================================
 * 1. A request and its answer
 * ========================================================================= */

/** @brief Boot creates the request queue and one reply queue per client (every task of the table, and the OBC); a failed creation is reported. */
void test_queues_are_created_for_every_client(void)
{
    TEST_ASSERT_NOT_NULL(obdh_queue_handle);
    for (uint32_t i = 0; i < OBDH_CLIENTS; i++) {
        TEST_ASSERT_NOT_NULL(obdh_answer_queues[i]);
    }

    fake_rtos_fail_next_queue_create();
    TEST_ASSERT_FALSE(obdh_queues_create());
}

/** @brief An HT read comes back at once, without waiting out the block time, with the block, its length and its number; the rest of the caller's buffer is untouched. */
void test_an_ht_read_returns_the_block_its_length_and_number(void)
{
    TEST_ASSERT_EQUAL_INT(HAL_OK, read_ht(100, OBDH_REQUEST_WAIT));

    TEST_ASSERT_EQUAL_UINT32(HT_TEST_LEN, got_len);
    TEST_ASSERT_EQUAL_UINT32(105, got_seq);
    for (uint32_t i = 0; i < HT_TEST_LEN; i++) {
        TEST_ASSERT_EQUAL_HEX8(pattern(100, i), got[i]);
    }
    TEST_ASSERT_EACH_EQUAL_HEX8(0xEE, &got[HT_TEST_LEN], HT_BLOCK_MAX - HT_TEST_LEN);
    TEST_ASSERT_EQUAL_STRING("h", events);
    TEST_ASSERT_EQUAL_UINT32(0, xTaskGetTickCount());
}

/** @brief Every request reaches its store function with its arguments, and returns OBDH's status. */
void test_every_operation_reaches_its_function(void)
{
    uint8_t data[24];
    for (uint32_t i = 0; i < sizeof(data); i++) data[i] = (uint8_t)(0xA0u + i);

    TEST_ASSERT_EQUAL_INT(HAL_OK, obdh_param_write_request(0x42, data, 16, OBDH_REQUEST_WAIT));
    TEST_ASSERT_EQUAL_HEX8(0x42, last_param_id);
    TEST_ASSERT_EQUAL_UINT32(16, last_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(data, last_data, 16);

    TEST_ASSERT_EQUAL_INT(HAL_OK, read_ht(7, OBDH_REQUEST_WAIT));
    TEST_ASSERT_EQUAL_UINT32(12, got_seq);
    TEST_ASSERT_EQUAL_STRING("Ph", events);

    store_result = HAL_ERROR;   /* the store refuses: the caller gets its status */
    TEST_ASSERT_EQUAL_INT(HAL_ERROR, obdh_param_write_request(0x42, data, 16, OBDH_REQUEST_WAIT));
    TEST_ASSERT_EQUAL_STRING("PhP", events);
}

/** @brief A failed HT read writes nothing back; a read with nowhere to put the block, its length or its number is refused without sending. */
void test_a_failed_or_refused_ht_read_writes_nothing(void)
{
    uint8_t block[HT_BLOCK_MAX];
    memset(block, 0xEE, sizeof(block));
    uint32_t len = 77, seq = 88;
    store_result = HAL_ERROR;
    TEST_ASSERT_EQUAL_INT(HAL_ERROR, obdh_ht_read_request(0, block, &len, &seq, OBDH_REQUEST_WAIT));
    TEST_ASSERT_EACH_EQUAL_HEX8(0xEE, block, sizeof(block));
    TEST_ASSERT_EQUAL_UINT32(77, len);
    TEST_ASSERT_EQUAL_UINT32(88, seq);

    n_events = 0;
    events[0] = '\0';
    TEST_ASSERT_EQUAL_INT(HAL_ERROR, obdh_ht_read_request(0, NULL, &len, &seq, OBDH_REQUEST_WAIT));
    TEST_ASSERT_EQUAL_INT(HAL_ERROR, obdh_ht_read_request(0, block, NULL, &seq, OBDH_REQUEST_WAIT));
    TEST_ASSERT_EQUAL_INT(HAL_ERROR, obdh_ht_read_request(0, block, &len, NULL, OBDH_REQUEST_WAIT));
    TEST_ASSERT_EQUAL_INT(0, n_events);
    TEST_ASSERT_EQUAL_UINT32(0, fake_rtos_queue_items(obdh_queue_handle));
}

/** @brief A read carries no data: its request's length is 0, and the caller's buffer stays on the client's side. */
void test_a_read_request_carries_no_data(void)
{
    obdh_stuck = true;
    TEST_ASSERT_EQUAL_INT(HAL_TIMEOUT, read_ht(9, 0));

    static obdh_request sent;
    TEST_ASSERT_EQUAL_INT(pdTRUE, xQueueReceive(obdh_queue_handle, &sent, 0));
    TEST_ASSERT_EQUAL_INT(HT_READ_BLOCK, sent.header.op);
    TEST_ASSERT_EQUAL_UINT32(9, sent.header.seq);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, sent.header.len, "a read sent a length");
}

/**
 * @brief A block that fills the caller's buffer is copied; a longer one is refused after the answer, and nothing is written.
 * @details No HT block is longer than HT_BLOCK_MAX: the ring takes a header for
 *          blank when its length does not fit the slot. This checks the copy
 *          itself, as if the store were broken.
 */
void test_a_block_longer_than_the_buffer_is_refused(void)
{
    block_len = HT_BLOCK_MAX;
    TEST_ASSERT_EQUAL_INT(HAL_OK, read_ht(3, OBDH_REQUEST_WAIT));
    TEST_ASSERT_EQUAL_UINT32(HT_BLOCK_MAX, got_len);
    for (uint32_t i = 0; i < HT_BLOCK_MAX; i++) {
        TEST_ASSERT_EQUAL_HEX8(pattern(3, i), got[i]);
    }

    memset(got, 0xEE, sizeof(got));
    got_len = 77u;
    got_seq = 88u;
    block_len = HT_BLOCK_MAX + 1u;
    TEST_ASSERT_EQUAL_INT(HAL_ERROR, read_ht(3, OBDH_REQUEST_WAIT));
    TEST_ASSERT_EACH_EQUAL_HEX8(0xEE, got, sizeof(got));
    TEST_ASSERT_EQUAL_UINT32(77, got_len);
    TEST_ASSERT_EQUAL_UINT32(88, got_seq);
    TEST_ASSERT_EQUAL_STRING("hh", events);
}

/**
 * @brief Requests that break the rules are refused at once, and nothing is sent (spec §5.2.1).
 * @details NULL data, a length of 0 or above OBDH_DATA_MAX, and a calling
 * task that is no client, for writes and posts alike. OBDH_DATA_MAX itself is
 * accepted.
 */
void test_bad_requests_are_refused_without_sending(void)
{
    static uint8_t big[OBDH_DATA_MAX + 1u];

    TEST_ASSERT_EQUAL_INT(HAL_ERROR, obdh_param_write_request(1, big, sizeof(big), 100));
    TEST_ASSERT_EQUAL_INT(HAL_ERROR, obdh_param_write_request(1, NULL, 8, 100));
    TEST_ASSERT_EQUAL_INT(HAL_ERROR, obdh_param_write_request(1, big, 0, 100));
    TEST_ASSERT_EQUAL_INT(HAL_ERROR, obdh_ht_post(NULL));
    current_client = -1;
    TEST_ASSERT_EQUAL_INT(HAL_ERROR, write_param(100));
    TEST_ASSERT_EQUAL_INT(HAL_ERROR, read_ht(0, 100));
    TEST_ASSERT_EQUAL_INT(HAL_ERROR, obdh_ht_post(big));
    TEST_ASSERT_EQUAL_UINT32(0, fake_rtos_queue_items(obdh_queue_handle));
    TEST_ASSERT_EQUAL_INT(0, n_events);
    TEST_ASSERT_EQUAL_UINT32(0, xTaskGetTickCount());

    current_client = CLIENT_EPS;
    TEST_ASSERT_EQUAL_INT(HAL_OK, obdh_param_write_request(1, big, OBDH_DATA_MAX, 100));
    TEST_ASSERT_EQUAL_UINT32(OBDH_DATA_MAX, last_len);
}

/* =========================================================================
 * 2. Block time: one deadline, HAL_BUSY or HAL_TIMEOUT
 * ========================================================================= */

/** @brief With OBDH stuck, a request returns HAL_TIMEOUT after exactly its block time: sent, result unknown. */
void test_stuck_obdh_gives_timeout_after_the_block_time(void)
{
    obdh_stuck = true;
    TEST_ASSERT_EQUAL_INT(HAL_TIMEOUT, read_ht(0, 100));
    TEST_ASSERT_EQUAL_UINT32(100, xTaskGetTickCount());
    TEST_ASSERT_EQUAL_UINT32(1, fake_rtos_queue_items(obdh_queue_handle));   /* sent: OBDH may still do it */
}

/** @brief A full queue gives HAL_BUSY after the block time, and the request is not sent. */
void test_full_queue_gives_busy_and_sends_nothing(void)
{
    obdh_stuck = true;
    for (uint32_t i = 0; i < OBDH_QUEUE_LEN; i++) {
        TEST_ASSERT_EQUAL_INT(HAL_TIMEOUT, write_param(0));
    }
    TEST_ASSERT_EQUAL_UINT32(OBDH_QUEUE_LEN, fake_rtos_queue_items(obdh_queue_handle));

    TEST_ASSERT_EQUAL_INT(HAL_BUSY, write_param(50));
    TEST_ASSERT_EQUAL_UINT32(OBDH_QUEUE_LEN, fake_rtos_queue_items(obdh_queue_handle));
    TEST_ASSERT_EQUAL_UINT32(50, xTaskGetTickCount());
}

/**
 * @brief The block time covers queueing and the answer together (spec §5.2.1 rule 1).
 * @details The queue is full. After 60 ticks a slow OBDH serves one request of
 * another client, so EPS's request gets in; then OBDH answers nothing more.
 * EPS must give up 100 ticks after it started, not 60 + 100.
 */
void test_one_deadline_for_queueing_and_answer(void)
{
    obdh_stuck = true;
    current_client = CLIENT_COMMS;
    for (uint32_t i = 0; i < OBDH_QUEUE_LEN; i++) {
        TEST_ASSERT_EQUAL_INT(HAL_TIMEOUT, read_ht(i, 0));
    }

    current_client = CLIENT_EPS;
    fake_rtos_set_block_hook(slow_obdh_once);
    TEST_ASSERT_EQUAL_INT(HAL_TIMEOUT, read_ht(0, 100));
    TEST_ASSERT_EQUAL_UINT32(100, xTaskGetTickCount());
}

/**
 * @brief An answer to an older request does not extend the deadline (spec §5.2.1 rule 3).
 * @details 60 ticks into the wait, the answer to EPS's previous request
 * arrives. EPS drops it and waits only the 40 ticks left.
 */
void test_stale_answer_does_not_extend_the_deadline(void)
{
    obdh_stuck = true;
    fake_rtos_set_block_hook(stale_answer_once);
    TEST_ASSERT_EQUAL_INT(HAL_TIMEOUT, read_ht(0, 100));
    TEST_ASSERT_EQUAL_UINT32(100, xTaskGetTickCount());
}

/* =========================================================================
 * 3. Data by copy, answers by number, one reply queue per client
 * ========================================================================= */

/**
 * @brief The request carries a copy of the data: a caller that gave up can reuse its buffer (spec §5.2.1 rule 2).
 * @details EPS's parameter write times out; EPS then overwrites its buffer.
 * When OBDH finally serves the request, it stores the original bytes.
 */
void test_data_travels_by_copy(void)
{
    obdh_stuck = true;
    uint8_t data[16];
    for (uint32_t i = 0; i < sizeof(data); i++) data[i] = (uint8_t)(i + 1u);
    TEST_ASSERT_EQUAL_INT(HAL_TIMEOUT, obdh_param_write_request(0x42, data, sizeof(data), 10));

    uint8_t original[16];
    memcpy(original, data, sizeof(data));
    memset(data, 0, sizeof(data));
    obdh_stuck = false;
    run_obdh();

    TEST_ASSERT_EQUAL_STRING("P", events);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(original, last_data, sizeof(original));
}

/**
 * @brief A late answer is dropped by the next request, whose own answer and data come back; a restarted task keeps its numbers (FLS-REQ-040).
 * @details EPS's read times out with OBDH stuck. EPS is then restarted: its
 * request numbers are in static memory, so the new task continues them. OBDH
 * works again and answers the old read first. The next read must ignore that
 * answer and return its own data.
 */
void test_late_answer_is_dropped_by_the_next_request(void)
{
    obdh_stuck = true;
    TEST_ASSERT_EQUAL_INT(HAL_TIMEOUT, read_ht(0, 100));
    TEST_ASSERT_EACH_EQUAL_HEX8(0xEE, got, sizeof(got));
    uint32_t first = slots[CLIENT_EPS].request.header.number;

    obdh_stuck = false;
    run_obdh();
    TEST_ASSERT_EQUAL_UINT32(1, fake_rtos_queue_items(obdh_answer_queues[CLIENT_EPS]));

    TEST_ASSERT_EQUAL_INT(HAL_OK, read_ht(40, 100));
    TEST_ASSERT_EQUAL_UINT32(first + 1u, slots[CLIENT_EPS].request.header.number);
    TEST_ASSERT_EQUAL_UINT32(45, got_seq);
    for (uint32_t i = 0; i < HT_TEST_LEN; i++) {
        TEST_ASSERT_EQUAL_HEX8(pattern(40, i), got[i]);
    }
}

/** @brief OBDH answers each request on its client's reply queue, the OBC's included, and on no other. */
void test_answer_goes_to_the_requesting_client(void)
{
    obdh_stuck = true;
    current_client = CLIENT_COMMS;
    TEST_ASSERT_EQUAL_INT(HAL_TIMEOUT, read_ht(8, 0));
    current_client = (int)TM_OBC_INDEX;
    TEST_ASSERT_EQUAL_INT(HAL_TIMEOUT, read_ht(16, 0));

    obdh_stuck = false;
    run_obdh();

    static obdh_answer reply;
    TEST_ASSERT_EQUAL_INT(pdTRUE, xQueueReceive(obdh_answer_queues[CLIENT_COMMS], &reply, 0));
    TEST_ASSERT_EQUAL_UINT32(slots[CLIENT_COMMS].request.header.number, reply.number);
    TEST_ASSERT_EQUAL_UINT32(13, reply.seq);
    TEST_ASSERT_EQUAL_HEX8(pattern(8, 0), reply.data[0]);
    TEST_ASSERT_EQUAL_INT(pdTRUE, xQueueReceive(obdh_answer_queues[TM_OBC_INDEX], &reply, 0));
    TEST_ASSERT_EQUAL_UINT32(slots[TM_OBC_INDEX].request.header.number, reply.number);
    TEST_ASSERT_EQUAL_UINT32(21, reply.seq);
    TEST_ASSERT_EQUAL_HEX8(pattern(16, 0), reply.data[0]);
    for (uint32_t i = 0; i < OBDH_CLIENTS; i++) {
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, fake_rtos_queue_items(obdh_answer_queues[i]), "an answer went to another client");
    }
}

/* =========================================================================
 * 4. OBDH pauses and restarts
 * ========================================================================= */

/**
 * @brief A paused OBDH sleeps until the resume instead of polling, so lower-priority tasks keep running (KI-5); a request sent during the pause is served after the resume, within its block time (spec §5.2.1 rule 7).
 * @details The pause reaches OBDH with EPS's request in the queue. OBDH must
 * go to sleep once, without taking the request. The OBC switches the clock
 * (30 ticks) and resumes it, and OBDH serves the request. Had the pause
 * outlasted the block time, EPS would get HAL_TIMEOUT and the request would be
 * served after the resume all the same, as in test_data_travels_by_copy().
 */
void test_paused_obdh_sleeps_until_the_resume(void)
{
    notified = N_TASK_PAUSE;
    while_obdh_sleeps = obc_resumes_after_30_ticks;
    TEST_ASSERT_EQUAL_INT(HAL_OK, read_ht(0, 100));
    TEST_ASSERT_EQUAL_INT(1, obdh_sleeps);
    TEST_ASSERT_EQUAL_UINT32(30, xTaskGetTickCount());
    TEST_ASSERT_EQUAL_STRING("h", events);
}

/** @brief Another notification wakes a paused OBDH, which goes back to sleep: only the resume lets it serve. */
void test_other_notification_puts_a_paused_obdh_back_to_sleep(void)
{
    notified = N_TASK_PAUSE;
    while_obdh_sleeps = clear_ht_then_resume;
    TEST_ASSERT_EQUAL_INT(HAL_OK, read_ht(0, 100));
    TEST_ASSERT_EQUAL_INT(2, obdh_sleeps);
    TEST_ASSERT_EQUAL_STRING("h", events);
}

/**
 * @brief When OBDH starts it forgets deferred notifications, resets the flash interface and reloads the stores before serving; requests still queued are then served (spec §5.2.1 rule 5, FLS-REQ-070).
 */
void test_restarted_obdh_resets_and_reloads_before_serving(void)
{
    obdh_stuck = true;
    TEST_ASSERT_EQUAL_INT(HAL_TIMEOUT, read_ht(0, 0));

    deferred_notifications = N_TASK_RESUME;   /* left by the deleted task */
    setup_obdh();
    TEST_ASSERT_EQUAL_UINT32(0, deferred_notifications);
    obdh_stuck = false;
    run_obdh();
    TEST_ASSERT_EQUAL_STRING("RLHh", events);
}

/**
 * @brief The request a deleted OBDH was serving gets no answer: its caller gets HAL_TIMEOUT within its block time, and the next request works.
 */
void test_request_lost_with_obdh_times_out(void)
{
    fake_rtos_set_block_hook(obdh_dies_while_serving);
    TEST_ASSERT_EQUAL_INT(HAL_TIMEOUT, read_ht(0, 100));
    TEST_ASSERT_EQUAL_UINT32(100, xTaskGetTickCount());
    TEST_ASSERT_EQUAL_UINT32(0, fake_rtos_queue_items(obdh_queue_handle));

    setup_obdh();
    fake_rtos_set_block_hook(run_obdh);
    TEST_ASSERT_EQUAL_INT(HAL_OK, read_ht(4, 100));
    TEST_ASSERT_EQUAL_HEX8(pattern(4, 0), got[0]);
}

/* =========================================================================
 * 5. Posts
 * ========================================================================= */

/** @brief Fills the beacon of test number n. */
static void make_beacon(uint8_t beacon[HT_BEACON_SIZE], uint32_t n)
{
    for (uint32_t i = 0; i < HT_BEACON_SIZE; i++) beacon[i] = (uint8_t)(n * 16u + i + 1u);
}

/** @brief A post reaches the HT store with the beacon's bytes, and OBDH answers nothing (spec §5.2.1 rule 4). */
void test_a_post_is_served_and_never_answered(void)
{
    uint8_t beacon[HT_BEACON_SIZE];
    make_beacon(beacon, 1);
    current_client = CLIENT_BEACON;
    TEST_ASSERT_EQUAL_INT(HAL_OK, obdh_ht_post(beacon));
    TEST_ASSERT_EQUAL_UINT32(1, fake_rtos_queue_items(obdh_queue_handle));

    run_obdh();
    TEST_ASSERT_EQUAL_STRING("B", events);
    TEST_ASSERT_EQUAL_UINT32(HT_BEACON_SIZE, last_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(beacon, last_data, HT_BEACON_SIZE);
    for (uint32_t i = 0; i < OBDH_CLIENTS; i++) {
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, fake_rtos_queue_items(obdh_answer_queues[i]), "OBDH answered a post");
    }
    TEST_ASSERT_EQUAL_UINT32(0, xTaskGetTickCount());
}

/**
 * @brief Posts never take the entries the clients need (V14, FLS-REQ-050).
 * @details OBDH is stuck. Posts fill only the entries beyond one per client; the
 * next post is refused at once, without waiting; then every client, the OBC
 * included, can still queue a request.
 */
void test_posts_leave_one_entry_per_client(void)
{
    obdh_stuck = true;
    uint8_t beacon[HT_BEACON_SIZE];
    current_client = CLIENT_BEACON;
    for (uint32_t n = 0; n < OBDH_QUEUE_LEN - OBDH_CLIENTS; n++) {
        make_beacon(beacon, n);
        TEST_ASSERT_EQUAL_INT_MESSAGE(HAL_OK, obdh_ht_post(beacon), "a post was refused while posts had room");
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(HAL_BUSY, obdh_ht_post(beacon), "a post took an entry kept for the clients");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, xTaskGetTickCount(), "a post waited");
    TEST_ASSERT_EQUAL_UINT32(OBDH_QUEUE_LEN - OBDH_CLIENTS, fake_rtos_queue_items(obdh_queue_handle));

    for (int client = 0; client < (int)OBDH_CLIENTS; client++) {
        current_client = client;
        /* HAL_TIMEOUT: sent, and OBDH is stuck; HAL_BUSY would mean no room */
        TEST_ASSERT_EQUAL_INT_MESSAGE(HAL_TIMEOUT, read_ht(0, 0), "a client found no room for its request");
    }
    TEST_ASSERT_EQUAL_UINT32(OBDH_QUEUE_LEN, fake_rtos_queue_items(obdh_queue_handle));
}

/** @brief Requests in the queue count too: once only one entry per client is free, a post is refused even with no post queued. */
void test_a_post_needs_more_free_entries_than_clients(void)
{
    obdh_stuck = true;
    for (uint32_t n = 0; n < OBDH_QUEUE_LEN - OBDH_CLIENTS; n++) {
        TEST_ASSERT_EQUAL_INT(HAL_TIMEOUT, read_ht(n, 0));
    }

    uint8_t beacon[HT_BEACON_SIZE];
    make_beacon(beacon, 7);
    current_client = CLIENT_BEACON;
    TEST_ASSERT_EQUAL_INT(HAL_BUSY, obdh_ht_post(beacon));
    TEST_ASSERT_EQUAL_UINT32(OBDH_QUEUE_LEN - OBDH_CLIENTS, fake_rtos_queue_items(obdh_queue_handle));
}

/**
 * @brief Test runner. Lists every test of this suite, in execution order.
 */
int main(int argc, char **argv)
{
    if (pocat_parse_args(argc, argv) != 0) return 1;
    POCAT_BEGIN();

    // 1. A request and its answer
    POCAT_RUN_TEST(test_queues_are_created_for_every_client);
    POCAT_RUN_TEST(test_an_ht_read_returns_the_block_its_length_and_number);
    POCAT_RUN_TEST(test_every_operation_reaches_its_function);
    POCAT_RUN_TEST(test_a_failed_or_refused_ht_read_writes_nothing);
    POCAT_RUN_TEST(test_a_read_request_carries_no_data);
    POCAT_RUN_TEST(test_a_block_longer_than_the_buffer_is_refused);
    POCAT_RUN_TEST(test_bad_requests_are_refused_without_sending);

    // 2. Block time
    POCAT_RUN_TEST(test_stuck_obdh_gives_timeout_after_the_block_time);
    POCAT_RUN_TEST(test_full_queue_gives_busy_and_sends_nothing);
    POCAT_RUN_TEST(test_one_deadline_for_queueing_and_answer);
    POCAT_RUN_TEST(test_stale_answer_does_not_extend_the_deadline);

    // 3. Data by copy, answers by number
    POCAT_RUN_TEST(test_data_travels_by_copy);
    POCAT_RUN_TEST(test_late_answer_is_dropped_by_the_next_request);
    POCAT_RUN_TEST(test_answer_goes_to_the_requesting_client);

    // 4. Pauses and restarts
    POCAT_RUN_TEST(test_paused_obdh_sleeps_until_the_resume);
    POCAT_RUN_TEST(test_other_notification_puts_a_paused_obdh_back_to_sleep);
    POCAT_RUN_TEST(test_restarted_obdh_resets_and_reloads_before_serving);
    POCAT_RUN_TEST(test_request_lost_with_obdh_times_out);

    // 5. Posts
    POCAT_RUN_TEST(test_a_post_is_served_and_never_answered);
    POCAT_RUN_TEST(test_posts_leave_one_entry_per_client);
    POCAT_RUN_TEST(test_a_post_needs_more_free_entries_than_clients);

    return POCAT_END();
}
