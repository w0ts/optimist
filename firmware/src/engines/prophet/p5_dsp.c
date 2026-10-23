/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* PROPHET's voice: Melodee's Prophet-5 Rev 4 oscillators and filter (keremimo/melodee firmware/src/eng_prophet_test.c,
 * 398ada1, GPL-3.0-only), float, its own unit: the FM-1's FPU is on only in units built with the FPU flags
 * (tools/build.py), the host tests include it into the one unit. Kept from Melodee: the polyBLEP saws and pulses, B's
 * triangle (the leaky integral of its square), hard sync with its BLEP-like correction, Poly-Mod from oscillator B to A's
 * pitch and to the cutoff, the Curtis (Rev 3 / 4) 4-pole: four trapezoidal one-poles with the algebraic feedback, a soft
 * clip at the input and the output. Left out (Optimist's track does them): Melodee's native patch and its envelopes,
 * the LFO and wheel, the SSM (Rev 1 / 2) stages, the noise source, the voice drift, the dual-core batching. */
#include "p5_dsp.h"
#include "p5_tables.h"

typedef union { float f; uint32_t u; int32_t i; } p5_bits_t;

static inline float p5_absf(float x) { return x < 0.0f ? -x : x; }
static inline float p5_minf(float a, float b) { return a < b ? a : b; }
static inline float p5_clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }
static inline float p5_ld(const int32_t *s) { p5_bits_t b; b.i = *s; return b.f; }
static inline void p5_st(int32_t *s, float f) { p5_bits_t b; b.f = f; *s = b.i; }

/* Melodee's p5_fast_blep / p5_fast_osc_saw: the integer phase, everything after it normalised float */
static inline float p5_blep(uint32_t ph, uint32_t inc)
{
    if (!inc)
        return 0.0f;
    if (ph < inc) {
        float x = (float)ph / (float)inc;
        return x + x - x * x - 1.0f;
    }
    if (ph > 0xFFFFFFFFu - inc) {
        float x = -(float)(0xFFFFFFFFu - ph) / (float)inc;
        return x * x + x + x + 1.0f;
    }
    return 0.0f;
}
static inline float p5_saw(uint32_t ph, uint32_t inc) { return (float)ph * (1.0f / 2147483648.0f) - 1.0f - p5_blep(ph, inc); }

/* tanh from P5_TANH (|x| < 8), linearly interpolated */
static inline float p5_softclip(float x)
{
    float a = p5_absf(x) * 128.0f, y;
    uint32_t i;
    if (a >= 1024.0f)
        return x < 0.0f ? -1.0f : 1.0f;
    i = (uint32_t)a;
    y = P5_TANH[i] + (P5_TANH[i + 1u] - P5_TANH[i]) * (a - (float)i);
    return x < 0.0f ? -y : y;
}

/* 2^x for x in -8..6: one octave interpolated, the power of two in the exponent bits */
static inline float p5_ratio(float x)
{
    int32_t oct = (int32_t)x;
    float pos;
    uint32_t i;
    p5_bits_t sc;
    if ((float)oct > x)
        oct--;
    pos = (x - (float)oct) * 512.0f;
    i = (uint32_t)pos;
    sc.u = (uint32_t)(oct + 127) << 23;
    return (P5_EXP2[i] + (P5_EXP2[i + 1u] - P5_EXP2[i]) * (pos - (float)i)) * sc.f;
}
static inline uint32_t p5_inc(uint32_t base, float x)
{
    if (x == 0.0f)
        return base;
    return (uint32_t)p5_clampf((float)base * p5_ratio(p5_clampf(x, -8.0f, 6.0f)), 1.0f, 1932735232.0f);
}

/* the Curtis 4-pole (Melodee's p5_filter_core, curtis): z[0..3] the stages */
static inline float p5_filter(float *z, float in, float cutoff, float k, float gain)
{
    float g, g2, g3, g4, sigma, u, rec;
    uint32_t i, j;
    cutoff = p5_clampf(cutoff, 0.0f, 127.0f);
    i = (uint32_t)cutoff;
    g = P5_TPT_G[i];
    if (i < 127u)
        g += (P5_TPT_G[i + 1u] - g) * (cutoff - (float)i);
    g2 = g * g, g3 = g2 * g, g4 = g3 * g;
    rec = 1.0f / (1.0f + k * g4);
    sigma = (1.0f - g) * (g3 * z[0] + g2 * z[1] + g * z[2] + z[3]);
    u = p5_softclip((in * gain - k * sigma) * rec);
    for (j = 0; j < 4u; j++) {
        float d = g * (u - z[j]), y = z[j] + d;
        z[j] = p5_clampf(y + d, -1.83105469f, 1.83105469f);
        if (p5_absf(z[j]) < 1e-20f)
            z[j] = 0.0f;
        u = y;
    }
    return p5_softclip(u);
}

/* one oscillator: saw / pulse / triangle (the triangle integrates the square: tri its state), each half scale */
static inline float p5_wave(uint32_t ph, uint32_t inc, uint32_t pw, int saw, int pulse, int tri, float *tr)
{
    float y = 0.0f, a, sq = 0.0f;
    if (!saw && !pulse && !tri)
        return 0.0f;
    a = p5_saw(ph, inc);
    if (saw)
        y += a * 0.5f;
    if (pulse) {
        sq = (a - p5_saw(ph + pw, inc)) * 0.5f;
        y += sq;
    }
    if (tri) {
        if (!pulse || pw != 0x80000000u)
            sq = (a - p5_saw(ph + 0x80000000u, inc)) * 0.5f;
        *tr = p5_clampf(*tr * (32767.0f / 32768.0f) + sq * (float)inc * (8.0f / 4294967296.0f), -1.220703125f, 1.220703125f);
        y += *tr * 0.5f;
    }
    return y;
}

void p5_render(int32_t *out, uint32_t n, uint32_t *ph, int32_t *st, const p5_args_t *a)
{
    float z[4] = {p5_ld(&st[0]), p5_ld(&st[1]), p5_ld(&st[2]), p5_ld(&st[3])};
    float tr = p5_ld(&st[4]), tail = p5_ld(&st[5]), unused = 0.0f;
    float rn = n ? 1.0f / (float)n : 0.0f;
    float la = a->mix > 0 ? (float)(64 - a->mix) * (1.0f / 64.0f) : 1.0f;
    float lb = a->mix < 0 ? (float)(64 + a->mix) * (1.0f / 64.0f) : 1.0f;
    float pm_freq = a->pmod > 0 ? (float)a->pmod * (1.085f / 63.0f) : 0.0f;   /* (Melodee: POLY B 127 = 1.085 octaves) */
    float pm_filt = a->pmod < 0 ? (float)-a->pmod * (112.0f / 64.0f) : 0.0f;  /* (Melodee: 112 cutoff steps) */
    float cut0 = (float)a->cut0 * (1.0f / 256.0f), cut1 = (float)a->cut1 * (1.0f / 256.0f);
    float k = (float)a->res * (19200.0f / (127.0f * 4096.0f)), gain = 1.0f - (float)a->res * (100.0f / 32768.0f);
    float scale = (float)a->scale;
    uint32_t i, pa = ph[0], pb = ph[1], ib = a->inc_b;
    for (i = 0; i < n; i++) {
        float t = (float)i * rn, b, bn, xa, x, fc, amp;
        uint32_t da, nb, na;
        b = p5_wave(pb, ib, a->pw_b, a->saw_b, a->pulse_b, a->tri_b, &tr);
        bn = b * 2.0f;
        da = pm_freq != 0.0f ? p5_inc(a->inc_a, bn * pm_freq) : a->inc_a;
        xa = p5_wave(pa, da, a->pw_a, a->saw_a, a->pulse_a, 0, &unused) + tail;
        tail = 0.0f;
        nb = pb + ib, na = pa + da;
        if (a->sync && ib && nb < pb) {              /* B wrapped inside this sample: A restarts there */
            float frac = p5_minf((float)(0u - pb) / (float)ib, 1.0f), rem = 1.0f - frac;
            uint32_t at = pa + (uint32_t)((float)da * frac);
            float before = p5_wave(at, da, a->pw_a, a->saw_a, a->pulse_a, 0, &unused);
            float after = p5_wave(0, da, a->pw_a, a->saw_a, a->pulse_a, 0, &unused);
            float jump = (after - before) * 0.5f;
            xa += jump * rem * rem;
            tail = -jump * frac * frac;
            na = (uint32_t)((float)da * rem);
        }
        pa = na, pb = nb;
        x = xa * la + b * lb;
        fc = cut0 + (cut1 - cut0) * t + bn * pm_filt;
        amp = ((float)a->amp0 + (float)(a->amp1 - a->amp0) * t) * (1.0f / 32768.0f);
        out[i] += (int32_t)(p5_filter(z, p5_softclip(x), fc, k, gain) * amp * scale);
    }
    ph[0] = pa, ph[1] = pb;
    p5_st(&st[0], z[0]), p5_st(&st[1], z[1]), p5_st(&st[2], z[2]), p5_st(&st[3], z[3]);
    p5_st(&st[4], tr), p5_st(&st[5], tail);
}
