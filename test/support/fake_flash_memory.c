#include "fake_flash_memory.h"
#include <string.h>

uint8_t fake_flash_memory[HT_REGION_SIZE];
int mock_erase_count = 0;
int mock_program_count = 0;
int mock_power_cut_after_ops = -1;
int mock_op_counter = 0;
bool mock_strict_mode = true;

void fake_flash_reset(void) {
    memset(fake_flash_memory, 0xFF, HT_REGION_SIZE);
    mock_erase_count = 0;
    mock_program_count = 0;
    mock_power_cut_after_ops = -1;
    mock_op_counter = 0;
    mock_strict_mode = true;
}

bool mock_power_dead(void) {
    if (mock_power_cut_after_ops >= 0 && mock_op_counter >= mock_power_cut_after_ops) {
        return true;
    }
    return false;
}
