#ifndef EVENT_GROUPS_H
#define EVENT_GROUPS_H
#include "FreeRTOS.h"

typedef uint32_t EventBits_t;
typedef struct fake_event_group *EventGroupHandle_t;

EventGroupHandle_t xEventGroupCreate(void);
EventBits_t xEventGroupSetBits(EventGroupHandle_t eg, EventBits_t bits);
EventBits_t xEventGroupClearBits(EventGroupHandle_t eg, EventBits_t bits);
EventBits_t xEventGroupGetBits(EventGroupHandle_t eg);
EventBits_t xEventGroupWaitBits(EventGroupHandle_t eg, EventBits_t bits_to_wait_for,
                                BaseType_t clear_on_exit, BaseType_t wait_for_all,
                                TickType_t ticks);

#endif
