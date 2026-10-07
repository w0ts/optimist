/* SPDX-License-Identifier: GPL-3.0-only */
/* The tempo delay (fx.c dly_step) through fx_buses: the gain of each repeat over FDBK and COLOR, FDBK at its top
 * (100 %) repeating for ever, the saturator in the loop, the idle skip.
 *   build/host/delay_test [WAVDIR [TAG]]
 * Numbers: per FDBK (0, 60, 100, 120: 0, 50, 83, 100 %) and COLOR (0, 70, 127) the DC / low gain of one pass (the
 * area of a smooth pulse, repeat 2 over repeat 1 .. 5 over 4: the loop's gain at DC, which every COLOR keeps) and the
 * broadband gain of a noise burst (the RMS of the first two repeats: COLOR darkens it). Checks: FDBK 100 % holds the
 * repeats at a gain of 1 at DC (500 passes lose under 0.5 %), the default FDBK (60) keeps its gain of 60 x 230 / 2^15,
 * FDBK 0 gives one repeat only, the gain never falls as FDBK rises, loud input on a full loop saturates without
 * reaching the int16 rails, a held delay keeps the bus running, and lowered it rings out to exactly 0 and goes idle.
 * WAVDIR: a stab into the delay (TIME 1/8 at 120 BPM, COLOR 70, MIX 127) at FDBK 60, 72, 108, 120 (50, 60, 90,
 * 100 %), 20 s each: WAVDIR/dly-<TAG>-fdbk<raw>.wav (TAG: "now" unless given). */
#define main hostsim_main
#include "hostsim.c"
#undef main

static uint32_t fails;

static void check(int ok, const char *what)
{
    printf("delay: %-96s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

static void dly_clear(void)                     /* the line and the filter to 0, the bus idle */
{
    memset(dly_buf, 0, sizeof dly_buf);
    fx.dly_lp = fx.dly_le = 0;
    fx.dly_q = FX_Q_MAX;
}

static int dly_idle(void)                       /* fx_buses would skip the bus on a silent block */
{
    uint32_t i;
    int32_t any = fx.dly_lp;
    for (i = 0; i < DLY_LEN; i++)
        any |= dly_buf[i];
    return fx.dly_q >= DLY_LEN && !any;
}

/* the delay bus alone: n samples of in (NULL: silence) -> out (mono: the bus' left, which equals its right; NULL:
 * dropped). *rail: set when a value written into the line reached the int16 rails (the hard clip) */
static void bus(const int32_t *in, int32_t *out, uint32_t n, int *rail)
{
    static const int32_t z[CTL];
    int32_t wl[CTL], wr[CTL];
    uint32_t b, i;
    for (b = 0; b < n; b += CTL) {
        fx_buses(z, in ? in + b : z, z, wl, wr, CTL);
        for (i = 0; out && i < CTL; i++)
            out[b + i] = wl[i];
        for (i = 1; rail && i <= CTL; i++) {
            int32_t v = dly_buf[(fx.dly_w - i) & (DLY_LEN - 1u)];
            *rail |= v >= 32767 || v <= -32768;
        }
    }
}

static void set_dly(int32_t fdbk, int32_t color)
{
    song.g[G_DFDBK] = (int16_t)fdbk;
    song.g[G_DCOLOR] = (int16_t)color;
    song.g[G_DMIX] = 127;
    song.g[G_DTIME] = 2;                         /* 1/16 at 120 BPM: 5512 samples */
    dly_clear();
}

static double area(const int32_t *x, uint32_t a, uint32_t b)
{
    double s = 0;
    for (; a < b; a++)
        s += x[a];
    return s;
}

static double rms(const int32_t *x, uint32_t a, uint32_t b)
{
    double s = 0;
    uint32_t i;
    for (i = a; i < b; i++)
        s += (double)x[i] * x[i];
    return sqrt(s / (b - a));
}

/* a Hann pulse half the delay long (its energy far below every COLOR's corner), 30000 high (15000 in the line) */
static void pulse(int32_t *in, uint32_t n, uint32_t dl)
{
    uint32_t i, len = dl / 2u;
    for (i = 0; i < n; i++)
        in[i] = i < len ? (int32_t)(15000.0 * (1.0 - cos(2 * M_PI * i / len))) : 0;
}

static void noise_burst(int32_t *in, uint32_t n, uint32_t len, int32_t amp, uint32_t seed)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        seed = seed * 1664525u + 1013904223u;
        in[i] = i < len ? (int32_t)((((int32_t)(seed >> 16) - 32768) * (int64_t)amp) >> 15) : 0;
    }
}

/* the DC gain of one pass at fdbk / color: the mean of the area ratios of repeats 2/1 .. (np)/(np - 1) */
static double dc_gain(int32_t fdbk, int32_t color, uint32_t np)
{
    uint32_t dl, n, k;
    int32_t *in, *out;
    double g = 0;
    set_dly(fdbk, color);
    dl = delay_samples();
    n = (np + 1u) * dl / CTL * CTL + CTL;
    in = malloc(4u * n), out = malloc(4u * n);
    pulse(in, n, dl);
    bus(in, out, n, 0);
    for (k = 1; k < np; k++) {
        double a0 = area(out, k * dl, (k + 1u) * dl), a1 = area(out, (k + 1u) * dl, (k + 2u) * dl);
        g += a0 != 0 ? a1 / a0 : 0;
    }
    free(in);
    free(out);
    return g / (np - 1u);
}

/* the broadband gain: a 10 ms noise burst, the RMS of repeat 2 over repeat 1 */
static double bb_gain(int32_t fdbk, int32_t color)
{
    uint32_t dl, n;
    int32_t *in, *out;
    double r1, r2;
    set_dly(fdbk, color);
    dl = delay_samples();
    n = 4u * dl / CTL * CTL + CTL;
    in = malloc(4u * n), out = malloc(4u * n);
    noise_burst(in, n, FS / 100u, 20000, 99);
    bus(in, out, n, 0);
    r1 = rms(out, dl, 2u * dl), r2 = rms(out, 2u * dl, 3u * dl);
    free(in);
    free(out);
    return r1 > 0 ? r2 / r1 : 0;
}

/* 1. the table: DC and broadband gain per pass at FDBK x COLOR; FDBK 0 one repeat, 60 as it was, 120 a gain of 1 */
static void t_table(void)
{
    static const int32_t FD[4] = {0, 60, 100, 120}, CO[3] = {0, 70, 127};
    uint32_t f, c;
    int ok0 = 1, ok60 = 1, ok120 = 1;
    printf("delay: per-repeat gain, DC (pulse area) / broadband (noise RMS); FDBK raw (shown %%), COLOR 0 / 70 / 127\n");
    for (f = 0; f < 4u; f++) {
        printf("delay:   FDBK %3d (%3d %%):", FD[f], FD[f] * 100 / 120);
        for (c = 0; c < 3u; c++) {
            double g = dc_gain(FD[f], CO[c], 5), b = bb_gain(FD[f], CO[c]);
            printf("  C%-3d %.4f (%+6.2f dB) / %.4f", CO[c], g, g > 0 ? 20 * log10(g) : -999.0, b);
            ok0 &= f != 0 || fabs(g) < 1e-9;
            ok60 &= f != 1 || fabs(g - 60 * 230 / 32768.0) < 0.003;
            ok120 &= f != 3 || g >= 0.9995;
        }
        printf("\n");
    }
    check(ok0, "FDBK 0: one repeat, nothing after it");
    check(ok60, "FDBK 60 (the default): the DC gain per repeat 60 x 230 / 2^15 (0.421) within 0.003, as before");
    check(ok120, "FDBK 120 (100 %): the DC gain per repeat at least 0.9995 at every COLOR");
}

/* 2. the gain never falls as FDBK rises (0..120, COLOR 70), and the top 85..100 % spans long .. endless */
static void t_curve(void)
{
    int32_t v;
    double prev = -1, g102 = 0, g114 = 0;
    int mono = 1;
    for (v = 0; v <= 120; v++) {
        double g = dc_gain(v, 70, 3);
        mono &= g >= prev - 1e-6;
        prev = g;
        if (v == 102)
            g102 = g;
        if (v == 114)
            g114 = g;
        if (v % 6 == 0 || v > 100)
            printf("delay:   curve FDBK %3d (%3d %%): DC gain %.4f, %5.0f repeats to -60 dB\n", v, v * 100 / 120, g,
                   g < 0.99999 && g > 0 ? -3.0 / log10(g) : 1e9);
    }
    check(mono, "the DC gain never falls as FDBK rises, 0..120");
    check(g102 > 0.70 && g102 < 0.75 && g114 > 0.95 && g114 < 0.995,
          "the top is stretched: 85 % (102) ~0.72 as before, 95 % (114) between 0.95 and 0.995");
}

/* 3. FDBK 120 holds: a pulse after 500 passes keeps over 98 % of its area (every COLOR), the bus still running */
static void t_endless(void)
{
    static const int32_t CO[3] = {0, 70, 127};
    uint32_t c, ok = 1, run = 1;
    for (c = 0; c < 3u; c++) {
        uint32_t dl, n, np = 500;
        int32_t *in, *out;
        double a1, an;
        set_dly(120, CO[c]);
        dl = delay_samples();
        n = (np + 2u) * dl / CTL * CTL + CTL;
        in = malloc(4u * n), out = malloc(4u * n);
        pulse(in, n, dl);
        bus(in, out, n, 0);
        a1 = area(out, dl, 2u * dl), an = area(out, np * dl, (np + 1u) * dl);
        printf("delay:   FDBK 120, COLOR %3d: repeat %u keeps %.4f of repeat 1's area (%.1f s)\n", CO[c], np,
               a1 != 0 ? an / a1 : 0, np * dl / (double)FS);
        ok &= a1 != 0 && an / a1 > 0.995;
        run &= fx.dly_q < DLY_LEN;
        free(in);
        free(out);
    }
    check(ok, "FDBK 100 %: endless repeats (500 passes, ~62 s, keep over 99.5 % of the first one's area)");
    check(run, "FDBK 100 %: the bus keeps running (not skipped as idle)");
}

/* 4. loud noise on top of a full loop (FDBK 120, COLOR 127): it saturates softly, never at the int16 rails; then the
 * input stops: the loop holds, bounded */
static void t_saturate(void)
{
    uint32_t n = 3u * FS / CTL * CTL, i;
    int32_t *in = malloc(4u * n), *out = malloc(4u * n), pk = 0;
    int rail = 0;
    double hold;
    set_dly(120, 127);
    noise_burst(in, n, n, 40000, 1234);
    bus(in, out, n, &rail);
    bus(0, out, n, &rail);
    for (i = 0; i < n; i++)
        pk = out[i] > pk ? out[i] : -out[i] > pk ? -out[i] : pk;
    hold = rms(out, n - FS, n);
    printf("delay:   saturator: 3 s of noise at 2x full scale on FDBK 120: peak out %d, RMS 2..3 s after %.0f\n", pk,
           hold);
    check(!rail, "a full loop with loud input saturates softly: no value written reaches the int16 rails");
    check(hold > 3000, "after the input stops the saturated loop keeps repeating (RMS > 3000)");
    free(in);
    free(out);
}

/* 5. a held delay (FDBK 120) lowered to 60 rings out to exactly 0 and the bus goes idle */
static void t_idle(void)
{
    uint32_t dl, n = 10u * FS / CTL * CTL, f, idle_at = 0;
    int32_t *in = malloc(4u * n);
    set_dly(120, 70);
    dl = delay_samples();
    pulse(in, n, dl);
    bus(in, 0, n, 0);
    check(fx.dly_q < DLY_LEN && !dly_idle(), "FDBK 100 %: 10 s after a pulse the bus is still busy");
    song.g[G_DFDBK] = 60;
    for (f = 0; f < 3u * n && !idle_at; f += CTL) {
        bus(0, 0, CTL, 0);
        if (dly_idle())
            idle_at = f + CTL;
    }
    printf("delay:   FDBK 120 -> 60: idle %.2f s later\n", idle_at / (double)FS);
    check(idle_at != 0, "FDBK lowered from 100 % to 60: the tail reaches exactly 0 and the bus goes idle within 30 s");
    free(in);
}

/* the renders: a stab (a decaying saw chord, 120 ms) into the delay, dry + wet, 20 s */
static void renders(const char *dir, const char *tag)
{
    static const int32_t FD[4] = {60, 72, 108, 120};
    uint32_t n = 20u * FS / CTL * CTL, i, f;
    int32_t *in = malloc(4u * n), *out = malloc(4u * n);
    static const double HZ[3] = {220.0, 277.18, 329.63};
    for (i = 0; i < n; i++) {
        double t = (double)i / FS, s = 0;
        uint32_t k;
        for (k = 0; k < 3u; k++)
            s += 2.0 * fmod(t * HZ[k], 1.0) - 1.0;
        in[i] = i < FS * 12u / 100u ? (int32_t)(5000.0 * s * exp(-t * 25.0)) : 0;
    }
    for (f = 0; f < 4u; f++) {
        char path[512];
        FILE *w;
        set_dly(FD[f], 70);
        song.g[G_DTIME] = 1;                     /* 1/8 at 120 BPM */
        bus(in, out, n, 0);
        snprintf(path, sizeof path, "%s/dly-%s-fdbk%d.wav", dir, tag, FD[f]);
        w = fopen(path, "wb");
        if (!w) {
            printf("delay: cannot write %s\n", path);
            fails++;
            continue;
        }
        wav_hdr(w, n);
        for (i = 0; i < n; i++)
            wav_put(w, in[i] / 2 + out[i] / 2, in[i] / 2 + out[i] / 2);
        fclose(w);
        printf("delay:   wrote %s\n", path);
    }
    free(in);
    free(out);
}

int main(int argc, char **argv)
{
    host_tracks_init();
    t_table();
    t_curve();
    t_endless();
    t_saturate();
    t_idle();
    if (argc > 1)
        renders(argv[1], argc > 2 ? argv[2] : "now");
    printf("delay: %u failed\n", fails);
    return fails != 0;
}
