/* SPDX-License-Identifier: GPL-3.0-only */
/* The further INSERT types of the tracks' insert slots (fx_slots.c S1 S2, 2026-10-13): FOLD FUZZ PHSR FLNG FRMT WIDE,
 * and CHRS ECHO (a count of instances: fxi_kern.c, fxi.c), GATER (2026-10-14: gater.c), a builder item each (registry.h FELUCCA_INS_*). Included by fx.c; fx.c ins_one (a part) and dbus_ins (the drum bus)
 * call them.
 *   FOLD FUZZ PHSR FLNG  after Felucca 1.5.6's track INSERT (hugelton/Felucca 9f6a885, firmware/src/fx.c ins_shape,
 *                        ins_drive, ins_swept; perform.c PH_A, pf_tri), Copyright (C) 2026 Leo Kuroshita
 *                        (@kurogedelic), Hügelton Instruments, GPL-3.0-only.
 *   FRMT WIDE            after Melodee 1.3.1's inserts (keremimo/melodee next 757b65b, firmware/src/fx.c fx_design's
 *                        FX_FORMANT vowels and FX_WIDTH, fx_return), by Kerem Kilic, GPL-3.0-only.
 * Rewritten here in fixed point with no divide in the audio path (tools/div_audit.py) and no packed-16 SIMD.
 * One amount a type (the slots' one knob, D4: core.h P_TFOLD ..), 0 off:
 *   FOLD  the drive into a triangle folder (reflected at the clip's full scale): DIST's drive relative to the track's
 *         peak, its low band kept dry, its tone and make-up (fx.c dist_gains), 0.5x .. ~13x (Felucca's range)
 *   FUZZ  the same drive (0.5x .. 48x) into Felucca's FUZZ: a hard top, a soft halved bottom; a DC blocker after
 *   PHSR  four first-order all-pass stages swept by a triangle at ~0.5 Hz over 150 Hz .. 4.8 kHz (Felucca's PH_A):
 *         (dry + swept) / 2; 1..64 its share, 64..127 its feedback (0 .. 0.7)
 *   FLNG  a 256-sample line swept 0.25 .. 5 ms by a squared triangle at ~0.25 Hz: (dry + delayed) / 2; 1..64 its share,
 *         64..127 its feedback (0 .. 0.85)
 *   FRMT  Melodee's five vowels' three formants (A E I O U, its table) as three resonant band-passes (Q 4, +10 dB)
 *         over the dry at half, the vowel along the amount (1 A .. 127 U); Melodee picked one at random each LFO turn
 *   WIDE  Melodee's width: the left through four swept all-passes (~0.1 Hz, around 1 kHz), the right dry, the
 *         amount the left's share. A part is mono until its pan: it gets the difference d as mid + side (its block
 *         + d / 2, the side d / 2 to the left and from the right after its level: ifx_side_mix); the drum bus its L.
 * Each fades its wet share in and out over 2.9 ms (a change of amount from or to 0 makes no click); at 0 and faded
 * nothing runs (the track sample for sample as without). A part's: one state a type a part; the drum bus's: L and R
 * only (its sends' accumulators, the drum sounds' own sends, pass them by, as every bus insert at QUALITY). */
#if INS_FX
#define IFX_NI (NPART + 12u)                            /* the instances: the parts, the drum bus's L, R, the master's L, R,
                                                         * the USB input's L, R (fx-rework stage 3), the returns A B C's
                                                         * L, R (fx-pool 2026-10-15: IFX_RT) */
#define IFX_AW_UT (NTRK + 1u)                           /* ifx_awake's entry for the USB input */
#define IFX_AW_MST NTRK                                 /* ifx_awake's entry for the master */
#define IFX_SLOPE 256                                   /* the wet share's step a sample, Q15: 0 .. all in 128 (2.9 ms) */
AINL int32_t ifx_mix(int32_t x, int32_t y, int32_t w) { return w >= 32768 ? y : x + mulq16(y - x, (uint32_t)w << 1); }
AINL uint32_t ifx_tri(uint32_t q)                       /* a triangle of phase q: 0 .. 65534 .. 0 (Felucca's pf_tri) */
{
    uint32_t t = q >> 16;
    return t < 32768u ? t << 1 : (65535u - t) << 1;
}
/* n bytes at p (a multiple of 4, aligned) to 0: a loop of words (memset is XIP code: no direct call from RAM code) */
AINL void ifx_zero(void *p, uint32_t n)
{
    typedef uint32_t w_t __attribute__((may_alias));
    w_t *w = (w_t *)p;
    for (n >>= 2; n; n--)
        *w++ = 0;
}
#define IFX_INC(r) (LFO_INC[r] >> CTL_LOG2)              /* an LFO_INC rate a sample (gen_tables.py: 0.05 x 800^(r / 127) Hz) */
/* the input's silence: a fixed-point state can stop a few units off 0 (a one-pole's rounding, an all-pass's limit cycle),
 * so after IFX_QUIET blocks of exact silence in an instance rests (its states 0: silence out, nothing run). *q: the
 * blocks of silence so far; returns 1 while resting (b is 0: nothing to do) */
#define IFX_QUIET 64u                                   /* 46 ms: every tail here but FLNG's is far below a unit by then */
AINL int ifx_quiet(uint8_t *q, const int32_t *b, uint32_t n)
{
    if (fx_any(b, n)) {
        *q = 0;
        return 0;
    }
    if (*q < IFX_QUIET)
        (*q)++;
    return *q >= IFX_QUIET;
}

/* ---- FOLD, FUZZ: DIST's drive (fx.c dist_gains: relative to the peak, the low band dry, the tone, the make-up) into
 * another shape. a: the amount (0: fading out at the last one) */
#if FELUCCA_INS_FOLD || FELUCCA_INS_FUZZ
typedef struct {
    dist_t d;
    int32_t w, dc;                                      /* the wet share (Q15), FUZZ's DC blocker (Q8) */
    uint8_t a;                                          /* the last amount (a fade-out runs at it) */
    uint8_t q;                                          /* blocks of silence in (ifx_quiet) */
} ifx_drv_t;
#if FELUCCA_INS_FOLD
static ifx_drv_t ifx_fold[IFX_NI];
#endif
#if FELUCCA_INS_FUZZ
static ifx_drv_t ifx_fuzz[IFX_NI];
#endif
#define IFX_FOLD_D(a) (1 + ((((a) - 1) * 183) >> 8))   /* FOLD's DST: 1 .. 91 (0.5x .. 12.8x, Felucca's 1x .. 12.8x) */
static HOT2 __attribute__((noinline)) void ifx_drive(ifx_drv_t *s, uint32_t fuzz, int32_t a, int32_t *b, uint32_t n)
{
    dist_g_t g;
    int32_t i, m = 0, lo, l1, dc = s->dc, w = s->w, to = a ? 32768 : 0;
    if (!a && !w) {
        s->d.env = 0;                                   /* (off: it restarts from rest) */
        return;
    }
    if (a)
        s->a = (uint8_t)a;
    if (ifx_quiet(&s->q, b, n)) {                       /* (resting on silence: its states 0) */
        s->d.env = 0, s->d.lo = s->d.l1 = 0, s->dc = 0;
        s->w = to;
        return;
    }
    if (!w)
        dc = 0;
    a = s->a;
    FAR(dist_gains)(&s->d, fuzz ? a : IFX_FOLD_D(a), b, n, &g);
    lo = s->d.lo, l1 = s->d.l1;
    for (i = 0; i < (int32_t)n; i++) {
        int32_t x = b[i], u, y;
        w += clamp(to - w, -IFX_SLOPE, IFX_SLOPE);
        lo += (x - lo + 64) >> 7;                       /* ~55 Hz: the low band, kept out of the shape */
        u = clamp((x - lo) >> g.sh, -32767, 32767);
        y = u < 0 ? -u : u;
        m = y > m ? y : m;
        u = (u * g.gi) >> g.us;                         /* the drive, the clip's full scale 32768 */
        if (fuzz) {
            y = u > 0 ? (u > 20000 ? 20000 : u) : softclip(u) >> 1;   /* a hard top, a soft halved bottom */
            dc += ((y << 8) - dc) >> 9;                 /* ~14 Hz (its halves are lopsided) */
            y -= dc >> 8;
        } else {
            y = (int32_t)(((uint32_t)u + 32768u) & 0x1FFFFu) - 65536;   /* a triangle of period 2^17: */
            y = 32768 - (y < 0 ? -y : y);               /* u while |u| <= 32768, reflected beyond (every fold more harmonics) */
        }
        l1 += mulq15(y - l1, g.k);                      /* the tone (DIST's) */
        b[i] = ifx_mix(x, ((l1 * g.go) >> (15 - g.sh)) + lo, w);
    }
    s->d.lo = lo, s->d.l1 = l1, s->dc = dc, s->w = w;
    m <<= g.sh;
    if (m > s->d.env)
        s->d.env = m;
    if (!w)
        s->d.env = 0;                                   /* (faded out: it restarts from rest) */
}
#endif

/* ---- PHSR, WIDE: first-order all-pass stages y = a (u - y1) + u1, a from Felucca's PH_A (Q14) at a triangle's place */
#if FELUCCA_INS_PHASER || FELUCCA_INS_WIDTH
#define IFX_AP 4u
static const int16_t IFX_PH_A[33] = {                   /* (Felucca perform.c PH_A: 150 Hz .. 4.8 kHz) */
    -16038, -15998, -15955, -15906, -15853, -15793, -15727, -15653, -15572, -15481, -15381, -15270, -15147, -15012,
    -14862, -14696, -14514, -14313, -14092, -13849, -13582, -13290, -12970, -12621, -12240, -11825, -11375, -10886,
    -10356, -9784, -9166, -8500, -7783,
};
AINL int32_t ifx_ap_coef(uint32_t q)                    /* q: 0 .. 65535 */
{
    uint32_t i = q >> 11;
    return IFX_PH_A[i] + (((IFX_PH_A[i + 1u] - IFX_PH_A[i]) * (int32_t)(q & 2047u)) >> 11);
}
AINL int32_t ifx_ap(int32_t *px, int32_t *py, int32_t c, int32_t u)   /* u, px, py within +-65535: c (u - py) < 2^31 */
{
    uint32_t st;
    for (st = 0; st < IFX_AP; st++) {
        int32_t y = clamp(px[st] + ((c * (u - py[st]) + 8192) >> 14), -65535, 65535);
        px[st] = u, py[st] = y;
        u = y;
    }
    return u;
}
#endif
#if FELUCCA_INS_PHASER
typedef struct {
    int32_t w, fb, dc;
    uint32_t ph;
    int32_t px[IFX_AP], py[IFX_AP];
    uint8_t a, q;
} ifx_ph_t;
static ifx_ph_t ifx_phsr[IFX_NI];
#define IFX_PH_RATE 44                                  /* ~0.5 Hz */
static HOT2 __attribute__((noinline)) void ifx_phaser(ifx_ph_t *s, int32_t a, int32_t *b, uint32_t n, uint32_t sh)
{
    uint32_t i, inc = IFX_INC(IFX_PH_RATE);
    int32_t c, fbk, to, w = s->w;
    if (!a && !w)
        return;
    to = a ? (a > 64 ? 64 : a) * 512 : 0;
    if (!w || ifx_quiet(&s->q, b, n)) {                 /* starting, or resting on silence: no stale stages */
        uint32_t ph = s->ph, q = s->q;
        ifx_zero(s, sizeof *s);
        s->ph = ph, s->q = (uint8_t)q;
        if (w) {
            s->w = to, s->a = (uint8_t)(a ? a : s->a);
            return;
        }
    }
    if (a)
        s->a = (uint8_t)a;
    fbk = s->a > 64 ? (s->a - 64) * 364 : 0;           /* 0 .. 0.7, Q15 */
    c = ifx_ap_coef(ifx_tri(s->ph + (n >> 1) * inc));  /* (a block's coefficient, at its middle) */
    for (i = 0; i < n; i++) {
        int32_t x = b[i], u = clamp(x >> sh, -32767, 32767) + ((s->fb * fbk + 16384) >> 15), y;
        w += clamp(to - w, -IFX_SLOPE, IFX_SLOPE);
        y = ifx_ap(s->px, s->py, c, u);
        s->fb = clamp(y, -32767, 32767);
        y <<= sh - 1u;                                  /* (the swept half) */
        s->dc += ((y << 4) - s->dc) >> 9;               /* a DC blocker, ~14 Hz (Q4) */
        b[i] = ifx_mix(x, (x >> 1) + y - (s->dc >> 4), w);
    }
    s->ph += n * inc;
    s->w = w;
}
#endif

/* ---- FLNG: Felucca's flanger line. A limited type since fx-pool (2026-10-15): a builder count of instances (fxi.c), each
 * with a line a side (a part: the first only); the sides share the sweep and the wet share (a stereo owner's two copies of
 * before, bit for bit: they always moved together) */
#if FELUCCA_INS_FLANGER
#define IFX_FL_LEN 256u                                 /* 5.8 ms (a power of 2: masked) */
typedef struct {
    int32_t w, dc[2];
    uint32_t ph;
    uint16_t wp;
    uint8_t a;
    int16_t line[2][IFX_FL_LEN];
} ifx_fl_t;
static ifx_fl_t ifx_flng[FELUCCA_INS_FLANGER] __attribute__((section(".pool"), aligned(4)));
#define IFX_FL_RATE 31                                  /* ~0.25 Hz */
/* one side c of the block: from the block's first sweep, share and write place (*ph, *w, *wp: its last, out) */
static HOT2 __attribute__((noinline)) void ifx_fl_side(ifx_fl_t *s, uint32_t c, int32_t *b, uint32_t n, uint32_t sh, int32_t to,
                                                       int32_t fbk, uint32_t *php, int32_t *wq, uint32_t *wpp)
{
    uint32_t i, inc = IFX_INC(IFX_FL_RATE), wp = *wpp, ph = *php;
    int32_t w = *wq, dc = s->dc[c];
    int16_t *ln = s->line[c];
    for (i = 0; i < n; i++, ph += inc) {
        int32_t x = b[i], r0, r1, v, fr;
        uint32_t tr = ifx_tri(ph), sw = (tr * tr) >> 16;   /* longer near the short end */
        uint32_t d = (11u << 16) + 209u * (((sw >> 1) * 65532u) >> 15);   /* the delay, Q16: 0.25 .. 5 ms (DEPTH full) */
        w += clamp(to - w, -IFX_SLOPE, IFX_SLOPE);
        r0 = (int32_t)(wp - (d >> 16)) & (int32_t)(IFX_FL_LEN - 1u);
        r1 = (r0 - 1) & (int32_t)(IFX_FL_LEN - 1u);
        fr = (int32_t)((d >> 1) & 0x7FFFu);
        v = ln[r0] + (((ln[r1] - ln[r0]) * fr) >> 15);
        ln[wp] = (int16_t)clamp((x >> sh) + softclip(mulq15(v, fbk)), -32767, 32767);
        wp = (wp + 1u) & (IFX_FL_LEN - 1u);
        v <<= sh - 1u;                                  /* (the delayed half) */
        dc += ((v << 4) - dc) >> 9;                     /* a DC blocker (Q4) */
        b[i] = ifx_mix(x, (x >> 1) + v - (dc >> 4), w);
    }
    s->dc[c] = dc;
    *php = ph, *wq = w, *wpp = wp;
}
/* l (a part: r 0), or l and r (a stereo owner) */
static HOT2 __attribute__((noinline)) void ifx_flanger(ifx_fl_t *s, int32_t a, int32_t *l, int32_t *r, uint32_t n, uint32_t sh)
{
    uint32_t ph, wp;
    int32_t fbk, to, w = s->w;
    if (!a && !w)
        return;
    if (!w)
        ifx_zero(s, sizeof *s);                        /* starting: no stale line */
    if (a)
        s->a = (uint8_t)a;
    to = a ? (a > 64 ? 64 : a) * 512 : 0;
    fbk = s->a > 64 ? (s->a - 64) * 442 : 0;           /* 0 .. 0.85, Q15 */
    ph = s->ph, wp = s->wp, w = s->w;
    ifx_fl_side(s, 0, l, n, sh, to, fbk, &ph, &w, &wp);
    if (r) {
        ph = s->ph, wp = s->wp, w = s->w;               /* (the same sweep and share on the right) */
        ifx_fl_side(s, 1, r, n, sh, to, fbk, &ph, &w, &wp);
    }
    s->ph = ph, s->wp = (uint16_t)wp, s->w = w;
}
#endif

/* ---- FRMT: three band-passes (tsvf_tick's, Q 4) at the vowel's formants over the dry at half (Melodee: three
 * peaking sections, +10 dB, the sum halved) */
#if FELUCCA_INS_FORMANT
typedef struct {
    int32_t w, z[3][2];
    uint8_t a, q;
} ifx_fm_t;
static ifx_fm_t ifx_frmt[IFX_NI];
/* the three formants' SVF coefficients (tsvf_t a1 a2 a3, Q13) at 33 places, 8 a vowel: A E I O U (Melodee's
 * {800,1150,2900} {400,1700,2600} {300,2200,3000} {500,800,2800} {350,650,2700} Hz), geometric between them; from
 * g = 4096 tan(pi f / FS), k = 1024 (Q 4) as dsp.c tsvf_coef_k (Python: den = 4096 + (g (g + k) >> 12),
 * a1 = (4096 << 13) // den, a2 = a1 g >> 12, a3 = a2 g >> 12): no divide here */
static const int16_t IFX_FMT[33][9] = {
    {8052, 458, 26, 7975, 654, 53, 7473, 1565, 327}, {8065, 421, 21, 7962, 686, 59, 7486, 1546, 319},
    {8077, 386, 18, 7947, 719, 65, 7499, 1526, 310}, {8089, 355, 15, 7932, 753, 71, 7511, 1509, 303},
    {8099, 326, 13, 7915, 790, 78, 7525, 1489, 294}, {8106, 298, 10, 7898, 827, 86, 7536, 1471, 287},
    {8114, 273, 9, 7880, 867, 95, 7548, 1453, 279},  {8122, 251, 7, 7860, 909, 105, 7560, 1435, 272},
    {8128, 230, 6, 7837, 952, 115, 7572, 1417, 265}, {8130, 222, 6, 7823, 981, 123, 7557, 1440, 274},
    {8134, 214, 5, 7806, 1013, 131, 7542, 1465, 284}, {8136, 206, 5, 7792, 1044, 139, 7526, 1488, 294},
    {8138, 200, 4, 7774, 1076, 148, 7509, 1512, 304}, {8140, 192, 4, 7756, 1109, 158, 7493, 1538, 315},
    {8142, 186, 4, 7736, 1144, 169, 7476, 1562, 326}, {8144, 178, 3, 7717, 1179, 180, 7458, 1587, 337},
    {8146, 173, 3, 7697, 1215, 191, 7440, 1612, 349}, {8142, 184, 4, 7772, 1079, 149, 7448, 1601, 344},
    {8138, 196, 4, 7836, 956, 116, 7456, 1589, 338}, {8134, 210, 5, 7889, 847, 90, 7466, 1576, 332},
    {8130, 224, 6, 7934, 751, 71, 7474, 1563, 327}, {8126, 238, 6, 7972, 663, 55, 7483, 1552, 322},
    {8120, 253, 7, 8002, 588, 43, 7491, 1539, 316}, {8116, 269, 8, 8029, 519, 33, 7499, 1528, 311},
    {8110, 287, 10, 8052, 458, 26, 7506, 1517, 306}, {8114, 275, 9, 8056, 446, 24, 7511, 1511, 303},
    {8118, 263, 8, 8060, 434, 23, 7514, 1504, 301}, {8122, 251, 7, 8064, 425, 22, 7520, 1498, 298},
    {8124, 241, 7, 8067, 413, 21, 7525, 1491, 295}, {8128, 230, 6, 8071, 403, 20, 7528, 1485, 292},
    {8132, 220, 5, 8075, 392, 19, 7531, 1480, 290}, {8134, 210, 5, 8079, 382, 18, 7536, 1473, 288},
    {8136, 202, 5, 8083, 372, 17, 7540, 1467, 285},
};
#define IFX_FMT_GK 9175                                 /* the band-passes' share: (3.24 - 1) / 2 x k (0.25), Q15 */
static HOT2 __attribute__((noinline)) void ifx_formant(ifx_fm_t *s, int32_t a, int32_t *b, uint32_t n, uint32_t sh)
{
    tsvf_t c[3];
    uint32_t i, k, p, j, f;
    int32_t w = s->w, to = a ? 32768 : 0;
    if (!a && !w)
        return;
    if (a)
        s->a = (uint8_t)a;
    if (!w || ifx_quiet(&s->q, b, n)) {
        ifx_zero(s->z, sizeof s->z);                   /* starting, or resting on silence: no stale filters */
        if (w) {
            s->w = to;
            return;
        }
    }
    p = (uint32_t)((s->a - 1) * 2081) >> 8;             /* the vowel's place, 0 .. 1024 (256 a vowel) */
    j = p >> 5, f = p & 31u;
    for (k = 0; k < 3u; k++) {                          /* (neighbours 1/8 vowel apart: their coefficients blend) */
        const int16_t *t0 = &IFX_FMT[j][3u * k], *t1 = &IFX_FMT[j < 32u ? j + 1u : 32u][3u * k];
        c[k].a1 = t0[0] + (((t1[0] - t0[0]) * (int32_t)f) >> 5);
        c[k].a2 = t0[1] + (((t1[1] - t0[1]) * (int32_t)f) >> 5);
        c[k].a3 = t0[2] + (((t1[2] - t0[2]) * (int32_t)f) >> 5);
    }
    for (i = 0; i < n; i++) {
        int32_t x = b[i], xs = clamp(x >> sh, -37500, 37500), sum = 0, bp;
        w += clamp(to - w, -IFX_SLOPE, IFX_SLOPE);
        for (k = 0; k < 3u; k++) {
            tsvf_tick(&c[k], xs, &s->z[k][0], &s->z[k][1], &bp);
            sum += bp;
        }
        b[i] = ifx_mix(x, (x >> 1) + ((((sum >> 2) * IFX_FMT_GK) >> 13) << sh), w);
    }
    s->w = w;
}
#endif

/* ---- WIDE: d = share x (the all-passed - the dry) of one side */
#if FELUCCA_INS_WIDTH
typedef struct {
    int32_t w;
    uint32_t ph;
    int32_t px[IFX_AP], py[IFX_AP];
    uint8_t q;
} ifx_wd_t;
static ifx_wd_t ifx_wide[IFX_BUS + 6u];                 /* (the parts, the drum bus's L, the master's L at IFX_BUS + 1, the
                                                         * USB input's at IFX_BUS + 2, the returns' at + 3 .. 5: ifx_wix) */
AINL uint32_t ifx_wix(uint32_t i)
{
    return i == IFX_MST ? IFX_BUS + 1u : i == IFX_UT ? IFX_BUS + 2u : i >= IFX_RT0 ? IFX_BUS + 3u + ((i - IFX_RT0) >> 1) % 3u : i;
}
static int32_t ifx_side[NPART][CTL];                    /* a part's side this block (before its level): ifx_side_mix */
static uint8_t ifx_sd[NPART];                           /* its side is set this block */
#define IFX_WD_RATE 13                                  /* ~0.1 Hz */
/* the difference into d (n samples of b; 0: none, nothing written) */
static HOT2 __attribute__((noinline)) int ifx_width(ifx_wd_t *s, int32_t a, const int32_t *b, int32_t *d, uint32_t n,
                                                     uint32_t sh)
{
    uint32_t i, inc = IFX_INC(IFX_WD_RATE);
    int32_t c, to = a * 258, w = s->w;
    if (!a && !w)
        return 0;
    if (!w || ifx_quiet(&s->q, b, n)) {
        ifx_zero(s->px, sizeof s->px), ifx_zero(s->py, sizeof s->py);
        if (w) {
            s->w = to;
            return 0;                                   /* (resting on silence: no difference) */
        }
    }
    c = ifx_ap_coef(16384u + (ifx_tri(s->ph + (n >> 1) * inc) >> 1));   /* (a quarter around the middle: ~1 kHz) */
    for (i = 0; i < n; i++) {
        int32_t x = b[i], y = ifx_ap(s->px, s->py, c, clamp(x >> sh, -32767, 32767)) << sh;
        w += clamp(to - w, -IFX_SLOPE, IFX_SLOPE);
        d[i] = w >= 32768 ? y - x : mulq16(y - x, (uint32_t)w << 1);
    }
    s->ph += n * inc;
    s->w = w;
    return 1;
}
#endif
#if FELUCCA_INS_WIDTH || FELUCCA_INS_JCHORUS
/* a part's side (WIDE's, JUNO CHORUS's: sd) into the mix after its level and gain (fx.c mix_part: the same lvl0 + dl i and ga .. gb ramps as its
 * dry; to the left, from the right: the pan law's centre, 4096 each) */
static HOT2 __attribute__((noinline)) void ifx_side_mix(const int32_t *sd, int32_t lvl0, int32_t dl, int32_t ga, int32_t gb,
                                                        int32_t *ml, int32_t *mr, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        int32_t x = ((clamp(sd[i], -884000, 884000) >> 2) * (lvl0 + dl * (int32_t)i)) >> 10;
        int32_t g = ga + (((gb - ga) * (int32_t)i) >> CTL_LOG2);
        if (g < 32767)
            x = (x >> 4) * (g >> 3) >> 8;
        x = ((x >> 4) * 4096) >> 8;                     /* (the centre's gain, as the dry's) */
        ml[i] += x, mr[i] -= x;
    }
}
#endif
#if FELUCCA_INS_JCHORUS
static int32_t ifx_jside[NPART][CTL];                   /* JUNO CHORUS's side of a part this block (juno_cho_ms) */
static uint8_t ifx_jsd[NPART];                          /* set this block */
#endif

#include "fxi_kern.c"                                   /* CHRS ECHO: the limited types' kernels, their instances (fxi.c) */
#include "gater.c"                                      /* GATER: the tempo-synced gate (one instance a track) */
#if FELUCCA_INS_GATER && FELUCCA_USB_AUDIO
#define IFX_GATE_UT(i) ((i) == IFX_UT ? ifx_gate_ut.c : 0)   /* (the USB input's closure) */
#else
#define IFX_GATE_UT(i) 0
#endif
#include "eq3.c"                                        /* EQ3: the three-band DJ EQ (a state an owner) */
#if INS_LIM
#define IFX_EV(ty, k) ifx_lim_ev(ty, k)                 /* (fading out to give a lent line back: as off) */
#else
#define IFX_EV(ty, k) 0
#endif

/* the state of type ty on instance i: its wet share now (0: at rest; the CPU guard). A limited type (CHRS ECHO): i is
 * the track, its instance's (none: 0) */
AINL int32_t ifx_w(uint32_t ty, uint32_t i)
{
#if INS_LIM
    if (FXT_IS_LIM(ty))
        return ifx_lim_w(ty, i);
#endif
    switch (ty) {
#if FELUCCA_INS_FOLD
    case FXT_FOLD: return ifx_fold[i].w;
#endif
#if FELUCCA_INS_FUZZ
    case FXT_FUZZ: return ifx_fuzz[i].w;
#endif
#if FELUCCA_INS_PHASER
    case FXT_PHSR: return ifx_phsr[i].w;
#endif
#if FELUCCA_INS_FORMANT
    case FXT_FRMT: return ifx_frmt[i].w;
#endif
#if FELUCCA_INS_WIDTH
    case FXT_WIDE: return ifx_wix(i) < IFX_BUS + 6u ? ifx_wide[ifx_wix(i)].w : 0;
#endif
#if FELUCCA_INS_GATER
    case FXT_GATE: return i < NTRK ? ifx_gate[i].c : IFX_GATE_UT(i);   /* (its closure: 0 open, at rest) */
#endif
#if FELUCCA_INS_EQ3
    case FXT_EQ3: return eq3_st[ifx_owner(i)].w;       /* (its share: 0 bypassed) */
#endif
    default: return 0;
    }
}
/* type ty can run on track k: a limited type (CHRS ECHO) only with an instance (fxi.c), every other always (the guard) */
AINL int ifx_has(uint32_t ty, uint32_t k)
{
#if INS_LIM
    if (FXT_IS_LIM(ty))
        return ifx_lim_at(ty, k) != FXI_NONE;
#endif
    (void)ty, (void)k;
    return 1;
}
/* the types (a bit each) that may still need a call at amount 0 on track k: not fully at rest. A call at 0 from rest
 * (its wet share 0 before and after) does what every type does last at rest (FOLD FUZZ: no peak; WIDE: no side; CHRS
 * ECHO: not running) and changes nothing more after it, so a type in no slot whose bit is clear is not called at all
 * (fx.c ins_part, dbus_run; ifx_quiet_part): a type built and in no slot costs nothing a block. Set by any call with an
 * amount or a wet share, cleared by the call at 0 from rest and by ifx_quiet_part's rest */
#define IFX_AW_RT0 (NTRK + 2u)                          /* ifx_awake's entries for the returns A B C */
static uint32_t ifx_awake[NTRK + 5];                    /* (the tracks', the master's: IFX_AW_MST, the USB input's, the returns') */
AINL uint32_t ifx_aw(uint32_t k)                        /* the audio's index k -> its ifx_awake entry */
{
    return k == IFX_MST ? IFX_AW_MST : k == IFX_UT ? IFX_AW_UT : k >= IFX_RT0 ? IFX_AW_RT0 + ((k - IFX_RT0) >> 1) % 3u : k % NTRK;
}
static __attribute__((noinline)) int32_t ifx_wn(uint32_t ty, uint32_t k) { return ifx_w(ty, k); }   /* (flash: the
                                                        * wrappers and the quiet loop call it through FAR, a block) */
static __attribute__((noinline)) void ifx_mark(uint32_t ty, uint32_t k, int32_t a, int32_t w0)   /* (flash, FAR) */
{
    uint32_t j = ifx_aw(k);
    if (a || w0 || ifx_w(ty, k))
        ifx_awake[j] |= FXT_BIT(ty);
    else
        ifx_awake[j] &= ~FXT_BIT(ty);
}

/* type ty (a further one) at amount a on instance i's n samples of b; sh: b's headroom (a part's 3, the bus's 2:
 * fx.c tflt_run's) */
static HOT2 __attribute__((noinline)) void ifx_run(uint32_t ty, uint32_t i, int32_t a, int32_t *b, uint32_t n, uint32_t sh)
{
    switch (ty) {
#if FELUCCA_INS_FOLD
    case FXT_FOLD: ifx_drive(&ifx_fold[i], 0, a, b, n); break;
#endif
#if FELUCCA_INS_FUZZ
    case FXT_FUZZ: ifx_drive(&ifx_fuzz[i], 1, a, b, n); break;
#endif
#if FELUCCA_INS_PHASER
    case FXT_PHSR: ifx_phaser(&ifx_phsr[i], a, b, n, sh + 1u); break;
#endif
#if FELUCCA_INS_FORMANT
    case FXT_FRMT: ifx_formant(&ifx_frmt[i], a, b, n, sh + 1u); break;
#endif
#if FELUCCA_INS_GATER
    case FXT_GATE: {
        igt_par_t p;
        igt_par_trk(&trk[i % NTRK], &p);
        ifx_gater(&ifx_gate[i % NTRK], &p, a, b, 0, n);
        break;
    }
#endif
#if FELUCCA_INS_EQ3
    case FXT_EQ3: (void)eq3_run(&eq3_st[ifx_owner(i)], eq3_v[ifx_owner(i)], a, b, 0, n); break;   /* (a part: mono) */
#endif
    default: break;
    }
    (void)i, (void)a, (void)b, (void)n, (void)sh;
}
/* part k's further insert ty at amount a (fx.c ins_one; WIDE: its block + d / 2, its side d / 2) */
AINL void ifx_part_run(uint32_t ty, uint32_t k, int32_t a, int32_t *b, uint32_t n)
{
#if INS_LIM
    if (FXT_IS_LIM(ty)) {                               /* (CHRS ECHO: the track's instance, none: bypassed) */
        ifx_lim_run(ty, k % NPART, a, b, 0, n, 3);
        return;
    }
#endif
#if FELUCCA_INS_WIDTH
    if (ty == FXT_WIDE) {
        int32_t *sd = ifx_side[k % NPART];
        uint32_t i;
        ifx_sd[k % NPART] = (uint8_t)ifx_width(&ifx_wide[k % NPART], a, b, sd, n, 4);
        for (i = 0; ifx_sd[k % NPART] && i < n; i++)
            sd[i] >>= 1, b[i] += sd[i];
        return;
    }
#endif
    ifx_run(ty, k % NPART, a, b, n, 3);
}
static HOT2 __attribute__((noinline)) void ifx_part(uint32_t ty, uint32_t k, int32_t a, int32_t *b, uint32_t n)
{
    int32_t w0 = FAR(ifx_wn)(ty, k % NPART);
    ifx_part_run(ty, k, a, b, n);
    FAR(ifx_mark)(ty, k % NPART, a, w0);
}
/* a part not heard this block (silent, MUTE / SOLO: fx.c mix_part): its further inserts not on any more (in no slot, at
 * 0, its FX bypassed) go to rest at once, not frozen mid-fade, so its next note starts dry, sample for sample (a fade
 * there would be the old effect's last 2.9 ms on a new note). Those still on keep their state */
static HOT2 __attribute__((noinline)) void ifx_quiet_part(const track_t *t)
{
    uint32_t k = (uint32_t)(t - trk) % NPART, ty, m = ifx_awake[k] & FXT_BUILT;
    int32_t on = fx_on(t);
    for (ty = 0; m; ty++, m >>= 1) {                    /* (only the types not at rest: ifx_awake) */
        if (!(m & 1u) || ty < FXT_NEW0 || !FAR(ifx_wn)(ty, k) ||
            (on && INS_ON(k, ty) && t->p[FXT_AMT[ty]] && !IFX_EV(ty, k)))   /* (evicted: fxi.c, as off) */
            continue;
        ifx_awake[k] &= ~FXT_BIT(ty);                   /* (to rest here, below) */
#if INS_LIM
        if (FXT_IS_LIM(ty)) {
            ifx_lim_rest(ty, k);                        /* (its wet share 0: it starts from a clean line, fxi_kern.c) */
            continue;
        }
#endif
        switch (ty) {
#if FELUCCA_INS_FOLD
        case FXT_FOLD: ifx_zero(&ifx_fold[k], sizeof ifx_fold[k]); break;
#endif
#if FELUCCA_INS_FUZZ
        case FXT_FUZZ: ifx_zero(&ifx_fuzz[k], sizeof ifx_fuzz[k]); break;
#endif
#if FELUCCA_INS_PHASER
        case FXT_PHSR: ifx_zero(&ifx_phsr[k], sizeof ifx_phsr[k]); break;
#endif
#if FELUCCA_INS_FORMANT
        case FXT_FRMT: ifx_zero(&ifx_frmt[k], sizeof ifx_frmt[k]); break;
#endif
#if FELUCCA_INS_WIDTH
        case FXT_WIDE: ifx_zero(&ifx_wide[k], sizeof ifx_wide[k]), ifx_sd[k] = 0; break;
#endif
#if FELUCCA_INS_GATER
        case FXT_GATE: ifx_zero(&ifx_gate[k], sizeof ifx_gate[k]); break;   /* (open: its phase from the clock) */
#endif
#if FELUCCA_INS_EQ3
        case FXT_EQ3: eq3_st[k].w = 0; break;           /* (bypassed: re-engaging seeds it, eq3.c) */
#endif
        default: break;
        }
    }
}
/* the drum bus's further insert ty at amount a: on its L and R (WIDE: its L + d); the master's (i0 IFX_MST: no limited
 * type but JUNO CHORUS, no GATER there: fx_slots.c MST_OK) */
AINL void ifx_st_run(uint32_t ty, uint32_t i0, int32_t a, int32_t *l, int32_t *r, uint32_t n)
{
#if INS_LIM
    if (FXT_IS_LIM(ty)) {                               /* (the owner's instance, fxi.c; none: bypassed) */
        ifx_lim_run(ty, i0, a, l, r, n, 2);
        return;
    }
#endif
#if FELUCCA_INS_GATER
    if (ty == FXT_GATE) {                               /* (one gain on L and R: the USB input's instance, its own values; no
                                                        * other stereo owner holds it: fx_slots.c MST_OK RT_OK) */
#if FELUCCA_USB_AUDIO
        if (i0 == IFX_UT) {
            igt_par_t p;
            igt_par_ut(&p);
            ifx_gater(&ifx_gate_ut, &p, a, l, r, n);
        }
#endif
        return;
    }
#endif
#if FELUCCA_INS_EQ3
    if (ty == FXT_EQ3) {                                /* (a stereo owner: its state's two sides) */
        (void)eq3_run(&eq3_st[ifx_owner(i0)], eq3_v[ifx_owner(i0)], a, l, r, n);
        return;
    }
#endif
#if FELUCCA_INS_WIDTH
    if (ty == FXT_WIDE) {
        int32_t d[CTL];
        uint32_t i;
        if (ifx_width(&ifx_wide[ifx_wix(i0)], a, l, d, n, 3))
            for (i = 0; i < n; i++)
                l[i] += d[i];
        return;
    }
#endif
    ifx_run(ty, i0, a, l, n, 2);
    ifx_run(ty, i0 + 1u, a, r, n, 2);
}
AINL void ifx_bus_run(uint32_t ty, int32_t a, int32_t *l, int32_t *r, uint32_t n)
{
#if INS_LIM
    if (FXT_IS_LIM(ty)) {                               /* (CHRS ECHO: one instance on the bus's mid, out to L and R) */
        ifx_lim_run(ty, TRK_DRUM, a, l, r, n, 2);
        return;
    }
#endif
#if FELUCCA_INS_GATER
    if (ty == FXT_GATE) {                               /* (one gain on L and R: the bus's one instance) */
        igt_par_t p;
        igt_par_trk(TDRUM, &p);
        ifx_gater(&ifx_gate[IFX_BUS], &p, a, l, r, n);
        return;
    }
#endif
    ifx_st_run(ty, IFX_BUS, a, l, r, n);
}
_Static_assert(IFX_BUS == TRK_DRUM, "ifx_awake: the drum bus's bit is the drum track's");
static HOT2 __attribute__((noinline)) void ifx_bus(uint32_t ty, int32_t a, int32_t *l, int32_t *r, uint32_t n)
{
    int32_t w0 = FAR(ifx_wn)(ty, IFX_BUS);
    ifx_bus_run(ty, a, l, r, n);
    FAR(ifx_mark)(ty, IFX_BUS, a, w0);
}
/* a stereo owner's further insert ty at amount a, its L and R: the master (fx.c mst_ins, i0 IFX_MST), the USB input (ut_ins,
 * IFX_UT), a return (IFX_RT(j)); a limited type: the owner's instance (fxi.c) */
static HOT2 __attribute__((noinline)) void ifx_st(uint32_t ty, uint32_t i0, int32_t a, int32_t *l, int32_t *r, uint32_t n)
{
    int32_t w0 = FAR(ifx_wn)(ty, i0);
    ifx_st_run(ty, i0, a, l, r, n);
    FAR(ifx_mark)(ty, i0, a, w0);
}
#define ifx_ut(ty, a, l, r, n) ifx_st(ty, IFX_UT, a, l, r, n)
#define ifx_mst(ty, a, l, r, n) ifx_st(ty, IFX_MST, a, l, r, n)
#endif
