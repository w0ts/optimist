/* SPDX-License-Identifier: GPL-3.0-only */
/* The STORAGE reserve (seq/undo.c stg_*, 2026-11-01: the save / load staging buffers lent): a block past the arena and
 * its undo keep, the undo history's third part while nothing stores, storage's from a buffer's first use. On the host,
 * with an arena of 8 KB (FELUCCA_ARENA_BYTES) and the cache RAM's ways a buffer (FELUCCA_CACHE_RAM, as undo_cache_test):
 *   lent       given to the history once storage left it alone STG_LINGER_MS, stopped, nothing pinned; the ring its
 *              three parts, sessions into the reserve each undone and redone bit-exact
 *   taken      stopped (a save, the autosave): the ring turned, only the oldest levels past the ring without the
 *              reserve dropped, every kept one bit-exact; the reserve written all over (a record staged), the history
 *              as it was; playing: the oldest levels in the way dropped (all when the newest are), never a wrong one
 *   held       while used, while playing, while pinned (a restore), before the linger: never given back
 *   cycles     100 take / give-back cycles with sessions between: the ring's size back each time, every level exact
 *   arena full every block placed (the arena refuses more): the reserve still there, outside the arena
 * Exit status: failed checks. */
#define FELUCCA_STG_LEND 1                             /* (the device's: engines.c turns it on there alone) */
#ifndef FELUCCA_CACHE_RAM
#define FELUCCA_CACHE_RAM 0
#endif
#include "../firmware/hal/fm1_cache.h"
#define main hostsim_main
#include "hostsim.c"
#undef main

#ifndef STG_TEST_N
#define STG_TEST_N 16600u                              /* (mots-config's reserve, 16,600 B) */
#endif
static uint32_t stg_bytes(void) { return STG_TEST_N; }
static uint8_t pinned;
static int stg_quiet(void) { return !song.playing && !transport_req; }
static int stg_pinned(void) { return pinned; }

static int fails;
static void check(int ok, const char *what)
{
    printf("stg lend: %-96s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

typedef struct {
    step_t st[NTRK][NSTEP];
    int16_t len[NTRK], div[NTRK];
} state_t;
static void state_get(state_t *s)
{
    uint32_t i;
    memset(s, 0, sizeof *s);
    for (i = 0; i < NTRK; i++) {
        memcpy(s->st[i], trk[i].step, sizeof s->st[i]);
        s->len[i] = trk[i].p[P_SLEN];
        s->div[i] = trk[i].p[P_SDIV];
    }
}
static int state_is(const state_t *s)
{
    state_t now;
    state_get(&now);
    return !memcmp(&now, s, sizeof now);
}
static uint32_t rs = 777u;
static uint32_t rnd(void)
{
    rs = rs * 1664525u + 1013904223u;
    return rs >> 8;
}
static void session(uint32_t t, uint32_t nsteps)
{
    uint32_t k;
    undo_mark(&trk[t], (undo_sess += 4u) | 3u);
    for (k = 0; k < nsteps; k++) {
        uint8_t *b = (uint8_t *)&trk[t].step[rnd() % NSTEP];
        uint32_t j;
        for (j = 0; j < sizeof(step_t); j++)
            b[j] = (uint8_t)rnd();
    }
    undo_close();
}
#define NS 2000u
static state_t S[NS + 1];
static uint32_t run(int (*stop)(void), uint32_t most)
{
    uint32_t i;
    state_get(&S[0]);
    for (i = 1; i <= most; i++) {
        session(rnd() % 3u, 2u + rnd() % 12u);
        state_get(&S[i]);
        if (stop && stop())
            break;
    }
    return i <= most ? i : most;
}
static uint32_t more(uint32_t c, uint32_t n)            /* n sessions after the c of S: -> c + n */
{
    uint32_t i;
    for (i = 0; i < n && c < NS; i++) {
        session(rnd() % 3u, 2u + rnd() % 12u);
        state_get(&S[++c]);
    }
    return c;
}
static int back_and_forth(uint32_t c)
{
    uint32_t i, n, m, tk;
    int ok;
    undo_status(&n, &m, &tk);
    ok = n == m && m <= c;
    for (i = 0; i < m && ok; i++)
        ok &= undo_apply(0) == 1 && state_is(&S[c - 1u - i]);
    for (i = 0; i < m && ok; i++)
        ok &= undo_apply(1) == 1;
    return ok && state_is(&S[c]);
}
static uint32_t levels(void)
{
    uint32_t n, m, tk;
    undo_status(&n, &m, &tk);
    return m;
}
static uint32_t ring_r(void) { return undo_h.size - undo_h.len[2]; }      /* (where the reserve starts in the ring) */
static int in_reserve(void) { return undo_h.all_b && undo_h.head + undo_h.all_b > ring_r(); }   /* (records in it) */
static int wrapped(void) { return undo_h.head + undo_h.all_b > undo_h.size; }

static int in(const void *p, const void *lo, uint32_t n)
{
    return (const uint8_t *)p >= (const uint8_t *)lo && (const uint8_t *)p < (const uint8_t *)lo + n;
}

static void reset(void)
{
    uint32_t i;
    host_tracks_init();
    for (i = 0; i < NTRK; i++)
        steps_clear(&trk[i]);
    song.rec = 0, song.playing = 0, transport_req = 0;
    rec_wait = 0;
    pinned = 0;
    fm1_in.notes = fm1_in.buttons = 0;
#if FELUCCA_CACHE_RAM
    fm1_cache.status = FM1_CR_OK;
#endif
    memset(&undo_h, 0, sizeof undo_h);                  /* (a power-on: the ring set up at the first edit) */
    stg_out = 0, stg_mem = 0, stg_ms = 0;
    fm1_ms = 1000u;
}
static void idle(uint32_t ms)                           /* the main loop's passes for ms */
{
    uint32_t i;
    for (i = 0; i < ms; i += 10u) {
        fm1_ms += 10u;
        stg_poll();
    }
}
static void stage_all(void)                             /* a record staged: every byte of the reserve written */
{
    memset(stg_p(0), 0x5A, STG_TEST_N);
}

static void t_lend(void)
{
    uint32_t c, size0;
    char what[120];
    reset();
    (void)stg_p(0);                                     /* (the boot's storage: persist_boot, the autosave's resume) */
    check(!stg_lent(), "after the boot's use: storage's");
    idle(STG_LINGER_MS - 20u);
    check(!stg_lent(), "not given back before STG_LINGER_MS");
    idle(40u);
    check(stg_lent(), "given to the undo history after STG_LINGER_MS, stopped, nothing pinned");
    undo_ready();
    size0 = undo_h.size;
    check(undo_h.ready && undo_h.len[2] == STG_TEST_N && undo_h.seg[2] == stg_mem &&
          size0 == undo_h.len[0] + undo_h.len[1] + STG_TEST_N, "the ring's third part: the reserve, all of it");
    check(!in((const uint8_t *)stg_mem, arn_mem, ARN_BYTES), "the reserve: outside the arena");
    c = run(in_reserve, NS);
    snprintf(what, sizeof what, "%u sessions until records are in the reserve, none dropped: each bit-exact", (unsigned)c);
    check(in_reserve() && levels() == c && back_and_forth(c), what);
    /* a save, stopped: taken back */
    stage_all();
    snprintf(what, sizeof what, "taken stopped: the ring without it (%u B), %u of %u levels kept, each bit-exact",
             (unsigned)undo_h.size, (unsigned)levels(), (unsigned)c);
    check(!stg_lent() && undo_h.len[2] == 0u && undo_h.size == size0 - STG_TEST_N && levels() > 0u &&
          levels() <= c && back_and_forth(c), what);
    idle(STG_LINGER_MS + 20u);
    check(stg_lent() && undo_h.size == size0, "given back: the ring its size again");
    c = run(0, NS);
    snprintf(what, sizeof what, "%u more sessions round the three parts: every level kept bit-exact", (unsigned)c);
    check(wrapped() || undo_h.head > 0u, "the ring wrapped round (its oldest levels dropped)");
    check(back_and_forth(c), what);
    stage_all();
    snprintf(what, sizeof what, "taken with the ring wrapped (turned first): %u levels kept, each bit-exact",
             (unsigned)levels());
    check(levels() > 0u && back_and_forth(c), what);
}

static void t_held(void)
{
    reset();
    (void)stg_p(0);
    song.playing = 1;
    idle(STG_LINGER_MS * 2u);
    check(!stg_lent(), "held while playing");
    song.playing = 0, pinned = 1;
    idle(STG_LINGER_MS * 2u);
    check(!stg_lent(), "held while pinned (a restore, an editor write: proj_tmp_lent)");
    pinned = 0;
    idle(STG_LINGER_MS / 2u);
    (void)stg_p(0);                                     /* (used again: the linger starts again) */
    idle(STG_LINGER_MS - 20u);
    check(!stg_lent(), "used again: kept the linger's time from its last use");
    idle(40u);
    check(stg_lent(), "then given back");
}

static void t_playing(void)
{
    uint32_t c;
    char what[120];
    reset();
    (void)stg_p(0);
    idle(STG_LINGER_MS + 20u);
    undo_ready();
    c = run(0, 40u);
    check(stg_lent() && !in_reserve(), "lent, the history not in it (40 sessions)");
    song.playing = 1;
    idle(10u);
    check(!stg_lent() && undo_h.len[2] == 0u && levels() == c && back_and_forth(c),
          "the transport plays: storage's again at once, every level kept bit-exact");
    song.playing = 0;
    reset();
    (void)stg_p(0);
    idle(STG_LINGER_MS + 20u);
    undo_ready();
    c = run(in_reserve, NS);
    song.playing = 1;                                   /* (a pattern launch, a song's part staged: no turn) */
    stage_all();
    snprintf(what, sizeof what, "taken playing: no turn, %u of %u levels kept (the oldest in the way dropped), each bit-exact",
             (unsigned)levels(), (unsigned)c);
    check(!stg_lent() && undo_h.len[2] == 0u && back_and_forth(c), what);
    song.playing = 0;
}

static void t_cycles(void)
{
    uint32_t i, c, size0 = 0, ok = 1, lost = 0, was;
    char what[120];
    reset();
    (void)stg_p(0);
    idle(STG_LINGER_MS + 20u);
    undo_ready();
    size0 = undo_h.size;
    state_get(&S[0]);
    c = 0;
    for (i = 0; i < 100u && ok; i++) {
        c = more(c, 5u + rnd() % 15u);
        ok &= back_and_forth(c);
        was = levels();
        stage_all();                                    /* (a save) */
        ok &= !stg_lent() && undo_h.size == size0 - STG_TEST_N && back_and_forth(c);
        lost += levels() < was;
        idle(STG_LINGER_MS + 20u);                      /* (given back) */
        ok &= stg_lent() && undo_h.size == size0 && undo_h.len[2] == STG_TEST_N;
    }
    snprintf(what, sizeof what, "100 saves with sessions between: the ring's size back each time, every level bit-exact"
             " (%u saves dropped old levels)", (unsigned)lost);
    check(ok && i == 100u, what);
}

static void t_full(void)
{
    uint32_t k, placed = 0, n = (ARN_BYTES / ARN_PARTS + ARN_ALIGN - 1u) & ~(ARN_ALIGN - 1u);
    reset();
    for (k = 0; k < ARN_PARTS; k++)
        if (!arn_reserve(k, n) && arn_take(k))
            placed++;
    check(placed && arn_free_bytes() < n, "the arena full (no room for another block)");
    stage_all();
    check(stg_mem && !in(stg_mem, arn_mem, ARN_BYTES), "a save then: the reserve there all the same, outside the arena");
    for (k = 0; k < ARN_PARTS; k++)
        arn_give(k);
    (void)placed;
}

int main(void)
{
    t_lend();
    t_held();
    t_playing();
    t_cycles();
    t_full();
    printf("stg lend: %d failed\n", fails);
    return fails;
}
