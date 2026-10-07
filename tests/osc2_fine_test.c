/* SPDX-License-Identifier: GPL-3.0-only */
/* Osc 2 follows osc 1's fine pitch (the fix after isod89/sloop-fm1 PR #45, Erick Buendia Barrientos): the
 * interval between the two oscillators of ANALOG (ANALOG 2, or the original ANALOG with -DFELUCCA_ANALOG2=0)
 * and PHASE stays DTN cents under the fine TUNE, the UNISON detune, a glide and the LFO's pitch.
 * Measured from the phases the engine keeps: over one block, osc 1 moves v->ph[0] by inc1 * CTL, osc 2 moves
 * v->ph[1] by inc2 * CTL; the interval is 1200 log2(dph1 / dph0) cents, exact for every block and voice.
 *   osc2_fine_test          the cases, the worst error each; exit 1 past ERR_MAX cents */
#define main hostsim_main
#include "hostsim.c"
#undef main

#define DTN 10                   /* cents, osc 2 above osc 1 */
#define ERR_MAX 0.6              /* cents: the increments' resolution (fine_inc 1/4096: 0.42 ct) */

typedef struct { uint32_t ph0, ph1; } snap_t;

/* one part on engine e: sine oscillators, DTN cents on det_p, osc 2 half in the mix, SUS 127, no FX */
static void setup(track_t *t, uint32_t e, uint32_t det_p)
{
    uint32_t i;
    host_tracks_init();
    memset(t->v, 0, sizeof t->v);
    host_preset(t, e, 0);
    for (i = 0; i < 8u; i++)
        t->p[P_E0 + i] = 0;
    t->p[P_E0] = 3;                                   /* (ANALOG: SIN; PHASE: its third wave) */
    t->p[det_p] = DTN;
    if (det_p == P_E1)                                /* ANALOG: MIX 64, the filter open */
        t->p[P_E2] = 64, t->p[P_E4] = 127;
    t->p[P_ATK] = 0, t->p[P_DEC] = 60, t->p[P_SUS] = 127, t->p[P_REL] = 20;
    t->p[P_ED_FLT] = t->p[P_ED_PIT] = t->p[P_ED_SHP] = 0;
    t->p[P_VOICE] = V_POLY, t->p[P_GLIDE] = 0, t->p[P_TRANS] = 0, t->p[P_DETUNE] = 0;
    t->p[P_DIST] = t->p[P_CHOR] = t->p[P_DLY] = t->p[P_REV] = 0;
    t->p[P_LD_PIT] = t->p[P_LD_FLT] = t->p[P_LD_SHP] = t->p[P_LD_AMP] = 0;
#if FELUCCA_ANALOG2
    if (det_p == P_E1)
        t->p[P_A2DRFT] = 0, t->p[P_A2SWRM] = 0, t->p[P_A2SEMI] = 0, t->p[P_A2SYNC] = 0;
#endif
}

/* render blocks blocks; the worst |interval - DTN| over every sounding voice and block, in cents */
static double worst_err(track_t *t, uint32_t blocks, double *seen)
{
    int32_t o[CTL];
    snap_t s[NVOICE];
    double w = 0;
    uint32_t b, i;
    for (b = 0; b < blocks; b++) {
        for (i = 0; i < NVOICE; i++)
            s[i].ph0 = t->v[i].ph[0], s[i].ph1 = t->v[i].ph[1];
        track_render(t, o, CTL);
        for (i = 0; i < NVOICE; i++) {
            const voice_t *v = &t->v[i];
            uint32_t d0 = v->ph[0] - s[i].ph0, d1 = v->ph[1] - s[i].ph1;
            double c, e;
            if (!v->active || v->stage < 1u || v->stage > 2u || !d0 || b < 2u)
                continue;
            c = 1200.0 * log2((double)d1 / (double)d0);
            e = fabs(c - DTN);
            if (e > w)
                w = e, *seen = c;
        }
    }
    return w;
}

static int report(const char *eng, const char *what, double w, double seen)
{
    int ok = w < ERR_MAX;
    printf("%-8s %-34s osc 2 - osc 1: worst %+7.2f ct (DTN %d): error %5.2f ct %s\n", eng, what, seen, DTN, w,
           ok ? "ok" : "FAIL");
    return ok;
}

static int engine_cases(const char *eng, uint32_t e, uint32_t det_p)
{
    track_t *t = &trk[0];
    double w, seen = DTN;
    int ok = 1;

    setup(t, e, det_p);
    trk_note_on(t, 60, 100);
    w = worst_err(t, 40, &seen);
    ok &= report(eng, "plain", w, seen);

    setup(t, e, det_p);
    song.g[G_TUNE] = 37;                              /* 5/16 st + 5.75 ct of fine */
    trk_note_on(t, 60, 100);
    seen = DTN, w = worst_err(t, 40, &seen);
    ok &= report(eng, "TUNE +37 ct", w, seen);

    setup(t, e, det_p);
    t->p[P_VOICE] = V_UNISON, t->p[P_DETUNE] = 127;   /* up to ~+-40 ct a voice */
    trk_note_on(t, 60, 100);
    seen = DTN, w = worst_err(t, 40, &seen);
    ok &= report(eng, "UNISON DETUNE 127", w, seen);

    setup(t, e, det_p);
    t->p[P_VOICE] = V_LEGATO, t->p[P_GLIDE] = 70;
    trk_note_on(t, 48, 100);
    worst_err(t, 20, &seen);
    trk_note_on(t, 60, 100);                          /* an octave up, through every fraction */
    seen = DTN, w = worst_err(t, 400, &seen);
    ok &= report(eng, "glide C3 -> C4", w, seen);

    setup(t, e, det_p);
    t->p[P_LD_PIT] = 24, t->p[P_LRATE] = 70;
    trk_note_on(t, 60, 100);
    seen = DTN, w = worst_err(t, 800, &seen);
    ok &= report(eng, "LFO pitch", w, seen);
    return ok;
}

int main(void)
{
    int ok = 1;
#if FELUCCA_ANALOG2
    ok &= engine_cases("ANALOG2", ENG_SLOT_ANALOG, P_E1);
#else
    ok &= engine_cases("ANALOG", ENG_SLOT_ANALOG, P_E1);
#endif
#if FELUCCA_ENG_PHASE
    ok &= engine_cases("PHASE", ENG_SLOT_PHASE, P_E4);
#endif
    return ok ? 0 : 1;
}
