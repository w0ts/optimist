/* SPDX-License-Identifier: GPL-3.0-only */
/* Step conditions (FELUCCA_COND, Felucca 1.5.6; seq/auto.c cond_miss) in a build with the automation store for them
 * alone (no MICRO, FILLS, PLOCK, CHANCE, MOTION: the sequencer's first seq_tick, SEQ_SKIP from COND): drum lane 0 on
 * every step of 8, step 2 1:2, step 3 2:4, step 4 3:3, step 5 4:4; a synth step 1:2 with a ratchet; 24 passes, then
 * PLAY again from pass 1. tests/auto_test.c has them beside FILL and chance with every switch on.
 * Exit status: the number of failed checks. */
#define FELUCCA_COND 1
#define main hostsim_main
#include "hostsim.c"
#undef main

static int fails;
static void check(int ok, const char *what)
{
    printf("cond: %-96s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}
typedef struct { uint32_t pos; uint8_t note; } hit_t;
static hit_t hits[20000];
static uint32_t nhits;
static void run_block(void)
{
    int32_t out[CTL * 2];
    uint32_t a = drums.age, k, beat = clk_beat, pos = clk_pos;
    mix_block(out, CTL);
    if (drums.age != a)
        for (k = 0; k < NDRUM; k++)
            if (drums.v[k].age > a && nhits < 20000u) {
                hits[nhits].pos = beat * BEAT_U + pos;
                hits[nhits++].note = drums.v[k].note;
            }
}
static uint32_t hits_on(uint32_t step)
{
    uint32_t i, n = 0, slen = BEAT_U / 4u;
    for (i = 0; i < nhits; i++)
        n += hits[i].note == LANE_NOTE[0] && (hits[i].pos + slen / 2u) / slen % 8u == step;
    return n;
}
static void play(uint32_t beats)
{
    nhits = 0;
    clk_beat = clk_pos = 0;
    transport_req = 1;
    while (clk_beat < beats)
        run_block();
    seq_stop();
}
int main(void)
{
    uint32_t i, n[8], on = 0, guard;
    _Static_assert(FELUCCA_AUTO && SEQ_SKIP && !SL24_STEPX, "the store and the skip for the conditions alone");
    host_tracks_init();
    for (i = 0; i < NTRK; i++)
        steps_clear(&trk[i]), trk[i].p[P_SLEN] = 8;
    memset(&drums, 0, sizeof drums);
    drums.set = -2;
    song.g[G_BPM] = 120;
    for (i = 0; i < 8u; i++)
        dstep_set(&TDRUM->dstep[i], 0, LV_NORM, 0);
    (void)step_cond_set(TDRUM, 1, 1);
    (void)step_cond_set(TDRUM, 2, 7);
    (void)step_cond_set(TDRUM, 3, 5);
    (void)step_cond_set(TDRUM, 4, 9);
    play(48);                                         /* 24 passes of 8 sixteenths */
    for (i = 0; i < 8u; i++)
        n[i] = hits_on(i);
    printf("cond: over 24 passes: %u %u %u %u %u\n", n[0], n[1], n[2], n[3], n[4]);
    check(n[0] == 24u && n[1] == 12u && n[2] == 6u && n[3] == 8u && n[4] == 6u,
          "1:2 12 of 24, 2:4 6, 3:3 8, 4:4 6, no condition 24");
    play(8);
    check(hits_on(1) == 2u && hits_on(2) == 1u && hits_on(3) == 1u && hits_on(4) == 1u,
          "PLAY again: the count from pass 1 (4 passes: 1:2 twice, 2:4, 3:3, 4:4 once)");
    {   /* a synth step 1:2 with a ratchet x2: nothing at all on the even passes */
        track_t *t = &trk[0];
        t->step[0].time = ST_NOTE, t->step[0].n = 1, t->step[0].note[0] = 60, t->step[0].vel = 100, t->step[0].rat = 1;
        (void)step_cond_set(t, 0, 1);
        clk_beat = clk_pos = 0;
        transport_req = 1;
        for (guard = 0; clk_beat < 8u && guard < 100000u; guard++) {   /* passes 1..4 */
            run_block();
            if (t->seq_n && clk_beat < 8u)
                on |= 1u << (clk_beat / 2u);
        }
        seq_stop();
        check(on == 5u, "a synth step 1:2 with its ratchet: heard on passes 1 and 3 only");
    }
    printf("cond test %s (%d failed)\n", fails ? "FAILED" : "passed", fails);
    return fails;
}
