#ifndef OBC_INTERNAL_H_
#define OBC_INTERNAL_H_

#include "FreeRTOS.h"
#include "main.h"

#define COMMS_STACK_SIZE 3000
#define COMMS_PRIORITY 1

typedef enum {
    NOMINAL,
} ObcState_t;

ObcState_t Nominal(void);

static uint32_t waitForNotification(void);

#endif /* OBC_INTERNAL_H_ */