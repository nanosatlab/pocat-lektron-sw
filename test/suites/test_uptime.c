/**
 * @file test_uptime.c
 * @brief Host tests for the uptime: seconds since boot from the kernel tick counter, across its wrap.
 * @date 2026-11-21
 *
 * @details
 * **What is under test**
 * - src/utils/uptime.c: `uptime_get_s()`.
 *
 * The beacon reports this uptime (UPTIME_S). It used to be "now minus the
 * boot time stored in flash", which cost one page erase at every boot (F3)
 * and jumped whenever the clock was set.
 *
 * **How it is built** (see test/CMakeLists.txt)
 * - `uptime.c` is `#include`d below. The fake RTOS gives the tick count
 *   (1 tick = 1 ms) and counts its overflows as the kernel does.
 *
 * **How to run**
 * @code
 * docker run --rm -v "${PWD}:/app" pocat-tests test_uptime
 * @endcode
 */
#include "unity.h"
#include "fake_rtos.h"
#include "pocat_test_main.h"

#include "uptime.c"

void setUp(void)
{
    fake_rtos_reset();
}

void tearDown(void)
{
}

/** @brief The uptime is the tick count in whole seconds. */
void test_uptime_counts_whole_seconds(void)
{
    TEST_ASSERT_EQUAL_UINT32(0u, uptime_get_s());
    fake_rtos_advance_ticks(999u);
    TEST_ASSERT_EQUAL_UINT32(0u, uptime_get_s());
    fake_rtos_advance_ticks(1u);
    TEST_ASSERT_EQUAL_UINT32(1u, uptime_get_s());
}

/**
 * @brief The uptime keeps counting when the 32-bit tick count wraps, after 49.7 days.
 * @details 2^32 − 1 ticks are 4,294,967.295 s. 1,001 ticks later the tick
 * count has wrapped to 1,000, and the uptime is (2^32 + 1,000) ms, that is
 * 4,294,968 whole seconds. Without the overflow count it would be 1 s.
 */
void test_uptime_continues_across_the_tick_wrap(void)
{
    fake_rtos_advance_ticks(0xFFFFFFFFu);
    TEST_ASSERT_EQUAL_UINT32(4294967u, uptime_get_s());

    fake_rtos_advance_ticks(1001u);
    TEST_ASSERT_EQUAL_UINT32(1000u, xTaskGetTickCount());
    TEST_ASSERT_EQUAL_UINT32(4294968u, uptime_get_s());
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
    POCAT_RUN_TEST(test_uptime_counts_whole_seconds);
    POCAT_RUN_TEST(test_uptime_continues_across_the_tick_wrap);
    return POCAT_END();
}
