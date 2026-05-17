#include "frame.h"
#include <string.h>

uint8_t air_encode(uint8_t *buf, uint8_t type, uint8_t flags,
                   uint8_t seq, const uint8_t *payload, uint8_t len)
{
    if (len > AIR_PAYLOAD_MAX) {
        return 0;
    }
    buf[0] = AIR_PROTOCOL_VER;
    buf[1] = type;
    buf[2] = flags;
    buf[3] = seq;
    buf[4] = len;
    if (len > 0 && payload != NULL) {
        memcpy(&buf[5], payload, len);
    }
    return (uint8_t)(AIR_FRAME_HDR + len);
}

int air_decode(const uint8_t *buf, uint8_t buf_len, AirFrame_t *out)
{
    if (buf_len < AIR_FRAME_HDR) {
        return -1;
    }
    if (buf[0] != AIR_PROTOCOL_VER) {
        return -1;
    }
    uint8_t len = buf[4];
    if ((uint8_t)(AIR_FRAME_HDR + len) > buf_len) {
        return -1;
    }
    out->ver   = buf[0];
    out->type  = buf[1];
    out->flags = buf[2];
    out->seq   = buf[3];
    out->len   = len;
    if (len > 0) {
        memcpy(out->payload, &buf[5], len);
    }
    return 0;
}

uint8_t air_encode_ack(uint8_t *buf, uint8_t own_seq,
                       uint8_t ref_type, uint8_t ref_seq)
{
    uint8_t body[3] = { ref_type, ref_seq, 0x00 };
    return air_encode(buf, AIR_ACK, 0x00, own_seq, body, 3);
}

uint8_t air_encode_nack(uint8_t *buf, uint8_t own_seq,
                        uint8_t ref_type, uint8_t ref_seq, uint8_t reason)
{
    uint8_t body[3] = { ref_type, ref_seq, reason };
    return air_encode(buf, AIR_NACK, 0x00, own_seq, body, 3);
}
