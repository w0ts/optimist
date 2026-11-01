/* SPDX-License-Identifier: GPL-3.0-only */
/* Undo / redo of the patterns (EDIT + OCT- / OCT+). seq.c includes this.
 *
 * A session is one change as the player sees it: a recording pass of a track (UNDO_REC), one erase,
 * one held layer of step edits or tools, a step entry, a clear, a free take, NEW. Its first change
 * is preceded by undo_mark(t, sess): a copy of the track's pattern (64 steps, synth or drum: both are
 * 10-byte steps of the same union) and of its pattern parameters. Further marks of the same track and
 * session cost nothing.
 *
 * FELUCCA_UNDO_HISTORY 1 (the default): a history of many levels. The session stays open until
 * another one is marked, an undo / redo asks, or the held layer that made it is let go
 * (undo_close); then it is compared with the track and only what differs is kept, as one record of a
 * ring: the track, then (step index, its 10 bytes as they were) for each step changed, the changed
 * pattern parameters (LEN, DIV) and the record's size (to walk back). Undo swaps a record with the
 * track (the record then holds what undo took away), redo swaps it back: no second copy. Records of
 * one session that marked several tracks (NEW) are linked and undone / redone together, one level.
 * A new change drops what was undone (no redo after it); a full ring forgets its oldest levels.
 * The ring is the memory nobody else uses, sized by the linker (app.ld _undo_*): the pool after
 * .pool up to its last 8 KiB (build.py's spare), then main RAM after .bss; FELUCCA_UNDO_CAP (bytes)
 * caps it. Loading a project (or a song section) clears the history: its steps are other steps.
 * The swaps run with the audio ISR held off (from the UI) or inside it (events_block): a safe
 * point, as the single level did.
 *
 * FELUCCA_UNDO_HISTORY 0: the single level of SLOOP 2.x, as it was: a copy of one track's pattern
 * and LEN; undo and redo swap it with the track.
 *
 * With the automation store (FELUCCA_AUTO, auto.h: the track's list of events, 385 B: locks, motion, nudges, fills,
 * chance) a mark also copies the track's list, and undo / redo swap it with the steps: a step edit of a lock, a
 * nudge, a fill or a chance, and the motion a recording pass wrote, is undone with it (both UIs mark before such an
 * edit). The history keeps the list only when it changed (header bit 4). Without the store nothing here changes. */
#ifndef FELUCCA_UNDO_HISTORY
#define FELUCCA_UNDO_HISTORY 1
#endif
#ifndef FELUCCA_UNDO_CAP
#define FELUCCA_UNDO_CAP 0u              /* the history's ring at most this many bytes, 0 = all there is */
#endif
#define UNDO_MIN 1408u                   /* build.py refuses a smaller ring (a record is up to 1,353 B) */

#if FELUCCA_UNDO_HISTORY
#define UNDO_NP 2u                       /* the pattern parameters kept with the steps */
static const uint8_t UNDO_P[UNDO_NP] = {P_SLEN, P_SDIV};
#else
#define UNDO_NP 1u
static const uint8_t UNDO_P[UNDO_NP] = {P_SLEN};
#endif

/* the marked pattern: single level, what undo brings back; history, the open session's start */
static struct {
    uint8_t valid, undone, trk;
    int16_t pp[UNDO_NP];
    uint32_t sess;
    step_t st[NSTEP];
#if FELUCCA_AUTO
    auto_list_t al;                      /* the track's automation as it was */
#endif
#if GEN_BUILT
    uint32_t gon;                        /* its note generators' ON states (gen.c gen_on_mask) */
#endif
} undo IN_POOL;
static uint32_t undo_sess = 1;           /* UI sessions (seq.c: recording passes use the track's pass) */
static volatile uint8_t undo_isr;        /* events_block is running (the audio ISR): IRQs stay as they are */
#ifndef STG_LEND
#define STG_LEND 0
#endif
#if STG_LEND && FELUCCA_UNDO_HISTORY
static volatile uint8_t undo_frozen;     /* the main loop moves the ring's bytes (the STORAGE reserve, below): the audio's
                                          * marks, undos and clears wait (none: they are skipped, the transport stopped) */
#define UNDO_FROZEN() undo_frozen
#else
#define UNDO_FROZEN() 0
#endif
#define UNDO_REC(t) (((t)->pass << 2) | 1u)      /* a recording pass of track t */
static uint32_t undo_erase_sess;

static void undo_snap(const track_t *t, uint32_t i, uint32_t sess)
{
    uint32_t k;
    memcpy(undo.st, t->step, sizeof undo.st);
    for (k = 0; k < UNDO_NP; k++)
        undo.pp[k] = t->p[UNDO_P[k]];
#if FELUCCA_AUTO
    memcpy(&undo.al, AUTO_L(i), sizeof undo.al);
#endif
#if GEN_BUILT
    undo.gon = gen_on_mask(i);
#endif
    undo.trk = (uint8_t)i;
    undo.sess = sess;
    undo.valid = 1;
    undo.undone = 0;
}

#if !FELUCCA_UNDO_HISTORY
/* ------------------------------------------------------- single level --- */
static void undo_mark(const track_t *t, uint32_t sess)
{
    uint32_t i = (uint32_t)(t - trk);
    if (undo.valid && !undo.undone && undo.trk == i && undo.sess == sess)
        return;                                          /* (this session is marked already) */
    undo_snap(t, i, sess);
}
static void undo_close(void) {}
static void undo_end(uint32_t sess) { (void)sess; }
static void undo_clear(void) {}          /* (as SLOOP 2.x: a project load keeps the level) */
/* undo (redo = 0) / redo: the marked pattern and the one now swap places; 0 = nothing to do */
static int undo_apply(int redo)
{
    track_t *t;
    int16_t len;
    if (!undo.valid || (uint32_t)!!redo != undo.undone)
        return 0;
    t = &trk[undo.trk % NTRK];
    fm1_irq_off();
    {
        uint32_t i;
        for (i = 0; i < NSTEP; i++) {
            step_t x = t->step[i];
            t->step[i] = undo.st[i];
            undo.st[i] = x;
        }
    }
    len = t->p[P_SLEN];
    t->p[P_SLEN] = undo.pp[0];
    undo.pp[0] = len;
#if FELUCCA_AUTO
    {
        auto_list_t x = *AUTO_L(undo.trk);              /* (the automation swaps with the steps) */
        *AUTO_L(undo.trk) = undo.al;
        undo.al = x;
        auto_touch(undo.trk % NTRK);
    }
#endif
#if GEN_BUILT
    {
        uint32_t g = gen_on_mask(undo.trk);             /* (the generators' ON states swap with the steps) */
        gen_on_put(undo.trk, undo.gon);
        undo.gon = g;
    }
#endif
    undo.undone = (uint8_t)!redo;
    fm1_irq_on();
    return 1;
}
static int undo_can(int redo) { return undo.valid && (uint32_t)!!redo == undo.undone; }
static void undo_status(uint32_t *n, uint32_t *m, uint32_t *tk)
{
    *n = undo.valid && !undo.undone;
    *m = undo.valid;
    *tk = undo.trk;
}

#if PART_DROPS
static void undo_yield(uint8_t *lo) { (void)lo; }       /* (one level: no ring to give way) */
#endif
#else
/* ------------------------------------------------------------ history --- */
#define UREC_LINK 0x80u                  /* header byte 0: undone / redone with the record before it */
#define UREC_HEAD 2u                     /* [trk | pmask << 2 | LINK] [steps n] */
#define UREC_TAIL 2u                     /* [size lo] [size hi] */
#define UREC_STEP (1u + sizeof(step_t))  /* [index] [the step's 14 bytes: its own note ends too] */
#define UREC_GEN 0x20u                   /* header byte 0 bit 5: the note generators' ON states kept (4 bytes, after the list) */
#if GEN_BUILT
#define UREC_GEN_N 4u
#else
#define UREC_GEN_N 0u
#endif
#if FELUCCA_AUTO
#define UREC_AL 0x10u                    /* header byte 0 bit 4: the automation list kept (sizeof(auto_list_t)) */
#define UREC_MAX (UREC_HEAD + NSTEP * UREC_STEP + UNDO_NP * 2u + (uint32_t)sizeof(auto_list_t) + UREC_GEN_N + UREC_TAIL)
#else
#define UREC_MAX (UREC_HEAD + NSTEP * UREC_STEP + UNDO_NP * 2u + UREC_GEN_N + UREC_TAIL)
#endif
_Static_assert(UREC_MAX <= UNDO_MIN, "undo: the smallest ring holds a record");
static struct {
    uint8_t *seg[3];                     /* the ring: pool's leftover, then main RAM's (one after the other); the third the
                                          * STORAGE reserve while storage lends it (STG_LEND), else empty */
    uint32_t len[3];
    uint32_t size;                       /* bytes, 0 = none */
    uint8_t ready;                       /* the ring was set up (undo_ready) */
    uint8_t last_trk;                    /* the track of the last undo / redo (its message) */
    uint32_t head, cur, tail;            /* offsets: the oldest record, the end of the last applied one, the end */
    uint32_t done_b, all_b;              /* bytes head..cur, head..tail */
    uint32_t n_done, n_all;              /* levels (linked records: one): applied, applied + undone */
    uint32_t run_sess;                   /* marks of one session in a row (NEW: one track after another) */
    uint8_t run_ok, run_pushed;          /* .. a record of that run is the newest: the next one links to it */
} undo_h;

/* the ring's segments from the leftover pool and main RAM (lo..hi; hi <= lo: none), capped (0 = no cap):
 * the pool first. Returns the ring's size. (The linker's symbols on the device; the tests simulate them.) */
static uint32_t undo_arena_set(uint8_t *plo, uint8_t *phi, uint8_t *rlo, uint8_t *rhi, uint32_t cap)
{
    uint32_t a = phi > plo ? (uint32_t)(phi - plo) : 0u, b = rhi > rlo ? (uint32_t)(rhi - rlo) : 0u;
    if (cap) {
        if (a > cap)
            a = cap;
        if (b > cap - a)
            b = cap - a;
    }
    undo_h.seg[0] = a ? plo : rlo;       /* (an empty pool part: main RAM is the only segment) */
    undo_h.len[0] = a ? a : b;
    undo_h.seg[1] = rlo;
    undo_h.len[1] = a ? b : 0u;
    undo_h.seg[2] = 0;
    undo_h.len[2] = 0;
    undo_h.size = a + b;
    undo_h.ready = 1;
    undo_h.head = undo_h.cur = undo_h.tail = 0;
    undo_h.done_b = undo_h.all_b = 0;
    undo_h.n_done = undo_h.n_all = 0;
    undo_h.run_ok = 0;
    undo.valid = 0;
    return undo_h.size;
}
#if PART_DROPS
/* With the arena (the user, 2026-10-29: "arena first, undo gets the rest"; engines.c ARN_FREE): the ring is the SRAM
 * after the arena's highest block up to the arena's end and ARN_UNDO_KEEP past it (on the device the free SRAM's end,
 * the build's spare left). It gives way when a block is placed past its start (undo_yield, from core/arena.c: its
 * oldest levels dropped, all of them when the newest are in the way) and takes the room back when the arena lets it
 * go (undo_regrow, before a record is kept) */
#define UNDO_RING_END (arn_mem + ARN_BYTES + ARN_UNDO_KEEP)
#if FELUCCA_CACHE_RAM
/* the cache RAM's ways after it when their test passed (hal/fm1_cache.h fm1_cache_ram_lo / _free; the trial's on
 * fm1-base's plat_cache_ram): its second segment, which only the main loop reads (the arena's blocks stay in the SRAM:
 * whether the second core and DMA read the ways has not been measured). Its least in the SRAM is that much less
 * (engines.c ARN_UNDO_KEEP), the arena that much more; a failed test: none, the least all in the SRAM as without */
#define UNDO_ARENA() undo_arena_set(arn_mem + ((arn_top() + 3u) & ~3u), UNDO_RING_END, fm1_cache_ram_lo(), \
                                    fm1_cache_ram_lo() + fm1_cache_ram_free(), 0u)
#else
#define UNDO_ARENA() undo_arena_set(arn_mem + ((arn_top() + 3u) & ~3u), UNDO_RING_END, 0, 0, 0u)
#endif
static void undo_drop_oldest(void);
static void undo_forget(void);
static void undo_yield(uint8_t *lo)
{
    uint32_t d;
    if (!undo_h.ready || lo <= undo_h.seg[0])
        return;
    d = (uint32_t)(lo - undo_h.seg[0]);
    d = (d + 3u) & ~3u;
    if (d >= undo_h.len[0]) {
        undo_forget();
        d = undo_h.len[0];
    } else {
        while (undo_h.all_b && undo_h.head < d && undo_h.head + undo_h.all_b <= undo_h.size && undo_h.n_done &&
               undo_h.done_b)
            undo_drop_oldest();                         /* (the oldest first, while they are in the way) */
        if (undo_h.all_b && (undo_h.head < d || undo_h.head + undo_h.all_b > undo_h.size))
            undo_forget();                              /* (the ring wraps: the newest in the way too, every level) */
    }
    if (undo_h.all_b) {
        undo_h.head -= d, undo_h.cur -= d, undo_h.tail -= d;
    } else {
        undo_h.head = undo_h.cur = undo_h.tail = 0;     /* (the open session is not in the ring: kept) */
    }
    undo_h.seg[0] += d;
    undo_h.size -= d;
    undo_h.len[0] -= d;                                 /* (the cache RAM's part, seg 1, as it was) */
}
static void undo_regrow(void)                           /* the room the arena no longer uses: the ring's again */
{
    uint8_t *lo = arn_mem + ((arn_top() + 3u) & ~3u);
    uint32_t d;
    if (!undo_h.ready || lo >= undo_h.seg[0] || undo_h.head + undo_h.all_b > undo_h.size)
        return;                                         /* (a wrapped ring keeps its size until it is empty) */
    d = (uint32_t)(undo_h.seg[0] - lo);
    if (undo_h.all_b)
        undo_h.head += d, undo_h.cur += d, undo_h.tail += d;
    undo_h.seg[0] = lo;
    undo_h.size += d;
    undo_h.len[0] += d;
}
#elif defined(__PI32V2__)
extern uint8_t _undo_pool_lo[], _undo_pool_hi[], _undo_ram_lo[], _undo_ram_hi[];   /* app.ld */
#if FELUCCA_CACHE_RAM
/* EXPERIMENTAL cache RAM (hal/fm1_cache.h fm1_cache_ram_lo / _free; the trial's on fm1-base's plat_cache_ram): when its
 * boot test passed, its n x 4 KiB (after .cache_ram) are the second segment instead of main RAM's leftover; else main
 * RAM's as without it */
#define UNDO_ARENA() (fm1_cache_ram_ok()                                                                              \
    ? undo_arena_set(_undo_pool_lo, _undo_pool_hi, fm1_cache_ram_lo(), fm1_cache_ram_lo() + fm1_cache_ram_free(),  \
                     FELUCCA_UNDO_CAP)                                                                             \
    : undo_arena_set(_undo_pool_lo, _undo_pool_hi, _undo_ram_lo, _undo_ram_hi, FELUCCA_UNDO_CAP))
#else
#define UNDO_ARENA() undo_arena_set(_undo_pool_lo, _undo_pool_hi, _undo_ram_lo, _undo_ram_hi, FELUCCA_UNDO_CAP)
#endif
#else
#ifndef UNDO_HOST_ARENA
#define UNDO_HOST_ARENA 16384u           /* the host build: a stand-in for the leftover memory */
#endif
static uint8_t undo_host_arena[UNDO_HOST_ARENA];
#define UNDO_ARENA() undo_arena_set(undo_host_arena, undo_host_arena + sizeof undo_host_arena, 0, 0, FELUCCA_UNDO_CAP)
#endif
#if STG_LEND
static void stg_ring_init(void);
#endif
static void undo_ready(void)
{
    if (!undo_h.ready) {
        UNDO_ARENA();
#if STG_LEND
        stg_ring_init();                                /* (the STORAGE reserve, while the history holds it) */
#endif
    }
}

/* the ring's byte at offset o (< 2 x size) */
static uint8_t *ub(uint32_t o)
{
    if (o >= undo_h.size)
        o -= undo_h.size;
    if (o < undo_h.len[0])
        return undo_h.seg[0] + o;
    o -= undo_h.len[0];
#if STG_LEND
    if (o >= undo_h.len[1])
        return undo_h.seg[2] + (o - undo_h.len[1]);
#endif
    return undo_h.seg[1] + o;
}
static uint32_t uwrap(uint32_t o) { return o >= undo_h.size ? o - undo_h.size : o; }
static uint32_t uback(uint32_t o, uint32_t n) { return o >= n ? o - n : o + undo_h.size - n; }
static void ring_put(uint32_t o, const void *src, uint32_t n)
{
    const uint8_t *s = (const uint8_t *)src;
    while (n--)
        *ub(o++) = *s++;
}
/* the ring's bytes at o and memory p trade places */
static void ring_swap(uint32_t o, void *p, uint32_t n)
{
    uint8_t *d = (uint8_t *)p;
    while (n--) {
        uint8_t *r = ub(o++), x = *r;
        *r = *d;
        *d++ = x;
    }
}
static uint32_t popc2(uint32_t m) { return (m & 1u) + ((m >> 1) & 1u); }
#if FELUCCA_AUTO
#define REC_SX(h) (((h) & UREC_AL ? (uint32_t)sizeof(auto_list_t) : 0u) + ((h) & UREC_GEN ? UREC_GEN_N : 0u))
#else
#define REC_SX(h) ((h) & UREC_GEN ? UREC_GEN_N : 0u)
#endif
static uint32_t rec_size(uint32_t o)     /* the record starting at o, from its header */
{
    return UREC_HEAD + *ub(o + 1u) * UREC_STEP + popc2((*ub(o) >> 2) & 3u) * 2u + REC_SX(*ub(o)) + UREC_TAIL;
}
static void undo_drop_oldest(void)       /* the oldest level goes (with the records linked to it) */
{
    uint32_t guard = 0;
    do {
        uint32_t sz = rec_size(undo_h.head);
        LOOP_GUARD(0x80, guard, 256u)
        undo_h.head = uwrap(undo_h.head + sz);
        undo_h.all_b -= sz;
        undo_h.done_b -= sz;
    } while (undo_h.all_b && (*ub(undo_h.head) & UREC_LINK));
    undo_h.n_all--;
    undo_h.n_done--;
}
static void undo_forget(void)            /* the history is empty (its records stay unread) */
{
    undo_h.head = undo_h.cur = undo_h.tail = 0;
    undo_h.done_b = undo_h.all_b = 0;
    undo_h.n_done = undo_h.n_all = 0;
    undo_h.run_ok = 0;
}

#if STG_LEND
/* ---- The STORAGE reserve (2026-11-01, the user: "lend the save/load staging"). The buffers a save, a load, the autosave,
 * a compaction, a backup or a restore, a snapshot or an editor write stage records in (storage.c st_buf, project.c
 * proj_tmp, sections.c sec_rb, stepx_log.c sx_rbuf, the automation stores of proj_tmp and of the
 * autosave: stg_bytes) are one block past the arena and its undo keep (the device: the free SRAM's last bytes, engines.c
 * STG_MEM; the host: stg_host), never the arena's: a save never finds it taken, whatever the project holds. While
 * nothing stores it is the undo history's third segment (its last bytes); the first use of a buffer takes it back
 * (stg_p, the main loop only):
 *   stopped (stg_quiet)  the ring turned so its newest byte ends where the reserve starts: only its oldest levels in the
 *                        reserve are dropped (none while the history fits the ring without it);
 *   playing              not turned (the audio may place a part's block over the ring's start meanwhile): the oldest
 *                        levels in the way dropped, every level when the newest are in it.
 * It goes back to the undo history once storage has not used it for STG_LINGER_MS, the transport stopped, nothing
 * holding a buffer across passes (proj_tmp_lent: a restore, an editor write): stg_poll, from autosave_tick every pass,
 * which also takes it back as soon as the transport plays while the history has nothing in it (so only a history that
 * grew past the ring without it while stopped can lose levels to a use while playing).
 * A buffer taken back is zeroed (as the pool's were at power-on); none keeps its bytes across a give-back. */
#ifndef STG_LINGER_MS
#define STG_LINGER_MS 3000u
#endif
static uint8_t *stg_mem;                                /* (the reserve: set at the first use) */
static uint8_t stg_out;                                 /* 1: the undo history holds it */
static uint32_t stg_ms;                                 /* the last use */
static int stg_quiet(void);                             /* (storage/project.c: the transport stopped, nothing staged) */
static int stg_pinned(void);                            /* (storage/project.c: a buffer held across passes) */
#ifdef STG_MEM
#define STG_BASE() STG_MEM
#else
#ifndef STG_HOST
#define STG_HOST 24576u                                 /* (the host's stand-in: stg_bytes at most) */
#endif
static uint8_t stg_host[STG_HOST] __attribute__((aligned(8)));
#define STG_BASE() stg_host
#endif
static void ur_rev(uint32_t a, uint32_t b)              /* the ring's bytes a .. b - 1 in the opposite order */
{
    while (a + 1u < b) {
        uint8_t *x = ub(a), *y = ub(--b), t = *x;
        *x = *y, *y = t;
        a++;
    }
}
static void ur_turn(uint32_t r)                         /* every byte of the ring r places on (the main loop, stopped) */
{
    if (!r || r >= undo_h.size)
        return;
    undo_frozen = 1;
    ur_rev(0, undo_h.size), ur_rev(0, r), ur_rev(r, undo_h.size);
    undo_h.head = uwrap(undo_h.head + r), undo_h.cur = uwrap(undo_h.cur + r), undo_h.tail = uwrap(undo_h.tail + r);
    undo_frozen = 0;
}
static void ur_ends(void)                               /* cur and tail again from head (the ring not wrapped) */
{
    if (!undo_h.all_b) {
        undo_h.head = undo_h.cur = undo_h.tail = 0;
        return;
    }
    undo_h.cur = uwrap(undo_h.head + undo_h.done_b);
    undo_h.tail = uwrap(undo_h.head + undo_h.all_b);
}
/* the reserve out of the ring (its last len[2] bytes): the oldest levels in the way dropped (quiet: turned first) */
static void stg_vacate(int quiet)
{
    uint32_t n = undo_h.len[2], r;
    if (!undo_h.ready || !n)
        return;
    r = undo_h.size - n;                                /* (where the reserve starts in the ring) */
    if (quiet && undo_h.all_b && undo_h.head + undo_h.all_b > r)
        ur_turn(r >= undo_h.tail ? r - undo_h.tail : r + undo_h.size - undo_h.tail);   /* (the newest byte ends at r) */
    IRQ_SAVE(ie);
    while (undo_h.all_b && undo_h.head + undo_h.all_b > r) {
        if (undo_h.n_done && undo_h.done_b) {
            undo_drop_oldest();
        } else {
            undo_forget();
            break;
        }
    }
    undo_h.size = r;
    undo_h.len[2] = 0;
    ur_ends();
    IRQ_RESTORE(ie);
}
/* the reserve to the ring again, its last bytes (the main loop, stopped): a wrapped ring turned to start at 0 first */
static void stg_lend_undo(void)
{
    uint32_t n = stg_bytes();
    if (undo_h.ready) {
        if (undo_h.all_b && undo_h.head + undo_h.all_b > undo_h.size)
            ur_turn(undo_h.size - undo_h.head);
        IRQ_SAVE(ie);
        undo_h.seg[2] = stg_mem;
        undo_h.len[2] = n;
        undo_h.size += n;
        ur_ends();
        IRQ_RESTORE(ie);
    }
    stg_out = 1;
}
static void stg_ring_init(void)                         /* (undo_ready, after UNDO_ARENA) the reserve in the ring, if lent */
{
    if (stg_out) {
        undo_h.seg[2] = stg_mem;
        undo_h.len[2] = stg_bytes();
        undo_h.size += undo_h.len[2];
    }
}
/* the reserve's byte off, storage's (taken back from the undo history now if it holds it, zeroed) */
static uint8_t *stg_a(uint32_t off)                    /* the reserve's byte off, where it is (an address: nothing taken) */
{
    if (!stg_mem)
        stg_mem = STG_BASE();
    return stg_mem + off;
}
static uint8_t *stg_p(uint32_t off)
{
    if (!stg_mem)
        stg_mem = STG_BASE();
    if (undo_isr)
        return stg_mem + off;                           /* (the audio compares a buffer's address: never taken there) */
    if (stg_out) {
        stg_vacate(stg_quiet());
        stg_out = 0;
        memset(stg_mem, 0, stg_bytes());
    }
    stg_ms = fm1_ms;
    return stg_mem + off;
}
/* (every main-loop pass) the reserve back to the undo history once storage is done with it */
static void stg_poll(void)
{
    if (!stg_out && stg_mem && fm1_ms - stg_ms >= STG_LINGER_MS && stg_quiet() && !stg_pinned()) {
        stg_lend_undo();
    } else if (stg_out && !stg_quiet() &&
               !(undo_h.ready && undo_h.all_b && undo_h.head + undo_h.all_b > undo_h.size - undo_h.len[2])) {
        (void)stg_p(0);                                 /* (playing, nothing of the history in it: storage's again at no
                                                         * cost, so a launch or a song's part never drops a level) */
    }
}
static int stg_lent(void) { return stg_out; }           /* (the tests, INFO: the undo history holds it) */
#endif
/* the open session into the history: the steps and parameters that differ from the track now */
static void undo_commit(void)
{
    track_t *t;
    uint32_t i, k, n = 0, pm = 0, sz, o, link;
    uint8_t hd[2];
    if (!undo.valid)
        return;
    undo.valid = 0;
    t = &trk[undo.trk % NTRK];
    for (i = 0; i < NSTEP; i++)
        n += memcmp(&undo.st[i], &t->step[i], sizeof(step_t)) != 0;
    for (k = 0; k < UNDO_NP; k++)
        if (undo.pp[k] != t->p[UNDO_P[k]])
            pm |= 1u << k;
#if FELUCCA_AUTO
    if (memcmp(&undo.al, AUTO_L(undo.trk), sizeof undo.al))
        pm |= 4u;                                       /* (bit 2 of pm: header bit 4) */
#endif
#if GEN_BUILT
    if (undo.gon != gen_on_mask(undo.trk))
        pm |= 8u;                                       /* (bit 3 of pm: header bit 5, UREC_GEN) */
#endif
    if (!n && !pm)
        return;                                         /* nothing changed: no level (redo stays) */
#if PART_DROPS
    undo_regrow();                                      /* (the arena's room it no longer uses) */
#endif
    sz = UREC_HEAD + n * UREC_STEP + popc2(pm) * 2u + REC_SX(pm << 2) + UREC_TAIL;
    link = undo_h.run_ok && undo_h.run_sess == undo.sess && undo_h.run_pushed && undo_h.cur == undo_h.tail &&
           undo_h.n_all;                                /* (another track of the session just before) */
    undo_h.tail = undo_h.cur;                           /* a new change: what was undone goes */
    undo_h.all_b = undo_h.done_b;
    undo_h.n_all = undo_h.n_done;
    if (sz > undo_h.size) {                             /* (a ring smaller than one record: none kept) */
        undo_forget();
        return;
    }
    {
        uint32_t guard = 0;
        while (undo_h.size - undo_h.all_b < sz) {
            LOOP_GUARD(0x81, guard, 256u)
            undo_drop_oldest();
        }
    }
    if (!undo_h.all_b)
        link = 0;                                       /* (what it linked to was forgotten) */
    o = undo_h.tail;
    hd[0] = (uint8_t)((undo.trk & 3u) | pm << 2 | (link ? UREC_LINK : 0u));
    hd[1] = (uint8_t)n;
    ring_put(o, hd, 2u);
    o += 2u;
    for (i = 0; i < NSTEP; i++)
        if (memcmp(&undo.st[i], &t->step[i], sizeof(step_t))) {
            uint8_t ix = (uint8_t)i;
            ring_put(o, &ix, 1u);
            ring_put(o + 1u, &undo.st[i], sizeof(step_t));
            o += UREC_STEP;
        }
    for (k = 0; k < UNDO_NP; k++)
        if ((pm >> k) & 1u) {
            uint8_t v[2] = {(uint8_t)undo.pp[k], (uint8_t)((uint16_t)undo.pp[k] >> 8)};
            ring_put(o, v, 2u);
            o += 2u;
        }
#if FELUCCA_AUTO
    if ((pm >> 2) & 1u) {
        ring_put(o, &undo.al, sizeof undo.al);
        o += sizeof undo.al;
    }
#endif
#if GEN_BUILT
    if ((pm >> 3) & 1u) {
        ring_put(o, &undo.gon, UREC_GEN_N);
        o += UREC_GEN_N;
    }
#endif
    hd[0] = (uint8_t)sz;
    hd[1] = (uint8_t)(sz >> 8);
    ring_put(o, hd, 2u);
    undo_h.tail = uwrap(undo_h.tail + sz);
    undo_h.cur = undo_h.tail;
    undo_h.all_b += sz;
    undo_h.done_b = undo_h.all_b;
    undo_h.n_all += !link;
    undo_h.n_done = undo_h.n_all;
    undo_h.run_pushed = undo_h.run_ok && undo_h.run_sess == undo.sess;
}

/* the record at o and its track swap their steps and parameters (undo and redo alike) */
static void rec_swap(uint32_t o)
{
    uint32_t h = *ub(o), n = *ub(o + 1u), k;
    track_t *t = &trk[h & 3u];
    undo_h.last_trk = (uint8_t)(h & 3u);
    o += UREC_HEAD;
    for (; n; n--, o += UREC_STEP)
        ring_swap(o + 1u, &t->step[*ub(o) % NSTEP], sizeof(step_t));
    for (k = 0; k < UNDO_NP; k++)
        if ((h >> (2u + k)) & 1u) {
            uint8_t v[2] = {(uint8_t)t->p[UNDO_P[k]], (uint8_t)((uint16_t)t->p[UNDO_P[k]] >> 8)};
            ring_swap(o, v, 2u);
            t->p[UNDO_P[k]] = (int16_t)(uint16_t)(v[0] | v[1] << 8);
            o += 2u;
        }
#if FELUCCA_AUTO
    if (h & UREC_AL) {
        ring_swap(o, AUTO_L(h & 3u), sizeof(auto_list_t));
        auto_touch(h & 3u);
        o += sizeof(auto_list_t);
    }
#endif
#if GEN_BUILT
    if (h & UREC_GEN) {
        uint32_t g = gen_on_mask(h & 3u);
        ring_swap(o, &g, UREC_GEN_N);
        gen_on_put(h & 3u, g);
    }
#endif
}

#define UNDO_LOCK() uint32_t undo_lk = !undo_isr; if (undo_lk) fm1_irq_off()
#define UNDO_UNLOCK() if (undo_lk) fm1_irq_on()

/* a session's first change on track t comes. Marks of one session on several tracks one after another
 * (NEW) make linked records: one level. Session numbers: undo_sess (UI), UNDO_REC (a track's pass) */
static void undo_mark(const track_t *t, uint32_t sess)
{
    uint32_t i = (uint32_t)(t - trk), run;
    if (UNDO_FROZEN())
        return;                                          /* (the ring moving: this change keeps no level) */
    UNDO_LOCK();
    if (!(undo.valid && undo.trk == i && undo.sess == sess)) {    /* (else: this session is marked already) */
        run = undo_h.run_ok && undo_h.run_sess == sess && undo.valid;
        undo_ready();
        undo_commit();                                   /* (the session before: into the history) */
        if (!run) {
            undo_h.run_sess = sess;
            undo_h.run_ok = 1;
            undo_h.run_pushed = 0;
        }
        undo_snap(t, i, sess);
    }
    UNDO_UNLOCK();
}
static void undo_close(void)             /* the open session is complete */
{
    if (UNDO_FROZEN())
        return;
    UNDO_LOCK();
    undo_ready();
    undo_commit();
    undo_h.run_ok = 0;
    UNDO_UNLOCK();
}
static void undo_end(uint32_t sess)      /* session sess is complete (a held layer let go): its level now */
{
    if (UNDO_FROZEN())
        return;
    UNDO_LOCK();
    if (undo.valid && undo.sess == sess) {
        undo_ready();
        undo_commit();
        undo_h.run_ok = 0;
    }
    UNDO_UNLOCK();
}
static void undo_clear(void)             /* a project / song section loaded: no history */
{
    if (UNDO_FROZEN()) {
        undo.valid = 0;                                  /* (the ring is emptied after the move: stg_vacate's caller) */
        return;
    }
    UNDO_LOCK();
    undo_ready();
    undo.valid = 0;
    undo_forget();
    UNDO_UNLOCK();
}
/* undo (redo = 0) / redo one level; 0 = nothing to do */
static int undo_apply(int redo)
{
    int done = 0;
    if (UNDO_FROZEN())
        return 0;
    UNDO_LOCK();
    undo_ready();
    undo_commit();
    undo_h.run_ok = 0;
    if (!redo && undo_h.n_done) {
        uint32_t link;
        do {
            uint32_t end = undo_h.cur, sz = *ub(uback(end, 2u)) | (uint32_t)*ub(uback(end, 1u)) << 8;
            undo_h.cur = uback(end, sz);
            undo_h.done_b -= sz;
            link = *ub(undo_h.cur) & UREC_LINK;
            rec_swap(undo_h.cur);
        } while (link && undo_h.done_b);
        undo_h.n_done--;
        done = 1;
    } else if (redo && undo_h.n_done < undo_h.n_all) {
        do {
            uint32_t sz = rec_size(undo_h.cur);
            rec_swap(undo_h.cur);
            undo_h.cur = uwrap(undo_h.cur + sz);
            undo_h.done_b += sz;
        } while (undo_h.done_b < undo_h.all_b && (*ub(undo_h.cur) & UREC_LINK));
        undo_h.n_done++;
        done = 1;
    }
    UNDO_UNLOCK();
    return done;
}
/* something to undo (redo = 0) / redo (an open session counts as one to undo, if it changed anything) */
static int undo_can(int redo)
{
    undo_ready();
    return redo ? undo_h.n_done < undo_h.n_all && !undo.valid : undo_h.n_done || undo.valid;
}
/* levels applied n of m, the track of the last undo / redo */
static void undo_status(uint32_t *n, uint32_t *m, uint32_t *tk)
{
    *n = undo_h.n_done;
    *m = undo_h.n_all;
    *tk = undo_h.last_trk;
}
#endif
