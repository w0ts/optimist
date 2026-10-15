/* SPDX-License-Identifier: GPL-3.0-only */
/* EQ3 (fx-pool 2026-10-15; the user: "we lack an EQ like EQ Three from Ableton"): a three-band DJ equaliser, an insert type
 * on any slot (a part's, the drum bus's, the master's, the USB input's, a return's) and on a drum sound's (its lane).
 * Included by insfx.c.
 *   bands   two Linkwitz-Riley crossovers of 12 dB/oct (LR2), FREQ LO (51 Hz .. 1 kHz, 250 Hz) and FREQ HI (625 Hz .. 12 kHz,
 *           2.5 kHz), each made of TPT (bilinear) one-poles: y1 = LP(x), lo = LP(y1) (the LR2 low), ap = 2 y1 - x (the first-order
 *           all-pass of the same pole), hs = ap - lo (the LR2 high, the exact rest of the all-pass); hs split the same way
 *           at FREQ HI into mid and hi; lo through FREQ HI's all-pass (one more one-pole) so the three sum to the two
 *           all-passes: flat in magnitude at any setting of the frequencies. 5 one-poles a channel, no divide, no 64 bit
 *   gains   LO MID HI: KILL (-inf, the knob's left end), -30 .. +6 dB in 1 dB steps (EQ_G, Q15), 0 dB the default
 *   bypass  (the user's ruling, 2026-10-15) all three at 0 dB, the type out of the slot, its amount (EQ3: ON / OFF) 0 or the
 *           FX bypassed: the kernel is not run, the samples pass bit for bit; entering or leaving that state crossfades the
 *           EQ's output and the dry over 128 samples (2.9 ms, IFX_SLOPE, as every insert's wet share). Re-engaging from
 *           rest seeds the one-poles with the first sample (its DC steady state: lo = x, the rest 0) instead of running
 *           them warm at flat (no CPU at flat); the crossfade covers what is left of the start
 *   gains moving: ramped over the block (no zipper); the frequencies: a block's coefficient
 * The settings: each owner's (eq3_v: T1 T2 T3 DR, the master, the USB input, the returns' lines 0 1 2), a drum sound's the
 * drum track's; the amount (P_TEQ, the master's mst_amt, a return's fxs_ramt, the USB slot's, a lane's dins_amt) its on
 * switch. CPU (host, tests/eq3_test.c): measured per sample, mono and stereo; the CPU guard counts an EQ3 only while engaged */
#if FELUCCA_INS_EQ3
AINL int eq3_flat(const int16_t *v) { return !v[EQV_LO] && !v[EQV_MID] && !v[EQV_HI]; }
/* the CPU guard's cost (cpuguard.c): EQ3 while engaged (not flat), a part's mono instance, host instructions a sample
 * (tests/eq3_test.c measures it and checks it within this); a stereo owner's two sides CG_COST_EQ3S */
#define CG_COST_EQ3M 80u
#define CG_COST_EQ3S (2u * CG_COST_EQ3M)
typedef struct {
    int32_t y1, lo, z1, mid, c;                         /* the one-poles: FREQ LO's two, FREQ HI's two, lo's all-pass */
} eq_ch_t;
typedef struct {
    eq_ch_t ch[2];                                      /* a side each (a part: the first) */
    int32_t w;                                          /* the EQ's share (Q15): 0 bypassed, 32768 engaged */
    uint16_t g[3];                                      /* the gains at the end of the last block (Q15) */
} eq_st_t;
static eq_st_t eq3_st[EQ_NO];
/* one TPT (trapezoidal, Zavalishin) one-pole low-pass: its state *z, G = g / (1 + g) (Q16); 2 lp - x is then an exact
 * first-order all-pass (the bilinear map of (1 - s) / (1 + s)), which the crossovers need */
AINL int32_t eq_lp(int32_t *z, int32_t x, uint32_t k)
{
    int32_t v = mulq16(x - *z, k), lp = v + *z;
    *z = lp + v;
    return lp;
}
/* one side: n samples of b, the gains from g0 by dg a sample (Q15, << 5 more), the share from w to `to` */
static HOT2 __attribute__((noinline)) void eq3_side(eq_ch_t *s, int32_t *b, uint32_t n, uint32_t kl, uint32_t kh,
                                                    const int32_t *g0, const int32_t *dg, int32_t w, int32_t to)
{
    uint32_t i;
    int32_t gl = g0[0], gm = g0[1], gh = g0[2];
    eq_ch_t z = *s;
    if (!(dg[0] | dg[1] | dg[2]) && w >= 32768 && to >= 32768) {   /* (steady: engaged, the gains still: no ramp, no mix) */
        uint32_t ql = (uint32_t)(gl >> 5), qm = (uint32_t)(gm >> 5), qh = (uint32_t)(gh >> 5);
        for (i = 0; i < n; i++) {
            int32_t x = b[i], y1, lo, hs, z1, mid;
            y1 = eq_lp(&z.y1, x, kl);
            lo = eq_lp(&z.lo, y1, kl);
            hs = 2 * y1 - x - lo;
            z1 = eq_lp(&z.z1, hs, kh);
            mid = eq_lp(&z.mid, z1, kh);
            b[i] = (mulq16(2 * eq_lp(&z.c, lo, kh) - lo, ql) + mulq16(mid, qm) + mulq16(2 * z1 - hs - mid, qh)) << 1;
        }
        *s = z;
        return;
    }
    for (i = 0; i < n; i++) {
        int32_t x = b[i], y1, lo, hs, z1, mid, hi, lo2, y;
        y1 = eq_lp(&z.y1, x, kl);
        lo = eq_lp(&z.lo, y1, kl);                      /* (the LR2 low) */
        hs = 2 * y1 - x - lo;                           /* (the all-pass less the low: the LR2 high, inverted) */
        z1 = eq_lp(&z.z1, hs, kh);
        mid = eq_lp(&z.mid, z1, kh);
        hi = 2 * z1 - hs - mid;
        lo2 = 2 * eq_lp(&z.c, lo, kh) - lo;             /* (the low through FREQ HI's all-pass) */
        y = (mulq16(lo2, (uint32_t)(gl >> 5)) + mulq16(mid, (uint32_t)(gm >> 5)) + mulq16(hi, (uint32_t)(gh >> 5))) << 1;
        gl += dg[0], gm += dg[1], gh += dg[2];
        w += clamp(to - w, -IFX_SLOPE, IFX_SLOPE);
        b[i] = ifx_mix(x, y, w);
    }
    *s = z;
}
/* engaged at once from silence (a drum voice's hit: dins_rest), its gains v's */
static void eq3_engage(eq_st_t *s, const int16_t *v)
{
    uint32_t k;
    ifx_zero(s, sizeof *s);
    for (k = 0; k < 3u; k++)
        s->g[k] = EQ_G[clamp(v[k], EQ_KILL, 6) - EQ_KILL];
    s->w = 32768;
}
/* owner o's EQ3 on l (a part: r 0) or l and r, on: its amount / in its slot / its FX on. Returns its share after (0: at rest) */
static HOT2 __attribute__((noinline)) int32_t eq3_run(eq_st_t *s, const int16_t *v, int32_t on, int32_t *l, int32_t *r, uint32_t n)
{
    int32_t to = on && !eq3_flat(v) ? 32768 : 0, g0[3], dg[3], w = s->w;
    uint32_t k, kl, kh;
    if (!to && !w)
        return 0;                                       /* (bypassed: the samples as they are) */
    if (!w) {                                           /* (engaging from rest: seeded with the first samples' DC) */
        s->ch[0].y1 = s->ch[0].lo = s->ch[0].c = l[0], s->ch[0].z1 = s->ch[0].mid = 0;   /* (a TPT state at DC: the input) */
        s->ch[1].y1 = s->ch[1].lo = s->ch[1].c = r ? r[0] : 0, s->ch[1].z1 = s->ch[1].mid = 0;
        for (k = 0; k < 3u; k++)
            s->g[k] = EQ_G[clamp(v[k], EQ_KILL, 6) - EQ_KILL];
    }
    for (k = 0; k < 3u; k++) {
        int32_t g1 = EQ_G[clamp(v[k], EQ_KILL, 6) - EQ_KILL];
        g0[k] = (int32_t)s->g[k] << 5, dg[k] = g1 - (int32_t)s->g[k];   /* (<< 5: a step a sample over CTL) */
        s->g[k] = (uint16_t)g1;
    }
    kl = EQ_FL_K[clamp(v[EQV_FL], 0, EQ_FMAX)], kh = EQ_FH_K[clamp(v[EQV_FH], 0, EQ_FMAX)];
    eq3_side(&s->ch[0], l, n, kl, kh, g0, dg, w, to);
    if (r)
        eq3_side(&s->ch[1], r, n, kl, kh, g0, dg, w, to);
    w = to > w ? (w + (int32_t)n * IFX_SLOPE < to ? w + (int32_t)n * IFX_SLOPE : to)
               : (w - (int32_t)n * IFX_SLOPE > to ? w - (int32_t)n * IFX_SLOPE : to);   /* (as eq3_side's ramp) */
    s->w = w;
    return w;
}
_Static_assert(CTL == 32, "eq3_run: the gains' ramp is << 5");
#endif
