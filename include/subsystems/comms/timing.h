#pragma once
#include <stdint.h>

#define LORA_SF_DEFAULT   8u
#define LORA_BW_DEFAULT   125000ul
#define LORA_CR_DEFAULT   1u
#define LORA_PRE_DEFAULT  64u
#define LORA_CRC_DEFAULT  1u

uint32_t lora_toa_ms(uint16_t payload_bytes);
uint32_t ack_timeout_ms(uint8_t tc_frame_len);
