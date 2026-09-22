#include "dedup.h"
#include <string.h>

void dedup_init(DedupRing_t *r)
{
    memset(r, 0, sizeof(*r));
}

bool dedup_check(const DedupRing_t *r, uint8_t type, uint8_t seq,
                 const uint8_t **ack_out, uint8_t *alen)
{
    for (uint8_t i = 0; i < r->n; i++) {
        const DedupEntry_t *e = &r->e[i];
        if (e->type == type && e->seq == seq) {
            if (ack_out != NULL) {
                *ack_out = e->ack;
            }
            if (alen != NULL) {
                *alen = e->ack_len;
            }
            return true;
        }
    }
    return false;
}

void dedup_add(DedupRing_t *r, uint8_t type, uint8_t seq,
               const uint8_t *ack, uint8_t alen)
{
    DedupEntry_t *e = &r->e[r->head];
    e->type = type;
    e->seq  = seq;

    uint8_t copy_len = (alen > DEDUP_ACK_MAX) ? DEDUP_ACK_MAX : alen;
    memcpy(e->ack, ack, copy_len);
    e->ack_len = copy_len;

    r->head = (uint8_t)((r->head + 1u) % DEDUP_SIZE);
    if (r->n < DEDUP_SIZE) {
        r->n++;
    }
}
