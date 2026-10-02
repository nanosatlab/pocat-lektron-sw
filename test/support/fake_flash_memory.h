#ifndef FAKE_FLASH_MEMORY_H
#define FAKE_FLASH_MEMORY_H
#include <stdint.h>
#include <stdbool.h>
#include "flash.h"

extern uint8_t fake_flash_memory[HT_REGION_SIZE];

// Test Statistics and Fault Injection
extern int mock_erase_count;
extern int mock_program_count;
extern int mock_power_cut_after_ops;
extern int mock_op_counter;
extern bool mock_strict_mode; 

void fake_flash_reset(void);
bool mock_power_dead(void);

#endif
