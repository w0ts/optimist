/* SPDX-License-Identifier: GPL-3.0-only */
/* REC QUANTIZE (core.h G_RECQ, the TEMPO screen's second page; seq.c rec_exact, rec_nudge, ft_nudge), built with PLOCK and
 * MICRO:
 *   OFF     a note pressed late keeps its start as its step's nudge and its held length (a GATE lock): it plays as
 *           played; the free take too
 *   ON      (the default) today's: the start on the grid, whole steps, no nudge, no lock
 *   project QUANT through a project and back, packed in the G_INFO slot (bit 0 = OFF); a project from before (0
 *           there) loads ON; the slot itself loads 0 (the CPU read-out's value, as an older build clamps it)
 * Exit status: the failed checks. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
static uint32_t trk_def_engine(uint32_t i)
{
    static const uint8_t E[NPART] = {0, 1, 3};
    return i < NPART ? E[i] : 0u;
}
#include "../firmware/src/storage/project.c"

#if !FELUCCA_PLOCK || !FELUCCA_MICRO
#error "recq_test: build with -DFELUCCA_PLOCK=1 -DFELUCCA_MICRO=1"
#endif

static int fails;
static void check(int ok, const char *what)
{
    printf("recq: %-96s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}
static void say(const char *what, int32_t nudge, int32_t gate, double played)
{
    printf("recq: %-40s nudge %3d, GATE lock %4d, plays %.2f steps\n", what, (int)nudge, (int)gate, played);
}

/* live: t1 with QUANT OFF, then t2 with QUANT ON, the same two notes, each pressed 0.35 into its step (0.26 as heard:
 * REC_LAT is 0.093 step at 120): held 0.6 step from step 4, 1.6 steps from step 8 */
static void t_live(void)
{
    track_t *t1 = &trk[0], *t2 = &trk[1];
    int32_t n4, n8, g4, g9;
    double p4, p8;
    host_tracks_init();
    host_preset(t1, 0, 1);
    host_preset(t2, 0, 1);
    t1->p[P_VOICE] = t2->p[P_VOICE] = V_POLY;
    song.rec = 0x03u;
    transport_req = 1;
    song.g[G_RECQ] = 0;                                 /* OFF */
    rl_hold(t1, 60, 4, 35, 4, 95);
    rl_hold(t1, 62, 8, 35, 9, 95);
    song.g[G_RECQ] = 1;                                 /* ON (the default) */
    rl_hold(t2, 60, 4, 35, 4, 95);
    rl_hold(t2, 62, 8, 35, 9, 95);
    rec_run_to(t2, 11, 50);
    song.rec = 0;
    n4 = step_micro(t1, 4), n8 = step_micro(t1, 8), g4 = rl_gate(t1, 4), g9 = rl_gate(t1, 9);
    p4 = rl_played(t1, 4), p8 = rl_played(t1, 8);
    say("OFF: held 0.6 from 0.26 late", n4, g4, p4);
    say("OFF: held 1.6 from 0.26 late", n8, g9, p8);
    check(n4 >= 15 && n4 <= 18 && rec_step_is(t1, 4, ST_NOTE, 1, 60) && t1->step[5].time != ST_TIE && g4 >= 75 && g4 <= 79 &&
          fabs(p4 - 0.6) < 0.04, "OFF: held 0.6, pressed late: its start as the nudge (16/64), the NOTE locked 77, plays 0.6");
    check(n8 >= 15 && n8 <= 18 && rec_step_is(t1, 9, ST_TIE, 0, 0) && g9 >= 43 && g9 <= 48 && fabs(p8 - 1.6) < 0.04,
          "OFF: held 1.6, pressed late: the nudge, NOTE TIE with its lock, plays 1.6 (the time held)");
    n4 = step_micro(t2, 4), n8 = step_micro(t2, 8), g4 = rl_gate(t2, 4), g9 = rl_gate(t2, 9);
    p4 = rl_played(t2, 4), p8 = rl_played(t2, 8);
    say("ON: held 0.6 from 0.26 late", n4, g4, p4);
    say("ON: held 1.6 from 0.26 late", n8, g9, p8);
    check(!n4 && !n8 && g4 < 0 && g9 < 0 && rec_step_is(t2, 4, ST_NOTE, 1, 60) && t2->step[5].time != ST_TIE &&
          rec_step_is(t2, 9, ST_TIE, 0, 0) && fabs(p4 - 0.5) < 0.04 && fabs(p8 - 2.0) < 0.04,
          "ON: as before this: on the grid, whole steps (one NOTE; NOTE TIE), no nudge, no lock, the track's GATE");
}

/* the free take, QUANT OFF: a note 0.25 step late held 0.6 keeps both; ON: on the grid, whole steps */
static void t_free(void)
{
    track_t *t1 = &trk[0];
    int32_t o[2 * CTL];
    uint32_t b, pass, T = 2u * FS / CTL;
    double sb = (double)T / 16.0;
    for (pass = 0; pass < 2u; pass++) {
        int32_t n, g;
        double p;
        host_tracks_init();
        host_preset(t1, 0, 1);
        t1->p[P_VOICE] = V_POLY;
        song.g[G_RECQ] = (int16_t)pass;                 /* 0 OFF, 1 ON */
        song.sel = 0, song.playing = 0, song.rec = 0;
        rec_wait = 1;
        for (b = 0; b < T; b++) {
            if (b == 0u)
                input_on(t1, 48, 100);                  /* (the take starts here: its first note on the grid) */
            if (b == (uint32_t)(0.5 * sb + 0.5))
                input_off(t1, 48);
            if (b == (uint32_t)(4.25 * sb + 0.5))
                input_on(t1, 60, 100);
            if (b == (uint32_t)(4.85 * sb + 0.5))
                input_off(t1, 60);
            mix_block(o, CTL);
        }
        ft_close();
        n = step_micro(t1, 4), g = rl_gate(t1, 4), p = rl_played(t1, 4);
        say(pass ? "free take ON: 0.25 late, held 0.6" : "free take OFF: 0.25 late, held 0.6", n, g, p);
        if (pass)
            check(ft_bars == 1u && !n && g < 0 && fabs(p - 0.5) < 0.04, "free take ON: on the grid, the GATE, as before");
        else
            check(ft_bars == 1u && n == 16 && g >= 75 && g <= 79 && fabs(p - 0.6) < 0.04,
                  "free take OFF: the nudge (16/64) and the length (the NOTE locked 77: 0.6)");
        song.playing = 0;
        transport_req = 0;
    }
}

/* the project: QUANT saved and loaded in the G_INFO slot */
static void t_project(void)
{
    static project_t pj;
    static dlrec_t dl;
    int ok;
    host_tracks_init();
    check(song.g[G_RECQ] == 1, "the default: ON");
    song.g[G_RECQ] = 0;
    proj_capture(&pj, &dl);
    ok = pj.g[G_INFO] == 1 && sizeof pj.g == 32u * 2u;
    song.g[G_RECQ] = 1;
    proj_apply(&pj, &dl, 1);
    check(ok && song.g[G_RECQ] == 0 && song.g[G_INFO] == 0,
          "OFF saved (G_INFO slot 1, g[] 32 as before) and loaded; the slot itself loads 0 (the CPU read-out's, clamped)");
    song.g[G_RECQ] = 1;
    proj_capture(&pj, &dl);
    ok = pj.g[G_INFO] == 0;
    song.g[G_RECQ] = 0;
    proj_apply(&pj, &dl, 1);
    check(ok && song.g[G_RECQ] == 1, "ON saved (the slot 0, as every project before) and loaded");
    pj.g[G_INFO] = 0;                                   /* a project from before */
    song.g[G_RECQ] = 0;
    proj_apply(&pj, &dl, 1);
    check(song.g[G_RECQ] == 1, "a project from before (0 there): ON");
    {
        uint32_t i, kept = 1;                           /* what an older build does with the slot: the same loop, 0..0 */
        pj.g[G_INFO] = 1;
        for (i = 0; i < PJ_NG; i++)
            if (i == G_INFO)
                kept = clamp(pj.g[i], GP[i].min, GP[i].max) == 0;
        check(kept && GP[G_INFO].min == 0 && GP[G_INFO].max == 0,
              "an older build loading it: G_INFO clamps to 0..0 (no QUANT there: ON), nothing else reads the slot");
    }
}

int main(void)
{
    t_project();
    t_live();
    t_free();
    printf("recq: %s\n", fails ? "FAILED" : "all checks ok");
    return fails;
}
