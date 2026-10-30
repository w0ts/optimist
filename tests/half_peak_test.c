/* SPDX-License-Identifier: GPL-3.0-only */
/* The release's master meter peak (fm1base/optimist/core/opt_half_peak.h): the largest |sample| of each half, kept as
 * one word a half instead of a 4 KB copy of the halves (opt_core.c's abuf, read only by ui/meters.c meter_tap). Against
 * the copy and the meter's scan of it, as they were: random renders of every length the render may have (a whole
 * half, a half and a part, shorter), full-scale and negative extremes, silence: the same peak, half by half. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../fm1base/optimist/core/opt_half_peak.h"

#define HALF_FRAMES 256u
#define HALF_WORDS (HALF_FRAMES * 2u)
static uint32_t seed = 7u;
static uint32_t rng(void)
{
    seed ^= seed << 13, seed ^= seed >> 17, seed ^= seed << 5;
    return seed;
}
/* the old path: the copy (zeros past n), then meter_tap's scan */
static int32_t old_peak(const int32_t *lr, uint32_t f, uint32_t n)
{
    static int32_t h[HALF_WORDS];
    int32_t pk = 0;
    uint32_t i;
    for (i = 0; i < HALF_WORDS; i++)
        h[i] = f * 2u + i < 2u * n ? lr[f * 2u + i] : 0;
    for (i = 0; i < HALF_WORDS; i++) {
        int32_t a = h[i] < 0 ? -h[i] : h[i];
        if (a > pk)
            pk = a;
    }
    return pk;
}
int main(void)
{
    static int32_t lr[2u * 2u * HALF_FRAMES];
    static const uint32_t NS[] = {HALF_FRAMES, 2u * HALF_FRAMES, HALF_FRAMES + 32u, 32u, 1u, 0u};
    uint32_t t, k, f, i, bad = 0, n_cmp = 0;
    for (t = 0; t < 4000u; t++) {
        uint32_t n = NS[t % (sizeof NS / sizeof NS[0])], mode = t % 5u;
        for (i = 0; i < sizeof lr / sizeof lr[0]; i++) {
            int32_t v = (int32_t)(rng() & 0x7FFFFFu) - 0x400000;   /* (24-bit, as the render's after OUT_SHIFT) */
            lr[i] = mode == 0u ? 0 : mode == 1u ? -0x800000 : mode == 2u ? (int32_t)(rng() >> 1) - 0x40000000 : v;
        }
        for (f = 0; f < (n ? n : 1u); f += HALF_FRAMES, n_cmp++)
            if (opt_half_peak(lr, f, n, HALF_WORDS) != old_peak(lr, f, n)) {
                if (bad++ < 5)
                    printf("FAIL render %u, n %u, half at %u: %d, the copy's %d\n", (unsigned)t, (unsigned)n, (unsigned)f,
                           (int)opt_half_peak(lr, f, n, HALF_WORDS), (int)old_peak(lr, f, n));
            }
        (void)k;
    }
    printf("%s   the per-half peak equals the copy's scan: %u halves compared, %u differ\n", bad ? "FAIL" : "ok",
           (unsigned)n_cmp, (unsigned)bad);
    return bad != 0;
}
