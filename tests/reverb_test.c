/* SPDX-License-Identifier: GPL-3.0-only */
/* The ROOM reverb (fx.c rev_step) through fx_buses: decay time, level, the top octave, the idle skip.
 *   build/host/reverb_test REF [WAVDIR]        full rate: checks, writes its numbers to REF
 *   build/host/reverb_test_half REF [WAVDIR]   FELUCCA_REV_HALF=1 (the tank at 22.05 kHz): checks, and
 *                                              compares with REF (written by the full-rate run first)
 * Numbers: RT60 (T30 of the Schroeder curve) of a noise burst at five SIZE / DAMP settings, the wet RMS of a
 * low-passed noise (the band both rates keep) and of white noise, the wet energy below 8 kHz, 8..11 kHz and
 * above 11 kHz; per sound (a drum hit, a pad chord, a pluck: their send as the mix makes it) the same bands
 * and the decay of the tail. Half against full: RT60 within 5 %, the low-passed noise level within 1 dB, the
 * sounds' decay within 5 %, the energy below 8 kHz within 1 dB. WAVDIR: <full|half>-<sound>-mix.wav (the whole
 * mix) and -wet.wav (the reverb alone), for listening. */
#define main hostsim_main
#include "hostsim.c"
#undef main

#define NSET 5
static const uint8_t SET[NSET][2] = {{0, 60}, {90, 60}, {127, 60}, {90, 0}, {90, 127}};   /* SIZE, DAMP */
#define NSND 3
static const char *const SND[NSND] = {"drum", "pad", "pluck"};
#define NKEY (NSET + 5 + NSND * 4)
static const char *KEY[NKEY];
static double val[NKEY];
static char keybuf[NKEY][32];
static uint32_t fails;

static void check(int ok, const char *what)
{
    printf("reverb: %-84s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

static void rev_clear(void)                     /* every line, diffuser and filter to 0, the bus idle */
{
    memset(rev_line, 0, sizeof rev_line);
    memset(rev_ap, 0, sizeof rev_ap);
    memset(fx.line_lp, 0, sizeof fx.line_lp);
    fx.rev_q = FX_Q_MAX;
#if FELUCCA_REV_HALF
    memset(&rev_half, 0, sizeof rev_half);
#endif
}

static int rev_idle(void)                       /* fx_buses would skip the bus on a silent block */
{
    uint32_t i;
    int32_t any = fx.line_lp[0] | fx.line_lp[1] | fx.line_lp[2] | fx.line_lp[3] | REV_HALF_BUSY();
    for (i = 0; i < sizeof rev_line / 2u; i++)
        any |= rev_line[i];
    for (i = 0; i < sizeof rev_ap / 2u; i++)
        any |= rev_ap[i];
    return fx.rev_q >= REV_Q && !any;
}

/* the bus alone: n samples of in (0: silence) -> out (interleaved L, R; NULL: dropped) */
static void bus(const int32_t *in, int32_t *out, uint32_t n)
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

/* RT60 from the Schroeder curve of x (stereo, from frame t0): T30 (-5 .. -35 dB) x 2 */
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

/* in-place radix-2 FFT (re, im; n a power of two) */
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

/* the energy of x (stereo, frames a..b) below 8 kHz, 8..11 kHz, above 11.025 kHz (Hann windows of 4096, half
 * overlapped), in dB */
#define NFFT 4096u
static void bands(const int32_t *x, uint32_t a, uint32_t b, double *db)
{
    static double re[NFFT], im[NFFT];
    double e[3] = {0, 0, 0};
    uint32_t s, i, ch;
    for (s = a; s + NFFT <= b; s += NFFT / 2)
        for (ch = 0; ch < 2u; ch++) {
            for (i = 0; i < NFFT; i++)
                re[i] = x[2 * (s + i) + ch] * (0.5 - 0.5 * cos(2 * M_PI * i / NFFT)), im[i] = 0;
            fft(re, im, NFFT);
            for (i = 1; i < NFFT / 2; i++) {
                double f = (double)i * FS / NFFT, p = re[i] * re[i] + im[i] * im[i];
                e[f < 8000 ? 0 : f < FS / 4.0 ? 1 : 2] += p;
            }
        }
    for (i = 0; i < 3u; i++)
        db[i] = 10 * log10(e[i] + 1e-9);
}

static double rms(const int32_t *x, uint32_t a, uint32_t b)
{
    double acc = 0;
    uint32_t i;
    for (i = 2 * a; i < 2 * b; i++)
        acc += (double)x[i] * x[i];
    return sqrt(acc / (2.0 * (b - a)));
}

static void put(uint32_t k, const char *name, double v)
{
    snprintf(keybuf[k], sizeof keybuf[k], "%s", name);
    KEY[k] = keybuf[k];
    val[k] = v;
}

/* a noise burst (20 ms, two poles at ~4.5 kHz: the band both rates keep; a level a synth send reaches) */
#define BURST (FS / 50u)
static void burst(int32_t *in, uint32_t n)
{
    uint32_t i, seed = 777;
    double lp1 = 0, lp2 = 0;
    for (i = 0; i < n; i++) {
        double x;
        seed = seed * 1664525u + 1013904223u;
        x = i < BURST ? ((int32_t)(seed >> 16) - 32768) / 2.0 : 0;
        lp1 += 0.45 * (x - lp1);
        lp2 += 0.45 * (lp1 - lp2);
        in[i] = (int32_t)(2.0 * lp2);
    }
}

/* 1. the burst at each setting: RT60 (from the burst's end); the tail rings out to exactly 0, the bus goes idle.
 * (An impulse would put half its energy above 11 kHz, where only the full rate has a tank; the loops' rounding
 * toward 0 then shortens the half rate's quieter tail: not what a send sounds like.) */
static void t_decay(uint32_t *k)
{
    uint32_t n = 14u * FS / CTL * CTL, s, f, idle_ok = 1;
    int32_t *in = malloc(4u * n), *out = malloc(8u * n);
    char nm[32];
    burst(in, n);
    for (s = 0; s < NSET; s++) {
        uint32_t idle_at = 0;
        song.g[G_RSIZE] = SET[s][0];
        song.g[G_RDAMP] = SET[s][1];
        rev_clear();
        for (f = 0; f < n; f += CTL) {
            bus(in + f, out + 2u * f, CTL);
            if (!idle_at && f > BURST && rev_idle())   /* (to the last LSB: the idle skip's condition) */
                idle_at = f + CTL;
        }
        idle_ok &= idle_at != 0;
        snprintf(nm, sizeof nm, "rt60_s%u_d%u", SET[s][0], SET[s][1]);
        put((*k)++, nm, rt60(out, BURST, n));
        printf("reverb: SIZE %3u DAMP %3u: RT60 %.2f s, idle %.1f s after the burst\n", SET[s][0], SET[s][1],
               val[*k - 1], idle_at / (double)FS);
    }
    check(idle_ok, "a burst rings out to exactly 0 and the bus goes idle within 14 s, at every setting");
    free(in);
    free(out);
}

/* 2. noise into the send (SIZE 90, DAMP 60): the wet level (low-passed noise, white noise) and the bands */
static void t_noise(uint32_t *k)
{
    uint32_t n = 3u * FS / CTL * CTL, i, seed = 12345, w;
    int32_t *in = malloc(4u * n), *out = malloc(8u * n), peak = 0;
    double lp1 = 0, lp2 = 0, db[3];
    song.g[G_RSIZE] = 90;
    song.g[G_RDAMP] = 60;
    for (w = 0; w < 2u; w++) {
        for (i = 0; i < n; i++) {
            double x;
            seed = seed * 1664525u + 1013904223u;
            x = ((int32_t)(seed >> 16) - 32768) / 3.0;
            lp1 += 0.45 * (x - lp1);                    /* (two poles at ~4.5 kHz) */
            lp2 += 0.45 * (lp1 - lp2);
            in[i] = (int32_t)(w ? x : 2.0 * lp2);
        }
        rev_clear();
        bus(in, out, n);
        for (i = 0; i < 2u * n; i++)
            peak = abs(out[i]) > peak ? abs(out[i]) : peak;
        put((*k)++, w ? "rms_white" : "rms_lowpassed", 20 * log10(rms(out, FS, n)));
        if (w) {
            bands(out, FS, n, db);
            put((*k)++, "white_lo8k", db[0]);
            put((*k)++, "white_8k_11k", db[1]);
            put((*k)++, "white_hi11k", db[2]);
            printf("reverb: white noise: wet bands <8k %.1f dB, 8..11k %.1f dB, >11k %.1f dB\n", db[0], db[1], db[2]);
        }
    }
    printf("reverb: noise: wet RMS low-passed %.1f dB, white %.1f dB; peak %d\n", val[*k - 5], val[*k - 4], peak);
    check(peak < 32768 * 4, "noise into the send: the wet output bounded");
    free(in);
    free(out);
}

/* 3. a sound through the mix, its reverb send 100 (no chorus, no delay): the wet's bands and tail */
static void t_sound(uint32_t *k, uint32_t which, const char *wavdir)
{
    uint32_t n = 5u * FS / CTL * CTL, f, i, last = 0;
    int32_t *wet = malloc(8u * n), *mix = malloc(8u * n), o[2 * CTL];
    track_t *t = &trk[0];
    double db[3], pk_send = 0;
    char path[512], nm[32];
    host_tracks_init();
    rev_clear();
    if (which == 0) {
        host_drum_rev(100);
    } else {
        host_preset(t, 0, which == 1 ? 17u : 10u);      /* ANALOG SUPER PAD, TRAP PLUCK */
        t->p[P_AMODE] = 0;
        t->p[P_CHOR] = t->p[P_DLY] = 0;
        t->p[P_REV] = 100;
    }
    for (f = 0; f < n; f += CTL) {
        static const uint8_t CH[4] = {48, 55, 59, 64};
        if (f == CTL) {
            if (which == 0) {
                input_on(TDRUM, 36, 120);
                input_on(TDRUM, 38, 110);
            } else
                for (i = 0; i < (which == 1 ? 4u : 1u); i++)
                    input_on(t, which == 1 ? CH[i] : 60u, 100);
        }
        if (f == (which == 1 ? FS * 3 / 2 : FS * 3 / 10) / CTL * CTL)
            for (i = 0; i < 4u; i++) {
                if (which == 0)
                    input_off(TDRUM, 36 + 2 * (i & 1));
                else
                    input_off(t, which == 1 ? CH[i] : 60u);
            }
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++) {
            double s = fabs((double)send_r[i]);
            mix[2 * (f + i)] = o[2 * i], mix[2 * (f + i) + 1] = o[2 * i + 1];
            wet[2 * (f + i)] = wet_l[i], wet[2 * (f + i) + 1] = wet_r[i];
            if (s > pk_send)
                pk_send = s;
            if (s > pk_send / 1000)                     /* (-60 dB: the send has ended after this) */
                last = f + i;
        }
    }
    bands(wet, 0, n, db);
    snprintf(nm, sizeof nm, "%s_rt60", SND[which]);
    put((*k)++, nm, rt60(wet, last, n));
    snprintf(nm, sizeof nm, "%s_lo8k", SND[which]);
    put((*k)++, nm, db[0]);
    snprintf(nm, sizeof nm, "%s_8k_11k", SND[which]);
    put((*k)++, nm, db[1]);
    snprintf(nm, sizeof nm, "%s_hi11k", SND[which]);
    put((*k)++, nm, db[2]);
    printf("reverb: %-5s: wet <8k %.1f dB, 8..11k %.1f dB, >11k %.1f dB; tail RT60 %.2f s (from %.2f s)\n",
           SND[which], db[0], db[1], db[2], val[*k - 4], last / (double)FS);
    if (wavdir) {
        uint32_t m;
        for (m = 0; m < 2u; m++) {
            FILE *fo;
            snprintf(path, sizeof path, "%s/%s-%s-%s.wav", wavdir, FELUCCA_REV_HALF ? "half" : "full", SND[which],
                     m ? "wet" : "mix");
            if (!(fo = fopen(path, "wb"))) {
                printf("reverb: cannot write %s\n", path);
                fails++;
                continue;
            }
            wav_hdr(fo, n);
            for (i = 0; i < n; i++)
                wav_put(fo, (m ? wet : mix)[2 * i], (m ? wet : mix)[2 * i + 1]);
            fclose(fo);
        }
    }
    free(wet);
    free(mix);
}

static int load_ref(const char *path, double *ref)
{
    FILE *f = fopen(path, "r");
    char name[64];
    double v;
    uint32_t i, got = 0;
    if (!f)
        return 0;
    while (fscanf(f, "%63s %lf", name, &v) == 2)
        for (i = 0; i < NKEY; i++)
            if (KEY[i] && !strcmp(KEY[i], name))
                ref[i] = v, got++;
    fclose(f);
    return got == NKEY;
}

static void compare(const double *ref)
{
    uint32_t i, ok_rt = 1, ok_snd = 1, ok_lo = 1;
    for (i = 0; i < NKEY; i++) {
        int rt = strstr(KEY[i], "rt60") != 0, lo = strstr(KEY[i], "lo8k") != 0;
        double d = rt ? (ref[i] > 0 ? 100 * (val[i] / ref[i] - 1) : 1e9) : val[i] - ref[i];
        printf("reverb: half vs full  %-16s %9.2f  %9.2f  %+7.2f %s\n", KEY[i], ref[i], val[i], d, rt ? "%" : "dB");
        if (rt && i < NSET)
            ok_rt &= fabs(d) <= 5;
        else if (rt)
            ok_snd &= fabs(d) <= 5;
        else if (lo || !strcmp(KEY[i], "rms_lowpassed"))
            ok_lo &= fabs(d) <= 1;
    }
    check(ok_rt, "half rate: the burst's RT60 within 5 % of the full rate's, every SIZE / DAMP");
    check(ok_snd, "half rate: each sound's tail decays within 5 % of the full rate's");
    check(ok_lo, "half rate: the wet level below 8 kHz within 1 dB (low-passed noise, every sound)");
}

int main(int argc, char **argv)
{
    uint32_t k = 0, which;
    static double ref[NKEY];
    if (argc < 2) {
        fprintf(stderr, "usage: %s REF [WAVDIR]\n", argv[0]);
        return 2;
    }
    host_tracks_init();
    t_decay(&k);
    t_noise(&k);
    for (which = 0; which < NSND; which++)
        t_sound(&k, which, argc > 2 ? argv[2] : 0);
    if (FELUCCA_REV_HALF) {
        if (!load_ref(argv[1], ref)) {
            printf("reverb: no full-rate numbers in %s (run build/host/reverb_test %s first)\n", argv[1], argv[1]);
            return 1;
        }
        compare(ref);
    } else {
        FILE *f = fopen(argv[1], "w");
        uint32_t i;
        if (!f) {
            printf("reverb: cannot write %s\n", argv[1]);
            return 1;
        }
        for (i = 0; i < NKEY; i++)
            fprintf(f, "%s %.6f\n", KEY[i], val[i]);
        fclose(f);
    }
    printf("reverb: %u failed\n", fails);
    return fails != 0;
}
