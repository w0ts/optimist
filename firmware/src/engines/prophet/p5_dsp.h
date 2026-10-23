/* SPDX-License-Identifier: GPL-3.0-only */
/* PROPHET's voice (p5_dsp.c: float, its own unit with the FPU flags, as ANALOG 2's filter models): one entry point */
#ifndef P5_DSP_H
#define P5_DSP_H
#include <stdint.h>

/* what one block of a voice needs, worked out by eng_prophet.c from the part and the voice's modulation: integers only
 * (eng_prophet.c is in the integer unit; every float is p5_dsp.c's) */
typedef struct {
    uint32_t inc_a, inc_b;          /* the oscillators' base increments */
    uint32_t pw_a, pw_b;            /* their pulse widths, of 2^32 */
    int32_t mix;                    /* the mixer: -64 A alone, 0 both at full, 63 B alone */
    int32_t pmod;                   /* Poly-Mod from B: 1..63 on A's pitch, -1..-64 on the cutoff */
    int32_t cut0, cut1;             /* the cutoff over the block, 0..127 << 8 (the CUTOFF_HZ scale) */
    int32_t res;                    /* the resonance, 0..127 */
    int32_t scale;                  /* the output scale (the voice level: a full-scale wave gives about this) */
    int32_t amp0, amp1;             /* the amplitude over the block, Q15 */
    uint8_t saw_a, pulse_a, sync;   /* oscillator A's waves, hard sync to B */
    uint8_t saw_b, tri_b, pulse_b;  /* oscillator B's waves */
} p5_args_t;

/* n samples of a voice added into out; ph[0..1] the oscillators' phases, st[0..5] its float state (stored as bits) */
void p5_render(int32_t *out, uint32_t n, uint32_t *ph, int32_t *st, const p5_args_t *a);
#endif
