/* SPDX-License-Identifier: GPL-3.0-only */
/* The shared DSP blocks (firmware/src/dsp/dsp_common.h, dsp.c, dsp_float.h; docs/DSP-SHARED.md) against the copies they
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

/* ---- samples: ima_nibble, lerp16, smp_lp_k ---- */
static void t_samples(void)
{
    uint64_t bad = 0, n = 0;
    int32_t pred, idx, c, x;
    uint32_t r;
    for (pred = -32768; pred <= 32767; pred += 7)
        for (idx = 0; idx <= 88; idx++)
            for (c = 0; c < 16; c++, n++) {                    /* (the copy of sample_next / gr_dec / slc_dec_next) */
                int32_t step = IMA_STEP[idx], vd = step >> 3, p1 = pred, i1 = idx, p2 = pred, i2 = idx, y;
                uint32_t code = (uint32_t)c;
                if (code & 4u)
                    vd += step;
                if (code & 2u)
                    vd += step >> 1;
                if (code & 1u)
                    vd += step >> 2;
                p1 = clamp(p1 + ((code & 8u) ? -vd : vd), -32768, 32767);
                i1 = clamp(i1 + IMA_IDX[code & 7u], 0, 88);
                y = ima_nibble(code, IMA_STEP[i2], &p2, &i2);
                bad += y != p1 || p2 != p1 || i2 != i1;
            }
    check("ima_nibble = the IMA decode of SAMPLE, GRAIN, SLICE (predictor x index x nibble)", bad, n);
    bad = n = 0;
    for (r = 0; r < N_RAND; r++, n++) {
        int32_t a = (int32_t)(tst_rand() >> 16) - 32768, b = (int32_t)(tst_rand() >> 16) - 32768;
        uint32_t fr = tst_rand() & 0xFFFFu;
        int32_t fi = (int32_t)fr;
        bad += lerp16(a, b, fr) != a + (((b - a) * (int32_t)(fr >> 1)) >> 15)   /* sample, drums, slice */
             || lerp16(a, b, (uint32_t)fi) != a + (((b - a) * (fi >> 1)) >> 15); /* grain (int32 frac) */
    }
    check("lerp16 = the resamplers' interpolation in SAMPLE, the drum lanes, SLICE, GRAIN", bad, n);
    bad = n = 0;
    for (x = -70000; x <= 70000; x++, n++)
        bad += smp_lp_k(x) != 4000 + ((clamp(x, 0, 127 << 8) * 28767) >> 15);
    check("smp_lp_k = the low-pass coefficient of SAMPLE, GRAIN, SLICE (every cutoff)", bad, n);
}

/* ---- mixing and decays: pan_gains, decay_q16, decay_to0, the drum lanes' edits ---- */
static void t_mix(void)
{
    uint64_t bad = 0, n = 0;
    int32_t pan, x, gl, gr;
    uint32_t r;
    for (pan = -128; pan <= 127; pan++, n++) {
        pan_gains(pan, &gl, &gr);
        bad += gl != 4096 - (pan > 0 ? pan * 64 : 0) || gr != 4096 + (pan < 0 ? pan * 64 : 0);
    }
    for (r = 0; r < N_RAND; r++, n++) {
        int32_t v = (int32_t)(tst_rand() >> 1);
        uint32_t k = tst_rand() & 0xFFFFu;
        bad += decay_q16(v, k) != (int32_t)(((uint32_t)v * k) >> 16);
    }
    for (x = -70000; x <= 70000; x++, n++) {
        int32_t a = x, b = x;
        a -= (a >> 11) + 1;                                   /* drums.c, bassplus.c */
        bad += decay_to0(b) != a;
    }
    for (x = -64; x <= 64; x++, n++)                          /* drum_edit.c / drum_x0x.c (DE_CUT, DE_LEVEL) */
        bad += dl_cut_k(x) != ds_onepole((uint32_t)clamp(127 + 2 * x, 20, 127))
             || dl_lvl_g(clamp(x, -24, 6)) != (clamp(x, -24, 6) ? (int32_t)(pow2_q16(clamp(x, -24, 6) * 32) >> 4) : 0);
    check("pan_gains, decay_q16, decay_to0, dl_cut_k / dl_lvl_g = their copies (fx / drums / slicer, drums, edits)", bad, n);
}

/* ---- float (dsp_float.h): fm_tdf3, the polyBLEP halves, fm_fold3, fm_lin_pot, fm_discharge, fm_clip_sym ---- */
/* (run_tests.sh builds this with -ffp-contract=off, as the FM-1's float units: dsp_float.h is compiled where ACID's
 * unit first includes it, before any pragma) */
static float tst_f(float lo, float hi) { return lo + (hi - lo) * (float)(tst_rand() >> 8) * (1.0f / 16777216.0f); }
static int fdiff(float a, float b) { return memcmp(&a, &b, sizeof a) != 0; }
static void t_float(void)
{
    uint64_t bad = 0, n = 0;
    uint32_t r;
    int pot;
    {   /* the 303's RAT loop (locals) and the 808's bq3_run (struct state) against fm_tdf3, side by side */
        float c[7], z[3] = {0, 0, 0}, z0 = 0, z1 = 0, z2 = 0, w[3] = {0, 0, 0};
        for (r = 0; r < N_RAND / 4u; r++, n++) {
            float x = tst_f(-1.5f, 1.5f), y, yb;
            if (!(r & 1023u)) {
                uint32_t k;
                for (k = 0; k < 7u; k++)
                    c[k] = tst_f(-1.6f, 1.6f) * (k >= 4u ? 0.5f : 1.0f);
                z[0] = z[1] = z[2] = w[0] = w[1] = w[2] = z0 = z1 = z2 = 0.0f;
            }
            y = x * c[0] + z0;                                /* acid/bass303.c drive_rat */
            z0 = x * c[1] - y * c[4] + z1;
            z1 = x * c[2] - y * c[5] + z2;
            z2 = x * c[3] - y * c[6];
            yb = c[0] * x + w[0];                             /* x0x/drum808.c bq3_run */
            w[0] = c[1] * x - c[4] * yb + w[1];
            w[1] = c[2] * x - c[5] * yb + w[2];
            w[2] = c[3] * x - c[6] * yb;
            bad += fdiff(fm_tdf3(c, z, x), y) || fdiff(y, yb) || fdiff(z[0], z0) || fdiff(z[2], w[2]);
        }
    }
    for (r = 0; r < N_RAND / 4u; r++, n++) {
        float x = tst_f(-1.0f, 1.0f), v = tst_f(-8.0f, 8.0f), f = v, lo = tst_f(-2, 2), hi = lo + tst_f(0, 4);
        int i;
        for (i = 0; i < 3; i++) {                             /* the 909's / the 808's fold */
            if (f > 1.0f) f = 2.0f - f;
            if (f < -1.0f) f = -2.0f - f;
        }
        bad += fdiff(fm_blep_after(x), x + x - x * x - 1.0f) || fdiff(fm_blep_before(x), x * x + x + x + 1.0f)
             || fdiff(fm_fold3(v), f);
        {
            float s = v * 300000.0f, l = 1048576.0f;
            bad += fdiff(fm_clip_sym(s, l), s > l ? l : s < -l ? -l : s);   /* ACID / X0X output clip */
            bad += fdiff(fm_clampf(x, lo, hi), x < lo ? lo : (x > hi ? hi : x));   /* the 808's clampf */
        }
    }
    for (r = 0; r < 4096u; r++)
        for (pot = 0; pot <= 127; pot++, n++) {
            float lo = tst_f(-5000, 5000), hi = tst_f(-5000, 5000), t = (float)pot / 127.0f;
            bad += fdiff(fm_lin_pot(lo, hi, pot), lo + (hi - lo) * t);
        }
    for (r = 0; r < 4096u; r++) {                             /* the 808 cymbal's / hi-hat's discharge, to 0 */
        float a = tst_f(0, 2), b = a, step = tst_f(1e-6f, 1e-2f), base = 0, bb = 0;
        uint8_t ra = 0, rb = 0;
        int32_t na = 0, nb = 0, k;
        for (k = 0; k < 2000; k++, n++) {
            if (!ra) {
                ra = 1;
                base = a;
                na = 0;
            }
            if (a > 0.0f) {
                a = base - (float)(++na) * step;
                if (a < 0.0f)
                    a = 0.0f;
            }
            fm_discharge(&b, &rb, &bb, &nb, step);
            bad += fdiff(a, b) || na != nb;
        }
    }
    check("fm_tdf3, fm_blep_after/before, fm_fold3, fm_clip_sym, fm_clampf, fm_lin_pot, fm_discharge = the 303 / 909 / 808 copies", bad, n);
}

int main(void)
{
    t_xorshift();
    t_knee();
    t_small();
    t_svf();
    t_pitch();
    t_samples();
    t_mix();
    t_float();
    if (fails)
        printf("dsp_shared_test: %d blocks FAILED\n", fails);
    return fails != 0;
}
