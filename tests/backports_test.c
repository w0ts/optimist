/* SPDX-License-Identifier: GPL-3.0-only */
/* The backported features (firmware/src/backports.h, tools/backports.json), each section built only with its
 * switch on (tests/run_tests.sh builds this with every switch on; the goldens in regress.c check that a switch
 * at 0 leaves the sound alone, and that a switch at 1 changes nothing until the feature is used).
 *   chance   per-step chance: the 5 % grid, 0 % never, 100 % always (no random number), about half at 50 %,
 *            ratchets of a dropped step silent, TIE steps unaffected
 * Exit status: the number of failed checks. */
#define main hostsim_main
#include "hostsim.c"
#undef main

static uint64_t blk;
static int fails;
static void check(int ok, const char *what)
{
    printf("backports: %-78s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}
static void run_block(void)
{
    int32_t out[CTL * 2];
    mix_block(out, CTL);
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
    blk = 0;
    song.g[G_BPM] = (int16_t)bpm;
    song.rec = 0;
    fm1_in.notes = fm1_in.buttons = 0;
}
/* note starts on the synth parts while n blocks render */
static uint32_t starts_over(uint64_t n)
{
    uint32_t starts = 0, last = vage;
    uint64_t end = blk + n;
    while (blk < end) {
        run_block();
        starts += vage - last;
        last = vage;
    }
    return starts;
}

#if FELUCCA_CHANCE
static void t_chance(void)
{
    step_t s;
    uint32_t i, ok = 1, n, r0;
    memset(&s, 0, sizeof s);
    check(step_chance(&s) == 100u, "chance: a step written before (flags bits clear) is 100 %");
    for (i = 0; i <= 100u; i += 5u) {
        step_set_chance(&s, i);
        ok &= step_chance(&s) == i && (s.flags & (SF_ACCENT | SF_SLIDE)) == 0;
    }
    s.flags |= SF_ACCENT | SF_SLIDE;
    step_set_chance(&s, 35);
    ok &= step_chance(&s) == 35u && (s.flags & (SF_ACCENT | SF_SLIDE)) == (SF_ACCENT | SF_SLIDE);
    step_set_chance(&s, 100);
    ok &= s.flags == (SF_ACCENT | SF_SLIDE);
    check(ok, "chance: 0..100 % in 5 % steps round trip; ACCENT / SLIDE kept; 100 % clears the bits");

    /* 16 NOTE steps of 1/16 at 120 BPM, 32 passes */
    reset(120);
    host_preset(&trk[0], 0, 7);                      /* TRAP PLUCK (POLY) */
    for (i = 0; i < 16u; i++)
        put_step(&trk[0], i, 1, (const uint8_t[]){60}, ST_NOTE, 0);
    trk[0].p[P_SLEN] = 16;
    transport_req = 1;
    n = starts_over((uint64_t)div_samples(2) * 16u * 4u / CTL);
    check(n >= 63u && n <= 65u, "chance: 100 % everywhere: every step plays");
    r0 = rng_state;
    i = (uint32_t)chance_drop(&trk[0].step[0]);
    check(!i && rng_state == r0, "chance: a step at 100 % draws no random number (renders stay as before)");

    reset(120);
    host_preset(&trk[0], 0, 7);
    for (i = 0; i < 16u; i++) {
        put_step(&trk[0], i, 1, (const uint8_t[]){60}, ST_NOTE, 0);
        step_set_chance(&trk[0].step[i], 50);
    }
    trk[0].p[P_SLEN] = 16;
    transport_req = 1;
    n = starts_over((uint64_t)div_samples(2) * 16u * 32u / CTL);
    printf("backports: chance 50 %%: %u of 512 steps played\n", n);
    check(n > 200u && n < 312u, "chance: 50 % on 512 steps: 200..312 play");

    reset(120);
    host_preset(&trk[0], 0, 7);
    for (i = 0; i < 16u; i++) {
        put_step(&trk[0], i, 1, (const uint8_t[]){60}, ST_NOTE, 0);
        step_set_chance(&trk[0].step[i], 0);
    }
    trk[0].step[3].rat = 2;                          /* x3: its hits silent too */
    trk[0].p[P_SLEN] = 16;
    transport_req = 1;
    n = starts_over((uint64_t)div_samples(2) * 16u * 4u / CTL);
    check(n == 0u, "chance: 0 %: nothing plays, a x3 ratchet on a dropped step neither");

    reset(120);                                      /* a NOTE (always) then TIEs at 0 %: the tie holds */
    host_preset(&trk[0], 0, 7);
    put_step(&trk[0], 0, 1, (const uint8_t[]){60}, ST_NOTE, 0);
    for (i = 1; i < 4u; i++) {
        put_step(&trk[0], i, 0, (const uint8_t[]){0}, ST_TIE, 0);
        step_set_chance(&trk[0].step[i], 0);
    }
    trk[0].p[P_SLEN] = 4;
    transport_req = 1;
    starts_over((uint64_t)div_samples(2) * 3u / CTL);
    {
        uint32_t held = 0;
        for (i = 0; i < NVOICE; i++)
            held |= trk[0].v[i].active && trk[0].v[i].gate && trk[0].v[i].note == 60;
        check(held, "chance: a TIE is not rolled (it holds the note before it)");
    }
}
#endif

int main(void)
{
    host_tracks_init();
#if FELUCCA_CHANCE
    t_chance();
#endif
    printf("backports: %d failed\n", fails);
    return fails;
}
