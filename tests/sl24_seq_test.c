/* SPDX-License-Identifier: GPL-3.0-only */
/* SLOOP 2.4 sequencer backports (isod89/sloop-fm1 v2.4, 8d3823f; firmware/src/backports24seq.h), each switch on:
 *   div      DIV 1/2, 1BAR, 2BAR: a step every 2 / 4 / 8 beats, on the beat, unswung; no burst on a change
 *   delay    TIME 1/8D, 1/16D: 3/4 and 3/8 of a beat; longer than the line: halved (still dotted)
 *   micro    a nudge plays its step early / late by m/64 of a step; never skipped nor doubled, in order
 *   fills    FILL ONLY steps play only in a fill, NO FILL steps only outside one; GLO + 10: the next bar
 *   locks    a lock sets the parameter on its step, back at the next step without one; motion first, the
 *            lock wins on its step; a knob turned meanwhile is the new base; STOP restores
 *   chain    SAVE + taps: the sections in order, each for its bars, looped
 * Exit status: the number of failed checks. */
#define main hostsim_main
#include "hostsim.c"
#undef main

static uint64_t blk;
typedef struct { uint64_t blk; uint32_t pos; uint8_t note, vel; } hit_t;
static hit_t hits[100000];
static uint32_t nhits;
static void run_block(void)
{
    int32_t out[CTL * 2];
    uint32_t a = drums.age, k, beat = clk_beat, pos = clk_pos;
    mix_block(out, CTL);
    if (drums.age != a)
        for (k = 0; k < NDRUM; k++)
            if (drums.v[k].age > a && nhits < 100000u) {
                hits[nhits].blk = blk;
                hits[nhits].pos = beat * BEAT_U + pos;   /* (the clock at the block's start: < 2^32 for 16 beats) */
                hits[nhits].note = drums.v[k].note;
                hits[nhits].vel = drums.v[k].vel;
                nhits++;
            }
    blk++;
}
static void reset(uint32_t bpm)
{
    uint32_t i;
    host_tracks_init();
    for (i = 0; i < NTRK; i++) {
        steps_clear(&trk[i]);
        trk[i].seq_abs = SEQ_NONE;                   /* (a test after a STOP: no step played yet) */
        trk[i].seq_idx = 0;
    }
    memset(&drums, 0, sizeof drums);
    drums.set = -2;
    nhits = 0;
    blk = 0;
    song.g[G_BPM] = (int16_t)bpm;
    song.g[G_SWING] = 0;
    song.rec = 0;
    rec_wait = 0;
    fm1_in.notes = fm1_in.buttons = 0;
    kb_prev = 0;
    clk_beat = clk_pos = 0;
    song.playing = 0;
}
static int fails;
static void check(int ok, const char *what)
{
    printf("sl24: %-76s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}
static uint32_t count_note(uint32_t note)
{
    uint32_t i, n = 0;
    for (i = 0; i < nhits; i++)
        n += hits[i].note == note;
    return n;
}
static void run_beats(uint32_t beats)
{
    while (clk_beat < beats)
        run_block();
}

/* ---- DIV 1/2, 1BAR, 2BAR */
static void t_div_long(void)
{
    static const uint8_t DV[3] = {6, 7, 8}, BEATS[3] = {2, 4, 8};
    uint32_t d, ok = 1;
    for (d = 0; d < 3u; d++) {
        uint32_t i, prev = 0, n = 0;
        reset(120);
        song.g[G_SWING] = 100;                         /* (swing must not move whole-beat steps) */
        TDRUM->p[P_SDIV] = DV[d];
        TDRUM->p[P_SLEN] = 4;
        for (i = 0; i < 4u; i++)
            dstep_set(&TDRUM->dstep[i], 0, LV_NORM, 0);
        transport_req = 1;
        run_beats(16);
        for (i = 0; i < nhits; i++) {
            if (hits[i].note != 36)
                continue;
            if (n && (int32_t)(hits[i].pos - prev - BEATS[d] * BEAT_U) * (int32_t)(hits[i].pos - prev - BEATS[d] * BEAT_U) > (int32_t)CTL * 120 * CTL * 120) {   /* (within a block) */
                ok = 0;
                printf("sl24:   DIV %u: hit %u %u units after the last\n", DV[d], n, hits[i].pos - prev);
            }
            if (hits[i].pos % BEAT_U >= (uint32_t)CTL * 120u) {   /* within the block of its beat */
                ok = 0;
                printf("sl24:   DIV %u: hit %u at %u units into its beat\n", DV[d], n, hits[i].pos % BEAT_U);
            }
            prev = hits[i].pos;
            n++;
        }
        if (n != 16u / BEATS[d]) {
            ok = 0;
            printf("sl24:   DIV %u: %u hits in 16 beats\n", DV[d], n);
        }
    }
    check(ok, "DIV 1/2, 1BAR, 2BAR: a step every 2, 4, 8 beats, on the beat, swing 100 % ignored");
}
static void t_div_change(void)
{
    uint32_t i, n0;
    reset(120);
    TDRUM->p[P_SDIV] = 8;                              /* 2BAR */
    for (i = 0; i < 16u; i++)
        dstep_set(&TDRUM->dstep[i], 4, LV_NORM, 0);
    transport_req = 1;
    run_beats(3);
    n0 = nhits;
    TDRUM->p[P_SDIV] = 3;                              /* 2BAR -> 1/32 in the middle of a step */
    run_block();
    check(nhits - n0 <= 1u && TDRUM->p[P_SDIV] == 3, "DIV 2BAR -> 1/32 mid-step: one step a block, no burst");
    check(TP[P_SDIV].max == 8 && !strcmp(TP[P_SDIV].names[8], "2BAR") && !strcmp(TP[P_SDIV].names[2], "1/16"),
          "DIV's names: the six as before, then 1/2 1BAR 2BAR (appended)");
}

/* ---- delay TIME 1/8D, 1/16D */
static void t_dly_dot(void)
{
    uint32_t e8, e16, s8, s16, s8slow;
    song.g[G_BPM] = 120;
    song.g[G_DTIME] = 6;
    s8 = delay_samples();
    song.g[G_DTIME] = 7;
    s16 = delay_samples();
    e8 = BEAT_U * 3u / 4u / 120u;
    e16 = BEAT_U * 3u / 8u / 120u;
    check(s8 == e8 && s16 == e16, "TIME 1/8D, 1/16D at 120 BPM: 3/4 and 3/8 of a beat");
    song.g[G_BPM] = 40;                                /* 1/8D at 40 BPM: 49612 samples */
    song.g[G_DTIME] = 6;
    s8slow = delay_samples();
    check(s8slow < DLY_LEN && (DLY_LEN > BEAT_U * 3u / 4u / 40u ? s8slow == BEAT_U * 3u / 4u / 40u
                                                               : s8slow == BEAT_U * 3u / 8u / 40u),
          "TIME 1/8D at 40 BPM: fits, or halves to 1/16D when the line is shorter (DLY_HALVE)");
    check(GP[G_DTIME].max == 7 && !strcmp(GP[G_DTIME].names[6], "1/8D") && !strcmp(GP[G_DTIME].names[7], "1/16D"),
          "TIME's names: the six as before, then 1/8D 1/16D (appended)");
    song.g[G_BPM] = 120;
}

#if FELUCCA_MICRO
/* ---- micro timing: step k of a 1/16 drum track (lane k: its own note) fires at k x slen + its nudge */
static void t_micro(void)
{
    static const int8_t M[16] = {0, 0, 0, 0, 16, 0, 0, 0, -16, 0, 31, -32, 0, 0, -32, 31};
    uint32_t i, ok = 1, n = 0, slen = BEAT_U / 4u, blk_u;
    reset(120);
    blk_u = (uint32_t)CTL * 120u;
    for (i = 0; i < 16u; i++) {
        dstep_set(&TDRUM->dstep[i], i, LV_NORM, 0);
        TX(TDRUM)->micro[i] = M[i];
    }
    transport_req = 1;
    run_beats(9);                                    /* two bars and a beat */
    for (i = 0; i < nhits; i++) {
        uint32_t k, at;
        int32_t want, d;
        for (k = 0; k < 16u && LANE_NOTE[k] != hits[i].note; k++)
            ;
        if (k == 16u)
            continue;
        at = hits[i].pos;
        want = (int32_t)((n / 16u) * 16u * slen + k * slen) + (int32_t)(slen / 64u) * M[k];
        if (want < 0)
            want = 0;                                /* (step 0 nudged early at PLAY: at once) */
        d = (int32_t)at - want;
        if (n < 32u && (d < 0 || d >= (int32_t)blk_u)) {
            ok = 0;
            printf("sl24:   step %u (hit %u): %d units from its nudged time\n", k, n, d);
        }
        n++;
    }
    check(ok && n >= 32u, "MICRO: each step fires within a block of its nudged time (+-1/4, +-1/2 step)");
}
static void t_micro_order(void)
{
    uint32_t i, ok = 1, steps = 0, last = 0xFFFFu, seed = 12345u;
    reset(97);
    for (i = 0; i < 16u; i++) {
        dstep_set(&TDRUM->dstep[i], 0, LV_NORM, 0);
        seed = seed * 1103515245u + 12345u;
        TX(TDRUM)->micro[i] = (int8_t)(MICRO_MIN + (int32_t)((seed >> 16) % 64u));
    }
    transport_req = 1;
    while (clk_beat < 32u) {
        uint32_t a = TDRUM->seq_abs;
        run_block();
        if (TDRUM->seq_abs != a) {
            if (last != 0xFFFFu && TDRUM->seq_idx != (last + 1u) % 16u)
                ok = 0;
            last = TDRUM->seq_idx;
            steps++;
        }
    }
    check(ok && count_note(36) == steps && steps >= 127u && steps <= 129u,
          "MICRO: random nudges on every step, 8 bars: each step once, in order, no double");
}
#endif

#if FELUCCA_FILLS
/* ---- fills: step 0 FILL ONLY (kick), step 4 NO FILL (snare), step 8 normal (hat) */
static void t_fills(void)
{
    uint32_t k0, s0, h0, k1, s1, h1;
    reset(120);
    dstep_set(&TDRUM->dstep[0], 0, LV_NORM, 0);
    dstep_set(&TDRUM->dstep[4], 2, LV_NORM, 0);
    dstep_set(&TDRUM->dstep[8], 4, LV_NORM, 0);
    step_fill_set(TDRUM, 0, FC_FILL);
    step_fill_set(TDRUM, 4, FC_NOFILL);
    transport_req = 1;
    run_beats(4);                                    /* bar 1: no fill */
    k0 = count_note(36), s0 = count_note(38), h0 = count_note(42);
    fill_held = 1;                                   /* bar 2: GLO + 9 held */
    run_beats(8);
    fill_held = 0;
    k1 = count_note(36) - k0, s1 = count_note(38) - s0, h1 = count_note(42) - h0;
    check(k0 == 0 && s0 == 1 && h0 == 1 && k1 == 1 && s1 == 0 && h1 == 1,
          "FILLS: FILL ONLY plays in a fill only, NO FILL outside one only, normal always");
    {
        uint32_t k[4], sn[4], hh[4], i, ok = 1;
        static const uint8_t WK[3] = {0, 1, 0}, WS[3] = {1, 0, 1};
        run_beats(9);                                /* GLO + 10 in the middle of bar 3: bar 4 is the fill */
        fill_arm = 1;
        for (i = 0; i < 4u; i++) {
            k[i] = count_note(36), sn[i] = count_note(38), hh[i] = count_note(42);
            if (i < 3u)
                run_beats(12u + 4u * i);
        }
        for (i = 0; i < 3u; i++)                     /* bar 3 (from beat 8: its kick, if any, came before), 4, 5 */
            ok &= (i == 0 || k[i + 1] - k[i] == WK[i]) && sn[i + 1] - sn[i] == WS[i] && hh[i + 1] - hh[i] == 1u;
        ok &= !fill_arm && !fill_bar_on;
        check(ok, "FILLS: GLO + 10 makes the next bar a fill, then normal again");
    }
    transport_req = 2;
    fill_arm = 1;
    run_block();
    check(!fill_arm && !fill_held && !fill_bar_on, "FILLS: STOP ends a fill, held or armed");
}
#endif

#if FELUCCA_PLOCK
/* ---- locks: track 1, 1/16, a lock of LFO DEST FLT on step 2 and of PAN on steps 2 and 3 */
static int16_t val_at_step(track_t *t, uint32_t id, uint32_t step)   /* p[id] once step has fired (from PLAY) */
{
    while (!(t->seq_abs != SEQ_NONE && t->seq_idx == step))
        run_block();
    return t->p[id];
}
static void t_locks(void)
{
    track_t *t = &trk[0];
    int16_t b, v2, v3, v4, p2, p3, p4;
    reset(120);
    b = t->p[P_LD_FLT];
    check(lock_set(t, 2, P_LD_FLT, 40) && lock_set(t, 2, P_PAN, -30) && lock_set(t, 3, P_PAN, 20),
          "PLOCK: three locks set (two on one step)");
    check(!lock_set(t, 2, P_SDIV, 1) && !lock_set(t, 2, P_ARATE, 1), "PLOCK: DIV and the arp's RATE are not lockable");
    transport_req = 1;
    v2 = val_at_step(t, P_LD_FLT, 2), p2 = t->p[P_PAN];
    v3 = val_at_step(t, P_LD_FLT, 3), p3 = t->p[P_PAN];
    v4 = val_at_step(t, P_LD_FLT, 4), p4 = t->p[P_PAN];
    check(v2 == 40 && p2 == -30 && v3 == b && p3 == 20 && v4 == b && p4 == 0,
          "PLOCK: on its step the value, back at the next step without one (PAN: -30, 20, then 0)");
    val_at_step(t, P_LD_FLT, 2);
    t->p[P_LD_FLT] = 7;                              /* a knob turned while the lock holds: the new base */
    check(val_at_step(t, P_LD_FLT, 3) == 7, "PLOCK: a knob turned during a lock: kept as the base after it");
    val_at_step(t, P_LD_FLT, 2);
    transport_req = 2;
    run_block();
    check(t->p[P_LD_FLT] == 7 && t->p[P_PAN] == 0, "PLOCK: STOP: every parameter back to its base");
    {
        uint32_t i, n = 0;
        for (i = 0; i < 30u; i++)
            n += lock_set(t, i, P_REV, 10);
        check(n == NLOCK - 3u, "PLOCK: 24 locks a track (21 more after the three)");
    }
#if FELUCCA_MACROS
    check(!lock_set(TDRUM, 1, P_ED_FLT, 10) && lock_set(TDRUM, 1, P_E0, 3),
          "PLOCK: the drum track: the macros' places not lockable, the kit is");
#endif
}
#if FELUCCA_MOTION
/* motion first, the lock wins on its step and lets go to the motion value */
static void t_lock_motion(void)
{
    track_t *t = &trk[0];
    reset(120);
    motion.count = 1;
    motion.ev[0].place = (uint8_t)(0u << 6 | 2u);    /* track 1, step 2: LFO DEST FLT = 25 */
    motion.ev[0].param = P_LD_FLT;
    motion.ev[0].value = 25;
    motion_set_enabled(t, 1);
    lock_set(t, 2, P_LD_FLT, 50);
    transport_req = 1;
    { int16_t a = val_at_step(t, P_LD_FLT, 2), c = val_at_step(t, P_LD_FLT, 3);
    check(a == 50 && c == 25,
          "PLOCK + MOTION on one step: the lock wins there, then the motion value"); }
    transport_req = 2;
    run_block();
    motion.count = 0;
}
#endif
#endif

int main(void)
{
    t_div_long();
    t_div_change();
    t_dly_dot();
#if FELUCCA_MICRO
    t_micro();
    t_micro_order();
#endif
#if FELUCCA_FILLS
    t_fills();
#endif
#if FELUCCA_PLOCK
    t_locks();
#if FELUCCA_MOTION
    t_lock_motion();
#endif
#endif
    printf("sl24: %d failed\n", fails);
    return fails;
}
