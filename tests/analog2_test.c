/* SPDX-License-Identifier: GPL-3.0-only */
/* ANALOG 2 measurements on the Mac, against the original ANALOG (build this file with -DFELUCCA_ANALOG2=0 for
 * the old engine; same sources as the firmware, through hostsim.c):
 *   analog2_test alias     saw / square at C7 (and hard sync at C6, ANALOG 2): the power off the harmonics
 *   analog2_test filter    the filter kernel's response (impulse, FFT) and self-oscillation at RES 126 / 127
 *   analog2_test zipper    a fast filter envelope on a sine: the power at the block rate (1378 Hz) and its multiples
 *   analog2_test check     the above as pass / fail limits (tests/run_tests.sh)
 *   analog2_test wav E P OUT.wav [id:v,..]   engine E preset P (the whole preset, its sends; then the track
 *                          parameters given) on a phrase, 5 s, to a WAV; prints its RMS and peak */
#define main hostsim_main
#include "hostsim.c"
#undef main

#define NFFT 16384
static double re_[NFFT], im_[NFFT];
static void fft(double *re, double *im, uint32_t n)
{
    uint32_t i, j = 0, len, k;
    for (i = 1; i < n; i++) {
        uint32_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j) {
            double t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (len = 2; len <= n; len <<= 1) {
        double a = -2 * M_PI / len, wr = cos(a), wi = sin(a);
        for (i = 0; i < n; i += len) {
            double cr = 1, ci = 0;
            for (k = 0; k < len / 2; k++) {
                double ur = re[i + k], ui = im[i + k];
                double vr = re[i + k + len / 2] * cr - im[i + k + len / 2] * ci;
                double vi = re[i + k + len / 2] * ci + im[i + k + len / 2] * cr;
                double t;
                re[i + k] = ur + vr; im[i + k] = ui + vi;
                re[i + k + len / 2] = ur - vr; im[i + k + len / 2] = ui - vi;
                t = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = t;
            }
        }
    }
}
/* power spectrum of x[0..NFFT) with a Blackman-Harris window into re_ (bins 0..NFFT/2) */
static void spectrum(const double *x)
{
    uint32_t i;
    for (i = 0; i < NFFT; i++) {
        double w = 2 * M_PI * i / (NFFT - 1);
        re_[i] = x[i] * (0.35875 - 0.48829 * cos(w) + 0.14128 * cos(2 * w) - 0.01168 * cos(3 * w));
        im_[i] = 0;
    }
    fft(re_, im_, NFFT);
    for (i = 0; i <= NFFT / 2; i++)
        re_[i] = re_[i] * re_[i] + im_[i] * im_[i];
}
static double db(double p) { return 10 * log10(p > 1e-30 ? p : 1e-30); }

/* one part, plain: ANALOG with the values given, no FX, POLY, SUS 127 */
static void part_setup(track_t *t, const int16_t *e8)
{
    uint32_t i;
    host_tracks_init();
    memset(t->v, 0, sizeof t->v);                     /* (no voice of the measurement before) */
    host_preset(t, 0, 0);
    for (i = 0; i < 8u; i++)
        t->p[P_E0 + i] = e8[i];
    t->p[P_ATK] = 0, t->p[P_DEC] = 60, t->p[P_SUS] = 127, t->p[P_REL] = 20;
    t->p[P_ED_FLT] = t->p[P_ED_PIT] = t->p[P_ED_SHP] = 0;
    t->p[P_VOICE] = V_POLY, t->p[P_GLIDE] = 0, t->p[P_TRANS] = 0;
    t->p[P_DIST] = t->p[P_CHOR] = t->p[P_DLY] = t->p[P_REV] = 0;
    t->p[P_LD_PIT] = t->p[P_LD_FLT] = t->p[P_LD_SHP] = t->p[P_LD_AMP] = 0;
}
static double xbuf[NFFT];
/* n samples of the part's own output (track_render: no FX, no master), after skip samples */
static void part_capture(track_t *t, uint32_t skip, double *x, uint32_t n)
{
    int32_t o[CTL];
    uint32_t f, i;
    for (f = 0; f < skip; f += CTL)
        track_render(t, o, CTL);
    for (f = 0; f < n; f += CTL) {
        track_render(t, o, CTL);
        for (i = 0; i < CTL && f + i < n; i++)
            x[f + i] = o[i];
    }
}

/* power in the harmonics of f0 (+-4 bins) and off them (aliases), above 40 Hz; *lim: off-harmonic power
 * below 10 kHz only (where the folded partials are heard among the low harmonics) */
static void alias_ratio(double f0, double *all, double *low, double *worst)
{
    double h = 0, a = 0, al = 0, fund = 0, w = 0, bin = (double)FS / NFFT;
    uint32_t i;
    for (i = (uint32_t)(40 / bin); i <= NFFT / 2; i++) {
        double f = i * bin, k = floor(f / f0 + 0.5), d = fabs(f - k * f0) / bin;
        if (k >= 1 && d <= 4) {
            h += re_[i];
            if (k == 1)
                fund = re_[i] > fund ? re_[i] : fund;
        } else {
            a += re_[i];
            if (f < 10000)
                al += re_[i];
            w = re_[i] > w ? re_[i] : w;
        }
    }
    *all = db(a / h), *low = db(al / h), *worst = db(w / fund);
}

static int alias_test(int quiet, double *saw_db)
{
    static const char *const WN[2] = {"saw", "square"};
    uint32_t w;
    for (w = 0; w < 2u; w++) {
        int16_t e8[8] = {(int16_t)w, 0, 0, 0, 127, 0, 0, 0};
        double all, low, worst;
        part_setup(&trk[0], e8);
        trk_note_on(&trk[0], 96, 100);                /* C7, 2093 Hz */
        part_capture(&trk[0], FS / 5u, xbuf, NFFT);
        spectrum(xbuf);
        alias_ratio(440.0 * pow(2, (96 - 69) / 12.0), &all, &low, &worst);
        if (!quiet)
            printf("alias: %-6s C7: off-harmonic power %6.1f dB (below 10 kHz %6.1f dB), largest alias %6.1f dB re the fundamental\n",
                   WN[w], all, low, worst);
        if (!w)
            *saw_db = all;
    }
#if FELUCCA_ANALOG2
    {   /* hard sync: osc 2 alone (MIX 127), a saw an octave + a fourth up (no whole ratio: a jump at each restart), restarted by osc 1 at C6 */
        int16_t e8[8] = {0, 0, 127, 0, 127, 0, 0, 0};
        double all, low, worst, f1 = 440.0 * pow(2, (84 - 69) / 12.0), r = pow(2, 17 / 12.0);
        uint32_t i;
        part_setup(&trk[0], e8);
        trk[0].p[P_A2SYNC] = 1, trk[0].p[P_A2SEMI] = 17;
        trk_note_on(&trk[0], 84, 100);
        part_capture(&trk[0], FS / 5u, xbuf, NFFT);
        spectrum(xbuf);
        alias_ratio(f1, &all, &low, &worst);
        if (!quiet)
            printf("alias: sync   C6 (osc 2 +17 st, BLEP): off-harmonic %6.1f dB (below 10 kHz %6.1f dB), largest %6.1f dB\n",
                   all, low, worst);
        {   /* the same without any correction: a naive saw restarted at osc 1's wrap */
            double p0 = 0, p1 = 0, i0 = f1 / FS, i1 = f1 * r / FS;
            for (i = 0; i < NFFT; i++) {
                xbuf[i] = p1 - 0.5;
                p0 += i0;
                p1 += i1;
                if (p1 >= 1)
                    p1 -= 1;
                if (p0 >= 1) {
                    p0 -= 1;
                    p1 = p0 * r;
                }
            }
            spectrum(xbuf);
            alias_ratio(f1, &all, &low, &worst);
            if (!quiet)
                printf("alias: sync   naive reference (no BLEP):    off-harmonic %6.1f dB (below 10 kHz %6.1f dB), largest %6.1f dB\n",
                       all, low, worst);
        }
    }
#endif
    return 1;
}

/* the filter: impulse response of the kernel at CUT c, RES r (mode m: ANALOG 2's FTYP) */
static void filter_ir(int32_t c, int32_t r, uint32_t m, double *x, uint32_t n, int32_t amp)
{
    int32_t st[2] = {0, 0}, st2[2] = {0, 0}, b[CTL];
    uint32_t f, i;
    tsvf_t k1;
#if FELUCCA_ANALOG2
    tsvf_t k2;
    a2_coef(&k1, c << 8, a2_k(r));
    a2_coef(&k2, c << 8, 5793);
#else
    (void)m;
    tsvf_coef(&k1, c << 8, r);
#endif
    for (f = 0; f < n; f += CTL) {
        for (i = 0; i < CTL; i++)
            b[i] = f + i == 0 ? amp : 0;
#if FELUCCA_ANALOG2
        A2_FLT[m](b, st, &k1, a2_k(r), CTL);
        if (m == 1u)
            a2_lp(b, st2, &k2, 0, CTL);
#else
        for (i = 0; i < CTL; i++)
            b[i] = tsvf_lp(&k1, b[i], &st[0], &st[1]);
        (void)st2;
#endif
        for (i = 0; i < CTL && f + i < n; i++)
            x[f + i] = b[i];
    }
}

static int filter_test(int quiet, double *selfosc_err, double *selfosc_lvl)
{
    static const uint8_t RES[] = {0, 64, 100, 112, 120, 127};
    double bin = (double)FS / NFFT;
    uint32_t r, i, m;
    for (m = 0; m < (FELUCCA_ANALOG2 ? 4u : 1u); m++)
        for (r = 0; r < sizeof RES; r++) {
            double pk = 0, pf = 0, dc, f3 = 0;
            uint32_t ipk = 0;
            if (m && RES[r] != 64)
                continue;
            filter_ir(64, RES[r], m, xbuf, NFFT, 1000);
            for (i = 0; i < NFFT; i++)
                re_[i] = xbuf[i] / 1000.0, im_[i] = 0;
            fft(re_, im_, NFFT);
            for (i = 0; i <= NFFT / 2; i++)
                re_[i] = re_[i] * re_[i] + im_[i] * im_[i];
            dc = re_[1];
            for (i = 1; i <= NFFT / 2; i++)
                if (re_[i] > pk)
                    pk = re_[i], ipk = i;
            pf = ipk * bin;
            for (i = ipk; i <= NFFT / 2; i++)          /* -3 dB below the peak, above it */
                if (re_[i] < pk / 2) {
                    f3 = i * bin;
                    break;
                }
            if (!quiet)
                printf("filter: %s CUT 64 (%u Hz) RES %3u: peak %+6.1f dB at %6.0f Hz (DC %+6.1f dB), -3 dB at %6.0f Hz, at 10 kHz %+6.1f dB\n",
                       m == 0 ? "LP12" : m == 1 ? "LP24" : m == 2 ? "BP  " : "HP  ", CUTOFF_HZ[64], RES[r], db(pk), pf, db(dc),
                       f3, db(re_[(uint32_t)(10000 / bin)]));
        }
    *selfosc_err = 0, *selfosc_lvl = 1e9;
    {   /* self-oscillation: an impulse, then nothing for 1.5 s; the level and pitch of the last 0.5 s */
        static const uint8_t CUTS[] = {40, 64, 90};
        static double y[3 * FS / 2];
        uint32_t c, rr;
        for (rr = 126; rr <= 127; rr++)
            for (c = 0; c < sizeof CUTS; c++) {
                uint32_t n = 3u * FS / 2u, zc = 0, k0 = n - FS / 2u, first = 0, last = 0;
                double pk = 0, f;
                filter_ir(CUTS[c], (int32_t)rr, 0, y, n, 16000);
                for (i = k0; i < n; i++) {
                    pk = fabs(y[i]) > pk ? fabs(y[i]) : pk;
                    if (y[i - 1] < 0 && y[i] >= 0) {
                        if (!zc)
                            first = i;
                        last = i;
                        zc++;
                    }
                }
                f = zc > 1 ? (double)(zc - 1) * FS / (last - first) : 0;
                if (!quiet)
                    printf("filter: RES %u CUT %3u (%5u Hz): after 1 s %s, level %6.0f (%5.1f dBFS at the output), %7.1f Hz (%+5.1f %%)\n",
                           rr, CUTS[c], CUTOFF_HZ[CUTS[c]], pk > 100 ? "rings" : "silent", pk, db(pk * pk * 4 / (32768.0 * 32768.0)),
                           f, f ? (f / CUTOFF_HZ[CUTS[c]] - 1) * 100 : 0);
                if (rr == 127) {
                    double e = f ? fabs(f / CUTOFF_HZ[CUTS[c]] - 1) : 1;
                    *selfosc_err = e > *selfosc_err ? e : *selfosc_err;
                    *selfosc_lvl = pk < *selfosc_lvl ? pk : *selfosc_lvl;
                }
            }
    }
    return 1;
}

/* zipper: a sine (C3) through LP CUT 20 RES 90, a fast envelope opening it (ENV DEST FLT 63, DEC 30);
 * the power at the block rate and its multiples (+-2 bins) against the rest, in the 370 ms after the note */
static double zipper_one(int fenv, int quiet, const char *what)
{
    int16_t e8[8] = {3, 0, 0, 0, 20, 90, 0, 0};
    double z = 0, tot = 0, bin = (double)FS / NFFT, br = (double)FS / CTL;
    uint32_t i, k;
    part_setup(&trk[0], e8);
    trk[0].p[P_SUS] = 127;
    if (fenv) {
#if FELUCCA_ANALOG2
        trk[0].p[P_A2FATK] = 0, trk[0].p[P_A2FDEC] = 30, trk[0].p[P_A2FENV] = 63;
#endif
    } else {
        trk[0].p[P_DEC] = 30, trk[0].p[P_SUS] = 0, trk[0].p[P_ED_FLT] = 63;
    }
    trk_note_on(&trk[0], 48, 100);
    part_capture(&trk[0], 0, xbuf, NFFT);
    spectrum(xbuf);
    for (i = 2; i <= NFFT / 2; i++) {
        double f = i * bin;
        tot += re_[i];
        for (k = 1; k <= 8u; k++)
            if (fabs(f - k * br) <= 2 * bin)
                z += re_[i];
    }
    if (!quiet)
        printf("zipper: %-34s power at the block rate x1..8: %6.1f dB re the total\n", what, db(z / tot));
    return db(z / tot);
}

static int zipper_test(int quiet, double *zip)
{
    *zip = zipper_one(0, quiet, "ENV DEST FLT 63, DEC 30 (the ADSR)");
#if FELUCCA_ANALOG2
    *zip = fmax(*zip, zipper_one(1, quiet, "FENV 63, FDEC 30 (ANALOG 2's AD)"));
#endif
    return 1;
}

/* engine e preset p (as ui.c loads it) on a phrase: notes, a held chord, 5 s, into a WAV */
static int wav_render(uint32_t e, uint32_t pi, const char *path, const char *pset)
{
    static const uint8_t MEL[8] = {48, 55, 60, 63, 60, 55, 51, 53};
    static const uint8_t CH[4] = {60, 63, 67, 70};
    FILE *w = fopen(path, "wb");
    uint32_t f, i, total = 5u * FS, on = 0;
    double ss = 0;
    int32_t pk = 0;
    if (!w)
        return 0;
    host_tracks_init();
    host_preset(&trk[0], e, pi);
    trk[0].p[P_AMODE] = 0;
    while (pset && *pset) {                           /* id:value,... over the preset (a demo patch) */
        int id, val, k;
        if (sscanf(pset, "%d:%d%n", &id, &val, &k) != 2)
            break;
        if (id >= 0 && id < P_COUNT)
            trk[0].p[id] = (int16_t)val;
        pset += k + (pset[k] == ',');
    }
    wav_hdr(w, total);
    for (f = 0; f < total; f += CTL) {
        int32_t o[2 * CTL];
        uint32_t ms = f * 1000u / FS;
        if (ms < 2400u && ms % 300u < 23u && !on) {   /* a line of 8 notes, 300 ms apart, 250 ms long */
            trk_note_on(&trk[0], MEL[ms / 300u], 100);
            on = MEL[ms / 300u];
        }
        if (on && ms < 2400u && ms % 300u >= 250u) {
            trk_note_off(&trk[0], on);
            on = 0;
        }
        if (ms >= 2500u && ms < 2523u)
            for (i = 0; i < 4u; i++)
                trk_note_on(&trk[0], CH[i], 90);
        if (ms >= 4000u && ms < 4023u)
            for (i = 0; i < 4u; i++)
                trk_note_off(&trk[0], CH[i]);
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++) {
            wav_put(w, o[2 * i], o[2 * i + 1]);
            ss += (double)o[2 * i] * o[2 * i];
            pk = abs(o[2 * i]) > pk ? abs(o[2 * i]) : pk;
        }
    }
    fclose(w);
    printf("%s/%s: rms %.1f dBFS, peak %.1f dBFS\n", ENGINES[e]->name, ENGINES[e]->presets[pi].name,
           db(ss / total / (32768.0 * 32768.0)), db((double)pk * pk / (32768.0 * 32768.0)));
    return 1;
}

int main(int argc, char **argv)
{
    const char *cmd = argc > 1 ? argv[1] : "check";
    double sawdb, err, lvl, zip;
    if (!strcmp(cmd, "alias"))
        return !alias_test(0, &sawdb);
    if (!strcmp(cmd, "filter"))
        return !filter_test(0, &err, &lvl);
    if (!strcmp(cmd, "zipper"))
        return !zipper_test(0, &zip);
    if (!strcmp(cmd, "wav") && argc > 4)
        return !wav_render((uint32_t)atoi(argv[2]) % NENGINES, (uint32_t)atoi(argv[3]), argv[4], argc > 5 ? argv[5] : 0);
    alias_test(1, &sawdb);
    filter_test(1, &err, &lvl);
    zipper_test(1, &zip);
    {   /* limits: what ANALOG 2 measured when it was written (tests/analog2_test.c alias / filter / zipper) */
        int ok = sawdb < -35;
#if FELUCCA_ANALOG2
        ok &= err < 0.03 && lvl > 2000 && zip < -52;
#endif
        printf("analog2: saw C7 off-harmonic %.1f dB, self-oscillation pitch error %.1f %% level %.0f, zipper %.1f dB %s\n",
               sawdb, err * 100, lvl, zip, ok ? "PASS" : "FAIL");
        return !ok;
    }
}
