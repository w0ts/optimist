/* SPDX-License-Identifier: GPL-3.0-only */
/* A soft note's release, MASTER at the top (issue 132; after SLOOP 2.8.4's "clean fades", isod89/sloop-fm1 91f3018):
 * the error of the tail against a float reference, in LSB of the 16-bit output (Q15).
 * Each case holds an ANALOG sine (no drive, filter open, no sends, no inserts), lets it go on a long release and renders
 * the codec's output (mix_block: the Q15 sample and its 7 bits under, audio.c's 24). Over windows of
 * WIN samples it fits the note by least squares in doubles (H harmonics, each amplitude a quadratic in time, plus a
 * line for the DC blocker's leftovers): the fit is the reference, what is left the error. Reported a window: its level
 * (dBFS of a sine as loud), the error's rms in LSB and the THD+N. A plain 16-bit rounding is 0.29 LSB rms.
 *   fade_tail_test          the table of every case (information)
 *   fade_tail_test check    pass / fail: the worst error between -45 and -80 dBFS under BOUND LSB rms, and the window
 *                           nearest -70 dBFS with THD+N under BOUND_THDN dB (tests/run_tests.sh)
 * Exit status: the number of failed checks. */
#define main hostsim_main
#include "hostsim.c"
#undef main

#define WIN 4096
#define NBLK 9000u                                      /* 6.5 s */
#ifndef REL
#define REL 100                                         /* (the release: about 70 dB in 3 s) */
#endif
#ifndef VSOFT
#define VSOFT 12
#endif
#ifndef BOUND
#define BOUND 0.6                                       /* LSB rms (the floored products before: 0.85 to 1.3; rounded 0.48) */
#endif
#ifndef BOUND_THDN
#define BOUND_THDN (-20.0)                              /* dB, at -70 dBFS (the floored products before: -17 to -18 at MASTER max) */
#endif
static double cap[NBLK * CTL];
static double incq[NBLK];                               /* the note's phase step a sample this block (2^32 a turn) */
static int fails, checking;

/* least squares: H harmonics of w (radians a sample), each with a constant, linear and quadratic amplitude, and a line;
 * returns the rms of what is left, *sig the fit's rms (the note) */
static double fit_left(const double *y, double w, int H, double *sig)
{
    enum { KMAX = 6 * 4 + 2 };
    static double A[KMAX][KMAX], bv[KMAX], phi[KMAX], c[KMAX];
    int K = 6 * H + 2, i, j, k, n;
    double e2 = 0, s2 = 0;
    memset(A, 0, sizeof A), memset(bv, 0, sizeof bv);
    for (n = 0; n < WIN; n++) {
        double t = (n - WIN / 2.0) / WIN;
        for (i = 1, k = 0; i <= H; i++) {
            double s = sin(i * w * n), cs = cos(i * w * n);
            phi[k++] = s, phi[k++] = cs, phi[k++] = s * t, phi[k++] = cs * t, phi[k++] = s * t * t, phi[k++] = cs * t * t;
        }
        phi[k++] = 1, phi[k++] = t;
        for (i = 0; i < K; i++) {
            bv[i] += phi[i] * y[n];
            for (j = 0; j <= i; j++)
                A[i][j] += phi[i] * phi[j];
        }
    }
    for (i = 0; i < K; i++)                             /* Cholesky, then the two triangular solves */
        for (j = 0; j <= i; j++) {
            double s = A[i][j];
            for (k = 0; k < j; k++)
                s -= A[i][k] * A[j][k];
            A[i][j] = i == j ? sqrt(s > 1e-30 ? s : 1e-30) : s / A[j][j];
        }
    for (i = 0; i < K; i++) {
        double s = bv[i];
        for (k = 0; k < i; k++)
            s -= A[i][k] * c[k];
        c[i] = s / A[i][i];
    }
    for (i = K - 1; i >= 0; i--) {
        double s = c[i];
        for (k = i + 1; k < K; k++)
            s -= A[k][i] * c[k];
        c[i] = s / A[i][i];
    }
    for (n = 0; n < WIN; n++) {
        double t = (n - WIN / 2.0) / WIN, f = 0, e;
        for (i = 1, k = 0; i <= H; i++, k += 6) {
            double s = sin(i * w * n), cs = cos(i * w * n);
            f += (c[k] + c[k + 2] * t + c[k + 4] * t * t) * s + (c[k + 1] + c[k + 3] * t + c[k + 5] * t * t) * cs;
        }
        e = y[n] - f - c[K - 2] - c[K - 1] * t;
        e2 += e * e;
        s2 += f * f;
    }
    *sig = sqrt(s2 / WIN);
    return sqrt(e2 / WIN);
}

static void check(int ok, const char *what)
{
    printf("fade: %-100s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

/* vel: the note's velocity; lvl: the part's LEVEL (104: the default, -4 dB); master: MASTER (Q12) */
static void run(const char *name, uint32_t vel, int lvl, int32_t master, int show)
{
    static int32_t o[2 * CTL];
    static double y[WIN];
    track_t *t = &trk[0];
    uint32_t b, i, n, held = 300u, nb = 0;
    double worst = 0, wlev = 0, d70 = 1e9, at70 = 0, thdn70 = 0, mark = -10;
    char s[240];
    host_tracks_init();
    for (i = 0; i < NVOICE; i++)
        memset(&t->v[i], 0, sizeof t->v[i]);
    t->nmono = 0, t->mono_note = 0;
    song.master_q12 = (uint32_t)master;
    master_cur = -1;
    host_preset(t, 0, 0);                               /* ANALOG 808 BOOM, made a plain sine: */
    t->p[P_E0] = 3, t->p[P_E1] = 0, t->p[P_E2] = 0, t->p[P_E3] = 0, t->p[P_E4] = 127, t->p[P_E5] = 0;
    t->p[P_E6] = 0, t->p[P_E7] = 0, t->p[P_ED_PIT] = 0, t->p[P_ED_FLT] = 0, t->p[P_GLIDE] = 0;
    t->p[P_ATK] = 0, t->p[P_DEC] = 0, t->p[P_SUS] = 127, t->p[P_REL] = REL;
    t->p[P_DIST] = 0, t->p[P_CHOR] = t->p[P_DLY] = t->p[P_REV] = 0, t->p[P_TRANS] = 0;
    t->p[P_LEVEL] = (int16_t)lvl, t->p[P_ED_FX] = 0, t->p[P_PAN] = 0;
    for (b = 0; b < 8; b++)
        mix_block(o, CTL);
    trk_note_on(t, 69, vel);                            /* A4, 440 Hz */
    for (b = 0; b < NBLK; b++) {
        uint32_t ph = t->v[0].ph[0];
        if (b == held)
            trk_note_off(t, 69);
        mix_block(o, CTL);
        incq[b] = (double)(uint32_t)(t->v[0].ph[0] - ph) / CTL;
        for (i = 0; i < CTL; i++) {
            cap[b * CTL + i] = o[2 * i] + mst_frac[2 * i] / 128.0;   /* (the DAC's 24 bits: audio.c audio_block) */
        }
        if (t->v[0].active)
            nb = b + 1u;
    }
    if (show)
        printf("%s (vel %u, LEVEL %d, MASTER %d): the voice sounds %.2f s after the release\n", name, vel, lvl, master,
               (nb - held) * CTL / (double)FS);
    for (n = (held + 8u) * CTL; n + WIN <= nb * CTL; n += WIN) {
        double rms = 0, lev, left, sig, thdn;
        for (i = 0; i < WIN; i++)
            y[i] = cap[n + i], rms += y[i] * y[i];
        lev = 20 * log10(sqrt(rms / WIN) * sqrt(2.0) / 32767 + 1e-12);
        left = fit_left(y, 2 * M_PI * incq[(n + WIN / 2) / CTL] / 4294967296.0, 3, &sig);
        thdn = 20 * log10(left / (sig + 1e-12));
        if (show && lev < mark + 0.5) {                 /* (the window nearest each 10 dB down) */
            printf("  %6.2f s  %7.1f dBFS  error %6.3f LSB rms  THD+N %6.1f dB\n", (double)(n - held * CTL) / (double)FS,
                   lev, left, thdn);
            while (lev < mark + 0.5)
                mark -= 10;
        }
        if (lev <= -45 && lev >= -80 && left > worst)
            worst = left, wlev = lev;
        if (fabs(lev + 70) < d70)
            d70 = fabs(lev + 70), at70 = lev, thdn70 = thdn;
    }
    if (!checking)
        return;
    snprintf(s, sizeof s, "%s: the tail's error (-45..-80 dBFS) at most %.3f LSB rms (at %.0f dBFS), bound %.2f", name,
             worst, wlev, BOUND);
    check(worst > 0 && worst < BOUND, s);
    snprintf(s, sizeof s, "%s: THD+N at %.0f dBFS %.1f dB, bound %.0f", name, at70, thdn70, BOUND_THDN);
    check(d70 < 4 && thdn70 < BOUND_THDN, s);
}

int main(int argc, char **argv)
{
    checking = argc > 1 && !strcmp(argv[1], "check");
    run("soft note", VSOFT, 104, 4096, !checking);
    run("soft note, LEVEL 0 dB", VSOFT, 112, 4096, !checking);
    run("loud note", 100, 104, 4096, !checking);
    run("soft note, MASTER 2048", VSOFT, 104, 2048, !checking);
    printf("fade: %d failed\n", fails);
    return fails;
}
