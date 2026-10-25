/* SPDX-License-Identifier: GPL-3.0-only */
/* The arena (docs/SD-MODE-DESIGN.md 6b): one pool region the parts' engine memories are placed in by need, instead of
 * a fixed memory per part sized for the largest engine. At most one block per part, first fit in address order;
 * blocks never move (the audio and the second core read them while the main loop plans).
 *
 * The main loop reserves before a part's engine changes (arn_reserve, the interrupts off); the audio takes the
 * reservation at the switch (arn_take, engines.c part_mem_claim, every voice of the part over) and the part's old
 * block, when it is another, goes back. A switch to an engine that keeps nothing here leaves the block as it is, idle
 * (arn_rest): switching back finds the state. An idle block is reclaimed without asking when a reservation needs its
 * room. What still does not fit is refused (the user, 2026-10-25: refuse, and propose what to drop): arn_drop_for
 * names the parts whose memory would make room.
 *
 *   arn_reserve(k, n)       0: part k's next block (n bytes; 0: none) is set aside; -1: no room
 *   arn_take(k)             (the audio, at the switch) the reservation becomes the part's block: its pointer
 *   arn_rest(k)             (the audio, at a switch to an engine that keeps nothing here) the block is idle
 *   arn_ptr(k)              the part's block, 0 none
 *   arn_drop_for(k, n)      the parts (a bit mask) whose blocks, given back, would let n bytes fit for part k, the
 *                           least memory given back; 0 it fits as it is; -1 nothing would (n larger than the arena)
 *   arn_free_bytes()        the room left (the largest gap, for the MEM message)
 * ARN_PARTS and ARN_BYTES are set by the includer. With ARN_FIXED, ARN_SLOT (the largest block a part can ask, a
 * multiple of ARN_ALIGN) and an arena of ARN_PARTS x ARN_SLOT, the placement is fixed, part k at k x ARN_SLOT, as the fixed per-part memories were: nothing is
 * ever refused (engines.c's default). A smaller arena places by first fit and may refuse.
 * A part's own block is free for its own reservation: the new block is written only at the switch, when the old
 * engine is done (as the one fixed memory per part was reused); the other parts' blocks and reservations are not. */
#ifndef ARN_ALIGN
#define ARN_ALIGN 8u
#endif

typedef struct {
    uint32_t off, len;                                  /* len 0: none */
    uint8_t idle;                                       /* the part's engine no longer reads it */
} arn_blk_t;

static arn_blk_t arn_cur[ARN_PARTS], arn_next[ARN_PARTS];
static uint8_t arn_has_next[ARN_PARTS];
static uint8_t arn_mem[ARN_BYTES] __attribute__((aligned(8))) IN_POOL;

static uint32_t arn_round(uint32_t n) { return (n + ARN_ALIGN - 1u) & ~(ARN_ALIGN - 1u); }
#ifndef ARN_FIXED                                       /* (1: the includer's ARN_BYTES is ARN_PARTS x ARN_SLOT) */
#define ARN_FIXED 0
#endif

/* the blocks in use (the parts' blocks, the reservations), but those in skip (a bit mask of parts whose current block
 * is not counted) -> a first-fit gap of n bytes, ARN_BYTES when none */
static uint32_t arn_gap(uint32_t n, uint32_t skip)
{
    uint32_t off[2u * ARN_PARTS], len[2u * ARN_PARTS], c = 0, i, j, at = 0;
    for (i = 0; i < ARN_PARTS; i++) {
        if (arn_cur[i].len && !((skip >> i) & 1u))
            off[c] = arn_cur[i].off, len[c++] = arn_cur[i].len;
        if (arn_has_next[i] && arn_next[i].len)          /* (also when it overlaps its own block: it may be longer) */
            off[c] = arn_next[i].off, len[c++] = arn_next[i].len;
    }
    for (i = 1; i < c; i++)                              /* by address (at most 2 x ARN_PARTS) */
        for (j = i; j > 0 && off[j - 1u] > off[j]; j--) {
            uint32_t t = off[j]; off[j] = off[j - 1u]; off[j - 1u] = t;
            t = len[j]; len[j] = len[j - 1u]; len[j - 1u] = t;
        }
    for (i = 0; i < c; i++) {
        if (off[i] >= at + n)
            return at;
        if (off[i] + len[i] > at)
            at = off[i] + len[i];
    }
    return at + n <= ARN_BYTES ? at : ARN_BYTES;
}

static int arn_reserve(uint32_t k, uint32_t n)
{
    uint32_t at, i, idle = 0;
    if (k >= ARN_PARTS)
        return -1;
    n = arn_round(n);
    arn_has_next[k] = 1;
    if (!n) {
        arn_next[k].len = 0;
        return 0;
    }
#if ARN_FIXED
    if (n > arn_round(ARN_SLOT))
        return arn_has_next[k] = 0, -1;
    arn_next[k].off = k * arn_round(ARN_SLOT), arn_next[k].len = n, arn_next[k].idle = 0;
    return 0;
#endif
    if (arn_cur[k].len >= n) {                          /* its own block holds it: reused at the switch */
        arn_next[k] = arn_cur[k];
        return 0;
    }
    arn_has_next[k] = 0;                                /* (its old reservation is not in the way of its new one) */
    at = arn_gap(n, 1u << k);                           /* (its own block: free for it) */
    if (at == ARN_BYTES) {                              /* the idle blocks back (nothing reads them), then again */
        for (i = 0; i < ARN_PARTS; i++)
            if (arn_cur[i].len && arn_cur[i].idle)
                idle |= 1u << i;
        at = arn_gap(n, idle | 1u << k);
        if (at == ARN_BYTES)
            return -1;
        for (i = 0; i < ARN_PARTS; i++)
            if ((idle >> i) & 1u)
                arn_cur[i].len = 0;                     /* (k's own idle block too: it is not in use) */
    }
    arn_next[k].off = at, arn_next[k].len = n, arn_next[k].idle = 0;
    arn_has_next[k] = 1;
    return 0;
}

static void *arn_take(uint32_t k)
{
    if (k >= ARN_PARTS)
        return 0;
    if (arn_has_next[k]) {
        arn_cur[k] = arn_next[k];
        arn_has_next[k] = 0;
    }
    arn_cur[k].idle = 0;
    return arn_cur[k].len ? arn_mem + arn_cur[k].off : 0;
}

static void arn_rest(uint32_t k)
{
    if (k < ARN_PARTS) {
        arn_cur[k].idle = 1;
        arn_has_next[k] = 0;
    }
}

static void *arn_ptr(uint32_t k) { return k < ARN_PARTS && arn_cur[k].len ? arn_mem + arn_cur[k].off : 0; }

static int32_t arn_drop_for(uint32_t k, uint32_t n)
{
    uint32_t mask, i, idle = 0, best = 0, best_bytes = 0xFFFFFFFFu, had = k < ARN_PARTS ? arn_has_next[k] : 0u;
    n = arn_round(n);
    if (k >= ARN_PARTS || n > ARN_BYTES)
        return -1;
#if ARN_FIXED
    return n <= arn_round(ARN_SLOT) ? 0 : -1;           /* (its slot: always, up to the slot) */
#endif
    if (arn_cur[k].len >= n)
        return 0;                                       /* its own block holds it (arn_reserve reuses it) */
    for (i = 0; i < ARN_PARTS; i++)                     /* the idle blocks: free already (arn_reserve takes them back) */
        if (arn_cur[i].len && arn_cur[i].idle)
            idle |= 1u << i;
    arn_has_next[k] = 0;                                /* (its own old reservation: replaced by the new one) */
    for (mask = 0; mask < (1u << ARN_PARTS); mask++) {  /* (2^ARN_PARTS sets: 16 with four parts) */
        uint32_t bytes = 0;
        if (((mask >> k) & 1u) || (mask & idle))
            continue;                                   /* (not the part asking; idle ones are free anyway) */
        for (i = 0; i < ARN_PARTS; i++)
            if ((mask >> i) & 1u)
                bytes += arn_cur[i].len;
        if (arn_gap(n, mask | idle | 1u << k) != ARN_BYTES && bytes < best_bytes)
            best = mask, best_bytes = bytes;            /* the least memory given back that makes room */
    }
    arn_has_next[k] = (uint8_t)had;
    return best_bytes == 0xFFFFFFFFu ? -1 : (int32_t)best;
}

static uint32_t arn_free_bytes(void)
{
    uint32_t lo = 0, hi = ARN_BYTES, n;
    while (lo < hi) {                                   /* the largest n that fits, in ARN_ALIGN steps */
        n = arn_round((lo + hi + 1u) / 2u);
        if (n > hi)
            n = hi;
        if (arn_gap(n, 0) != ARN_BYTES)
            lo = n;
        else
            hi = n - ARN_ALIGN;
    }
    return lo;
}
