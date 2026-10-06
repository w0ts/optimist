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
    drums.set = -2;
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

int main(int argc, char **argv)
{
    uint32_t kit, lane, j, k;
    int32_t peak, ref_peak = 0;
    uint64_t e;
    FILE *rep = argc > 2 ? fopen(argv[2], "w") : stdout;
    host_tracks_init();
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
            drums.set = -2;
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
        drums.set = -2;
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
