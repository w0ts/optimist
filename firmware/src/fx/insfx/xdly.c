/* SPDX-License-Identifier: GPL-3.0-only */
/* XDLY (2026-10-22; the user: "a cross audio rate modulated dual delay like in lyra8 as an FX"): two delay lines whose
 * outputs move each other's time at the audio rate, after the SOMA LYRA-8's modulated delay (its idea only: no circuit,
 * no code). A limited insert type (fxi.c, a builder count FELUCCA_INS_XDLY), on the tracks' slots and the master's.
 * Included by fxi_kern.c.
 *   TIME 1, TIME 2  each line's time, 1 ms .. its line (exponential, fx_slots.c XD_D), followed over ~23 ms: turning one
 *                   bends the repeats' pitch, as a tape's
 *   MOD             line 1's time moved by line 2's last output and line 2's by line 1's, up to half the time either
 *                   way: from a slow chorus to metallic FM grit as the repeats build up
 *   FDBK            each line's repeats; past ~95 % they grow into a soft clip and sustain (the LYRA-8's self-oscillating
 *                   delay); a fixed one-pole (~7 kHz) in each loop darkens them a little at every pass
 *   MIX             the slot's amount (P_TXDLY, the master's mst_amt): 0 off .. all wet; faded over 2.9 ms
 * A part (mono) is fed its block, gets the lines' mean; a stereo owner (the drum bus, the master) feeds their mid, line 1 to
 * L, line 2 to R. The settings are the owner's (fx_slots.c xdl_v). Two lines of FELUCCA_XDLY_LEN samples an instance (pool).
 * From rest nothing the lines held is read (fill, as ECHO's); once both hold only zeros and the input is silent the block
 * is skipped. No divide, no packed SIMD */
#if FELUCCA_INS_XDLY
#define XD_LEN ((uint32_t)FELUCCA_XDLY_LEN)
#define XD_M (XD_LEN - 1u)
#define XD_LPK 20600                                    /* the loops' one-pole, Q15: ~7 kHz */
/* the CPU guard's cost (cpuguard.c): an instance running, host instructions a sample (tests/xdly_test.c measures it and
 * checks it within this) */
#define CG_COST_XDLY 200u
typedef struct {
    int32_t w;                                          /* the MIX now (Q15) */
    int32_t da, db;                                     /* the lines' times now, Q8 samples */
    int32_t ya, yb;                                     /* their last outputs (Q15): the modulation, the feedback */
    int32_t fa, fb;                                     /* the loops' one-poles */
    uint16_t wp, fill, q;                               /* the write place, samples written since the start (<= XD_LEN),
                                                         * zeros written in a row on both (<= XD_LEN) */
    uint8_t on, evict;                                  /* running (0: at rest); evict: unused (no lent line), kept 0 */
} ifx_xd_t;
static int16_t ifx_xdly_ln[FELUCCA_INS_XDLY][2][XD_LEN] __attribute__((section(".pool")));
static ifx_xd_t ifx_xdly[FELUCCA_INS_XDLY] __attribute__((section(".pool")));
/* line ln's sample d (Q8) back from wp (the one just written: 0), linear between two; 0 past what was written (fill:
 * the samples written, wp's among them) */
AINL int32_t xd_tap(const int16_t *ln, uint32_t wp, uint32_t fill, int32_t d)
{
    uint32_t i = (uint32_t)d >> 8;
    int32_t y0, y1;
    if (i >= fill)
        return 0;
    y0 = ln[(wp - i) & XD_M], y1 = i + 1u < fill ? ln[(wp - i - 1u) & XD_M] : 0;
    return y0 + (((y1 - y0) * (d & 255)) >> 8);
}
/* a line's time moved by the other's output y (Q15) at depth md (Q15): up to half its time either way, kept inside it */
AINL int32_t xd_mod(int32_t d, int32_t y, int32_t md)
{
    return clamp(d + ((mulq15(y, md) * (d >> 9)) >> 7), 2 << 8, (int32_t)(XD_M - 2u) << 8);
}
/* the one into a line: the input and its repeats through the loop's one-pole, softly clipped (rounded toward 0: a tail
 * ends at exact 0, as ECHO's) */
AINL int32_t xd_in(int32_t x, int32_t *f, int32_t y, int32_t g)
{
    *f += mul_tz(y - *f, XD_LPK);
    return clamp(soft_knee(x + mul_tz(*f, g), 20000), -32767, 32767);
}
/* instance s of owner k (the audio's index) at MIX a: a part's b (r 0), a stereo owner's l and r; sh: the headroom */
static HOT2 __attribute__((noinline)) void ifx_xdly_run(ifx_xd_t *s, int16_t (*ln)[XD_LEN], uint32_t k, int32_t a,
                                                        int32_t *l, int32_t *r, uint32_t n, uint32_t sh)
{
    static const int16_t D[XDV_N] = XD_V_DEF;
    uint32_t o = ifx_owner(k), i, wp, fill, q;
    const int16_t *v = o < XD_NO ? XDL_V(o) : D;
    int32_t w = s->w, to, ta, tb, md, g, da, db, ya, yb;
    if (s->evict)
        a = 0;
    if (!a && !w) {
        s->on = 0;                                      /* (at rest: it starts again from clean lines) */
        return;
    }
    ta = (int32_t)xd_samples(v[XDV_T1]) << 8, tb = (int32_t)xd_samples(v[XDV_T2]) << 8;
    if (!s->on) {                                       /* starting: what the lines hold is not read, the times at once */
        s->fill = 0, s->q = 0, s->ya = s->yb = s->fa = s->fb = 0;
        s->da = ta, s->db = tb, s->on = 1;
    }
    to = a * 258;
    if (s->q >= XD_LEN && !fx_any(l, n) && (!r || !fx_any(r, n))) {
        s->w = to;                                      /* (clear lines, silence in: silence out) */
        return;
    }
    md = clamp(v[XDV_MOD], 0, 127) * 258;
    g = clamp(v[XDV_FB], 0, 127) * 270;                 /* (Q15: up to 1.05, into the clip) */
    wp = s->wp, fill = s->fill, q = s->q, da = s->da, db = s->db, ya = s->ya, yb = s->yb;
    for (i = 0; i < n; i++) {
        int32_t x = (r ? (l[i] >> 1) + (r[i] >> 1) : l[i]) >> sh, ia, ib;
        da += (ta - da) >> 10, db += (tb - db) >> 10;   /* (~23 ms: a turned TIME bends the repeats) */
        ia = xd_in(x, &s->fa, ya, g), ib = xd_in(x, &s->fb, yb, g);
        wp = (wp + 1u) & XD_M;
        ln[0][wp] = (int16_t)ia, ln[1][wp] = (int16_t)ib;
        q = ia | ib ? 0u : q < XD_LEN ? q + 1u : q;
        fill += fill < XD_LEN;
        {
            int32_t na = xd_tap(ln[0], wp, fill, xd_mod(da, yb, md)), nb = xd_tap(ln[1], wp, fill, xd_mod(db, ya, md));
            ya = na, yb = nb;                           /* (each read with the other's last output: the cross modulation) */
        }
        w += clamp(to - w, -IFX_SLOPE, IFX_SLOPE);
        if (r) {
            l[i] = ifx_mix(l[i], ya << sh, w);
            r[i] = ifx_mix(r[i], yb << sh, w);
        } else {
            l[i] = ifx_mix(l[i], ((ya + yb) >> 1) << sh, w);
        }
    }
    s->wp = (uint16_t)wp, s->fill = (uint16_t)fill, s->q = (uint16_t)q;
    s->da = da, s->db = db, s->ya = ya, s->yb = yb, s->w = w;
}
#endif
