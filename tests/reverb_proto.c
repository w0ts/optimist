/* SPDX-License-Identifier: GPL-3.0-only */
/* The reverb tanks side by side (exp/reverb): one build per tank and budget (FELUCCA_REVERB 0 ROOM, 1 PLATE,
 * 2 FDN8; FELUCCA_REV_HALF: half the RAM), each run renders and measures the same material:
 *   build/host/reverb_proto LABEL WAVDIR [TSV]
 * Settings (SIZE / DAMP): small 20 / 40, medium 80 / 50, long 120 / 30, dark 120 / 100.
 * Per setting: WAVDIR/LABEL-SETTING-mix.wav and -wet.wav: a snare (the kit's 38), an ANALOG SUPER PAD chord
 * and a TRAP PLUCK note, 4 s apart, each with REV 100, no chorus or delay; the wet file is the reverb alone, 6 dB down.
 * Numbers (one line per setting, appended to TSV):
 *   rt60      T30 of a 20 ms low-passed noise burst (as tests/reverb_test.c), s
 *   mix_ms    the impulse response's mixing time: the first 20 ms window whose normalised echo density
 *             (Abel and Huang 2006: the share of samples beyond one standard deviation / erfc(1 / sqrt 2)) is
 *             0.9 or more, ms (lower: dense sooner)
 *   ned_mean  the mean echo density 0..100 ms (1: Gaussian, like noise)
 *   ripple_db the late impulse response's spectrum (Welch, 8192 points, 0.1 s .. 1.5 s), its deviation from
 *             its own third-octave smoothing, RMS over 200 Hz .. 5 kHz, dB (higher: more isolated modes,
 *             metallic ringing; a noise-like tail reads about 1.5 here)
 *   peak_db   the largest such deviation, dB (a single ringing mode)
 *   env_db    the impulse response's 5 ms RMS envelope around its fitted exponential decay, RMS dB, from
 *             30 ms down to -40 dB (higher: lumpy, fluttering decay)
 *   iacc      the left / right correlation of the impulse response from 30 ms, the largest over +-1 ms
 *             (lower: wider)
 *   lvl_db    the wet RMS of low-passed noise into the send, dB (the levels match)
 *   ipc       instructions per output sample of the bus alone, the tank running (the host's counter) */
#define main hostsim_main
#include "hostsim.c"
#undef main
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#endif

static uint64_t instr_now(void)
{
#ifdef __APPLE__
    struct rusage_info_v4 ri;
    if (!proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri))
        return ri.ri_instructions;
#endif
    return 0;
}

#define NSET 4
static const struct { const char *name; uint8_t size, damp; } SET[NSET] = {
    {"small", 20, 40}, {"medium", 80, 50}, {"long", 120, 30}, {"dark", 120, 100}};

static void rev_reset(void)
{
    rev_tank_clear();
    fx.rev_q = FX_Q_MAX;
#if REV_TANK_HALF
    memset(&rev_half, 0, sizeof rev_half);
#endif
}

static void bus(const int32_t *in, int32_t *out, uint32_t n)   /* the bus alone; out interleaved */
{
    static const int32_t z[CTL];
    int32_t wl[CTL], wr[CTL];
    uint32_t b, i;
    for (b = 0; b < n; b += CTL) {
        fx_buses(z, z, in ? in + b : z, wl, wr, CTL);
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

static void fft(double *re, double *im, uint32_t n)
{
    uint32_t i, j = 0, k, m;
    for (i = 1; i < n; i++) {
        for (k = n >> 1; j & k; k >>= 1)
            j ^= k;
        j |= k;
        if (i < j) {
            double t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (m = 2; m <= n; m <<= 1) {
        double a = -2 * M_PI / m;
        for (i = 0; i < n; i += m)
            for (k = 0; k < m / 2; k++) {
                double c = cos(a * k), s = sin(a * k), *xr = re + i + k, *xi = im + i + k;
                double tr = c * xr[m / 2] - s * xi[m / 2], ti = s * xr[m / 2] + c * xi[m / 2];
                xr[m / 2] = xr[0] - tr, xi[m / 2] = xi[0] - ti;
                xr[0] += tr, xi[0] += ti;
            }
    }
}

/* the normalised echo density of x (one channel, stride 2) in a window of w frames from a */
static double ned(const int32_t *x, uint32_t a, uint32_t w)
{
    double m = 0, s = 0, sd;
    uint32_t i, c = 0;
    for (i = a; i < a + w; i++)
        m += x[2 * i];
    m /= w;
    for (i = a; i < a + w; i++)
        s += (x[2 * i] - m) * (x[2 * i] - m);
    sd = sqrt(s / w);
    if (sd <= 0)
        return 0;
    for (i = a; i < a + w; i++)
        c += fabs(x[2 * i] - m) > sd;
    return (double)c / w / 0.3173105;
}

#define NFFT 8192u
static void ripple(const int32_t *x, uint32_t a, uint32_t b, double *rms_db, double *peak_db)
{
    static double re[NFFT], im[NFFT], p[NFFT / 2], d[NFFT / 2];
    uint32_t s, i, j, ch, nb = 0;
    double acc = 0, pk = 0;
    for (i = 0; i < NFFT / 2; i++)
        p[i] = 0;
    for (s = a; s + NFFT <= b; s += NFFT / 2)
        for (ch = 0; ch < 2u; ch++) {
            double e = 0;
            for (i = 0; i < NFFT; i++)
                e += (double)x[2 * (s + i) + ch] * x[2 * (s + i) + ch];
            if (e <= 0)
                continue;
            for (i = 0; i < NFFT; i++)
                re[i] = x[2 * (s + i) + ch] * (0.5 - 0.5 * cos(2 * M_PI * i / NFFT)) / sqrt(e), im[i] = 0;
            fft(re, im, NFFT);
            for (i = 1; i < NFFT / 2; i++)
                p[i] += re[i] * re[i] + im[i] * im[i];
        }
    for (i = 1; i < NFFT / 2; i++)
        d[i] = 10 * log10(p[i] + 1e-30);
    for (i = 1; i < NFFT / 2; i++) {
        double f = (double)i * FS / NFFT, lo = f / 1.122, hi = f * 1.122, sm = 0;
        uint32_t c = 0;
        if (f < 200 || f > 5000)
            continue;
        for (j = (uint32_t)(lo * NFFT / FS); j <= (uint32_t)(hi * NFFT / FS) && j < NFFT / 2; j++)
            sm += p[j], c++;
        sm = 10 * log10(sm / c + 1e-30);
        acc += (d[i] - sm) * (d[i] - sm), nb++;
        if (d[i] - sm > pk)
            pk = d[i] - sm;
    }
    *rms_db = sqrt(acc / (nb ? nb : 1));
    *peak_db = pk;
}

static double env_rough(const int32_t *x, uint32_t a, uint32_t n)
{
    enum { W = FS / 200 };
    double db[4096], t[4096], e0 = 0, sx = 0, sy = 0, sxx = 0, sxy = 0, k, c, acc = 0;
    uint32_t m = 0, f, i;
    for (f = a; f + W <= n && m < 4096u; f += W) {
        double e = 0;
        for (i = f; i < f + W; i++)
            e += (double)x[2 * i] * x[2 * i] + (double)x[2 * i + 1] * x[2 * i + 1];
        e = 10 * log10(e / W + 1e-9);
        if (!m)
            e0 = e;
        if (e < e0 - 40)
            break;
        db[m] = e, t[m] = m, m++;
    }
    for (i = 0; i < m; i++)
        sx += t[i], sy += db[i], sxx += t[i] * t[i], sxy += t[i] * db[i];
    k = (m * sxy - sx * sy) / (m * sxx - sx * sx);
    c = (sy - k * sx) / m;
    for (i = 0; i < m; i++)
        acc += (db[i] - (c + k * t[i])) * (db[i] - (c + k * t[i]));
    return m > 2 ? sqrt(acc / m) : -1;
}

static double iacc(const int32_t *x, uint32_t a, uint32_t b)
{
    int32_t lag, ml = FS / 1000;
    double best = 0, el = 0, er = 0;
    uint32_t i;
    for (i = a; i < b; i++)
        el += (double)x[2 * i] * x[2 * i], er += (double)x[2 * i + 1] * x[2 * i + 1];
    for (lag = -ml; lag <= ml; lag++) {
        double s = 0;
        for (i = a + ml; i < b - ml; i++)
            s += (double)x[2 * i] * x[2 * (i + lag) + 1];
        s = fabs(s) / sqrt(el * er + 1e-30);
        best = s > best ? s : best;
    }
    return best;
}

static double rms_db(const int32_t *x, uint32_t a, uint32_t b)
{
    double acc = 0;
    uint32_t i;
    for (i = 2 * a; i < 2 * b; i++)
        acc += (double)x[i] * x[i];
    return 20 * log10(sqrt(acc / (2.0 * (b - a))) + 1e-9);
}

/* the three sounds through the mix, 4 s apart */
#define SEG (4u * FS / CTL * CTL)
static void render_sounds(const char *dir, const char *label, const char *set)
{
    uint32_t n = 3u * SEG, f, i;
    int32_t *wet = malloc(8u * n), *mix = malloc(8u * n), o[2 * CTL];
    static const uint8_t CH[4] = {48, 55, 59, 64};
    char path[600];
    uint32_t m;
    host_tracks_init();
    song.g[G_RSIZE] = 0;   /* (set below again: host_tracks_init resets the globals) */
    for (i = 0; i < NSET; i++)
        if (!strcmp(SET[i].name, set))
            song.g[G_RSIZE] = SET[i].size, song.g[G_RDAMP] = SET[i].damp;
    rev_reset();
    song.g[G_DRREV] = 100;
    host_preset(&trk[0], 0, 17u);                       /* ANALOG SUPER PAD */
    host_preset(&trk[1], 0, 10u);                       /* TRAP PLUCK */
    for (i = 0; i < 2u; i++) {
        trk[i].p[P_AMODE] = 0;
        trk[i].p[P_CHOR] = trk[i].p[P_DLY] = 0;
        trk[i].p[P_REV] = 100;
    }
    for (f = 0; f < n; f += CTL) {
        if (f == CTL)
            input_on(TDRUM, 38, 115);
        if (f == CTL * 20)
            input_off(TDRUM, 38);
        if (f == SEG)
            for (i = 0; i < 4u; i++)
                input_on(&trk[0], CH[i], 100);
        if (f == SEG + FS * 3 / 2 / CTL * CTL)
            for (i = 0; i < 4u; i++)
                input_off(&trk[0], CH[i]);
        if (f == 2u * SEG)
            input_on(&trk[1], 60, 100);
        if (f == 2u * SEG + FS * 3 / 10 / CTL * CTL)
            input_off(&trk[1], 60);
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++) {
            mix[2 * (f + i)] = o[2 * i], mix[2 * (f + i) + 1] = o[2 * i + 1];
            wet[2 * (f + i)] = wet_l[i], wet[2 * (f + i) + 1] = wet_r[i];
        }
    }
    for (m = 0; m < 2u; m++) {
        FILE *fo;
        snprintf(path, sizeof path, "%s/%s-%s-%s.wav", dir, label, set, m ? "wet" : "mix");
        if (!(fo = fopen(path, "wb"))) {
            printf("cannot write %s\n", path);
            continue;
        }
        wav_hdr(fo, n);
        for (i = 0; i < n; i++)
            wav_put(fo, m ? wet[2 * i] >> 1 : mix[2 * i], m ? wet[2 * i + 1] >> 1 : mix[2 * i + 1]);   /* (wet: -6 dB, unclipped) */
        fclose(fo);
    }
    free(wet);
    free(mix);
}

int main(int argc, char **argv)
{
    uint32_t n = 8u * FS / CTL * CTL, s, i, idle_ok = 1;
    int32_t *in = malloc(4u * n), *out = malloc(8u * n);
    FILE *tsv = NULL;
    if (argc < 3) {
        fprintf(stderr, "usage: %s LABEL WAVDIR [TSV]\n", argv[0]);
        return 2;
    }
    if (argc > 3 && !(tsv = fopen(argv[3], "a")))
        return 1;
    host_tracks_init();
    printf("%-14s %-7s %6s %7s %6s %9s %7s %6s %5s %7s %6s\n", "tank", "setting", "rt60", "mix_ms", "ned", "ripple_db",
           "peak_db", "env_db", "iacc", "lvl_db", "ipc");
    for (s = 0; s < NSET; s++) {
        double r60, mix_ms = -1, nedm = 0, rip, pk, env, ic, lvl, ipc;
        uint32_t w = FS / 50, idle_at = 0;
        uint64_t i0;
        song.g[G_RSIZE] = SET[s].size;
        song.g[G_RDAMP] = SET[s].damp;
        /* burst: RT60 and the ring-out to exactly 0 */
        noise_lp(in, n, FS / 50, 0.5, 777);
        rev_reset();
        for (i = 0; i < n; i += CTL) {
            bus(in + i, out + 2 * i, CTL);
            if (!idle_at && i > FS / 50 && fx.rev_q >= REV_Q && !REV_LP_BUSY() && !REV_HALF_BUSY())
                idle_at = i;
        }
        idle_ok &= idle_at != 0;
        r60 = rt60(out, FS / 50, n);
        /* impulse: density, ripple, envelope, width */
        for (i = 0; i < n; i++)
            in[i] = 0;
        in[0] = 30000;
        rev_reset();
        bus(in, out, n);
        for (i = 0; i + w < FS / 2; i += w / 4) {
            double d = ned(out, i, w);
            if (i < FS / 10)
                nedm += d / (FS / 10 / (w / 4) + 1);
            if (mix_ms < 0 && d >= 0.9)
                mix_ms = 1000.0 * i / FS;
        }
        ripple(out, FS / 10, FS * 3 / 2, &rip, &pk);
        env = env_rough(out, FS * 3 / 100, n);
        ic = iacc(out, FS * 3 / 100, FS);
        /* level */
        noise_lp(in, n, n, 1 / 3.0, 12345);
        rev_reset();
        bus(in, out, 3 * FS / CTL * CTL);
        lvl = rms_db(out, FS, 3 * FS / CTL * CTL);
        /* CPU: the bus alone, the tank running all along */
        rev_reset();
        bus(in, out, FS / CTL * CTL);
        i0 = instr_now();
        bus(in + FS, NULL, 4u * FS / CTL * CTL);
        ipc = i0 ? (double)(instr_now() - i0) / (4.0 * FS / CTL * CTL) : 0;
        printf("%-14s %-7s %6.2f %7.1f %6.2f %9.2f %7.1f %6.2f %5.2f %7.1f %6.1f\n", argv[1], SET[s].name, r60, mix_ms,
               nedm, rip, pk, env, ic, lvl, ipc);
        if (tsv)
            fprintf(tsv, "%s\t%s\t%.2f\t%.1f\t%.2f\t%.2f\t%.1f\t%.2f\t%.2f\t%.1f\t%.1f\t%.1f\n", argv[1], SET[s].name, r60,
                    mix_ms, nedm, rip, pk, env, ic, lvl, ipc, idle_at / (double)FS);
        render_sounds(argv[2], argv[1], SET[s].name);
    }
    printf("%s: ring-out to exactly 0 and idle at every setting: %s\n", argv[1], idle_ok ? "ok" : "FAIL");
    if (tsv)
        fclose(tsv);
    free(in);
    free(out);
    return !idle_ok;
}
