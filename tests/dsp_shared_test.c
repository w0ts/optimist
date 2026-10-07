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

/* ---- saturation: soft_knee ---- */
static int32_t ref_knee(int32_t y, int32_t K)                /* the copy in a2_out_c, trio, super, analog, formant, fx */
{
    int32_t a = y < 0 ? -y : y;
    if (a > K) {
        a = K + (softclip((a - K) * 2) >> 1);
        y = y < 0 ? -a : a;
    }
    return y;
}
static void t_knee(void)
{
    static const int32_t K[3] = {16000, 16384, 24000};
    uint64_t bad = 0, n = 0;
    int32_t x;
    uint32_t k, r;
    for (k = 0; k < 3u; k++) {
        for (x = -(1 << 21); x <= 1 << 21; x++, n++)         /* every input to 2^21 (softclip saturates at 2^16) */
            bad += soft_knee(x, K[k]) != ref_knee(x, K[k]);
        for (r = 0; r < N_RAND / 4u; r++, n++) {             /* and the rest of the domain, |x| < 2^30 */
            x = (int32_t)(tst_rand() >> 1) - (1 << 30);
            bad += soft_knee(x, K[k]) != ref_knee(x, K[k]);
        }
        bad += soft_knee(-(1 << 30) + 1, K[k]) != ref_knee(-(1 << 30) + 1, K[k]);
    }
    check("soft_knee = the knee of ANALOG, SUPER, TRIO (twice), FORMANT, fx.c (k 16000, 16384, 24000)", bad, n);
}

/* ---- lfsr15, clamp, mul_tz, crush_tz, lowcut_ef ---- */
static void t_small(void)
{
    uint64_t bad = 0, n = 0;
    uint32_t l, r;
    for (l = 0; l < 0x10000u; l++, n++) {                    /* every 16-bit state (the LFSRs keep 15) */
        uint32_t a = (l >> 1) | (((l ^ (l >> 1)) & 1u) << 14);              /* eng_lofi.c */
        uint32_t b = (l ^ (l >> 1)) & 1u;                                   /* drum_synth.c */
        bad += lfsr15(l) != a || lfsr15(l) != ((l >> 1) | (b << 14));
    }
    check("lfsr15 = LOFI's NES noise, the CHIP drum noise (every 16-bit state)", bad, n);
    bad = n = 0;
    for (r = 0; r < N_RAND; r++, n++) {
        int32_t x = (int32_t)tst_rand(), y = (int32_t)tst_rand() >> (r & 15u);
        int32_t ua = x > 32767 ? 32767 : x < -32768 ? -32768 : x;           /* usb_audio_stream.c ua_clip */
        int32_t lo = y < x ? y : x, hi = y < x ? x : y;
        int32_t pc = x < lo ? lo : x > hi ? hi : x;                         /* phys_dsp.c px_clamp */
        bad += clamp(x, -32768, 32767) != ua || clamp(x, lo, hi) != pc || ua_clip(x) != ua;
    }
    check("clamp = PHYS px_clamp, the USB capture's ua_clip", bad, n);
    bad = n = 0;
    for (r = 0; r < N_RAND; r++, n++) {
        int32_t a = (int32_t)(tst_rand() >> 15) - 65536, g = (int32_t)(tst_rand() >> 17);   /* |a g| < 2^31 */
        int32_t o = a * g, sp = (o + ((o >> 31) & 32767)) >> 15;            /* spring.c */
        int32_t p = a * g, fx = (p + ((p >> 31) & 0x7FFF)) >> 15;           /* fx.c mul_tz */
        bad += mul_tz(a, g) != sp || mul_tz(a, g) != fx;
    }
    check("mul_tz = fx.c's delay / reverb loop gain, spring.c's loop gain", bad, n);
    bad = n = 0;
    for (r = 0; r < N_RAND; r++, n++) {
        int32_t x = (int32_t)tst_rand();
        int32_t sh = (int32_t)(r % 16u);
        int32_t pu, fxv;
        if (x == INT32_MIN)
            continue;                                         /* (-x overflows in every copy) */
        pu = x >= 0 ? x & ~0x7FF : -((-x) & ~0x7FF);          /* punch.c */
        fxv = x >= 0 ? (x >> sh) << sh : -((-x >> sh) << sh); /* fx.c crush_bits */
        bad += crush_tz(x, 11) != pu || crush_tz(x, sh) != fxv;
    }
    check("crush_tz = DUST's crush_bits, PUNCH's CRUSH (shift 11)", bad, n);
    {   /* the three low cuts run side by side on the same signal: states and outputs */
        int32_t lc[3] = {0, 0, 0}, er[3] = {0, 0, 0}, sl[2] = {0, 0}, se[2] = {0, 0}, hp = 0, he = 0;
        bad = n = 0;
        for (r = 0; r < N_RAND; r++, n++) {
            int32_t x = (int32_t)(tst_rand() >> 14) - (1 << 17), o, y6, y5, e, d;
            if ((r >> 12) & 1u)
                x >>= 6;                                      /* quiet stretches: the dead band a rounded step had */
            e = x - lc[0] + er[0]; d = e >> 6; er[0] = e - (d << 6); lc[0] += d; y6 = x - lc[0];   /* fx.c lowcut1 */
            e = x - lc[1] + er[1]; d = e >> 5; er[1] = e - (d << 5); lc[1] += d; y5 = x - lc[1];   /* bassplus.c */
            o = x - hp + he; he = o & 63; hp += o >> 6;                                              /* spring.c */
            bad += lowcut_ef(x, &sl[0], &se[0], 6) != y6 || sl[0] != lc[0] || se[0] != er[0];
            bad += lowcut_ef(x, &sl[1], &se[1], 5) != y5 || sl[1] != lc[1] || se[1] != er[1];
            bad += y6 != x - hp || hp != lc[0] || he != er[0];
        }
        check("lowcut_ef = the LOWCUT's lowcut1 (6), BASS+'s lowcut5 (5), the SPRING's low cut (6; o & 63)", bad, n);
    }
}

/* ---- filters: tsvf_coef_k, tsvf_tick ---- */
static void ref_trio_coef(int32_t cut, int32_t k, int32_t *a1, int32_t *a2, int32_t *a3)   /* eng_trio.c (cut clamped) */
{
    int32_t g = SVF_G[cut >> 8];
    if ((cut >> 8) < 127)
        g += ((SVF_G[(cut >> 8) + 1] - g) * (cut & 255)) >> 8;
    *a1 = (int32_t)((4096u << 13) / (uint32_t)(4096 + ((g * (g + k)) >> 12)));
    *a2 = (*a1 * g) >> 12;
    *a3 = (*a2 * g) >> 12;
}
static void ref_ds_coef(int32_t cut, int32_t fk, int32_t *a1, int32_t *a2, int32_t *a3)    /* drum_synth.c ds_filter */
{
    int32_t i, g, den;
    cut = clamp(cut, 0, 127 << 8);
    i = cut >> 8;
    g = SVF_G[i];
    if (i < 127)
        g += ((SVF_G[i + 1] - g) * (cut & 255)) >> 8;
    den = 4096 + ((g * (g + fk)) >> 12);
    *a1 = (int32_t)((4096u << 13) / (uint32_t)den);
    *a2 = (*a1 * g) >> 12;
    *a3 = (*a2 * g) >> 12;
}
static void t_svf(void)
{
    uint64_t bad = 0, n = 0;
    int32_t cut, r, a1, a2, a3;
    tsvf_t c;
    for (r = 0; r <= 127; r++)
        for (cut = -300; cut <= (127 << 8) + 300; cut++, n += 3) {
            int32_t kt = 8192 - r * 7168 / 127, kd = 8192 - r * 245, cc = clamp(cut, 0, 127 << 8);
            tsvf_coef_k(&c, cc, kt);
            ref_trio_coef(cc, kt, &a1, &a2, &a3);
            bad += c.a1 != a1 || c.a2 != a2 || c.a3 != a3;
            tsvf_coef_k(&c, cut, kd);
            ref_ds_coef(cut, kd, &a1, &a2, &a3);
            bad += c.a1 != a1 || c.a2 != a2 || c.a3 != a3;
            tsvf_coef_k(&c, cut, 8192 - r * 7600 / 127);     /* tsvf_coef = its old body (k from reso) */
            ref_ds_coef(cut, 8192 - r * 7600 / 127, &a1, &a2, &a3);
            bad += c.a1 != a1 || c.a2 != a2 || c.a3 != a3;
        }
    check("tsvf_coef_k = tsvf_coef's body, TRIO's filter, the synth drums' ds_filter (every cut x reso)", bad, n);
    bad = n = 0;
    {
        int32_t f1 = 0, f2 = 0, g1 = 0, g2 = 0, k = 0;
        for (r = 0; r < (int32_t)N_RAND; r++, n++) {
            int32_t x = (int32_t)(tst_rand() >> 15) - 65536, v3, v1, v2, lp, bp;
            if (!(r & 4095)) {
                tsvf_coef_k(&c, (int32_t)(tst_rand() % (128u << 8)), k = 8192 - (int32_t)(tst_rand() % 128u) * 245);
            }
            v3 = x - f2;                                      /* drum_synth.c ds_render (and SUPER tsvf_lpbp) */
            v1 = (c.a1 * f1 + c.a2 * v3) >> 13;
            v2 = f2 + ((c.a2 * f1 + c.a3 * v3) >> 13);
            f1 = clamp(2 * v1 - f1, -150000, 150000);
            f2 = clamp(2 * v2 - f2, -150000, 150000);
            lp = tsvf_tick(&c, x, &g1, &g2, &bp);
            bad += lp != v2 || bp != v1 || f1 != g1 || f2 != g2;
        }
        (void)k;
    }
    check("tsvf_tick = SUPER's tsvf_lpbp, the synth drums' SVF (LP and BP out, states), tsvf_lp", bad, n);
}

/* ---- pitch: det_inc, fine_inc call sites; neg_cos16 ---- */
static void t_pitch(void)
{
    uint64_t bad = 0, n = 0;
    int32_t pitch, det, r;
    uint32_t ph;
    for (pitch = -64; pitch < 2048 + 64; pitch += 3)
        for (det = -127; det <= 127; det++, n++) {            /* (DTN: 0..127 cents, signed with the swarm's) */
            int32_t fine = (int32_t)(tst_rand() % 513u) - 256;
            int32_t d16 = det * 16 / 100, rem = det * 16 - d16 * 100;   /* eng_analog2.c / eng_phase.c / eng_analog.c */
            uint32_t inc2 = fine_inc(PITCH_INC[clamp(pitch + d16, 0, 2047)], fine);
            inc2 += (uint32_t)((int32_t)(inc2 >> 12) * (rem * 2367 / 16000));
            bad += det_inc(pitch, det, fine) != inc2;
        }
    check("det_inc = the DTN lines of ANALOG 2, PHASE, the original ANALOG (pitch x det x fine)", bad, n);
    bad = n = 0;
    for (r = 0; r < (int32_t)N_RAND; r++, n++) {
        uint32_t inc = tst_rand() >> 1;
        int32_t f = (int32_t)(tst_rand() % 65536u) - 32768, x = (int32_t)inc;
        uint32_t a = inc;
        a += (uint32_t)((int32_t)(a >> 12) * f);              /* voice.c, eng_analog2.c drift, eng_super.c, cz_native.c */
        bad += fine_inc(inc, f) != a;
        (void)x;
    }
    check("fine_inc = the inline copies in voice.c (TUNE), ANALOG 2 / SUPER drift, CZ's detune", bad, n);
    bad = n = 0;
    for (ph = 0; ph < 0x20000u; ph++, n++)                    /* (bits above 16 dropped: two turns) */
        bad += neg_cos16(ph) != -sine_i(((ph & 0xFFFFu) << 16) + 0x40000000u);
    check("neg_cos16 = PHASE's pd_cos, CZ's cz_cos (every 16-bit phase)", bad, n);
}

int main(void)
{
    t_xorshift();
    t_knee();
    t_small();
    t_svf();
    t_pitch();
    if (fails)
        printf("dsp_shared_test: %d blocks FAILED\n", fails);
    return fails != 0;
}
