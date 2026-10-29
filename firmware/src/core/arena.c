/* SPDX-License-Identifier: GPL-3.0-only */
/* The arena (docs/SD-MODE-DESIGN.md 6b): one pool region the parts' engine memories are placed in by need, instead of
 * a fixed memory per part sized for the largest engine; in every build with one (2026-10-29, the user: one memory
 * setting) the effects' lines and the other buffers too (fx/fx_arena.c, core/arena_bufs.c). At most one block per
 * client, first fit in address order (the parts), best fit from the top (ARN_TOPS: the send lines, the buffers).
 *
 * The main loop reserves before a part's engine changes (arn_reserve, the interrupts off); the audio takes the
 * reservation at the switch (arn_take, engines.c part_mem_claim, every voice of the part over) and the part's old
 * block, when it is another, goes back. A switch to an engine that keeps nothing here leaves the block as it is, idle
 * (arn_rest): switching back finds the state. An idle block is reclaimed without asking when a reservation needs its
 * room. What still does not fit is refused (the user, 2026-10-25: refuse, and propose what to drop): arn_fits with
 * the blocks it would give back says which part's memory would make room (engines.c part_mem_frees, model.c mem_offer).
 *
 *   arn_reserve(k, n)       0: client k's next block (n bytes; 0: none) is set aside; -1: no room
 *   arn_take(k)             (the audio, at the switch) the reservation becomes the client's block: its pointer
 *   arn_rest(k)             (the audio, at a switch to an engine that keeps nothing here) the block is idle
 *   arn_ptr(k)              the client's block, 0 none
 *   arn_give(k)             (the main loop) the block given back, none reserved (the audio no longer reads it)
 *   arn_resize(k, o, n)     0: client k's block now o .. o + n (a block in use growing or shrinking at its edges:
 *                           fx/insfx/fxi.c's room); -1: something in the way, nothing changed
 *   arn_fits(k, n, skip)    1: arn_reserve(k, n) would succeed now with the blocks of skip (a bit mask of clients)
 *                           given back too (the UI's counts, what to propose to drop; nothing changed)
 *   arn_room()              the room left, every gap (the idle blocks free: the load's gauge, the inserts' counts)
 *   arn_free_bytes()        the room left (the largest gap, for the MEM message)
 * ARN_PARTS and ARN_BYTES are set by the includer; ARN_TOPS (default none) the clients placed best fit from the top;
 * ARN_ALIGN the unit every size and place is a whole number of. A part's own block is free for its own reservation:
 * the new block is written only at the switch, when the old engine is done (as the one fixed memory per part was
 * reused); the other blocks and reservations are not.
 *
 * Packing (the user, 2026-10-29: "how do we avoid fragmentation?"). The clients of ARN_MOVES may be moved, and only
 * while ARN_CAN says (the includer: a part while it is quiet, a send line while its bus is silent, the inserts' room
 * while none of its lines is heard; any of them in a load's pack): never a drop-out while one plays. A refusal the
 * free bytes would hold (arn_pack: a part's change, a load's part, a send line, the inserts' room growing at a given
 * place, another buffer) plans moves on a copy of the arena: the blocks over a place asked for moved anywhere else,
 * the ARN_UPS (the send lines, the room) up to the highest place when that leaves a larger gap, the others by address
 * down to the lowest place below them, until the refused block fits (the fewest moves in that order); no plan, the
 * refusal stands, and when a block that sounds was in the way the arena is packed once it is quiet (arn_pk.want, tried
 * every 32 passes). A project's load packs every block once its parts have switched (arn_pack_all). The main loop
 * runs the plan (arn_poll, every pass): one block at a time, its client gated while it moves (arn_moving: the audio
 * neither renders nor starts its voices, a switch waits; engines.c, voice.c; ARN_MV_READY: a send line's bus gated
 * two blocks before), the contents copied (no state lost) in windows of at most ARN_MV_CHUNK bytes with the
 * interrupts off and at most ARN_MV_US microseconds (the window stops at the first 256 bytes past it; the block period
 * is CTL / FS = 726 us, so the audio's interrupt waits 1/18 of it at most), started just after an audio block
 * (arn_mv_sync: the copy is usually done before the next one, the client not skipped at all), then its place and
 * pointer swapped with the interrupts off (ARN_MV_MOVED). Before each move its target is checked again (a reservation
 * in the audio's section apply may have taken it) and the client too (it may sound again): the plan stops.
 * The memory (the user, 2026-10-29: "arena first, undo gets the rest"): on the device the arena is the SRAM the link
 * leaves free (ARN_FREE, arn_boot: the pool's end to the stacks' guard band less the build's spare), less ARN_UNDO_KEEP
 * kept after it for the undo history (seq/undo.c), which also borrows whatever the arena leaves unused at its top and
 * gives it back when a block is placed there (ARN_GREW: its oldest levels dropped). The host: a stand-in of ARN_BYTES.
 * The probe (arn_probe: SYSTEM > INFO ARENA MOVE): the moves, the longest (us, its start to its swap), the
 * longest window with the interrupts off (us), the audio blocks a client was skipped in */
#ifndef AINL
#define AINL static inline __attribute__((always_inline))
#endif
#ifndef ARN_ALIGN
#define ARN_ALIGN 8u
#endif
#ifndef ARN_TOPS
#define ARN_TOPS 0u                                     /* (the clients placed best fit from the top: none) */
#endif
#ifndef ARN_HIGH
#define ARN_HIGH 0                                      /* the ARN_TOPS clients: best fit (0, measured as good as the
                                                         * highest place, 1: tests/arena_frag_test.c) */
#endif
#ifndef ARN_MOVES
#define ARN_MOVES 0u                                    /* (the clients that may be moved: none) */
#endif
#ifndef ARN_CAN
#define ARN_CAN(k) 1                                    /* (client k's block may move now: quiet, or a load) */
#endif
#ifndef ARN_MV_READY
#define ARN_MV_READY(k) 1                               /* (its client ready for the copy: the includer's) */
#endif
#ifndef ARN_UPS
#define ARN_UPS 0u                                      /* (of those, the ones moved up, to the highest place, a
                                                         * unit of ARN_UP_ALIGN: the send lines, the inserts' room) */
#endif
#ifndef ARN_UP_ALIGN
#define ARN_UP_ALIGN ARN_ALIGN
#endif
#ifndef ARN_MV_CHUNK
#define ARN_MV_CHUNK 2048u                              /* bytes at most in one interrupts-off window of a move */
#endif
#ifndef ARN_MV_US
#define ARN_MV_US 40u                                   /* .. and microseconds (the first 256 B past it end it) */
#endif
#ifndef ARN_US
#define ARN_US() 0u                                     /* (the clock for the probe: engines.c fm1_micros) */
#endif
#ifndef ARN_MV_MOVED
#define ARN_MV_MOVED(k, p) ((void)(k), (void)(p))       /* (client k's block now at p: the includer's pointers) */
#endif
#define ARN_NO 0xFFu
_Static_assert(ARN_PARTS <= 32u, "the arena's clients: a bit each in a mask (skip, ARN_TOPS, ARN_MOVES)");
#ifndef ARN_UNDO_KEEP
#define ARN_UNDO_KEEP 0u                                /* (after the arena, the undo history's least: the includer's) */
#endif
#ifndef ARN_UNDO_ROOM
#define ARN_UNDO_ROOM ARN_UNDO_KEEP                     /* (the most ARN_UNDO_KEEP can be: the includer's when it is not a
                                                         * constant; the host's array) */
#endif
#ifndef ARN_TOPS_HW
#define ARN_TOPS_HW 0                                   /* the ARN_TOPS from the top of what is in use (1: the undo history
                                                         * after it borrows the rest) or from the arena's end (0: measured
                                                         * 2026-10-29, tests/arena_frag_test.c on the farm: a half to a third
                                                         * of the bytes hidden in gaps and fewer refusals at the real sizes;
                                                         * 1 gave the undo history 3 to 27 KB more on average) */
#endif
#ifndef ARN_GREW
#define ARN_GREW(end) ((void)(end))                     /* (a block now ends at end: the undo history makes way) */
#endif

typedef struct {
    uint32_t off, len;                                  /* len 0: none */
    uint8_t idle;                                       /* the part's engine no longer reads it */
} arn_blk_t;

typedef struct {
    arn_blk_t b[2][ARN_PARTS];                          /* the clients' blocks, then their reservations */
    uint8_t hn[ARN_PARTS];                              /* a reservation made (its len 0: none needed) */
    uint8_t mk;                                         /* the client whose block is moving (ARN_NO none) .. */
    uint32_t mo;                                        /* .. to here: taken (nothing else is placed there) */
} arn_st_t;
static arn_st_t arn = {.mk = ARN_NO};                   /* (one struct: a trial copies it and puts it back, fx_arena.c,
                                                         * model.c mem_load_fits, arn_pack's plan) */
#define arn_cur arn.b[0]
#define arn_next arn.b[1]
#define arn_has_next arn.hn
#ifdef ARN_FREE                                         /* the device: the free SRAM, taken at boot (arn_boot) */
static uint8_t *arn_mem;
static uint32_t arn_size;                               /* (ARN_BYTES: the includer's name for it) */
/* (at power-on, before anything is placed) the arena from lo, what lo .. hi holds less ARN_UNDO_KEEP after it, in units */
static void arn_boot(uint8_t *lo, uint8_t *hi)
{
    uintptr_t a = ((uintptr_t)lo + ARN_ALIGN - 1u) & ~(uintptr_t)(ARN_ALIGN - 1u);
    uint32_t room = (uint8_t *)a < hi ? (uint32_t)(hi - (uint8_t *)a) : 0u;
    arn_mem = (uint8_t *)a;
    arn_size = room > ARN_UNDO_KEEP ? (room - ARN_UNDO_KEEP) & ~(ARN_ALIGN - 1u) : 0u;
}
#else                                                   /* (the host: a stand-in of ARN_BYTES, the undo's least after it) */
_Static_assert(ARN_BYTES % ARN_ALIGN == 0u, "the arena: whole units");
static uint8_t arn_mem[ARN_BYTES + ARN_UNDO_ROOM] __attribute__((aligned(8))) IN_POOL;
#define arn_boot(lo, hi) ((void)(lo), (void)(hi))
#endif

static uint32_t arn_round(uint32_t n) { return (n + ARN_ALIGN - 1u) & ~(ARN_ALIGN - 1u); }

/* block j of A (the clients' blocks, then the reservations, then the moving block's target) but the clients' blocks
 * in skip (a bit mask of clients whose current block is not counted): its end, 0 none; its start in *o */
static uint32_t arn_end(const arn_st_t *A, uint32_t j, uint32_t skip, uint32_t *o)
{
    const arn_blk_t *b;
    if (j == 2u * ARN_PARTS) {
        if (A->mk >= ARN_PARTS)
            return 0;
        *o = A->mo;
        return A->mo + A->b[0][A->mk].len;
    }
    b = &A->b[0][0] + j;
    if (j < ARN_PARTS ? skip >> j & 1u : !A->hn[j - ARN_PARTS])
        return 0;                                       /* (a reservation overlapping its own block counts: it may be longer) */
    *o = b->off;
    return b->len ? b->off + b->len : 0u;
}
#define ARN_NB (2u * ARN_PARTS + 1u)                    /* (arn_end's blocks) */
/* 1: at .. at + n overlaps none of A's counted blocks */
static int arn_clear(const arn_st_t *A, uint32_t at, uint32_t n, uint32_t skip)
{
    uint32_t i, o = 0, e;
    for (i = 0; i < ARN_NB; i++)
        if ((e = arn_end(A, i, skip, &o)) && o < at + n && e > at)
            return 0;
    return 1;
}
/* the counted blocks -> a gap of n bytes below lim, ARN_BYTES when none: the lowest that fits (first fit), or with top
 * (the place's unit) the highest place (ARN_HIGH) or the smallest gap that fits (best fit; of two alike the higher), n
 * bytes at its top end. The places tried: the arena's start and each block's end (first fit), each block's start and
 * lim, less n (from the top); no sort, no copy */
static uint8_t arn_high;                                /* (arn_plan's moves up: the highest place, not best fit) */
static uint32_t arn_scan(const arn_st_t *A, uint32_t n, uint32_t skip, uint32_t top, uint32_t lim)
{
    uint32_t j, i, best = ARN_BYTES, room = 0xFFFFFFFFu;
    for (j = 0; j <= ARN_NB; j++) {
        uint32_t o = lim, e = 0, at, lo = 0, bo = 0, be;
        if (j < ARN_NB && !(e = arn_end(A, j, skip, &o)))
            continue;
        if (top && (o < n || o > lim))
            continue;
        at = top ? (o - n) & ~(top - 1u) : e;           /* (the last place: lim, or the arena's start; top: the unit) */
        if (at + n > lim)
            continue;
        for (i = 0; i < ARN_NB; i++) {
            if (!(be = arn_end(A, i, skip, &bo)))
                continue;
            if (bo < at + n && be > at)
                break;                                  /* (in the way) */
            if (be <= at && be > lo)
                lo = be;                                /* (the gap's start) */
        }
        if (i < ARN_NB)
            continue;
        if (!top ? at < best : (ARN_HIGH || arn_high) ? best == ARN_BYTES || at > best : o - lo < room || (o - lo == room && at > best))
            room = o - lo, best = at;
    }
    return best;
}
/* A's highest block's end but those of skip (ARN_UNDO_KEEP: what the undo history after it borrows starts there) */
static uint32_t arn_hw(const arn_st_t *A, uint32_t skip)
{
    uint32_t j, o = 0, e, t = 0;
    for (j = 0; j < ARN_NB; j++)
        if ((e = arn_end(A, j, skip, &o)) > t)
            t = e;
    return t;
}
/* client k's place for n: the ARN_TOPS best fit from the top of what is in use (the undo history keeps the rest), else
 * (and the others) first fit from the bottom */
static uint32_t arn_place(const arn_st_t *A, uint32_t k, uint32_t n, uint32_t skip)
{
    uint32_t at = ARN_BYTES, hw;
    if (ARN_TOPS >> k & 1u) {
        hw = ARN_TOPS_HW && ARN_UNDO_KEEP ? arn_hw(A, skip) : ARN_BYTES;
        at = arn_scan(A, n, skip, ARN_ALIGN, hw);
        if (at == ARN_BYTES && hw < ARN_BYTES)          /* (none under it: just over what is in use) */
            at = arn_scan(A, n, skip, ARN_ALIGN, hw + n < ARN_BYTES ? hw + n : ARN_BYTES);
    }
    return at != ARN_BYTES ? at : arn_scan(A, n, skip, 0u, ARN_BYTES);
}
static uint32_t arn_gap(uint32_t n, uint32_t skip) { return arn_scan(&arn, n, skip, 0, ARN_BYTES); }

/* the idle blocks (a bit mask): free, nothing reads them; but one set aside for its client's switch back (a reservation
 * of its own block: engines.c part_mem_park), kept */
static uint32_t arn_idle_of(const arn_st_t *A)
{
    uint32_t i, m = 0;
    for (i = 0; i < ARN_PARTS; i++)
        m |= (uint32_t)(A->b[0][i].len && A->b[0][i].idle &&
                        !(A->hn[i] && A->b[1][i].off == A->b[0][i].off && A->b[1][i].len)) << i;
    return m;
}
#define arn_idle() arn_idle_of(&arn)

static int arn_reserve_in(arn_st_t *A, uint32_t k, uint32_t n)
{
    uint32_t at, i, idle;
    if (k >= ARN_PARTS)
        return -1;
    n = arn_round(n);
    A->hn[k] = 1;
    if (!n) {
        A->b[1][k].len = 0;
        return 0;
    }
    if (A->b[0][k].len >= n && A->mk != k) {            /* its own block holds it: reused at the switch */
        A->b[1][k] = A->b[0][k];
        return 0;
    }
    A->hn[k] = 0;                                       /* (its old reservation is not in the way of its new one) */
    at = arn_place(A, k, n, A->mk == k ? 0u : 1u << k); /* (its own block: free for it, but while it moves) */
    if (at == ARN_BYTES) {                              /* the idle blocks back (nothing reads them), then again */
        idle = arn_idle_of(A);
        at = arn_place(A, k, n, idle | (A->mk == k ? 0u : 1u << k));
        if (at == ARN_BYTES)
            return -1;
        for (i = 0; i < ARN_PARTS; i++)
            if ((idle >> i) & 1u)
                A->b[0][i].len = 0;                     /* (k's own idle block too: it is not in use) */
    }
    A->b[1][k].off = at, A->b[1][k].len = n, A->b[1][k].idle = 0;
    A->hn[k] = 1;
    if (A == &arn)
        ARN_GREW(at + n);
    return 0;
}
static int arn_reserve(uint32_t k, uint32_t n) { return arn_reserve_in(&arn, k, n); }

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
    if (k < ARN_PARTS && arn.mk != k)
        arn_cur[k].len = 0, arn_has_next[k] = 0;
}

static int arn_resize(uint32_t k, uint32_t o, uint32_t n)
{
    n = arn_round(n);
    if (k >= ARN_PARTS || o % ARN_ALIGN || o + n > ARN_BYTES || arn.mk == k || !arn_clear(&arn, o, n, 1u << k))
        return -1;
    arn_cur[k].off = o, arn_cur[k].len = n, arn_cur[k].idle = 0;
    ARN_GREW(o + n);
    return 0;
}

/* 1: arn_reserve(k, n) would place it now, the blocks of skip given back too (nothing changed) */
static int arn_fits(uint32_t k, uint32_t n, uint32_t skip)
{
    n = arn_round(n);
    if (k >= ARN_PARTS)
        return 0;
    if (!n || (arn_cur[k].len >= n && arn.mk != k))
        return 1;
    return arn_place(&arn, k, n, arn_idle() | skip | 1u << k) != ARN_BYTES;
}

static void *arn_ptr(uint32_t k) { return k < ARN_PARTS && arn_cur[k].len ? arn_mem + arn_cur[k].off : 0; }

static uint32_t arn_top(void) { return arn_hw(&arn, 0); }   /* the highest block's end (a reservation, a move's target) */
static uint32_t arn_room(void)                          /* the room left, every gap (the idle blocks free) */
{
    uint32_t k, used = 0;
    for (k = 0; k < ARN_PARTS; k++) {
        uint32_t c = arn_cur[k].idle ? 0u : arn_cur[k].len, x = arn_has_next[k] ? arn_next[k].len : 0u;
        used += arn_has_next[k] && arn_next[k].off == arn_cur[k].off ? (c > x ? c : x) : c + x;
    }
    if (arn.mk < ARN_PARTS)
        used += arn_cur[arn.mk].len;                    /* (a move's target: both places taken until the swap) */
    return used < ARN_BYTES ? ARN_BYTES - used : 0u;
}

static uint32_t arn_big(const arn_st_t *A)              /* A's largest gap (the largest n that fits, in ARN_ALIGN steps) */
{
    uint32_t lo = 0, hi = ARN_BYTES, n;
    while (lo < hi) {
        n = arn_round((lo + hi + 1u) / 2u);
        if (n > hi)
            n = hi;
        if (arn_scan(A, n, 0, 0, ARN_BYTES) != ARN_BYTES)
            lo = n;
        else
            hi = n - ARN_ALIGN;
    }
    return lo;
}
static uint32_t arn_free_bytes(void) { return arn_big(&arn); }

/* ---- Packing (above) */
static struct {
    uint32_t moves, max_us, off_us, gated, stops;       /* the probe: moves done, the longest (us), the longest
                                                         * interrupts-off window (us), audio blocks a client was
                                                         * skipped in (the includer counts), plans stopped */
} arn_probe;
static struct {
    uint8_t n, i;                                       /* the plan's moves, the next */
    uint8_t k[2u * ARN_PARTS];                          /* .. each: the client, */
    uint32_t from[2u * ARN_PARTS], to[2u * ARN_PARTS];  /* its place now and its new one */
    uint32_t done;                                      /* the moving block's bytes copied */
    uint32_t t0;                                        /* (ARN_US at its start) */
    uint8_t all;                                        /* arn_pack_all asked (a load): when nothing is pending */
    uint8_t want;                                       /* a refusal found no plan (a block in the way still sounds):
                                                         * every block packed once it is quiet (arn_poll) */
    uint8_t blocked;                                    /* (arn_plan: blocks it could not move, they sound) */
    uint8_t ready, tick;                                /* (the moving block's client ready; arn_poll's passes) */
    uint8_t load;                                       /* the plan under way is a load's (arn_can: every block moves) */
} arn_pk;
/* the goal of a plan: client k's n bytes, at the place at (ARN_BYTES: where arn_reserve puts it) */
typedef struct { uint32_t k, n, at; } arn_goal_t;
static int arn_goal_fits(const arn_st_t *A, const arn_goal_t *g)
{
    arn_st_t T = *A;
    T.hn[g->k] = 0;                                     /* (arn_plan's stand-in for its place: not in its way) */
    if (g->at != ARN_BYTES)
        return arn_clear(&T, g->at, arn_round(g->n), arn_idle_of(&T) | 1u << g->k);
    return !arn_reserve_in(&T, g->k, g->n);
}
static int arn_movable(const arn_st_t *A, uint32_t k)   /* client k's block may be moved now */
{
    return (ARN_MOVES >> k & 1u) && A->b[0][k].len && !A->b[0][k].idle && !A->hn[k] && ARN_CAN(k);
}
/* the plan on T (a copy), until g fits (g 0: every move): first the movable blocks over g's place, each to the
 * lowest place anywhere else that holds it (another line's free place too); then the ARN_UPS, the highest first, each
 * to the highest place; then the others by address, each to the lowest place below it. The moves; -1 g
 * does not fit even so */
static int arn_plan(arn_st_t *T, const arn_goal_t *g)
{
    uint32_t i, j, k, m = 0, at, o[ARN_PARTS], fx = g && g->at != ARN_BYTES;
    uint8_t id[ARN_PARTS];
    arn_pk.blocked = 0;
    for (k = 0; k < ARN_PARTS; k++)
        if (!arn_movable(T, k))
            arn_pk.blocked += (ARN_MOVES >> k & 1u) && T->b[0][k].len && !T->b[0][k].idle && !T->hn[k];
        else {
            for (j = m; j > 0 && o[j - 1u] > T->b[0][k].off; j--)
                o[j] = o[j - 1u], id[j] = id[j - 1u];
            o[j] = T->b[0][k].off, id[j] = (uint8_t)k, m++;
        }
    arn_pk.n = 0;
    if (fx)                                             /* (the place asked for: kept free while the others move) */
        T->hn[g->k] = 1, T->b[1][g->k].off = g->at, T->b[1][g->k].len = arn_round(g->n);
    if (g && arn_goal_fits(T, g))
        return 0;
    for (i = 0; i < 3u * m; i++) {
        uint32_t pass = i / m, up, over;
        k = id[pass == 1u ? m - 1u - i % m : i % m];
        up = ARN_UPS >> k & 1u;
        over = fx && T->b[0][k].off < g->at + arn_round(g->n) && T->b[0][k].off + T->b[0][k].len > g->at;
        if (pass ? over || up != (pass == 1u) : !over)
            continue;
        arn_high = (uint8_t)up;
        at = arn_scan(T, T->b[0][k].len, 1u << k, up ? ARN_UP_ALIGN : 0u,
                      up && ARN_TOPS_HW && ARN_UNDO_KEEP ? arn_hw(T, 1u << k) : ARN_BYTES);   /* (up: no higher
                                                         * than what is in use) */
        arn_high = 0;
        if (at == ARN_BYTES && !pass)
            return -1;
        if (at == ARN_BYTES || (pass && (up ? at <= T->b[0][k].off : at >= T->b[0][k].off)))
            continue;                                   /* (nothing higher, or lower, holds it) */
        if (pass == 1u) {                               /* (moved up only when that leaves a larger gap: a place a
                                                         * unit short of the block above would cut the room in two) */
            uint32_t was = T->b[0][k].off, big = arn_big(T), more;
            T->b[0][k].off = at;
            more = arn_big(T) > big;
            T->b[0][k].off = was;
            if (!more)
                continue;
        }
        arn_pk.k[arn_pk.n] = (uint8_t)k, arn_pk.from[arn_pk.n] = T->b[0][k].off, arn_pk.to[arn_pk.n] = at;
        arn_pk.n++;
        T->b[0][k].off = at;
        if (g && arn_goal_fits(T, g))
            return arn_pk.n;
    }
    return g ? -1 : arn_pk.n;
}
/* a refusal: the moves that make g fit planned (the idle blocks given back first: the reservation would take them);
 * 1 planned (arn_poll runs it: arn_busy until done, then the caller tries again), 0 no plan (the refusal stands) */
static int arn_pack(uint32_t k, uint32_t n, uint32_t at)
{
    arn_st_t T;
    arn_goal_t g;
    uint32_t i, idle;
    if (arn_pk.i < arn_pk.n || arn.mk != ARN_NO)
        return 0;
    g.k = k, g.n = n, g.at = at;
    fm1_irq_off();
    T = arn;
    fm1_irq_on();
    idle = arn_idle_of(&T);
    for (i = 0; i < ARN_PARTS; i++)
        if (idle >> i & 1u)
            T.b[0][i].len = 0;
    if (arn_plan(&T, &g) <= 0) {
        arn_pk.n = arn_pk.i = 0;
        arn_pk.want |= arn_pk.blocked != 0;            /* (a block that sounds in the way: packed once it is quiet) */
        return 0;
    }
    arn_pk.i = 0;
    fm1_irq_off();
    for (i = 0; i < ARN_PARTS; i++)
        if ((idle >> i & 1u) && arn_cur[i].len && arn_cur[i].idle)
            arn_cur[i].len = 0;                         /* (still idle: given back, as arn_reserve would) */
    fm1_irq_on();
    return 1;
}
static int arn_busy(void) { return arn_pk.i < arn_pk.n || arn.mk != ARN_NO; }   /* (a load's pack_all: not waited for) */
AINL int arn_moving(uint32_t k) { return arn.mk == k; }   /* (the audio, CPU1: client k's block is being moved) */
static void arn_pack_all(void) { arn_pk.all = 1; }      /* (a load: every block packed once nothing is pending) */

#ifndef ARN_MV_SYNC
#define ARN_MV_SYNC() ((void)0)                         /* (wait for the end of an audio block: the includer's) */
#endif
/* one window: the next bytes of the moving block (at most ARN_MV_CHUNK and about ARN_MV_US), the interrupts off */
static void arn_mv_window(void)
{
    uint32_t k = arn.mk, len = arn_cur[k].len, src = arn_cur[k].off, dst = arn.mo, t0, us, c = 0;
    fm1_irq_off();
    t0 = ARN_US();
    while (arn_pk.done < len && c < ARN_MV_CHUNK && (!c || (uint32_t)(ARN_US() - t0) < ARN_MV_US)) {
        uint32_t s = len - arn_pk.done < 256u ? len - arn_pk.done : 256u, at;
        at = dst < src ? arn_pk.done : len - arn_pk.done - s;   /* (overlapping: up from the start, down from the end) */
        memmove(arn_mem + dst + at, arn_mem + src + at, s);
        arn_pk.done += s, c += s;
    }
    us = (uint32_t)(ARN_US() - t0);
    fm1_irq_on();
    if (us > arn_probe.off_us)
        arn_probe.off_us = us;
}
/* the main loop, every pass: a plan's next move (its target checked again; taken: the plan stops), copied, swapped */
static void arn_poll(void)
{
    uint32_t k, i, ok, idle;
    arn_st_t T;
    if (arn.mk == ARN_NO && arn_pk.i >= arn_pk.n) {
        if (!arn_pk.all && (!arn_pk.want || ++arn_pk.tick & 31u))
            return;                                     /* (a refusal's pack later: tried every 32 passes) */
        for (k = 0; k < ARN_PARTS; k++)
            if (arn_has_next[k] && (ARN_MOVES >> k & 1u))
                return;                                 /* (a load's switches first: the pack waits) */
        fm1_irq_off();
        T = arn;
        fm1_irq_on();
        idle = arn_idle_of(&T);
        for (i = 0; i < ARN_PARTS; i++)
            if (idle >> i & 1u)
                T.b[0][i].len = 0;                      /* (the idle blocks: given back, as a reservation would) */
        arn_pk.load = arn_pk.all, arn_pk.all = 0;      /* (a load's pack: its blocks move, sounding or not) */
        if (arn_plan(&T, 0) <= 0) {
            arn_pk.load = 0;
            if (!arn_pk.blocked)
                arn_pk.want = 0;                        /* (nothing left that sounds in the way: packed) */
            return;
        }
        arn_pk.i = 0;
        fm1_irq_off();
        for (i = 0; i < ARN_PARTS; i++)
            if ((idle >> i & 1u) && arn_cur[i].len && arn_cur[i].idle)
                arn_cur[i].len = 0;
        fm1_irq_on();
    }
    if (arn.mk == ARN_NO) {
        i = arn_pk.i;
        k = arn_pk.k[i];
        fm1_irq_off();
        ok = arn_movable(&arn, k) && arn_cur[k].off == arn_pk.from[i] &&
             arn_clear(&arn, arn_pk.to[i], arn_cur[k].len, 1u << k);
        if (ok) {
            arn.mk = (uint8_t)k, arn.mo = arn_pk.to[i];
            ARN_GREW(arn.mo + arn_cur[k].len);
        }
        fm1_irq_on();
        if (!ok) {
            arn_pk.n = arn_pk.i = 0, arn_pk.load = 0;   /* (something took its place, or it sounds again: the plan
                                                         * stops, packed later) */
            arn_pk.want = 1;
            arn_probe.stops++;
            return;
        }
        arn_pk.done = 0, arn_pk.ready = 0;
    }
    if (!arn_pk.ready) {
        if (!ARN_MV_READY(arn.mk))
            return;                                     /* (its client gated for it: a pass later, a send line's bus) */
        arn_pk.ready = 1;
        ARN_MV_SYNC();
        arn_pk.t0 = ARN_US();
    }
#ifdef ARN_MV_STEP                                      /* (the host tests: a window a pass, the audio in between) */
    arn_mv_window();
#else
    while (arn_pk.done < arn_cur[arn.mk].len)
        arn_mv_window();                                /* (the interrupts on between two windows) */
#endif
    if (arn_pk.done < arn_cur[arn.mk].len)
        return;
    k = arn.mk;
    fm1_irq_off();
    if (arn_has_next[k] && arn_next[k].off == arn_cur[k].off)
        arn_next[k].off = arn.mo;                       /* (its own block reserved meanwhile: where it is now) */
    arn_cur[k].off = arn.mo;
    arn.mk = ARN_NO;
    ARN_MV_MOVED(k, arn_mem + arn_cur[k].off);
    fm1_irq_on();
    {
        uint32_t us = (uint32_t)(ARN_US() - arn_pk.t0);
        if (us > arn_probe.max_us)
            arn_probe.max_us = us;
    }
    arn_probe.moves++;
    if (++arn_pk.i >= arn_pk.n)
        arn_pk.load = 0;
}
