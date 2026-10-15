/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 the Optimist contributors (written for SLOOP-plus, Optimist's earlier name) */
/* Hand-written pi32v2 inner loops for the hottest DSP kernels (measured with the emulator's FM1_HOT
 * profile). From perf/asm-hotspots (written for the DX7 engine's msfa operator); in this branch they run
 * FM6's operators (eng_fm6.c): MODERN is the same msfa operator (asm_fm_*), MARK I has its own (asm_mki_*);
 * asm_fm6_out is FM6's voice output.
 * Each helper computes exactly what the C loop it replaces computes (same operations, same
 * order, same wrap-around), so the output is bit-identical; the C stays next to every call as the
 * reference and is what the host build and tests run.
 *
 * FELUCCA_ASM: 1 = use these (default on the pi32v2 target), 0 = the C everywhere (the default on any
 * other compiler; FELUCCA_ASM=0 ./build.sh builds the target with the C, to compare).
 *
 * What the asm gains over clang -Os: post-increment addressing instead of an index scaled every
 * sample, one bit-field extract (uextra) for a table index, two table bases so both interpolation
 * points load with a scaled index, the decrement-and-branch loop (one instruction of loop control)
 * and, for a block clear, a hardware repeat.
 *
 * Interrupts: the loops are ordinary branches (any interrupt is taken between two instructions). The
 * only hardware repeat is asm_zero32, a 32-iteration immediate `rep` of one store: the same form the
 * vendor compiler emits for small copies (-pi32v2-enable-rep-memop), at most 32 stores of latency if
 * the core holds an interrupt until the block ends. (A register-count `rep` would need the SDK's
 * re-entry idiom, `1: rep rN {..} if (rN != 0) goto 1b`, jl_math.c: not used here.) Registers used:
 * only the operands below (clang allocates them); no special register (no MAC accumulator, no loop
 * register) is left changed.
 *
 * The rep block length is at most 32 bytes (16 halfwords): the operator bodies do not fit, so they loop
 * with `if (--rN != 0) goto`, one instruction of loop control. */
#ifndef FM1_DSP_ASM_H
#define FM1_DSP_ASM_H
#include <stdint.h>
#include "fm1_simd.h"                                /* FELUCCA_SIMD: the packed 16-bit swarm below */

#ifndef FELUCCA_ASM
#ifdef __PI32V2__
#define FELUCCA_ASM 1
#else
#define FELUCCA_ASM 0
#endif
#endif
#if FELUCCA_ASM && !defined(__PI32V2__)
#error "FELUCCA_ASM=1 needs the pi32v2 target (JieLi clang)"
#endif
#ifndef FELUCCA_ASM_CHECK
#define FELUCCA_ASM_CHECK 0
#endif
#if FELUCCA_ASM_CHECK && !FELUCCA_ASM
#error "FELUCCA_ASM_CHECK needs FELUCCA_ASM"
#endif

#if FELUCCA_ASM
/* b[0..31] = 0 */
static inline __attribute__((always_inline)) void asm_zero32(int32_t *b)
{
    int32_t z = 0;
    __asm__ volatile("rep 32 {\n\t"
                     "[%[b]++=4] = %[z]\n\t"
                     "}\n\t"
                     : [b] "+r"(b)
                     : [z] "r"(z)
                     : "memory");
}

/* The FM operator of msfa (Dexed) over a Q24 sine table of 1025 entries (tab[1024] closes the cycle):
 *   for each of n (> 0) samples:  g += dg;  x = phase [+ in[i]];
 *       y = tab[i] + (((tab[i + 1] - tab[i]) * (x & 0x3FFF)) >> 14),  i = (x >> 14) & 1023;
 *       y = (int32_t)(((int64_t)y * g) >> 24);  out[i] = y  or  out[i] += y;  phase += freq
 * The body: the interpolated lookup, the 32x32->64 product, its >> 24. */
#define ASM_FM_LOOKUP(X)                                                                                \
    "%[ix] = uextra(" X ", p:14, l:10)\n\t"                                                             \
    "%[y0] = [%[t0]+%[ix]<<2]\n\t"                                                                      \
    "%[ix] = [%[t1]+%[ix]<<2]\n\t"                                                                      \
    "%[ix] = %[ix] - %[y0]\n\t"                                                                         \
    "%[lo] = uextra(" X ", p:0, l:14)\n\t"                                                              \
    "%[ix] *= %[lo]\n\t"                                                                                \
    "%[ix] = %[ix] >>> 14\n\t"                                                                          \
    "%[ix] += %[y0]\n\t"                                                                                \
    "%[p] = %[ix] * %[g] (s)\n\t"                                                                       \
    "%[p] >>= 24\n\t"
#define ASM_FM_STORE "[%[out]++=4] = %[p].l\n\t"
#define ASM_FM_ADD "[%[out]+0] += %[p].l\n\t%[out] += 4\n\t"

#define ASM_FM_PURE(OUT)                                                                                \
    __asm__ volatile("1:\n\t"                                                                           \
                     "%[g] += %[dg]\n\t" ASM_FM_LOOKUP("%[ph]") OUT "%[ph] += %[fq]\n\t"                \
                     "if (--%[n] != 0) goto 1b\n\t"                                                     \
                     : [out] "+r"(out), [ph] "+r"(phase), [g] "+r"(g), [n] "+r"(n), [ix] "=&r"(ix),    \
                       [y0] "=&r"(y0), [lo] "=&r"(lo), [p] "=&r"(p)                                     \
                     : [fq] "r"(freq), [dg] "r"(dg), [t0] "r"(tab), [t1] "r"(tab + 1)                   \
                     : "memory")

/* no modulation input: x = phase */
static inline __attribute__((always_inline)) void asm_fm_pure(int32_t *out, int32_t phase, int32_t freq,
                                                              int32_t g, int32_t dg, const int32_t *tab,
                                                              int32_t n, int add)
{
    int32_t ix, y0, lo;
    int64_t p;
    if (add)
        ASM_FM_PURE(ASM_FM_ADD);
    else
        ASM_FM_PURE(ASM_FM_STORE);
}

#define ASM_FM_MOD(OUT)                                                                                 \
    __asm__ volatile("1:\n\t"                                                                           \
                     "%[g] += %[dg]\n\t"                                                                \
                     "%[x] = [%[in]++=4]\n\t"                                                           \
                     "%[x] += %[ph]\n\t" ASM_FM_LOOKUP("%[x]") OUT "%[ph] += %[fq]\n\t"                 \
                     "if (--%[n] != 0) goto 1b\n\t"                                                     \
                     : [out] "+r"(out), [in] "+r"(in), [ph] "+r"(phase), [g] "+r"(g), [n] "+r"(n),     \
                       [x] "=&r"(x), [ix] "=&r"(ix), [y0] "=&r"(y0), [lo] "=&r"(lo), [p] "=&r"(p)       \
                     : [fq] "r"(freq), [dg] "r"(dg), [t0] "r"(tab), [t1] "r"(tab + 1)                   \
                     : "memory")

/* modulated: x = phase + in[i] (in may be out: each in[i] is read before out[i] is written) */
static inline __attribute__((always_inline)) void asm_fm_mod(int32_t *out, const int32_t *in, int32_t phase,
                                                             int32_t freq, int32_t g, int32_t dg,
                                                             const int32_t *tab, int32_t n, int add)
{
    int32_t x, ix, y0, lo;
    int64_t p;
    if (add)
        ASM_FM_MOD(ASM_FM_ADD);
    else
        ASM_FM_MOD(ASM_FM_STORE);
}

#define ASM_FM_FB(OUT)                                                                                  \
    __asm__ volatile("1:\n\t"                                                                           \
                     "%[x] = %[y0] + %[p].l\n\t"                                                        \
                     "%[x] = %[x] >>> %[sh]\n\t"                                                        \
                     "%[g] += %[dg]\n\t"                                                                \
                     "%[y0] = %[p].l\n\t"                                                               \
                     "%[x] += %[ph]\n\t" ASM_FM_LOOKUP_FB OUT "%[ph] += %[fq]\n\t"                      \
                     "if (--%[n] != 0) goto 1b\n\t"                                                     \
                     : [out] "+r"(out), [ph] "+r"(phase), [g] "+r"(g), [n] "+r"(n), [y0] "+r"(y0),     \
                       [p] "+r"(p), [x] "=&r"(x), [ix] "=&r"(ix), [a] "=&r"(a)                          \
                     : [fq] "r"(freq), [dg] "r"(dg), [t0] "r"(tab), [t1] "r"(tab + 1), [sh] "r"(sh)     \
                     : "memory")
/* the lookup of the feedback loop: x is free after its extracts, so it holds the fraction */
#define ASM_FM_LOOKUP_FB                                                                                \
    "%[ix] = uextra(%[x], p:14, l:10)\n\t"                                                              \
    "%[a] = [%[t0]+%[ix]<<2]\n\t"                                                                       \
    "%[ix] = [%[t1]+%[ix]<<2]\n\t"                                                                      \
    "%[ix] = %[ix] - %[a]\n\t"                                                                          \
    "%[x] = uextra(%[x], p:0, l:14)\n\t"                                                                \
    "%[ix] *= %[x]\n\t"                                                                                 \
    "%[ix] = %[ix] >>> 14\n\t"                                                                          \
    "%[ix] += %[a]\n\t"                                                                                 \
    "%[p] = %[ix] * %[g] (s)\n\t"                                                                       \
    "%[p] >>= 24\n\t"

/* the feedback operator (no modulation input): fb[0], fb[1] the last two outputs;
 *   m = (y0 + y) >> (shift + 1); g += dg; y0 = y; y = op(phase + m); out = y or out += y */
static inline __attribute__((always_inline)) void asm_fm_fb(int32_t *out, int32_t phase, int32_t freq,
                                                            int32_t g, int32_t dg, int32_t *fb, int shift,
                                                            const int32_t *tab, int32_t n, int add)
{
    int32_t x, ix, a, y0 = fb[0], sh = shift + 1;
    int64_t p = (int64_t)(uint32_t)fb[1];             /* y: the low word of the product register */
    if (add)
        ASM_FM_FB(ASM_FM_ADD);
    else
        ASM_FM_FB(ASM_FM_STORE);
    fb[0] = y0;
    fb[1] = (int32_t)p;
}

/* FM6 MARK I (Dexed's EngineMkI: the DX7's log-sine and exponent tables), eng_fm6.c fm6_mki; env = the
 * operator's attenuation (1024 an octave), a 16-bit sum whose top bit is the sign:
 *   for each of n (> 0) samples:  g += dg;  x = phase [+ in[i]];
 *       j = (x >> 12) & 2047;  e = log[j < 1024 ? j : 2047 - j] + ((x >> 8) & 0x8000) + g;
 *       y = (exp[e & 1023] >> ((e & 0x7FFF) >> 10)) << 13;  y = e & 0x8000 ? -y - 8192 : y;
 *       out[i] = y  or  out[i] += y;  phase += freq
 * log is a quarter cycle (1024 entries, eng_fm6.c FM6_MKI_FOLD): bit 22 of x (j's top bit) as 0 / -1
 * (sextra), x ^ that flips bits 12..21 when it is set, so its 10 bits there are j or 2047 - j.
 * (e & 0xFFFF is not needed: only bits 0..15 of e are read.) neg8k: -8192 in a register. */
#define ASM_MKI_LOOKUP(X)                                                                               \
    "%[a] = sextra(" X ", p:22, l:1)\n\t"                                                              \
    "%[a] ^= " X "\n\t"                                                                                \
    "%[a] = uextra(%[a], p:12, l:10)\n\t"                                                              \
    "%[a] = h[%[tl]+%[a]<<1] (u)\n\t"                                                                  \
    "%[a] += %[g]\n\t"                                                                                 \
    "%[b] = uextra(" X ", p:23, l:1)\n\t"                                                              \
    "%[b] <<= 15\n\t"                                                                                  \
    "%[a] += %[b]\n\t"                                                                                 \
    "%[b] = uextra(%[a], p:0, l:10)\n\t"                                                               \
    "%[b] = h[%[te]+%[b]<<1] (u)\n\t"                                                                  \
    "%[x] = uextra(%[a], p:10, l:5)\n\t"                                                               \
    "%[b] = %[b] >> %[x]\n\t"                                                                          \
    "%[b] <<= 13\n\t"                                                                                  \
    "%[a] = %[a].l (s)\n\t"                                                                            \
    "%[x] = %[k] - %[b]\n\t"                                                                           \
    "ifs (%[a] >= 0) {\n\t"                                                                            \
    "%[x] = %[b]\n\t"                                                                                  \
    "}\n\t"
#define ASM_MKI_STORE "[%[out]++=4] = %[x]\n\t"
#define ASM_MKI_ADD "[%[out]+0] += %[x]\n\t%[out] += 4\n\t"

#define ASM_MKI_PURE(OUT)                                                                               \
    __asm__ volatile("1:\n\t"                                                                          \
                     "%[g] += %[dg]\n\t" ASM_MKI_LOOKUP("%[ph]") OUT "%[ph] += %[fq]\n\t"             \
                     "if (--%[n] != 0) goto 1b\n\t"                                                    \
                     : [out] "+r"(out), [ph] "+r"(phase), [g] "+r"(g), [n] "+r"(n), [x] "=&r"(x),      \
                       [a] "=&r"(a), [b] "=&r"(b)                                                       \
                     : [fq] "r"(freq), [dg] "r"(dg), [tl] "r"(tlog), [te] "r"(texp), [k] "r"(k)         \
                     : "memory")

static inline __attribute__((always_inline)) void asm_mki_pure(int32_t *out, int32_t phase, int32_t freq, int32_t g,
                                                               int32_t dg, const uint16_t *tlog,
                                                               const uint16_t *texp, int32_t n, int add)
{
    int32_t x, a, b, k = -8192;
    if (add)
        ASM_MKI_PURE(ASM_MKI_ADD);
    else
        ASM_MKI_PURE(ASM_MKI_STORE);
}

/* modulated: x = phase + in[i] (x is free once its two extracts are done: it then holds the shift) */
#define ASM_MKI_MOD(OUT)                                                                                \
    __asm__ volatile("1:\n\t"                                                                          \
                     "%[g] += %[dg]\n\t"                                                               \
                     "%[x] = [%[in]++=4]\n\t"                                                          \
                     "%[x] += %[ph]\n\t" ASM_MKI_LOOKUP("%[x]") OUT "%[ph] += %[fq]\n\t"              \
                     "if (--%[n] != 0) goto 1b\n\t"                                                    \
                     : [out] "+r"(out), [in] "+r"(in), [ph] "+r"(phase), [g] "+r"(g), [n] "+r"(n),     \
                       [x] "=&r"(x), [a] "=&r"(a), [b] "=&r"(b)                                         \
                     : [fq] "r"(freq), [dg] "r"(dg), [tl] "r"(tlog), [te] "r"(texp), [k] "r"(k)         \
                     : "memory")

static inline __attribute__((always_inline)) void asm_mki_mod(int32_t *out, const int32_t *in, int32_t phase,
                                                              int32_t freq, int32_t g, int32_t dg,
                                                              const uint16_t *tlog, const uint16_t *texp,
                                                              int32_t n, int add)
{
    int32_t x, a, b, k = -8192;
    if (add)
        ASM_MKI_MOD(ASM_MKI_ADD);
    else
        ASM_MKI_MOD(ASM_MKI_STORE);
}

/* the feedback operator: m = (y0 + y) >> (shift + 1); g += dg; y0 = y; y = op(phase + m); out = y or += y */
#define ASM_MKI_FB(OUT)                                                                                 \
    __asm__ volatile("1:\n\t"                                                                          \
                     "%[x] = %[y0] + %[y]\n\t"                                                         \
                     "%[x] = %[x] >>> %[sh]\n\t"                                                       \
                     "%[g] += %[dg]\n\t"                                                               \
                     "%[y0] = %[y]\n\t"                                                                \
                     "%[x] += %[ph]\n\t" ASM_MKI_LOOKUP("%[x]")                                       \
                     "%[y] = %[x]\n\t" OUT "%[ph] += %[fq]\n\t"                                      \
                     "if (--%[n] != 0) goto 1b\n\t"                                                    \
                     : [out] "+r"(out), [ph] "+r"(phase), [g] "+r"(g), [n] "+r"(n), [y0] "+r"(y0),     \
                       [y] "+r"(y), [x] "=&r"(x), [a] "=&r"(a), [b] "=&r"(b)                            \
                     : [fq] "r"(freq), [dg] "r"(dg), [tl] "r"(tlog), [te] "r"(texp), [k] "r"(k),        \
                       [sh] "r"(sh)                                                                     \
                     : "memory")

static inline __attribute__((always_inline)) void asm_mki_fb(int32_t *out, int32_t phase, int32_t freq, int32_t g,
                                                             int32_t dg, int32_t *fb, int shift,
                                                             const uint16_t *tlog, const uint16_t *texp,
                                                             int32_t n, int add)
{
    int32_t x, a, b, k = -8192, y0 = fb[0], y = fb[1], sh = shift + 1;
    if (add)
        ASM_MKI_FB(ASM_MKI_ADD);
    else
        ASM_MKI_FB(ASM_MKI_STORE);
    fb[0] = y0;
    fb[1] = y;
}

/* FM6's voice output (eng_fm6.c fm6_render): the voice clipped at 16 unit sines, times the block's amplitude
 * ramp (dsp.c amp_at) and the voice level k (VOICE_FS), into the part's output; for i = 0..n-1 (n > 0):
 *   out[i] += (int32_t)(((int64_t)clamp(in[i], -2^28, 2^28 - 1) * (((a + d * i) >> 5) * k)) >> 40)
 * a = amp0 * 32 (+ 31 when d < 0: amp_at's division rounds towards zero, and with d < 0 every d * i <= 0,
 * so the bias is the same for the whole block). The amplitude ramp then costs one add and one shift. */
static inline __attribute__((always_inline)) void asm_fm6_out(int32_t *out, const int32_t *in, int32_t a, int32_t d,
                                                              int32_t k, int32_t n)
{
    int32_t x, g, lo = -(1 << 28), hi = (1 << 28) - 1;
    int64_t p;
    __asm__ volatile("1:\n\t"
                     "%[x] = [%[in]++=4]\n\t"
                     "%[g] = %[a] >>> 5\n\t"
                     "%[x] = smax(%[x], %[lo])\n\t"
                     "%[x] = smin(%[x], %[hi])\n\t"
                     "%[g] *= %[k]\n\t"
                     "%[p] = %[x] * %[g] (s)\n\t"
                     "%[p] >>>= 40\n\t"
                     "[%[out]+0] += %[p].l\n\t"
                     "%[out] += 4\n\t"
                     "%[a] += %[d]\n\t"
                     "if (--%[n] != 0) goto 1b\n\t"
                     : [out] "+r"(out), [in] "+r"(in), [a] "+r"(a), [n] "+r"(n), [x] "=&r"(x), [g] "=&r"(g),
                       [p] "=&r"(p)
                     : [lo] "r"(lo), [hi] "r"(hi), [d] "r"(d), [k] "r"(k)
                     : "memory");
}
/* ------------------------------------------------------------------ ANALOG 2 (eng_analog2.c) --- */
/* The polyBLEP saw of dsp.c (osc_saw) into a buffer: for each of n (> 0) samples
 *   b[i] += (osc_saw(ph, inc) * g) >> 15;  ph += inc
 * on a biased phase q = ph + 2^31: q >>> 16 is (ph >> 16) - 32768 in one shift, and after q += inc the
 * BLEP window of the sample just taken (ph < inc or ph > ~inc) is exactly q <s INT32_MIN + 2 inc
 * when inc < 2^30 (from fs / 4 up to inc < 2^31 every sample takes the slow path, which tests the window
 * exactly). The window's samples (two a period) take the branch out of the loop: blep() recomputed
 * from ph = q - inc - 2^31 with its divide, the same integer steps as the C. (x * x) >> 15 is a logical
 * shift, as clang compiles blep(): x >= 0, so it takes x * x as non-negative; the product wraps only
 * below about 1.5 Hz (inc < 98304, x up to 65534), where the asm still matches the compiled C.
 * Registers: the operands only.
 * SIMD: a 2 x 16-bit form would run two copies of the swarm per instruction here (the wave is 16 bits,
 * g 16 bits): the lane macros below are what one lane does. */
#define ASM_SAW_LANE(Q, INC, LIM, X, SLOW)                                                              \
    "%[" X "] = %[" Q "] >>> 16\n\t"                                                                    \
    "%[" Q "] += %[" INC "]\n\t"                                                                        \
    "ifs (%[" Q "] < %[" LIM "]) goto " SLOW "\n\t"
/* the BLEP of one lane: X -= blep(ph, inc), back to label BACK (t, d: scratch) */
#define ASM_SAW_BLEP(Q, INC, X, BACK, HI)                                                               \
    "%[t] = %[" Q "] - %[" INC "]\n\t"                                                                  \
    "%[t] = %[t] + 0x80000000\n\t"                    /* ph of the sample */                            \
    "%[d] = %[" INC "] >> 15\n\t"                                                                       \
    "if (%[d] == 0) goto " BACK "\n\t"                /* blep() = 0 */                                  \
    "if (%[t] >= %[" INC "]) goto " HI "f\n\t"                                                           \
    "%[t] = %[t] / %[d] (u)\n\t"                      /* ph < inc: x = ph / d */                        \
    "%[d] = %[t] * %[t]\n\t"                                                                            \
    "%[d] = %[d] >> 15\n\t"                                                                             \
    "%[" X "] = %[" X "] - %[t]\n\t"                  /* - (x + x - ((x * x) >> 15) - 32768) */         \
    "%[" X "] = %[" X "] - %[t]\n\t"                                                                    \
    "%[" X "] += %[d]\n\t"                                                                              \
    "%[" X "] = %[" X "] + 0x8000\n\t"                                                                  \
    "goto " BACK "\n\t"                                                                                 \
    HI ":\n\t"                                                                                          \
    "%[t] = ~%[t]\n\t"                                /* ph > ~inc: x = -(~ph / d) */                   \
    "if (%[t] >= %[" INC "]) goto " BACK "\n\t"           /* (neither: inc >= 2^30, blep() = 0) */          \
    "%[t] = %[t] / %[d] (u)\n\t"                                                                        \
    "%[d] = %[t] * %[t]\n\t"                                                                            \
    "%[d] = %[d] >> 15\n\t"                                                                             \
    "%[" X "] = %[" X "] - %[d]\n\t"                  /* - (((x * x) >> 15) + x + x + 32768) */        \
    "%[" X "] += %[t]\n\t"                                                                              \
    "%[" X "] += %[t]\n\t"                                                                              \
    "%[" X "] = %[" X "] - 0x8000\n\t"                                                                  \
    "goto " BACK "\n\t"

/* the lane's limit: the window test of ASM_SAW_LANE (every sample to the slow path from fs / 4 up) */
static inline __attribute__((always_inline)) int32_t asm_saw_lim(uint32_t inc)
{
    return inc < 0x40000000u ? (int32_t)(0x80000000u + 2u * inc) : INT32_MAX;
}

/* Software-pipelined: the store of sample i rides in the bundle (#: a parallel pair, an ALU primary and
 * a load or store slot, no register shared between the two) of sample i + 1's phase step, the load in
 * the multiply's: 7 instructions a sample. The first sample enters past the store, the last stores
 * after the loop. */
static inline __attribute__((always_inline)) void asm_saw_acc(int32_t *b, uint32_t ph, uint32_t inc, int32_t g,
                                                              uint32_t n)
{
    uint32_t q = ph + 0x80000000u, x, y, t, d;
    int32_t lim = asm_saw_lim(inc);
    __asm__ volatile(ASM_SAW_LANE("q", "inc", "lim", "x", "3f")
                     "goto 2f\n\t"
                     "1:\n\t"
                     "%[x] = %[q] >>> 16\n\t"
                     "%[q] += %[inc] # [%[b]++=4] = %[y]\n\t"
                     "ifs (%[q] < %[lim]) goto 3f\n\t"
                     "2:\n\t"
                     "%[x] *= %[g] # %[y] = [%[b]+0]\n\t"
                     "%[x] = %[x] >>> 15\n\t"
                     "%[y] += %[x]\n\t"
                     "if (--%[n] != 0) goto 1b\n\t"
                     "[%[b]++=4] = %[y]\n\t"
                     "goto 9f\n\t"
                     "3:\n\t" ASM_SAW_BLEP("q", "inc", "x", "2b", "4")
                     "9:\n\t"
                     : [b] "+r"(b), [q] "+r"(q), [n] "+r"(n), [x] "=&r"(x), [y] "=&r"(y), [t] "=&r"(t),
                       [d] "=&r"(d)
                     : [inc] "r"(inc), [lim] "r"(lim), [g] "r"(g)
                     : "memory");
}

/* Two saws of the same gain into b (two copies of the swarm): b[i] += saw1 * g >> 15, then
 * += saw2 * g >> 15, per sample (the same integer sums as one copy after the other). One load and one
 * store a sample for both: 13 instructions a sample, 6.5 a copy. The SIMD slot: lanes 1 and 2 are the
 * two 16-bit halves a packed multiply would take at once. */
static inline __attribute__((always_inline)) void asm_saw2_acc(int32_t *b, uint32_t ph1, uint32_t inc1,
                                                               uint32_t ph2, uint32_t inc2, int32_t g,
                                                               uint32_t n)
{
    uint32_t q1 = ph1 + 0x80000000u, q2 = ph2 + 0x80000000u, x, y, t, d;
    int32_t lim1 = asm_saw_lim(inc1), lim2 = asm_saw_lim(inc2);
    __asm__ volatile(ASM_SAW_LANE("q1", "inc1", "lim1", "x", "3f")
                     "goto 2f\n\t"
                     "1:\n\t"
                     "%[x] = %[q1] >>> 16\n\t"
                     "%[q1] += %[inc1] # [%[b]++=4] = %[y]\n\t"
                     "ifs (%[q1] < %[lim1]) goto 3f\n\t"
                     "2:\n\t"                       /* lane 1 */
                     "%[x] *= %[g] # %[y] = [%[b]+0]\n\t"
                     "%[x] = %[x] >>> 15\n\t"
                     "%[y] += %[x]\n\t" ASM_SAW_LANE("q2", "inc2", "lim2", "x", "5f")
                     "6:\n\t"                       /* lane 2 */
                     "%[x] *= %[g]\n\t"
                     "%[x] = %[x] >>> 15\n\t"
                     "%[y] += %[x]\n\t"
                     "if (--%[n] != 0) goto 1b\n\t"
                     "[%[b]++=4] = %[y]\n\t"
                     "goto 9f\n\t"
                     "3:\n\t" ASM_SAW_BLEP("q1", "inc1", "x", "2b", "4")
                     "5:\n\t" ASM_SAW_BLEP("q2", "inc2", "x", "6b", "7")
                     "9:\n\t"
                     : [b] "+r"(b), [q1] "+r"(q1), [q2] "+r"(q2), [n] "+r"(n), [x] "=&r"(x), [y] "=&r"(y),
                       [t] "=&r"(t), [d] "=&r"(d)
                     : [inc1] "r"(inc1), [lim1] "r"(lim1), [inc2] "r"(inc2), [lim2] "r"(lim2), [g] "r"(g)
                     : "memory");
}

/* The low-pass SVF of eng_analog2.c (A2_SVF, its output v2) in place over n (> 0) samples:
 *   x = b[i] (LP24's second stage: clamped to +-65536);  v3 = x - ic2;
 *   v1 = (a1 ic1 + a2 v3) >> 13;  T = (a2 ic1 + a3 v3) >> 13;  v2 = ic2 + T;
 *   ic1 = 2 v1 - ic1;  ic2 = 2 v2 - ic2 (= v2 + T);  b[i] = v2;  [moving: a1..a3 += d[0..2]]
 * Each sum of two products is a 64-bit multiply-accumulate shifted by 13 (one instruction less than two
 * multiplies and an add); its low word is the C's 32-bit result, since the sums fit 32 bits (|a1|, |a3|
 * <= 8192, |a2| <= 4096, |ic1| < 65536, |v3| < 196608: below 1.9e9). The states' knee (A2_SAT) is the
 * C's: when ic1 + 32768 > 65536 or ic2 + 65536 > 131072 (unsigned), the loop leaves with that sample
 * stored (and the coefficients stepped) but n not counted down; the caller applies the knee and goes on.
 * Returns the samples left (0: all done). 16 instructions a sample (clang's C 20), the clamp +2, moving
 * 19 (C 23): the load of the next sample and the store ride in parallel bundles (# : an ALU primary and
 * a load or store slot, no register shared), as do the coefficient steps' loads. */
#define ASM_SVF_HEAD                                                                                    \
    "%[v] = %[v] - %[ic2]\n\t"                                                                          \
    "%[p] = %[ic1] * %[a1] (s)\n\t"                                                                     \
    "%[p] += %[v] * %[a2] (s)\n\t"                                                                      \
    "%[p] >>= 13\n\t"                                 /* v1 */                                          \
    "%[t] = %[p].l << 1\n\t"                                                                            \
    "%[p] = %[ic1] * %[a2] (s)\n\t"                                                                     \
    "%[p] += %[v] * %[a3] (s)\n\t"                                                                      \
    "%[ic1] = %[t] - %[ic1]\n\t"
#define ASM_SVF_TESTS                                                                                   \
    "if (%[t] > 65536) goto 8f\n\t"                                                                     \
    "%[t] = %[ic2] + 0x10000\n\t"                                                                       \
    "if (%[t] > 131072) goto 8f\n\t"                                                                    \
    "if (--%[n] != 0) goto 1b\n\t"                                                                      \
    "8:\n\t"
#define ASM_SVF_STILL                                                                                   \
    ASM_SVF_HEAD                                                                                        \
    "%[p] >>= 13\n\t"                                 /* T */                                           \
    "%[y] = %[ic2] + %[p].l\n\t"                                                                        \
    "%[ic2] = %[y] + %[p].l\n\t"                                                                        \
    "%[t] = %[ic1] + 0x8000 # [%[b]++=4] = %[y]\n\t"                                                    \
    "if (%[t] > 65536) goto 8f\n\t"                                                                     \
    "%[t] = %[ic2] + 0x10000 # %[v] = [%[b]+0]\n\t"                                                     \
    "if (%[t] > 131072) goto 8f\n\t"                                                                    \
    "if (--%[n] != 0) goto 1b\n\t"                                                                      \
    "8:\n\t"
#define ASM_SVF_MOVING                                                                                  \
    ASM_SVF_HEAD                                                                                        \
    "%[p] >>= 13 # %[t] = [%[d]+0]\n\t"               /* T; d1 */                                       \
    "%[y] = %[ic2] + %[p].l # %[v] = [%[d]+4]\n\t"    /* d2 */                                          \
    "%[ic2] = %[y] + %[p].l\n\t"                                                                        \
    "%[a1] += %[t] # [%[b]++=4] = %[y]\n\t"                                                             \
    "%[a2] += %[v] # %[t] = [%[d]+8]\n\t"             /* d3 */                                          \
    "%[a3] += %[t] # %[v] = [%[b]+0]\n\t"                                                               \
    "%[t] = %[ic1] + 0x8000\n\t" ASM_SVF_TESTS
#define ASM_SVF_CLAMP                                                                                   \
    "%[v] = smin(%[v], %[hi])\n\t"                                                                      \
    "%[v] = smax(%[v], %[lo])\n\t"

/* c: a1, a2, a3 (updated when moving); d: their steps (NULL: still); clamp: LP24's second stage */
static inline __attribute__((always_inline)) uint32_t asm_svf_lp(int32_t **pb, int32_t *pic1, int32_t *pic2,
                                                                 int32_t *c, const int32_t *d, int clamp,
                                                                 uint32_t n)
{
    /* the operands of the parallel slots in r0..r4 (the slot's 16-bit forms take r0..r7 only) */
    register int32_t *b __asm__("r0") = *pb;
    register int32_t v __asm__("r1");
    register int32_t y __asm__("r2");
    register int32_t t __asm__("r3");
    register const int32_t *dp __asm__("r4") = d;
    int32_t ic1 = *pic1, ic2 = *pic2, a1 = c[0], a2 = c[1], a3 = c[2], hi = 65536, lo = -65536;
    int64_t p;
#define ASM_SVF_OPS                                                                                     \
    : [b] "+r"(b), [n] "+r"(n), [ic1] "+r"(ic1), [ic2] "+r"(ic2), [a1] "+r"(a1), [a2] "+r"(a2),          \
      [a3] "+r"(a3), [v] "=&r"(v), [t] "=&r"(t), [y] "=&r"(y), [p] "=&r"(p)
    if (d && clamp)
        __asm__ volatile("%[v] = [%[b]+0]\n\t1:\n\t" ASM_SVF_CLAMP ASM_SVF_MOVING ASM_SVF_OPS
                         : [d] "r"(dp), [hi] "r"(hi), [lo] "r"(lo) : "memory");
    else if (d)
        __asm__ volatile("%[v] = [%[b]+0]\n\t1:\n\t" ASM_SVF_MOVING ASM_SVF_OPS : [d] "r"(dp) : "memory");
    else if (clamp)
        __asm__ volatile("%[v] = [%[b]+0]\n\t1:\n\t" ASM_SVF_CLAMP ASM_SVF_STILL ASM_SVF_OPS
                         : [hi] "r"(hi), [lo] "r"(lo) : "memory");
    else
        __asm__ volatile("%[v] = [%[b]+0]\n\t1:\n\t" ASM_SVF_STILL ASM_SVF_OPS :: "memory");
#undef ASM_SVF_OPS
    *pb = b;
    *pic1 = ic1;
    *pic2 = ic2;
    c[0] = a1;
    c[1] = a2;
    c[2] = a3;
    return n;
}

/* The voice's output stage of eng_analog2.c (a2_out) over n (> 0) samples:
 *   y = b[i];  |y| > 16000: y = sign(y) (16000 + (softclip((|y| - 16000) 2) >> 1))   (the soft knee)
 *   out[i] += mulq15(mulq15(y << 1, acc >> 5), fs) << 1;  acc += d
 * mulq15(y << 1, g) is bits 14..30 of y g, sign-extended (sextra p:14 l:17), as clang compiles it.
 * softclip of a positive t: TANH_Q15[t >> 8] interpolated over t & 255, TANH_Q15[256] from t >= 65536.
 * The knee leaves the loop and comes back (y kneed in place). 11 instructions a sample (clang's C 15):
 * b's next sample loads, out's sample loads and stores in parallel bundles (operands in r0..r3). */
static inline __attribute__((always_inline)) void asm_a2_out(int32_t *out_, const int32_t *b_, int32_t acc,
                                                             int32_t d, int32_t fs, const int16_t *tanh,
                                                             uint32_t n)
{
    register int32_t x __asm__("r0");
    register const int32_t *b __asm__("r1") = b_;
    register int32_t o __asm__("r2");
    register int32_t *out __asm__("r3") = out_;
    int32_t a, g, y;
    __asm__ volatile("%[x] = [%[b]++=4]\n\t"
                     "1:\n\t"
                     "%[a] = abs(%[x]) # %[o] = [%[out]+0]\n\t"
                     "ifs (%[a] > 16000) goto 5f\n\t"
                     "2:\n\t"
                     "%[g] = %[acc] >>> 5\n\t"
                     "%[y] = %[x] * %[g]\n\t"
                     "%[y] = sextra(%[y], p:14, l:17)\n\t"
                     "%[y] *= %[fs] # %[x] = [%[b]++=4]\n\t"
                     "%[y] = %[y] >>> 15\n\t"
                     "%[y] = %[y] << 1\n\t"
                     "%[o] += %[y]\n\t"
                     "%[acc] += %[d] # [%[out]++=4] = %[o]\n\t"
                     "if (--%[n] != 0) goto 1b\n\t"
                     "goto 9f\n\t"
                     "5:\n\t"                                       /* the knee: x from a = |x| */
                     "%[g] = %[a] - 16000\n\t"
                     "%[g] = %[g] << 1\n\t"                         /* t */
                     "if (%[g] >= 65536) goto 6f\n\t"                /* (t > 0) */
                     "%[y] = %[g] >> 8\n\t"
                     "%[a] = h[%[tanh]+%[y]<<1] (s)\n\t"
                     "%[y] += 1\n\t"
                     "%[y] = h[%[tanh]+%[y]<<1] (s)\n\t"
                     "%[y] = %[y] - %[a]\n\t"
                     "%[g] = uextra(%[g], p:0, l:8)\n\t"
                     "%[y] *= %[g]\n\t"
                     "%[y] = %[y] >>> 8\n\t"
                     "%[y] += %[a]\n\t"
                     "goto 7f\n\t"
                     "6:\n\t"
                     "%[y] = 256\n\t"
                     "%[y] = h[%[tanh]+%[y]<<1] (s)\n\t"
                     "7:\n\t"
                     "%[y] = %[y] >>> 1\n\t"
                     "%[y] = %[y] + 16000\n\t"
                     "ifs (%[x] >= 0) goto 3f\n\t"
                     "%[y] = 0 - %[y]\n\t"
                     "3:\n\t"
                     "%[x] = %[y]\n\t"
                     "goto 2b\n\t"
                     "9:\n\t"
                     : [x] "=&r"(x), [b] "+r"(b), [o] "=&r"(o), [out] "+r"(out), [acc] "+r"(acc), [n] "+r"(n),
                       [a] "=&r"(a), [g] "=&r"(g), [y] "=&r"(y)
                     : [d] "r"(d), [fs] "r"(fs), [tanh] "r"(tanh)
                     : "memory");
}

/* ANALOG's drive (a2_drive) in place over n (> 0) samples, k = drive >> 2:
 *   s = b[i];  x = ((s >> 1) k) >> 11;  b[i] = s + mulq15((softclip(x) >> 1) - s, dw)
 * softclip: TANH_Q15 at |x| >> 8 interpolated over |x| & 255 (t1 = TANH_Q15 + 1: both points with one
 * index), TANH_Q15[256] from |x| >= 65536, negated for x < 0 (a conditional instruction, as clang).
 * Pipelined: the next sample loads in a bundle mid-loop, the store rides with the next iteration's
 * first instruction (the last stores after the loop). 22 instructions a sample (clang's C 25). */
static inline __attribute__((always_inline)) void asm_a2_drive(int32_t *b_, int32_t k, int32_t dw,
                                                               const int16_t *tanh, uint32_t n)
{
    register int32_t x __asm__("r0");
    register int32_t *b __asm__("r1") = b_;
    register int32_t y __asm__("r2");
    int32_t s, a, i;
    const int16_t *t1 = tanh + 1;
    __asm__ volatile("%[x] = [%[b]+0]\n\t"
                     "%[s] = %[x]\n\t"
                     "goto 3f\n\t"
                     "1:\n\t"
                     "%[s] = %[x] # [%[b]++=4] = %[y]\n\t"
                     "3:\n\t"
                     "%[x] = %[x] >>> 1\n\t"
                     "%[x] *= %[k]\n\t"
                     "%[x] = %[x] >>> 11\n\t"
                     "%[a] = abs(%[x])\n\t"
                     "if (%[a] >= 65536) goto 5f\n\t"
                     "%[i] = %[a] >> 8\n\t"
                     "%[y] = h[%[t0]+%[i]<<1] (s)\n\t"
                     "%[i] = h[%[t1]+%[i]<<1] (s)\n\t"
                     "%[i] = %[i] - %[y]\n\t"
                     "%[a] = uextra(%[a], p:0, l:8)\n\t"
                     "%[i] *= %[a]\n\t"
                     "%[i] = %[i] >>> 8\n\t"
                     "%[y] += %[i]\n\t"
                     "2:\n\t"
                     "ifs (%[x] < 0) {\n\t%[y] = 0 - %[y]\n\t}\n\t"
                     "%[y] = %[y] >>> 1 # %[x] = [%[b]+4]\n\t"
                     "%[y] = %[y] - %[s]\n\t"
                     "%[y] *= %[dw]\n\t"
                     "%[y] = %[y] >>> 15\n\t"
                     "%[y] += %[s]\n\t"
                     "if (--%[n] != 0) goto 1b\n\t"
                     "[%[b]++=4] = %[y]\n\t"
                     "goto 9f\n\t"
                     "5:\n\t"
                     "%[y] = 256\n\t"
                     "%[y] = h[%[t0]+%[y]<<1] (s)\n\t"
                     "goto 2b\n\t"
                     "9:\n\t"
                     : [x] "=&r"(x), [b] "+r"(b), [y] "=&r"(y), [n] "+r"(n), [s] "=&r"(s), [a] "=&r"(a),
                       [i] "=&r"(i)
                     : [k] "r"(k), [dw] "r"(dw), [t0] "r"(tanh), [t1] "r"(t1)
                     : "memory");
}

/* The interpolated sine of dsp.c (sine_i) into a buffer, n (> 0) samples:
 *   b[i] += (sine_i(ph) * g) >> 15;  ph += inc
 * sine_i: SINE[ph >> 22] + (((SINE[(ph >> 22) + 1 & 1023] - it) * ((ph >> 7) & 0x7FFF)) >> 15); the
 * second index as (ph + 2^22) >> 22 (the wrap for free). b[i] loads with the first index and stores with
 * the phase step, in parallel bundles: 15 instructions a sample (clang's C 16). */
static inline __attribute__((always_inline)) void asm_sin_acc(int32_t *b_, uint32_t ph, uint32_t inc, int32_t g,
                                                              const int16_t *tab, uint32_t n)
{
    register int32_t o __asm__("r0");
    register int32_t *b __asm__("r1") = b_;
    uint32_t i, j;
    int32_t y;
    __asm__ volatile("1:\n\t"
                     "%[i] = %[ph] >> 22 # %[o] = [%[b]+0]\n\t"
                     "%[y] = h[%[t]+%[i]<<1] (s)\n\t"
                     "%[j] = %[ph] + 0x400000\n\t"
                     "%[j] = %[j] >> 22\n\t"
                     "%[j] = h[%[t]+%[j]<<1] (s)\n\t"
                     "%[j] = %[j] - %[y]\n\t"
                     "%[i] = uextra(%[ph], p:7, l:15)\n\t"
                     "%[j] *= %[i]\n\t"
                     "%[j] = %[j] >>> 15\n\t"
                     "%[y] += %[j]\n\t"
                     "%[y] *= %[g]\n\t"
                     "%[y] = %[y] >>> 15\n\t"
                     "%[o] += %[y]\n\t"
                     "%[ph] += %[inc] # [%[b]++=4] = %[o]\n\t"
                     "if (--%[n] != 0) goto 1b\n\t"
                     : [o] "=&r"(o), [b] "+r"(b), [ph] "+r"(ph), [n] "+r"(n), [i] "=&r"(i), [j] "=&r"(j),
                       [y] "=&r"(y)
                     : [inc] "r"(inc), [g] "r"(g), [t] "r"(tab)
                     : "memory");
}

/* PHYS's sympathetic strings (engines/phys/phys_symp.c px_symp_run), one string's comb over m (> 0) half-rate steps:
 *   t = wp + d;  s = h0 l[t & 255] + h1 l[(t + 1) & 255] + h2 l[(t + 2) & 255]   (64-bit)
 *   y = clamp((s / 2^24 rounded toward 0) + x[i], -32767, 32767);  l[wp] = y;  acc[i] += y;  wp = (wp - 1) & 255
 * Rounded toward 0 as the C's (s + (s < 0 ? 2^24 - 1 : 0)) >> 24: q = the low word of s >> 24 (floor; |q| < 2^17,
 * so its sign is s's), plus 1 when s < 0 and its low 24 bits are not 0. The C keeps h0..h2 sign-extended to 64 bits
 * on the stack and works the sum out twice (perf/mots-asm 2026-10-13: ~44 instructions a step); here 33. Forms:
 * those of the operator loops above and of clang's own code for this loop (h[...] (s) loads and stores, the 32x32
 * -> 64 product and its accumulate, ifs blocks, a memory add), no packed form. */
static inline __attribute__((always_inline)) void asm_symp_comb(int16_t *l, uint32_t wp, uint32_t d, int32_t h0,
                                                                int32_t h1, int32_t h2, const int32_t *x,
                                                                int32_t *acc, int32_t m)
{
    uint32_t rd = wp + d;                            /* the read point, stepping down with wp (masked where read) */
    int32_t a, t;
    int64_t p;
    __asm__ volatile("1:\n\t"
                     "%[a] = uextra(%[rd], p:0, l:8)\n\t"
                     "%[a] = h[%[l]+%[a]<<1] (s)\n\t"
                     "%[p] = %[a] * %[h0] (s)\n\t"
                     "%[a] = %[rd] + 1\n\t"
                     "%[a] = uextra(%[a], p:0, l:8)\n\t"
                     "%[a] = h[%[l]+%[a]<<1] (s)\n\t"
                     "%[p] += %[a] * %[h1] (s)\n\t"
                     "%[a] = %[rd] + 2\n\t"
                     "%[a] = uextra(%[a], p:0, l:8)\n\t"
                     "%[a] = h[%[l]+%[a]<<1] (s)\n\t"
                     "%[p] += %[a] * %[h2] (s)\n\t"
                     "%[t] = %[p].l\n\t"
                     "%[t] <<= 8\n\t"
                     "%[p] >>= 24\n\t"
                     "%[a] = 0 - %[t]\n\t"
                     "%[t] |= %[a]\n\t"
                     "%[t] = uextra(%[t], p:31, l:1)\n\t"
                     "%[a] = %[p].l\n\t"
                     "ifs (%[a] < 0) {\n\t"
                     "%[a] += %[t]\n\t"
                     "}\n\t"
                     "%[t] = [%[xp]++=4]\n\t"
                     "%[a] += %[t]\n\t"
                     "%[t] = 32767\n\t"
                     "%[a] = smin(%[a], %[t])\n\t"
                     "%[t] = -32767\n\t"
                     "%[a] = smax(%[a], %[t])\n\t"
                     "h[%[l]+%[wp]<<1] = %[a]\n\t"
                     "[%[ap]+0] += %[a]\n\t"
                     "%[ap] += 4\n\t"
                     "%[a] = %[wp] + 255\n\t"
                     "%[wp] = uextra(%[a], p:0, l:8)\n\t"
                     "%[rd] += -1\n\t"
                     "if (--%[n] != 0) goto 1b\n\t"
                     : [wp] "+r"(wp), [rd] "+r"(rd), [xp] "+r"(x), [ap] "+r"(acc), [n] "+r"(m), [a] "=&r"(a),
                       [t] "=&r"(t), [p] "=&r"(p)
                     : [l] "r"(l), [h0] "r"(h0), [h1] "r"(h1), [h2] "r"(h2)
                     : "memory");
}

/* PHYS's main string (engines/phys/phys_dsp.c px_string_run), the curved bridge (B->curved: the nonlinear models'
 * strings), over n (> 0) samples. K: a frame of the loop's values in memory (px_srk_t below, the offsets fixed):
 * the line, delay x comp (per block), the curving, the one-pole's G, the resampler's ratio, then the state (bridge,
 * DcBlock x / y / e, the low-pass; the last two outputs and the write index, kept in registers in the loop) and two
 * scratch words. Per sample, as the C:
 *   ein += in;  src += ratio;  a string step when src > 2^30 (unsigned: src -= 2^30;
 *     dl = (dl0 (2^30 - (bridge curving >> 20))) >> 30 (unsigned);  s = the Hermite read at wp + dl;
 *     bridge = |s| > 26214 ? (s > 0 ? 2 : -3) (|s| - 26214) : 0;  s = clamp(s + ein, +-20 2^20), ein = 0;
 *     DcBlock (acc = 1073498345 dcy + dce, 64 bit: y = s - dcx + acc >> 30, dce = acc & (2^30 - 1));
 *     the one-pole (v = G (s - lp) >> 30, s = lp + v, lp = s + v);  line[wp] = s, wp - 1;  o1 = o0, o0 = s)
 *   out = o1 + ((o0 - o1) src) >> 30
 * The Hermite read as the C's (DelayLine::ReadHermite) with its int64 y held in 32 bits: every product is a 32x32
 * -> 64 one and each sum fits (the line holds the DcBlock's and the one-pole's outputs of a value clamped to
 * +-20 2^20: |x| < 2^26, so |a f >> 16| < 2^29, |y| < 2^30), the same values. Measured: bench.c 16 (every curved
 * string at full excitation, low damping, random sweeps, 20 s) peaks at 33,518,553 (2^25.0), and renders the same
 * samples as FELUCCA_ASM=0's. Forms: those of the operator loops and
 * of clang's own code for this loop (the unsigned compare with its immediate, abs, the (s) and (u) 32x32 -> 64
 * products, the memory frame); no packed form. ~112 instructions a stepping sample (clang: ~124) */
typedef struct {
    int32_t *line;               /* 0 */
    uint32_t dl0;                /* 4: B->delay B->comp >> 30 */
    int32_t curving, G, ratio;   /* 8, 12, 16 */
    int32_t bridge, dcx, dcy, dce, lp, o0, o1;   /* 20 .. 44 */
    uint32_t wp;                 /* 48 */
    int32_t c, x0;               /* 52, 56: scratch */
} px_srk_t;
static inline __attribute__((always_inline)) void asm_px_string_curved(px_srk_t *K, const int32_t *in, int32_t *out,
                                                                       int32_t n, int32_t *ein, uint32_t *src)
{
    int32_t e = *ein, o0 = K->o0, o1 = K->o1, a, b, c, d, f;
    uint32_t wp = K->wp;
    uint32_t sr = *src;
    int64_t p;
    __asm__ volatile("1:\n\t"
                     "%[a] = [%[in]++=4]\n\t"
                     "%[e] += %[a]\n\t"
                     "%[a] = [%[K]+16]\n\t"
                     "%[sr] += %[a]\n\t"
                     "if (%[sr] <= 1073741824) goto 2f\n\t"
                     "%[sr] = %[sr] + 0xC0000000\n\t"
                     /* dl */
                     "%[a] = [%[K]+20]\n\t"
                     "%[b] = [%[K]+8]\n\t"
                     "%[p] = %[a] * %[b] (s)\n\t"
                     "%[p] >>= 20\n\t"
                     "%[a] = %[p].l\n\t"
                     "%[b] = 0x40000000 - %[a]\n\t"
                     "%[a] = [%[K]+4]\n\t"
                     "%[p] = %[a] * %[b] (u)\n\t"
                     "%[p] >>= 30\n\t"
                     "%[d] = %[p].l\n\t"
                     /* the Hermite read at wp + (dl >> 16), its fraction f = dl & 0xFFFF */
                     "%[a] = %[d] >> 16\n\t"
                     "%[a] += %[wp]\n\t"
                     "%[f] = [%[K]+0]\n\t"
                     "%[b] = %[a] + 511\n\t"
                     "%[b] = uextra(%[b], p:0, l:9)\n\t"
                     "%[b] = [%[f]+%[b]<<2]\n\t"
                     "%[c] = %[a] + 1\n\t"
                     "%[c] = uextra(%[c], p:0, l:9)\n\t"
                     "%[c] = [%[f]+%[c]<<2]\n\t"
                     "%[b] = %[c] - %[b]\n\t"
                     "%[b] = %[b] >>> 1\n\t"
                     "[%[K]+52] = %[b]\n\t"
                     "%[b] = uextra(%[a], p:0, l:9)\n\t"
                     "%[b] = [%[f]+%[b]<<2]\n\t"
                     "[%[K]+56] = %[b]\n\t"
                     "%[c] = %[b] - %[c]\n\t"
                     "%[a] = %[a] + 2\n\t"
                     "%[a] = uextra(%[a], p:0, l:9)\n\t"
                     "%[a] = [%[f]+%[a]<<2]\n\t"
                     "%[a] = %[a] - %[b]\n\t"
                     "%[a] = %[a] >>> 1\n\t"
                     "%[b] = [%[K]+52]\n\t"
                     "%[b] += %[c]\n\t"
                     "%[a] += %[c]\n\t"
                     "%[a] += %[b]\n\t"
                     "%[b] += %[a]\n\t"
                     "%[d] = uextra(%[d], p:0, l:16)\n\t"
                     "%[p] = %[a] * %[d] (s)\n\t"
                     "%[p] >>= 16\n\t"
                     "%[a] = %[p].l\n\t"
                     "%[a] = %[a] - %[b]\n\t"
                     "%[p] = %[a] * %[d] (s)\n\t"
                     "%[p] >>= 16\n\t"
                     "%[a] = %[p].l\n\t"
                     "%[b] = [%[K]+52]\n\t"
                     "%[a] += %[b]\n\t"
                     "%[p] = %[a] * %[d] (s)\n\t"
                     "%[p] >>= 16\n\t"
                     "%[a] = %[p].l\n\t"
                     "%[b] = [%[K]+56]\n\t"
                     "%[a] += %[b]\n\t"
                     /* the bridge */
                     "%[b] = abs(%[a])\n\t"
                     "%[c] = -26214\n\t"
                     "%[b] += %[c]\n\t"
                     "%[c] = 2\n\t"
                     "ifs (%[a] <= 0) {\n\t"
                     "%[c] = -3\n\t"
                     "}\n\t"
                     "%[c] = %[b] * %[c]\n\t"
                     "ifs (%[b] < 1) {\n\t"
                     "%[c] = 0\n\t"
                     "}\n\t"
                     "[%[K]+20] = %[c]\n\t"
                     /* + the excitation, clamped */
                     "%[a] += %[e]\n\t"
                     "%[e] = 0\n\t"
                     "%[b] = 20971520\n\t"
                     "%[a] = smin(%[a], %[b])\n\t"
                     "%[b] = -20971520\n\t"
                     "%[a] = smax(%[a], %[b])\n\t"
                     /* DcBlock */
                     "%[b] = [%[K]+28]\n\t"
                     "%[c] = 1073498345\n\t"
                     "%[p] = %[b] * %[c] (s)\n\t"
                     "%[b] = [%[K]+32]\n\t"
                     "%[c] = 1\n\t"
                     "%[p] += %[b] * %[c] (s)\n\t"
                     "%[b] = %[p].l\n\t"
                     "%[b] = %[b] & 0x3FFFFFFF\n\t"
                     "[%[K]+32] = %[b]\n\t"
                     "%[p] >>= 30\n\t"
                     "%[b] = %[p].l\n\t"
                     "%[c] = [%[K]+24]\n\t"
                     "[%[K]+24] = %[a]\n\t"
                     "%[b] += %[a]\n\t"
                     "%[b] = %[b] - %[c]\n\t"
                     "[%[K]+28] = %[b]\n\t"
                     /* the one-pole */
                     "%[c] = [%[K]+36]\n\t"
                     "%[a] = %[b] - %[c]\n\t"
                     "%[d] = [%[K]+12]\n\t"
                     "%[p] = %[d] * %[a] (s)\n\t"
                     "%[p] >>= 30\n\t"
                     "%[a] = %[p].l\n\t"
                     "%[b] = %[c] + %[a]\n\t"
                     "%[c] = %[b] + %[a]\n\t"
                     "[%[K]+36] = %[c]\n\t"
                     /* the line, the outputs */
                     "[%[f]+%[wp]<<2] = %[b]\n\t"
                     "%[d] = %[wp] + 511\n\t"
                     "%[wp] = uextra(%[d], p:0, l:9)\n\t"
                     "%[o1] = %[o0]\n\t"
                     "%[o0] = %[b]\n\t"
                     "2:\n\t"
                     "%[a] = %[o0] - %[o1]\n\t"
                     "%[p] = %[a] * %[sr] (s)\n\t"
                     "%[p] >>= 30\n\t"
                     "%[a] = %[p].l\n\t"
                     "%[a] += %[o1]\n\t"
                     "[%[out]++=4] = %[a]\n\t"
                     "if (--%[n] != 0) goto 1b\n\t"
                     : [in] "+r"(in), [out] "+r"(out), [n] "+r"(n), [e] "+r"(e), [sr] "+r"(sr), [o0] "+r"(o0),
                       [o1] "+r"(o1), [wp] "+r"(wp), [a] "=&r"(a),
                       [b] "=&r"(b), [c] "=&r"(c), [d] "=&r"(d), [f] "=&r"(f), [p] "=&r"(p)
                     : [K] "r"(K)
                     : "memory");
    *ein = e;
    *src = sr;
    K->o0 = o0, K->o1 = o1, K->wp = wp;
}

#if FELUCCA_SIMD
/* EXPERIMENTAL (FELUCCA_SIMD, hal/fm1_simd.h: the packed 16-bit forms, their meaning inferred, the
 * emulator's): asm_saw2_acc with its two lanes packed. p = pack(q1.h, q2.h) takes both saw values at
 * once (q >>> 16 of each biased phase); one dual Q15 multiply by g in both halves gives (x1 g) >> 15 and
 * (x2 g) >> 15 (floored as the C's >> 15; |x| <= 32768 and |g| < 32768: no saturation); the high half
 * and the sign-extended low half add into the sample. A lane in its BLEP window takes the scalar BLEP on
 * its half (x - blep() stays within 16 bits for inc < 2^31: blep() lies between -32768 and 0 where the
 * saw is near -32768, between 0 and 32768 where it is near 32767) and packs it back. 11 instructions a
 * sample for both copies (the scalar pair: 13). The caller keeps |g| < 32768 (else asm_saw2_acc). */
static inline __attribute__((always_inline)) void asm_saw2_pk(int32_t *b_, uint32_t ph1, uint32_t inc1,
                                                              uint32_t ph2, uint32_t inc2, int32_t g,
                                                              uint32_t n)
{
    register int32_t *b __asm__("r0") = b_;
    register int32_t y __asm__("r1");
    uint32_t q1 = ph1 + 0x80000000u, q2 = ph2 + 0x80000000u, x, t, d, p;
    int32_t lim1 = asm_saw_lim(inc1), lim2 = asm_saw_lim(inc2);
    uint32_t gg = ((uint32_t)g << 16) | ((uint32_t)g & 0xFFFFu);
    __asm__ volatile("%[p] = pack(%[q1].h, %[q2].h)\n\t"
                     "goto 2f\n\t"
                     "1:\n\t"
                     "%[p] = pack(%[q1].h, %[q2].h) # [%[b]++=4] = %[y]\n\t"
                     "2:\n\t"
                     "%[q1] += %[inc1] # %[y] = [%[b]+0]\n\t"
                     "ifs (%[q1] < %[lim1]) goto 3f\n\t"
                     "4:\n\t"
                     "%[q2] += %[inc2]\n\t"
                     "ifs (%[q2] < %[lim2]) goto 5f\n\t"
                     "6:\n\t"
                     "%[p] = %[p].h,%[p].l *|* %[gg].h,%[gg].l (ssat,x2)\n\t"
                     "%[x] = %[p] >>> 16\n\t"
                     "%[y] += %[x]\n\t"
                     "%[x] = %[p].l (s)\n\t"
                     "%[y] += %[x]\n\t"
                     "if (--%[n] != 0) goto 1b\n\t"
                     "[%[b]++=4] = %[y]\n\t"
                     "goto 9f\n\t"
                     "3:\n\t"                                       /* lane 1 (high) in its window */
                     "%[x] = %[p] >>> 16\n\t" ASM_SAW_BLEP("q1", "inc1", "x", "7f", "8")
                     "7:\n\t"
                     "%[p] = pack(%[x].l, %[p].l)\n\t"
                     "goto 4b\n\t"
                     "5:\n\t"                                       /* lane 2 (low) */
                     "%[x] = %[p].l (s)\n\t" ASM_SAW_BLEP("q2", "inc2", "x", "7f", "8")
                     "7:\n\t"
                     "%[p] = pack(%[p].h, %[x].l)\n\t"
                     "goto 6b\n\t"
                     "9:\n\t"
                     : [b] "+r"(b), [y] "=&r"(y), [q1] "+r"(q1), [q2] "+r"(q2), [n] "+r"(n), [x] "=&r"(x),
                       [t] "=&r"(t), [d] "=&r"(d), [p] "=&r"(p)
                     : [inc1] "r"(inc1), [lim1] "r"(lim1), [inc2] "r"(inc2), [lim2] "r"(lim2), [gg] "r"(gg)
                     : "memory");
}
#endif /* FELUCCA_SIMD */
#endif /* FELUCCA_ASM */
#endif /* FM1_DSP_ASM_H */
