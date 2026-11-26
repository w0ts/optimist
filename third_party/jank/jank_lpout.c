/* SPDX-License-Identifier: GPL-3.0-only */
/* The Launchpad app's packets for the Digitone, in order (jank_lpout.h). */
#include "jank_lpout.h"

void jlo_init(struct jlo *q) { memset(q, 0, sizeof *q); }

void jlo_clear(struct jlo *q)
{
    q->head = 0;
    q->count = 0;
}

size_t jlo_count(const struct jlo *q) { return q->count; }

int jlo_push(struct jlo *q, const uint8_t pkt[4])
{
    if (q->count >= JLO_PACKETS)
        return 0;
    memcpy(q->b[(q->head + q->count) % JLO_PACKETS], pkt, 4);
    q->count++;
    return 1;
}

size_t jlo_drain(struct jlo *q, size_t room, uint8_t *out)
{
    size_t n = 0;
    while (n < room && q->count) {
        memcpy(out + 4u * n, q->b[q->head], 4);
        q->head = (uint8_t)((q->head + 1u) % JLO_PACKETS);
        q->count--;
        n++;
    }
    return n;
}
