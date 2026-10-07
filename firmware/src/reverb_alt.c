/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The reverb's other tanks (FELUCCA_REVERB, registry.h): 1 PLATE, 2 FDN8. Included by fx.c in place of the
 * ROOM's four lines (FELUCCA_REVERB 0). Both run at 22.05 kHz behind fx.c's half-band filters (rev_half_pair):
 * the same RAM holds twice the delay time, the top octave (> 11 kHz) is gone, and the in-loop damping took it
 * anyway. One ring of int16 for every line and allpass (RV_N, a power of two; REV_HALF: half of it), one write
 * index that counts down: line k is written at p + base_k and read d samples later at p + base_k + d
 * (Mutable Instruments' FxEngine layout: one mask per access, no per-line wrap).
 *
 * PLATE (after J. Dattorro, "Effect Design Part 1", JAES 45(9), 1997, his plate after Griesinger): four input
 * allpasses (0.75, 0.75, 0.625, 0.625), then a figure-of-eight tank: each half a modulated allpass (-0.7), a
 * delay, the damping low-pass and the decay gain, an allpass (0.5), a delay, into the other half. The output:
 * seven taps per side across both halves (Dattorro's table, scaled). Lengths: Dattorro's at 29761 Hz, scaled
 * to fill RV_N at 22.05 kHz (0.49 of his plate's time; REV_HALF 0.24), each the nearest unused prime.
 *
 * FDN8 (after J.-M. Jot's feedback delay networks and S. Costello's ReverbSc, all lines modulated): two input
 * allpasses, eight lines (30..63 ms; REV_HALF 15..31 ms), each read through a slowly moving linear-interpolated
 * tap (+-RV_FEXC samples, four quadrature LFOs at 0.4..0.9 Hz), damped by its own one-pole, its own decay gain
 * for its length (Jot: the same dB per second on every line), mixed by the 8 x 8 Householder matrix
 * (I - 2/8: one sum, a shift and a subtract per line, no multiply). Left: lines 0, 2, 4, 6; right: 1, 3, 5, 7.
 *
 * SIZE: the decay time, the ROOM's for the same value (its loop gain 17000 + 104 SIZE over its 49 ms mean line:
 * RV_KT below, a cubic in SIZE within 0.4 %); each element's gain is 2^(-kt L) for its length L. DAMP: a
 * one-pole per pass whose dB per second best matches the ROOM's (100 Hz .. 9 kHz, least squares; a cubic in
 * DAMP per tank). Both are recomputed only when SIZE or DAMP changes (rv_params, per block).
 * Every product rounds toward 0 (mul_tz) and every low-pass steps at least 1 (fx_step): a tail with no input
 * reaches exactly 0, and the bus' idle skip (fx.c) works as for the ROOM. */
#define RV_N (FELUCCA_REV_HALF ? 4096u : 8192u)
#define RV_M (RV_N - 1u)
static int16_t rev_line[FELUCCA_FX_REVERB ? RV_N : 1] REV_SECTION;
static struct {
    uint32_t p;                  /* the write index (counts down, masked on access) */
    uint32_t ph[4];              /* the LFOs */
    int32_t mod[8];              /* this block's modulated read offsets, Q8 samples */
    int32_t lp[8];               /* the damping low-passes */
    int32_t gain[8];             /* the decay gain per line (PLATE: per half), Q15 */
    int32_t k;                   /* the damping coefficient, Q15 */
    int32_t size, damp;          /* the SIZE / DAMP the gains are for (-1: not yet) */
} rv = {.size = -1};
#define RV_RD(b, d) rev_line[(rv.p + (uint32_t)(b) + (uint32_t)(d)) & RV_M]
#define RV_WR(b, v) (rev_line[(rv.p + (uint32_t)(b)) & RV_M] = (int16_t)(v))
#define REV_Q (2u * RV_N)        /* the longest a value stays in the ring, in output samples */
#define REV_LP_BUSY() (rv.lp[0] | rv.lp[1] | rv.lp[2] | rv.lp[3] | rv.lp[4] | rv.lp[5] | rv.lp[6] | rv.lp[7])

AINL int32_t rv_sat(int32_t x) { return clamp(x, -32768, 32767); }
/* a Schroeder allpass of len samples at base, coefficient g (Q15): v = x + g b stored, out b - g v */
AINL int32_t rv_ap(int32_t x, uint32_t base, uint32_t len, int32_t g, int32_t *wr)
{
    int32_t b = RV_RD(base, len), v = rv_sat(x + mul_tz(b, g));
    RV_WR(base, v);
    *wr |= v;
    return b - mul_tz(v, g);
}
AINL int32_t rv_tap(uint32_t base, int32_t d)            /* a read d samples back, d in Q8, linear */
{
    int32_t a = RV_RD(base, (uint32_t)d >> 8), b = RV_RD(base, ((uint32_t)d >> 8) + 1u);
    return a + (((b - a) * (d & 255)) >> 8);
}
AINL int32_t rv_apm(int32_t x, uint32_t base, int32_t d, int32_t g, int32_t *wr)   /* modulated: d in Q8 */
{
    int32_t b = rv_tap(base, d), v = rv_sat(x + mul_tz(b, g));
    RV_WR(base, v);
    *wr |= v;
    return b - mul_tz(v, g);
}

/* 2^(-y / 2^24), Q15 (a cubic for the fraction, 1e-4) */
static int32_t rv_exp2n(uint32_t y)
{
    uint32_t n = y >> 24;
    int32_t x = (int32_t)((y >> 8) & 0xFFFFu), p;
    p = 32765 - ((x * (22645 - ((x * (7556 - ((x * 1295) >> 16))) >> 16))) >> 16);
    return n > 15u ? 0 : p >> n;
}
/* the ROOM's decay per 22.05 kHz sample at SIZE s, Q24 of log2 (fitted: -log2((17000 + 104 s) / 32768) / 2167.5
 * x 2) */
AINL uint32_t rv_kt(int32_t s) { return (uint32_t)(14647 - 135 * s + ((s * s * (23109 - 44 * s)) >> 16)); }
AINL int32_t rv_damp(int32_t d, int32_t c0, int32_t c1, int32_t c2, int32_t c3)   /* the fitted cubic, Q15 */
{
    int32_t k = c0 - ((c1 * d) >> 8) - ((d * d * (c2 - c3 * d)) >> 16);
    return k > 32767 ? 32767 : k < 1 ? 1 : k;
}

#if FELUCCA_REVERB == 1
/* ---------------------------------------------------------------------------------------------- PLATE --- */
#if FELUCCA_REV_HALF
#define RV_EXC 4                                         /* the tank allpasses' modulation, +- samples */
enum { RV_I1 = 29, RV_I2 = 19, RV_I3 = 67, RV_I4 = 53, RV_M1 = 127, RV_D1 = 797, RV_A2 = 331, RV_D2 = 673,
       RV_M3 = 163, RV_D3 = 757, RV_A4 = 479, RV_D4 = 569 };
enum { RV_TL0 = 48, RV_TL1 = 535, RV_TL2 = 344, RV_TL3 = 359, RV_TL4 = 358, RV_TL5 = 34, RV_TL6 = 192,
       RV_TR0 = 63, RV_TR1 = 652, RV_TR2 = 221, RV_TR3 = 481, RV_TR4 = 379, RV_TR5 = 60, RV_TR6 = 22 };
#define RV_DAMP(d) rv_damp(d, 32686, 27188, 80686, 233)
#else
#define RV_EXC 6
enum { RV_I1 = 53, RV_I2 = 41, RV_I3 = 137, RV_I4 = 101, RV_M1 = 241, RV_D1 = 1607, RV_A2 = 653, RV_D2 = 1327,
       RV_M3 = 331, RV_D3 = 1523, RV_A4 = 953, RV_D4 = 1151 };
enum { RV_TL0 = 96, RV_TL1 = 1073, RV_TL2 = 691, RV_TL3 = 720, RV_TL4 = 718, RV_TL5 = 67, RV_TL6 = 385,
       RV_TR0 = 127, RV_TR1 = 1309, RV_TR2 = 443, RV_TR3 = 965, RV_TR4 = 762, RV_TR5 = 121, RV_TR6 = 44 };
#define RV_DAMP(d) rv_damp(d, 32801, 61866, 61770, 499)
#endif
/* the ring: each element's base (its length + 1; the modulated two + 2 RV_EXC + 2) */
enum { RB_I1 = 0, RB_I2 = RB_I1 + RV_I1 + 1, RB_I3 = RB_I2 + RV_I2 + 1, RB_I4 = RB_I3 + RV_I3 + 1,
       RB_M1 = RB_I4 + RV_I4 + 1, RB_D1 = RB_M1 + RV_M1 + 2 * RV_EXC + 2, RB_A2 = RB_D1 + RV_D1 + 1,
       RB_D2 = RB_A2 + RV_A2 + 1, RB_M3 = RB_D2 + RV_D2 + 1, RB_D3 = RB_M3 + RV_M3 + 2 * RV_EXC + 2,
       RB_A4 = RB_D3 + RV_D3 + 1, RB_D4 = RB_A4 + RV_A4 + 1, RB_END = RB_D4 + RV_D4 + 1 };
_Static_assert(RB_END <= RV_N, "PLATE: the ring holds every element");
#define RV_IN 13000              /* the send into the tank, Q15 (the ROOM's) */
#define RV_OUT (FELUCCA_REV_HALF ? 6760 : 8920)   /* the seven taps' sum, Q17: the wet level as the ROOM's */

static __attribute__((noinline)) void rv_params(void)   /* SIZE / DAMP -> the gains, the damping (XIP: rare; FAR) */
{
    int32_t s = song.g[G_RSIZE], d = song.g[G_RDAMP];
    uint32_t kt;
    rv.size = s, rv.damp = d;
    kt = rv_kt(s);
    rv.gain[0] = rv_exp2n(kt * (RV_M1 + RV_EXC + RV_D1 + RV_A2 + RV_D2));
    rv.gain[1] = rv_exp2n(kt * (RV_M3 + RV_EXC + RV_D3 + RV_A4 + RV_D4));
    rv.k = RV_DAMP(d);
}
AINL void rv_lfo(void)                                    /* the two tank allpasses' read offsets, per block */
{
    int32_t m;
    rv.ph[0] += LFO_INC[46];                             /* (~0.55 Hz; the right half's a quarter turn on) */
    m = osc_sine(rv.ph[0]);
    rv.mod[0] = ((RV_M1 + RV_EXC) << 8) + ((m * RV_EXC) >> 7);
    m = osc_sine(rv.ph[0] + 0x40000000u);
    rv.mod[1] = ((RV_M3 + RV_EXC) << 8) + ((m * RV_EXC) >> 7);
}
FX_STEP int32_t rv_step(int32_t in, int32_t *yr, int32_t *wr)
{
    int32_t x = mulq15(in, RV_IN), d2 = RV_RD(RB_D2, RV_D2), d4 = RV_RD(RB_D4, RV_D4), t, l, r;
    x = rv_ap(x, RB_I1, RV_I1, 24576, wr);
    x = rv_ap(x, RB_I2, RV_I2, 24576, wr);
    x = rv_ap(x, RB_I3, RV_I3, 20480, wr);
    x = rv_ap(x, RB_I4, RV_I4, 20480, wr);
    t = rv_sat(rv_apm(x + d4, RB_M1, rv.mod[0], -22938, wr));   /* the left half */
    RV_WR(RB_D1, t);
    *wr |= t;
    t = RV_RD(RB_D1, RV_D1);
    rv.lp[0] += fx_step(t - rv.lp[0], rv.k);
    t = rv_sat(rv_ap(mul_tz(rv.lp[0], rv.gain[0]), RB_A2, RV_A2, 16384, wr));
    RV_WR(RB_D2, t);
    *wr |= t;
    t = rv_sat(rv_apm(x + d2, RB_M3, rv.mod[1], -22938, wr));   /* the right half */
    RV_WR(RB_D3, t);
    *wr |= t;
    t = RV_RD(RB_D3, RV_D3);
    rv.lp[1] += fx_step(t - rv.lp[1], rv.k);
    t = rv_sat(rv_ap(mul_tz(rv.lp[1], rv.gain[1]), RB_A4, RV_A4, 16384, wr));
    RV_WR(RB_D4, t);
    *wr |= t;
    /* the outputs: Dattorro's table 2 (his node names: 24_30 D1, 31_33 A2, 33_39 D2, 48_54 D3, 55_59 A4, 59_63 D4) */
    l = RV_RD(RB_D3, RV_TL0) + RV_RD(RB_D3, RV_TL1) - RV_RD(RB_A4, RV_TL2) + RV_RD(RB_D4, RV_TL3) -
        RV_RD(RB_D1, RV_TL4) - RV_RD(RB_A2, RV_TL5) - RV_RD(RB_D2, RV_TL6);
    r = RV_RD(RB_D1, RV_TR0) + RV_RD(RB_D1, RV_TR1) - RV_RD(RB_A2, RV_TR2) + RV_RD(RB_D2, RV_TR3) -
        RV_RD(RB_D3, RV_TR4) - RV_RD(RB_A4, RV_TR5) - RV_RD(RB_D4, RV_TR6);
    rv.p--;
    *yr = mulq15(r, RV_OUT) << 2;
    return mulq15(l, RV_OUT) << 2;
}

#elif FELUCCA_REVERB == 2
/* ----------------------------------------------------------------------------------------------- FDN8 --- */
#if FELUCCA_REV_HALF
#define RV_FEXC 3
enum { RV_IA = 23, RV_IB = 17, RV_IC = 47, RV_ID = 31, RV_L0 = 331, RV_L1 = 367, RV_L2 = 409, RV_L3 = 457, RV_L4 = 503, RV_L5 = 557,
       RV_L6 = 617, RV_L7 = 659 };
#define RV_DAMP(d) rv_damp(d, 32831, 10056, -1613, -245)
#else
#define RV_FEXC 4
enum { RV_IA = 47, RV_IB = 37, RV_IC = 113, RV_ID = 83, RV_L0 = 673, RV_L1 = 743, RV_L2 = 823, RV_L3 = 911, RV_L4 = 1013, RV_L5 = 1123,
       RV_L6 = 1237, RV_L7 = 1297 };
#define RV_DAMP(d) rv_damp(d, 32779, 15872, 28161, -150)
#endif
#define RV_SP (2 * RV_FEXC + 3)  /* a line's span past its length: the modulation, the interpolation, + 1 */
enum { RB_IA = 0, RB_IB = RV_IA + 1, RB_IC = RB_IB + RV_IB + 1, RB_ID = RB_IC + RV_IC + 1, RB_0 = RB_ID + RV_ID + 1, RB_1 = RB_0 + RV_L0 + RV_SP,
       RB_2 = RB_1 + RV_L1 + RV_SP, RB_3 = RB_2 + RV_L2 + RV_SP, RB_4 = RB_3 + RV_L3 + RV_SP,
       RB_5 = RB_4 + RV_L4 + RV_SP, RB_6 = RB_5 + RV_L5 + RV_SP, RB_7 = RB_6 + RV_L6 + RV_SP,
       RB_END = RB_7 + RV_L7 + RV_SP };
_Static_assert(RB_END <= RV_N, "FDN8: the ring holds every line");
static const uint16_t RV_L[8] = {RV_L0, RV_L1, RV_L2, RV_L3, RV_L4, RV_L5, RV_L6, RV_L7};
#define RV_IN 13000
#define RV_OUT (FELUCCA_REV_HALF ? 4770 : 6020)   /* the four lines' sum per side, Q17: the wet level as the ROOM's */

static __attribute__((noinline)) void rv_params(void)   /* (XIP: rare; fx.c calls it FAR) */
{
    int32_t s = song.g[G_RSIZE], d = song.g[G_RDAMP];
    uint32_t kt, k;
    rv.size = s, rv.damp = d;
    kt = rv_kt(s);
    for (k = 0; k < 8u; k++)
        rv.gain[k] = rv_exp2n((kt * 15u >> 4) * (RV_L[k] + RV_FEXC));   /* (x 0.94: the low-passes add to the decay) */
    rv.k = RV_DAMP(d);
}
AINL void rv_lfo(void)                                   /* eight read offsets: four LFOs, sine and cosine */
{
    static const uint8_t RATE[4] = {40, 44, 49, 54};     /* (~0.42 .. 0.88 Hz) */
    uint32_t j;
    for (j = 0; j < 4u; j++) {
        int32_t s, c;
        rv.ph[j] += LFO_INC[RATE[j]];
        s = osc_sine(rv.ph[j]), c = osc_sine(rv.ph[j] + 0x40000000u);
        rv.mod[2 * j] = ((RV_L[2 * j] + RV_FEXC) << 8) + ((s * RV_FEXC) >> 7);
        rv.mod[2 * j + 1] = ((RV_L[2 * j + 1] + RV_FEXC) << 8) + ((c * RV_FEXC) >> 7);
    }
}
FX_STEP int32_t rv_step(int32_t in, int32_t *yr, int32_t *wr)
{
    int32_t x = mulq15(in, RV_IN), kd = rv.k, o0, o1, o2, o3, o4, o5, o6, o7, f0, f1, f2, f3, f4, f5, f6, f7, s, w, l, r;
    x = rv_ap(x, RB_IA, RV_IA, 24576, wr);
    x = rv_ap(x, RB_IB, RV_IB, 24576, wr);
    x = rv_ap(x, RB_IC, RV_IC, 20480, wr);
    x = rv_ap(x, RB_ID, RV_ID, 20480, wr);
#define RV_LN(k) o##k = rv_tap(RB_##k, rv.mod[k]);                  /* a line: read, damp, decay */ \
                 rv.lp[k] += fx_step(o##k - rv.lp[k], kd); \
                 f##k = mul_tz(rv.lp[k], rv.gain[k])
    RV_LN(0); RV_LN(1); RV_LN(2); RV_LN(3); RV_LN(4); RV_LN(5); RV_LN(6); RV_LN(7);
#undef RV_LN
    s = f0 + f1 + f2 + f3 + f4 + f5 + f6 + f7;
    s = (s + ((s >> 31) & 3)) >> 2;                      /* 2/8 of the sum, toward 0 */
#define RV_WL(k, v) w = rv_sat(s - f##k + (v)); RV_WR(RB_##k, w); *wr |= w
    RV_WL(0, x); RV_WL(1, -x); RV_WL(2, x); RV_WL(3, -x); RV_WL(4, x); RV_WL(5, -x); RV_WL(6, x); RV_WL(7, -x);
#undef RV_WL
    l = o0 - o2 + o4 - o6;
    r = o1 - o3 + o5 - o7;
    rv.p--;
    *yr = mulq15(r, RV_OUT) << 2;
    return mulq15(l, RV_OUT) << 2;
}
#endif

static void rev_tank_clear(void)                         /* every cell and filter to 0 (the bus then idle) */
{
    uint32_t i;
    for (i = 0; i < sizeof rev_line / 2u; i++)
        rev_line[i] = 0;
    for (i = 0; i < 8u; i++)
        rv.lp[i] = 0;
}
