/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 SLOOP */
/* EXPERIMENTAL: the packed 16-bit ("SIMD") instructions of pi32v2, for one DSP kernel.
 *
 * JieLi's clang assembles these forms for the AC79 core (-mcpu=r3, no feature flag) but never emits
 * them, and no JieLi document describes them: the forms and their encodings come from the vendor
 * objdump, their meaning is INFERRED (Blackfin conventions, which the syntax copies) and is what the
 * emulator implements (fm1-emulator src/simd.rs). The SDK itself (apps/common/jl_math/jl_math.c) uses
 * the dual multiply `rD_rC = rA.h,rA.l *|* rB.h,rB.l (ssat)` on this CPU, and its prebuilt wl82
 * libraries (image, face detection, QR, an AEC) contain `rD.l = rA.h + rA.l`, dual and quad adds and
 * `rD = rA.h * imm (ssat)`; the Q15 form e543 below appears in none of them. Whether the FM-1's core runs
 * the forms below is NOT known: FELUCCA_SIMD_PROBE builds test it at boot (src/simd_probe.c).
 *
 * FELUCCA_SIMD: 1 = sine_i (dsp.c) as asm_sine_pk below; 0 = the C (the default, and the only choice
 * on any other compiler). FELUCCA_SIMD_CHECK=1 (verification, not for release): every call also runs
 * the C; simd_check counts calls and differences. FELUCCA_SIMD_PROBE=1 (implies FELUCCA_SIMD): the
 * boot probe, and the asm only if the probe passed.
 *
 * The forms (x: the instruction's second halfword; rD 15:12, rB 11:8, rA 7:4, then mode bits):
 *   e543  rD.l = rA.h * rB.l (ssat,x2)   Q15 product: (a * b) >> 15 floored, saturated to 16 bits
 *   e500  rD.l = rA.l + rB.l             16-bit add, wraps; rD.h kept
 * and, in the probe only, e500 (subtract), e541 (ssat), e551 / e553 (into a word), e404 (pack),
 * e519 (dual add / subtract, ssat) and e569 / e56b (dual multiply). */
#ifndef FM1_SIMD_H
#define FM1_SIMD_H
#include <stdint.h>

#ifndef FELUCCA_SIMD_PROBE
#define FELUCCA_SIMD_PROBE 0
#endif
#ifndef FELUCCA_SIMD
#define FELUCCA_SIMD FELUCCA_SIMD_PROBE
#endif
#ifndef FELUCCA_SIMD_CHECK
#define FELUCCA_SIMD_CHECK 0
#endif
#if FELUCCA_SIMD && !defined(__PI32V2__)
#error "FELUCCA_SIMD=1 needs the pi32v2 target (JieLi clang)"
#endif
#if FELUCCA_SIMD_PROBE && !FELUCCA_SIMD
#error "FELUCCA_SIMD_PROBE needs FELUCCA_SIMD"
#endif
#if FELUCCA_SIMD_CHECK && !FELUCCA_SIMD
#error "FELUCCA_SIMD_CHECK needs FELUCCA_SIMD"
#endif

#if FELUCCA_SIMD
/* sine_i of dsp.c over a packed table, tab[i] = (SINE[i + 1] - SINE[i]) << 16 | (uint16_t)SINE[i]
 * (index 1023 closes the cycle with SINE[0]):
 *   i = ph >> 22;  f = (ph >> 7) & 0x7FFF;  y = a + ((d * f) >> 15)   (a, d: the halves of tab[i])
 * One load instead of two, no index mask, the product and its shift in one instruction: 6 instructions
 * instead of 10. Exact: |d| <= 201 (SINE), so d * f >> 15 lies between 0 and d, a + it between a and the next
 * point (16 bits, the add cannot wrap), and the Q15 product floors as the C's >> 15 does. */
static inline __attribute__((always_inline)) int32_t asm_sine_pk(uint32_t ph, const uint32_t *tab)
{
    int32_t ix, w, f;
    __asm__("%[ix] = %[ph] >> 22\n\t"
            "%[w] = [%[t]+%[ix]<<2]\n\t"
            "%[f] = uextra(%[ph], p:7, l:15)\n\t"
            "%[f].l = %[w].h * %[f].l (ssat,x2)\n\t"
            "%[w].l = %[w].l + %[f].l\n\t"
            "%[ix] = %[w].l (s)\n\t"
            : [ix] "=&r"(ix), [w] "=&r"(w), [f] "=&r"(f)
            : [ph] "r"(ph), [t] "r"(tab), "m"(*(const uint32_t(*)[1024])tab));
    return ix;
}
#endif

#if FELUCCA_SIMD_PROBE
/* One instruction each, d = form(a, b, d): every form the emulator implements. noinline: the probe
 * records which form it is about to run, so a trap (an unimplemented instruction) names it. */
#define FM1_SIMD_FORM(name, text)                                                                       \
    static __attribute__((noinline)) uint32_t name(uint32_t a, uint32_t b, uint32_t d)                  \
    {                                                                                                   \
        __asm__ volatile(text : [d] "+r"(d) : [a] "r"(a), [b] "r"(b));                                  \
        return d;                                                                                       \
    }
FM1_SIMD_FORM(simd_qmul16x2_l, "%[d].l = %[a].h * %[b].l (ssat,x2)")          /* e543 (the kernel's) */
FM1_SIMD_FORM(simd_add16_l, "%[d].l = %[a].l + %[b].l")                       /* e500 (the kernel's) */
FM1_SIMD_FORM(simd_sub16_l, "%[d].l = %[a].l - %[b].h")                       /* e500, subtract */
FM1_SIMD_FORM(simd_qmul16_l, "%[d].l = %[a].h * %[b].l (ssat)")               /* e541 */
FM1_SIMD_FORM(simd_qmul16_w, "%[d] = %[a].l * %[b].h (ssat)")                 /* e551 */
FM1_SIMD_FORM(simd_qmul16x2_w, "%[d] = %[a].l * %[b].h (ssat,x2)")            /* e553 */
FM1_SIMD_FORM(simd_pack, "%[d] = pack(%[a].l, %[b].h)")                       /* e404 */
FM1_SIMD_FORM(simd_qadd16_2, "%[d] = %[a].h,%[a].l +|+ %[b].h,%[b].l (ssat)") /* e519 */
FM1_SIMD_FORM(simd_qaddsub16_2, "%[d] = %[a].h,%[a].l +|- %[b].h,%[b].l (ssat)")
FM1_SIMD_FORM(simd_qmul16_2, "%[d] = %[a].h,%[a].l *|* %[b].h,%[b].l (ssat)")    /* e569 */
FM1_SIMD_FORM(simd_qmul16x2_2, "%[d] = %[a].h,%[a].l *|* %[b].h,%[b].l (ssat,x2)") /* e56b */
#undef FM1_SIMD_FORM
#endif
#endif /* FM1_SIMD_H */
