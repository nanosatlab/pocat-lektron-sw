#include "fake_obdh_requests.h"
#include "fake_rtos.h"
#include "obdh.h"
#include "param_store.h"
#include "unity.h"
#include <string.h>

#define CALL_LOG_MAX 256

static HAL_StatusTypeDef injected = HAL_OK;
static fake_obdh_request_hook_t request_hook;
static fake_obdh_call_t call_log[CALL_LOG_MAX];
static int call_n;

void fake_obdh_reset(void)
{
    injected = HAL_OK;
    request_hook = NULL;
    call_n = 0;
}

void fake_obdh_inject_error(HAL_StatusTypeDef err) { injected = err; }
void fake_obdh_set_request_hook(fake_obdh_request_hook_t hook) { request_hook = hook; }
int  fake_obdh_call_count(void) { return call_n; }

fake_obdh_call_t fake_obdh_call_at(int i)
{
    TEST_ASSERT_TRUE_MESSAGE(i >= 0 && i < call_n, "fake_obdh: call log index out of range");
    return call_log[i];
}

HAL_StatusTypeDef obdh_param_write_request(uint8_t id, const uint8_t *data, size_t length, TickType_t ticks_to_wait)
{
    if (request_hook != NULL) request_hook();
    if (call_n < CALL_LOG_MAX) {
        call_log[call_n] = (fake_obdh_call_t){ .id = id, .len = length, .ticks = ticks_to_wait };
        call_n++;
    }

    if (injected != HAL_OK) return injected;
    if (data == NULL || length == 0 || length > OBDH_DATA_MAX) return HAL_ERROR;

    /* The OBDH task serves it: the store refuses any other caller */
    TaskHandle_t caller = fake_rtos_current_task();
    fake_rtos_set_current_task(tm_get_task_handle(TM_TASK_OBDH));
    HAL_StatusTypeDef status = param_store_write(id, data, length);
    fake_rtos_set_current_task(caller);
    return status;
}
