/* SPDX-License-Identifier: GPL-3.0-only */
/* The arena, the undo history after it and the cache RAM (hal/fm1_cache.h CACHE_RAM, the user 2026-10-29: "more memory
 * from the data cache" for the arena), on the host: the cache RAM's ways a buffer (its test's result set by hand).
 *   passed    the cache RAM the history's second part: its least in the SRAM 16 KB less the cache RAM's 8 KB
 *             (engines.c ARN_UNDO_KEEP: the arena gets those 8 KB), the ring both parts; sessions until the newest
 *             records are in the cache RAM, every level bit-exact; the arena's block placed over the history's start
 *             (undo_yield): the oldest levels go, every one kept still bit-exact, the cache RAM's part as it was; the
 *             room given back (undo_regrow), 400 sessions round the ring, every level kept bit-exact
 *   failed    the test failed (FM1_CR_BAD): the history's least all in the SRAM (16 KB), no second part, the cache's
 *             buffer never written, 400 sessions bit-exact as before
 * An arena of 8 KB (FELUCCA_ARENA_BYTES: the parts it refuses silent placeholders), so the SRAM part is small.
 * Built with CACHE_RAM 2 (8 KB), and 4 (16 KB: the SRAM part its least, ARN_UNDO_SRAM). Exit status: failed checks. */
#ifndef FELUCCA_CACHE_RAM
#define FELUCCA_CACHE_RAM 2
#endif
#include "../firmware/hal/fm1_cache.h"
#define main hostsim_main
#include "hostsim.c"
#undef main

static int fails;
static void check(int ok, const char *what)
{
    printf("undo cache: %-84s %s\n", what, ok ? "ok" : "FAIL");
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
static uint32_t rs = 4242u;
static uint32_t rnd(void)
{
    rs = rs * 1664525u + 1013904223u;
    return rs >> 8;
}
static void reset(uint32_t cache_status)
{
    uint32_t i;
    host_tracks_init();
    for (i = 0; i < NTRK; i++)
        steps_clear(&trk[i]);
    song.rec = 0;
    rec_wait = 0;
    fm1_in.notes = fm1_in.buttons = 0;
    fm1_cache.status = cache_status;                    /* (the test's result at the start: before the ring is set up) */
    memset(fm1_cr_host, 0xA5, sizeof fm1_cr_host);
    memset(&undo_h, 0, sizeof undo_h);                  /* (a power-on: the ring set up at the first edit) */
}
/* one session on a synth track: nsteps whole steps rewritten (a record of 1 + 15 x nsteps + 2 B and the tail) */
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
static int in(const void *p, const void *lo, uint32_t n)
{
    return (const uint8_t *)p >= (const uint8_t *)lo && (const uint8_t *)p < (const uint8_t *)lo + n;
}

#define NS 400u
static state_t S[NS + 1];

/* sessions from the state now (S[0]) until stop says (or NS): -> how many; S[i] the state after the i-th */
static uint32_t run(int (*stop)(void))
{
    uint32_t i;
    state_get(&S[0]);
    for (i = 1; i <= NS; i++) {
        session(rnd() % 3u, 2u + rnd() % 12u);
        state_get(&S[i]);
        if (stop && stop())
            break;
    }
    return i <= NS ? i : NS;
}
/* every level kept (of c sessions) undone bit-exact, then redone: 1 good */
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
static int in_cache(void) { return undo_h.tail > undo_h.len[0] + 1024u; }   /* (the newest records in the cache RAM) */

static void t_passed(void)
{
    uint32_t c, m, m2, n, tk, keep = FELUCCA_CACHE_RAM * 4096u < 15u * 1024u ? 16384u - FELUCCA_CACHE_RAM * 4096u : 1024u;
    uint8_t *lo;
    char what[100];
    reset(FM1_CR_OK);
    snprintf(what, sizeof what, "the test passed: %u B of cache RAM, the history's least in the SRAM %u B (of 16 KB)",
             (unsigned)fm1_cache_ram_free(), (unsigned)ARN_UNDO_KEEP);
    check(fm1_cache_ram_free() == FELUCCA_CACHE_RAM * 4096u && ARN_UNDO_KEEP == keep, what);
    c = run(in_cache);
    check(undo_h.ready && undo_h.seg[0] == arn_mem + ((arn_top() + 3u) & ~3u) &&
          undo_h.seg[0] + undo_h.len[0] == arn_mem + ARN_BYTES + keep,
          "the ring's first part: the arena's unused top and the least, in the SRAM");
    check(undo_h.seg[1] == fm1_cr_host && undo_h.len[1] == sizeof fm1_cr_host &&
          undo_h.size == undo_h.len[0] + undo_h.len[1], "its second part: the cache RAM, all of it");
    undo_status(&n, &m, &tk);
    snprintf(what, sizeof what, "%u sessions into the cache RAM, none dropped: each undone and redone bit-exact", (unsigned)c);
    check(in_cache() && m == c && undo_h.head == 0u && back_and_forth(c), what);
    /* the arena grows over the history's start (core/arena.c ARN_GREW: a block placed past it) */
    lo = undo_h.seg[0] + undo_h.len[0] / 2u;
    undo_yield(lo);
    undo_status(&m2, &n, &tk);
    check(undo_h.seg[0] >= lo && undo_h.seg[1] == fm1_cr_host && undo_h.len[1] == sizeof fm1_cr_host,
          "the arena's block over the first part: it gives way, the cache RAM's part as it was");
    snprintf(what, sizeof what, "the oldest levels dropped (%u of %u kept), each kept one undone and redone bit-exact",
             (unsigned)m2, (unsigned)m);
    check(m2 > 0u && m2 < m && back_and_forth(c), what);
    /* the arena lets it go: the room comes back at the next record */
    undo_regrow();
    check(undo_h.seg[0] <= lo && undo_h.len[1] == sizeof fm1_cr_host, "the room given back (undo_regrow)");
    c = run(0);
    snprintf(what, sizeof what, "%u more sessions, the ring round both parts many times: every level kept bit-exact",
             (unsigned)c);
    check(back_and_forth(c), what);
}

static void t_failed(void)
{
    uint32_t m, n;
    char what[100];
    reset(FM1_CR_BAD);
    snprintf(what, sizeof what, "the test failed: no cache RAM, the history's least all in the SRAM (%u B)",
             (unsigned)ARN_UNDO_KEEP);
    check(fm1_cache_ram_free() == 0u && fm1_cache_ram_lo() == 0 && ARN_UNDO_KEEP == 16384u, what);
    session(0, 1);
    check(undo_h.ready && undo_h.len[1] == 0u && undo_h.size == undo_h.len[0] &&
          undo_h.seg[0] + undo_h.len[0] == arn_mem + ARN_BYTES + 16384u && !in(undo_h.seg[0], fm1_cr_host, sizeof fm1_cr_host),
          "the ring: the SRAM alone (the arena's unused top and the 16 KB)");
    m = run(0);
    check(back_and_forth(m), "400 sessions: every level kept bit-exact");
    for (n = 0; n < sizeof fm1_cr_host && fm1_cr_host[n] == 0xA5u; n++)
        ;
    check(n == sizeof fm1_cr_host, "the cache's ways never written");
}

int main(void)
{
    t_passed();
    t_failed();
    printf("undo cache: %d failed\n", fails);
    return fails;
}
