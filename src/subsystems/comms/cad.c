#include "cad.h"
#include "radiolib_wrapper.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdlib.h>

#define CAD_MAX_ATTEMPTS 4

cad_result_t cad_precheck(void)
{
    for (int attempt = 0; attempt < CAD_MAX_ATTEMPTS; attempt++) {
        int16_t result = RadioLib_ScanChannel();
        if (result == RADIOLIB_CHANNEL_FREE) {
            return CAD_RESULT_FREE;
        }
        vTaskDelay(pdMS_TO_TICKS((uint32_t)(rand() % 200) + 1u));
    }
    return CAD_RESULT_GIVEUP;
}
