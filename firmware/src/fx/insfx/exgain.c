/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 the Optimist contributors */
/* EXGN (FX type 29, FELUCCA_EX_GAIN, off by default): the example insert effect of CONTRIBUTING.md, "Add an insert
 * effect". A gain: the slot's one amount, GAIN (core.h P_TEXGN), 0 OFF (bypassed: the sound as without), else LEVEL's
 * curve, -55.5 dB at 1 .. 0 dB at 112 .. +7.5 dB at 127 (felucca_tables.h LEVEL_Q12; the card shows the dB). On the
 * tracks' insert slots S1 S2 and the drum bus's (fx_slots.c: not the master, the returns, the USB input nor a drum
 * sound). Included by insfx.c, whose ifx_w (the state's "acting" word), ifx_run (the call below) and ifx_quiet_part
 * (back to rest) each have a case FXT_EXGN.
 *
 * What every insert here does, so does this one (the reviewers check it):
 *   - at 0, once at rest, nothing runs: the block comes out bit for bit as it went in (tests/examples_test.c checks);
 *   - no click: a change of the amount (to and from OFF too) glides, here one step a block (dsp.c glide_next, ~10 ms);
 *   - integers only, no divide by a variable (tools/div_audit.py), no state a static per call: one state an instance
 *     (IFX_NI: the parts, then the drum bus's L and R and the other stereo owners);
 *   - b holds the samples with headroom (a part's << 3, the bus's << 2): a gain may go past full scale here, the mix's
 *     clamp is later. [INSERT] marks what a new insert has to have. */
#if FELUCCA_EX_GAIN
typedef struct {
    int32_t g;                                          /* [INSERT] the gain now, Q12 (4096: 0 dB); 0: at rest (bypassed) */
} ifx_exgn_t;
static ifx_exgn_t ifx_exgn[IFX_NI];                     /* [INSERT] one state an instance (insfx.c IFX_NI) */
#define EXGN_UNITY 4096                                 /* 0 dB, Q12 (LEVEL_Q12[112]) */

/* [INSERT] the block b (n samples) at amount a (0..127; 0: OFF, gliding back to 0 dB, then at rest) */
static HOT2 __attribute__((noinline)) void ifx_exgn_run(ifx_exgn_t *s, int32_t a, int32_t *b, uint32_t n)
{
    int32_t to = a ? LEVEL_Q12[a & 127] : EXGN_UNITY, g0 = s->g ? s->g : EXGN_UNITY, g1;
    uint32_t i;
    if (!a && !s->g)
        return;                                         /* OFF and at rest: nothing to do (bit for bit) */
    g1 = glide_next(g0, to);                            /* the block's step towards the target (no zipper, no click) */
    for (i = 0; i < n; i++) {                           /* a ramp from g0 to g1 over the block, then b x gain */
        int32_t g = g0 + (((g1 - g0) * (int32_t)i) >> CTL_LOG2);
        b[i] = (b[i] >> 12) * g + (((b[i] & 4095) * g) >> 12);   /* b x g / 4096 without a 64-bit multiply: no
                                                        * overflow while |b| < 2^29 (g < 2^14); exactly b at 4096 */
    }
    s->g = !a && g1 == EXGN_UNITY ? 0 : g1;             /* back at 0 dB with the amount at OFF: at rest */
}
#endif
