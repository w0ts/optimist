/* SPDX-License-Identifier: GPL-3.0-only */
/* The master COMP and LIMIT (FELUCCA_MASTER_COMP): the design, the sources and what is left for later (a sidechain key,
 * per-track inserts) are in master_comp.h. Included by fx.c (after master_out); the per-sample code runs from main
 * RAM (HOT2: RAMTEXT is full), the helpers it inlines are AINL. Tables: tools-free, from the formulas beside them
 * (tests/master_comp_test.c recomputes them in double precision and checks every entry). */
#include "master_comp.h"

/* round(4096 log2(1 + i / 64)), i = 0..64: the mantissa's log2, Q12 */
static const uint16_t MC_LOG2[65] = {
    0, 92, 182, 271, 358, 445, 530, 613, 696, 778, 858, 937, 1016, 1093, 1169, 1244, 1319, 1392, 1465, 1536, 1607,
    1677, 1746, 1814, 1882, 1949, 2015, 2080, 2145, 2208, 2272, 2334, 2396, 2457, 2518, 2578, 2637, 2696, 2754, 2812,
    2869, 2926, 2982, 3037, 3092, 3146, 3200, 3254, 3307, 3359, 3412, 3463, 3514, 3565, 3615, 3665, 3715, 3764, 3812,
    3861, 3908, 3956, 4003, 4050, 4096};
/* round(32768 2^(i / 64)), i = 0..64: 2^frac, Q15 */
static const uint32_t MC_EXP2[65] = {
    32768, 33125, 33486, 33850, 34219, 34591, 34968, 35349, 35734, 36123, 36516, 36914, 37316, 37722, 38133, 38548,
    38968, 39392, 39821, 40255, 40693, 41136, 41584, 42037, 42495, 42958, 43425, 43898, 44376, 44859, 45348, 45842,
    46341, 46846, 47356, 47871, 48393, 48920, 49452, 49991, 50535, 51085, 51642, 52204, 52773, 53347, 53928, 54515,
    55109, 55709, 56316, 56929, 57549, 58176, 58809, 59449, 60097, 60751, 61413, 62081, 62757, 63441, 64132, 64830,
    65536};
/* the settings' values (params.c N_CRAT, N_CATK, N_CREL, N_CCEIL: the same order). Steps of a one-pole a sub-block
 * (11025 Hz), Q20: 1 - exp(-1 / (tau 11025)) */
static const uint16_t MC_SLOPE[8] = {5461, 8192, 10923, 12288, 13653, 14336, 15565, 16384};   /* 1 - 1 / R, Q14 */
static const int32_t MC_KATK[6] = {625243, 273590, 90923, 31228, 9468, 3166};                 /* 0.1 .. 30 ms */
static const int32_t MC_KREL[6] = {1900, 951, 475, 317, 158, 79};                            /* 50 .. 1200 ms */
#define MC_KFAST 1584                    /* AUTO: the fast release, 60 ms */
#define MC_KSLOW 79                     /* AUTO: the slow release, 1.2 s */
#define MC_KCHG 238                     /* AUTO: the slow envelope charges, 400 ms */
#define MC_KNEE 256                     /* the soft knee's width: 1 octave of log2 Q8, 6.02 dB */
#define MC_FLOOR8 (4 << 8)              /* below |x| 16 (-66 dB): the detector's floor */
/* the ceilings -0.1 .. -6 dB below 32767: floor(256 log2 C) less 2 (a margin of 0.047 dB over the tables' rounding),
 * and the largest |x| that needs no reduction (2^(c8 / 256), floored) */
static const int16_t MLIM_C8[7] = {3833, 3825, 3816, 3795, 3752, 3710, 3582};
static const int32_t MLIM_CLIN[7] = {32152, 31463, 30706, 29009, 25820, 23045, 16295};
#define MLIM_K20 297                    /* the release, 80 ms a sample at 44.1 kHz: 1 - exp(-1 / 3528), Q20 */

/* log2(a) Q8 (rounded), a >= 1 (a < 16: the floor). Exponent by halving, the mantissa's top 6 bits index MC_LOG2,
 * the next 9 interpolate (Q12, then rounded to Q8: within 0.55 / 256 octave) */
AINL int32_t mc_log2(uint32_t a)
{
    uint32_t e = 0, n, i, f;
    if (a < 16u)
        return MC_FLOOR8;
    if (a >> 16) e = 16u;
    if (a >> (e + 8u)) e += 8u;
    if (a >> (e + 4u)) e += 4u;
    if (a >> (e + 2u)) e += 2u;
    if (a >> (e + 1u)) e += 1u;                         /* e = floor(log2 a) (4 .. 31) */
    n = e > 15u ? a >> (e - 15u) : a << (15u - e);      /* 1.xxx, the top bit at 15 */
    i = (n >> 9) & 63u;
    f = n & 511u;
    return (int32_t)(e << 8) + (int32_t)((MC_LOG2[i] + (((MC_LOG2[i + 1u] - MC_LOG2[i]) * f) >> 9) + 8u) >> 4);
}

/* 2^(x / 65536) in Q13 (x: log2 Q16, at most +2.99 octaves; far below 0: 0) */
AINL int32_t mc_exp2(int32_t x)
{
    int32_t n = x >> 16, sh = 2 - n;
    uint32_t f = (uint32_t)x & 0xFFFFu, i = f >> 10, m = MC_EXP2[i] + (((MC_EXP2[i + 1u] - MC_EXP2[i]) * (f & 1023u)) >> 10);
    return sh >= 31 ? 0 : sh < 0 ? (int32_t)(m << -sh) : (int32_t)(m >> sh);
}

/* x * g / 8192, g Q13 < 2^17: the product split so it never overflows 32 bits; exact at g = 8192 */
AINL int32_t mc_mul13(int32_t x, int32_t g)
{
    return (((x >> 8) * g) >> 5) + (((x & 255) * g) >> 13);
}

/* ---- the compressor (an instance: master_comp.h) */

/* the settings of a compressor from its parameters (the master's: GLO > COMP, LIMIT > GAIN) */
static HOT2 void mc_settings(mc_set_t *s, int32_t thr, int32_t rat, int32_t atk, int32_t rel, int32_t gain)
{
    s->on = thr < 0;
    s->thr8 = (15 << 8) + ((thr * 10885) >> 8);         /* dB -> log2 Q8: 256 / 6.0206 a dB */
    s->slope14 = MC_SLOPE[rat & 7];
    s->katk = MC_KATK[clamp(atk, 0, 5)];
    s->krel = rel >= 6 ? 0 : MC_KREL[clamp(rel, 0, 5)];
    s->gain16 = gain * 10885;                           /* dB -> log2 Q16 */
}

/* the gain reduction the static curve asks for at level x8 (log2 Q8), log2 Q16 (Giannoulis et al. eq. 4: below the
 * knee none, in it the quadratic, above it (x - T)(1 - 1 / R)) */
AINL int32_t mc_curve(const mc_set_t *s, int32_t x8)
{
    int32_t d = x8 - s->thr8;
    if (2 * d <= -MC_KNEE)
        return 0;
    if (2 * d < MC_KNEE) {
        int32_t u = d + MC_KNEE / 2;                    /* 0 .. 256 */
        return (u * u * s->slope14) >> 15;              /* (u^2 / 2W) s, in Q16: << 8 >> 9 >> 14 */
    }
    return (d * s->slope14) >> 6;
}

/* one step of a one-pole from y toward to (log2 Q16, |to - y| < 2^21), k Q20 < 2^20: the difference at Q8 so the product
 * fits 32 bits. Rising it may stop up to 1/256 octave (0.02 dB) short; falling it moves at least 1 and lands on to */
AINL int32_t mc_rise(int32_t y, int32_t to, int32_t k)
{
    return y + ((((to - y) >> 8) * k) >> 12);
}
AINL int32_t mc_fall(int32_t y, int32_t to, int32_t k)
{
    y += ((((to - y) >> 8) * k) >> 12) - 1;
    return y < to ? to : y;
}

/* n samples (a multiple of MC_SUB) of l, r through compressor c, its level taken from the key kl, kr (the master: the
 * same buffers, read before they are written: no copy). Each sub-block: the key's peak -> log2 -> the curve -> the
 * ballistics -> make-up -> 2^x, ramped linearly over the sub-block */
static HOT2 __attribute__((noinline)) void mc_run(mc_t *c, const mc_set_t *s, const int32_t *kl, const int32_t *kr,
                                                 int32_t *l, int32_t *r, uint32_t n)
{
    uint32_t i, j;
    int32_t gr = c->gr16, slow = c->slow16, g0 = c->g13, pk = c->gr_pk;
    for (i = 0; i < n; i += MC_SUB) {
        uint32_t a = 0;
        int32_t want, o, g1, dg;
        for (j = i; j < i + MC_SUB; j++) {               /* the key's peak, both sides */
            uint32_t x = (uint32_t)(kl[j] < 0 ? -kl[j] : kl[j]), y = (uint32_t)(kr[j] < 0 ? -kr[j] : kr[j]);
            x = x > y ? x : y;
            a = x > a ? x : a;
        }
        want = s->on ? mc_curve(s, mc_log2(a < 0xFFFFFu ? a : 0xFFFFFu)) : 0;   /* (+30 dB: the products fit) */
        if (want > gr)                                  /* the ballistics on the reduction (log domain) */
            gr = mc_rise(gr, want, s->katk);
        else if (gr > want)
            gr = mc_fall(gr, want, s->krel ? s->krel : MC_KFAST);
        o = gr;
        if (!s->krel) {                                 /* AUTO: a slow envelope, charged by a long squeeze only */
            if (gr > slow)
                slow = mc_rise(slow, gr, MC_KCHG);
            else if (slow > gr)
                slow = mc_fall(slow, gr, MC_KSLOW);
            o = slow > gr ? slow : gr;
        } else {
            slow = 0;
        }
        if (o > pk)
            pk = o;
        g1 = mc_exp2(s->gain16 - o);
        dg = (g1 - g0) / (int32_t)MC_SUB;
        if (g0 != 8192 || g1 != 8192)                   /* (unity: the samples as they are) */
            for (j = 0; j < MC_SUB; j++) {
                int32_t g = j + 1u == MC_SUB ? g1 : g0 + dg * (int32_t)(j + 1u);
                l[i + j] = mc_mul13(l[i + j], g);
                r[i + j] = mc_mul13(r[i + j], g);
            }
        g0 = g1;
    }
    c->gr16 = gr, c->slow16 = slow, c->g13 = g0, c->gr_pk = pk;
}

/* ---- the limiter */
static mlim_t mlim;

AINL void mlim_reset(void)                             /* (inlined: called from RAM code) */
{
    uint32_t i;
    for (i = 0; i < MLIM_N; i++) {
        mlim.ring[i] = 0;
        mlim.dl[i] = mlim.dr[i] = 0;
    }
    mlim.hold = mlim.m2 = mlim.env16 = mlim.sum = 0;
    mlim.cnt = MLIM_HOLD;
    mlim.pos = 0;
}

/* one sample: in, its need recorded; out, the sample MLIM_N before with the reduction averaged over the box */
AINL void mlim_step(int32_t *l, int32_t *r)
{
    int32_t al = *l < 0 ? -*l : *l, ar = *r < 0 ? -*r : *r, a = al > ar ? al : ar, q = 0, h16, e, s8, ol, orr;
    uint32_t p = mlim.pos;
    if (a > mlim.clin) {
        q = mc_log2((uint32_t)a) - mlim.c8;
        q = q < 0 ? 0 : q;
    }
    if (q >= mlim.hold) {                               /* the running maximum over MLIM_HOLD samples: */
        mlim.hold = q;                                  /* a new peak restarts it; else at the end of its time the */
        mlim.cnt = MLIM_HOLD;                           /* largest since (m2) takes over */
        mlim.m2 = 0;
    } else {
        if (q > mlim.m2)
            mlim.m2 = q;
        if (--mlim.cnt == 0) {
            mlim.hold = mlim.m2;
            mlim.cnt = MLIM_HOLD;
            mlim.m2 = 0;
        }
    }
    h16 = mlim.hold << 8;
    e = mlim.env16;
    if (h16 >= e)
        e = h16;                                        /* attack: at once (the box ramps it) */
    else
        e -= (((e - h16) >> 4) * MLIM_K20 >> 16) + 1;   /* release, 80 ms */
    e = e < h16 ? h16 : e;
    mlim.env16 = e;
    e = (e + 255) >> 8;                                 /* (rounded up) */
    mlim.sum += e - mlim.ring[p];
    mlim.ring[p] = (uint16_t)e;
    s8 = (mlim.sum + (int32_t)MLIM_N - 1) >> 6;         /* the box's mean, rounded up (MLIM_N 64) */
    ol = mlim.dl[p], orr = mlim.dr[p];
    mlim.dl[p] = *l, mlim.dr[p] = *r;
    mlim.pos = (p + 1u) & (MLIM_N - 1u);
    if (s8) {
        if (s8 != mlim.s8) {                            /* (2^-s, only when it moved) */
            mlim.s8 = s8;
            mlim.g13 = mc_exp2(-(s8 << 8));
        }
        ol = mc_mul13(ol, mlim.g13);
        orr = mc_mul13(orr, mlim.g13);
        if (s8 > mlim.gr_pk)
            mlim.gr_pk = s8;
    }
    *l = clamp(ol, -32767, 32767);                      /* (never reached: the ceiling is lower; a last guard) */
    *r = clamp(orr, -32767, 32767);
}
_Static_assert(MLIM_N == 64u, "mlim_step: the box's mean is >> 6");
static void mlim_sample(int32_t *l, int32_t *r) { mlim_step(l, r); }   /* (one sample: tests/master_comp_test.c) */

/* LIMIT on: the output stage of a block (fx.c mix_finish's loop with the brickwall in place of the limiter and the knee):
 * the master volume ramped over the block exactly as there, master_pre (the DC blocker, the low cut), the limiter */
static HOT2 __attribute__((noinline)) void mlim_block(const int32_t *ml, const int32_t *mr, int32_t *out, uint32_t n,
                                                     int32_t m0, int32_t m1)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        int32_t m = m0 + (((m1 - m0) * (int32_t)i) >> CTL_LOG2);
        int32_t l = ((ml[i] >> 2) * m) >> 10;
        int32_t r = ((mr[i] >> 2) * m) >> 10;
        master_pre(&l, &r);
        mlim_step(&l, &r);
        out[2u * i] = l;
        out[2u * i + 1u] = r;
    }
}

/* ---- the master's */
static mc_t mc = {0, 0, 8192, 0};
static mc_set_t mc_s;

/* fx.c mix_finish, once a block on the mix bus (before the master volume): the settings, the compressor and the gain,
 * and the limiter's switch for mix_finish. Off (THRS OFF, GAIN 0): nothing is touched */
static HOT2 __attribute__((noinline)) void mc_master(int32_t *l, int32_t *r, uint32_t n)
{
    int32_t ceil = song.g[G_CCEIL];
    if (ceil > 0 && ceil <= 7) {
        if (!mlim.on || mlim.c8 != MLIM_C8[ceil - 1]) {
            if (!mlim.on)
                mlim_reset();                           /* (from OFF: an empty lookahead) */
            mlim.c8 = MLIM_C8[ceil - 1];
            mlim.clin = MLIM_CLIN[ceil - 1];
        }
        mlim.on = 1;
    } else {
        mlim.on = 0;
    }
    mc_settings(&mc_s, song.g[G_CTHR], song.g[G_CRAT], song.g[G_CATK], song.g[G_CREL], song.g[G_CGAIN]);
    if (!mc_s.on && !mc_s.gain16) {                     /* off: the next start from rest */
        mc.gr16 = mc.slow16 = 0;
        mc.g13 = 8192;
        return;
    }
    mc_run(&mc, &mc_s, l, r, l, r, n);
}

/* the meters (main loop, meters.c): the largest reduction since the last take, quarter dB (0..127 each) */
static void mc_take_gr(uint32_t *comp_q, uint32_t *lim_q)
{
    int32_t c, m;
    fm1_irq_off();
    c = mc.gr_pk, m = mlim.gr_pk;
    mc.gr_pk = mlim.gr_pk = 0;
    fm1_irq_on();
    c = (c * 3083) >> 23;                               /* log2 Q16 (< 2^19) -> dB x 4 (24.08 a octave) */
    m = (m * 3083) >> 15;                               /* log2 Q8 -> dB x 4 */
    *comp_q = (uint32_t)(c > 127 ? 127 : c);
    *lim_q = (uint32_t)(m > 127 ? 127 : m);
}
