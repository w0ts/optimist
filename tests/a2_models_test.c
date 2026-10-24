/* SPDX-License-Identifier: GPL-3.0-only */
/* ANALOG 2's filter models (engines/analog/a2_models.c) on the Mac, every model built (-DFELUCCA_A2_LADR=1 ..):
 *   a2_models_test resp    each model and FTYP at CUT 64 (708 Hz): the impulse response's spectrum (DC, peak, -3 dB,
 *                          an octave and two below and above), RES 0 / 64 / 110
 *   a2_models_test osc     RES 127 at CUT 40 / 64 / 90: rings after the impulse? its level and pitch
 *   a2_models_test speed   the host's time a sample, each model against the SVF (a ratio, not the FM-1's)
 *   a2_models_test check   pass / fail: the responses' shapes, self-oscillation, a full-scale noise at RES 127 with the
 *                          cutoff jumping (bounded, finite), a model switched under a sounding voice, FLT 2's two cards
 *                          on the one stored value (tests/run_tests.sh) */
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
static double db(double p) { return 10 * log10(p > 1e-30 ? p : 1e-30); }
static const char *const MN[A2M_N] = {"SVF", "LADR", "OTA", "DIOD", "MS20"};
static const char *const FN[A2M_MODES] = {"LP12", "LP24", "BP", "HP", "NOTCH"};

/* model m, FTYP f, CUT c, RES r: an impulse of amp, n samples of the output into x */
static void model_ir(uint32_t m, uint32_t f, int32_t c, int32_t r, int32_t amp, double *x, uint32_t n)
{
    int32_t s[8] = {0}, b[CTL];
    uint32_t k, i;
    for (k = 0; k < n; k += CTL) {
        for (i = 0; i < CTL; i++)
            b[i] = k + i == 0 ? amp : 0;
        a2m_block(b, s, c << 8, c << 8, m, f, r, CTL);
        for (i = 0; i < CTL && k + i < n; i++)
            x[k + i] = b[i];
    }
}

/* the magnitude (dB) at hz of the last response (re_: the power spectrum) */
static double at(double hz) { return db(re_[(uint32_t)(hz * NFFT / FS + 0.5)]); }

typedef struct { double dc, pk, fpk, f3, o2d, o1d, o1u, o2u, fc; } resp_t;
static resp_t response(uint32_t m, uint32_t f, int32_t r)
{
    static double x[NFFT];
    resp_t o;
    double bin = (double)FS / NFFT;
    uint32_t i, ipk = 1;
    model_ir(m, f, 64, r, 1000, x, NFFT);
    for (i = 0; i < NFFT; i++)
        re_[i] = x[i] / 1000.0, im_[i] = 0;
    fft(re_, im_, NFFT);
    for (i = 0; i <= NFFT / 2; i++)
        re_[i] = re_[i] * re_[i] + im_[i] * im_[i];
    for (i = 2; i <= NFFT / 2; i++)
        if (re_[i] > re_[ipk])
            ipk = i;
    o.fc = CUTOFF_HZ[64];
    o.dc = db(re_[1]), o.pk = db(re_[ipk]), o.fpk = ipk * bin, o.f3 = 0;
    for (i = ipk; i <= NFFT / 2; i++)
        if (re_[i] < re_[ipk] / 2) {
            o.f3 = i * bin;
            break;
        }
    o.o2d = at(o.fc / 4), o.o1d = at(o.fc / 2), o.o1u = at(o.fc * 2), o.o2u = at(o.fc * 4);
    return o;
}

/* the shape each FTYP must have (dB, CUT 64, RES 0): a low-pass flat below and falling (LP24 the steeper), a band-pass
 * peaking near the cutoff, a high-pass the other way round, a notch dipping at the cutoff */
static int shape_ok(uint32_t m, uint32_t f, const resp_t *o, const char **why)
{
    double mid = at(o->fc);
    *why = "";
    switch (f) {
    case 0:
        if (fabs(o->dc) > (m == A2M_DIOD ? 3 : 1.5)) return *why = "LP12 DC not 0 dB", 0;   /* (DIOD: -3 dB near 100 Hz) */
        if (o->o2u > -18) return *why = "LP12 not 12 dB / oct (2 oct up)", 0;
        return 1;
    case 1:
        if (fabs(o->dc) > (m == A2M_DIOD ? 3 : 1.5)) return *why = "LP24 DC not 0 dB", 0;
        if (o->o2u > (m == A2M_MS20 ? -27 : -36)) return *why = "LP24 not steep (2 oct up)", 0;
        return 1;
    case 2:
        if (o->fpk < o->fc / 2 || o->fpk > o->fc * 2) return *why = "BP peak away from the cutoff", 0;
        if (o->o2d > o->pk - 6 || o->o2u > o->pk - 6) return *why = "BP not falling both ways", 0;
        return 1;
    case 3:
        if (o->o2d > o->o2u - 18) return *why = "HP not cutting the lows", 0;
        if (o->o2u < -3) return *why = "HP not passing the highs", 0;
        return 1;
    default:
        if (mid > o->o2u - 12 || mid > o->o2d - 12) return *why = "NOTCH not dipping at the cutoff", 0;
        if (o->o2u < -4 || o->dc < -4) return *why = "NOTCH not passing both sides", 0;
        return 1;
    }
}

static int resp_test(int quiet)
{
    static const int32_t RES[] = {0, 64, 110};
    uint32_t m, f, r;
    int ok = 1;
    for (m = 1; m < A2M_N; m++)
        for (f = 0; f < A2M_MODES; f++)
            for (r = 0; r < sizeof RES / sizeof RES[0]; r++) {
                resp_t o = response(m, f, RES[r]);
                const char *why = "";
                int good = RES[r] || shape_ok(m, f, &o, &why);
                if (!quiet || !good)
                    printf("resp: %-4s %-5s RES %3d: DC %+6.1f, peak %+6.1f dB at %5.0f Hz, -3 dB %5.0f; fc/4 %+6.1f fc/2 %+6.1f "
                           "2fc %+6.1f 4fc %+6.1f %s%s\n", MN[m], FN[f], (int)RES[r], o.dc, o.pk, o.fpk, o.f3, o.o2d, o.o1d,
                           o.o1u, o.o2u, good ? "" : "FAIL: ", why);
                ok &= good;
            }
    return ok;
}

/* RES 127: an impulse (full scale), then 1.5 s of nothing: the last 0.5 s' level and pitch */
static int osc_test(int quiet)
{
    static const uint8_t CUTS[] = {40, 64, 90};
    static double y[3 * FS / 2];
    uint32_t m, c, i;
    int ok = 1;
    for (m = 1; m < A2M_N; m++)
        for (c = 0; c < sizeof CUTS; c++) {
            uint32_t n = 3u * FS / 2u, zc = 0, k0 = n - FS / 2u, first = 0, last = 0;
            double pk = 0, hz;
            model_ir(m, 1, CUTS[c], 127, 16000, y, n);
            for (i = k0; i < n; i++) {
                pk = fabs(y[i]) > pk ? fabs(y[i]) : pk;
                if (y[i - 1] < 0 && y[i] >= 0) {
                    if (!zc)
                        first = i;
                    last = i;
                    zc++;
                }
            }
            hz = zc > 1 ? (double)(zc - 1) * FS / (last - first) : 0;
            if (!quiet || pk < 2000)
                printf("osc: %-4s RES 127 CUT %3u (%5u Hz): %s, level %6.0f, %7.1f Hz (%+5.1f %%)\n", MN[m], CUTS[c],
                       CUTOFF_HZ[CUTS[c]], pk > 2000 ? "rings" : "FAIL: silent", pk, hz,
                       hz ? (hz / CUTOFF_HZ[CUTS[c]] - 1) * 100 : 0);
            ok &= pk > 2000;
        }
    return ok;
}

static uint32_t rnd(uint32_t *s)
{
    *s = *s * 1664525u + 1013904223u;
    return *s;
}

/* full-scale noise (and the drive's peaks past it) at RES 127, the cutoff jumping anywhere every block, each mode: the
 * output within the bound, the states finite; then the input stops: no NaN, the non-ringing RES 100 dies away */
static int abuse_test(int quiet)
{
    uint32_t m, f, k, i, seed = 7;
    int ok = 1;
    for (m = 1; m < A2M_N; m++)
        for (f = 0; f < A2M_MODES; f++) {
            int32_t s[8] = {0}, b[CTL], c0 = 64 << 8, worst = 0, tail = 0;
            int bad = 0;
            for (k = 0; k < 2000u; k++) {
                int32_t cut = (int32_t)(rnd(&seed) % (127u << 8)), res = k < 1500u ? 127 : 100;
                for (i = 0; i < CTL; i++)
                    b[i] = k < 1000u ? (int32_t)(rnd(&seed) % 262143u) - 131071 : 0;
                a2m_block(b, s, c0, cut, m, f, res, CTL);
                c0 = cut;
                for (i = 0; i < CTL; i++) {
                    int32_t a = b[i] < 0 ? -b[i] : b[i];
                    worst = a > worst ? a : worst;
                    if (k >= 1990u)
                        tail = a > tail ? a : tail;
                }
                for (i = 0; i < 8u; i++)
                    if (i != 2u && i != 3u && i != 6u && i != 7u && !isfinite(a2m_ld(s[i])))
                        bad = 1;
            }
            bad |= worst > 32 * 32768;
            bad |= tail > 64;                         /* (RES 100, 15 blocks of silence after: gone) */
            if (!quiet || bad)
                printf("abuse: %-4s %-5s: peak %8d, tail %5d %s\n", MN[m], FN[f], (int)worst, (int)tail, bad ? "FAIL" : "ok");
            ok &= !bad;
        }
    return ok;
}

/* a voice whose states hold the SVF's integers, then a model: cleared (s[7]), not read as floats; back to the SVF: the
 * engine clears the floats (eng_analog2.c analog_render) */
static int switch_test(void)
{
    int32_t s[8] = {123456, -99999, 0, 0, 77777, -5, 0, 0}, b[CTL], i, mx = 0;
    for (i = 0; i < CTL; i++)
        b[i] = 1000;
    a2m_block(b, s, 64 << 8, 64 << 8, A2M_DIOD, 1, 64, CTL);
    for (i = 0; i < CTL; i++)
        mx = abs(b[i]) > mx ? abs(b[i]) : mx;
    if (s[7] != A2M_DIOD || mx > 2000) {
        printf("switch: SVF states read as floats (peak %d) FAIL\n", (int)mx);
        return 0;
    }
    {   /* the engine: a note on LADR, then FTYP's value back to the SVF while it sounds */
        static double x[CTL * 64];
        double pk = 0;
        uint32_t j;
        int16_t e8[8] = {0, 0, 0, 0, 80, 100, 0, 0};
        host_tracks_init();
        memset(trk[0].v, 0, sizeof trk[0].v);
        host_preset(&trk[0], 0, 0);
        for (j = 0; j < 8u; j++)
            trk[0].p[P_E0 + j] = e8[j];
        trk[0].p[P_A2FTYP] = (int16_t)(A2M_LADR * A2M_MODES + 1u);
        trk_note_on(&trk[0], 48, 100);
        for (j = 0; j < 32u; j++) {
            int32_t o[CTL];
            track_render(&trk[0], o, CTL);
        }
        trk[0].p[P_A2FTYP] = 1;
        for (j = 0; j < 64u; j++) {
            int32_t o[CTL];
            track_render(&trk[0], o, CTL);
            for (i = 0; i < CTL; i++)
                x[j * CTL + (uint32_t)i] = o[i];
        }
        for (j = 0; j < CTL * 64u; j++)
            pk = fabs(x[j]) > pk || !isfinite(x[j]) ? (isfinite(x[j]) ? fabs(x[j]) : 1e30) : pk;
        if (pk > 1e8 || pk < 100) {
            printf("switch: LADR -> SVF under a note: peak %.0f FAIL\n", pk);
            return 0;
        }
    }
    return 1;
}

/* FLT 2's cards: FTYP and MODEL each a part of P_A2FTYP */
static int cards_test(void)
{
    int16_t *vp;
    int ok = 1;
    TSEL->p[P_A2FTYP] = 1;                              /* SVF LP24 */
    ok &= a2f_word(PID_A2MDL, A2M_DIOD) == A2M_DIOD * A2M_MODES + 1;
    TSEL->p[P_A2FTYP] = A2M_DIOD * A2M_MODES + 1;
    ok &= a2f_word(P_A2FTYP, 4) == A2M_DIOD * A2M_MODES + 4;
    (void)a2f_view(PID_A2MDL, &vp);
    ok &= *vp == A2M_DIOD;
    (void)a2f_view(P_A2FTYP, &vp);
    ok &= *vp == 1;
    ok &= TP[P_A2FTYP].max == A2M_N * A2M_MODES - 1;
    if (!ok)
        printf("cards: FLT 2's MODEL / FTYP FAIL\n");
    {   /* FLT B: MODEL OFF, then FLT 2's (+ 1); TYPE its mode; the whole value P_A2BTYP */
        int okb = 1;
        TSEL->p[P_A2BTYP] = 3;                          /* OFF HP (the default) */
        (void)a2f_view(PID_A2BMDL, &vp);
        okb &= *vp == 0;
        (void)a2f_view(P_A2BTYP, &vp);
        okb &= *vp == 3;
        okb &= a2f_word(PID_A2BMDL, 1 + A2M_MS20) == (1 + A2M_MS20) * A2M_MODES + 3;   /* MS20 HP */
        TSEL->p[P_A2BTYP] = (1 + A2M_MS20) * A2M_MODES + 3;
        okb &= a2f_word(P_A2BTYP, 0) == (1 + A2M_MS20) * A2M_MODES;                 /* MS20 LP12 */
        okb &= a2f_word(PID_A2BMDL, 0) == 3 && a2f_word(PID_A2BMDL, 99) == TSEL->p[P_A2BTYP];   /* OFF; past the end: kept */
        okb &= TP[P_A2BTYP].max == A2M_N * A2M_MODES + A2M_MODES - 1;
        if (!okb)
            printf("cards: FLT B's MODEL / TYPE FAIL\n");
        ok &= okb;
    }
    return ok;
}

/* FLT B through each model (a2_fltb on a voice of part 1): full-scale noise, HP and LP24 at RES 127, the cutoff jumping:
 * bounded by the +-65535 FLT takes, the model switched under the note every 40 blocks */
static int fltb_models_test(void)
{
    int32_t b[CTL];
    uint32_t m, f, k, i, seed = 7;
    int ok = 1;
    host_tracks_init();
    for (k = 0; k < 400u; k++) {
        int32_t pk = 0;
        m = 1u + (k / 40u) % A2M_N, f = k & 1u ? 3u : 1u;
        trk[0].p[P_A2BTYP] = (int16_t)(m * A2M_MODES + f);
        trk[0].p[P_A2BCUT] = (int16_t)(rnd(&seed) % 128u), trk[0].p[P_A2BRES] = 127;
        for (i = 0; i < CTL; i++)
            b[i] = (int32_t)(rnd(&seed) >> 16) - 32768;
        a2_fltb(&trk[0], &trk[0].v[0], b, CTL);
        for (i = 0; i < CTL; i++)
            pk = abs(b[i]) > pk ? abs(b[i]) : pk;
        ok &= pk <= 65535;
    }
    if (!ok)
        printf("fltb: a model through FLT B out of bounds FAIL\n");
    return ok;
}

static int speed_test(void)
{
    int32_t s[8] = {0}, b[CTL];
    uint32_t m, k, i, seed = 1;
    for (m = 0; m < A2M_N; m++) {
        clock_t t0 = clock();
        for (k = 0; k < 200000u; k++) {
            for (i = 0; i < CTL; i++)
                b[i] = (int32_t)(rnd(&seed) >> 17) - 16384;
            if (m)
                a2m_block(b, s, (k & 63) << 8, ((k + 1) & 63) << 8, m, 1, 90, CTL);
            else
                a2_filter(b, s, (k & 63) << 8, ((k + 1) & 63) << 8, 1, a2_k(90));
        }
        printf("speed: %-4s LP24 %.1f ns a sample (host)\n", MN[m], (double)(clock() - t0) / CLOCKS_PER_SEC * 1e9 / (200000.0 * CTL));
    }
    return 1;
}

int main(int argc, char **argv)
{
    const char *cmd = argc > 1 ? argv[1] : "check";
    int ok;
    if (!strcmp(cmd, "resp"))
        return !resp_test(0);
    if (!strcmp(cmd, "osc"))
        return !osc_test(0);
    if (!strcmp(cmd, "speed"))
        return !speed_test();
    ok = resp_test(1);
    ok &= osc_test(1);
    ok &= abuse_test(1);
    ok &= switch_test();
    ok &= cards_test();
    ok &= fltb_models_test();
    printf("a2_models: responses, self-oscillation, abuse, switch, cards, FLT B %s\n", ok ? "PASS" : "FAIL");
    return !ok;
}
