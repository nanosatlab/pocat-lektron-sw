#include "fake_obdh_requests.h"
#include "unity.h"
#include <stdbool.h>
#include <string.h>

#define CALL_LOG_MAX 256

static uint8_t image[FAKE_FLASH_SIZE];
static HAL_StatusTypeDef injected = HAL_OK;
static fake_obdh_call_t call_log[CALL_LOG_MAX];
static int call_n;

void fake_obdh_reset(void)
{
    memset(image, 0xFF, sizeof(image));
    injected = HAL_OK;
    call_n = 0;
}

void fake_obdh_inject_error(HAL_StatusTypeDef err) { injected = err; }
int  fake_obdh_call_count(void) { return call_n; }

fake_obdh_call_t fake_obdh_call_at(int i)
{
    TEST_ASSERT_TRUE_MESSAGE(i >= 0 && i < call_n, "fake_obdh: call log index out of range");
    return call_log[i];
}

static bool in_range(uint32_t addr, size_t len)
{
    return addr >= FAKE_FLASH_BASE && (uint64_t)addr - FAKE_FLASH_BASE + len <= FAKE_FLASH_SIZE;
}

uint8_t *fake_obdh_flash(uint32_t addr)
{
    return in_range(addr, 0) ? &image[addr - FAKE_FLASH_BASE] : NULL;
}

static void log_call(fake_obdh_op_t op, uint32_t addr, size_t len)
{
    if (call_n < CALL_LOG_MAX) {
        call_log[call_n].op = op;
        call_log[call_n].addr = addr;
        call_log[call_n].len = len;
        call_n++;
    }
}

/* Common argument checks. Returns HAL_OK when the operation may proceed. */
static HAL_StatusTypeDef precheck(const void *data, uint32_t addr, size_t len)
{
    if (injected != HAL_OK) return injected;
    if (data == NULL || len == 0) return HAL_ERROR;
    if (!in_range(addr, len)) return HAL_ERROR;
    return HAL_OK;
}

HAL_StatusTypeDef obdh_read_request(uint32_t address, uint8_t *data, size_t length)
{
    log_call(FAKE_OBDH_READ, address, length);
    HAL_StatusTypeDef st = precheck(data, address, length);
    if (st != HAL_OK) return st;
    memcpy(data, &image[address - FAKE_FLASH_BASE], length);
    return HAL_OK;
}

HAL_StatusTypeDef obdh_write_request(uint32_t address, const uint8_t *data, size_t length)
{
    log_call(FAKE_OBDH_WRITE, address, length);
    HAL_StatusTypeDef st = precheck(data, address, length);
    if (st != HAL_OK) return st;
    memcpy(&image[address - FAKE_FLASH_BASE], data, length);
    return HAL_OK;
}

HAL_StatusTypeDef obdh_program_request(uint32_t address, const uint8_t *data, size_t length)
{
    log_call(FAKE_OBDH_PROGRAM, address, length);
    HAL_StatusTypeDef st = precheck(data, address, length);
    if (st != HAL_OK) return st;
    for (size_t i = 0; i < length; i++) image[address - FAKE_FLASH_BASE + i] &= data[i];
    return HAL_OK;
}

HAL_StatusTypeDef obdh_erase_program_request(uint32_t address, const uint8_t *data, size_t length)
{
    log_call(FAKE_OBDH_ERASE_PROGRAM, address, length);
    HAL_StatusTypeDef st = precheck(data, address, length);
    if (st != HAL_OK) return st;
    uint32_t off = address - FAKE_FLASH_BASE;
    memset(&image[off - (off % FLASH_PAGE_SIZE)], 0xFF, FLASH_PAGE_SIZE);
    for (size_t i = 0; i < length; i++) image[off + i] &= data[i];
    return HAL_OK;
}
