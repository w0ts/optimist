/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * (a block taken from another project's file brings its author's line here, and its origin is named at the block;
 * the files whose copies a block replaced keep their own licence: they now call the GPL-3.0 block, docs/DSP-SHARED.md) */
/* Integer DSP primitives shared by every engine, effect and drum source, with no table and no firmware header:
 * libc.c, dsp.c and the float units of the X0X ports (x0x/x0x_drums.c, acid/acid_dsp.c) include it (docs/DSP-SHARED.md
 * lists what each block replaced and the proof that every caller's output stayed bit for bit the same).
 * Everything is always inlined, as the copies it replaces were (or compiled to the same code). Organised by kind:
 *   random: xorshift32, lfsr15
 *   clamps and rounding: clamp, mul_tz, crush_tz
 *   filters: lowcut_ef */
#ifndef FELUCCA_DSP_COMMON_H
#define FELUCCA_DSP_COMMON_H
#include <stdint.h>

#define DSP_INL static inline __attribute__((always_inline))

/* ---- random ---------------------------------------------------------------------------------------------- */

/* Marsaglia's xorshift32 (13, 17, 5): the next state, which is also the output. A zero state stays zero.
 * The body is dsp.c's noise32 (Felucca). It replaced identical copies: dsp.c noise32, libc.c rng (Felucca), phys_dsp.c px_rand (PHYS, from Felucca 1.0),
 * eng_acid.c tb_rng (TB-3PO, from X0X), eng_formant.c's RAND vowel (inline), cz_native.c's ring noise (CZ, from
 * Melodee: inline), x0x/drum909_dsp.h d9_noise (9W9's wa_noise, from X0X) */
DSP_INL uint32_t xorshift32(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *s = x;
}

/* a 15-bit LFSR clocked once: bit 0 ^ bit 1 into bit 14 (the NES noise channel's long mode). Was: eng_lofi.c's
 * NES noise (inline), drum_synth.c's CHIP noise (inline) */
DSP_INL uint32_t lfsr15(uint32_t l) { return (l >> 1) | (((l ^ (l >> 1)) & 1u) << 14); }

/* ---- clamps and rounding --------------------------------------------------------------------------------- */

/* Was dsp.c's (Felucca); now also PHYS's px_clamp (phys_dsp.c) and the USB capture's ua_clip */
DSP_INL int32_t clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

/* a * b / 2^15 rounded toward zero (a loop gain that shrinks every non-zero value: no offset held for ever).
 * Was fx.c's mul_tz (the delay and reverb loops) and spring.c's loop gain (inline) */
DSP_INL int32_t mul_tz(int32_t a, int32_t b)
{
    int32_t p = a * b;
    return (p < 0 ? p + 0x7FFF : p) >> 15;            /* (= (p + ((p >> 31) & 0x7FFF)) >> 15: an add and a select,
                                                         * less RAM code in mix_block) */
}

/* v with its low `shift` bits cleared, toward zero (no DC from tails). Was fx.c's crush_bits (DUST) and
 * punch.c's CRUSH (shift 11, inline) */
DSP_INL int32_t crush_tz(int32_t v, int32_t shift)
{
    return v >= 0 ? (v >> shift) << shift : -((-v >> shift) << shift);
}

/* ---- filters --------------------------------------------------------------------------------------------- */

/* x minus its one-pole low-pass, coefficient 2^-shift, the step's remainder kept in *err (no dead band: the state
 * follows the input exactly, down to 0). Was fx.c's lowcut1 (shift 6, the menu's LOWCUT) and bassplus.c's lowcut5
 * (shift 5). spring.c keeps its inline copy (the same bits, tests/dsp_shared_test.c): a pointer to its struct's
 * fields stops the compiler splitting the struct, 184 B of flash */
DSP_INL int32_t lowcut_ef(int32_t x, int32_t *lc, int32_t *err, int shift)
{
    int32_t e = x - *lc + *err, d = e >> shift;
    *err = e - (d << shift);
    *lc += d;
    return x - *lc;
}
#endif /* FELUCCA_DSP_COMMON_H */
