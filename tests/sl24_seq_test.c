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
    for (i = 0; i < NTRK; i++)
        steps_clear(&trk[i]);
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

int main(void)
{
    t_div_long();
    t_div_change();
    t_dly_dot();
    printf("sl24: %d failed\n", fails);
    return fails;
}
