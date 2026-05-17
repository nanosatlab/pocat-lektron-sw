#pragma once
#include <stdint.h>
#include "types.h"

#define AIR_FRAME_HDR    5u
#define AIR_FRAME_MAX    255u
#define AIR_PAYLOAD_MAX  250u

typedef struct {
    uint8_t ver;
    uint8_t type;
    uint8_t flags;
    uint8_t seq;
    uint8_t len;
    uint8_t payload[AIR_PAYLOAD_MAX];
} AirFrame_t;

uint8_t air_encode(uint8_t *buf, uint8_t type, uint8_t flags,
                   uint8_t seq, const uint8_t *payload, uint8_t len);

int air_decode(const uint8_t *buf, uint8_t buf_len, AirFrame_t *out);

uint8_t air_encode_ack(uint8_t *buf, uint8_t own_seq,
                       uint8_t ref_type, uint8_t ref_seq);

uint8_t air_encode_nack(uint8_t *buf, uint8_t own_seq,
                        uint8_t ref_type, uint8_t ref_seq, uint8_t reason);
