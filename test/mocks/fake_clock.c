#include "fake_clock.h"
#include "fake_rtos.h"

static ClockFreq_t current_freq = CLK_FREQ_80MHZ;
static int switch_count, reconfigure_count, init_count, last_switch_suspend_depth;
static ClockFreq_t last_switch_freq, last_reconfigure_freq;

void fake_clock_reset(void)
{
    current_freq = CLK_FREQ_80MHZ;
    switch_count = reconfigure_count = init_count = 0;
    last_switch_suspend_depth = 0;
    last_switch_freq = last_reconfigure_freq = CLK_FREQ_80MHZ;
}

void fake_clock_set_current(ClockFreq_t f) { current_freq = f; }
int  fake_clock_switch_count(void) { return switch_count; }
ClockFreq_t fake_clock_last_switch_freq(void) { return last_switch_freq; }
int  fake_clock_last_switch_suspend_depth(void) { return last_switch_suspend_depth; }
int  fake_clock_reconfigure_count(void) { return reconfigure_count; }
ClockFreq_t fake_clock_last_reconfigure_freq(void) { return last_reconfigure_freq; }
int  fake_clock_init_count(void) { return init_count; }

ClockFreq_t clock_get_current(void) { return current_freq; }

bool clock_switch_to_freq(ClockFreq_t freq)
{
    switch_count++;
    last_switch_freq = freq;
    last_switch_suspend_depth = fake_rtos_suspend_all_depth();
    current_freq = freq;
    return true;
}

bool systemclock_init_for_freq(ClockFreq_t freq)
{
    init_count++;
    current_freq = freq;
    return true;
}

void periph_reconfigure_for_freq(ClockFreq_t freq)
{
    reconfigure_count++;
    last_reconfigure_freq = freq;
}

void periph_init_for_freq(ClockFreq_t freq)
{
    (void)freq;
    init_count++;
}
