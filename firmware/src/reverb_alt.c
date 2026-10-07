/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The reverb's other tanks (registry.h FELUCCA_REV_PLATE, FELUCCA_REV_FDN8): PLATE and FDN8, beside the ROOM's
 * four lines and SPRING (FX > REVERB > TYPE picks one at run time: fx.c rev_bus, rev_type.c). Included by fx.c.
 * Both run at 22.05 kHz behind fx.c's half-band filters (rev_half_pair):
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
 * FDN8 (after J.-M. Jot's feedback delay networks and S. Costello's ReverbSc, all lines modulated), long and
 * lush: a pre-delay, four input allpasses, eight lines (62..117 ms in the pool's 32 KB ring, REV_POOL; 29..57 ms in
 * 16 KB, REV_HALF in the pool or neither; 14..29 ms in 8 KB, REV_HALF in main RAM), each read through a slowly
 * moving tap (its own LFO, ~0.6..1.2 Hz, the depth growing with SIZE) whose fraction goes through a first-order
 * allpass (flat: the modulation takes no treble), damped by its own one-pole (Jot: per length), its own decay
 * gain for its length (the same dB per second on every line), mixed by the 8 x 8 Householder matrix (I - 2/8:
 * one sum, a shift and a subtract per line, no multiply). Left: lines 0, 2, 4, 6; right: 1, 3, 5, 7. Above SIZE
 * 90 the decay time grows exponentially to ~15 s and a near-freeze at 127 (the FDN8 section below).
 *
 * SIZE: the decay time, the ROOM's for the same value (its loop gain 17000 + 104 SIZE over its 49 ms mean line:
 * RV_KT below, a cubic in SIZE within 0.4 %); each element's gain is 2^(-kt L) for its length L. DAMP: a
 * one-pole per pass whose dB per second best matches the ROOM's (100 Hz .. 9 kHz, least squares; a cubic in
 * DAMP per tank). Both are recomputed only when SIZE or DAMP changes (rv_params, per block).
 * Every product rounds toward 0 (mul_tz; FDN8's damping and gains at random above RV_RT, rv_rnd) and every
 * low-pass steps at least 1 (fx_step; FDN8's moves toward its input, its allpasses shrink a lone value): a tail
 * with no input reaches exactly 0, and the bus' idle skip (fx.c) works as for the ROOM. */
#if FELUCCA_REV_FDN8             /* FDN8: twice the ring in the pool (REV_POOL), below */
#define RV_N ((FELUCCA_REV_HALF ? 4096u : 8192u) << (FELUCCA_REV_POOL ? 1 : 0))
#else
#define RV_N (FELUCCA_REV_HALF ? 4096u : 8192u)
#endif
#define RV_M (RV_N - 1u)
static int16_t rev_line[FELUCCA_FX_REVERB ? (RV_N > REV_LINE_OWN ? RV_N : REV_LINE_OWN) : 1] REV_SECTION;   /* (shared:
                                 * the ROOM's lines and SPRING's loop too, fx.c) */
static struct {                 /* (one tank runs at a time: PLATE's and FDN8's fields side by side, fx.c's switch clears) */
    uint32_t p;                  /* the write index (counts down, masked on access) */
#if FELUCCA_REV_FDN8
    uint32_t ph[8];              /* the LFOs (FDN8: one per line; PLATE: ph[0]) */
#else
    uint32_t ph[4];              /* the LFOs */
#endif
    int32_t mod[8];              /* this block's read offsets (PLATE: modulated, Q8 samples; FDN8: whole samples, the
                                  * allpass interpolation's D) */
    int32_t lp[8];               /* the damping low-passes */
    int32_t gain[8];             /* the decay gain per line (PLATE: per half), Q15 */
#if FELUCCA_REV_PLATE
    int32_t k;                   /* PLATE's damping coefficient, Q15 */
#endif
#if FELUCCA_REV_FDN8
    int32_t ap[8];               /* FDN8: the interpolating allpasses' last outputs */
    int16_t eta[8];              /* this block's interpolating allpass coefficients, Q15 */
    uint16_t kl[8];              /* the damping low-pass' memory per line, Q15 (0: none) */
    uint32_t rq;                 /* the random rounding's generator */
    int32_t in, g1, g2, pd, dq;  /* the send, the diffusers' coefficients, the pre-delay, the depth (Q8) */
#endif
    int32_t size, damp;          /* the SIZE / DAMP the gains are for (-1: not yet) */
#if FELUCCA_REV_FDN8             /* (FDN8's LFOs start apart: no two lines move together) */
} rv = {.size = -1, .ph = {0x00000000u, 0x9E3779B9u, 0x3C6EF372u, 0xDAA66D2Bu, 0x78DDE6E4u, 0x1715609Du,
                           0xB54CDA56u, 0x5384540Fu}};
#else
} rv = {.size = -1};
#endif
#define RV_RD(b, d) rev_line[(rv.p + (uint32_t)(b) + (uint32_t)(d)) & RV_M]
#define RV_WR(b, v) (rev_line[(rv.p + (uint32_t)(b)) & RV_M] = (int16_t)(v))
#define RV_Q (2u * RV_N)         /* the longest a value stays in the ring, in output samples */
#if FELUCCA_REV_FDN8
#define RV_LP_BUSY() (rv.lp[0] | rv.lp[1] | rv.lp[2] | rv.lp[3] | rv.lp[4] | rv.lp[5] | rv.lp[6] | rv.lp[7] | \
                      rv.ap[0] | rv.ap[1] | rv.ap[2] | rv.ap[3] | rv.ap[4] | rv.ap[5] | rv.ap[6] | rv.ap[7])
#else
#define RV_LP_BUSY() (rv.lp[0] | rv.lp[1] | rv.lp[2] | rv.lp[3] | rv.lp[4] | rv.lp[5] | rv.lp[6] | rv.lp[7])
#endif

AINL int32_t rv_sat(int32_t x) { return clamp(x, -32768, 32767); }
#define RV_RT 32                 /* FDN8's random rounding down to this size, toward 0 below */
/* a * b / 2^15: rounded at random (u: uniform 0 .. 2^15 - 1) while |a| >= RV_RT, toward 0 below it */
AINL int32_t rv_rnd(int32_t a, int32_t b, int32_t u)
{
    return (a >= RV_RT || a <= -RV_RT) ? (a * b + u) >> 15 : mul_tz(a, b);
}
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

/* Each tank's own names: rv_params, rv_lfo, rv_step, RB_END below are rvp_* / RBP_END (PLATE), rvf_* / RBF_END (FDN8);
 * the state rv is shared (one tank runs at a time; fx.c's switch clears it) */
#if FELUCCA_REV_PLATE
#define rv_params rvp_params
#define rv_lfo rvp_lfo
#define rv_step rvp_step
#define RB_END RBP_END
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

#undef rv_params
#undef rv_lfo
#undef rv_step
#undef RB_END
#undef RV_DAMP
#undef RV_IN
#undef RV_OUT
#undef RV_EXC
#endif

#if FELUCCA_REV_FDN8
#define rv_params rvf_params
#define rv_lfo rvf_lfo
#define rv_step rvf_step
#define RB_END RBF_END
/* ----------------------------------------------------------------------------------------------- FDN8 --- */
/* SIZE up to RV_KNEE (90, the default): the decay as before, the ROOM's. Above it the decay time doubles every
 * RV_STEPS / 10 steps (exponential in RT60, even to the ear) to ~13 s at 126; 127 adds RV_FRZ8 / 8 doublings, a
 * near-freeze. Above the knee too: the damping per pass shrinks with the decay (the treble's RT60 keeps its
 * ratio to the bass', DAMP still sets it: a long tail does not go dull), the modulation deepens from RV_FEXC to
 * RV_EXC samples, the diffusers grow from 0.75 / 0.625 to 0.8 / 0.7, a pre-delay grows to RV_PD and the send
 * comes down a little (2^(-doublings / 8): the build-up of a long tail keeps its headroom). Measured
 * (tests/reverb_proto.c, DAMP 30): RT60 0.5 .. 1.5 s at SIZE 0..90, 9.2 s at 120, 13.0 s at 126, ~48 s at 127; its
 * treble at 4 kHz as long as DAMP says.
 * Why the pool's ring is twice as long (2026-10-07, fdn8-ring): a long tail rang. The eight lines held 0.34 s, a mode
 * every ~2.9 Hz, each ~0.2 Hz wide at 10 s; only the moving reads spread them, by a share of their frequency, so
 * the low modes (200 .. 600 Hz) stood still. Deeper modulation smoothed them but bent the pitch more (a chorus);
 * twice the lines halves the mode spacing and the passes a second, so the same modulation smooths more and bends
 * less: ring_db at long / huge 1.37 / 0.87 dB (16 KB 1.76 / 1.08; before 1.61 / 1.11), a held sine's spread 7 /
 * 15 cents RMS (before 16 / 30). A Hadamard matrix, a turning one, allpasses in the loop, noise or triangle LFOs
 * and a deeper low band did not measurably help.
 * And why the random rounding (rv_rnd): rounding toward 0 took ~1/2 LSB a product, a steady hiss in the tail
 * (against the same tank with 8 more bits: ~-62 dBFS of the wet at SIZE 120, ~-44 at 127); at random it is
 * unbiased and the hiss ~-74 dBFS (~-66 at 127). Below RV_RT toward 0 again, so a tail still rings out to exactly
 * 0 (unbiased rounding alone kept a floor alive). The unbiased decay is a little longer than the biased one was,
 * hence RV_STEPS 110 and RV_FRZ8 18 (before 100 and 4 doublings). */
#define RV_KNEE 90
#define RV_STEPS 110             /* SIZE steps per doubling of the decay time above the knee, x 10 */
#define RV_FRZ8 18               /* SIZE 127: this many eighths of a doubling more */
#define RV_RATES 50, 58, 46, 55, 60, 49, 52, 47   /* LFO_INC, a line each: ~0.6 .. 1.2 Hz (RV_EXC: 4.6 cents at most) */
#define RV_KG 31                 /* the loop gains' share of the decay, / 32 (the low-passes add the rest) */
#define RV_INSH 3                /* the send above the knee: x 2^(-doublings / 2^RV_INSH) */
#if RV_N == 4096u                /* 8 KB (REV_HALF in main RAM) */
#define RV_EXC 6                 /* the deepest modulation, +- samples (SIZE 127) */
#define RV_FEXC 3                /* up to the knee */
#define RV_PD 110                /* the longest pre-delay, samples (5 ms) */
#define RV_LREF 488              /* the mean line the damping's fit was made for */
enum { RV_IA = 23, RV_IB = 17, RV_IC = 47, RV_ID = 31, RV_L0 = 317, RV_L1 = 353, RV_L2 = 389, RV_L3 = 439, RV_L4 = 479,
       RV_L5 = 541, RV_L6 = 593, RV_L7 = 631 };
#define RV_DAMP(d) rv_damp(d, 32831, 10056, -1613, -245)
#else                            /* 16 KB (main RAM; REV_HALF in the pool) and 32 KB (the pool) */
#define RV_EXC 8
#define RV_FEXC 4
#define RV_PD 220                /* (10 ms) */
#define RV_LREF 978
#define RV_DAMP(d) rv_damp(d, 32779, 15872, 28161, -150)
enum { RV_IA = 47, RV_IB = 37, RV_IC = 113, RV_ID = 83 };
#if RV_N == 8192u
enum { RV_L0 = 647, RV_L1 = 719, RV_L2 = 787, RV_L3 = 877, RV_L4 = 977, RV_L5 = 1087, RV_L6 = 1187, RV_L7 = 1249 };
#else
enum { RV_L0 = 1361, RV_L1 = 1499, RV_L2 = 1637, RV_L3 = 1823, RV_L4 = 2039, RV_L5 = 2267, RV_L6 = 2477, RV_L7 = 2579 };
#endif
#endif
#define RV_SP (2 * RV_EXC + 2)   /* a line's span past its length: reads at L .. L + 2 RV_EXC + 1 */
#define RV_C (RV_EXC + 1)        /* the read's centre past the length */
enum { RB_PD = 0, RB_IA = RV_PD + 1, RB_IB = RB_IA + RV_IA + 1, RB_IC = RB_IB + RV_IB + 1, RB_ID = RB_IC + RV_IC + 1,
       RB_0 = RB_ID + RV_ID + 1, RB_1 = RB_0 + RV_L0 + RV_SP, RB_2 = RB_1 + RV_L1 + RV_SP, RB_3 = RB_2 + RV_L2 + RV_SP,
       RB_4 = RB_3 + RV_L3 + RV_SP, RB_5 = RB_4 + RV_L4 + RV_SP, RB_6 = RB_5 + RV_L5 + RV_SP,
       RB_7 = RB_6 + RV_L6 + RV_SP, RB_END = RB_7 + RV_L7 + RV_SP };
_Static_assert(RB_END <= RV_N, "FDN8: the ring holds every line");
static const uint16_t RV_L[8] = {RV_L0, RV_L1, RV_L2, RV_L3, RV_L4, RV_L5, RV_L6, RV_L7};
#ifndef RV_OUT32
#define RV_OUT32 7670
#endif
#define RV_OUT (RV_N == 4096u ? 4770 : RV_N == 8192u ? 6020 : RV_OUT32)   /* the four lines' sum per side, Q17: the wet
                                                                            * level as the ROOM's (per ring) */

/* The damping per line (rv_params only: XIP, rare). The one-pole lp = o + c (lp - o) loses 10 log10(1 + K c /
 * (1 - c)^2) dB at w (K = 2 - 2 cos w); its dB scale with neither c nor the line. rv_lpc: the c whose loss at
 * 4 kHz is sc / 2^15 times c's (the line's length over the mean, the decay's slowing above the knee) */
#define RV_K4 76311u             /* K at 4 kHz of 22.05 kHz, Q16 */
static uint32_t rv_log2q16(uint32_t v)                   /* log2(v / 2^16), Q24, for v >= 2^16 */
{
    uint64_t m;
    uint32_t r = 0, i;
    int32_t e = 31;
    while (!(v & 0x80000000u))
        v <<= 1, e--;
    m = v;                                               /* the mantissa, Q31, squared bit by bit */
    for (i = 0; i < 24u; i++) {
        m = (m * m) >> 31;
        if (m >> 32)
            m >>= 1, r |= 1u << (23u - i);
    }
    return ((uint32_t)(e - 16) << 24) + r;
}
static uint32_t rv_isqrt(uint64_t x)
{
    uint64_t r = 0, b = 1ull << 62;
    while (b > x)
        b >>= 2;
    for (; b; b >>= 2)
        if (x >= r + b)
            x -= r + b, r = (r >> 1) + b;
        else
            r >>= 1;
    return (uint32_t)r;
}
static int32_t rv_lpc(int32_t c, uint32_t sc)            /* (32-bit divides only: the target has no 64-bit one) */
{
    uint32_t q, v, p, r;
    if (c <= 0)
        return 0;
    c = c > 31000 ? 31000 : c;
    r = 32768u - (uint32_t)c;
    q = ((uint32_t)c << 16) / ((r * r) >> 15);                                  /* c / (1 - c)^2, Q16 */
    v = rv_log2q16(65536u + (uint32_t)(((uint64_t)q * RV_K4) >> 16));          /* the loss, log2 */
    p = (uint32_t)rv_exp2n((uint32_t)(((uint64_t)v * sc) >> 15));               /* the new loss, as 2^-loss */
    v = 0x80000000u / (p ? p : 1u);                                             /* 1 + K q, Q16 */
    v = v > (1u << 23) ? 1u << 23 : v;                                          /* (21 dB a pass: past any DAMP) */
    q = v > 65536u ? ((v - 65536u) << 8) / (RV_K4 >> 8) : 0;                   /* q, Q16 */
    v = ((2u * q) << 7) / ((2u * q + 65536u + rv_isqrt(((uint64_t)4u * q + 65536u) << 16)) >> 8);   /* c, Q15 */
    return v > 32767u ? 32767 : (int32_t)v;
}

static __attribute__((noinline)) void rv_params(void)   /* (XIP: rare; fx.c calls it FAR) */
{
    int32_t s = song.g[G_RSIZE], d = song.g[G_RDAMP], y = s > RV_KNEE ? s - RV_KNEE : 0, rho = 32768, a;
    uint32_t kt, e = 0, i;
    rv.size = s, rv.damp = d;
    kt = rv_kt(s - y);
    if (y) {                                             /* above the knee: the decay time x 2^(e / 2^24) */
        e = (uint32_t)y * ((10u << 24) / RV_STEPS) + (s >= 127 ? (uint32_t)RV_FRZ8 << 21 : 0u);
        rho = rv_exp2n(e);
        kt = (kt * (uint32_t)rho) >> 15;
        kt = kt < 1u ? 1u : kt;
    }
    a = 32768 - RV_DAMP(d);                              /* DAMP's low-pass, for the mean line */
    for (i = 0; i < 8u; i++) {
        rv.gain[i] = rv_exp2n((kt * RV_KG >> 5) * (RV_L[i] + RV_C));   /* (the low-passes add to the decay) */
        rv.kl[i] = (uint16_t)rv_lpc(a, RV_L[i] * (uint32_t)rho / RV_LREF);   /* (Jot) */
    }
    rv.dq = (RV_FEXC << 8) + y * (((RV_EXC - RV_FEXC) << 8) / (127 - RV_KNEE));
    rv.pd = y * RV_PD / (127 - RV_KNEE);
    rv.g1 = 24576 + y * (1638 / (127 - RV_KNEE));        /* 0.75 .. 0.8 */
    rv.g2 = 20480 + y * (2458 / (127 - RV_KNEE));        /* 0.625 .. 0.7 */
    rv.in = y ? (13000 * rv_exp2n(e >> RV_INSH)) >> 15 : 13000;
}
AINL void rv_lfo(void)          /* per block: each line's read, D whole samples and an allpass for the fraction */
{
    static const uint8_t RATE[8] = {RV_RATES};
    uint32_t j;
    for (j = 0; j < 8u; j++) {
        int32_t t, h;
        rv.ph[j] += LFO_INC[RATE[j]];
        t = ((RV_L[j] + RV_C) << 8) + ((osc_sine(rv.ph[j]) * rv.dq) >> 15) - 128;   /* Q8, the fraction 0.5 .. 1.5 */
        rv.mod[j] = t >> 8;
        h = ((t & 255) - 128) << 6;                       /* (fraction - 1) / 2, Q15 */
        rv.eta[j] = (int16_t)(-((h * (32768 - h + ((h * h) >> 15))) >> 15));   /* (1 - f) / (1 + f), its series */
    }
}
FX_STEP int32_t rv_step(int32_t in, int32_t *yr, int32_t *wr)
{
    int32_t x = rv_sat(mulq15(in, rv.in)), g1 = rv.g1, g2 = rv.g2, o0, o1, o2, o3, o4, o5, o6, o7;
    int32_t f0, f1, f2, f3, f4, f5, f6, f7, s, w, l, r;
    uint32_t rq = rv.rq;
    RV_WR(RB_PD, x);                                     /* the pre-delay */
    *wr |= x;
    x = RV_RD(RB_PD, rv.pd);
    x = rv_ap(x, RB_IA, RV_IA, g1, wr);
    x = rv_ap(x, RB_IB, RV_IB, g1, wr);
    x = rv_ap(x, RB_IC, RV_IC, g2, wr);
    x = rv_ap(x, RB_ID, RV_ID, g2, wr);
    /* a line: D samples back, then a first-order allpass for the fraction (Dattorro: a flat response, no treble
     * lost to the modulation as through a linear interpolation; its correction to the nearest: |eta| <= 1/3, at 0
     * in it still shrinks to 0; held to int16, the low-pass' product fits),
     * damped (lp = o + c (lp - o), toward o: exact where the damping is slight), its decay gain. The damping's
     * and the gain's products round at random (floor of the product plus a uniform 0 .. 2^15 - 1: unbiased, the
     * error a noise with no pattern): rounding toward 0 took ~1/2 LSB a product, a fixed hiss floor ~12 dB higher
     * and a tail that sank out early below ~-50 dB. A 0 still stays 0 (0 + u < 2^15), and a lone small value
     * dies at random, so the tail still rings out to exactly 0 */
#define RV_LN(k) { int32_t d_ = rv.mod[k], u0_ = RV_RD(RB_##k, d_), u1_ = RV_RD(RB_##k, d_ + 1); \
                   o##k = rv.ap[k] = rv_sat(u1_ + ((rv.eta[k] * (u0_ - rv.ap[k]) + 0x4000) >> 15)); } \
                 rq = rq * 1664525u + 1013904223u; \
                 rv.lp[k] = o##k + rv_rnd(rv.lp[k] - o##k, rv.kl[k], (int32_t)(rq >> 17)); \
                 f##k = rv_rnd(rv.lp[k], rv.gain[k], (int32_t)((rq >> 2) & 0x7FFFu))
    RV_LN(0); RV_LN(1); RV_LN(2); RV_LN(3); RV_LN(4); RV_LN(5); RV_LN(6); RV_LN(7);
#undef RV_LN
    s = f0 + f1 + f2 + f3 + f4 + f5 + f6 + f7;
    s = (s + ((s >> 31) & 3)) >> 2;                      /* 2/8 of the sum, toward 0 */
    rv.rq = rq;
#define RV_WL(k, v) w = rv_sat(s - f##k + (v)); RV_WR(RB_##k, w); *wr |= w
    RV_WL(0, x); RV_WL(1, -x); RV_WL(2, x); RV_WL(3, -x); RV_WL(4, x); RV_WL(5, -x); RV_WL(6, x); RV_WL(7, -x);
#undef RV_WL
    l = o0 - o2 + o4 - o6;
    r = o1 - o3 + o5 - o7;
    rv.p--;
    *yr = mulq15(r, RV_OUT) << 2;
    return mulq15(l, RV_OUT) << 2;
}
#undef rv_params
#undef rv_lfo
#undef rv_step
#undef RB_END
#undef RV_DAMP
#undef RV_IN
#undef RV_OUT
#endif

static void rv_clear(void)                               /* the ring and the filters to 0 (fx.c rev_tank_clear) */
{
    uint32_t i;
    for (i = 0; i < sizeof rev_line / 2u; i++)
        rev_line[i] = 0;
    for (i = 0; i < 8u; i++)
        rv.lp[i] = 0;
#if FELUCCA_REV_FDN8
    for (i = 0; i < 8u; i++)
        rv.ap[i] = 0;
#endif
}
