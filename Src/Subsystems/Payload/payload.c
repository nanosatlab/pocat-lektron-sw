#include "payload_internal.h"

void PayloadTask(void) 
{
    for (;;) 
    {
        uint32_t notificationValue = waitForNotification();
        if (notificationValue & PAYLOAD_PHOTO_CAPTURE) {
            // Handle payload capture event
            CapturePhoto();
        }
    }
}

uint32_t waitForNotification(void) {
    uint32_t notificationValue;
    xTaskNotifyWait( 0,          // don’t clear on entry
                    0xFFFFFFFF, // clear all bits on exit
                    &notificationValue,
                    portMAX_DELAY );
    return notificationValue;
}

void CapturePhoto(void) {
    //     /* 1. Initialize camera with current settings */
    // initCam(huart4, resolution, compressibility, info);

    // /* 2. Grab a photo into ‘info’ buffer */
    // getPhoto(huart4, info);
    printf("Capturing photo...\n");
}