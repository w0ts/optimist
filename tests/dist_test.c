/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of the DIST insert (firmware/src/fx.c track_dist), same sources as the firmware (through hostsim.c).
 * DIST did "nothing" (2026-10-07): its drive was fixed (1x .. 9x) while the parts' levels differ by 30 dB, so
 * a quiet part barely clipped at 127 and a loud one was clipped from DST 1 on, with the level falling 2 to 6 dB
 * at the first step. Now the drive is relative to a peak follower; this test keeps it audible:
 * 1. DST 0 and the FX bypass leave the signal untouched (bit for bit).
 * 2. a sine at -24, -12 and 0 dB of the part's usual range: the harmonics (2..15 over the fundamental) rise
 *    with every step of DST; at most 3 % at DST 1, at least 15 % at 64, 30 % at 127.
 * 3. the same distortion at any level: at a given DST the THD of the three levels within 2 points.
 * 4. the loudness holds: out / in RMS within -7 .. +3 dB at every DST and level.
 * 5. the bass keeps its weight: a 40 Hz sine at DST 127 within 3 dB of its level (the band under ~55 Hz
 *    skips the clipper and is added back).
 * 6. no overflow: a full-scale square (the mix's clamp, 884000) comes out with the input's sign, bounded.
 * 7. cost: instructions per sample of track_dist at DST 64 (printed; proc_pid_rusage on macOS). */
#define main hostsim_main
#include "hostsim.c"
#undef main
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#endif

#define N (FS / 2u)                     /* 0.5 s: 0.25 s to settle, 0.25 s measured */
static int fails;
static void check(const char *what, int ok)
{
    printf("dist: %-92s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

static double goertzel(const int32_t *x, uint32_t n, double f)
{
    double w = 2 * M_PI * f / FS, c = 2 * cos(w), s1 = 0, s2 = 0;
    uint32_t i;
    for (i = 0; i < n; i++) {
        double s = x[i] * (0.5 - 0.5 * cos(2 * M_PI * i / n)) + c * s1 - s2;
        s2 = s1, s1 = s;
    }
    return sqrt(s1 * s1 + s2 * s2 - c * s1 * s2);
}

static int32_t x[N], y[N];
static track_t tt;
static void run(int dist, int fxoff)
{
    uint32_t i;
    memset(&tt, 0, sizeof tt);
    tt.p[P_DIST] = (int16_t)dist;
    tt.p[P_FXOFF] = (int16_t)fxoff;
    memcpy(y, x, sizeof y);
    for (i = 0; i + CTL <= N; i += CTL)
        track_dist(&tt, y + i, CTL);
}
static void sine(double amp, double f)
{
    uint32_t i;
    for (i = 0; i < N; i++)
        x[i] = (int32_t)lrint(amp * sin(2 * M_PI * f * i / FS));
}
static double rms(const int32_t *v, uint32_t n)
{
    double s = 0;
    uint32_t i;
    for (i = 0; i < n; i++)
        s += (double)v[i] * v[i];
    return sqrt(s / n);
}
/* THD (%) and out / in RMS (dB) of the second half */
static void measure(double f0, double *thd, double *db)
{
    double h1 = goertzel(y + N / 2, N / 2, f0), hs = 0;
    uint32_t k;
    for (k = 2; k <= 15u; k++)
        hs += pow(goertzel(y + N / 2, N / 2, f0 * k), 2);
    *thd = 100 * sqrt(hs) / (h1 + 1e-9);
    *db = 20 * log10(rms(y + N / 2, N / 2) / rms(x + N / 2, N / 2));
}

static uint64_t instr_now(void)
{
#ifdef __APPLE__
    struct rusage_info_v4 ri;
    if (!proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri))
        return ri.ri_instructions;
#endif
    return 0;
}

int main(void)
{
    static const int D[] = {1, 16, 32, 48, 64, 80, 96, 112, 127};
    static const double A[] = {14000, 56000, 220000};   /* peaks: a quiet pluck, a typical part, a loud pad */
    static const double F0 = 220.5;                   /* whole cycles in the window */
    double thd[3][9], db[3][9];
    char what[160];
    uint32_t a, d, i;
    int ok;

    sine(56000, F0);
    run(0, 0);
    check("DST 0: the signal untouched", !memcmp(x, y, sizeof x));
    run(127, 1);
    check("DST 127 with the FX bypassed (FX OFF): the signal untouched", !memcmp(x, y, sizeof x));

    for (a = 0; a < 3u; a++)
        for (d = 0; d < 9u; d++) {
            sine(A[a], F0);
            run(D[d], 0);
            measure(F0, &thd[a][d], &db[a][d]);
        }
    for (a = 0; a < 3u; a++) {
        ok = thd[a][0] <= 3.0 && thd[a][4] >= 15.0 && thd[a][8] >= 30.0;
        for (d = 1; d < 9u; d++)
            ok &= thd[a][d] > thd[a][d - 1];
        sprintf(what, "sine peak %6.0f: THD rises with DST (1: %.1f %%, 32: %.1f, 64: %.1f, 96: %.1f, 127: %.1f)", A[a], thd[a][0], thd[a][2],
                thd[a][4], thd[a][6], thd[a][8]);
        check(what, ok);
    }
    for (ok = 1, d = 0; d < 9u; d++) {
        double lo = thd[0][d], hi = thd[0][d];
        for (a = 1; a < 3u; a++)
            lo = thd[a][d] < lo ? thd[a][d] : lo, hi = thd[a][d] > hi ? thd[a][d] : hi;
        ok &= hi - lo <= 2.0;
    }
    check("the same THD at the three levels (within 2 points at every DST)", ok);
    for (ok = 1, a = 0; a < 3u; a++)
        for (d = 0; d < 9u; d++)
            ok &= db[a][d] >= -7.0 && db[a][d] <= 3.0;
    sprintf(what, "loudness: out / in within -7 .. +3 dB (DST 127: %+.1f %+.1f %+.1f dB)", db[0][8], db[1][8], db[2][8]);
    check(what, ok);

    {
        double t, l;
        sine(56000, 40.0);
        run(127, 0);
        l = 20 * log10(goertzel(y + N / 2, N / 2, 40.0) / goertzel(x + N / 2, N / 2, 40.0));
        (void)t;
        sprintf(what, "a 40 Hz sine at DST 127 keeps its level (%+.1f dB)", l);
        check(what, l > -3.0 && l < 3.0);
    }
    {
        int32_t pk = 0;
        for (i = 0; i < N; i++)
            x[i] = (i / 100u) & 1u ? -884000 : 884000;
        run(127, 0);
        for (ok = 1, i = N / 2; i < N; i++) {
            int32_t v = y[i] < 0 ? -y[i] : y[i];
            pk = v > pk ? v : pk;
            if (i % 100u > 20u)                       /* (past the tone filter's edge) */
                ok &= (y[i] > 0) == (x[i] > 0);
        }
        sprintf(what, "full-scale square at DST 127: no wrap, the input's sign, peak %d", pk);
        check(what, ok && pk < 2 * 884000);
    }
    {
        uint64_t i0;
        uint32_t r;
        sine(56000, F0);
        memset(&tt, 0, sizeof tt);
        tt.p[P_DIST] = 64;
        i0 = instr_now();
        for (r = 0; r < 40u; r++)
            for (i = 0; i + CTL <= N; i += CTL)
                track_dist(&tt, x + i, CTL);
        if (i0)
            printf("dist: cost at DST 64: %.1f host instructions a sample\n", (double)(instr_now() - i0) / (40.0 * N));
    }
    return fails ? 1 : 0;
}
