/* SPDX-License-Identifier: GPL-3.0-only */
/* Synthesised drum kits (drum_synth.c): every kit x every sound is bounded, audible and
 * ends; the level of each kit stays near the sampled kit; the host cost of 6 synth voices.
 * argv[1]: a WAV demo (each kit plays one bar), argv[2]: a per-kit report. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <assert.h>
static const uint8_t DS_NOTE[DS_LANES] = {36, 38, 39, 42, 46, 43, 48, 49, 51, 70, 63, 37, 56, 75, 35, 40};   /* one GM note per synth lane */
static uint32_t one_hit(uint32_t kit, uint32_t note, int32_t *peak, uint64_t *energy)
{
    uint32_t j, k, blocks = 0;
    int32_t l[CTL], r[CTL], rv[CTL];
    memset(&drums, 0, sizeof drums);
    drums_set = -2;
    TDRUM->p[P_E0] = (int16_t)kit;
    song.g[G_DRLVL] = 100;
    drum_on(note, 110);
    *peak = 0;
    *energy = 0;
    for (j = 0; j < FS * 6u / CTL; j++) {
        memset(l, 0, sizeof l), memset(r, 0, sizeof r), memset(rv, 0, sizeof rv);
        drums_render(l, r, rv, CTL);
        for (k = 0; k < CTL; k++) {
            int32_t a = l[k] < 0 ? -l[k] : l[k];
            assert(a < 131072);
            if (a > *peak) *peak = a;
            *energy += (uint64_t)a;
        }
        blocks++;
        for (k = 0; k < NDRUM && !drums.v[k].active; k++)
            ;
        if (k == NDRUM)
            break;
    }
    return blocks;
}

/* the power of x[0..n) at f Hz (Goertzel), a mean square */
static double tone_pow(const int32_t *x, uint32_t n, double f)
{
    double w = 2.0 * M_PI * f / FS, c = 2.0 * cos(w), s1 = 0, s2 = 0, s;
    uint32_t i;
    for (i = 0; i < n; i++) {
        s = x[i] + c * s1 - s2;
        s2 = s1;
        s1 = s;
    }
    return (s1 * s1 + s2 * s2 - c * s1 * s2) * 2.0 / ((double)n * n);
}
/* SLOOP 2.6 (isod89/sloop-fm1 v2.6 d30d655, tests/dsyn_test.c, GPL-3.0-only): a whistle on the kicks. The kick of every
 * synthesised kit (not the square-wave or bell ones, nor a crushed kit: their own steps), alone, through the drum track:
 *   curvature  the third difference at the block edges (the first 3 samples of a block) over inside the blocks. The
 *              levels stepped at every edge before (2 to 5 times); gliding exactly they do not (< 1.6, 2.6's bound)
 *   band       the tail's (from 100 ms) power in 1290..1470 Hz, around the step's rate FS / CTL, over 990..1170 Hz
 *              (dB, the mean over the kits): printed, the whistle a listener hears (-0.6 dB before, -2.2 after) */
static void kick_edges(FILE *rep)
{
    static int32_t buf[FS * 2];
    uint32_t kit, worst_k = 0, n, j;
    double worst = 0, tone_sum = 0;
    uint32_t nk = 0;
    for (kit = DRUM_SAMPLED; kit < DRUM_SYNTH_END; kit++) {
        const dkit_t *K = &DS_KITS[kit - DRUM_SAMPLED];
        int32_t semi, l[CTL], r[CTL], rv[CTL];
        const dsnd_t *d = &K->s[ds_lane(36, &semi)];
        double edge = 0, in = 0, tn;
        uint32_t ne = 0, ni = 0;
        if (!d->wave || d->wave == DW_SQUARE || d->wave == DW_BELL || K->crush)
            continue;
        memset(&drums, 0, sizeof drums);
        drums_set = -2;
        TDRUM->p[P_E0] = (int16_t)kit;
        song.g[G_DRLVL] = 100;
        drum_on(36, 110);
        for (n = 0; n + CTL <= sizeof buf / sizeof buf[0]; n += CTL) {
            memset(l, 0, sizeof l), memset(r, 0, sizeof r), memset(rv, 0, sizeof rv);
            drums_render(l, r, rv, CTL);
            memcpy(&buf[n], l, sizeof l);
        }
        for (j = 10u * CTL; j < 110u * CTL; j++) {
            double d3 = fabs((double)buf[j] - 3.0 * buf[j - 1] + 3.0 * buf[j - 2] - buf[j - 3]);
            if (j % CTL < 3u) edge += d3, ne++;
            else in += d3, ni++;
        }
        if (in > 0 && edge / ne / (in / ni) > worst)
            worst = edge / ne / (in / ni), worst_k = kit;
        {   /* the band around the block rate (the levels' sawtooth error times the kick's sine: lines at 1378 +- its
             * pitch) over the band below it, in the tail: a peak there is the whistle */
            double f, band = 0, below = 0;
            for (f = 1290; f < 1470; f += 4)
                band += tone_pow(&buf[4410], 16384u, f), below += tone_pow(&buf[4410], 16384u, f - 300);
            tn = 10.0 * log10((band + 1e-30) / (below + 1e-30));
        }
        tone_sum += tn, nk++;
    }
    fprintf(rep, "kick, curvature at the block edges / inside: at most %.2f (%s); the 1290..1470 Hz band over 990..1170 Hz in the tail: %.1f dB (mean of %u kits)\n",
            worst, DRUM_KIT_NAMES[worst_k], tone_sum / nk, nk);
    printf("  kick: edge curvature ratio %.2f (%s), 1.3-1.5 kHz band %+.1f dB over 1.0-1.2 kHz in the tail (mean of %u kits)\n", worst,
           DRUM_KIT_NAMES[worst_k], tone_sum / nk, nk);
    assert(worst < 1.6);                                  /* no step at the block edges: no whistle at 1.4 / 2.8 kHz */
}

int main(int argc, char **argv)
{
    uint32_t kit, lane, j, k;
    int32_t peak, ref_peak = 0;
    uint64_t e;
    FILE *rep = argc > 2 ? fopen(argv[2], "w") : stdout;
    host_tracks_init();
    kick_edges(rep);
    one_hit(0, 36, &ref_peak, &e);                       /* the sampled kick */
    for (kit = DRUM_SAMPLED; kit < DRUM_SYNTH_END; kit++) {
        int32_t kpk = 0;
        fprintf(rep, "%-8s %-12s", DRUM_KIT_NAMES[kit], DRUM_KIT_STYLES[kit]);
        for (lane = 0; lane < DS_LANES; lane++) {
            uint32_t b = one_hit(kit, DS_NOTE[lane], &peak, &e);
            assert(b < FS * 6u / CTL);                    /* every sound ends */
            assert(peak > 600);                           /* and is heard */
            if (peak > kpk) kpk = peak;
            fprintf(rep, " %5d/%4ums", peak, b * CTL * 1000u / FS);
        }
        fprintf(rep, "\n");
        assert(kpk < ref_peak * 3 && kpk > ref_peak / 4);  /* near the sampled kit */
    }
    {   /* cost: 6 synth voices (open hat, crash, ride, kick, snare, clap) vs 6 sampled ones */
        int32_t l[CTL], r[CTL], rv[CTL];
        struct timespec t0, t1;
        double ns_s, ns_y;
        for (k = 0; k < 2u; k++) {
            uint32_t rep_n = 2000;
            memset(&drums, 0, sizeof drums);
            drums_set = -2;
            TDRUM->p[P_E0] = (int16_t)(k ? DRUM_SAMPLED + 1u : 0u);
            clock_gettime(CLOCK_MONOTONIC, &t0);
            for (j = 0; j < rep_n; j++) {
                if (j % 100u == 0u) {
                    static const uint8_t N[6] = {46, 49, 51, 36, 38, 39};
                    uint32_t q;
                    for (q = 0; q < 6u; q++) drum_on(N[q], 100);
                }
                drums_render(l, r, rv, CTL);
            }
            clock_gettime(CLOCK_MONOTONIC, &t1);
            *(k ? &ns_y : &ns_s) = ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / (rep_n * (double)CTL);
        }
        fprintf(rep, "cost: 6 voices sampled %.1f ns/sample, synthesised %.1f ns/sample (host)\n", ns_s, ns_y);
    }
    if (argc > 1) {   /* demo: each kit, one bar: K . H . S . H . K K H . S . H O */
        static const uint8_t P[16][3] = {{36, 42, 0}, {0}, {42, 0}, {0}, {38, 42, 0}, {0}, {42, 0}, {70, 0},
                                         {36, 42, 0}, {36, 0}, {42, 0}, {37, 0}, {38, 39, 0}, {0}, {42, 63, 0}, {46, 0}};
        uint32_t step = FS * 60u / 120u / 4u, total = (DRUM_SYNTH_END - DRUM_SAMPLED) * 16u * step;
        FILE *f = fopen(argv[1], "wb");
        int32_t l[CTL], r[CTL], rv[CTL];
        wav_hdr(f, total / CTL * CTL);
        memset(&drums, 0, sizeof drums);
        drums_set = -2;
        for (kit = DRUM_SAMPLED; kit < DRUM_SYNTH_END; kit++) {
            TDRUM->p[P_E0] = (int16_t)kit;
            for (j = 0; j < 16u; j++) {
                uint32_t q, s;
                for (q = 0; q < 3u && P[j][q]; q++) drum_on(P[j][q], q ? 90 : 115);
                for (s = 0; s < step / CTL; s++) {
                    memset(l, 0, sizeof l), memset(r, 0, sizeof r), memset(rv, 0, sizeof rv);
                    drums_render(l, r, rv, CTL);
                    for (k = 0; k < CTL; k++) wav_put(f, clamp(l[k], -32767, 32767), clamp(r[k], -32767, 32767));
                }
            }
        }
        fclose(f);
    }
    printf("drum kits: %u synthesised x %u sounds bounded, audible, finite; levels near the sampled kit PASS\n",
           DRUM_SYNTH_END - DRUM_SAMPLED, DS_LANES);
    return 0;
}
