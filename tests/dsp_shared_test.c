/* SPDX-License-Identifier: GPL-3.0-only */
/* The shared DSP blocks (firmware/src/dsp_common.h, dsp.c, dsp_float.h; docs/DSP-SHARED.md) against the copies they
 * replaced: each copy is kept here, verbatim, as the reference, and run against the shared block over every input
 * that matters (exhaustively where the domain allows, else edges plus 2^24 pseudo-random ones). A merge that
 * changed one caller's arithmetic fails here; tests/dsp_ab.sh compares whole renders of two trees.
 *   dsp_shared_test            prints one line per block, exits 1 on a mismatch */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int fails;
static void check(const char *what, uint64_t bad, uint64_t n)
{
    printf("%-100s %s (%llu cases)\n", what, bad ? "FAILED" : "ok", (unsigned long long)n);
    if (bad)
        fails++;
}

static uint32_t tst_s = 0x9E3779B9u;
static uint32_t tst_rand(void)                   /* the test's own inputs: an LCG, not the block under test */
{
    tst_s = tst_s * 1664525u + 1013904223u;
    return tst_s ^ (tst_s >> 16);
}
#define N_RAND (1u << 24)

/* ---- random: xorshift32 ---- */
static uint32_t ref_noise32(int32_t *st)                     /* dsp.c noise32 */
{
    uint32_t s = (uint32_t)*st;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    *st = (int32_t)s;
    return s;
}
static uint32_t ref_px_rand(uint32_t *s)                     /* phys_dsp.c px_rand (and libc.c rng, cz_native.c) */
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *s = x;
}
static uint32_t ref_tb_rng(uint32_t *r)                      /* eng_acid.c tb_rng */
{
    uint32_t x = *r ? *r : 1u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *r = x;
}
static void ref_formant(int32_t *nz)                         /* eng_formant.c formant_note_on's RAND */
{
    *nz ^= (int32_t)((uint32_t)*nz << 13);                   /* (the original shifted the int32: the same bits) */
    *nz ^= (int32_t)((uint32_t)*nz >> 17);
    *nz ^= (int32_t)((uint32_t)*nz << 5);
}
static void t_xorshift(void)
{
    uint64_t bad = 0, n;
    for (n = 0; n < N_RAND + 2u; n++) {
        uint32_t s = n < 2u ? (uint32_t)n * 0x80000000u : tst_rand(), a = s, b = s, c = s, d = s;
        int32_t e = (int32_t)s, f = (int32_t)s, g = (int32_t)s, h = (int32_t)s;
        uint32_t ra = ref_px_rand(&a), rb = xorshift32(&b);
        uint32_t rc = ref_tb_rng(&c);
        uint32_t rd;
        if (!d)
            d = 1u;
        rd = xorshift32(&d);
        bad += ra != rb || a != b || rc != rd || c != d;
        bad += ref_noise32(&e) != noise32(&f) || e != f;
        ref_formant(&g);
        xorshift32((uint32_t *)&h);
        bad += g != h;
    }
    check("xorshift32 = noise32, px_rand, rng, tb_rng (zero taken as 1), the formant RAND, the CZ ring noise", bad, n);
}

int main(void)
{
    t_xorshift();
    if (fails)
        printf("dsp_shared_test: %d blocks FAILED\n", fails);
    return fails != 0;
}
