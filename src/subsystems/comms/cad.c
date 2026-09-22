#include "cad.h"
#include "radiolib_wrapper.h"
#include "test_instrumentation.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdlib.h>

#define CAD_MAX_ATTEMPTS 4

cad_result_t cad_precheck(void)
{
    for (int attempt = 0; attempt < CAD_MAX_ATTEMPTS; attempt++) {
        uint32_t t0 = TINSTR_US();
        int16_t result = RadioLib_ScanChannel();
        TINSTR_LOG("cad_scan_us=%lu attempt=%d", (unsigned long)(TINSTR_US() - t0), attempt);
        if (result == RADIOLIB_CHANNEL_FREE) {
            TINSTR_LOG("cad_result=FREE attempts=%d", attempt + 1);
            return CAD_RESULT_FREE;
        }
        vTaskDelay(pdMS_TO_TICKS((uint32_t)(rand() % 200) + 1u));
    }
    TINSTR_LOG("cad_result=GIVEUP attempts=%d", CAD_MAX_ATTEMPTS);
    return CAD_RESULT_GIVEUP;
}
