/**
 * @file test_ht_store.c
 * @brief Host tests for the telemetry-history store: the 30 s credit, the POCKET+ blocks on the ring, restarts, resets and dropped posts.
 * @date 2026-11-21
 *
 * @details
 * **What is under test**
 * - src/subsystems/obdh/ht_store.c: ht_store_add() (the credit and the
 *   blocks), ht_store_load(), ht_store_read(), ht_beacon_post(),
 *   ht_read_request() and ht_store_stats().
 * - Underneath, the real ring.c, crc32.c, compression.c (POCKET+) and uptime.c.
 *
 * **How it is built** (see test/CMakeLists.txt)
 * - ht_store.c is `#include`d below, so boot() can put its static state back
 *   to the values the program starts with, as a reset does.
 * - `mocks/fake_flash_driver.c` replaces the flash driver. The fake RTOS gives
 *   the tick count, so uptime moves with fake_rtos_set_tick() and
 *   fake_rtos_advance_ticks().
 * - obdh_ht_post() and obdh_ht_read_request() are faked below: they run the
 *   OBDH side at once, as the OBDH task. The queue itself (posts never
 *   waiting, V14) is tested in test_obdh_requests.
 * - The store accepts only the OBDH task (spec FLS-REQ-110).
 *   tm_get_task_handle() is faked below to name an OBDH task, created at each
 *   boot(), and the tests run as that task unless they say otherwise.
 *
 * **Reference model.** reference_blocks() runs compression.c directly on a
 * list of beacons with the rule of ht_store.h: the first beacon of a block
 * raw, each later one compressed and appended while it fits 112 bytes. The
 * store must write exactly those blocks, byte for byte and in order. The model
 * uses the same global POCKET+ state as the store, so it runs before the store
 * gets the beacons.
 *
 * **How to run**
 * @code
 * docker run --rm -v "${PWD}:/app" pocat-tests test_ht_store
 * docker run --rm -v "${PWD}:/app" pocat-tests test_ht_store -f credit
 * @endcode
 */
#include "unity.h"
#include "fake_rtos.h"
#include "fake_flash_driver.h"
#include "pocat_test_main.h"

#include "ht_store.c"

#include <stdio.h>
#include <string.h>

#define MAX_BEACONS  400u
#define MAX_BLOCKS   128u

/** @brief A block as read back, or as the reference model makes it. */
typedef struct {
    uint8_t  data[HT_BLOCK_MAX];
    uint32_t len;
    uint32_t seq;
} block_t;

typedef enum { FLIGHT_LIKE, STEADY, NOISE, IDENTICAL } beacon_kind_t;

/* ---- Fake of the task manager: the store asks it only for the OBDH task, to check its caller ---- */

static TaskHandle_t obdh_task_handle;     /**< The OBDH task, created at each boot(): what tm_get_task_handle(TM_TASK_OBDH) returns. */
static TaskHandle_t beacon_task_handle;   /**< The beacon task, created at each boot(); the store must refuse its calls. */

TaskHandle_t tm_get_task_handle(uint32_t task_bit)
{
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(TM_TASK_OBDH, task_bit, "only the OBDH task is asked for");
    return obdh_task_handle;
}

static void task_stub(void *p) { (void)p; }

/* ---- Fakes of the OBDH queue: the OBDH task serves at once ---- */

static bool queue_full;   /**< The fake obdh_ht_post() finds no room in the queue. */

HAL_StatusTypeDef obdh_ht_post(const uint8_t beacon[HT_BEACON_SIZE])
{
    if (queue_full) return HAL_BUSY;
    TaskHandle_t caller = fake_rtos_current_task();
    fake_rtos_set_current_task(obdh_task_handle);
    TEST_ASSERT_EQUAL_INT(HAL_OK, ht_store_add(beacon));
    fake_rtos_set_current_task(caller);
    return HAL_OK;
}

HAL_StatusTypeDef obdh_ht_read_request(uint32_t from_seq, uint8_t block[HT_BLOCK_MAX], uint32_t *len, uint32_t *seq, TickType_t ticks_to_wait)
{
    (void)ticks_to_wait;
    TaskHandle_t caller = fake_rtos_current_task();
    fake_rtos_set_current_task(obdh_task_handle);
    HAL_StatusTypeDef status = ht_store_read(from_seq, block, len, seq);
    fake_rtos_set_current_task(caller);
    return status;
}

/* ---- Boot ---- */

/**
 * @brief A reset: the store's RAM back to the values the program starts with, uptime 0, the power back, then OBDH starts and loads the store. Flash is kept.
 * @details The first call, from the first test's setUp(), takes those values
 * before anything changes them. A wrong start value in ht_store.c (a credit of
 * 30, say) therefore shows up here as it would after a real reset.
 */
static void boot(void)
{
    uint32_t *const vars[] = { &open_len, &credit_s, &credit_at_s, &posts_dropped, &beacons_skipped, &samples };
    static uint32_t vars_at_start[sizeof(vars) / sizeof(vars[0])];
    static ring_t ring_at_start;
    static bool taken;

    if (!taken) {
        for (size_t i = 0; i < sizeof(vars) / sizeof(vars[0]); i++) vars_at_start[i] = *vars[i];
        ring_at_start = ring;
        taken = true;
    }
    for (size_t i = 0; i < sizeof(vars) / sizeof(vars[0]); i++) *vars[i] = vars_at_start[i];
    ring = ring_at_start;

    fake_rtos_reset();
    TEST_ASSERT_EQUAL_INT(pdPASS, xTaskCreate(task_stub, "OBDH", 512, NULL, 5, &obdh_task_handle));
    TEST_ASSERT_EQUAL_INT(pdPASS, xTaskCreate(task_stub, "BEACON", 512, NULL, 4, &beacon_task_handle));
    fake_rtos_set_current_task(obdh_task_handle);
    fake_flashdrv_power_on();
    TEST_ASSERT_EQUAL_INT(HAL_OK, ht_store_load());
}

void setUp(void)
{
    fake_flashdrv_reset();
    queue_full = false;
    boot();
}

void tearDown(void)
{
    TEST_ASSERT_EQUAL_INT(0, fake_rtos_critical_depth());
}

/* ---- Beacons ---- */

static uint32_t rng_state;

static uint32_t rnd(void)
{
    rng_state = rng_state * 1103515245u + 12345u;
    return rng_state >> 8;
}

static void put_be32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (24 - 8 * i));
}

static void put_le32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}

/**
 * @brief Beacon number k, laid out as beacon.c builds it: version, Unix time, mode, health, temperatures, voltage, last telecommand, configuration, uptime.
 * @details FLIGHT_LIKE: time and uptime advance 30 s per beacon, the
 * temperature drifts with the orbit and has noise, the last telecommand
 * changes now and then. STEADY: only time and uptime advance, 30 s per
 * beacon. NOISE: every byte random, which POCKET+ expands. IDENTICAL: the same
 * beacon every time.
 */
static void make_beacon(uint8_t b[HT_BEACON_SIZE], beacon_kind_t kind, uint32_t k)
{
    if (kind == NOISE) {
        for (uint32_t i = 0; i < HT_BEACON_SIZE; i++) b[i] = (uint8_t)rnd();
        return;
    }
    uint32_t t = (kind == IDENTICAL) ? 0u : 30u * k;
    memset(b, 0, HT_BEACON_SIZE);
    b[0] = 0x01u;
    put_be32(&b[1], 1795000000u + t);
    b[5] = 2u;
    b[6] = 0x3Fu;
    b[7] = (uint8_t)(20u + ((kind == FLIGHT_LIKE) ? (t / 600u) % 8u + rnd() % 2u : 0u));
    b[9] = 33u;
    b[12] = (uint8_t)((kind == FLIGHT_LIKE) ? k / 40u : 0xFFu);
    b[14] = 1u;
    put_be32(&b[15], 1000u + t);
}

static void make_beacons(uint8_t (*b)[HT_BEACON_SIZE], uint32_t n, beacon_kind_t kind, uint32_t seed)
{
    rng_state = seed;
    for (uint32_t k = 0; k < n; k++) make_beacon(b[k], kind, k);
}

/* ---- Reference model, posting and reading ---- */

/**
 * @brief The closed blocks the store must write for beacons[0..n-1], all allowed by the credit, made with compression.c directly.
 * @details Runs the global POCKET+ state: call it before the store gets the
 * beacons. The last block stays open in the store and is not returned.
 * @return Number of closed blocks.
 */
static uint32_t reference_blocks(const uint8_t (*beacons)[HT_BEACON_SIZE], uint32_t n, block_t *blocks)
{
    uint32_t closed = 0;
    block_t open = { .len = 0u };

    for (uint32_t k = 0; k < n; k++) {
        uint8_t packet[HT_PACKET_SIZE] = { 0 };
        memcpy(packet, beacons[k], HT_BEACON_SIZE);
        uint32_t words[HT_PACKET_SIZE / 4u];
        for (uint32_t w = 0; w < HT_PACKET_SIZE / 4u; w++) {
            words[w] = ((uint32_t)packet[4u * w] << 24) | ((uint32_t)packet[4u * w + 1u] << 16) |
                       ((uint32_t)packet[4u * w + 2u] << 8) | (uint32_t)packet[4u * w + 3u];
        }

        if (open.len > 0u) {
            uint8_t compressed[HT_PACKET_SIZE * 4u];
            int_fast16_t c = compressPacket(compressed, words);
            if ((c > 0) && (open.len + (uint32_t)c <= HT_BLOCK_MAX)) {
                memcpy(&open.data[open.len], compressed, (size_t)c);
                open.len += (uint32_t)c;
                continue;
            }
            TEST_ASSERT_LESS_THAN_UINT32(MAX_BLOCKS, closed);
            blocks[closed++] = open;
        }
        memcpy(open.data, packet, HT_PACKET_SIZE);
        open.len = HT_PACKET_SIZE;
        TEST_ASSERT_EQUAL_UINT32(HT_PACKET_SIZE, startPocket(words, HT_PACKET_SIZE, POCKET_PLUS_UPDATE_RATE, 1, 0, 0));
    }
    return closed;
}

/** @brief Posts beacons[0..n-1], one every 30 s from now, which the credit always allows; each must be added. */
static void post_every_30_s(const uint8_t (*beacons)[HT_BEACON_SIZE], uint32_t n)
{
    uint32_t before = ht_store_stats().samples;
    for (uint32_t k = 0; k < n; k++) {
        fake_rtos_advance_ticks(30000u);
        ht_beacon_post(beacons[k]);
    }
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(before + n, ht_store_stats().samples, "a beacon 30 s after the last one was not added");
}

/** @brief Reads every stored block numbered from_seq or later, in order, as the downlink will. */
static uint32_t read_blocks(uint32_t from_seq, block_t *blocks)
{
    uint32_t n = 0;
    block_t b;
    while (ht_read_request(from_seq, b.data, &b.len, &b.seq, OBDH_REQUEST_WAIT) == HAL_OK) {
        TEST_ASSERT_LESS_THAN_UINT32(MAX_BLOCKS, n);
        blocks[n++] = b;
        from_seq = b.seq + 1u;
    }
    return n;
}

/** @brief Checks that got[0..n-1] are the expected blocks, numbered from first_seq on with no gap. */
static void assert_blocks(const block_t *expected, uint32_t n, const block_t *got, uint32_t first_seq)
{
    for (uint32_t b = 0; b < n; b++) {
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(first_seq + b, got[b].seq, "blocks out of order, or a number skipped");
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(expected[b].len, got[b].len, "a block of the wrong length");
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(expected[b].data, got[b].data, expected[b].len, "a block differs from POCKET+'s output");
    }
}

/** @brief Posts one beacon at uptime ms and returns true if the store added it. */
static bool post_at_ms(uint32_t ms, uint32_t k)
{
    uint8_t beacon[HT_BEACON_SIZE];
    make_beacon(beacon, FLIGHT_LIKE, k);
    fake_rtos_set_tick(ms);
    uint32_t before = ht_store_stats().samples;
    ht_beacon_post(beacon);
    return ht_store_stats().samples > before;
}

/* =========================================================================
 * 1. The credit (FLS-RATE-020, O21)
 * ========================================================================= */

/** @brief The credit is 0 at boot: no beacon becomes a sample before 30 s of uptime, and the one at 30 s does (O21). */
void test_credit_starts_at_zero_at_boot(void)
{
    for (uint32_t s = 0; s < HT_SAMPLE_PERIOD_S; s++) {
        TEST_ASSERT_FALSE_MESSAGE(post_at_ms(s * 1000u, s), "a beacon was stored before 30 s of uptime");
    }
    TEST_ASSERT_TRUE(post_at_ms(HT_SAMPLE_PERIOD_S * 1000u, 30));
    TEST_ASSERT_EQUAL_UINT32(30, ht_store_stats().beacons_skipped);
    TEST_ASSERT_EQUAL_UINT32(1, ht_store_stats().samples);
}

/**
 * @brief Beacons of a 30 s timer are all stored, however late each one arrives.
 * @details The timer starts 0.7 s after boot and never fires early; each
 * beacon reaches OBDH 0 to 25 s after its time (the timer task, the beacon
 * task, a busy or paused OBDH). With 1 ms resolution, delays under 0.3 s put
 * the credit at exactly 30.
 */
void test_credit_stores_every_beacon_of_a_30_s_timer_however_late(void)
{
    rng_state = 777u;
    for (uint32_t k = 1; k <= 1000u; k++) {
        uint32_t delay_ms = rnd() % 25000u;
        TEST_ASSERT_TRUE_MESSAGE(post_at_ms(700u + 30000u * k + delay_ms, k), "a beacon of the 30 s timer was not stored");
    }
    TEST_ASSERT_EQUAL_UINT32(0, ht_store_stats().beacons_skipped);
}

/** @brief Beacons every 10 s: exactly every third one is stored, the ones at 30 s, 60 s, ... */
void test_credit_stores_one_beacon_in_three_at_10_s(void)
{
    for (uint32_t k = 1; k <= 300u; k++) {
        TEST_ASSERT_EQUAL_MESSAGE((k % 3u) == 0u, post_at_ms(10000u * k, k), "the wrong beacons were stored");
    }
    TEST_ASSERT_EQUAL_UINT32(100, ht_store_stats().samples);
}

/**
 * @brief In any T seconds at most 2 + T/30 samples are stored, resets included (FLS-RATE-020).
 * @details 6,000 beacons with random gaps: bursts of 0 to 2 s, ordinary gaps of
 * 5 to 44 s, and long ones of up to 4 minutes; one gap in a hundred ends in a
 * reset. Times are whole seconds, counted across resets. Every pair of stored
 * samples i < j must satisfy j - i + 1 <= 2 + (t_j - t_i) / 30.
 */
void test_credit_bounds_every_window_across_resets(void)
{
    static uint32_t stored_at[6000];
    uint32_t n = 0, now = 0, resets = 0;
    rng_state = 4242u;

    for (uint32_t k = 0; k < 6000u; k++) {
        uint32_t r = rnd() % 100u;
        uint32_t gap = (r < 40u) ? rnd() % 3u : (r < 90u) ? 5u + rnd() % 40u : 50u + rnd() % 200u;
        if (r == 99u) {
            boot();
            resets++;
        }
        now += gap;
        if (post_at_ms(xTaskGetTickCount() + gap * 1000u, k)) {
            stored_at[n++] = now;
        }
    }
    TEST_ASSERT_GREATER_THAN_UINT32(1000, n);
    TEST_ASSERT_GREATER_THAN_UINT32(20, resets);

    for (uint32_t i = 0; i < n; i++) {
        for (uint32_t j = i + 1u; j < n; j++) {
            if ((j - i - 1u) * HT_SAMPLE_PERIOD_S > stored_at[j] - stored_at[i]) {
                char msg[96];
                snprintf(msg, sizeof(msg), "%u samples within %u s", (unsigned)(j - i + 1u), (unsigned)(stored_at[j] - stored_at[i]));
                TEST_FAIL_MESSAGE(msg);
            }
        }
    }
}

/** @brief A boot loop shorter than 30 s stores nothing, even with a 10 s beacon (O21): at a credit of 30 at boot it would store one sample per boot. */
void test_credit_a_boot_loop_under_30_s_stores_nothing(void)
{
    for (uint32_t b = 0; b < 100u; b++) {
        boot();
        TEST_ASSERT_FALSE(post_at_ms(10000u, 1));
        TEST_ASSERT_FALSE(post_at_ms(20000u, 2));
    }
}

/**
 * @brief An OBDH restart keeps the credit: it counts time since boot, not since the task started.
 * @details Stored at 30 s (credit 0), skipped at 50 s (credit 20), OBDH
 * restarts at 55 s, and the beacon at 60 s has 30 s of credit. A restart that
 * cleared the credit, or restarted its clock, would skip it.
 */
void test_credit_is_kept_when_obdh_restarts(void)
{
    TEST_ASSERT_TRUE(post_at_ms(30000u, 1));
    TEST_ASSERT_FALSE(post_at_ms(50000u, 2));
    fake_rtos_set_tick(55000u);
    TEST_ASSERT_EQUAL_INT(HAL_OK, ht_store_load());
    TEST_ASSERT_TRUE_MESSAGE(post_at_ms(60000u, 3), "the restart lost the credit");
}

/* =========================================================================
 * 2. Blocks
 * ========================================================================= */

/**
 * @brief Posts n beacons of a kind 30 s apart; the blocks read back must be the reference blocks, numbered 0, 1, ...
 * @return The number of blocks.
 */
static uint32_t check_blocks_of(beacon_kind_t kind, uint32_t n)
{
    static uint8_t beacons[MAX_BEACONS][HT_BEACON_SIZE];
    static block_t expected[MAX_BLOCKS], got[MAX_BLOCKS];
    make_beacons(beacons, n, kind, 99u);
    uint32_t n_expected = reference_blocks(beacons, n, expected);

    post_every_30_s(beacons, n);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(n_expected, read_blocks(0, got), "wrong number of blocks stored");
    assert_blocks(expected, n_expected, got, 0);

    TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(beacons[0], got[0].data, HT_BEACON_SIZE, "the first beacon of a block is not raw");
    TEST_ASSERT_EQUAL_HEX8(0x00, got[0].data[HT_BEACON_SIZE]);
    return n_expected;
}

/** @brief Flight-like beacons: the first raw, the others compressed, a new block when the next does not fit. */
void test_blocks_of_flight_like_beacons(void)
{
    TEST_ASSERT_GREATER_THAN_UINT32(10, check_blocks_of(FLIGHT_LIKE, 300));
}

/**
 * @brief Steady beacons, where only the times change: a block holds more than two update periods (2 x 12 samples).
 * @details POCKET+ drops from its mask, every 12 samples, the bits that stopped
 * changing. The first time nothing is dropped, since the mask is still every
 * bit that changed since the block began, so only a block of 25 samples or more
 * shows the update rate. Flight-like blocks hold about 22. The ground decoder
 * must use the same rate.
 */
void test_blocks_of_steady_beacons(void)
{
    TEST_ASSERT_LESS_THAN_UINT32(300u / (2u * POCKET_PLUS_UPDATE_RATE + 1u), check_blocks_of(STEADY, 300));
}

/** @brief Noise, which POCKET+ expands: still appended while it fits, so blocks hold few beacons (O11). */
void test_blocks_of_incompressible_beacons(void)
{
    TEST_ASSERT_GREATER_THAN_UINT32(30, check_blocks_of(NOISE, 100));
}

/** @brief Identical beacons compress to almost nothing, so a block holds many: the length check still closes it at 112 bytes. */
void test_blocks_of_identical_beacons(void)
{
    TEST_ASSERT_GREATER_THAN_UINT32(1, check_blocks_of(IDENTICAL, 300));
}

/** @brief The block being filled is not stored yet: no read returns it until it closes. */
void test_the_open_block_is_not_readable(void)
{
    static uint8_t beacons[MAX_BEACONS][HT_BEACON_SIZE];
    make_beacons(beacons, 100, FLIGHT_LIKE, 5u);
    uint8_t data[HT_BLOCK_MAX];
    uint32_t len = 0, seq = 0;

    post_every_30_s(beacons, 3);
    TEST_ASSERT_EQUAL_INT(HAL_ERROR, ht_read_request(0, data, &len, &seq, OBDH_REQUEST_WAIT));

    uint32_t k = 3;
    while (ht_store_stats().ring.appended == 0u) {
        TEST_ASSERT_LESS_THAN_UINT32(100, k);
        post_every_30_s(&beacons[k++], 1);
    }
    TEST_ASSERT_EQUAL_INT(HAL_OK, ht_read_request(0, data, &len, &seq, OBDH_REQUEST_WAIT));
    TEST_ASSERT_EQUAL_UINT32(0, seq);
}

/* =========================================================================
 * 3. Restarts, resets and old data
 * ========================================================================= */

/**
 * @brief Beacons, then a restart (or a reset), then more beacons: the blocks stored before are kept, the open block is lost, and numbering goes on.
 * @param reset true for a reset (RAM and uptime back to boot), false for an OBDH restart.
 */
static void check_blocks_across(bool reset)
{
    static uint8_t a[MAX_BEACONS][HT_BEACON_SIZE], b[MAX_BEACONS][HT_BEACON_SIZE];
    static block_t expected_a[MAX_BLOCKS], expected_b[MAX_BLOCKS], got[MAX_BLOCKS];
    make_beacons(a, 100, FLIGHT_LIKE, 1u);
    make_beacons(b, 100, FLIGHT_LIKE, 2u);

    uint32_t na = reference_blocks(a, 100, expected_a);
    post_every_30_s(a, 100);
    TEST_ASSERT_TRUE_MESSAGE(open_len > 0u, "the test needs an open block before the restart");

    if (reset) {
        boot();
    } else {
        TEST_ASSERT_EQUAL_INT(HAL_OK, ht_store_load());
    }
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, open_len, "the open block outlived the restart");

    uint32_t nb = reference_blocks(b, 100, expected_b);
    post_every_30_s(b, 100);

    TEST_ASSERT_EQUAL_UINT32(na + nb, read_blocks(0, got));
    assert_blocks(expected_a, na, got, 0);
    assert_blocks(expected_b, nb, &got[na], na);
}

/** @brief An OBDH restart drops the open block (spec §5.2.1 rule 5) and keeps the stored ones; numbers go on. */
void test_an_obdh_restart_drops_only_the_open_block(void)
{
    check_blocks_across(false);
}

/** @brief A reset keeps every stored block; the next block takes the next number. */
void test_a_reset_keeps_the_stored_blocks(void)
{
    check_blocks_across(true);
}

/**
 * @brief Slots of the format before the ring (V3 of comms/ht_handling.c) count as blank: nothing is read from them, and the ring starts at slot 0 with number 0.
 * @details A V3 slot starts with the epoch, so the ring finds a length of about
 * 27,000 where its header has one.
 */
void test_slots_of_the_old_format_count_as_blank(void)
{
    uint8_t v3[HT_SLOT_SIZE];
    memset(v3, 0xFF, sizeof(v3));
    put_le32(&v3[0], 1795000000u);   /* epoch */
    put_le32(&v3[4], 7u);            /* sequence number */
    v3[8] = 60u;                     /* data length */
    v3[9] = 0u;
    v3[10] = 5u;                     /* beacon count */
    v3[11] = 0x03u;                  /* format V3 */
    memset(&v3[12], 0x5A, 60);
    memcpy(fake_flashdrv_bytes(FLASH_REGION_HT, 0), v3, sizeof(v3));
    memcpy(fake_flashdrv_bytes(FLASH_REGION_HT, HT_SLOT_SIZE), v3, sizeof(v3));
    boot();

    uint8_t data[HT_BLOCK_MAX];
    uint32_t len = 0, seq = 0;
    TEST_ASSERT_EQUAL_INT(HAL_ERROR, ht_read_request(0, data, &len, &seq, OBDH_REQUEST_WAIT));
    TEST_ASSERT_EQUAL_UINT32(0, ring.write);
    TEST_ASSERT_EQUAL_UINT32(0, ring.next_seq);
}

/* =========================================================================
 * 4. Posts
 * ========================================================================= */

/** @brief A beacon that finds no room in the OBDH queue is dropped and counted (FLS-REQ-050); the next one goes in. */
void test_a_beacon_that_finds_no_room_is_counted(void)
{
    uint8_t beacon[HT_BEACON_SIZE];
    make_beacon(beacon, FLIGHT_LIKE, 1);
    fake_rtos_set_tick(30000u);

    queue_full = true;
    ht_beacon_post(beacon);
    TEST_ASSERT_EQUAL_UINT32(1, ht_store_stats().posts_dropped);
    TEST_ASSERT_EQUAL_UINT32(0, ht_store_stats().samples);

    queue_full = false;
    ht_beacon_post(beacon);
    TEST_ASSERT_EQUAL_UINT32(1, ht_store_stats().posts_dropped);
    TEST_ASSERT_EQUAL_UINT32(1, ht_store_stats().samples);
}

/* =========================================================================
 * 5. Callers (FLS-REQ-110)
 * ========================================================================= */

/**
 * @brief Called from the beacon task in the middle of a block, the load, the add and the read return HAL_ERROR before they read, program or change anything.
 * @details The credit stays as it was (a beacon posted at the same instant is
 *          still added), and so do the counters; the read leaves the caller's
 *          buffer alone, and the block being filled is kept: the blocks stored
 *          afterwards are exactly the reference blocks. A load that ran would
 *          have dropped the open block, an add would have put the beacon in
 *          twice.
 */
void test_a_task_other_than_obdh_is_refused(void)
{
    static uint8_t beacons[MAX_BEACONS][HT_BEACON_SIZE];
    static block_t expected[MAX_BLOCKS], got[MAX_BLOCKS];
    make_beacons(beacons, 100, FLIGHT_LIKE, 3u);
    uint32_t n_expected = reference_blocks(beacons, 100, expected);

    post_every_30_s(beacons, 50);
    TEST_ASSERT_TRUE_MESSAGE((open_len > 0u) && (ht_store_stats().ring.appended > 0u), "the test needs stored blocks and an open one");
    ht_store_stats_t before = ht_store_stats();
    uint32_t reads = fake_flashdrv_reads();
    uint32_t programs = fake_flashdrv_programs();

    fake_rtos_set_current_task(beacon_task_handle);
    fake_rtos_advance_ticks(30000u);   /* credit for one sample, which the refused add must not take */
    uint8_t block[HT_BLOCK_MAX];
    memset(block, 0xA5, sizeof(block));
    uint32_t len = 77u, seq = 77u;
    TEST_ASSERT_EQUAL_INT(HAL_ERROR, ht_store_load());
    TEST_ASSERT_EQUAL_INT(HAL_ERROR, ht_store_add(beacons[50]));
    TEST_ASSERT_EQUAL_INT(HAL_ERROR, ht_store_read(0, block, &len, &seq));

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(reads, fake_flashdrv_reads(), "a refused call read flash");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(programs, fake_flashdrv_programs(), "a refused call programmed flash");
    TEST_ASSERT_EACH_EQUAL_HEX8_MESSAGE(0xA5, block, sizeof(block), "a refused read wrote the caller's buffer");
    TEST_ASSERT_EQUAL_UINT32(77u, len);
    TEST_ASSERT_EQUAL_UINT32(77u, seq);
    TEST_ASSERT_EQUAL_UINT32(before.samples, ht_store_stats().samples);
    TEST_ASSERT_EQUAL_UINT32(before.beacons_skipped, ht_store_stats().beacons_skipped);

    /* From the beacon task: OBDH serves the posts. The first comes at the same instant, on the credit the refused add left */
    ht_beacon_post(beacons[50]);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(before.samples + 1u, ht_store_stats().samples, "the refused add took the credit");
    post_every_30_s(&beacons[51], 49);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(n_expected, read_blocks(0, got), "wrong number of blocks stored");
    assert_blocks(expected, n_expected, got, 0);
}

/**
 * @brief Test runner. Lists every test of this suite, in execution order.
 */
int main(int argc, char **argv)
{
    if (pocat_parse_args(argc, argv) != 0) return 1;
    POCAT_BEGIN();

    // 1. The credit
    POCAT_RUN_TEST(test_credit_starts_at_zero_at_boot);
    POCAT_RUN_TEST(test_credit_stores_every_beacon_of_a_30_s_timer_however_late);
    POCAT_RUN_TEST(test_credit_stores_one_beacon_in_three_at_10_s);
    POCAT_RUN_TEST(test_credit_bounds_every_window_across_resets);
    POCAT_RUN_TEST(test_credit_a_boot_loop_under_30_s_stores_nothing);
    POCAT_RUN_TEST(test_credit_is_kept_when_obdh_restarts);

    // 2. Blocks
    POCAT_RUN_TEST(test_blocks_of_flight_like_beacons);
    POCAT_RUN_TEST(test_blocks_of_steady_beacons);
    POCAT_RUN_TEST(test_blocks_of_incompressible_beacons);
    POCAT_RUN_TEST(test_blocks_of_identical_beacons);
    POCAT_RUN_TEST(test_the_open_block_is_not_readable);

    // 3. Restarts, resets and old data
    POCAT_RUN_TEST(test_an_obdh_restart_drops_only_the_open_block);
    POCAT_RUN_TEST(test_a_reset_keeps_the_stored_blocks);
    POCAT_RUN_TEST(test_slots_of_the_old_format_count_as_blank);

    // 4. Posts
    POCAT_RUN_TEST(test_a_beacon_that_finds_no_room_is_counted);

    // 5. Callers
    POCAT_RUN_TEST(test_a_task_other_than_obdh_is_refused);

    return POCAT_END();
}
