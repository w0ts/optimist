/* SPDX-License-Identifier: GPL-3.0-only */
/* The reverb tanks side by side: one build per tank and budget (FELUCCA_REVERB 0 ROOM, 1 PLATE, 2 FDN8;
 * FELUCCA_REV_HALF: half the RAM), each run renders and measures the same material:
 *   build/host/reverb_proto LABEL WAVDIR [TSV]
 * Settings (SIZE / DAMP): small 20 / 40, medium 80 / 50, long 120 / 30, dark 120 / 100, huge 127 / 30.
 * Per setting medium, long, huge: WAVDIR/LABEL-SETTING-mix.wav and -wet.wav: a snare (the kit's 38), an ANALOG
 * SUPER PAD chord (1.5 s), a TRAP PLUCK note and one ANALOG SUPER PAD note held 4 s, 5 s apart, each with REV
 * 100, no chorus or delay; the wet file is the reverb alone, 6 dB down.
 * Numbers (one line per setting, appended to TSV):
 *   rt60      the decay time of a 20 ms low-passed noise burst, 64 s rendered: T30 of its Schroeder curve (as
 *             tests/reverb_test.c), or past 64 s the slope of its 100 ms envelope, s
 *   rt4k      the same in the octave band at 4 kHz, s (rt4k / rt60 falling: the tail goes dull)
 *   mix_ms    the impulse response's mixing time: the first 20 ms window whose normalised echo density
 *             (Abel and Huang 2006: the share of samples beyond one standard deviation / erfc(1 / sqrt 2)) is
 *             0.9 or more, ms (lower: dense sooner)
 *   ned_mean  the mean echo density 0..100 ms (1: Gaussian, like noise)
 *   ripple_db the late impulse response's spectrum (Welch, 8192 points, 0.1 s .. 1.5 s), its deviation from
 *             its own third-octave smoothing, RMS over 200 Hz .. 5 kHz, dB (higher: more isolated modes,
 *             metallic ringing; a noise-like tail reads about 1.5 here)
 *   peak_db   the largest such deviation, dB (a single ringing mode)
 *   env_db    the impulse response's 5 ms RMS envelope around its fitted exponential decay, RMS dB, from
 *             30 ms down to -40 dB or 10 LSB RMS (higher: lumpy, fluttering decay)
 *   iacc      the left / right correlation of the impulse response from 30 ms, the largest over +-1 ms
 *             (lower: wider)
 *   lvl_db    the wet RMS of low-passed noise into the send, dB (the levels match)
 *   ipc       instructions per output sample of the bus alone, the tank running (the host's counter)
 *   idle_s    when the burst's tail had rung out to exactly 0 and the bus went idle, s
 *   late_db   (RT60 of 3 s or more) ripple_db and peak_db of the late tail, 1.5 .. 5.5 s, the impulse 8.5 dB up:
 *   late_pk   the long tail's ringing modes
 *   ring_db   (RT60 of 3 s or more) the ringing: the late tail of a flat-spectrum burst, its spectrum's RMS over its
 *   ring_pk   third-octave median and the 99th percentile, dB (ringing() below)
 * Then the SIZE curve (RT60 at SIZE 0 .. 127, DAMP 30) and the stability: 2 s of full-scale white noise into the
 * send at SIZE 120 and 127, DAMP 0, then silence: the output's peak, the ring cells at the rails, the energy 1 s
 * and 60 s after, and when the bus went idle (it must, within 15 min).
 * FDN8 (FELUCCA_REVERB 2) also checks its long top: the curve rises, RT60 8 .. 20 s at SIZE 126, a near-freeze
 * (30 s or more) at 127, small and medium within 12 % of the ROOM's 0.62 / 1.23 s, no runaway, and its ringing:
 * ring_db at long / huge at most 1.5 / 1.05 dB with the 32 KB ring (REV_POOL), 2.0 / 1.4 with 16 KB, 2.6 / 2.1 with
 * 8 KB (measured 1.37 / 0.87, 1.76 / 1.08, 2.39 / 1.84; before 2026-10-07's fdn8-ring, which made the pool's ring
 * twice as long: 16 KB 1.61 / 1.11, 8 KB 2.42 / 1.56). */
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

#define NSET 5
static const struct { const char *name; uint8_t size, damp, render; } SET[NSET] = {
    {"small", 20, 40, 0}, {"medium", 80, 50, 1}, {"long", 120, 30, 1}, {"dark", 120, 100, 0}, {"huge", 127, 30, 1}};

static void rev_reset(void)
{
    rev_tank_clear();
    fx.rev_q = FX_Q_MAX;
#if REV_TANK_HALF
    memset(&rev_half, 0, sizeof rev_half);
#endif
}
static int rev_idle(void) { return fx.rev_q >= REV_Q && !REV_LP_BUSY() && !REV_HALF_BUSY(); }

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

/* the slope of the 100 ms envelope from 5 dB below its start down to -35 dB or the end, as RT60 (s) */
static double rt60_fit(const int32_t *x, uint32_t t0, uint32_t n)
{
    enum { W = FS / 10 };
    double sx = 0, sy = 0, sxx = 0, sxy = 0, e0 = 0, last = 0, k;
    uint32_t f, i, m = 0;
    for (f = t0; f + W <= n; f += W) {
        double e = 0;
        for (i = f; i < f + W; i++)
            e += (double)x[2 * i] * x[2 * i] + (double)x[2 * i + 1] * x[2 * i + 1];
        e = 10 * log10(e / W + 1e-30);
        if (f == t0)
            e0 = e;
        if (e > e0 - 5)
            continue;
        if (e < e0 - 35 || e < 0)
            break;
        sx += f, sy += e, sxx += (double)f * f, sxy += f * e, m++, last = e;
    }
    if (m < 5 || e0 - last < 8)
        return -1;
    k = (m * sxy - sx * sy) / (m * sxx - sx * sx);       /* dB per sample */
    return k < 0 ? -60.0 / (k * FS) : -1;
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
    if (t35 < 0 || t35 > n - 4u * FS)                     /* (the Schroeder curve's end too near: the slope) */
        return rt60_fit(x, t0, n);
    return 2.0 * (t35 - t5) / FS;
}

/* x through the octave band at fc (RBJ band-pass, 0 dB peak, Q 1.41), x 16, into y */
static void band(const int32_t *x, int32_t *y, uint32_t n, double fc)
{
    double w = 2 * M_PI * fc / FS, al = sin(w) / (2 * 1.41), a0 = 1 + al;
    double b0 = al / a0, a1 = -2 * cos(w) / a0, a2 = (1 - al) / a0;
    uint32_t ch, i;
    for (ch = 0; ch < 2u; ch++) {
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        for (i = 0; i < n; i++) {
            double v = x[2 * i + ch], o = b0 * v - b0 * x2 - a1 * y1 - a2 * y2;
            x2 = x1, x1 = v, y2 = y1, y1 = o;
            y[2 * i + ch] = (int32_t)lrint(16 * o);
        }
    }
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

/* Ringing: the late tail's spectral peaks that the modulation leaves standing (sparse modes, a cluster that rings).
 * A burst of RING_N samples with an exactly flat magnitude spectrum (20 Hz .. 9 kHz, random phases, RMS 4000) into
 * the send, then the tail from t60 x 0.3 to t60 x 0.7 after it (8 .. 20 s at most): its power spectrum (Hann, 8192
 * points, hop 2048, mid + side), each bin's level over its third-octave median, 200 Hz .. 5 kHz:
 *   ring_db  their RMS, dB (an ideal exponentially decaying noise reads ~0.7 at 11 s, ~0.3 at a minute; a fixed,
 *            unmodulated FDN8 ~3.7)
 *   ring_pk  their 99th percentile, dB (the tallest few modes) */
#define RING_N (1u << 17)
static int dcmp(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}
static void ringing(uint32_t size, uint32_t damp, double t60, int32_t *in, int32_t *out, double *ring_db, double *ring_pk)
{
    static double re[RING_N], im[RING_N], p[NFFT / 2], d[NFFT / 2], dv[NFFT / 2], win[256];
    double la_s = t60 * 0.3 < 8 ? t60 * 0.3 : 8, lb_s = t60 * 0.7 < 20 ? t60 * 0.7 : 20, e = 0, acc = 0;
    uint32_t i, j, s, ch, k, nb = 0, seed = 4242u, la, lb, n;
    *ring_db = *ring_pk = -1;
    if (t60 <= 0 || lb_s - la_s < 1)
        return;
    for (i = 0; i < RING_N; i++)
        re[i] = im[i] = 0;
    for (k = 1; k < RING_N / 2; k++) {                   /* unit magnitude, random phase; the inverse as a conjugate */
        double f = (double)k * FS / RING_N, ph;
        if (f < 20 || f > 9000)
            continue;
        seed = seed * 1664525u + 1013904223u;
        ph = 2 * M_PI * (seed >> 8) / 16777216.0;
        re[k] = re[RING_N - k] = cos(ph);
        im[k] = sin(ph), im[RING_N - k] = -sin(ph);
    }
    fft(re, im, RING_N);
    for (i = 0; i < RING_N; i++)
        e += re[i] * re[i];
    la = RING_N + (uint32_t)(la_s * FS), lb = RING_N + (uint32_t)(lb_s * FS);
    n = (lb + NFFT) / CTL * CTL + CTL;
    for (i = 0; i < n; i++)
        in[i] = i < RING_N ? (int32_t)lrint(re[i] * 4000 / sqrt(e / RING_N)) : 0;
    song.g[G_RSIZE] = size, song.g[G_RDAMP] = damp;
    rev_reset();
    bus(in, out, n);
    for (i = 0; i < NFFT / 2; i++)
        p[i] = 0;
    for (s = la; s + NFFT <= lb; s += NFFT / 4)
        for (ch = 0; ch < 2u; ch++) {                    /* mid, side */
            for (i = 0; i < NFFT; i++) {
                double l = out[2 * (s + i)], r = out[2 * (s + i) + 1];
                re[i] = (ch ? l - r : l + r) * (0.5 - 0.5 * cos(2 * M_PI * i / NFFT)), im[i] = 0;
            }
            fft(re, im, NFFT);
            for (i = 1; i < NFFT / 2; i++)
                p[i] += re[i] * re[i] + im[i] * im[i];
        }
    for (i = 1; i < NFFT / 2; i++)
        d[i] = 10 * log10(p[i] + 1e-30);
    for (i = 1; i < NFFT / 2; i++) {
        double f = (double)i * FS / NFFT;
        uint32_t a = (uint32_t)ceil(f / 1.122 * NFFT / FS), b = (uint32_t)(f * 1.122 * NFFT / FS), m = 0;
        if (f < 200 || f > 5000)
            continue;
        for (j = a; j <= b && j < NFFT / 2 && m < 256u; j++)
            win[m++] = d[j];
        qsort(win, m, sizeof win[0], dcmp);
        dv[nb] = d[i] - (m & 1 ? win[m / 2] : (win[m / 2 - 1] + win[m / 2]) / 2);
        acc += dv[nb] * dv[nb], nb++;
    }
    qsort(dv, nb, sizeof dv[0], dcmp);
    *ring_db = sqrt(acc / nb);
    *ring_pk = dv[(uint32_t)(0.99 * (nb - 1))];
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
        if (!m && e < 20)                               /* (the pre-delay: from the first sound) */
            continue;
        if (!m)
            e0 = e;
        if (e < e0 - 40 || e < 20)                      /* (or 10 LSB RMS: below it the int16 steps, not the tank) */
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

/* the four sounds through the mix, 5 s apart, 10 s of tail after the last */
#define SEG (5u * FS / CTL * CTL)
static void render_sounds(const char *dir, const char *label, uint32_t set)
{
    uint32_t n = 6u * SEG, f, i, m;
    int32_t *wet = malloc(8u * n), *mix = malloc(8u * n), o[2 * CTL];
    static const uint8_t CH[4] = {48, 55, 59, 64};
    char path[600];
    host_tracks_init();
    song.g[G_RSIZE] = SET[set].size, song.g[G_RDAMP] = SET[set].damp;   /* (after host_tracks_init: it resets them) */
    rev_reset();
    host_drum_rev(100);                                 /* (every drum sound's REV: drum_sends.c) */
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
        if (f == 3u * SEG)
            input_on(&trk[0], 57, 100);
        if (f == 3u * SEG + 4u * FS / CTL * CTL)
            input_off(&trk[0], 57);
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++) {
            mix[2 * (f + i)] = o[2 * i], mix[2 * (f + i) + 1] = o[2 * i + 1];
            wet[2 * (f + i)] = wet_l[i], wet[2 * (f + i) + 1] = wet_r[i];
        }
    }
    for (m = 0; m < 2u; m++) {
        FILE *fo;
        snprintf(path, sizeof path, "%s/%s-%s-%s.wav", dir, label, SET[set].name, m ? "wet" : "mix");
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

#define NLONG (64u * FS / CTL * CTL)                     /* the burst renders: 64 s */

/* a burst of low-passed noise at SIZE / DAMP: RT60 (s), the 4 kHz band's in *r4k, the time to idle in *idle_s */
static double burst(uint32_t size, uint32_t damp, int32_t *in, int32_t *out, int32_t *tmp, double *r4k, double *idle_s)
{
    uint32_t i, idle_at = 0;
    song.g[G_RSIZE] = size, song.g[G_RDAMP] = damp;
    noise_lp(in, NLONG, FS / 50, 0.5, 777);
    rev_reset();
    for (i = 0; i < NLONG; i += CTL) {
        bus(in + i, out + 2 * i, CTL);
        if (!idle_at && i > FS / 50 && rev_idle())
            idle_at = i;
    }
    if (idle_s)
        *idle_s = idle_at ? idle_at / (double)FS : -1;
    if (r4k) {
        band(out, tmp, NLONG, 4000);
        *r4k = rt60(tmp, FS / 50, NLONG);
    }
    return rt60(out, FS / 50, NLONG);
}

/* 2 s of full-scale white noise into the send, then silence until the bus is idle (15 min at most). Returns 1 when
 * it went idle and did not grow */
static int stability(const char *label, uint32_t size, uint32_t damp, FILE *tsv)
{
    uint32_t t, i, seed = 99, rails = 0, lim = 15u * 60u * FS;
    int32_t blk[CTL], o[2 * CTL];
    double e1 = 0, e60 = 0, pk = 0, idle = -1;
    song.g[G_RSIZE] = size, song.g[G_RDAMP] = damp;
    rev_reset();
    for (t = 0; t < lim; t += CTL) {
        int noisy = t < 2u * FS;
        for (i = 0; i < CTL; i++) {
            seed = seed * 1664525u + 1013904223u;
            blk[i] = noisy ? (int32_t)(seed >> 16) - 32768 : 0;
        }
        bus(noisy ? blk : NULL, o, CTL);
        for (i = 0; i < 2u * CTL; i++) {
            double v = fabs((double)o[i]);
            pk = v > pk ? v : pk;
            if (t >= 3u * FS && t < 4u * FS)
                e1 += v * v;
            if (t >= 62u * FS && t < 63u * FS)
                e60 += v * v;
        }
        if (t == 2u * FS) {                             /* the ring at the burst's end: cells at the rails */
            for (i = 0; i < sizeof rev_line / 2u; i++)
                rails += rev_line[i] >= 32767 || rev_line[i] <= -32768;
        }
        if (t > 63u * FS && rev_idle()) {
            idle = t / (double)FS;
            break;
        }
    }
    e1 = 10 * log10(e1 / (2.0 * FS) + 1e-30), e60 = 10 * log10(e60 / (2.0 * FS) + 1e-30);
    printf("%-14s stability SIZE %3u DAMP %3u: peak %.0f (%.1f dBFS), %u ring cells at the rails, %.1f dB at 1 s, "
           "%.1f dB at 60 s, idle at %.0f s\n", label, size, damp, pk, 20 * log10(pk / 32768 + 1e-30), rails, e1, e60,
           idle);
    if (tsv)
        fprintf(tsv, "%s\tstability-%u\t%.0f\t%u\t%.1f\t%.1f\t%.0f\n", label, size, pk, rails, e1, e60, idle);
    return idle > 0 && e60 < e1;
}

int main(int argc, char **argv)
{
    static const uint8_t CURVE[] = {0, 20, 40, 64, 80, 90, 100, 110, 120, 124, 126, 127};
    uint32_t n = 8u * FS / CTL * CTL, s, i, idle_ok = 1, long_ok = 1;
    int32_t *in = malloc(4u * NLONG), *out = malloc(8u * NLONG), *tmp = malloc(8u * NLONG);
    double rc[sizeof CURVE], rs[NSET], ring_db[NSET] = {-1, -1, -1, -1, -1};
    FILE *tsv = NULL;
    if (argc < 3) {
        fprintf(stderr, "usage: %s LABEL WAVDIR [TSV]\n", argv[0]);
        return 2;
    }
    if (argc > 3 && !(tsv = fopen(argv[3], "a")))
        return 1;
    host_tracks_init();
    printf("%-14s %-7s %6s %6s %7s %6s %9s %7s %6s %5s %7s %6s %6s %7s %7s %7s %7s\n", "tank", "setting", "rt60",
           "rt4k", "mix_ms", "ned", "ripple_db", "peak_db", "env_db", "iacc", "lvl_db", "ipc", "idle_s", "late_db",
           "late_pk", "ring_db", "ring_pk");
    for (s = 0; s < NSET; s++) {
        double r60, r4k, idle_s, mix_ms = -1, nedm = 0, rip, pk, env, ic, lvl, ipc, lrip = -1, lpk = -1, rdb = -1, rpk = -1;
        uint32_t w = FS / 50;
        uint64_t i0;
        /* burst: RT60 and the ring-out to exactly 0 */
        rs[s] = r60 = burst(SET[s].size, SET[s].damp, in, out, tmp, &r4k, &idle_s);
        idle_ok &= idle_s > 0 || SET[s].size >= 127;    /* (the near-freeze: within 15 min, the stability run) */
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
        if (r60 >= 3) {                                  /* the late tail (1.5 .. 5.5 s), the impulse 8.5 dB up */
            in[0] = 80000;
            rev_reset();
            bus(in, out, n);
            ripple(out, FS * 3 / 2, FS * 11 / 2, &lrip, &lpk);
            ringing(SET[s].size, SET[s].damp, r60 > 0 ? r60 : 60, in, out, &rdb, &rpk);   /* (127: past 64 s) */
            ring_db[s] = rdb;
        }
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
        printf("%-14s %-7s %6.2f %6.2f %7.1f %6.2f %9.2f %7.1f %6.2f %5.2f %7.1f %6.1f %6.1f %7.2f %7.1f %7.2f %7.1f\n",
               argv[1], SET[s].name, r60, r4k, mix_ms, nedm, rip, pk, env, ic, lvl, ipc, idle_s, lrip, lpk, rdb, rpk);
        if (tsv)
            fprintf(tsv, "%s\t%s\t%.2f\t%.2f\t%.1f\t%.2f\t%.2f\t%.1f\t%.2f\t%.2f\t%.1f\t%.1f\t%.1f\t%.2f\t%.1f\t%.2f\t%.1f\n",
                    argv[1], SET[s].name, r60, r4k, mix_ms, nedm, rip, pk, env, ic, lvl, ipc, idle_s, lrip, lpk, rdb, rpk);
        if (SET[s].render)
            render_sounds(argv[2], argv[1], s);
    }
    printf("%-14s curve (DAMP 30): SIZE / RT60 s / RT60 at 4 kHz s:", argv[1]);
    for (i = 0; i < sizeof CURVE; i++) {
        double r4k;
        rc[i] = burst(CURVE[i], 30, in, out, tmp, &r4k, NULL);
        printf(" %u %.2f %.2f%s", CURVE[i], rc[i], r4k, i + 1 < sizeof CURVE ? "," : "\n");
        if (tsv)
            fprintf(tsv, "%s\tcurve-%u\t%.2f\t%.2f\n", argv[1], CURVE[i], rc[i], r4k);
    }
    long_ok &= stability(argv[1], 120, 0, tsv);
    long_ok &= stability(argv[1], 127, 0, tsv);
#if FELUCCA_REVERB == 2
    for (i = 1; i < sizeof CURVE; i++)                   /* (127: past the 64 s, -1 if the fit had too little) */
        if (rc[i] >= 0 && rc[i] < rc[i - 1] * 0.98) {
            printf("%s: the curve falls at SIZE %u\n", argv[1], CURVE[i]);
            long_ok = 0;
        }
    if (rc[10] < 8 || rc[10] > 20 || (rc[11] >= 0 && rc[11] < 30)) {
        printf("%s: RT60 %.1f s at SIZE 126 (8 .. 20), %.1f s at 127 (30 or more)\n", argv[1], rc[10], rc[11]);
        long_ok = 0;
    }
    if (fabs(rs[0] / 0.62 - 1) > 0.12 || fabs(rs[1] / 1.23 - 1) > 0.12) {
        printf("%s: small %.2f s, medium %.2f s: not the ROOM's 0.62 / 1.23 s within 12 %%\n", argv[1], rs[0], rs[1]);
        long_ok = 0;
    }
    {   /* the ringing at long / huge, per ring: 32 KB (REV_POOL), 16 KB, 8 KB (REV_HALF without the pool) */
        const double lim_l = RV_N >= 16384u ? 1.5 : RV_N >= 8192u ? 2.0 : 2.6;
        const double lim_h = RV_N >= 16384u ? 1.05 : RV_N >= 8192u ? 1.4 : 2.1;
        if (ring_db[2] < 0 || ring_db[2] > lim_l || ring_db[4] < 0 || ring_db[4] > lim_h) {
            printf("%s: rings, ring_db %.2f at long, %.2f at huge (%.2f / %.2f at most, a %u-sample ring)\n", argv[1],
                   ring_db[2], ring_db[4], lim_l, lim_h, RV_N);
            long_ok = 0;
        }
    }
#endif
    printf("%s: ring-out to exactly 0 and idle at every setting: %s; the long top and stability: %s\n", argv[1],
           idle_ok ? "ok" : "FAIL", long_ok ? "ok" : "FAIL");
    if (tsv)
        fclose(tsv);
    free(in);
    free(out);
    free(tmp);
    return !(idle_ok && long_ok);
}
