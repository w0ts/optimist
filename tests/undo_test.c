/* SPDX-License-Identifier: GPL-3.0-only */
/* Undo / redo of the patterns (firmware/src/undo.c), on the host.
 *   chain     300 random sessions over all four tracks (synth steps, drum lanes, LEN, DIV): every undo
 *             brings back the state before its session, bit for bit (all tracks), every redo the one after
 *   redo      a new change drops what was undone; a session that changed nothing is no level
 *   link      one session over several tracks (NEW) is one level
 *   evict     a small ring keeps the newest levels (as many as fit) and forgets the oldest
 *   arena     the ring from simulated linker symbols: pool and / or main RAM, an overflowed pool, the
 *             cap, two segments a record spans, a ring too small for any record
 *   playing   three recording passes on a synth track and on the drum track while the transport runs:
 *             undo x3 / redo x2 at once, still playing
 *   clear     a cleared history has nothing to undo
 * Built twice: FELUCCA_UNDO_HISTORY=1 (the default) and 0 (the single level: its own checks).
 * Exit status: the number of failed checks. */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int fails;
static void check(int ok, const char *what)
{
    printf("undo: %-80s %s\n", what, ok ? "ok" : "FAIL");
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

static uint32_t rs = 12345u;
static uint32_t rnd(void)
{
    rs = rs * 1664525u + 1013904223u;
    return rs >> 8;
}

static void reset(void)
{
    uint32_t i;
    host_tracks_init();
    for (i = 0; i < NTRK; i++)
        steps_clear(&trk[i]);
    song.rec = 0;
    rec_wait = 0;
    fm1_in.notes = fm1_in.buttons = 0;
}

/* one session on track t: nsteps steps changed (random bytes; on the drum track lanes), maybe LEN / DIV */
static void session(uint32_t t, uint32_t nsteps, int params)
{
    uint32_t k;
    track_t *tr = &trk[t];
    undo_mark(tr, (undo_sess += 4u) | 3u);
    for (k = 0; k < nsteps; k++) {
        uint32_t i = rnd() % NSTEP;
        if (t == TRK_DRUM) {
            if (dstep_has(&tr->dstep[i], rnd() % 16u) && (rnd() & 1u))
                dstep_clr(&tr->dstep[i], rnd() % 16u);
            dstep_set(&tr->dstep[i], rnd() % 16u, rnd() % 4u, rnd() % 4u);
        } else {
            uint8_t *b = (uint8_t *)&tr->step[i];
            uint32_t j;
            for (j = 0; j < sizeof(step_t); j++)
                b[j] = (uint8_t)rnd();
        }
    }
    if (params) {
        if (rnd() & 1u)
            tr->p[P_SLEN] = (int16_t)(1u + rnd() % 64u);
        else
            tr->p[P_SDIV] = (int16_t)(rnd() % 6u);
    }
}
/* the same, every step it touches different from before (a known record size: steps x 11 + 4 B) */
static void session_exact(uint32_t t, uint32_t nsteps)
{
    uint32_t k, base = rnd() % NSTEP;
    undo_mark(&trk[t], (undo_sess += 4u) | 3u);
    for (k = 0; k < nsteps; k++) {
        step_t *s = &trk[t].step[(base + k) % NSTEP];
        s->vel = (uint8_t)(s->vel + 1u);
    }
}

#if FELUCCA_UNDO_HISTORY
#define NCHAIN 300u
static state_t S[NCHAIN + 1];

static void t_chain(void)
{
    uint32_t i, n, m, tk, ok = 1, okr = 1, cnt = 1;
    reset();
    undo_arena_set(undo_host_arena, undo_host_arena + sizeof undo_host_arena, 0, 0, 0);
    state_get(&S[0]);
    for (i = 1; i <= NCHAIN; i++) {
        session(rnd() % NTRK, 1u + rnd() % 6u, (rnd() % 5u) == 0u);
        undo_close();
        state_get(&S[i]);
    }
    undo_status(&n, &m, &tk);
    check(n == NCHAIN && m == NCHAIN, "300 sessions over the four tracks: 300 levels in a 16 KiB ring");
    for (i = NCHAIN; i > 0; i--) {
        ok &= undo_apply(0) == 1;
        ok &= state_is(&S[i - 1]);
        undo_status(&n, &m, &tk);
        cnt &= n == i - 1u && m == NCHAIN;
    }
    check(ok, "undo x300: each brings back the state before its session, every track bit-exact");
    check(cnt, "undo: n/m counts down (n of 300)");
    check(!undo_apply(0) && state_is(&S[0]), "undo at the start: nothing to undo, nothing changed");
    for (i = 1; i <= NCHAIN; i++) {
        okr &= undo_apply(1) == 1;
        okr &= state_is(&S[i]);
    }
    check(okr, "redo x300: each brings back the state after its session, bit-exact");
    check(!undo_apply(1) && state_is(&S[NCHAIN]), "redo at the end: nothing to redo");
    ok = 1;                                             /* back and forth in the middle */
    for (i = 0; i < 2000u; i++) {
        uint32_t back = rnd() & 1u;
        undo_status(&n, &m, &tk);
        if (back ? n > 0 : n < m) {
            ok &= undo_apply(!back) == 1;
            ok &= state_is(&S[back ? n - 1u : n + 1u]);
        }
    }
    check(ok, "2000 random undo / redo steps: always the state of that level");
}

static void t_redo_rules(void)
{
    uint32_t n, m, tk;
    state_t a, b, c;
    reset();
    undo_clear();
    state_get(&a);
    session(0, 3, 0);
    session(TRK_DRUM, 4, 0);
    state_get(&b);
    check(undo_can(0) && !undo_can(1), "two sessions: something to undo, nothing to redo");
    check(undo_apply(0) && undo_apply(0) && state_is(&a), "undo x2: the start");
    undo_status(&n, &m, &tk);
    check(n == 0 && m == 2 && tk == 0, "undo x2: 0/2, the last undo was on track 1");
    check(undo_apply(1), "redo once");
    undo_mark(&trk[1], (undo_sess += 4u) | 3u);         /* a session that changes nothing */
    undo_close();
    undo_status(&n, &m, &tk);
    check(n == 1 && m == 2 && undo_can(1), "a session that changed nothing: no level, redo kept");
    session(1, 2, 1);                                    /* a new change */
    undo_close();
    state_get(&c);
    undo_status(&n, &m, &tk);
    check(n == 2 && m == 2 && !undo_apply(1), "a new change: redo gone (2/2)");
    check(undo_apply(0) && !state_is(&c) && undo_apply(0) && state_is(&a), "undo x2 after it: the start again");
    check(undo_apply(1) && undo_apply(1) && state_is(&c), "redo x2: the new change, not the dropped one");
    (void)b;
    /* an open session: undo closes it first, then undoes it */
    session(2, 5, 0);
    check(undo_can(0) && undo_apply(0) && state_is(&c), "undo with the session still open: back to before it");
    /* recording-style sessions: the same session marked again is one level */
    reset();
    undo_clear();
    state_get(&a);
    {
        uint32_t k, sess = (undo_sess += 4u) | 3u;
        for (k = 0; k < 10u; k++) {
            undo_mark(&trk[0], sess);
            trk[0].step[k].note[0] = (uint8_t)(40u + k);
            trk[0].step[k].n = 1;
        }
    }
    undo_close();
    undo_status(&n, &m, &tk);
    check(m == 1 && undo_apply(0) && state_is(&a), "ten marks of one session: one level");
    {   /* undo_end: a held layer let go ends its own session only (not a recording pass open meanwhile) */
        uint32_t sess = (undo_sess += 4u) | 3u;
        undo_mark(&trk[0], sess);
        trk[0].step[1].vel ^= 1u;
        undo_end(sess + 4u);
        check(undo.valid, "undo_end of another session: this one stays open");
        undo_end(sess);
        undo_status(&n, &m, &tk);
        check(!undo.valid && n == 1 && m == 1, "undo_end of its own: one level now");
    }
}

static void t_link(void)
{
    uint32_t i, n, m, tk, sess;
    state_t a, b;
    reset();
    undo_clear();
    session(1, 2, 0);
    undo_close();
    state_get(&a);
    sess = (undo_sess += 4u) | 3u;                      /* NEW: every track, one session */
    for (i = 0; i < NTRK; i++) {
        undo_mark(&trk[i], sess);
        steps_clear(&trk[i]);
        trk[i].step[i].vel = 99;
        trk[i].p[P_SLEN] = 32;
    }
    undo_close();
    state_get(&b);
    undo_status(&n, &m, &tk);
    check(n == 2 && m == 2, "one session over four tracks: one level (2 in all)");
    check(undo_apply(0) && state_is(&a), "undo: all four tracks back at once");
    check(undo_apply(1) && state_is(&b), "redo: all four again");
    check(undo_apply(0) && undo_apply(0) && !undo_apply(0), "undo x2: the start; a third: nothing");
}

static void t_evict(void)
{
    static uint8_t small[2048];
    uint32_t i, n, m, tk, ok = 1;
    reset();
    check(undo_arena_set(small, small + sizeof small, 0, 0, 0) == 2048u, "a 2 KiB ring");
    state_get(&S[0]);
    for (i = 1; i <= 100u; i++) {
        session_exact(rnd() % NTRK, 10);                /* 114 B a level: 17 fit */
        undo_close();
        state_get(&S[i]);
    }
    undo_status(&n, &m, &tk);
    if (m != 17u)
        printf("undo:   evict: %u/%u levels\n", n, m);
    check(n == 17u && m == 17u, "100 levels of 114 B into 2 KiB: the newest 17 kept");
    for (i = 0; i < 17u; i++)
        ok &= undo_apply(0) && state_is(&S[99u - i]);
    check(ok, "undo x17: the newest levels, bit-exact");
    check(!undo_apply(0) && state_is(&S[83]), "the 18th: nothing (the oldest are forgotten)");
    ok = 1;
    for (i = 0; i < 17u; i++)
        ok &= undo_apply(1) && state_is(&S[84u + i]);
    check(ok, "redo x17: back to the end");
    /* the biggest record (64 steps + LEN + DIV) still fits a 1 KiB ring; nothing is lost for it */
    {
        static uint8_t kb[UNDO_MIN];
        uint32_t k;
        undo_arena_set(kb, kb + sizeof kb, 0, 0, 0);
        state_get(&S[0]);
        undo_mark(&trk[0], (undo_sess += 4u) | 3u);
        for (k = 0; k < NSTEP; k++)
            trk[0].step[k].vel ^= 0x5Au;
        trk[0].p[P_SLEN] = 7;
        trk[0].p[P_SDIV] = (int16_t)((trk[0].p[P_SDIV] + 1) % 6);
        undo_close();
        undo_status(&n, &m, &tk);
        check(UREC_MAX == 712u && m == 1u && undo_apply(0) && state_is(&S[0]),
              "a whole pattern + LEN + DIV (712 B) in the 1 KiB minimum ring: undone");
    }
}

static void t_arena(void)
{
    static uint8_t pool[3000], ram[1500];
    uint32_t i, ok = 1, n, m, tk;
    check(undo_arena_set(pool + 2000, pool + 1000, ram, ram + 1500, 0) == 1500u && undo_h.seg[0] == ram,
          "pool overflowed (hi < lo): the ring is main RAM's leftover only");
    check(undo_arena_set(pool, pool + 3000, ram, ram, 0) == 3000u && undo_h.len[1] == 0u,
          "no main RAM left: the pool's leftover only");
    check(undo_arena_set(pool, pool + 3000, ram, ram + 1500, 0) == 4500u && undo_h.len[0] == 3000u &&
          undo_h.len[1] == 1500u, "both: pool then main RAM, 4500 B");
    check(undo_arena_set(pool, pool + 3000, ram, ram + 1500, 3500) == 3500u && undo_h.len[1] == 500u,
          "FELUCCA_UNDO_CAP 3500: the pool's 3000 and 500 of main RAM");
    check(undo_arena_set(pool, pool + 3000, ram, ram + 1500, 2000) == 2000u && undo_h.len[1] == 0u,
          "a cap under the pool's leftover: the pool only");
    check(undo_arena_set(pool, pool, ram, ram, 0) == 0u, "nothing left anywhere: no ring");
    reset();
    session(0, 3, 0);
    check(!undo_apply(0) && !undo_can(1), "no ring: a change is not undoable (and nothing breaks)");
    /* two odd segments, records across the seam and the wrap, every level checked */
    reset();
    undo_arena_set(pool + 1, pool + 1002, ram + 3, ram + 780, 0);
    state_get(&S[0]);
    for (i = 1; i <= 120u; i++) {
        session(rnd() % NTRK, 1u + rnd() % 12u, (rnd() % 4u) == 0u);
        undo_close();
        state_get(&S[i]);
    }
    undo_status(&n, &m, &tk);
    for (i = 0; i < m; i++)
        ok &= undo_apply(0) && state_is(&S[119u - i]);
    check(m > 5u && ok && !undo_apply(0), "two segments of 1001 + 777 B, 120 sessions: every kept level bit-exact");
    if (!ok || m <= 5u)
        printf("undo:   arena: %u levels kept\n", m);
    for (i = 0; i < m; i++)
        ok &= undo_apply(1);
    check(ok && state_is(&S[120]), "redo all of them: the last state");
    undo_arena_set(undo_host_arena, undo_host_arena + sizeof undo_host_arena, 0, 0, 0);
}

/* the transport runs; key k pressed on step `at` of each of three passes; snapshots between passes */
static void run_blk(void)
{
    int32_t out[CTL * 2];
    mix_block(out, CTL);
}
static void t_playing(uint32_t t)
{
    state_t P[4];
    uint32_t pass, ok = 1, n, m, tk;
    static const uint8_t KEYS[3] = {7, 9, 11};          /* white keys: notes / lanes */
    reset();
    undo_clear();
    song.g[G_BPM] = 120;
    song.sel = (uint8_t)t;
    trk[t].p[P_SLEN] = 16;
    trk[t].p[P_SDIV] = 2;
    song.rec = (uint8_t)(1u << t);
    transport_req = 1;
    run_blk();
    state_get(&P[0]);
    for (pass = 0; pass < 3u; pass++) {
        uint32_t at = 3u + pass * 3u, b;
        while (trk[t].seq_idx != at)
            run_blk();
        fm1_in.notes = 1u << KEYS[pass];
        for (b = 0; b < 30u; b++)
            run_blk();
        fm1_in.notes = 0;
        while (trk[t].seq_idx != 14u)
            run_blk();
        state_get(&P[pass + 1u]);
        while (trk[t].seq_idx != 0u)
            run_blk();
    }
    check(ok && memcmp(&P[1], &P[0], sizeof P[0]) && memcmp(&P[2], &P[1], sizeof P[0]) &&
          memcmp(&P[3], &P[2], sizeof P[0]), t == TRK_DRUM ? "drums: three recording passes, each changed the pattern"
                                                            : "synth: three recording passes, each changed the pattern");
    check(song.playing, "still playing");
    ok = undo_apply(0) && state_is(&P[2]);
    run_blk();
    ok &= undo_apply(0) && state_is(&P[1]);
    run_blk();
    ok &= undo_apply(0) && state_is(&P[0]);
    undo_status(&n, &m, &tk);
    check(ok && n == 0 && m == 3 && tk == t, "undo x3 while playing: before each pass, bit-exact (0/3)");
    run_blk();
    ok = undo_apply(1) && state_is(&P[1]);
    run_blk();
    ok &= undo_apply(1) && state_is(&P[2]);
    check(ok, "redo x2 while playing: after passes 1, 2");
    {   /* record again in this pass: a new level, the redo of pass 3 goes */
        uint32_t b;
        while (trk[t].seq_idx != 5u)
            run_blk();
        fm1_in.notes = 1u << 5;
        for (b = 0; b < 30u; b++)
            run_blk();
        fm1_in.notes = 0;
        run_blk();
        check(!undo_apply(1) && undo_apply(0) && state_is(&P[2]), "recording after the undo: redo gone, its own undo");
    }
    transport_req = 2;
    run_blk();
}

static void t_clear(void)
{
    session(0, 3, 0);
    session(1, 3, 0);
    undo_clear();
    check(!undo_can(0) && !undo_can(1) && !undo_apply(0), "cleared (a project loaded): nothing to undo or redo");
}

int main(void)
{
    t_chain();
    t_redo_rules();
    t_link();
    t_evict();
    t_arena();
    t_playing(0);
    t_playing(TRK_DRUM);
    t_clear();
    printf("undo: %s (history)\n", fails ? "FAILED" : "PASS");
    return fails;
}

#else  /* the single level */
int main(void)
{
    state_t a, b;
    reset();
    state_get(&a);
    session(0, 4, 0);
    trk[0].p[P_SLEN] = 9;                               /* (the single level keeps LEN only) */
    state_get(&b);
    check(undo_apply(0) && state_is(&a), "single level: undo");
    check(!undo_apply(0), "single level: a second undo: nothing");
    check(undo_apply(1) && state_is(&b), "single level: redo");
    session(TRK_DRUM, 4, 0);
    session(1, 4, 0);
    check(undo_apply(0) && !state_is(&b), "single level: only the last session comes back");
    printf("undo: %s (single level)\n", fails ? "FAILED" : "PASS");
    return fails;
}
#endif
