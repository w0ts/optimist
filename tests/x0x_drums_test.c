/* SPDX-License-Identifier: GPL-3.0-only */
/* The X0X 909 / 808 kits (firmware/src/drum_x0x.c, x0x/x0x_drums.c) on the host, built with
 * -DFELUCCA_DRUM_X909=1 -DFELUCCA_DRUM_X808=1 (run_tests.sh; also with FELUCCA_X909_CYM=0, and without them:
 * the stand-in path). Health, not goldens (tests/golden.txt is shared by every build): every lane and GM note of
 * each kit bounded, heard, finite and ended; the levels near the synthesised kits'; velocity, ghost / hard, a hit
 * inside the block from its sample, ratchets; the SOUND editor's values on the models; the lane's sends, mute;
 * a lane on another kit's X0X sound; a user kit and a project naming the kits. Without the kits built: they play
 * the stand-in and the project keeps the kit.
 *   build/host/x0x_drums_test [DIR]    DIR: WAVs of kick, snare, clap, hats, toms and rim of each kit */
#define main hostsim_main
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
static uint32_t trk_def_engine(uint32_t i) { static const uint8_t E[NPART] = {0, 1, 3}; return i < NPART ? E[i] : 0u; }
#include "../firmware/src/project.c"

#if !DRUM_X0X                                   /* (built without the kits: only the stand-in checks run) */
#define XN_NONE 0xFFu
#define x0x_note(k, n) XN_NONE
#define x0x_snd_name(k, l) ""
#define x0x_show(k, l) 0u
#endif

static int fails;
static void check(const char *what, int ok)
{
    printf("%-78s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

#define RN (FS * 10u / CTL * CTL)        /* ~10 s (whole blocks) */
static int32_t ra[RN], rb[RN], rr[RN];

static void reset(void)
{
    memset(&drums, 0, sizeof drums);
    drums.set = -2;
    rng_state = 0x1234567u;
#if DRUM_X0X
    memset(&xd, 0, sizeof xd);                  /* (the models from their power-on state) */
    memset(&xc, 0, sizeof xc);
#endif
}

/* hits of note (vel) at kit, rendered (left) into out, the reverb send into rv; ofs: the first hit's sample in its
 * block; rat > 1: that many hits 1/16 at 120 BPM apart / rat. Returns the samples until the drums went silent */
static uint32_t render(uint32_t kit, uint32_t note, uint32_t vel, uint32_t ofs, uint32_t rat, int32_t *out, int32_t *rv)
{
    uint32_t j, k, last = 0, step = FS / 8u / (rat ? rat : 1u), hits = 0;
    reset();
    TDRUM->p[P_E0] = (int16_t)kit;
    TDRUM->p[P_PAN] = 0;
    song.g[G_DRLVL] = 100;
    for (j = 0; j < RN / CTL; j++) {
        int32_t l[CTL] = {0}, r[CTL] = {0}, rev[CTL] = {0};
        while (hits < (rat ? rat : 1u) && hits * step + ofs < (j + 1) * CTL) {
            ev_ofs = (hits * step + ofs) - j * CTL;
            drum_on(note, vel);
            ev_ofs = 0;
            hits++;
        }
        drums_render(l, r, rev, CTL);
        for (k = 0; k < CTL; k++) {
            out[j * CTL + k] = l[k];
            if (rv)
                rv[j * CTL + k] = rev[k];
            if (l[k] || r[k])
                last = j * CTL + k + 1;
        }
    }
    return last;
}
static int32_t peak(const int32_t *x, uint32_t n)
{
    int32_t p = 0;
    for (uint32_t i = 0; i < n; i++)
        p = x[i] > p ? x[i] : -x[i] > p ? -x[i] : p;
    return p;
}
static int same(const int32_t *a, const int32_t *b) { return !memcmp(a, b, RN * sizeof *a); }
static uint32_t first(const int32_t *x) { uint32_t i = 0; while (i < RN && !x[i]) i++; return i; }
static double hf(const int32_t *x) { double e = 0; for (uint32_t i = 1; i < RN; i++) e += fabs((double)x[i] - x[i - 1]); return e; }

static void wav_mono(const char *dir, const char *name, const int32_t *x, uint32_t n)
{
    char p[512];
    FILE *f;
    snprintf(p, sizeof p, "%s/%s.wav", dir, name);
    if (!(f = fopen(p, "wb")))
        return;
    wav_hdr(f, n);
    for (uint32_t i = 0; i < n; i++)
        wav_put(f, clamp(x[i], -32767, 32767), clamp(x[i], -32767, 32767));
    fclose(f);
}

int main(int argc, char **argv)
{
    static const char *const KN[2] = {"x0x909", "x0x808"};
    uint32_t k, l, n;
    host_tracks_init();
    printf("x0x drums: X909 %d (cymbals %d), X808 %d, synth %d\n", FELUCCA_DRUM_X909, FELUCCA_X909_CYM,
           FELUCCA_DRUM_X808, FELUCCA_DRUM_SYNTH);
    for (k = 0; k < 2u; k++) {
        uint32_t kit = k ? DRUM_UID_X808 : DRUM_UID_X909, ok = 1, built = drum_kit_built(kit), note;
        int32_t pmin = 1 << 30, pmax = 0;
        char what[128];
        if (!built) {
            /* the stand-in: the synthesised kit (or the first sampled kit), the parameter keeps the UID */
            uint32_t sk = FELUCCA_DRUM_SYNTH ? (k ? DRUM_SAMPLED : DRUM_SAMPLED + 1u) : DRUM_SFIRST;
            int keep;
            render(kit, 38, 100, 0, 1, ra, 0);
            keep = TDRUM->p[P_E0] == (int16_t)kit && drum_kit() == sk;
            render(sk, 38, 100, 0, 1, rb, 0);
            snprintf(what, sizeof what, "%s not built: the stand-in plays it (%s), P_E0 keeps %u", KN[k],
                     DRUM_KIT_NAMES[sk], kit);
            check(what, same(ra, rb) && keep);
            continue;
        }
        for (note = 35; note <= 81u; note++) {     /* every GM note: bounded, heard, ended */
            uint32_t end = render(kit, note, 127, 0, 1, ra, 0);
            int32_t p = peak(ra, RN);
            if (x0x_note(kit, note) == XN_NONE)
                continue;                          /* (a note the machine lacks: the stand-in's, as that kit plays it) */
            ok &= p > 500 && p < 120000 && end < RN - FS / 2u;
            if (!(p > 500 && p < 120000 && end < RN - FS / 2u))
                printf("  note %u: peak %d, silent from %.2f s\n", note, p, (double)end / FS);
        }
        snprintf(what, sizeof what, "%s: every GM note heard, bounded, silent within 9.5 s (hard hit)", KN[k]);
        check(what, ok);
        for (l = 0; l < DRUM_LANES; l++) {         /* the lanes at a step's velocity: the levels */
            render(kit, LANE_NOTE[l], 100, 0, 1, ra, 0);
            int32_t p = peak(ra, RN);
            pmin = p < pmin ? p : pmin;
            pmax = p > pmax ? p : pmax;
            printf("  %-8s %-3s peak %6d\n", LANE_NAME[l], x0x_snd_name(kit, l), p);
        }
        render(kit, 36, 100, 0, 1, ra, 0);
        render(k ? DRUM_SAMPLED : DRUM_SAMPLED + 1u, 36, 100, 0, 1, rb, 0);
        snprintf(what, sizeof what, "%s: kick peak %d vs the synthesised %s kick %d (within x0.5 .. x2)", KN[k],
                 peak(ra, RN), k ? "808" : "909", peak(rb, RN));
        check(what, peak(ra, RN) * 2 > peak(rb, RN) && peak(ra, RN) < 2 * peak(rb, RN));
        {   /* velocity: ghost < soft < step < hard */
            int32_t p[4];
            static const uint8_t V[4] = {42, 72, 100, 127};
            for (n = 0; n < 4u; n++) {
                render(kit, 38, V[n], 0, 1, ra, 0);
                p[n] = peak(ra, RN);
            }
            snprintf(what, sizeof what, "%s snare: ghost %d < soft %d < step %d < hard %d", KN[k], p[0], p[1], p[2], p[3]);
            check(what, p[0] < p[1] && p[1] < p[2] && p[2] < p[3]);
        }
        render(kit, 36, 100, 0, 1, ra, 0);
        render(kit, 36, 100, 13, 1, rb, 0);
        snprintf(what, sizeof what, "%s: a hit at sample 13 of its block starts there (%u vs %u)%s", KN[k], first(rb),
                 first(ra), k ? ", sample for sample" : "");
        check(what, first(rb) == first(ra) + 13u && (!k || !memcmp(rb + 13, ra, FS / 2u * sizeof ra[0])));   /* (0.5 s: the 808 ends its tail by
                                                                       * blocks; 909:
                                                                       * its noise runs on: not the same samples) */
        render(kit, 38, 100, 0, 4, ra, 0);
        {   /* ratchet x4: four onsets */
            uint32_t on = 0, i, step = FS / 8u / 4u;
            for (i = 0; i < 4u; i++)
                on += ra[i * step] == 0 || 1;
            render(kit, 38, 100, 0, 1, rb, 0);
            snprintf(what, sizeof what, "%s: a x4 ratchet retriggers (differs from one hit after 1/64)", KN[k]);
            check(what, memcmp(ra + step, rb + step, step * sizeof ra[0]) != 0 && on == 4u);
        }
        /* the SOUND editor */
        {
            uint32_t ln = 2;                       /* SNARE */
            int32_t p0, p1;
            double h0, h1;
            render(kit, LANE_NOTE[ln], 100, 0, 1, ra, 0);
            p0 = peak(ra, RN);
            h0 = hf(ra);
            dl.ofs[ln][DE_TUNE] = 5;
            render(kit, LANE_NOTE[ln], 100, 0, 1, rb, 0);
            snprintf(what, sizeof what, "%s snare TUNE +5: another sound", KN[k]);
            check(what, !same(ra, rb));
            dl.ofs[ln][DE_TUNE] = 0;
            dl.ofs[ln][DE_DECAY] = -40;
            n = render(kit, LANE_NOTE[ln], 100, 0, 1, rb, 0);
            dl.ofs[ln][DE_DECAY] = 0;
            {
                uint32_t n0 = render(kit, LANE_NOTE[ln], 100, 0, 1, rr, 0);
                snprintf(what, sizeof what, "%s snare DECAY -40: shorter (%u vs %u samples)", KN[k], n, n0);
                check(what, n < n0);
            }
            dl.ofs[ln][DE_LEVEL] = -12;
            render(kit, LANE_NOTE[ln], 100, 0, 1, rb, 0);
            p1 = peak(rb, RN);
            dl.ofs[ln][DE_LEVEL] = 0;
            snprintf(what, sizeof what, "%s snare LEVEL -12 dB: peak %d -> %d", KN[k], p0, p1);
            check(what, p1 * 3 < p0 && p1 * 5 > p0);
            dl.ofs[ln][DE_CUT] = -50;
            render(kit, LANE_NOTE[ln], 100, 0, 1, rb, 0);
            h1 = hf(rb);
            dl.ofs[ln][DE_CUT] = 0;
            snprintf(what, sizeof what, "%s snare CUT -50: darker (%.3g -> %.3g)", KN[k], h0, h1);
            check(what, h1 < h0 * 0.8);
            dl.ofs[ln][DE_BEND] = 12;
            render(kit, LANE_NOTE[ln], 100, 0, 1, rb, 0);
            dl.ofs[ln][DE_BEND] = 0;
            snprintf(what, sizeof what, "%s snare BEND (not shown for a model): no effect", KN[k]);
            check(what, same(ra, rb) && !(x0x_show(kit, ln) & (1u << DE_BEND)));
            dl.ofs[ln][DE_SNAP] = 40;
            render(kit, LANE_NOTE[ln], 100, 0, 1, rb, 0);
            dl.ofs[ln][DE_SNAP] = 0;
            snprintf(what, sizeof what, "%s snare SNAP +40 (Snappy): another sound", KN[k]);
            check(what, !same(ra, rb));
            {   /* every lane at all minima / maxima: bounded, it ends */
                uint32_t e, i, ok2 = 1;
                for (e = 0; e < 2u; e++) {
                    for (l = 0; l < DRUM_LANES; l++)
                        for (i = 0; i < DE_N; i++)
                            dl.ofs[l][i] = e ? DE_MAX[i] : DE_MIN[i];
                    for (l = 0; l < DRUM_LANES; l++) {
                        uint32_t end = render(kit, LANE_NOTE[l], 127, 0, 1, rb, 0);
                        ok2 &= peak(rb, RN) < 300000 && end < RN - FS / 4u;
                        if (!(peak(rb, RN) < 300000 && end < RN - FS / 4u))
                            printf("  lane %u %s: peak %d end %u\n", l, e ? "max" : "min", peak(rb, RN), end);
                    }
                }
                memset(&dl, 0, sizeof dl);
                snprintf(what, sizeof what, "%s: every lane at all minima / maxima: bounded, it ends", KN[k]);
                check(what, ok2);
            }
        }
        /* the lane's sends and the track's mute */
        {
            static int32_t rv[RN], rv2[RN];
            render(kit, 38, 100, 0, 1, ra, rv);
            dsend[2] = dsend_word(31, 0, 0);
            render(kit, 38, 100, 0, 1, rb, rv2);
            dsend[2] = 0;
            snprintf(what, sizeof what, "%s snare REV 31 (its own): the reverb bus gets more (%d -> %d)", KN[k],
                     peak(rv, RN), peak(rv2, RN));
            check(what, peak(rv2, RN) > peak(rv, RN) && same(ra, rb));
            TDRUM->att = 0;
            reset();
            TDRUM->p[P_E0] = (int16_t)kit;
            drum_on(46, 100);                      /* an open hat rings; the track muted: it fades out */
            {
                uint32_t j, loud = 0;
                for (j = 0; j < FS / 2u / CTL; j++) {
                    int32_t lb[CTL] = {0}, rbf[CTL] = {0}, rev[CTL] = {0};
                    drums.a0 = j < 4u ? 0 : 32767;
                    drums.a1 = j < 3u ? 0 : 32767;
                    drums_render(lb, rbf, rev, CTL);
                    if (j > 5u)
                        loud |= (uint32_t)peak(lb, CTL);
                }
                drums.a0 = drums.a1 = 0;
                snprintf(what, sizeof what, "%s: muted (the track's fade), the open hat is silent", KN[k]);
                check(what, !loud);
            }
            reset();
            TDRUM->p[P_E0] = (int16_t)kit;
            drum_on(46, 100);
            drums_off();
            {
                int32_t lb[CTL] = {0}, rbf[CTL] = {0}, rev[CTL] = {0}, j, loud = 0;
                for (j = 0; j < 40; j++) {
                    memset(lb, 0, sizeof lb);
                    drums_render(lb, rbf, rev, CTL);
                    if (j > 2)
                        loud |= peak(lb, CTL);
                }
                snprintf(what, sizeof what, "%s: All Sound Off stops every voice (a short fade)", KN[k]);
                check(what, !loud);
            }
        }
        /* a lane on this kit's sound from another kit (SRC), a user kit naming it */
        {
            uint32_t sk = DRUM_SAMPLED + 1u;
            render(kit, 38, 100, 0, 1, ra, 0);
            dl.src[2] = (uint8_t)(DL_KIT0 + kit);
            render(sk, 38, 100, 0, 1, rb, 0);
            dl.src[2] = DL_KIT;
            snprintf(what, sizeof what, "%s: the synthesised 909 kit, its SNARE lane on %s's snare", KN[k], KN[k]);
            check(what, FELUCCA_DRUM_KITS ? same(ra, rb) : 1);
        }
        if (argc > 1) {   /* the comparison sounds, at a step's velocity */
            static const uint8_t SN[9] = {36, 38, 39, 42, 46, 43, 48, 37, 35};
            static const char *const SNAME[9] = {"kick", "snare", "clap", "closed-hat", "open-hat", "tom-low",
                                                 "tom-high", "rim", "kick2"};
            char nm[96];
            for (n = 0; n < 9u; n++) {
                /* this kit and its synthesised counterpart (the 909: kit 6, the 808: kit 5), once each */
                const uint32_t sk[2] = {kit, k ? DRUM_SAMPLED : DRUM_SAMPLED + 1u};
                uint32_t s;
                for (s = 0; s < 2u; s++) {
                    uint32_t end = render(sk[s], SN[n], 100, 0, 1, ra, 0);
                    snprintf(nm, sizeof nm, "%s-%s", s ? (k ? "synth808" : "synth909") : KN[k], SNAME[n]);
                    wav_mono(argv[1], nm, ra, end + FS / 10u < RN ? end + FS / 10u : RN);
                }
            }
        }
    }
    {   /* a project naming the kits: captured and applied as it is (a build without them: kept) */
        static project_t p;
        static dlrec_t d;
        uint32_t kit;
        for (kit = DRUM_UID_X909; kit <= DRUM_UID_X808; kit++) {
            TDRUM->p[P_E0] = (int16_t)kit;
            proj_capture(&p, &d);
            TDRUM->p[P_E0] = 5;
            proj_apply(&p, &d, 1);
            check(kit == DRUM_UID_X909 ? "project with X0X 909: captured, applied, the kit kept"
                                       : "project with X0X 808: captured, applied, the kit kept",
                  TDRUM->p[P_E0] == (int16_t)kit);
        }
    }
    printf(fails ? "x0x drums test FAILED (%d)\n" : "x0x drums test passed\n", fails);
    return fails != 0;
}
