/* SPDX-License-Identifier: GPL-3.0-only */
/* PHASE's FOLD (E7; builder item PHASE_FOLD, engines/phase/eng_phase.c, after Felucca-Engines 04ffe90, MIT):
 *   off       FOLD 0: the render the build without the item makes ("hash" mode: tests/run_tests.sh compares both builds)
 *   fold      FOLD 64 / 127 on a held note: brighter than FOLD 0, more as FOLD rises; bounded; no DC left
 *   pitch     above A4 it folds less: the brightening at C7 smaller than at C3
 *   switch    FOLD turned on, then off, under a held note: the block that switches steps no more than a folded block
 *   label     E7 is FOLD (0..127), not "-"
 * Exit status: the number of failed checks. */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int fails;
static void check(int ok, const char *what)
{
    printf("phase_fold: %-92s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}
static void reset(void)
{
    uint32_t i;
    seq_stop();
    transport_req = 0;
    for (i = 0; i < NTRK; i++)
        memset(trk[i].v, 0, sizeof trk[i].v);
    host_tracks_init();
    for (i = 0; i < NTRK; i++)
        steps_clear(&trk[i]);
    memset(&drums, 0, sizeof drums);
    drums_set = -2;
}

typedef struct { uint64_t hash; double rms, hf, dc; int32_t peak, jump; } pf_res_t;

/* PHASE's preset 0 (SAW-based) at note, FOLD fold, held 1 s (the first 0.1 s left out of the measures); fold2 >= 0:
 * FOLD set to it from block b2 on (the switch's block: its largest sample-to-sample step in .jump) */
static pf_res_t render(uint32_t note, int32_t fold, int32_t fold2, uint32_t b2)
{
    pf_res_t r = {1469598103934665603ull, 0, 0, 0, 0, 0};
    int32_t out[CTL * 2], last = 0;
    uint32_t b, i, nb = FS / CTL, m = 0;
    reset();
    host_preset(&trk[0], ENG_SLOT_PHASE, 0);
    trk[0].p[P_DIST] = trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = 0;
    trk[0].p[P_E7] = (int16_t)fold;
    trk_note_on(&trk[0], note, 100);
    for (b = 0; b < nb; b++) {
        if (fold2 >= 0 && b == b2)
            trk[0].p[P_E7] = (int16_t)fold2;
        mix_block(out, CTL);
        for (i = 0; i < CTL; i++) {
            int32_t x = out[2 * i], a = x < 0 ? -x : x, d = x - last;
            r.hash = (r.hash ^ (uint32_t)x) * 1099511628211ull;
            if (b * CTL + i > FS / 10u) {
                r.rms += (double)x * x;
                r.hf += (double)d * d;
                r.dc += x;
                m++;
                if (a > r.peak)
                    r.peak = a;
                if ((fold2 < 0 || b == b2) && (d < 0 ? -d : d) > r.jump)
                    r.jump = d < 0 ? -d : d;
            }
            last = x;
        }
    }
    r.hf = sqrt(r.hf / (r.rms + 1));                  /* (the first difference's energy over the signal's: brightness) */
    r.rms = sqrt(r.rms / m);
    r.dc /= m;
    return r;
}

int main(int argc, char **argv)
{
    pf_res_t r0, r64, r127, h0, h127, on, off, steady;
    if (argc > 1 && !strcmp(argv[1], "hash")) {
        printf("%016llx\n", (unsigned long long)render(48, 0, -1, 0).hash);
        return 0;
    }
#if FELUCCA_PHASE_FOLD
    check(!strcmp(ENGINES[ENG_SLOT_PHASE]->edit[7].label, "FOLD") && ENGINES[ENG_SLOT_PHASE]->edit[7].max == 127,
          "E7 is FOLD, 0..127");
    r0 = render(48, 0, -1, 0);
    r64 = render(48, 64, -1, 0);
    r127 = render(48, 127, -1, 0);
    printf("phase_fold: C3 brightness FOLD 0 / 64 / 127: %.3f %.3f %.3f; rms %.0f %.0f %.0f; peak %d %d %d; dc %.0f %.0f\n",
           r0.hf, r64.hf, r127.hf, r0.rms, r64.rms, r127.rms, r0.peak, r64.peak, r127.peak, r64.dc, r127.dc);
    check(r64.hf > r0.hf * 1.3 && r127.hf > r64.hf, "FOLD 64 brighter than 0, FOLD 127 brighter still");
    check(r127.peak < 900000 && r127.rms > r0.rms * 0.5, "FOLD 127: bounded, not quieter than half of FOLD 0");
    check(fabs(r127.dc) < r127.rms * 0.02 && fabs(r64.dc) < r64.rms * 0.02, "no DC left (the blocker)");
    h0 = render(96, 0, -1, 0);
    h127 = render(96, 127, -1, 0);
    printf("phase_fold: brightening FOLD 127 / 0: C3 x%.2f, C7 x%.2f\n", r127.hf / r0.hf, h127.hf / h0.hf);
    check(h127.hf / h0.hf < r127.hf / r0.hf, "above A4 it folds less (C7 brightened less than C3)");
    steady = render(48, 64, -1, 0);
    on = render(48, 0, 64, FS / CTL / 2u);
    off = render(48, 64, 0, FS / CTL / 2u);
    printf("phase_fold: largest step: folded %d, the block turning it on %d, off %d\n", steady.jump, on.jump, off.jump);
    check(on.jump <= steady.jump * 3 / 2 && off.jump <= steady.jump * 3 / 2, "turning FOLD on or off mid-note: no click (crossfaded)");
#else
    (void)r0, (void)r64, (void)r127, (void)h0, (void)h127, (void)on, (void)off, (void)steady;
    check(ENGINES[ENG_SLOT_PHASE]->edit[7].max == 0, "PHASE_FOLD off: E7 is empty as before");
#endif
    printf("phase_fold: %d failed\n", fails);
    return fails != 0;
}
