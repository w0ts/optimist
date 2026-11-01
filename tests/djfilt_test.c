/* SPDX-License-Identifier: GPL-3.0-only */
/* The master DJ FILTER keeps silence silent (fx.c tflt_block / tflt_run on tflt[NTRK], mst_ins; G_FILT). Found by the vumeter
 * UI test (02a29d25): at G_FILT 1 (a hair into high-pass) a silent master gave a DC of ~726 (-33 dBFS) after the music stopped:
 * the SVF's states, truncated (>> 13: toward -inf) a sample at a time with the small coefficients of a low cutoff, stopped
 * short of 0 and the high-pass (x - LP) gave the LP's stuck state out.
 *   silence  every setting -64 .. 63 from rest: 0 out
 *   after    a sine (several pitches and levels) then silence, at every setting: within +-1 within 0.25 s
 *   move     the setting moved while a sine plays (both sides, across the middle), then silence: within +-1
 *   dc       a constant input through the high-pass: within +-1 of 0 (the low-pass: the input)
 *   curve    the gain at 24 frequencies x 7 settings within 0.1 dB of the same filter in double precision (same
 *            coefficients: the fix changes the rounding, not the response)
 * "measure": the tables (the DC left at each setting, the curves) instead of the checks; "curve": the curves only ("curve d":
 * the double-precision twin's). */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <libproc.h>

static int fails;
static void check(int ok, const char *what)
{
    printf("djfilt: %-100s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

#define DJ (&tflt[NTRK])
static void dj_reset(void) { memset(DJ, 0, sizeof *DJ); }

/* n samples of l (and r = l) through the master's filter at v, blocks of CTL as mst_ins; the left out to o (may be 0) */
static void dj_run(int32_t v, const int32_t *in, int32_t *o, uint32_t n)
{
    uint32_t i, k, m;
    int32_t l[CTL], r[CTL];
    for (i = 0; i < n; i += m) {
        tsvf_t c;
        m = n - i < CTL ? n - i : CTL;
        for (k = 0; k < m; k++)
            l[k] = r[k] = in ? in[i + k] : 0;
        if (tflt_block(DJ, v, &c, TFLT_RESO0)) {
            tflt_run(DJ, &c, l, m, 0, 0);
            tflt_run(DJ, &c, r, m, 1, 0);
        }
        for (k = 0; k < m && o; k++)
            o[i + k] = l[k];
    }
}

static int32_t buf_in[4 * FS], buf_out[4 * FS];
static void sine(double hz, double amp, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        buf_in[i] = (int32_t)lrint(amp * sin(2.0 * M_PI * hz * i / FS));
}
static int32_t peak(const int32_t *b, uint32_t n)
{
    int32_t p = 0;
    uint32_t i;
    for (i = 0; i < n; i++)
        p = abs(b[i]) > p ? abs(b[i]) : p;
    return p;
}

/* a sine at v for 0.5 s (the cutoff settles), then silence 1 s: the peak in the last 0.75 s */
static int32_t after_sine(int32_t v, double hz, double amp)
{
    dj_reset();
    sine(hz, amp, FS / 2);
    dj_run(v, buf_in, 0, FS / 2);
    dj_run(v, 0, buf_out, FS);
    return peak(buf_out + FS / 4, 3 * FS / 4);
}

/* the gain in dB at hz through the settled filter at v: integer (dbl 0) or the same coefficients in double precision */
static double gain_db(int32_t v, double hz, int dbl)
{
    uint32_t n = FS / 2, i, k;
    double si = 0, so = 0;
    dj_reset();
    sine(hz, 8000.0, n);
    dj_run(v, buf_in, 0, FS / 2);                         /* (the cutoff glides there, the states settle) */
    if (!dbl) {
        dj_run(v, buf_in, buf_out, n);
    } else {
        tsvf_t c;
        double z1 = 0, z2 = 0;
        int lp = DJ->mode < 0;
        tflt_block(DJ, v, &c, TFLT_RESO0);
        for (i = 0; i < n; i++) {
            double x = buf_in[i], v3 = x - z2;
            double v1 = (c.a1 * z1 + c.a2 * v3) / 8192.0, v2 = z2 + (c.a2 * z1 + c.a3 * v3) / 8192.0;
            z1 = 2 * v1 - z1, z2 = 2 * v2 - z2;
            buf_out[i] = (int32_t)lrint(lp ? v2 : x - v2);
        }
    }
    for (k = n / 4; k < n; k++)
        si += (double)buf_in[k] * buf_in[k], so += (double)buf_out[k] * buf_out[k];
    return 10.0 * log10((so + 1e-9) / si);
}
static const double HZ[24] = {20, 30, 45, 60, 80, 100, 150, 200, 300, 400, 600, 800, 1000, 1500, 2000, 3000, 4000, 5000, 6000, 8000,
                              10000, 12000, 15000, 18000};
static const int8_t VS[7] = {-64, -40, -10, -1, 1, 10, 63};

static void measure(int curves_only)
{
    int32_t v, worst = 0, wv = 0;
    uint32_t i, j;
    if (!curves_only) {
        printf("djfilt: v: DC from rest / after 440 Hz 16000 / after 60 Hz 30000 / after 5 kHz 3000 / after 30 Hz 100000\n");
        for (v = -64; v <= 63; v++) {
            int32_t a, b, c2, d, r;
            dj_reset();
            dj_run(v, 0, buf_out, FS);
            r = peak(buf_out, FS);
            a = after_sine(v, 440, 16000), b = after_sine(v, 60, 30000), c2 = after_sine(v, 5000, 3000), d = after_sine(v, 30, 100000);
            printf("djfilt: %4d  %6d %6d %6d %6d %6d\n", (int)v, (int)r, (int)a, (int)b, (int)c2, (int)d);
            if (a > worst || b > worst || c2 > worst || d > worst)
                worst = a > b ? a : b, worst = worst > c2 ? worst : c2, worst = worst > d ? worst : d, wv = v;
        }
        printf("djfilt: worst DC after a sine %d at v %d\n", (int)worst, (int)wv);
    }
    for (j = 0; j < 7; j++) {
        printf("djfilt: curve v %4d:", VS[j]);
        for (i = 0; i < 24; i++)
            printf(" %7.2f", gain_db(VS[j], HZ[i], curves_only == 2));
        printf("\n");
    }
}

static void t_silence(void)
{
    int32_t v, ok = 1;
    for (v = -64; v <= 63; v++) {
        dj_reset();
        dj_run(v, 0, buf_out, FS / 2);
        ok &= peak(buf_out, FS / 2) == 0;
    }
    check(ok, "silence from rest: 0 out at every setting -64 .. 63");
}

static void t_after(void)
{
    static const double S[5][2] = {{440, 16000}, {60, 30000}, {5000, 3000}, {30, 100000}, {150, 300}};
    int32_t v, w = 0, wv = 0, p;
    uint32_t k;
    for (v = -64; v <= 63; v++)
        for (k = 0; k < 5; k++)
            if ((p = after_sine(v, S[k][0], S[k][1])) > w)
                w = p, wv = v;
    printf("djfilt: the most left 0.25 s after a sine stops: %d (v %d)\n", (int)w, (int)wv);
    check(w <= 1, "after a sine stops (5 pitches / levels), every setting: within +-1 LSB within 0.25 s");
}

static void t_move(void)
{
    static const int8_t M[][2] = {{1, 63}, {63, 1}, {-64, -1}, {-1, -64}, {-30, 30}, {30, -30}, {1, 0}, {-1, 0}, {10, -1}, {-10, 1}};
    uint32_t k;
    int32_t w = 0, p;
    for (k = 0; k < sizeof M / sizeof M[0]; k++) {
        dj_reset();
        sine(220, 20000, FS / 2);
        dj_run(M[k][0], buf_in, 0, FS / 4);
        dj_run(M[k][1], buf_in + FS / 4, 0, FS / 4);       /* (moved while it plays) */
        dj_run(M[k][1], 0, buf_out, FS);
        p = peak(buf_out + FS / 4, 3 * FS / 4);
        w = p > w ? p : w;
    }
    printf("djfilt: the most left after a move while playing, then silence: %d\n", (int)w);
    check(w <= 1, "the setting moved while playing (both sides, across the middle), then silence: within +-1 LSB");
}

static void t_dc(void)
{
    static const int32_t D[4] = {726, -726, 20000, -3};
    int32_t v, w = 0, wl = 0;
    uint32_t k, i;
    for (v = -64; v <= 63; v++)
        for (k = 0; k < 4; k++) {
            if (!v)
                continue;
            for (i = 0; i < 2 * FS; i++)
                buf_in[i] = D[k];
            dj_reset();
            dj_run(v, buf_in, buf_out, 2 * FS);
            for (i = FS + FS / 2; i < 2 * FS; i++)
                if (v > 0)
                    w = abs(buf_out[i]) > w ? abs(buf_out[i]) : w;
                else
                    wl = abs(buf_out[i] - D[k]) > wl ? abs(buf_out[i] - D[k]) : wl;
        }
    printf("djfilt: a constant input: high-pass out at most %d, low-pass off the input by at most %d\n", (int)w, (int)wl);
    check(w <= 1, "a constant input through the high-pass (every setting): within +-1 LSB of 0");
    check(wl <= 1, "a constant input through the low-pass (every setting): the input, within +-1 LSB");
}

static void t_curve(void)
{
    uint32_t i, j;
    double w = 0;
    for (j = 0; j < 7; j++)
        for (i = 0; i < 24; i++) {
            double gi = gain_db(VS[j], HZ[i], 0), gd = gain_db(VS[j], HZ[i], 1), d = fabs(gi - gd);
            if (gd > -50.0 && d > w)                       /* (under -50 dB: an 8000 sine comes out under 30, its own output rounding) */
                w = d;
        }
    printf("djfilt: the response vs the same coefficients in double precision: at most %.3f dB apart (7 settings x 24 Hz)\n", w);
    check(w < 0.1, "the response curve: the integer filter within 0.1 dB of its double-precision twin");
}

static uint64_t insn(void)
{
    struct rusage_info_v4 ri;
    return proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri) ? 0 : ri.ri_instructions;
}
/* "cost": host instructions per stereo sample of the filter engaged (FILT 10, a sine; information only) */
static void cost(void)
{
    uint64_t i0;
    uint32_t k;
    dj_reset();
    sine(440, 16000, 2 * FS);
    dj_run(10, buf_in, 0, FS / 2);
    i0 = insn();
    for (k = 0; k < 20; k++)
        dj_run(10, buf_in, buf_out, 2 * FS);
    printf("djfilt: cost %.1f host instructions a stereo sample (FILT 10)\n", (double)(insn() - i0) / (40.0 * FS));
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "cost")) {
        cost();
        return 0;
    }
    if (argc > 1 && (!strcmp(argv[1], "measure") || !strcmp(argv[1], "curve"))) {
        measure(!strcmp(argv[1], "curve") ? 1 + (argc > 2) : 0);
        return 0;
    }
    t_silence();
    t_after();
    t_move();
    t_dc();
    t_curve();
    printf("djfilt: %s\n", fails ? "FAILED" : "all ok");
    return fails != 0;
}
