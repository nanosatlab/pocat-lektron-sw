#include "obdh_requests.h"
#include "fake_flash_memory.h"
#include "unity.h"
#include <string.h>

static HAL_StatusTypeDef injected_error = HAL_OK;

void mock_obdh_inject_error(HAL_StatusTypeDef err) {
    injected_error = err;
}

static int get_offset(uint32_t address) {
    if (address >= HT_BASE_ADDR && address < (HT_BASE_ADDR + HT_REGION_SIZE)) {
        return address - HT_BASE_ADDR;
    }
    return -1;
}

HAL_StatusTypeDef obdh_erase_program_request(uint32_t address, const uint8_t *data, size_t len) {
    if (injected_error != HAL_OK) return injected_error;
    if (mock_power_dead()) return HAL_ERROR;
    
    int offset = get_offset(address);
    if (offset < 0) return HAL_ERROR;
    
    // Simulate Erase
    int page_offset = offset - (offset % FLASH_PAGE_SIZE);
    memset(&fake_flash_memory[page_offset], 0xFF, FLASH_PAGE_SIZE);
    mock_erase_count++;
    mock_op_counter++;
    
    // A power cut could happen exactly after erase, before programming!
    if (mock_power_dead()) return HAL_ERROR; 
    
    // Simulate Program
    if (mock_strict_mode) {
        TEST_ASSERT_EQUAL_MESSAGE(0, len % 8, "Flash programming must be 8-byte aligned (double-word)");
        TEST_ASSERT_EQUAL_MESSAGE(0, address % 8, "Flash destination address must be 8-byte aligned");
    }
    
    for (size_t i = 0; i < len; i++) {
        uint8_t old_val = fake_flash_memory[offset + i];
        uint8_t new_val = data[i];
        if (mock_strict_mode) {
            TEST_ASSERT_MESSAGE((old_val & new_val) == new_val || old_val == 0xFF, "Flash bit transition error: STM32 Flash can only change 1 to 0 without erasing!");
        }
        fake_flash_memory[offset + i] &= new_val;
    }
    mock_program_count++;
    mock_op_counter++;
    return HAL_OK;
}

HAL_StatusTypeDef obdh_program_request(uint32_t address, const uint8_t *data, size_t len) {
    if (injected_error != HAL_OK) return injected_error;
    if (mock_power_dead()) return HAL_ERROR;
    
    int offset = get_offset(address);
    if (offset < 0) return HAL_ERROR;
    
    if (mock_strict_mode) {
        TEST_ASSERT_EQUAL_MESSAGE(0, len % 8, "Flash programming must be 8-byte aligned");
        TEST_ASSERT_EQUAL_MESSAGE(0, address % 8, "Flash destination address must be 8-byte aligned");
    }
    
    for (size_t i = 0; i < len; i++) {
        uint8_t old_val = fake_flash_memory[offset + i];
        uint8_t new_val = data[i];
        if (mock_strict_mode) {
            TEST_ASSERT_MESSAGE((old_val & new_val) == new_val || old_val == 0xFF, "Flash bit transition error: Target must be erased (0xFF) before programming!");
        }
        fake_flash_memory[offset + i] &= new_val;
    }
    mock_program_count++;
    mock_op_counter++;
    return HAL_OK;
}

HAL_StatusTypeDef obdh_read_request(uint32_t address, uint8_t *data, size_t len) {
    int offset = get_offset(address);
    if (offset < 0) return HAL_ERROR;
    memcpy(data, &fake_flash_memory[offset], len);
    return HAL_OK;
}

HAL_StatusTypeDef obdh_write_request(uint32_t address, const uint8_t *data, size_t len) {
    return obdh_erase_program_request(address, data, len);
}
