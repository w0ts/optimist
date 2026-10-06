/* SPDX-License-Identifier: GPL-3.0-only */
/* ANALOG 2 measurements on the Mac, against the original ANALOG (build this file with -DFELUCCA_ANALOG2=0 for
 * the old engine; same sources as the firmware, through hostsim.c):
 *   analog2_test alias     saw / square at C7 (and hard sync at C6, ANALOG 2): the power off the harmonics
 *   analog2_test filter    the filter kernel's response (impulse, FFT) and self-oscillation at RES 126 / 127
 *   analog2_test zipper    a fast filter envelope on a sine: the power at the block rate (1378 Hz) and its multiples
 *   analog2_test env2      ENV2: SUS2 held, REL2 (0: DEC2's time), the attack to the top, the destinations (each
 *                          alone as DST2 + AMT2 rendered before ENV2 DEST, bit for bit; several at once)
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
            a2_lp2(b, st2, &k2, 0, CTL);
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

#if FELUCCA_ANALOG2
/* zipcmp: the cutoff's update schemes on one signal and one cutoff path (the same ANALOG 2 filter, LP12;
 * only the update differs): S32 once a block at the block's cutoff (the original ANALOG), S8 four steps
 * of 8 (ANALOG 2 before), S16I the engine's a2_filter (every 16, coefficients stepped per sample), REF the
 * coefficients worked out every sample (the cutoff interpolated linearly over the block). Prints, per
 * scheme, the residual against REF (all of it, and above 2 kHz) and the power at the block rate's
 * multiples; writes OUT/zip-PATH-SCHEME.wav */
enum { S32, S8, S16I, REF, NSCH };
static const char *const SCH_N[NSCH] = {"once-per-32", "every-8", "16+interp", "per-sample"};
#define ZN (3u * FS)
static double zy[NSCH][ZN];

/* the cutoff (1/256 CUT) at the end of block f: acid, a 16th-note envelope (120 BPM: 125 ms, 40 ms decay)
 * from CUT 30 up to 110; lfo, CUT 64 +-40 at 12 Hz */
static int32_t zip_cut(uint32_t path, uint32_t f)
{
    double t = (double)(f + CTL) / FS;
    if (!path) {
        double ph = fmod(t, 0.125);
        return (int32_t)((30 << 8) + (80 << 8) * exp(-ph / 0.040));
    }
    return (int32_t)((64 << 8) + (40 << 8) * sin(2 * M_PI * 12 * t));
}

static void zip_run(uint32_t path, int32_t res, uint32_t note)
{
    int32_t st[NSCH][6], kd = a2_k(res), c0 = zip_cut(path, 0) , b[CTL], x[CTL];
    uint32_t ph = 0, inc = PITCH_INC[note * 16], f, i, s, j;
    tsvf_t c;
    memset(st, 0, sizeof st);
    for (f = 0; f < ZN; f += CTL) {
        int32_t cut = zip_cut(path, f);
        memset(x, 0, sizeof x);
        a2_saw(x, ph, inc, 0, 16384, CTL);            /* the engine's level: half scale */
        ph += inc * CTL;
        for (s = 0; s < NSCH; s++) {
            memcpy(b, x, sizeof b);
            if (s == S32) {
                a2_coef(&c, cut, kd);
                a2_lp(b, st[s], &c, kd, CTL);
            } else if (s == S8) {
                for (j = 0; j < 4u; j++) {
                    a2_coef(&c, c0 + (((cut - c0) * (int32_t)(j + 1u)) >> 2), kd);
                    a2_lp(b + 8 * j, st[s], &c, kd, 8);
                }
            } else if (s == S16I) {
                a2_filter(b, st[s], c0, cut, 0, kd);
            } else {
                for (i = 0; i < CTL; i++) {
                    a2_coef(&c, c0 + (int32_t)(((int64_t)(cut - c0) * (i + 1)) / CTL), kd);
                    a2_lp(b + i, st[s], &c, kd, 1);
                }
            }
            for (i = 0; i < CTL && f + i < ZN; i++)
                zy[s][f + i] = b[i];
        }
        c0 = cut;
    }
}

/* power at the block rate's multiples (+-2 bins), dB re the total, frames of NFFT from 0.25 s */
static double zip_lines(const double *y)
{
    double z = 0, tot = 0, bin = (double)FS / NFFT, br = (double)FS / CTL;
    uint32_t o, i, k;
    for (o = FS / 4u; o + NFFT <= ZN; o += NFFT) {
        spectrum(y + o);
        for (i = 2; i <= NFFT / 2; i++) {
            tot += re_[i];
            for (k = 1; k <= 8u; k++)
                if (fabs(i * bin - k * br) <= 2 * bin)
                    z += re_[i];
        }
    }
    return db(z / tot);
}

static int zipcmp(const char *dir)
{
    static const char *const PN[2] = {"acid", "lfo"};
    static const int32_t RS[2] = {120, 110};
    static const uint32_t NOTE[2] = {36, 48};
    uint32_t p, s, i;
    for (p = 0; p < 2u; p++) {
        double ref = 0;
        zip_run(p, RS[p], NOTE[p]);
        for (i = 0; i < ZN; i++)
            ref += zy[REF][i] * zy[REF][i];
        printf("zipcmp: %s (saw note %u, RES %d, %s)\n", PN[p], NOTE[p], RS[p],
               p ? "LFO 12 Hz, CUT 64 +-40" : "16ths at 120 BPM, CUT 30 -> 110, 40 ms decay");
        for (s = 0; s < NSCH; s++) {
            double r = 0, hf = 0, h1 = 0, h2 = 0;
            char path[512];
            FILE *w;
            for (i = 0; i < ZN; i++) {                /* hf: the residual through a one-pole high-pass (~2.3 kHz) */
                double d = zy[s][i] - zy[REF][i], a = 0.75;
                double y = a * (h1 + d - h2);
                r += d * d;
                h2 = d, h1 = y;
                hf += y * y;
            }
            if (s == REF)
                printf("zipcmp:   %-12s reference; block-rate lines %6.1f dB\n", SCH_N[s], zip_lines(zy[s]));
            else
                printf("zipcmp:   %-12s residual vs per-sample %6.1f dB (above ~2.3 kHz %6.1f dB) re the signal; block-rate lines %6.1f dB\n",
                       SCH_N[s], db(r / ref), db(hf / ref), zip_lines(zy[s]));
            snprintf(path, sizeof path, "%s/zip-%s-%s.wav", dir, PN[p], SCH_N[s]);
            if (!(w = fopen(path, "wb")))
                return 0;
            wav_hdr(w, ZN);
            for (i = 0; i < ZN; i++) {
                int32_t v = (int32_t)zy[s][i] * 2;          /* (the engine's output scale) */
                wav_put(w, v, v);
            }
            fclose(w);
        }
    }
    return 1;
}
#endif

#if FELUCCA_ANALOG2
/* ENV2 (ATK2 DEC2 SUS2 REL2, ENV2 DEST's amounts): the envelope's level (v->s[7], Q24) after held / let go, the
 * attack running to the top on a short note, and the destinations' effect on a sine */
static const uint8_t ENV2_ID[5] = {P_A2FENV, P_A2EPIT, P_A2ESHP, P_A2EOS2, P_A2ESDT};   /* A2E_* -> its amount */
static int32_t env2_level(const track_t *t)          /* (the sounding voice: POLY rotates them) */
{
    uint32_t i;
    for (i = 0; i < sizeof t->v / sizeof t->v[0]; i++)
        if (t->v[i].active)
            return t->v[i].s[7] & ((1 << 25) - 1);
    return -1;
}
static void env2_setup(int32_t atk, int32_t dec, int32_t sus, int32_t rel, int32_t amt, int32_t dst)
{
    int16_t e8[8] = {3, 0, 0, 0, 127, 0, 0, 0};       /* a sine, the filter open */
    part_setup(&trk[0], e8);
    trk[0].p[P_A2FATK] = (int16_t)atk, trk[0].p[P_A2FDEC] = (int16_t)dec, trk[0].p[P_A2ESUS] = (int16_t)sus;
    trk[0].p[P_A2EREL] = (int16_t)rel, trk[0].p[ENV2_ID[dst]] = (int16_t)amt;   /* (the other amounts: 0) */
    trk[0].p[P_REL] = 127;                            /* (the voice sounds on after a note-off) */
}
static uint32_t env2_crossings(uint32_t n)            /* rising zero crossings in n samples */
{
    int32_t o[CTL], last = 0;
    uint32_t f, i, c = 0;
    for (f = 0; f < n; f += CTL) {
        track_render(&trk[0], o, CTL);
        for (i = 0; i < CTL; i++) {
            c += last < 0 && o[i] >= 0;
            last = o[i];
        }
    }
    return c;
}
static void env2_run(uint32_t n) { part_capture(&trk[0], n, xbuf, 0); }   /* n samples, not kept */

/* each destination alone, as DST2 with AMT2 rendered it before ENV2 DEST (optimist 321b472, project format 10):
 * one FNV hash of a second of the part per case (eight set-ups x the five destinations x amounts 63 -64 17 -5;
 * the same loop run on both trees). The amounts at 0 are left out: DST2 PITCH at 0 still took the pitch from
 * pitch16 then (no unison detune, fine tune, LFO or bend fraction); an amount at 0 now leaves the pitch alone */
static const uint32_t ENV2_OLD[8][5][4] = {
    {{0x54c57eb3u, 0x3dd5dc91u, 0xa69fab01u, 0x677fb337u}, {0x7b8ebab5u, 0x31e32ccbu, 0x89111ffdu, 0xd71f8cedu}, {0x1c7cc08du, 0x1c7cc08du, 0x1c7cc08du, 0x1c7cc08du}, {0xeff6f0efu, 0x79e32ee1u, 0x0b94b2a9u, 0xb091a539u}, {0x1c7cc08du, 0x1c7cc08du, 0x1c7cc08du, 0x1c7cc08du}},
    {{0x79e35601u, 0x3519191bu, 0x3e8992fdu, 0xe7359835u}, {0x5f8c050fu, 0xeb56f1d1u, 0x80aa7de3u, 0x17f4b03du}, {0x06b1ae19u, 0x682404f9u, 0xaff2a9efu, 0xf2eeeb19u}, {0x6f5aa953u, 0x9c3ccd2fu, 0x988e1ee1u, 0x4d346a6fu}, {0x3be6594du, 0x3be6594du, 0x3be6594du, 0x3be6594du}},
    {{0xdc7f3eebu, 0x3b8a140bu, 0x07faedcfu, 0xf2dc9ed1u}, {0x8ad9ee4bu, 0x6483eb97u, 0xef0103f3u, 0x2d14fa27u}, {0x5c59c1fbu, 0x5c59c1fbu, 0x5c59c1fbu, 0x5c59c1fbu}, {0xcebdea6du, 0xc298dd61u, 0xbc344d03u, 0x46ce8743u}, {0x5c59c1fbu, 0x5c59c1fbu, 0x5c59c1fbu, 0x5c59c1fbu}},
    {{0xf00323a7u, 0xf23f1f09u, 0xae352787u, 0x9b89d241u}, {0x33b4ac43u, 0x60a3bd97u, 0x7f009b89u, 0xe235a87bu}, {0xa525b64du, 0xa525b64du, 0xa525b64du, 0xa525b64du}, {0x8abd9677u, 0x98dcb3a1u, 0x8c73d083u, 0x98faecedu}, {0xa525b64du, 0xa525b64du, 0xa525b64du, 0xa525b64du}},
    {{0xbeb59d23u, 0x24a68f3du, 0x02e191d9u, 0xad1c80a3u}, {0x757b9621u, 0xec924137u, 0x15bf0aefu, 0x7a9bdd6fu}, {0x24ddee2bu, 0x24ddee2bu, 0x24ddee2bu, 0x24ddee2bu}, {0xf78e9555u, 0x323d389bu, 0xf8b6a3f7u, 0xed5bb391u}, {0x0019f8b3u, 0x15d62aa1u, 0xd18f137du, 0x92cda0f5u}},
    {{0xbc5ba631u, 0xdb275a23u, 0x937ad6fdu, 0xd955eb59u}, {0x9359ac1bu, 0xb61f0b5du, 0x3772ff93u, 0x061e60adu}, {0xb137ba11u, 0xa9b138edu, 0xa4959e05u, 0xb9eda723u}, {0x44476141u, 0x67d0fcbfu, 0x51958b19u, 0x1cfd854bu}, {0xe05ef3cdu, 0x8852331du, 0xa824e961u, 0x6467e243u}},
    {{0xe98bd819u, 0x8188bc41u, 0x80ee32efu, 0xd8bde03fu}, {0x62bc81ffu, 0x712661ddu, 0xf446ee57u, 0x2f8ff20du}, {0xd3172cd5u, 0xd3172cd5u, 0xd3172cd5u, 0xd3172cd5u}, {0xdfad0247u, 0x8a2889b1u, 0xcea0a95bu, 0x1e189029u}, {0x53b5a9dfu, 0x15a88581u, 0x0519b579u, 0x4e2f798fu}},
    {{0x0a8da6c9u, 0x3de42e7bu, 0x2d546e8fu, 0x94114311u}, {0x6bf23223u, 0x7f9aa7f7u, 0xe5679b2du, 0xf7ab32d3u}, {0x656ece25u, 0x656ece25u, 0x656ece25u, 0x656ece25u}, {0xda421c03u, 0x4e40b83fu, 0x7a0c5983u, 0xe2439db1u}, {0xd696bd37u, 0x144ded37u, 0xa4637ed9u, 0x84730155u}},
};
static uint32_t env2_case(int c, int d, int amt)
{
    static const int16_t E8[4][8] = {
        {0, 10, 64, 0, 70, 40, 0, 64}, {4, 0, 64, 0, 90, 20, 10, 0}, {0, 0, 40, 20, 50, 100, 0, 32}, {2, 25, 100, 0, 80, 60, 30, 64},
    };
    track_t *t = &trk[0];
    int32_t o[CTL];
    uint32_t h = 2166136261u, f, i;
    part_setup(t, E8[c & 3]);
    vage = 0;                                         /* (the noise and drift seeds: the voice's age; the LFO */
    t->lfo_ph = 0, t->lfo_val = 0;                    /* from 0) */
    t->p[P_REL] = 40;
    t->p[P_A2FATK] = 10, t->p[P_A2FDEC] = 50, t->p[P_A2ESUS] = 64, t->p[P_A2EREL] = (int16_t)(c & 1 ? 30 : 0);
    t->p[P_A2SWRM] = (int16_t)(c >= 4 ? 3 : 0);
    t->p[P_A2SYNC] = (int16_t)(c == 1 || c == 5);
    t->p[P_A2SEMI] = (int16_t)(c == 1 ? 7 : c == 6 ? -12 : 0);
    t->p[P_A2FTYP] = (int16_t)(c & 3);
    t->p[P_A2DRFT] = (int16_t)(c == 7 ? 20 : 0);
    if (c == 6) {
        t->p[P_VOICE] = V_MONO, t->p[P_GLIDE] = 30;   /* (UNISON: random start phases, a static seed) */
        t->p[P_LD_PIT] = 20, t->p[P_LD_SHP] = 30, t->p[P_ED_SHP] = 40, t->p[P_ED_FLT] = 20;
    }
    t->p[ENV2_ID[d]] = (int16_t)amt;
    trk_note_on(t, 48, 100);
    if (c == 3)
        trk_note_on(t, 55, 70);
    for (f = 0; f < FS; f += CTL) {
        if (f == FS * 6u / 10u / CTL * CTL) {
            trk_note_off(t, 48);
            if (c == 3)
                trk_note_off(t, 55);
        }
        track_render(t, o, CTL);
        for (i = 0; i < CTL; i++)
            h = (h ^ (uint32_t)o[i]) * 16777619u;
    }
    return h;
}
/* ENV2 DEST's amounts switched from 0 to others and back mid-note (held, then in the release), on eight set-ups:
 * one FNV hash of it all. tests/run_tests.sh compares it with a build of -DA2_ENV2_ALWAYS=1 (ENV2 DEST never
 * skipped at all 0): the skip must not change a sample */
static uint32_t env2_switch(void)
{
    static const int8_t SW[6][5] = {{0, 0, 0, 0, 0}, {40, 0, 0, 0, 0}, {40, -20, 0, 0, 9}, {0, 0, 0, 0, 0},
                                    {0, 0, 30, -12, 0}, {0, 0, 0, 0, 0}};
    uint32_t h = 2166136261u;
    int c;
    for (c = 0; c < 8; c++) {
        track_t *t = &trk[0];
        int32_t o[CTL];
        uint32_t f, i, k;
        env2_case(c, 0, 0);                           /* (the set-up, then played again from here) */
        part_setup(t, (const int16_t[8]){c & 1 ? 4 : 0, 10, 64, c == 2 ? 20 : 0, 60, 70, 0, 64});
        vage = 0, t->lfo_ph = 0, t->lfo_val = 0;
        t->p[P_A2FATK] = 20, t->p[P_A2FDEC] = 80, t->p[P_A2ESUS] = 50, t->p[P_A2EREL] = (int16_t)(c & 2 ? 70 : 0);
        t->p[P_A2SWRM] = (int16_t)(c >= 4 ? 2 : 0), t->p[P_A2SYNC] = (int16_t)(c == 5), t->p[P_REL] = 60;
        trk_note_on(t, 48, 100);
        if (c == 3)
            trk_note_on(t, 52, 90);
        for (f = 0, k = 0; f < FS * 3u / 2u; f += CTL) {
            if (f % (FS / 4u) < CTL) {                /* every 250 ms the next row of amounts */
                for (i = 0; i < 5u; i++)
                    t->p[ENV2_ID[i]] = SW[k % 6u][i];
                k++;
            }
            if (f == FS * 7u / 10u / CTL * CTL)       /* let go: the release, amounts still switching */
                trk_note_off(t, 48), trk_note_off(t, 52);
            track_render(t, o, CTL);
            for (i = 0; i < CTL; i++)
                h = (h ^ (uint32_t)o[i]) * 16777619u;
        }
    }
    return h;
}
static int env2_old_test(int quiet)
{
    static const int AMT[4] = {63, -64, 17, -5};
    int c, d, a, bad = 0;
    for (c = 0; c < 8; c++)
        for (d = 0; d < 5; d++)
            for (a = 0; a < 4; a++)
                if (env2_case(c, d, AMT[a]) != ENV2_OLD[c][d][a]) {
                    bad++;
                    if (!quiet)
                        printf("env2: set-up %d, destination %d, amount %d: not the sound DST2 + AMT2 made\n", c, d, AMT[a]);
                }
    if (!quiet)
        printf("env2: one destination as DST2 + AMT2 made it: %d of 160 differ\n", bad);
    return !bad;
}
static int env2_test(int quiet)
{
    int ok = 1, k;
    int32_t held, rel_dec, rel_fast, top = 0;
    uint32_t c0, c1;
    double sum[5], want;
    env2_setup(0, 30, 64, 0, 63, 0);                  /* SUS2 64: held at half */
    trk_note_on(&trk[0], 48, 100);
    env2_run(FS);
    held = env2_level(&trk[0]);
    ok &= abs(held - (64 << 17)) < (1 << 17);
    env2_setup(0, 30, 0, 0, 63, 0);                   /* SUS2 0: to 0 (the AD it was) */
    trk_note_on(&trk[0], 48, 100);
    env2_run(FS);
    ok &= env2_level(&trk[0]) < (1 << 12);
    env2_setup(0, 110, 127, 0, 63, 0);                /* let go: REL2 0 = DEC2's (slow) time; REL2 20 fast */
    trk_note_on(&trk[0], 48, 100);
    env2_run(FS / 10u);
    trk_note_off(&trk[0], 48);
    env2_run(FS / 20u);
    rel_dec = env2_level(&trk[0]);
    env2_setup(0, 110, 127, 20, 63, 0);
    trk_note_on(&trk[0], 48, 100);
    env2_run(FS / 10u);
    trk_note_off(&trk[0], 48);
    env2_run(FS / 20u);
    rel_fast = env2_level(&trk[0]);
    ok &= rel_dec > (14 << 20) && rel_fast < rel_dec / 4;
    env2_setup(60, 110, 0, 0, 63, 0);                 /* a slow attack (78 ms), let go at once: it still tops out */
    trk_note_on(&trk[0], 48, 100);
    env2_run(CTL);
    trk_note_off(&trk[0], 48);
    for (k = 0; k < 400; k++) {
        env2_run(CTL);
        top = env2_level(&trk[0]) > top ? env2_level(&trk[0]) : top;
    }
    ok &= top == (1 << 24);
    env2_setup(0, 30, 127, 0, 0, A2E_PITCH);          /* PITCH: AMT2 63 at the top = +31.5 st */
    trk_note_on(&trk[0], 48, 100);
    env2_run(FS / 10u);
    c0 = env2_crossings(FS);
    env2_setup(0, 30, 127, 0, 63, A2E_PITCH);
    trk_note_on(&trk[0], 48, 100);
    env2_run(FS / 10u);
    c1 = env2_crossings(FS);
    want = pow(2, 31.5 * (127.0 * 131072 / 16777216) / 12);   /* (held at SUS2 127: 127 / 128 of the top) */
    ok &= fabs((double)c1 / c0 / want - 1) < 0.02;
    for (k = 0; k < 5; k++) {                         /* every destination moves something */
        uint32_t i;
        env2_setup(0, 60, 0, 0, k ? 63 : 0, k);
        trk[0].p[P_E2] = 64, trk[0].p[P_E4] = 60, trk[0].p[P_A2SWRM] = 2;   /* (osc 2, a closed filter, a swarm) */
        trk[0].p[P_E0] = 4;                           /* (PWM: SHAPE moves its width) */
        trk_note_on(&trk[0], 48, 100);
        part_capture(&trk[0], 0, xbuf, FS / 4u);
        sum[k] = 0;
        for (i = 0; i < FS / 4u; i++)
            sum[k] += xbuf[i] * xbuf[i] * (double)(i % 977u);
    }
    for (k = 1; k < 5; k++)
        ok &= sum[k] != sum[0];
    {   /* several at once (as ENV DEST's): FLT + PIT differs from either alone; all at 0 is no ENV2 at all */
        uint32_t i, j;
        double s2[4];
        for (j = 0; j < 4u; j++) {
            env2_setup(0, 60, 0, 0, 0, 0);
            trk[0].p[P_E2] = 64, trk[0].p[P_E4] = 60, trk[0].p[P_A2SWRM] = 2, trk[0].p[P_E0] = 4;
            if (j == 1u || j == 3u)
                trk[0].p[P_A2FENV] = 50;
            if (j == 2u || j == 3u)
                trk[0].p[P_A2EPIT] = -30;
            trk_note_on(&trk[0], 48, 100);
            part_capture(&trk[0], 0, xbuf, FS / 4u);
            s2[j] = 0;
            for (i = 0; i < FS / 4u; i++)
                s2[j] += xbuf[i] * xbuf[i] * (double)(i % 977u);
        }
        ok &= s2[0] == sum[0] && s2[3] != s2[1] && s2[3] != s2[2] && s2[1] != s2[0] && s2[2] != s2[0];
        ok &= env2_old_test(quiet);
    }
    if (!quiet)
        printf("env2: SUS2 64 held %.3f, let go after 50 ms: REL2 =DEC %.3f, REL2 20 %.3f, slow attack let go: top %.3f, "
               "PITCH +31.5 st: x%.3f (want %.3f)\n", held / 16777216.0, rel_dec / 16777216.0, rel_fast / 16777216.0,
               top / 16777216.0, (double)c1 / c0, want);
    return ok;
}
#endif

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
#if FELUCCA_ANALOG2
    if (!strcmp(cmd, "env2"))
        return !env2_test(0);
    if (!strcmp(cmd, "env2switch")) {
        printf("env2switch %08x\n", env2_switch());
        return 0;
    }
#endif
#if FELUCCA_ANALOG2
    if (!strcmp(cmd, "zipcmp") && argc > 2)
        return !zipcmp(argv[2]);
#endif
    if (!strcmp(cmd, "wav") && argc > 4)
        return !wav_render((uint32_t)atoi(argv[2]) % NENGINES, (uint32_t)atoi(argv[3]), argv[4], argc > 5 ? argv[5] : 0);
    alias_test(1, &sawdb);
    filter_test(1, &err, &lvl);
    zipper_test(1, &zip);
    {   /* limits: what ANALOG 2 measured when it was written (tests/analog2_test.c alias / filter / zipper) */
        int ok = sawdb < -35;
#if FELUCCA_ANALOG2
        ok &= err < 0.03 && lvl > 2000 && zip < -52;
        if (!env2_test(0)) {
            printf("analog2: ENV2 FAIL\n");
            ok = 0;
        }
#endif
        printf("analog2: saw C7 off-harmonic %.1f dB, self-oscillation pitch error %.1f %% level %.0f, zipper %.1f dB %s\n",
               sawdb, err * 100, lvl, zip, ok ? "PASS" : "FAIL");
        return !ok;
    }
}
