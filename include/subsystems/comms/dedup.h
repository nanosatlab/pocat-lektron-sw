#pragma once
#include <stdint.h>
#include <stdbool.h>

#define DEDUP_SIZE       16
#define DEDUP_ACK_MAX    16

typedef struct {
    uint8_t type;
    uint8_t seq;
    uint8_t ack[DEDUP_ACK_MAX];
    uint8_t ack_len;
} DedupEntry_t;

typedef struct {
    DedupEntry_t e[DEDUP_SIZE];
    uint8_t head;
    uint8_t n;
} DedupRing_t;

void dedup_init(DedupRing_t *r);
bool dedup_check(const DedupRing_t *r, uint8_t type, uint8_t seq,
                 const uint8_t **ack_out, uint8_t *alen);
void dedup_add(DedupRing_t *r, uint8_t type, uint8_t seq,
               const uint8_t *ack, uint8_t alen);
