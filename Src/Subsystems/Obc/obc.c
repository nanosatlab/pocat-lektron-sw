#include "obc.h"
#include "obc_internal.h"

ObcState_t Nominal(void) {
    // to do:
    //  frequency tratment for state
    for(;;)
	{
        uint32_t notificationValue = waitForNotification();

        if ( notificationValue & OBC_PHOTO_CAPTURE ) handlePayloadCapture();
        // ... handle other events
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

void handlePayloadCapture(void) {
    xTaskNotify(xPayloadTaskHandle,
            PAYLOAD_PHOTO_CAPTURE,
            eSetBits);
    // ...
}

void ObcTask(void) {
    // OBC state machine starts in NOMINAL state
    ObcState_t ObcState = NOMINAL; // have to change right?? NOMINAL for now

    for (;;) {
        switch (ObcState) {
            case NOMINAL:
                Nominal(); break;
        }
    }
}