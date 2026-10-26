/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 the Optimist contributors */
/* STRETCH (engine 27, FELUCCA_ENG_STRETCH, off by default; needs SAMPLE): PaulStretch-style extreme time stretching of
 * the SAMPLE material (the built-in sets and the user slots USR1..3). Our code, after Paul Nasca's PaulStretch algorithm
 * and the design of AEther (an fm1-base app, MPL-2.0) as a reference; none of either's code. The source is read through
 * a window of 4096 samples (a sine window), a fixed-point FFT, every bin turned by a random phase (SMEAR), the spectrum
 * moved to each voice's pitch and summed, one inverse FFT, the sine window again and a 50 % overlap-add: a frame every
 * 2048 output samples (46 ms). The scan through the source moves 2048 / STRCH source samples a frame (STRCH 1x .. ~8000x,
 * 127: frozen). The transients smear into a smooth, endless sound.
 *   EDIT 1 (SRC)    SRC   the sample set or user slot (as SAMPLE's SET)
 *                   POS   where the scan starts (0..100 % of the zone)
 *                   STRCH how many times slower than the source (127: frozen)
 *                   SMEAR 0 the phases kept (a plain, phasey stretch) .. 127 random (PaulStretch)
 *   EDIT 2 (SPEC)   TONE  a low-pass on the spectrum (the track's FLT moves it)
 *                   SUB   an octave below, mixed in
 *                   SHIM  an octave above, mixed in
 *                   BLUR  each bin's frequency jittered up to ~8 bins: tones into noise bands
 * One instance: the first part that plays STRETCH owns it (the pool's 45 KB, once); another part playing STRETCH at the
 * same time stays silent until the owner stops. Every voice of the owner transposes the zone of its newest note (one
 * analysis a frame, one inverse FFT for all the voices). A voice's level and pitch are taken once a frame. The work of a
 * frame is spread over the 64 blocks of the frame before it (the block hook), so no block carries a whole FFT; while no
 * voice sounds, nothing runs. Latency: one to two frames (46..93 ms). Voice state: s[0] the voice's zone; the phases are
 * unused. */
#if !FELUCCA_ENG_SAMPLE
#error "the STRETCH engine reads SAMPLE's material: it needs FELUCCA_ENG_SAMPLE"
#endif
#define ST_N 4096u                       /* the window, source samples */
#define ST_M 2048u                       /* complex points of the FFT (a real FFT of ST_N, packed) */
#define ST_LOG 11u                       /* log2 ST_M */
#define ST_HOP 2048u                     /* output samples a frame */
#define ST_BLK (ST_HOP / CTL)            /* blocks a frame */
#define ST_IN_SH 8                       /* the source's headroom bits in the FFT */
#define ST_OUT_SH 8                      /* the inverse FFT's output back to Q15 (tests/stretch_test.c: unity round trip) */
_Static_assert(ST_HOP % CTL == 0 && ST_BLK > 8u, "a frame is whole blocks");

static int32_t pst_a[2u * ST_M] IN_POOL;          /* the analysis: re, im (16 KB) */
static int32_t pst_b[2u * ST_M + 2u] IN_POOL;     /* the voices' spectrum, bins 0..M (16 KB) */
static int16_t pst_buf[3][ST_HOP] IN_POOL;        /* playing, the next, the tail (12 KB) */

enum { SP_IDLE, SP_DEC, SP_BREV_A, SP_FFT_A, SP_SPLIT, SP_MAP, SP_ISPLIT, SP_BREV_B, SP_FFT_B, SP_OLA, SP_DONE };
typedef struct {
    uint32_t ratio;                      /* output bins a source bin, Q16 (0: not sounding) */
    int32_t amp;                         /* Q15 */
} pst_voice_t;
typedef struct {
    const smp_zone_t *z;
    uint32_t scan;                       /* the decoder's next sample in the zone */
    int32_t pred, idx;                   /* its ADPCM state */
    uint32_t skip;                       /* samples to decode before the window (a new start: POS) */
    uint32_t advq;                       /* the scan's advance a frame, its Q16 fraction carried */
    uint32_t dp;                         /* SP_DEC: the window's next sample; its own decoder: */
    int32_t dpred, didx;
    uint32_t dpos;
    uint32_t cur, stage, pass;           /* the phase's cursor */
    uint32_t work, total, blk, pp;       /* units done of the frame's total, the block in the frame, the play index */
    int32_t rng, nyq;                    /* the random numbers; the Nyquist bin of the analysis (real) */
    int32_t cut;                         /* the newest voice's FLT modulation (vmod_t.cutoff), for TONE */
    uint32_t beat, seen[NPART], miss[NPART];   /* the owner's pulse; the others' view of it */
    uint32_t age;                        /* the newest voice's (its cut) */
    uint32_t last;                       /* SP_MAP resumed: TONE's slope, the bin it reached */
    int32_t gt;
    int16_t *play, *next, *tail;
    uint8_t owner;                       /* part + 1, 0 = none */
    uint8_t phase, ready, zl, src;
    pst_voice_t sv[NVOICE];
} pst_state_t;
static pst_state_t pstx;

/* one IMA ADPCM sample of the zone at pos (as GRAIN's gr_dec) */
AINL int32_t pst_dec(const smp_zone_t *z, uint32_t pos, int32_t *pred, int32_t *idx)
{
    uint32_t b = SMP_DATA[z->off + (pos >> 1)], code = (pos & 1u) ? (b >> 4) : (b & 15u);
    return ima_nibble(code, IMA_STEP[*idx], pred, idx);
}
/* the decoder past the zone's end: to its loop start (with the state stored there) or its start */
AINL void pst_wrap(const smp_zone_t *z, uint32_t *pos, int32_t *pred, int32_t *idx)
{
    if (*pos < z->n)
        return;
    if (z->looped && z->ls < z->n) {
        *pos = z->ls, *pred = z->pred, *idx = z->idx;
    } else {
        *pos = 0, *pred = 0, *idx = 0;
    }
}
AINL uint32_t pst_nz(uint32_t src) { return src < SMP_NSETS ? SMP_SETS[src].nz : usr_nz[(src - SMP_NSETS) % SMP_USER_SLOTS]; }
AINL const smp_zone_t *pst_zone(uint32_t src, uint32_t zl)
{
    return src < SMP_NSETS ? &SMP_ZONES[SMP_SETS[src].z0 + zl] : smp_usr_zone((src - SMP_NSETS) % SMP_USER_SLOTS, zl & 15u);
}
/* the zone of a note (as SAMPLE: the last zone that holds it; a built-in set falls back to its first), -1 none */
static int32_t pst_find(uint32_t src, uint32_t note)
{
    uint32_t i, nz = pst_nz(src);
    int32_t zl = src < SMP_NSETS && nz ? 0 : -1;
    for (i = 0; i < nz && i < 32u; i++) {
        const smp_zone_t *z = pst_zone(src, i);
        if (note >= z->lo && note <= z->hi)
            zl = (int32_t)i;
    }
    return zl;
}

/* the sine window sin(pi (i + 0.5) / N), Q15 */
AINL int32_t pst_win(uint32_t i) { return sine_i(((i << 1) + 1u) << 18); }
AINL uint32_t pst_rev(uint32_t i)                     /* the 11-bit reversal */
{
    i = ((i & 0x555u) << 1) | ((i >> 1) & 0x555u);
    i = ((i & 0x333u) << 2) | ((i >> 2) & 0x333u);
    i = ((i & 0x0F0Fu) << 4) | ((i >> 4) & 0x0F0Fu);
    i = ((i & 0x00FFu) << 8) | ((i >> 8) & 0x00FFu);
    return (i >> 5) & 0x7FFu;
}
AINL int32_t pst_mul14(int32_t a, int32_t w) { return (int32_t)(((int64_t)a * w) >> 14); }

/* ---- the phases: each does up to `budget` units and returns the units done (the cursor kept in pstx) ---- */
#define U_DEC 1u                         /* a decoded sample */
#define U_BREV 1u                        /* two indices */
#define U_BFLY 2u                        /* a butterfly */
#define U_SPLIT 6u                       /* a pair of bins (and their random turns) */
#define U_MAP 1u                         /* a bin of a voice pass */
#define U_ISPLIT 4u                      /* a pair of bins */
#define U_OLA 1u                         /* an output sample (two window points) */

static uint32_t sp_dec(uint32_t budget, uint32_t adv)
{
    const smp_zone_t *z = pstx.z;
    uint32_t done = 0;
    while (pstx.skip && done < budget) {                /* a new start: the scan decodes up to POS first */
        pst_dec(z, pstx.scan++, &pstx.pred, &pstx.idx);
        pst_wrap(z, &pstx.scan, &pstx.pred, &pstx.idx);
        pstx.skip--, done++;
    }
    if (pstx.skip)
        return done;
    if (pstx.dp == 0) {
        pstx.dpos = pstx.scan, pstx.dpred = pstx.pred, pstx.didx = pstx.idx;
    }
    while (pstx.dp < ST_N && done < budget) {
        uint32_t i = pstx.dp;
        int32_t s;
        if (i == adv) {                              /* the next frame's scan starts here */
            pstx.scan = pstx.dpos, pstx.pred = pstx.dpred, pstx.idx = pstx.didx;
        }
        s = pst_dec(z, pstx.dpos++, &pstx.dpred, &pstx.didx);
        pst_wrap(z, &pstx.dpos, &pstx.dpred, &pstx.didx);
        pst_a[i] = (int32_t)(((int64_t)(s << ST_IN_SH) * pst_win(i)) >> 15);
        pstx.dp++, done++;
    }
    return done;
}

static uint32_t sp_brev(int32_t *x, uint32_t budget)
{
    uint32_t done = 0;
    while (pstx.cur < ST_M && done < budget) {
        uint32_t i = pstx.cur, j = pst_rev(i);
        if (j > i) {
            int32_t r = x[2u * i], m = x[2u * i + 1u];
            x[2u * i] = x[2u * j], x[2u * i + 1u] = x[2u * j + 1u];
            x[2u * j] = r, x[2u * j + 1u] = m;
        }
        pstx.cur++;
        if (!(pstx.cur & 1u))
            done += U_BREV;
    }
    return done;
}

/* radix-2 stages over x; the forward one halves each stage (pst_a: the source's DFT / ST_N at the end, with the split's
 * half), the inverse one does not (its sum is the output) */
static uint32_t sp_fft(int32_t *x, int inv, uint32_t budget)
{
    uint32_t done = 0;
    while (pstx.stage < ST_LOG && done < budget) {
        uint32_t s = pstx.stage, half = 1u << s, j = pstx.cur, k = j & (half - 1u);
        uint32_t top = ((j >> s) << (s + 1u)) + k, bot = top + half, ph = k << (31u - s);
        int32_t wr = sine_i(ph + 0x40000000u) >> 1, wi = sine_i(ph) >> 1;   /* Q14 */
        int32_t br = x[2u * bot], bi = x[2u * bot + 1u], ar = x[2u * top], ai = x[2u * top + 1u], tr, ti;
        if (!inv)
            wi = -wi;
        tr = (int32_t)((((int64_t)br * wr) - ((int64_t)bi * wi)) >> 14);
        ti = (int32_t)((((int64_t)br * wi) + ((int64_t)bi * wr)) >> 14);
        if (inv) {
            x[2u * top] = ar + tr, x[2u * top + 1u] = ai + ti;
            x[2u * bot] = ar - tr, x[2u * bot + 1u] = ai - ti;
        } else {
            x[2u * top] = (ar + tr) >> 1, x[2u * top + 1u] = (ai + ti) >> 1;
            x[2u * bot] = (ar - tr) >> 1, x[2u * bot + 1u] = (ai - ti) >> 1;
        }
        done += U_BFLY;
        if (++pstx.cur == ST_M / 2u)
            pstx.cur = 0, pstx.stage++;
    }
    return done;
}

/* the real spectrum X[k] of the window from the packed one (k and M - k together), each bin turned by a random phase
 * (SMEAR: 0 none .. 127 any); X[0] stays in pst_a[0], X[M] in pstx.nyq */
AINL void pst_turn(int32_t *r, int32_t *i, uint32_t smear)
{
    uint32_t ph = (noise32(&pstx.rng) >> 7) * smear;   /* (25 bits x 127: up to a turn) */
    int32_t c = sine_i(ph + 0x40000000u) >> 1, s = sine_i(ph) >> 1, a = *r, b = *i;
    *r = pst_mul14(a, c) - pst_mul14(b, s);
    *i = pst_mul14(a, s) + pst_mul14(b, c);
}
AINL void pst_split1(int32_t ar, int32_t ai, int32_t br, int32_t bi, uint32_t k, int32_t *xr, int32_t *xi)
{
    /* A = Z[k], B = conj(Z[M - k]): X[k] = (A + B) / 2 + W^k (A - B) / 2i, W = e^(-2 pi i / N) */
    int32_t er = (ar + br) >> 1, ei = (ai - bi) >> 1, dr = (ar - br) >> 1, di = (ai + bi) >> 1;
    uint32_t ph = k << 20;
    int32_t wr = sine_i(ph + 0x40000000u) >> 1, wi = -(sine_i(ph) >> 1);
    int32_t orr = di, oi = -dr;                       /* (A - B) / 2i */
    *xr = er + pst_mul14(orr, wr) - pst_mul14(oi, wi);
    *xi = ei + pst_mul14(orr, wi) + pst_mul14(oi, wr);
}
static uint32_t sp_split(uint32_t budget, uint32_t smear)
{
    uint32_t done = 0;
    if (pstx.cur == 0) {
        int32_t r = pst_a[0], i = pst_a[1];
        pst_a[0] = r + i, pst_a[1] = 0;                /* X[0] = Re + Im, X[M] = Re - Im (the forward halves: as the rest) */
        pstx.nyq = r - i;
        pstx.cur = 1;
    }
    while (pstx.cur <= ST_M / 2u && done < budget) {
        uint32_t k = pstx.cur, mk = ST_M - k;
        int32_t ar = pst_a[2u * k], ai = pst_a[2u * k + 1u], br = pst_a[2u * mk], bi = pst_a[2u * mk + 1u], xr, xi, yr, yi;
        pst_split1(ar, ai, br, bi, k, &xr, &xi);
        pst_split1(br, bi, ar, ai, mk, &yr, &yi);
        pst_turn(&xr, &xi, smear);
        pst_a[2u * k] = xr, pst_a[2u * k + 1u] = xi;
        if (mk != k) {
            pst_turn(&yr, &yi, smear);
            pst_a[2u * mk] = yr, pst_a[2u * mk + 1u] = yi;
        }
        pstx.cur++, done += U_SPLIT;
    }
    return done;
}

/* the voices: each pass adds the spectrum, moved by a voice's ratio (SUB: half of it, SHIM: twice), at its level and
 * TONE's slope above the cutoff bin, into pst_b; BLUR jitters the bin it lands on */
static uint32_t pst_passes(void) { return NVOICE * 3u; }
static uint32_t sp_map(uint32_t budget, const int16_t *p, int32_t mcut)
{
    uint32_t done = 0;
    int32_t tone = clamp((p[P_E4] << 8) + mcut, 0, 127 << 8), ecut = (tone * 11) >> 15;   /* 2^0 .. 2^11 bins */
    uint32_t kc = 1u << ecut, blur = (uint32_t)clamp(p[P_E7], 0, 127);
    int32_t dec = clamp(32767 - ((32767 * 8) >> ecut), 0, 32767);   /* the slope above kc: steeper for a low cutoff */
    if (pstx.cur == 0 && pstx.pass == 0) {
        uint32_t i;
        for (i = 0; i < 2u * ST_M + 2u; i++)
            pst_b[i] = 0;
        done += ST_M / 4u;
    }
    while (pstx.pass < pst_passes() && done < budget) {
        const pst_voice_t *w = &pstx.sv[pstx.pass / 3u];
        uint32_t kind = pstx.pass % 3u, ratio = kind == 0u ? w->ratio : kind == 1u ? w->ratio >> 1 : w->ratio << 1;
        int32_t g = kind == 0u ? w->amp : mulq15(w->amp, (kind == 1u ? p[P_E5] : p[P_E6]) * 258);
        uint32_t k, last = pstx.cur ? pstx.last : 0u, kk = 0;
        int32_t gt = pstx.cur ? pstx.gt : 32767;
        if (!w->ratio || !g || ratio > (64u << 16)) {
            pstx.pass++, pstx.cur = 0;
            continue;
        }
        for (k = pstx.cur ? pstx.cur : 1u; k < ST_M && done < budget; k++, done += U_MAP) {
            uint32_t q = k * (ratio >> 4), f;          /* Q12 (k < 2^11, ratio < 2^22: no overflow) */
            int32_t xr = pst_a[2u * k], xi = pst_a[2u * k + 1u], gg;
            if (blur)
                q += ((noise32(&pstx.rng) >> 24) * blur) >> 2;   /* up to ~8 bins up (Q12: 255 x 127 / 4 = 8 bins) */
            kk = q >> 12, f = (q & 4095u) << 3;         /* f: Q15 */
            if (kk + 1u > ST_M)
                break;
            while (last < kk) {                       /* TONE: down by dec a bin above the cutoff */
                if (last >= kc)
                    gt = mulq15(gt, dec);
                last++;
            }
            gg = mulq15(g, gt);
            if (!gg)
                break;
            xr = mulq15(xr, gg), xi = mulq15(xi, gg);
            pst_b[2u * kk] += mulq15(xr, 32767 - (int32_t)f), pst_b[2u * kk + 1u] += mulq15(xi, 32767 - (int32_t)f);
            pst_b[2u * kk + 2u] += mulq15(xr, (int32_t)f), pst_b[2u * kk + 3u] += mulq15(xi, (int32_t)f);
        }
        if (k >= ST_M || kk + 1u > ST_M || done < budget)   /* (the pass ran to its end: the next one) */
            pstx.pass++, pstx.cur = 0;
        else
            pstx.cur = k, pstx.last = last, pstx.gt = gt;
    }
    return done;
}

/* the packed spectrum for the inverse FFT, in place in pst_b: Z[k] = E[k] + i O[k], E = (Y[k] + conj Y[M - k]) / 2,
 * O = (Y[k] - conj Y[M - k]) / 2 x W^-k */
AINL void pst_isplit1(int32_t ar, int32_t ai, int32_t br, int32_t bi, uint32_t k, int32_t *zr, int32_t *zi)
{
    int32_t er = (ar + br) >> 1, ei = (ai - bi) >> 1, dr = (ar - br) >> 1, di = (ai + bi) >> 1;
    uint32_t ph = k << 20;
    int32_t wr = sine_i(ph + 0x40000000u) >> 1, wi = sine_i(ph) >> 1;
    int32_t orr = pst_mul14(dr, wr) - pst_mul14(di, wi), oi = pst_mul14(dr, wi) + pst_mul14(di, wr);
    *zr = er - oi;                                    /* E + i O */
    *zi = ei + orr;
}
static uint32_t sp_isplit(uint32_t budget)
{
    uint32_t done = 0;
    if (pstx.cur == 0) {
        int32_t y0 = pst_b[0], ym = pst_b[2u * ST_M];
        pst_b[0] = (y0 + ym) >> 1, pst_b[1] = (y0 - ym) >> 1;
        pstx.cur = 1;
    }
    while (pstx.cur <= ST_M / 2u && done < budget) {
        uint32_t k = pstx.cur, mk = ST_M - k;
        int32_t ar = pst_b[2u * k], ai = pst_b[2u * k + 1u], br = pst_b[2u * mk], bi = pst_b[2u * mk + 1u], zr, zi, yr, yi;
        pst_isplit1(ar, ai, br, bi, k, &zr, &zi);
        pst_isplit1(br, bi, ar, ai, mk, &yr, &yi);
        pst_b[2u * k] = zr, pst_b[2u * k + 1u] = zi;
        pst_b[2u * mk] = yr, pst_b[2u * mk + 1u] = yi;
        pstx.cur++, done += U_ISPLIT;
    }
    return done;
}

AINL int16_t pst_sat(int32_t x) { return (int16_t)clamp(x, -32767, 32767); }
static uint32_t sp_ola(uint32_t budget)
{
    uint32_t done = 0;
    while (pstx.cur < ST_HOP && done < budget) {
        uint32_t i = pstx.cur, j = i + ST_HOP;         /* the frame's sample i (first half) and j (second half) */
        int32_t lo = pst_b[i] >> ST_OUT_SH, hi = pst_b[j] >> ST_OUT_SH;   /* (x[2n] = Re z[n], x[2n + 1] = Im z[n]: x[i] at i) */
        lo = mulq15(clamp(lo, -65535, 65535), pst_win(i));
        hi = mulq15(clamp(hi, -65535, 65535), pst_win(j));
        pstx.next[i] = pst_sat(pstx.tail[i] + lo);
        pstx.tail[i] = pst_sat(hi);
        pstx.cur++, done += U_OLA;
    }
    return done;
}

/* the frame's units, for the pacing (the map's passes counted for the voices sounding now) */
static uint32_t pst_units(const int16_t *p)
{
    uint32_t v, passes = 0;
    for (v = 0; v < NVOICE; v++)
        if (pstx.sv[v].ratio)
            passes += 1u + (p[P_E5] ? 1u : 0u) + (p[P_E6] ? 1u : 0u);
    return ST_N * U_DEC + ST_M * U_BREV + 2u * ST_LOG * (ST_M / 2u) * U_BFLY + (ST_M / 2u) * (U_SPLIT + U_ISPLIT) +
           ST_M / 4u + passes * ST_M * U_MAP + ST_HOP * U_OLA;
}

/* the scan's advance a frame: 2048 / STRCH source samples, STRCH 2^(v x 13 / 126) (127: none, frozen); Q16 */
static uint32_t pst_adv_q16(int32_t v)
{
    if (v >= 127)
        return 0;
    return (ST_HOP << 16) / pow2_q16(clamp(v, 0, 126) * 13 * 192 / 126);   /* (once a frame: tools/div_audit.txt) */
}

/* a frame's work for this block: the phases in order, up to the share of the frame this block is due (capped: a long
 * POS start spreads over more frames) */
static void pst_work(const track_t *t)
{
    const int16_t *p = t->p;
    uint32_t want = (uint32_t)((uint64_t)pstx.total * (pstx.blk + 1u) / (ST_BLK - 4u)), budget, cap = pstx.total / (ST_BLK / 2u);
    uint32_t smear = (uint32_t)clamp(p[P_E3], 0, 127), adv = pstx.advq >> 16;
    budget = want > pstx.work ? want - pstx.work : 0;
    if (budget > cap)
        budget = cap;
    while (budget && pstx.phase != SP_DONE) {
        uint32_t d = 0;
        switch (pstx.phase) {
        case SP_DEC:
            d = sp_dec(budget, adv);
            if (pstx.dp >= ST_N)
                pstx.phase = SP_BREV_A, pstx.cur = 0, pstx.advq &= 0xFFFFu;
            break;
        case SP_BREV_A:
            d = sp_brev(pst_a, budget);
            if (pstx.cur >= ST_M)
                pstx.phase = SP_FFT_A, pstx.cur = 0, pstx.stage = 0;
            break;
        case SP_FFT_A:
            d = sp_fft(pst_a, 0, budget);
            if (pstx.stage >= ST_LOG)
                pstx.phase = SP_SPLIT, pstx.cur = 0;
            break;
        case SP_SPLIT:
            d = sp_split(budget, smear);
            if (pstx.cur > ST_M / 2u)
                pstx.phase = SP_MAP, pstx.cur = 0, pstx.pass = 0;
            break;
        case SP_MAP:
            d = sp_map(budget, p, pstx.cut);
            if (pstx.pass >= pst_passes())
                pstx.phase = SP_ISPLIT, pstx.cur = 0;
            break;
        case SP_ISPLIT:
            d = sp_isplit(budget);
            if (pstx.cur > ST_M / 2u)
                pstx.phase = SP_BREV_B, pstx.cur = 0;
            break;
        case SP_BREV_B:
            d = sp_brev(pst_b, budget);
            if (pstx.cur >= ST_M)
                pstx.phase = SP_FFT_B, pstx.cur = 0, pstx.stage = 0;
            break;
        case SP_FFT_B:
            d = sp_fft(pst_b, 1, budget);
            if (pstx.stage >= ST_LOG)
                pstx.phase = SP_OLA, pstx.cur = 0;
            break;
        case SP_OLA:
            d = sp_ola(budget);
            if (pstx.cur >= ST_HOP)
                pstx.phase = SP_DONE;
            break;
        default:
            break;
        }
        if (!d)
            d = 1;                                    /* (a phase change: progress) */
        pstx.work += d;
        budget = d >= budget ? 0 : budget - d;
    }
}

/* a new frame's work starts: the scan's advance and the frame's units */
static void pst_frame(const track_t *t)
{
    pstx.advq = (pstx.advq & 0xFFFFu) + pst_adv_q16(t->p[P_E2]);
    pstx.phase = SP_DEC, pstx.dp = 0, pstx.cur = 0, pstx.stage = 0, pstx.pass = 0, pstx.work = 0, pstx.blk = 0;
    pstx.total = pst_units(t->p) + pstx.skip * U_DEC;
}

/* the zone for the part's newest note, and a fresh start at POS when nothing sounds */
static void pst_start(const track_t *t, uint32_t src, uint32_t zl)
{
    uint32_t i;
    const smp_zone_t *z = pst_zone(src, zl);
    pstx.z = z, pstx.src = (uint8_t)src, pstx.zl = (uint8_t)zl;
    pstx.scan = 0, pstx.pred = 0, pstx.idx = 0;
    pstx.skip = z->n > ST_N ? (uint32_t)(((uint64_t)(z->n - ST_N) * (uint32_t)clamp(t->p[P_E1], 0, 127)) >> 7) : 0u;
    pstx.advq = 0;
    for (i = 0; i < ST_HOP; i++)
        pst_buf[0][i] = pst_buf[1][i] = pst_buf[2][i] = 0;
    pstx.play = pst_buf[0], pstx.next = pst_buf[1], pstx.tail = pst_buf[2];
    pstx.ready = 0, pstx.pp = 0;
    pst_frame(t);
}

AINL uint32_t pst_part(const track_t *t) { return (uint32_t)(t - trk) % NPART; }

static void stretch_note_on(track_t *t, voice_t *v)
{
    uint32_t src = (uint32_t)t->p[P_E0] % SMP_NALL, part = pst_part(t);
    int32_t zl = pst_find(src, v->note);
    v->s[0] = zl;
    if (zl < 0 || pstx.owner != part + 1u)
        return;
    if (pstx.phase == SP_IDLE || pstx.src != src || pstx.zl != (uint32_t)zl || !pstx.z)   /* nothing playing, or another zone */
        pst_start(t, src, (uint32_t)zl);
}

/* the block: ownership, nothing while no voice sounds, the frame boundary, this block's share of the work */
static void stretch_block(track_t *t)
{
    uint32_t part = pst_part(t), i, any = 0;
    if (pstx.owner != part + 1u) {                      /* another part's: take it when its owner has gone quiet */
        if (pstx.owner && pstx.seen[part] != pstx.beat) {
            pstx.seen[part] = pstx.beat, pstx.miss[part] = 0;
            return;
        }
        if (pstx.owner && ++pstx.miss[part] < 4u)
            return;
        pstx.owner = (uint8_t)(part + 1u);
        pstx.phase = SP_IDLE;
        for (i = 0; i < NVOICE; i++)
            pstx.sv[i].ratio = 0;
    }
    for (i = 0; i < NVOICE; i++)
        any |= t->v[i].active;
    if (!any) {                                       /* (no pulse: another part playing STRETCH takes it over) */
        pstx.phase = SP_IDLE;
        return;
    }
    pstx.beat++;
    if (pstx.phase == SP_IDLE) {                        /* voices from before the ownership: their newest note's zone */
        uint32_t src = (uint32_t)t->p[P_E0] % SMP_NALL, best = 0, age = 0;
        int32_t zl;
        for (i = 0; i < NVOICE; i++)
            if (t->v[i].active && t->v[i].age >= age)
                age = t->v[i].age, best = i;
        zl = pst_find(src, t->v[best].note);
        if (zl < 0)
            return;
        pst_start(t, src, (uint32_t)zl);
    }
    if (pstx.blk >= ST_BLK) {                           /* the frame boundary */
        if (pstx.phase == SP_DONE) {
            int16_t *q = pstx.play;
            pstx.play = pstx.next, pstx.next = q, pstx.ready = 1;
            pst_frame(t);
        } else {                                      /* not ready (a long POS start): silence meanwhile */
            for (i = 0; i < ST_HOP; i++)
                pstx.play[i] = 0;
            pstx.ready = 0;
            pstx.blk = 0;
            pstx.total += pst_units(t->p);
            pstx.work = 0;
        }
        pstx.pp = 0;
    }
    pst_work(t);
    pstx.blk++;
}

/* a voice: its pitch (against the zone's root and rate) and level, taken by the next frame's map; no output here */
static void stretch_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    uint32_t vi = (uint32_t)(v - t->v);
    const smp_zone_t *z = pstx.z;
    (void)out, (void)n;
    if (pstx.owner != pst_part(t) + 1u || !z || vi >= NVOICE)
        return;
    pstx.sv[vi].ratio = (uint32_t)(((uint64_t)pow2_q16(clamp(m->pitch16 - z->root16, -2304, 1152)) * z->rate) >> 16);
    pstx.sv[vi].amp = m->amp1;
    if (v->age >= pstx.age)
        pstx.age = v->age, pstx.cut = m->cutoff;
}

/* the part's output: this frame's buffer, as the voices' sum would be */
static void stretch_post(track_t *t, int32_t *out, uint32_t n, uint32_t nr)
{
    uint32_t i, vi;
    (void)nr;
    for (vi = 0; vi < NVOICE; vi++)                   /* (a voice that stopped rendering: off for the next map) */
        if (!t->v[vi].active)
            pstx.sv[vi].ratio = 0;
    if (pstx.owner != pst_part(t) + 1u || pstx.phase == SP_IDLE || !pstx.ready)
        return;
    for (i = 0; i < n && pstx.pp + i < ST_HOP; i++)
        out[i] += mulq15(pstx.play[pstx.pp + i], VOICE_FS) << 3;   /* (x 4: the random phases spread a voice thin) */
    pstx.pp += n;
}

static const preset_t STRETCH_PRESETS[] = {
    /* {SRC, POS, STRCH, SMEAR, TONE, SUB, SHIM, BLUR}, {A D S R}, ENV -> FILTER, mono */
    {"GLACIER", {0, 0, 52, 127, 110, 0, 0, 0}, {90, 100, 127, 100}, 0, 0, FX(0, 0, 0, 60)},
    {"FROZEN", {0, 64, 127, 127, 96, 20, 30, 40}, {100, 100, 127, 110}, 0, 0, FX(0, 0, 0, 70)},
    {"FOG", {0, 30, 80, 127, 80, 40, 0, 60}, {90, 100, 127, 96}, 0, 0, FX(0, 0, 10, 60)},
    {"TIDE", {0, 10, 40, 100, 100, 0, 20, 10}, {80, 100, 127, 96}, 0, 0, FX(0, 20, 20, 50)},
    {"DEEP", {0, 20, 70, 127, 70, 80, 0, 20}, {90, 100, 127, 100}, 0, 0, FX(0, 0, 0, 50)},
    {"HALO", {0, 50, 90, 127, 120, 0, 60, 30}, {100, 100, 127, 110}, 0, 0, FX(0, 30, 10, 70)},
    {"PHASEY", {0, 0, 30, 0, 110, 0, 0, 0}, {40, 100, 127, 80}, 0, 0, FX(0, 0, 10, 40)},
    {"DUST", {0, 40, 100, 127, 90, 0, 0, 127}, {90, 100, 127, 100}, 0, 0, FX(0, 0, 10, 60)},
};

static const engine_t ENG_STRETCH = {
    .name = "STRETCH",
    .page_title = {"SRC", "SPEC"},
    .edit = {
        PN("SRC", F_ENUM, 0, SMP_NALL - 1, 0, SMP_ALL_NAMES),
        PU("POS", F_PCT, 0, 127, 0, 0),
        PU("STRCH", F_INT, 0, 127, 52, 0),
        PU("SMEAR", F_PCT, 0, 127, 127, 0),
        PU("TONE", F_CUTOFF, 0, 127, 110, 0),
        PU("SUB", F_PCT, 0, 127, 0, 0),
        PU("SHIM", F_PCT, 0, 127, 0, 0),
        PU("BLUR", F_PCT, 0, 127, 0, 0),
    },
    .presets = STRETCH_PRESETS,
    .npresets = sizeof(STRETCH_PRESETS) / sizeof(STRETCH_PRESETS[0]),
    .fil_page = 1,
    .note_on = stretch_note_on,
    .render = stretch_render,
    .color = 0xAEDE,                             /* #a8d8f0 (tools/colors.json) */
    .macro = {P_E4, P_E3, P_E2, P_E7},
    .block = stretch_block,
    .post = stretch_post,
};
