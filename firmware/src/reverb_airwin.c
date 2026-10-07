/* SPDX-License-Identifier: GPL-3.0-only
 * The network (its delay lengths, the 4 x 4 Householder stages, the cross-coupled left and right) is Airwindows'
 * VerbTiny: Copyright (c) 2018 Chris Johnson, airwindows (https://github.com/airwindows/airwindows; "Airwindows uses
 * the MIT license": LICENSES/MIT-airwindows.txt, LICENSING.md). */
/* AIRWIN (registry.h FELUCCA_REV_AIRWIN): Airwindows' VerbTiny on FX > REVERB > TYPE ("VTINY"), beside ROOM, PLATE,
 * FDN8 and SPRING (fx.c rev_bus picks one at run time). Included by fx.c after reverb_alt.c: it runs at 22.05 kHz
 * behind fx.c's half-band filters (rev_half_pair) in reverb_alt.c's ring (rev_line, RV_N int16, the same write
 * index rv.p counting down) and shares its state rv (size, damp, lp[8]): one tank runs at a time.
 *
 * VerbTiny (Chris Johnson, 2025: "a classic artificial reverb that expands reverb shape", a 4 x 4 Householder matrix
 * found by a genetic search for unusually high peak energy against RMS, "the tssshhhh of an early reverb device"):
 * per side, sixteen lines in four stages of four; each stage's four outputs mixed by the Householder matrix
 * (out_k = in_k - sum / 2: one sum, a shift and four subtracts, no multiply); the left's last stage feeds the right's
 * first, the right's the left's (one loop through both: every echo reaches both sides). The right walks the same
 * sixteen lengths in columns (D H L P, C G K O, B F J N, A E I M) where the left walks rows (A B C D, E F G H, ...).
 * Its lengths (5 .. 116 ms at 44.1 kHz) halved for 22.05 kHz and scaled to the ring: x 0.87 in 16 KB (RV_N 8192,
 * the default and REV_POOL; 46 ms a pass), x 0.43 in 8 KB (REV_HALF; 23 ms), x 1 (VerbTiny's own size) in 32 KB
 * (with FDN8 in the pool).
 * What differs from VerbTiny, and why:
 *   - Its "main" network only: VerbTiny's dual-mono second network (Wider) doubles the RAM and is silent at its
 *     default (Wider 0).
 *   - Its loop gain (Replace) and tone (Derez, Filter: the reverb run at a lower rate) become SIZE and DAMP: the loop
 *     gain per pass gives the ROOM's RT60 for SIZE (as PLATE and FDN8: rv_kt), and above SIZE 90 FDN8's long top (the
 *     decay time doubling every 11 steps to ~11 s at 126, a near-freeze at 127; VerbTiny's Replace 1 freezes); a
 *     one-pole on each of the eight crossing paths loses the ROOM's dB per second at 4 kHz for DAMP (VerbTiny has no
 *     damping in the loop: its tone comes from running slower).
 *   - The output taps: alternating signs (left + - - +, right + + - -) where VerbTiny sums the last stage's four. The
 *     plain sum takes the matrix' one "all ones" direction (the input's): with a mono send its left and right
 *     correlated in the tail (iacc 0.30 at SIZE 120) and it rang more (ripple 2.5 dB, ring 3.4); with the signs:
 *     iacc 0.03, ripple 2.1 dB, ring 3.0 (float references; this port 0.02 .. 0.04, 2.0 .. 2.1 dB, 3.2: fm1-firmware
 *     screens/airwindows-2026-10-08).
 * Fixed point: lines int16 (saturated on write), the sums int32; the Householder's half rounds toward 0 (the four
 * outputs' energy never grows: |s| - 1 less); the damping and the loop gain round at random (rv_rnd, as FDN8: no
 * steady hiss, unbiased) above RV_RT and toward 0 below, so a tail with no input reaches exactly 0 and the bus' idle
 * skip (fx.c) works. */
#if RV_N == 4096u                /* 8 KB (REV_HALF) */
enum { RVA_A = 29, RVA_B = 11, RVA_C = 12, RVA_D = 271, RVA_E = 45, RVA_F = 102, RVA_G = 118, RVA_H = 6, RVA_I = 20,
       RVA_J = 245, RVA_K = 303, RVA_L = 214, RVA_M = 283, RVA_N = 41, RVA_O = 272, RVA_P = 22 };
#elif RV_N == 8192u              /* 16 KB (B and C one sample apart, as VerbTiny's 52 / 53) */
enum { RVA_A = 60, RVA_B = 23, RVA_C = 24, RVA_D = 549, RVA_E = 91, RVA_F = 206, RVA_G = 239, RVA_H = 13, RVA_I = 40,
       RVA_J = 495, RVA_K = 612, RVA_L = 433, RVA_M = 572, RVA_N = 84, RVA_O = 550, RVA_P = 45 };
#else                            /* 32 KB: VerbTiny's own */
enum { RVA_A = 68, RVA_B = 26, RVA_C = 27, RVA_D = 631, RVA_E = 105, RVA_F = 237, RVA_G = 275, RVA_H = 15, RVA_I = 46,
       RVA_J = 569, RVA_K = 704, RVA_L = 498, RVA_M = 658, RVA_N = 96, RVA_O = 632, RVA_P = 52 };
#endif
#define RVA_SUM (RVA_A + RVA_B + RVA_C + RVA_D + RVA_E + RVA_F + RVA_G + RVA_H + RVA_I + RVA_J + RVA_K + RVA_L + \
                 RVA_M + RVA_N + RVA_O + RVA_P)  /* four passes' length: a pass is a quarter */
#define RVA_PREF 1009                    /* the pass the damping's fit was made for (16 KB), samples */
#define RVA_DAMP(d) rv_damp(d, 32752, 12422, 32741, -167)   /* 1 - c per RVA_PREF pass (airwin_dampfit.py) */
/* the ring: each line's base (its length + 1), the left's sixteen then the right's */
#define RVA_LINES(X, s) X(A, s) X(B, s) X(C, s) X(D, s) X(E, s) X(F, s) X(G, s) X(H, s) X(I, s) X(J, s) X(K, s) \
                        X(L, s) X(M, s) X(N, s) X(O, s) X(P, s)
#define RVA_BASE(k, s) RB##s##k, RB##s##k##_END = RB##s##k + RVA_##k,
enum { RVA_LINES(RVA_BASE, L) RVA_LINES(RVA_BASE, R) RBA_END };
#undef RVA_BASE
_Static_assert(RBA_END <= RV_N, "AIRWIN: the ring holds every line");
#define RVA_OUT (RV_N == 4096u ? 4250 : RV_N == 8192u ? 5540 : 5880)   /* the taps' sum per side, Q17: the wet level as
                                                                       * the ROOM's (per ring) */
#define RVA_KNEE 90                      /* above it FDN8's long top (reverb_alt.c): */
#define RVA_STEPS 110                    /* SIZE steps per doubling of the decay time, x 10 */
#define RVA_FRZ8 18                      /* SIZE 127: this many eighths of a doubling more */
#define RVA_BUSY() (rva.f[0] | rva.f[1] | rva.f[2] | rva.f[3])
static struct {
    int32_t f[4];                /* the right's last stage, into the left's first (next sample) */
    int32_t g, c;                /* the loop gain per pass, the damping's pole (Q15) */
    uint32_t rq;                 /* the random rounding's generator */
} rva;

static __attribute__((noinline)) void rva_params(void)   /* SIZE / DAMP -> the gain, the damping (XIP; fx.c FAR) */
{
    int32_t s = song.g[G_RSIZE], d = song.g[G_RDAMP], y = s > RVA_KNEE ? s - RVA_KNEE : 0, rho = 32768;
    uint32_t kt;
    rv.size = s, rv.damp = d;
    kt = rv_kt(s - y);
    if (y) {                                             /* above the knee: the decay time x 2^(e / 2^24) */
        uint32_t e = (uint32_t)y * ((10u << 24) / RVA_STEPS) + (s >= 127 ? (uint32_t)RVA_FRZ8 << 21 : 0u);
        rho = rv_exp2n(e);
        kt = (kt * (uint32_t)rho) >> 15;
        kt = kt < 1u ? 1u : kt;
    }
    rva.g = rv_exp2n((kt * RVA_SUM) >> 2);
    rva.c = rv_lpc(32768 - RVA_DAMP(d), (uint32_t)((RVA_SUM * (uint32_t)rho / 4u) / RVA_PREF));   /* (the treble's
                                         * RT60 keeps its ratio to the bass' above the knee, as FDN8's) */
}
/* one crossing path: the damping (lp = o + c (lp - o), toward o) and the loop gain, both rounded at random */
AINL int32_t rva_fb(int32_t o, int32_t *lp, uint32_t *rq)
{
    *rq = *rq * 1664525u + 1013904223u;
    *lp = o + rv_rnd(*lp - o, rva.c, (int32_t)(*rq >> 17));
    return rv_rnd(*lp, rva.g, (int32_t)((*rq >> 2) & 0x7FFFu));
}
/* a stage: read the four lines (o*), write v* into them; then the Householder mix of what was read into v* */
#define RVA_STAGE(s, a, b, c, d)                                                                                    \
    o0 = RV_RD(RB##s##a, RVA_##a), o1 = RV_RD(RB##s##b, RVA_##b), o2 = RV_RD(RB##s##c, RVA_##c),                    \
    o3 = RV_RD(RB##s##d, RVA_##d);                                                                                  \
    v0 = rv_sat(v0), v1 = rv_sat(v1), v2 = rv_sat(v2), v3 = rv_sat(v3);                                             \
    RV_WR(RB##s##a, v0), RV_WR(RB##s##b, v1), RV_WR(RB##s##c, v2), RV_WR(RB##s##d, v3);                             \
    *wr |= v0 | v1 | v2 | v3;                                                                                       \
    t = o0 + o1 + o2 + o3;                                                                                          \
    t = (t + ((t >> 31) & 1)) >> 1;                                                                                 \
    v0 = o0 - t, v1 = o1 - t, v2 = o2 - t, v3 = o3 - t
FX_STEP int32_t rva_step(int32_t in, int32_t *yr, int32_t *wr)
{
    int32_t x = rv_sat(mulq15(in, 13000)), o0, o1, o2, o3, v0, v1, v2, v3, t, l;
    uint32_t rq = rva.rq;
    v0 = x + rva_fb(rva.f[0], &rv.lp[0], &rq);           /* the left: the right's last stage in */
    v1 = x + rva_fb(rva.f[1], &rv.lp[1], &rq);
    v2 = x + rva_fb(rva.f[2], &rv.lp[2], &rq);
    v3 = x + rva_fb(rva.f[3], &rv.lp[3], &rq);
    RVA_STAGE(L, A, B, C, D);
    RVA_STAGE(L, E, F, G, H);
    RVA_STAGE(L, I, J, K, L);
    RVA_STAGE(L, M, N, O, P);
    l = o0 - o1 - o2 + o3;
    v0 = x + rva_fb(v0, &rv.lp[4], &rq);                 /* the right: the left's last stage in, the same sample */
    v1 = x + rva_fb(v1, &rv.lp[5], &rq);
    v2 = x + rva_fb(v2, &rv.lp[6], &rq);
    v3 = x + rva_fb(v3, &rv.lp[7], &rq);
    RVA_STAGE(R, D, H, L, P);
    RVA_STAGE(R, C, G, K, O);
    RVA_STAGE(R, B, F, J, N);
    RVA_STAGE(R, A, E, I, M);
    rva.f[0] = v0, rva.f[1] = v1, rva.f[2] = v2, rva.f[3] = v3;
    rva.rq = rq;
    rv.p--;
    *yr = mulq15(o0 + o1 - o2 - o3, RVA_OUT) << 2;
    return mulq15(l, RVA_OUT) << 2;
}
#undef RVA_STAGE
static void rva_clear(void)                              /* (the ring and rv.lp: reverb_alt.c rv_clear) */
{
    rva.f[0] = rva.f[1] = rva.f[2] = rva.f[3] = 0;
}
