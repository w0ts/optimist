/* SPDX-License-Identifier: GPL-3.0-only */
/* The SLOOP 2.3 / X0X 0.10.1 backports (firmware/src/backports23.h), each with its switch on:
 *   mono     a key let go just after a VOICE change leaves no stuck note (FELUCCA_MONO_RELEASE)
 * Exit status: the number of failed checks. */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int fails;
static void check(int ok, const char *what)
{
    printf("bp23: %-74s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}
static void run_block(void)
{
    int32_t out[CTL * 2];
    mix_block(out, CTL);
}
static void run_ms(uint32_t ms)
{
    uint32_t k, n = ms * (FS / 1000u) / CTL + 1u;
    for (k = 0; k < n; k++)
        run_block();
}
static uint32_t gated(const track_t *t)
{
    uint32_t i, n = 0;
    for (i = 0; i < NVOICE; i++)
        n += t->v[i].active && t->v[i].gate;
    return n;
}

/* MONO, a key held; VOICE -> POLY; the key let go; VOICE -> MONO; another key pressed and let go: nothing
 * may still sound (2.2: the first key stayed in the MONO stack and came back, held for ever) */
static void t_mono_release(void)
{
    track_t *t = &trk[0];
    host_tracks_init();
    t->p[P_VOICE] = V_MONO;
    trk_note_on(t, 60, 100);
    run_ms(20);
    t->p[P_VOICE] = V_POLY;
    trk_note_off(t, 60);
    run_ms(20);
    t->p[P_VOICE] = V_MONO;
    trk_note_on(t, 64, 100);
    run_ms(20);
    trk_note_off(t, 64);
    run_ms(20);
    check(gated(t) == 0u && t->nmono == 0u, "VOICE MONO -> POLY with a key held, let go, back to MONO: no stuck note");
}

int main(void)
{
    t_mono_release();
    printf("%s\n", fails ? "BP23 TEST FAILED" : "bp23 test passed");
    return fails;
}
