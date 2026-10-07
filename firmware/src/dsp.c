/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Shared DSP building blocks for the Felucca engines (all fixed point).
 * Voice output convention: add sample * amp to out[], where a full-scale
 * oscillator at amp = 1.0 (Q15 32767) contributes VOICE_FS. */
#include "dsp_common.h"                  /* the table-free primitives (xorshift32, ...), shared with the X0X units */
#define VOICE_FS 24000           /* per-voice level: one voice peaks near -6 dBFS before the master */

AINL int32_t mulq15(int32_t a, int32_t b) { return (a * b) >> 15; }
#if FELUCCA_GLIDE
/* a gain gliding to its target (FELUCCA_GLIDE, after X0X 0.10.1: fx.c mix_part, drums.c drums_mix): one step a block */
#define GLIDE_K 2294             /* Q15: 1 - exp(-CTL / (10 ms x FS)) = 0.0700 for CTL 32, FS 44.1 kHz */
_Static_assert(CTL == 32, "GLIDE_K is for blocks of 32 frames");
AINL int32_t glide_next(int32_t cur, int32_t tgt)
{
    int32_t st = ((tgt - cur) * GLIDE_K) >> 15;        /* (|tgt - cur| < 2^16: no overflow) */
    return st ? cur + st : tgt;                         /* a step that rounds to nothing: there */
}
#endif
/* a * k / 65536 without 64-bit: a up to 2^31, k Q16 */
AINL int32_t mulq16(int32_t a, uint32_t k)
{
    return (int32_t)(((a >> 16) * (int32_t)k) + (int32_t)(((uint32_t)(a & 0xFFFF) * k) >> 16));
}
/* an increment times (1 + fine / 4096): vmod_t.fine (the MIDI bend below 1/16 semitone, from Melodee) */
AINL uint32_t fine_inc(uint32_t inc, int32_t fine)
{
    return inc + (uint32_t)((int32_t)(inc >> 12) * fine);
}

/* sine, linearly interpolated between the 1024 table points (plain lookup: THD -55 dB) */
AINL int32_t sine_i_c(uint32_t ph)
{
    uint32_t i = ph >> 22;
    int32_t a = SINE[i], b = SINE[(i + 1u) & 1023u];
    return a + (((b - a) * (int32_t)((ph >> 7) & 0x7FFFu)) >> 15);
}
#include "../hal/fm1_simd.h"                     /* FELUCCA_SIMD: sine_i with the packed 16-bit forms */
#if FELUCCA_SIMD
/* EXPERIMENTAL (hal/fm1_simd.h): sine_i_c over a packed table, each point with its step to the next */
static uint32_t SINE_PK[1024];
#if FELUCCA_SIMD_PROBE
static uint32_t simd_ok;                         /* set by the boot probe (simd_probe.c): else the C */
static uint32_t simd_swarm_ok;                   /* every form passed: ANALOG 2's packed swarm too */
#else
#define simd_ok 1
#define simd_swarm_ok 1
#endif
#if FELUCCA_SIMD_CHECK
struct { uint32_t calls, bad; } simd_check;      /* read by the emulator (play_check peek:simd_check:2) */
#endif
static void sine_pk_init(void)
{
    uint32_t i;
    for (i = 0; i < 1024u; i++)
        SINE_PK[i] = ((uint32_t)(SINE[(i + 1u) & 1023u] - SINE[i]) << 16) | (uint16_t)SINE[i];
}
AINL int32_t sine_i(uint32_t ph)
{
    int32_t y;
    if (!simd_ok)
        return sine_i_c(ph);
    y = asm_sine_pk(ph, SINE_PK);
#if FELUCCA_SIMD_CHECK
    simd_check.calls++;
    simd_check.bad += (uint32_t)(y != sine_i_c(ph));
#endif
    return y;
}
#else
AINL int32_t sine_i(uint32_t ph) { return sine_i_c(ph); }
#endif
AINL int32_t osc_sine(uint32_t ph) { return sine_i(ph); }

/* polyBLEP residual (Q15) around a wrap of a phase accumulator */
AINL int32_t blep(uint32_t ph, uint32_t inc)
{
    uint32_t d = inc >> 15;
    int32_t x;
    if (!d)
        return 0;
    if (ph < inc) {
        x = (int32_t)(ph / d);                       /* 0..32767 */
        return x + x - ((x * x) >> 15) - 32768;
    }
    if (ph > 0xFFFFFFFFu - inc) {
        x = -(int32_t)((0xFFFFFFFFu - ph) / d);       /* -32767..0 */
        return ((x * x) >> 15) + x + x + 32768;
    }
    return 0;
}

AINL int32_t osc_saw(uint32_t ph, uint32_t inc)
{
    return ((int32_t)(ph >> 16) - 32768) - blep(ph, inc);
}

AINL int32_t osc_pulse(uint32_t ph, uint32_t inc, uint32_t pw)
{
    return (osc_saw(ph, inc) - osc_saw(ph + pw, inc)) >> 1;
}

AINL int32_t osc_tri(uint32_t ph)
{
    int32_t s = (int32_t)(ph >> 15);                 /* 0..131071 */
    return s < 65536 ? s - 32768 : 98303 - s;
}

/* tanh(x / 32768) * 32767, linearly interpolated (a plain lookup gives
 * 256-unit steps, audible on quiet signals) */
AINL int32_t softclip(int32_t x)
{
    int32_t a = x < 0 ? -x : x, i = a >> 8, y;
    if (i >= 256)
        y = TANH_Q15[256];                           /* continuous with the table */
    else
        y = TANH_Q15[i] + (((TANH_Q15[i + 1] - TANH_Q15[i]) * (a & 255)) >> 8);
    return x < 0 ? -y : y;
}

/* linear up to k, above it a tanh knee with the same slope at the joint (softclip of twice the excess, halved):
 * only peaks saturate. Was the same five lines in fx.c (knee, k 16384), ANALOG 2 (16000; eng_analog2.c a2_out_c,
 * whose asm twin asm_a2_out keeps its own copy), SUPER, TRIO (input and output). (|x| - k) * 2 must fit an int32:
 * |x| < 2^30. Kept as copies (measured, docs/DSP-SHARED.md): FORMANT (24000; the call costs formant_render 4 B of
 * RAM code, register allocation), the original ANALOG of ANALOG2=0 (the host compiles the call 10 % slower), PHYS
 * (it clamps the excess first) */
AINL int32_t soft_knee(int32_t x, int32_t k)
{
    int32_t a = x < 0 ? -x : x;
    if (a > k) {                                     /* (the copies' form: the same code in their loops) */
        a = k + (softclip((a - k) * 2) >> 1);
        x = x < 0 ? -a : a;
    }
    return x;
}

AINL uint32_t noise32(int32_t *st) { return xorshift32((uint32_t *)st); }   /* (dsp_common.h; int32 states) */

/* a random walk of the pitch per block (SUPER's DRFT, ANALOG 2's DRFT), cents x 256 in *dp, up to
 * +-30 ct at drift 127; returns 1/4096 semitone units */
AINL int32_t super_drift(int32_t *dp, int32_t *nst, int32_t drift)
{
    int32_t d = *dp, lim = drift * 30 * 256 / 127;
    d += ((int32_t)(noise32(nst) >> 24) - 128) * drift / 8;
    d -= d >> 7;                                      /* drawn back to the pitch */
    d = clamp(d, -lim, lim);
    *dp = d;
    return (d >> 8) * 2367 / 1000;
}

/* Trapezoidal SVF (A. Simper), unconditionally stable. Coefficients per
 * block in Q13; signals stay within +-150000 so products fit in 32 bits. */
typedef struct { int32_t a1, a2, a3; } tsvf_t;
AINL void tsvf_coef(tsvf_t *c, int32_t cut, int32_t reso)   /* cut: 0..127 << 8 */
{
    int32_t i, g, den, k = 8192 - reso * 7600 / 127;   /* damping 2.0 .. ~0.15 (Q12) */
    cut = clamp(cut, 0, 127 << 8);
    i = cut >> 8;
    g = SVF_G[i];
    if (i < 127)                                       /* between table points: sweeps without 128 steps */
        g += ((SVF_G[i + 1] - g) * (cut & 255)) >> 8;
    den = 4096 + ((g * (g + k)) >> 12);
    c->a1 = (int32_t)((4096u << 13) / (uint32_t)den);
    c->a2 = (c->a1 * g) >> 12;
    c->a3 = (c->a2 * g) >> 12;
}

AINL int32_t tsvf_lp(const tsvf_t *c, int32_t in, int32_t *ic1, int32_t *ic2)
{
    int32_t v3 = in - *ic2;
    int32_t v1 = (c->a1 * *ic1 + c->a2 * v3) >> 13;
    int32_t v2 = *ic2 + ((c->a2 * *ic1 + c->a3 * v3) >> 13);
    *ic1 = clamp(2 * v1 - *ic1, -150000, 150000);
    *ic2 = clamp(2 * v2 - *ic2, -150000, 150000);
    return v2;
}

/* amplitude ramp over the block. Blocks are always CTL long, so x / CTL is a
 * shift rounded towards zero (-Os would keep a hardware divide per sample) */
#define CTL_LOG2 5
#if (1 << CTL_LOG2) != CTL
#error "CTL_LOG2 does not match CTL"
#endif
AINL int32_t amp_at(const vmod_t *m, uint32_t i)
{
    int32_t x = (m->amp1 - m->amp0) * (int32_t)i;
    return m->amp0 + ((x + ((x >> 31) & (CTL - 1))) >> CTL_LOG2);
}
