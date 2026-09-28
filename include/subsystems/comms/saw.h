#pragma once
#include <stdint.h>
#include "FreeRTOS.h"
#include "queue.h"

#define SAW_MAX_RETX   3u

typedef enum { SAW_OK=0, SAW_NACKED=1, SAW_TIMEOUT=2, SAW_CAD_FAIL=3 } saw_result_t;

saw_result_t saw_send(const uint8_t *frame, uint8_t frame_len,
                      uint8_t own_seq, uint8_t own_type,
                      QueueHandle_t rx_q);
