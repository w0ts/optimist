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
#define UNDO_MIN 1152u                   /* build.py refuses a smaller ring (a record is up to 1,097 B) */

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
} undo;
static uint32_t undo_sess = 1;           /* UI sessions (seq.c: recording passes use the track's pass) */
static volatile uint8_t undo_isr;        /* events_block is running (the audio ISR): IRQs stay as they are */
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

#else
/* ------------------------------------------------------------ history --- */
#define UREC_LINK 0x80u                  /* header byte 0: undone / redone with the record before it */
#define UREC_HEAD 2u                     /* [trk | pmask << 2 | LINK] [steps n] */
#define UREC_TAIL 2u                     /* [size lo] [size hi] */
#define UREC_STEP (1u + sizeof(step_t))  /* [index] [the step's 10 bytes] */
#if FELUCCA_AUTO
#define UREC_AL 0x10u                    /* header byte 0 bit 4: the automation list kept (sizeof(auto_list_t)) */
#define UREC_MAX (UREC_HEAD + NSTEP * UREC_STEP + UNDO_NP * 2u + (uint32_t)sizeof(auto_list_t) + UREC_TAIL)
#else
#define UREC_MAX (UREC_HEAD + NSTEP * UREC_STEP + UNDO_NP * 2u + UREC_TAIL)
#endif
_Static_assert(UREC_MAX <= UNDO_MIN, "undo: the smallest ring holds a record");
static struct {
    uint8_t *seg[2];                     /* the ring: pool's leftover, then main RAM's (one after the other) */
    uint32_t len[2];
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
    undo_h.size = a + b;
    undo_h.ready = 1;
    undo_h.head = undo_h.cur = undo_h.tail = 0;
    undo_h.done_b = undo_h.all_b = 0;
    undo_h.n_done = undo_h.n_all = 0;
    undo_h.run_ok = 0;
    undo.valid = 0;
    return undo_h.size;
}
#ifdef __PI32V2__
extern uint8_t _undo_pool_lo[], _undo_pool_hi[], _undo_ram_lo[], _undo_ram_hi[];   /* app.ld */
#define UNDO_ARENA() undo_arena_set(_undo_pool_lo, _undo_pool_hi, _undo_ram_lo, _undo_ram_hi, FELUCCA_UNDO_CAP)
#else
#ifndef UNDO_HOST_ARENA
#define UNDO_HOST_ARENA 16384u           /* the host build: a stand-in for the leftover memory */
#endif
static uint8_t undo_host_arena[UNDO_HOST_ARENA];
#define UNDO_ARENA() undo_arena_set(undo_host_arena, undo_host_arena + sizeof undo_host_arena, 0, 0, FELUCCA_UNDO_CAP)
#endif
static void undo_ready(void)
{
    if (!undo_h.ready)
        UNDO_ARENA();
}

/* the ring's byte at offset o (< 2 x size) */
static uint8_t *ub(uint32_t o)
{
    if (o >= undo_h.size)
        o -= undo_h.size;
    return o < undo_h.len[0] ? undo_h.seg[0] + o : undo_h.seg[1] + (o - undo_h.len[0]);
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
#define REC_SX(h) ((h) & UREC_AL ? (uint32_t)sizeof(auto_list_t) : 0u)
#else
#define REC_SX(h) 0u
#endif
static uint32_t rec_size(uint32_t o)     /* the record starting at o, from its header */
{
    return UREC_HEAD + *ub(o + 1u) * UREC_STEP + popc2((*ub(o) >> 2) & 3u) * 2u + REC_SX(*ub(o)) + UREC_TAIL;
}
static void undo_drop_oldest(void)       /* the oldest level goes (with the records linked to it) */
{
    do {
        uint32_t sz = rec_size(undo_h.head);
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
    if (!n && !pm)
        return;                                         /* nothing changed: no level (redo stays) */
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
    while (undo_h.size - undo_h.all_b < sz)
        undo_drop_oldest();
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
    UNDO_LOCK();
    undo_ready();
    undo_commit();
    undo_h.run_ok = 0;
    UNDO_UNLOCK();
}
static void undo_end(uint32_t sess)      /* session sess is complete (a held layer let go): its level now */
{
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
