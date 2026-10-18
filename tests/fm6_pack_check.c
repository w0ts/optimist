/* SPDX-License-Identifier: GPL-3.0-only */
/* The FM6 voices measured: INIT VOICE and every voice of the packs this build has (FELUCCA_FM6_PK_*), through the host
 * renderer (the same engine code as the firmware, bit for bit with the Dexed parity test). For tools/fm6_packs_check.py,
 * which sets each voice's level (the trim of tools/fm6_packs/levels.json) and reads the rest as a listening check by numbers.
 *   fm6_pack_check TSV [WAV]
 * One line a voice: index, name, then
 *   single notes C2 C3 C4 C5 C6, velocity 100, held 1.5 s, 4 s in all, the track alone (no FX, no master):
 *     peak (dBFS, 16-bit full scale), DC (the mean of the last second after the release, dB under the peak: -999 none),
 *     clipped (samples at +-32767 or past), t40 (s from the start to the last time the level is within 40 dB of its loudest,
 *     RMS in 20 ms windows; 4 = still sounding at the end), rel60 (s from the key-up to 60 dB under the loudest; 4 = never)
 *   C4's spectrum: f0 (Hz, by autocorrelation in the first 0.5 s; 0 none), cent (the spectral centroid in Hz over the same
 *     window), hi (dB of the energy above 10 kHz under the total: how much there is to alias), wob (the biggest rise in dB
 *     of the 100 ms level after a lower one, 0.3 s .. 1.4 s: the beating of partners tuned apart)
 *   the phrase of each role (bass, held chords, comping, melody: tests/preset_level.c's) through the whole mix as a
 *     factory preset plays it, ITU-R BS.1770 integrated loudness in LUFS (tools/level_presets.py aims at -15) and the
 *     loudest sample of the stereo mix in dBFS, a pair a role
 * WAV (optional): the C4 notes of all the voices one after the other, 16-bit stereo, to listen to. */
#define main hostsim_main
#include "hostsim.c"
#undef main

enum { R_BASS, R_HELD, R_COMP, R_MELODY, R_N };
typedef struct { uint8_t step, note, len; } ev_t;
static const ev_t BASS[] = {{0, 36, 3}, {4, 36, 1}, {6, 43, 2}, {8, 41, 3}, {12, 39, 2}, {14, 38, 2},
                            {16, 36, 3}, {20, 36, 1}, {22, 43, 2}, {24, 46, 3}, {28, 43, 4}};
static const ev_t MELODY[] = {{0, 72, 2}, {2, 75, 2}, {4, 77, 3}, {8, 79, 2}, {10, 77, 2}, {12, 75, 4},
                              {16, 72, 2}, {18, 70, 2}, {20, 72, 3}, {24, 67, 4}, {28, 70, 2}, {30, 72, 2}};
static const uint8_t CHORD[2][4] = {{60, 63, 67, 70}, {58, 62, 65, 68}};
static const uint8_t COMP[] = {0, 3, 6, 10, 12, 16, 19, 22, 26, 28};

static uint8_t held[128];
static uint32_t off_at[128];
static void play(uint32_t note, uint32_t vel, uint32_t off)
{
    trk_note_on(&trk[0], note, vel);
    held[note] = 1;
    off_at[note] = off;
}

static uint32_t ref_e = ~0u, ref_pi;                      /* --ref: another engine's preset instead of an FM6 voice */
static void fresh(uint32_t voice)
{
    host_tracks_init();
    song.g[G_BPM] = 100;
    if (ref_e != ~0u) {
        host_preset(&trk[0], ref_e, ref_pi);
        song.sel = 0;
        memset(held, 0, sizeof held);
        return;
    }
    host_preset(&trk[0], ENG_IX_FM6, 0);                 /* FM6's first sound: the ADSR open, the sends at their defaults */
    trk[0].p[P_E0] = (int16_t)voice;                     /* VOICE: what nb_load / the knob do */
    fm6_cur[0] = 0;
    song.sel = 0;
    memset(held, 0, sizeof held);
}

/* BS.1770 integrated loudness of a mono signal (tools/level_presets.py's formula) */
static double biquad(double *z, const double *b, const double *a, double x)
{
    double y = b[0] * x + z[0];
    z[0] = b[1] * x - a[1] * y + z[1];
    z[1] = b[2] * x - a[2] * y;
    return y;
}
static double lufs(const double *x, uint32_t n)
{
    static const double b1[3] = {1.53512485958697, -2.69169618940638, 1.19839281085285}, a1[3] = {1, -1.69065929318241, 0.73248077421585};
    static const double b2[3] = {1, -2, 1}, a2[3] = {1, -1.99004745483398, 0.99007225036621};
    double z1[2] = {0, 0}, z2[2] = {0, 0}, *k = malloc(n * sizeof *k), blk[512], g[512], s, mean, rel;
    uint32_t i, w = (uint32_t)(0.4 * FS), h = (uint32_t)(0.1 * FS), nb = 0, ng = 0, j;
    for (i = 0; i < n; i++) {
        double y = biquad(z2, b2, a2, biquad(z1, b1, a1, x[i]));
        k[i] = y * y;
    }
    for (i = 0; i + w < n && nb < 512u; i += h) {
        for (s = 0, j = 0; j < w; j++)
            s += k[i + j];
        s /= w;
        if (s > 1e-7)
            blk[nb++] = s;
    }
    free(k);
    for (i = 0, s = 0, mean = 0; i < nb; i++)
        if (10 * log10(blk[i]) - 0.691 > -70) {
            g[ng++] = blk[i];
            mean += blk[i];
        }
    if (!ng)
        return -999;
    rel = 10 * log10(mean / ng) - 0.691 - 10;
    for (i = 0, ng = 0, mean = 0; i < nb; i++)
        if (10 * log10(blk[i]) - 0.691 > -70 && 10 * log10(blk[i]) - 0.691 > rel) {
            ng++;
            mean += blk[i];
        }
    return ng ? 10 * log10(mean / ng) - 0.691 : -999;
}

static double ph_peak;                                   /* the loudest sample of the last phrase, dBFS */
static double phrase(uint32_t voice, int role)
{
    const uint32_t step = FS * 60u / 100u / 4u, nb = (FS * 9u / 2u) / CTL, tail = (FS / 4u) / CTL;
    static double sig[(FS * 9u / 2u) + FS / 4u + 4u * CTL];
    int32_t o[2 * CTL];
    uint32_t b, n, i, st, frames = 0;
    double pk = 0;
    fresh(voice);
    for (b = 0; b < FS / CTL; b++)
        mix_block(o, CTL);
    for (b = 0; b < nb + tail; b++) {
        uint32_t s0 = b * CTL, s1 = s0 + CTL;
        for (n = 0; n < 128u; n++)
            if (held[n] && (off_at[n] < s1 || b >= nb)) {
                trk_note_off(&trk[0], n);
                held[n] = 0;
            }
        for (st = 0; st < 32u && b < nb; st++) {
            uint32_t at = st * step;
            if (at < s0 || at >= s1)
                continue;
            if (role == R_BASS || role == R_MELODY) {
                const ev_t *ev = role == R_BASS ? BASS : MELODY;
                uint32_t ne = role == R_BASS ? sizeof BASS / sizeof BASS[0] : sizeof MELODY / sizeof MELODY[0];
                for (i = 0; i < ne; i++)
                    if (ev[i].step == st)
                        play(ev[i].note, 100, at + ev[i].len * step - step / 4u);
            } else if (role == R_HELD) {
                if (st == 0 || st == 16)
                    for (i = 0; i < 4u; i++)
                        play(CHORD[st / 16u][i], 90, at + 15u * step);
            } else {
                for (n = 0; n < sizeof COMP; n++)
                    if (COMP[n] == st)
                        for (i = 0; i < 4u; i++)
                            play(CHORD[st / 16u][i], 95, at + 2u * step - step / 3u);
            }
        }
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++) {
            double l = (double)o[2 * i] / 32768.0, r = (double)o[2 * i + 1] / 32768.0;
            sig[frames++] = (l + r) / 2;
            pk = fmax(pk, fmax(fabs(l), fabs(r)));
        }
    }
    ph_peak = 20 * log10(pk > 1e-9 ? pk : 1e-9);
    return lufs(sig, frames);
}

#define NS (FS * 4u)
static int32_t wave[NS];
static void note(uint32_t voice, uint32_t key)       /* the track alone: key held 1.5 s, 4 s in all */
{
    int32_t b[CTL];
    uint32_t i, k;
    fresh(voice);
    trk_note_on(&trk[0], key, 100);
    for (i = 0; i < NS; i += CTL) {
        if (i == FS * 3u / 2u / CTL * CTL)
            trk_note_off(&trk[0], key);
        track_render(&trk[0], b, CTL);
        for (k = 0; k < CTL && i + k < NS; k++)
            wave[i + k] = b[k];
    }
}
static double win_peak(uint32_t a, uint32_t n)
{
    double p = 0;
    uint32_t i;
    for (i = a; i < a + n && i < NS; i++)
        if (fabs((double)wave[i]) > p)
            p = fabs((double)wave[i]);
    return p;
}
static double win_rms(uint32_t a, uint32_t n)            /* the RMS of n samples from a, as the amplitude of the sine it equals */
{
    double q = 0;
    uint32_t i, m = 0;
    for (i = a; i < a + n && i < NS; i++, m++)
        q += (double)wave[i] * wave[i];
    return m ? sqrt(2.0 * q / m) : 0.0;
}
static double dbf(double x) { return 20 * log10(x > 1e-9 ? x : 1e-9); }

static void measure(uint32_t voice, uint32_t key, FILE *tsv)
{
    uint32_t i, clip = 0, w = FS / 50u, last40 = 0, first60 = 0, rel0 = FS * 3u / 2u, pk_at = 0;
    double pk = 0, dc = 0, dcn = 0, rms_tail = 0, rpk = 0;
    note(voice, key);
    for (i = 0; i < NS; i++) {
        double a = fabs((double)wave[i]);
        if (a > pk) {
            pk = a;
            pk_at = i;
        }
        clip += a >= 32767.0;
    }
    for (i = NS - FS; i < NS; i++) {
        dc += wave[i];
        dcn++;
        rms_tail += (double)wave[i] * wave[i];
    }
    (void)pk_at;
    for (i = 0; i + w <= NS; i += w)                     /* the loudest 20 ms (RMS: a sum of detuned sines beats) */
        rpk = win_rms(i, w) > rpk ? win_rms(i, w) : rpk;
    for (i = 0; i + w <= NS; i += w)                     /* the last window within 40 dB of it, from the attack */
        if (win_rms(i, w) > rpk / 100.0)
            last40 = i + w;
    for (i = rel0; i + w <= NS; i += w)                  /* 60 dB under it after the key-up */
        if (win_rms(i, w) < rpk / 1000.0) {
            first60 = i;
            break;
        }
    fprintf(tsv, "\t%.1f\t%.0f\t%u\t%.2f\t%.2f", dbf(pk / 32768.0),
            pk > 0 && fabs(dc / dcn) > 0.5 ? dbf(fabs(dc / dcn) / pk) : -999.0, clip,
            last40 >= NS - w ? 4.0 : (double)last40 / FS,
            first60 ? (double)(first60 - rel0) / FS : 4.0);
}

static void spectrum(uint32_t voice, FILE *tsv)        /* C4: f0, centroid, energy above 10 kHz */
{
    uint32_t i, lag, best = 0, n = FS / 2u, a0 = FS / 20u, bin;
    double c, bc = 0, e_all = 0, e_hi = 0, num = 0, den = 0, wob;
    static double re[1024], im[1024];
    note(voice, 60);
    for (lag = 100; lag < 1200u; lag++) {              /* 36 Hz .. 441 Hz.. C4 is 261: lags 36..441 samples */
        c = 0;
        for (i = a0; i + lag < a0 + n / 2u; i++)
            c += (double)wave[i] * wave[i + lag];
        if (c > bc) {
            bc = c;
            best = lag;
        }
    }
    {
        double r0 = 0;
        for (i = a0; i < a0 + n / 2u; i++)
            r0 += (double)wave[i] * wave[i];
        if (r0 <= 0 || bc < 0.3 * r0)
            best = 0;
    }
    for (bin = 0; bin < 512u; bin++) {                 /* a Hann-windowed DFT of 1024 samples at 1/20 s: 43 Hz a bin */
        double fr = 0, fi = 0;
        for (i = 0; i < 1024u; i++) {
            double h = 0.5 - 0.5 * cos(6.283185307179586 * i / 1023.0), ph = 6.283185307179586 * bin * i / 1024.0;
            fr += wave[a0 + i] * h * cos(ph);
            fi -= wave[a0 + i] * h * sin(ph);
        }
        re[bin] = fr;
        im[bin] = fi;
    }
    for (bin = 1; bin < 512u; bin++) {
        double p = re[bin] * re[bin] + im[bin] * im[bin], f = bin * (double)FS / 1024.0;
        e_all += p;
        num += f * p;
        den += p;
        if (f > 10000.0)
            e_hi += p;
    }
    {   /* wob: the biggest rise of the level after a lower one while the key is held, 0.3 s .. 1.4 s (RMS in 100 ms windows, dB):
         * beating between detuned partners shows here, a plain decay or a swell that is meant does not (the swell is before 0.3 s) */
        double lo = 1e9;
        uint32_t w0;
        wob = 0;
        for (w0 = FS * 3u / 10u; w0 + FS / 10u <= FS * 7u / 5u; w0 += FS / 20u) {
            double r = dbf(win_rms(w0, FS / 10u) / 32768.0);
            lo = r < lo ? r : lo;
            wob = r - lo > wob ? r - lo : wob;
        }
    }
    fprintf(tsv, "\t%.1f\t%.0f\t%.1f\t%.1f", best ? (double)FS / best : 0.0, den > 0 ? num / den : 0.0,
            e_all > 0 && e_hi > 0 ? 10 * log10(e_hi / e_all) : -999.0, wob);
}

/* a radix-2 FFT of n (a power of 2) complex values */
static void fft(double *re, double *im, uint32_t n)
{
    uint32_t i, j = 0, k, m;
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
    for (m = 2; m <= n; m <<= 1) {
        double wr = cos(-6.283185307179586 / m), wi = sin(-6.283185307179586 / m);
        for (k = 0; k < n; k += m) {
            double cr = 1, ci = 0;
            for (i = 0; i < m / 2u; i++) {
                double ur = re[k + i], ui = im[k + i], vr = re[k + i + m / 2u] * cr - im[k + i + m / 2u] * ci,
                       vi = re[k + i + m / 2u] * ci + im[k + i + m / 2u] * cr, t;
                re[k + i] = ur + vr; im[k + i] = ui + vi;
                re[k + i + m / 2u] = ur - vr; im[k + i + m / 2u] = ui - vi;
                t = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = t;
            }
        }
    }
}

/* --dump V KEY: the level every 100 ms (dBFS, 50 ms windows), and the strongest partials at 0.05 s, 0.5 s and 1.4 s
 * (a Hann-windowed FFT of 8192 samples: peaks 5.4 Hz apart), as ratios of the key's frequency */
static void dump(uint32_t voice, uint32_t key)
{
    static double re[8192], im[8192];
    static const double AT[3] = {0.05, 0.5, 1.4};
    double f0 = 440.0 * pow(2.0, ((double)key - 69.0) / 12.0);
    uint32_t i, a, t, n;
    note(voice, key);
    printf("voice %u (%s) key %u (%.1f Hz): RMS level, dBFS (as a sine's peak) per 100 ms\n", voice, fm6_rom(voice)->name, key, f0);
    for (a = 0; a < NS; a += FS / 10u)
        printf("%s%.0f", a ? " " : "", dbf(win_rms(a, FS / 10u) / 32768.0));
    printf("\n");
    printf("pitch, Hz per 20 ms from the start (zero crossings; a dip or rise of the pitch envelope shows here):");
    for (a = 0; a < FS / 5u; a += FS / 50u) {
        uint32_t z = 0, last = 0, first = 0;
        for (i = a + 1u; i < a + FS / 50u; i++)
            if (wave[i - 1] < 0 && wave[i] >= 0) {
                if (!z++)
                    first = i;
                last = i;
            }
        printf(" %.0f", z > 1u && last > first ? (z - 1u) * (double)FS / (last - first) : 0.0);
    }
    printf("\n");
    for (t = 0; t < 3u; t++) {
        uint32_t s0 = (uint32_t)(AT[t] * FS), bins[12], nb = 0, b, c;
        double pk = 0;
        for (i = 0; i < 8192u; i++) {
            re[i] = (s0 + i < NS ? wave[s0 + i] : 0) * (0.5 - 0.5 * cos(6.283185307179586 * i / 8191.0));
            im[i] = 0;
        }
        fft(re, im, 8192);
        for (b = 1; b < 4096u; b++) {
            double m = re[b] * re[b] + im[b] * im[b];
            pk = m > pk ? m : pk;
        }
        for (b = 2; b < 4094u && nb < 12u; b++) {      /* local maxima within 40 dB of the strongest, strongest first */
            double m = re[b] * re[b] + im[b] * im[b];
            if (m < pk / 1e4 || m < re[b - 1] * re[b - 1] + im[b - 1] * im[b - 1] || m < re[b + 1] * re[b + 1] + im[b + 1] * im[b + 1])
                continue;
            for (c = nb; c > 0 && m > re[bins[c - 1]] * re[bins[c - 1]] + im[bins[c - 1]] * im[bins[c - 1]]; c--)
                if (c < 12u)
                    bins[c] = bins[c - 1];
            if (c < 12u) {
                bins[c] = b;
                nb += nb < 12u;
            }
        }
        printf("t=%.2fs partials (x f0: dB re strongest):", AT[t]);
        for (c = 0; c < nb; c++) {
            double m = re[bins[c]] * re[bins[c]] + im[bins[c]] * im[bins[c]], f = bins[c] * (double)FS / 8192.0;
            printf("  %.2f:%.0f", f / f0, 10 * log10(m / pk));
        }
        printf("\n");
    }
    (void)n;
}

static int has(const char *n, const char *w) { return strstr(n, w) != 0; }
static int role_of(const char *n)                        /* tests/preset_level.c's */
{
    if (has(n, "BASS") || has(n, "808") || has(n, "ACID") || has(n, "REESE") || has(n, "WOBBLE") || has(n, "SUB") || has(n, "BOOM"))
        return R_BASS;
    if (has(n, "PAD") || has(n, "STR") || has(n, "CHOIR") || has(n, "OOH") || has(n, "ORGAN") || has(n, "ORGN") || has(n, "GOSPEL") ||
        has(n, "B3") || has(n, "CLOUD") || has(n, "HAZE") || has(n, "ATMOS"))
        return R_HELD;
    if (has(n, "RHODES") || has(n, "WURLI") || has(n, "CLAV") || has(n, "KEYS") || has(n, "STAB") || has(n, "PNO") || has(n, "PIANO") ||
        has(n, "VIBES") || has(n, "BRASS") || has(n, "HORN") || has(n, "CHRD"))
        return R_COMP;
    return R_MELODY;
}

int main(int argc, char **argv)
{
    if (argc == 3 && !strcmp(argv[1], "--ref")) {         /* the factory presets of engine E, measured the same way */
        uint32_t e = (uint32_t)atoi(argv[2]) % NENGINES, pi;
        for (pi = 0; pi < ENGINES[e]->npresets; pi++) {
            int role = role_of(ENGINES[e]->presets[pi].name);
            double pk3, pk2;
            ref_e = e;
            ref_pi = pi;
            note(0, 48);
            pk2 = win_peak(0, NS);
            note(0, 60);
            pk3 = win_peak(0, NS);
            printf("%s\t%d\tC3 %.1f\tC4 %.1f\tphrase %.1f LUFS\tpeak %.1f\n", ENGINES[e]->presets[pi].name, role, dbf(pk2 / 32768.0),
                   dbf(pk3 / 32768.0), phrase(0, role), ph_peak);
        }
        return 0;
    }
    if (argc == 4 && !strcmp(argv[1], "--dump")) {
        dump((uint32_t)atoi(argv[2]) % FM6_NVOICE, (uint32_t)atoi(argv[3]));
        return 0;
    }
    FILE *tsv = fopen(argc > 1 ? argv[1] : "fm6_pack_check.tsv", "w"), *wf = argc > 2 ? fopen(argv[2], "wb") : 0;
    static const uint32_t KEYS[5] = {36, 48, 60, 72, 84};
    uint32_t v, k, r, frames = 0;
    if (!tsv)
        return 1;
    if (wf)
        wav_hdr(wf, 0);
    for (v = 0; v < FM6_NVOICE; v++) {
        fprintf(tsv, "%u\t%.10s", v, fm6_rom(v)->name);
        for (k = 0; k < 5u; k++)
            measure(v, KEYS[k], tsv);
        spectrum(v, tsv);
        for (r = 0; r < R_N; r++)
        {
            double l = phrase(v, (int)r);
            fprintf(tsv, "\t%.1f\t%.1f", l, ph_peak);
        }
        fprintf(tsv, "\n");
        if (wf) {
            note(v, 60);
            for (k = 0; k < NS; k++, frames++)
                wav_put(wf, wave[k], wave[k]);
        }
    }
    fclose(tsv);
    if (wf) {
        fseek(wf, 0, SEEK_SET);
        wav_hdr(wf, frames);
        fclose(wf);
    }
    return 0;
}
