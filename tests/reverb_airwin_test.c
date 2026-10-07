/* SPDX-License-Identifier: GPL-3.0-only */
/* The AIRWIN reverb (firmware/src/reverb_airwin.c, Airwindows' VerbTiny) through fx_buses, picked on TYPE when
 * other algorithms are built too (tests/run_tests.sh builds it alone, at half rate, beside ROOM and with FDN8 in the
 * pool: its 8, 16 and 32 KB rings):
 *   decay    a 20 ms low-passed noise burst: RT60 (T30) at SIZE 20 / 80 within 12 % of the ROOM's 0.62 / 1.23 s,
 *            rising with SIZE, 8 .. 20 s at SIZE 126; at every SIZE / DAMP it rings out to exactly 0 and the bus
 *            goes idle (the idle skip's conditions all hold)
 *   level    low-passed noise into the send (SIZE 80, DAMP 50): the wet RMS within 1.5 dB of the ROOM's (72.2 dB)
 *   stable   2 s of full-scale white noise, of a full-scale square and of full-scale DC into the send at SIZE 127,
 *            120 and 90 (DAMP 0, 0, 10), then silence: the wet stays bounded (below 2^19: no int32 product near
 *            overflow), the ring never sits at the rails afterwards, the tail falls (not DC's: it rests in the
 *            lines, unheard through the taps' signs, and spreads to the heard modes before it dies), and the bus
 *            goes idle within 300 / 120 / 20 s
 * Exit status: the number of failed checks. */
#define main hostsim_main
#include "hostsim.c"
#undef main

static uint32_t fails;
static void check(int ok, const char *what)
{
    printf("airwin: %-100s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}
static int rev_idle(void) { return fx.rev_q >= REV_Q && !REV_LP_BUSY() && !REV_HALF_BUSY(); }
static void reset(void)
{
    rev_tank_clear();
    fx.rev_q = FX_Q_MAX;
#if REV_TANK_HALF
    memset(&rev_half, 0, sizeof rev_half);
#endif
}
static void bus(const int32_t *in, int32_t *out, uint32_t n)   /* the bus alone; out interleaved (NULL: dropped) */
{
    static const int32_t z[CTL];
    int32_t wl[CTL], wr[CTL];
    uint32_t b, i;
    for (b = 0; b < n; b += CTL) {
        if (!fx_buses(z, z, in ? in + b : z, wl, wr, CTL))
            for (i = 0; i < CTL; i++)
                wl[i] = wr[i] = 0;
        for (i = 0; out && i < CTL; i++)
            out[2u * (b + i)] = wl[i], out[2u * (b + i) + 1u] = wr[i];
    }
}
static void noise_lp(int32_t *in, uint32_t n, uint32_t len, double amp, uint32_t seed)
{
    double lp1 = 0, lp2 = 0;
    uint32_t i;
    for (i = 0; i < n; i++) {
        double x;
        seed = seed * 1664525u + 1013904223u;
        x = i < len ? ((int32_t)(seed >> 16) - 32768) * amp : 0;
        lp1 += 0.45 * (x - lp1);
        lp2 += 0.45 * (lp1 - lp2);
        in[i] = (int32_t)(2.0 * lp2);
    }
}
/* RT60 from the Schroeder curve (T30 x 2) of x (stereo) from frame t0; past the curve's end: the 100 ms envelope's
 * slope (a near-freeze) */
static double rt60(const int32_t *x, uint32_t t0, uint32_t n)
{
    double *e = malloc(sizeof(double) * n), tot = 0, t5 = -1, t35 = -1;
    uint32_t i;
    for (i = n; i-- > t0;) {
        tot += (double)x[2 * i] * x[2 * i] + (double)x[2 * i + 1] * x[2 * i + 1];
        e[i] = tot;
    }
    for (i = t0; i < n && tot > 0; i++) {
        double db = 10 * log10(e[i] / tot + 1e-30);
        if (t5 < 0 && db <= -5)
            t5 = i;
        if (t35 < 0 && db <= -35) {
            t35 = i;
            break;
        }
    }
    free(e);
    return t5 < 0 || t35 < 0 ? -1 : 2.0 * (t35 - t5) / FS;
}
static double rms_db(const int32_t *x, uint32_t a, uint32_t b)
{
    double acc = 0;
    uint32_t i;
    for (i = 2 * a; i < 2 * b; i++)
        acc += (double)x[i] * x[i];
    return 10 * log10(acc / (2.0 * (b - a)) + 1e-30);
}

#define NLONG (40u * FS / CTL * CTL)
static void t_decay(void)
{
    static const uint8_t SZ[] = {0, 20, 40, 80, 90, 100, 120, 126}, DMP[] = {0, 30, 60, 127};
    int32_t *in = malloc(4u * NLONG), *out = malloc(8u * NLONG);
    double r[sizeof SZ], prev = 0;
    uint32_t s, d, i, rise = 1, idle_ok = 1;
    char what[128];
    noise_lp(in, NLONG, FS / 50, 0.5, 777);
    for (s = 0; s < sizeof SZ; s++) {
        song.g[G_RSIZE] = SZ[s], song.g[G_RDAMP] = SZ[s] == 20 ? 40 : 50;
        reset();
        bus(in, out, NLONG);
        r[s] = rt60(out, FS / 50, NLONG);
        printf("airwin: SIZE %3u DAMP %2u: RT60 %.2f s\n", SZ[s], song.g[G_RDAMP], r[s]);
        rise &= r[s] > prev;
        prev = r[s];
    }
    snprintf(what, sizeof what, "RT60 at SIZE 20 / 80: %.2f / %.2f s, the ROOM's 0.62 / 1.23 within 12 %%", r[1], r[3]);
    check(fabs(r[1] / 0.62 - 1) <= 0.12 && fabs(r[3] / 1.23 - 1) <= 0.12, what);
    snprintf(what, sizeof what, "RT60 rises with SIZE; %.1f s at SIZE 126 (8 .. 20)", r[7]);
    check(rise && r[7] >= 8 && r[7] <= 20, what);
    for (s = 0; s < sizeof SZ; s++)                    /* every SIZE / DAMP: rings out to exactly 0, the bus idle */
        for (d = 0; d < sizeof DMP; d++) {
            uint32_t ok = 0;
            song.g[G_RSIZE] = SZ[s], song.g[G_RDAMP] = DMP[d];
            reset();
            for (i = 0; i < NLONG; i += CTL) {
                bus(in + i, NULL, CTL);
                if (i > FS / 50 && rev_idle()) {
                    ok = 1;
                    break;
                }
            }
            idle_ok &= ok;
            if (!ok)
                printf("airwin: SIZE %u DAMP %u: not idle 40 s after the burst\n", SZ[s], DMP[d]);
        }
    check(idle_ok, "a burst rings out to exactly 0 and the bus goes idle within 40 s, SIZE 0 .. 126 x DAMP 0 .. 127");
    free(in);
    free(out);
}
static void t_level(void)
{
    uint32_t n = 3u * FS / CTL * CTL;
    int32_t *in = malloc(4u * n), *out = malloc(8u * n);
    double l;
    char what[128];
    song.g[G_RSIZE] = 80, song.g[G_RDAMP] = 50;
    noise_lp(in, n, n, 1 / 3.0, 12345);
    reset();
    bus(in, out, n);
    l = rms_db(out, FS, n);
    snprintf(what, sizeof what, "low-passed noise at SIZE 80 DAMP 50: the wet %.1f dB, the ROOM's 72.2 within 1.5 dB",
             l);
    check(fabs(l - 72.2) <= 1.5, what);
    free(in);
    free(out);
}
static void t_stable(void)
{
    static const struct { uint8_t size, damp; uint16_t lim_s; } ST[] = {{127, 0, 300}, {120, 0, 120}, {90, 10, 20}};
    static const char *const SIG[] = {"white noise", "a square (110 Hz)", "DC"};
    uint32_t s, k, t, i;
    char what[160];
    for (k = 0; k < 3u; k++)
        for (s = 0; s < sizeof ST / sizeof ST[0]; s++) {
            uint32_t seed = 99, lim = ST[s].lim_s * FS, rails = 0;
            int32_t blk[CTL], o[2 * CTL];
            double pk = 0, e1 = 0, e2 = 0, idle = -1;
            song.g[G_RSIZE] = ST[s].size, song.g[G_RDAMP] = ST[s].damp;
            reset();
            for (t = 0; t < lim + 2u * FS; t += CTL) {
                int loud = t < 2u * FS;
                for (i = 0; i < CTL; i++) {
                    seed = seed * 1664525u + 1013904223u;
                    blk[i] = k == 0 ? (int32_t)(seed >> 16) - 32768 : k == 1 ? (((t + i) / 200u) & 1u ? 32767 : -32768)
                                                                             : 32767;
                }
                bus(loud ? blk : NULL, o, CTL);
                for (i = 0; i < 2u * CTL; i++) {
                    double v = fabs((double)o[i]);
                    pk = v > pk ? v : pk;
                    if (t >= 3u * FS && t < 4u * FS)
                        e1 += v * v;
                    if (t >= 6u * FS && t < 7u * FS)
                        e2 += v * v;
                }
                if (t == 3u * FS)                       /* (1 s after: no cell held at the rails) */
                    for (i = 0; i < sizeof rev_line / 2u; i++)
                        rails += rev_line[i] >= 32767 || rev_line[i] <= -32768;
                if (t >= 2u * FS && rev_idle()) {
                    idle = (t + CTL) / (double)FS;
                    break;
                }
            }
            printf("airwin: %-18s SIZE %3u DAMP %2u: peak %.0f, %u cells at the rails 1 s after, %.1f / %.1f dB at "
                   "1 / 4 s, idle at %.1f s\n", SIG[k], ST[s].size, ST[s].damp, pk, rails, 10 * log10(e1 + 1e-30),
                   10 * log10(e2 + 1e-30), idle);
            snprintf(what, sizeof what, "full-scale %s at SIZE %u DAMP %u, then silence: bounded, falls, idle within "
                     "%u s", SIG[k], ST[s].size, ST[s].damp, ST[s].lim_s);
            check(pk < (double)(1 << 19) && rails == 0 && (k == 2 || (idle > 0 && idle < 4) || e2 < e1) && idle > 0 &&
                  idle <= ST[s].lim_s + 2, what);
        }
}

int main(void)
{
    host_tracks_init();
#if REV_MULTI
    bp_set[BPS_RTYPE] = (int16_t)rev_index(RT_AIRWIN);   /* TYPE: AIRWIN (the bus idle: switched at the next block) */
    bus(NULL, NULL, CTL);
    check(rsel.type == RT_AIRWIN, "TYPE picks AIRWIN");
#endif
    printf("airwin: the ring %u samples (%u KB), the lines %u cells, REV_HALF %d\n", RV_N, RV_N / 512u, RBA_END,
           FELUCCA_REV_HALF);
    t_decay();
    t_level();
    t_stable();
    printf("airwin: %u failed\n", fails);
    return fails != 0;
}
