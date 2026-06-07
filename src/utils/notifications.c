#include "notifications.h"
#include "FreeRTOS.h"
#include "task.h"

uint32_t wait_for_notification(TickType_t timeout) {

    uint32_t notificationValue = 0;
    xTaskNotifyWait( 0,          // don’t clear on entry
                    0xFFFFFFFF,  // clear all bits on exit
                    &notificationValue,
                    timeout );
    return notificationValue;

}
