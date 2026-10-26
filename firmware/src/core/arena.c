/* SPDX-License-Identifier: GPL-3.0-only */
/* The arena (docs/SD-MODE-DESIGN.md 6b): one pool region the parts' engine memories are placed in by need, instead of
 * a fixed memory per part sized for the largest engine. At most one block per part, first fit in address order;
 * blocks never move (the audio and the second core read them while the main loop plans).
 *
 * The main loop reserves before a part's engine changes (arn_reserve, the interrupts off); the audio takes the
 * reservation at the switch (arn_take, engines.c part_mem_claim, every voice of the part over) and the part's old
 * block, when it is another, goes back. A switch to an engine that keeps nothing here leaves the block as it is, idle
 * (arn_rest): switching back finds the state. An idle block is reclaimed without asking when a reservation needs its
 * room. What still does not fit is refused (the user, 2026-10-25: refuse, and propose what to drop): arn_fits with
 * the blocks it would give back says which part's memory would make room (engines.c part_mem_frees, model.c mem_offer).
 *
 *   arn_reserve(k, n)       0: part k's next block (n bytes; 0: none) is set aside; -1: no room
 *   arn_take(k)             (the audio, at the switch) the reservation becomes the part's block: its pointer
 *   arn_rest(k)             (the audio, at a switch to an engine that keeps nothing here) the block is idle
 *   arn_ptr(k)              the part's block, 0 none
 *   arn_give(k)             (the main loop) the block given back, none reserved: an effect's line no longer used
 *                           (fx/fx_arena.c; the audio no longer reads it)
 *   arn_fits(k, n, skip)    1: arn_reserve(k, n) would succeed now with the blocks of skip (a bit mask of parts)
 *                           given back too (the UI's counts, what to propose to drop; nothing changed)
 *   arn_free_bytes()        the room left (the largest gap, for the MEM message)
 * ARN_PARTS and ARN_BYTES are set by the includer; ARN_TOPS (default none)
 * the parts placed best fit from the arena's top instead of first fit from its bottom (the effects' lines: apart from
 * the engines' blocks, and the gaps they leave filled by size, so the arena cuts up less). With ARN_FIXED, ARN_SLOT (the largest block a part can ask, a
 * multiple of ARN_ALIGN) and an arena of ARN_PARTS x ARN_SLOT, the placement is fixed, part k at k x ARN_SLOT, as the fixed per-part memories were: nothing is
 * ever refused (engines.c's default). A smaller arena places by first fit and may refuse.
 * A part's own block is free for its own reservation: the new block is written only at the switch, when the old
 * engine is done (as the one fixed memory per part was reused); the other parts' blocks and reservations are not. */
#ifndef ARN_ALIGN
#define ARN_ALIGN 8u
#endif
#ifndef ARN_TOPS
#define ARN_TOPS 0u                                      /* (the parts placed best fit from the top: none) */
#endif

typedef struct {
    uint32_t off, len;                                  /* len 0: none */
    uint8_t idle;                                       /* the part's engine no longer reads it */
} arn_blk_t;

typedef struct {
    arn_blk_t b[2][ARN_PARTS];                          /* the parts' blocks, then their reservations */
    uint8_t hn[ARN_PARTS];                              /* a reservation made (its len 0: none needed) */
} arn_st_t;
static arn_st_t arn;                                    /* (one struct: a trial copies it and puts it back, fx_arena.c
                                                         * fxm_plan, model.c mem_load_fits) */
#define arn_cur arn.b[0]
#define arn_next arn.b[1]
#define arn_has_next arn.hn
static uint8_t arn_mem[ARN_BYTES] __attribute__((aligned(8))) IN_POOL;

static uint32_t arn_round(uint32_t n) { return (n + ARN_ALIGN - 1u) & ~(ARN_ALIGN - 1u); }
#ifndef ARN_FIXED                                       /* (1: the includer's ARN_BYTES is ARN_PARTS x ARN_SLOT) */
#define ARN_FIXED 0
#endif

/* block j of the 2 x ARN_PARTS (the parts' blocks, then the reservations), but the parts' blocks in skip (a bit mask of
 * parts whose current block is not counted): its end, 0 none; its start in *o */
static uint32_t arn_end(uint32_t j, uint32_t skip, uint32_t *o)
{
    const arn_blk_t *b = &arn.b[0][0] + j;
    if (j < ARN_PARTS ? skip >> j & 1u : !arn_has_next[j - ARN_PARTS])
        return 0;                                       /* (a reservation overlapping its own block counts: it may be longer) */
    *o = b->off;
    return b->len ? b->off + b->len : 0u;
}
/* the counted blocks -> a gap of n bytes, ARN_BYTES when none: the lowest that fits (first fit), or with top the
 * smallest that fits (best fit; of two alike the higher), n bytes at its top end. The places tried: the arena's start
 * and each block's end (first fit), each block's start and the arena's end, less n (best fit); no sort, no copy */
static uint32_t arn_scan(uint32_t n, uint32_t skip, uint32_t top)
{
    uint32_t j, i, best = ARN_BYTES, room = 0xFFFFFFFFu;
    for (j = 0; j <= 2u * ARN_PARTS; j++) {
        uint32_t o = ARN_BYTES, e = 0, at, lo = 0, bo = 0, be;
        if (j < 2u * ARN_PARTS && !(e = arn_end(j, skip, &o)))
            continue;
        if (top && o < n)
            continue;
        at = top ? (o - n) & ~(ARN_ALIGN - 1u) : e;     /* (the last place: the arena's end, or its start) */
        if (at + n > ARN_BYTES)
            continue;
        for (i = 0; i < 2u * ARN_PARTS; i++) {
            if (!(be = arn_end(i, skip, &bo)))
                continue;
            if (bo < at + n && be > at)
                break;                                  /* (in the way) */
            if (be <= at && be > lo)
                lo = be;                                /* (the gap's start) */
        }
        if (i < 2u * ARN_PARTS)
            continue;
        if (!top ? at < best : o - lo < room || (o - lo == room && at > best))
            room = o - lo, best = at;
    }
    return best;
}
static uint32_t arn_gap(uint32_t n, uint32_t skip) { return arn_scan(n, skip, 0); }

static uint32_t arn_idle(void)                          /* the idle blocks (a bit mask): free, nothing reads them */
{
    uint32_t i, m = 0;
    for (i = 0; i < ARN_PARTS; i++)
        m |= (uint32_t)(arn_cur[i].len && arn_cur[i].idle) << i;
    return m;
}

static int arn_reserve(uint32_t k, uint32_t n)
{
    uint32_t at, i, idle;
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
    at = arn_scan(n, 1u << k, ARN_TOPS >> k & 1u);      /* (its own block: free for it) */
    if (at == ARN_BYTES) {                              /* the idle blocks back (nothing reads them), then again */
        idle = arn_idle();
        at = arn_scan(n, idle | 1u << k, ARN_TOPS >> k & 1u);
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

static void arn_give(uint32_t k)
{
    if (k < ARN_PARTS)
        arn_cur[k].len = 0, arn_has_next[k] = 0;
}

/* 1: arn_reserve(k, n) would place it now, the blocks of skip given back too (nothing changed) */
static int arn_fits(uint32_t k, uint32_t n, uint32_t skip)
{
    n = arn_round(n);
    if (k >= ARN_PARTS)
        return 0;
    if (!n || arn_cur[k].len >= n)
        return 1;
#if ARN_FIXED
    (void)skip;
    return n <= arn_round(ARN_SLOT);
#endif
    return arn_gap(n, arn_idle() | skip | 1u << k) != ARN_BYTES;
}

static void *arn_ptr(uint32_t k) { return k < ARN_PARTS && arn_cur[k].len ? arn_mem + arn_cur[k].off : 0; }

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
