/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 SLOOP */
/* Hand-written pi32v2 inner loops for the hottest DSP kernels (measured with the emulator's FM1_HOT
 * profile). From perf/asm-hotspots (written for the DX7 engine's msfa operator); in this branch they run
 * FM6's operators (eng_fm6.c): MODERN is the same msfa operator (asm_fm_*), MARK I has its own (asm_mki_*). Each helper computes exactly what the C loop it replaces computes (same operations, same
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
 *       e = log[(x >> 12) & 2047] + ((x >> 8) & 0x8000) + g;
 *       y = (exp[e & 1023] >> ((e & 0x7FFF) >> 10)) << 13;  y = e & 0x8000 ? -y - 8192 : y;
 *       out[i] = y  or  out[i] += y;  phase += freq
 * (e & 0xFFFF is not needed: only bits 0..15 of e are read.) neg8k: -8192 in a register. */
#define ASM_MKI_LOOKUP(X)                                                                               \
    "%[a] = uextra(" X ", p:12, l:11)\n\t"                                                             \
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

/* out[i] += (int32_t)(((int64_t)in[i] * (g0 + ((d * i) >> 5))) >> 26), i = 0..n-1 (d * i wraps as int32):
 * a voice buffer into the part's output with a gain ramp over a 32-sample block */
static inline __attribute__((always_inline)) void asm_ramp_mix26(int32_t *out, const int32_t *in, int32_t g0,
                                                                 int32_t d, int32_t n)
{
    int32_t acc = 0, x, g;
    int64_t p;
    __asm__ volatile("1:\n\t"
                     "%[x] = [%[in]++=4]\n\t"
                     "%[g] = %[acc] >>> 5\n\t"
                     "%[g] += %[g0]\n\t"
                     "%[p] = %[x] * %[g] (s)\n\t"
                     "%[p] >>= 26\n\t"
                     "[%[out]+0] += %[p].l\n\t"
                     "%[out] += 4\n\t"
                     "%[acc] += %[d]\n\t"
                     "if (--%[n] != 0) goto 1b\n\t"
                     : [out] "+r"(out), [in] "+r"(in), [acc] "+r"(acc), [n] "+r"(n), [x] "=&r"(x),
                       [g] "=&r"(g), [p] "=&r"(p)
                     : [g0] "r"(g0), [d] "r"(d)
                     : "memory");
}
#endif /* FELUCCA_ASM */
#endif /* FM1_DSP_ASM_H */
