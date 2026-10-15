/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Effects: per-track DIST insert, then sends into three shared buses (chorus, tempo delay, reverb).
 * Stereo dry mix; the chorus and the reverb come back in stereo, the delay in the middle. */
#ifndef FELUCCA_DLY_LEN
#define FELUCCA_DLY_LEN 65536u   /* the delay line, samples (a power of two): 1.49 s, 1/4 at 40 BPM fits; 2 bytes
                                  * each in .pool. 32768 (0.74 s: 1/4 down to 81 BPM) frees 64 KiB of pool */
#endif
#define DLY_LEN ((uint32_t)FELUCCA_DLY_LEN)
_Static_assert(FELUCCA_DLY_LEN >= 4096 && (FELUCCA_DLY_LEN & (FELUCCA_DLY_LEN - 1)) == 0, "FELUCCA_DLY_LEN: a power of two");
#define CHO_LEN 2048u
static int16_t dly_buf[FELUCCA_FX_DELAY ? DLY_LEN : 1] __attribute__((section(".pool")));   /* (an FX not built: */
static int16_t cho_buf[FELUCCA_FX_CHORUS ? CHO_LEN : 1] __attribute__((section(".pool")));  /* no buffer) */
/* the reverb: two input diffusers, then four delay lines mixed by a Hadamard matrix (a feedback delay
 * network: every echo feeds all four, so it thickens instead of ringing like a comb), damped in the
 * loop, one line slowly modulated (no metallic tone on long tails); left and right take different lines.
 * That is the ROOM. The algorithms built (rev_type.c: ROOM, PLATE and FDN8 in reverb_alt.c at 22.05 kHz, AIRWIN in
 * reverb_airwin.c, SPRING in spring.c) are FX > REVERB > TYPE, switched at run time (rev_bus, below); only one runs,
 * so they share one line buffer, rev_line, the size of the largest built (ROOM 8684 samples, REV_HALF 4346; PLATE /
 * FDN8 / AIRWIN RV_N; SPRING 4096). */
#define REV_ALT (FELUCCA_REV_PLATE || FELUCCA_REV_FDN8 || FELUCCA_REV_AIRWIN)   /* (AIRWIN: reverb_airwin.c) */
#define REV_ROOM_HALF (FELUCCA_REV_ROOM && FELUCCA_REV_HALF)
#define REV_TANK_HALF (REV_ROOM_HALF || REV_ALT)   /* a tank behind the half-band filters */
#if FELUCCA_REV_POOL             /* REV_POOL: the lines in the pool (main RAM is the scarcer); the same code */
#define REV_SECTION __attribute__((section(".pool")))
#else
#define REV_SECTION              /* (.bss) */
#endif
#if FELUCCA_REV_ROOM
#if FELUCCA_REV_HALF             /* REV_HALF: the tank at 22.05 kHz (rev_half_run below), every length halved */
#define REV_MOD 6                /* (the same times in seconds: 35..63 ms, the modulation +-0.27 ms) */
#define REV_N0 779u              /* (coprime: 19 x 41 and three primes; their mean 2169 / 2 samples: the */
#define REV_N1 967u              /*  full rate's 2167.5 / 2, the same room and, at the same loop gain, the */
#define REV_N2 1193u             /*  same decay time) */
#define REV_N3 1399u
#define REV_A0 278u
#define REV_A1 221u
#else
#define REV_MOD 12               /* samples the modulated line moves (+-) */
#define REV_N0 1559u             /* 35..63 ms, coprime */
#define REV_N1 1931u
#define REV_N2 2389u
#define REV_N3 2791u
#define REV_A0 556u
#define REV_A1 441u
#endif
static const uint16_t REV_LINE[4] = {REV_N0, REV_N1, REV_N2, REV_N3};
static const uint16_t REV_AP[2] = {REV_A0, REV_A1};
#define REV_ROOM_LEN (REV_N0 + REV_N1 + REV_N2 + REV_N3 + REV_MOD + 2u)
static int16_t rev_ap[FELUCCA_FX_REVERB ? REV_A0 + REV_A1 : 1] __attribute__((section(".pool")));
#else
#define REV_MOD 6                /* (fx_buses' line-0 LFO: unused by the other tanks) */
#define REV_ROOM_LEN 0u
#endif
/* the shared line buffer without PLATE / FDN8 (with them: reverb_alt.c, at least RV_N): the ROOM's, SPRING's loop */
#define REV_LINE_OWN (REV_ROOM_LEN > 4096u * FELUCCA_SPRING ? REV_ROOM_LEN : 4096u * FELUCCA_SPRING)
#if !REV_ALT
static int16_t rev_line[(FELUCCA_FX_REVERB ? REV_LINE_OWN : 0u) ? REV_LINE_OWN : 1] REV_SECTION;
#endif
#define FX_Q_MAX 0x40000000u     /* (fx_q, below: the zero-write counts stop here) */
static struct {
    uint32_t dly_w, cho_w, cho_ph, rev_ph;
    int32_t dly_lp, dly_le;         /* the delay's COLOR low-pass and its step's remainder (0..32767) */
    uint16_t line_i[4], ap_i[2];
    int32_t line_lp[4];
    uint32_t cho_q, dly_q, rev_q;   /* samples since the bus last wrote a non-zero value into its lines */
} fx = {.cho_q = FX_Q_MAX, .dly_q = FX_Q_MAX, .rev_q = FX_Q_MAX};   /* at boot every line is 0
                                    * (main.c clears .bss and the pool), so the buses start idle */

/* An idle bus is skipped only when its output is exactly 0 and stays 0, never on a threshold: every
 * tail rings out to the last LSB. A bus is idle when (1) its send block is all 0, (2) for at least its
 * longest line it has written nothing but 0, so every line and diffuser cell holds 0, and (3) its
 * filters are at 0. From that state a 0 input writes 0, reads 0 and outputs 0, so skipping a block
 * only has to move the write / read indices on (the LFOs move on per block anyway). Resuming from it is
 * bit-identical to never having skipped. (The delay and reverb loops round so that a tail with no
 * input reaches exactly 0: mul_tz, fx_step, half_ap below; the delay's low-pass keeps its remainder, dly_step.) */
#if FELUCCA_REV_ROOM
#define REV_LONGEST (REV_N3 > REV_N0 + REV_MOD + 2u ? REV_N3 : REV_N0 + REV_MOD + 2u)
#define REV_Q_ROOM (REV_LONGEST * (FELUCCA_REV_HALF ? 2u : 1u))   /* the longest line, in output samples */
#define REV_LP_ROOM() (fx.line_lp[0] | fx.line_lp[1] | fx.line_lp[2] | fx.line_lp[3])
#else
#define REV_Q_ROOM 1u
#define REV_LP_ROOM() 0
#endif
AINL uint32_t fx_q(uint32_t q, int32_t wrote, uint32_t n)   /* the zero-write count after a block */
{
    return wrote ? 0u : q < FX_Q_MAX ? q + n : q;
}
AINL uint16_t fx_wrap(uint32_t i, uint32_t n, uint32_t len)   /* (i + n) mod len, n < len */
{
    i += n;
    return (uint16_t)(i >= len ? i - len : i);
}
AINL int32_t fx_any(const int32_t *x, uint32_t n)   /* any non-zero sample */
{
    int32_t o = 0;
    uint32_t i;
    for (i = 0; i < n; i++)
        o |= x[i];
    return o;
}

/* DIST: drive relative to the part's level. A peak follower (instant attack, ~0.37 s release) sets the
 * drive, so DST means the same amount of clipping on a quiet pluck and a loud pad (the parts' peaks
 * differ by 30 dB): the input is scaled to its peak, times the drive, through the asymmetric soft clip (a
 * little bias: even harmonics) and a one-pole tone low-pass that closes with DST (~5 kHz at 127), then
 * scaled back to the level it came in at, times a make-up that keeps the loudness about the same over the
 * knob (the top end a little louder: it bites). The band under ~55 Hz stays out of the clipper (no mud)
 * and is added back dry, so a bass keeps its weight.
 * The knob is exponential: every step of DST is the same step in dB of drive (0.31 dB), 0.5x of the peak
 * at DST 1 (barely touched) .. 48x at 127 (34 dB into the clip: a fuzz; 3x the 16x of 2026-10-07).
 * The per-block gains: dist_gains (XIP, once a block). The state: a dist_t (core.h), one per part now; the
 * same dist_run serves any other instance (a voice, an FX slot) that keeps its own dist_t. */
typedef struct {
    int32_t gi, us, go, sh, k, b0;   /* in: xs = (x - low) >> sh, u = xs * gi >> us; out: y * go >> (15 - sh); tone; softclip(bias) */
} dist_g_t;
#define DIST_BIAS 2400           /* the clip's offset (Q15): even harmonics */
#define DIST_Q(d) (((d) * 3398) >> 8)   /* the drive at DST d in 1/256 octaves over 0.5x: 0 .. 1685 (6.58 octaves: 48x) */
/* the drive at DST d (1..127), Q9 times the peak: 0.5 x 2^(DIST_Q / 256), 265 (0.52x) .. 24520 (47.9x) */
static int32_t dist_drive(int32_t d)
{
    static const int32_t EXP2[17] = {32768, 34219, 35734, 37316, 38968, 40693, 42495, 44376, 46341,
                                     48393, 50535, 52773, 55109, 57549, 60097, 62757, 65536};   /* 2^(i / 16), Q15 */
    int32_t q = DIST_Q(d), f = (q >> 4) & 15, m;
    m = EXP2[f] + (((EXP2[f + 1] - EXP2[f]) * (q & 15)) >> 4);   /* 2^(frac), Q15 (to 0.002 dB) */
    return (m << (q >> 8)) >> 7;                                 /* 0.5 = 256 (Q9): 2^15 x 2^oct >> 7 */
}
static void dist_gains(dist_t *s, int32_t d, const int32_t *b, uint32_t n, dist_g_t *o)
{
    /* the make-up (x 4096 / G, Q9) by half octave of drive (G = 0.5 x 2^(h / 2), h 0..13); 2^28 / the peak's step (no divides) */
    static const int32_t MKG[15] = {67824, 46901, 37272, 26997, 23068, 19125, 17980, 15569, 15861, 14822, 14768,
                                    15500, 16700, 18000, 19300};   /* (from 16x on: +2 dB at 127, it bites) */
    static const uint16_t INV[12] = {58254, 47663, 40330, 34953, 30840, 27594, 24966, 22795, 20972, 19418, 18079, 16913};
    int32_t e = s->env, q = DIST_Q(d), g = dist_drive(d), gi, us = 9, mkg, sh = 0, j, h = q >> 7;
    uint32_t i;
    if (!e) {                                           /* coming on: no stale states (a thump), the peak of this block */
        s->lo = b[0];
        s->l1 = 0;
        for (i = 0; i < n; i++)
            e = b[i] > e ? b[i] : -b[i] > e ? -b[i] : e;
    }
    e -= e >> 9;                                        /* the peak's release, ~0.37 s (blocks of 32; the attack: dist_run) */
    e = e > 4096 ? e : 4096;                            /* (a floor: near silence is not driven up to full scale; never 0: on) */
    s->env = e;
    while (sh < 30 && (e >> sh) >= 16384)           /* (bounded: a shift past 31 is undefined, the loop could not end) */
        sh++;
    j = ((e >> sh) >> 10) - 4;                          /* the peak in steps of 1024 (4096 .. 16383): in and out alike, */
    mkg = MKG[h] + (((MKG[h + 1] - MKG[h]) * (q & 127)) >> 7);   /* (so the small-signal gain is exact) */
    gi = (g * INV[j]) >> 13;                            /* x / peak x G, in the clip's units (32768 = 1) x 2^us */
    while (gi >= 65536)                                 /* (|xs| < 2^15: the product fits 32 bits) */
        gi >>= 1, us--;
    o->gi = gi, o->us = us;
    o->go = (((j + 4) * 1024 + 512) * mkg) >> 15;       /* back to the input's level, / G, x make-up: y * go < 2^31 */
    o->sh = sh;
    o->k = 32000 - d * 120;                             /* tone: open .. ~5 kHz at 127 (one pole), Q15 */
    o->b0 = softclip(DIST_BIAS);
}
/* one DIST insert over n samples of b at DST d (1..127; 0: off, its states restart when it comes back) */
static HOT void dist_run(dist_t *s, int32_t d, int32_t *b, uint32_t n)
{
    int32_t i, m = 0, lo, l1;
    dist_g_t g;
    if (!d) {
        s->env = 0;
        return;
    }
    FAR(dist_gains)(s, d, b, n, &g);
    lo = s->lo, l1 = s->l1;
    for (i = 0; i < (int32_t)n; i++) {
        int32_t x = b[i], y;
        lo += (x - lo + 64) >> 7;                       /* ~55 Hz: the low band, kept out of the clipper */
        x = clamp((x - lo) >> g.sh, -32767, 32767);     /* (2x the peak and more: x * gi fits 32 bits) */
        y = x < 0 ? -x : x;                             /* the block's peak (for the next block's gains) */
        m = y > m ? y : m;
        y = softclip(((x * g.gi) >> g.us) + DIST_BIAS) - g.b0;
        l1 += mulq15(y - l1, g.k);                      /* tames the fizz */
        b[i] = ((l1 * g.go) >> (15 - g.sh)) + lo;       /* the input's level again, the low band back dry */
    }
    s->lo = lo, s->l1 = l1;
    m <<= g.sh;
    if (m > s->env)                                     /* the follower's attack (one block late: the clamp holds) */
        s->env = m;
}

/* master: peak limiter in front of the soft clipper. Fast attack (~0.1 ms),
 * ~150 ms release, threshold where tanh is still nearly linear, so chords
 * get quieter instead of crushed. */
#define LIM_T 18000
static int32_t lim_env = LIM_T;
static volatile uint8_t fx_lowcut;     /* settings: 12 dB/oct ~110 Hz for the small speaker */
static int32_t lc_l1, lc_l2, lc_r1, lc_r2, dc_l, dc_r, dce_l, dce_r;

/* DC blocker (~2 Hz), always on: a leaky integrator of the input (Q6 state) subtracted from it.
 * The >> 12 step keeps its remainder (error feedback, 0..4095) and adds it to the next one, so no
 * part of the step is lost: the state follows the input exactly, down to 0 after the sound stops.
 * (It was rounded, and a rounded step of (x - dc) / 4096 stops moving at |x - dc| < 2048: a
 * constant offset of up to +-31 stayed at the output after silence.) */
AINL int32_t dc_block(int32_t x, int32_t *dc, int32_t *err)
{
    int32_t e = (x << 6) - *dc + *err, d = e >> 12;
    *err = e - (d << 12);
    *dc += d;
    return x - ((*dc + 32) >> 6);
}

static int32_t lce[4];
#define lowcut1(x, lc, err) lowcut_ef(x, lc, err, 6)     /* x minus its one-pole low-pass (dsp_common.h) */

/* the output stage: linear up to KNEE (a clean low end: no tanh harmonics on a loud sine), above it
 * a tanh knee with the same slope at the joint, to full scale */
#define KNEE 16384
AINL int32_t knee(int32_t x) { return soft_knee(x, KNEE); }   /* (dsp.c) */

#if FELUCCA_BASSPLUS
#include "bassplus/bassplus.c"          /* the menu's LOWCUT: BASS+ for the small speaker (from Felucca 1.0) */
#endif
AINL void master_pre(int32_t *l, int32_t *r)   /* the DC blocker and the low cut (master_out; LIMIT: master_comp.c) */
{
    *l = dc_block(*l, &dc_l, &dce_l);
    *r = dc_block(*r, &dc_r, &dce_r);
    if (fx_lowcut) {                  /* two one-pole high-passes, error feedback as dc_block (the */
#if FELUCCA_BASSPLUS
        if (fx_lowcut == 2u)
            bassplus_out(l, r);                     /* BASS+: an octave up, the bass' harmonics added */
        else
#endif
        {
        *l = lowcut1(*l, &lc_l1, &lce[0]);          /* rounded step stopped at |x - lc| < 32: an offset) */
        *l = lowcut1(*l, &lc_l2, &lce[1]);
        *r = lowcut1(*r, &lc_r1, &lce[2]);
        *r = lowcut1(*r, &lc_r2, &lce[3]);
        }
    }
}
static inline HOT void master_out(int32_t *l, int32_t *r)
{
    int32_t al, ar, a;
    master_pre(l, r);
    al = *l < 0 ? -*l : *l;
    ar = *r < 0 ? -*r : *r;
    a = al > ar ? al : ar;
    if (a > lim_env)
        lim_env += (a - lim_env) >> 2;
    else if (lim_env > LIM_T)
        lim_env -= ((lim_env - LIM_T) >> 12) + 1;
    if (lim_env > LIM_T) {
        int32_t g = (int32_t)(((uint32_t)LIM_T << 15) / (uint32_t)lim_env);   /* < 32768 */
        *l = ((*l >> 4) * g) >> 11;                      /* >> 4 first: |l| may be far above Q15 */
        *r = ((*r >> 4) * g) >> 11;
    }
    *l = knee(*l);
    *r = knee(*r);
}
#if FELUCCA_MASTER_COMP
#include "master_comp/master_comp.c"       /* GLO > COMP / LIMIT: the bus compressor, the brickwall limiter (mix_finish) */
#endif



AINL uint32_t delay_samples(void)
{
    uint32_t s = dly_samples((uint32_t)song.g[G_DTIME]);   /* (DLY_DOT: 1/8D, 1/16D; a too long one halves below:
                                                          * 1/8D -> 1/16D, still dotted, where 2.4 clamps) */
    while (s >= DLY_LEN)
        s >>= 1;                                        /* longer than the line: half of it, still on the beat
                                                         * (X0X 892a3b5 by Charles Vestal, GPL-3.0-only, unchanged
                                                         * in X0X 1.0.6); cutting it short is not */
    return s < 16u ? 16u : s >= DLY_LEN ? DLY_LEN - 1u : s;
}

/* The delay and reverb loops round so that a tail with no input decays to exactly 0 (and the bus can
 * then be skipped, above). With floor rounding everywhere a small state below 0 stopped for ever:
 * a filter step k * (x - lp) / 2^15 below +1 rounded to 0 (lp never reached x from below), a loop gain
 * g < 1 kept -1 at -1 (floor(-g) = -1), and so did a diffuser cell (-1 >> 1 = -1). The three spots
 * now differ from floor only where it stalled: a step moves at least 1 (fx_step), the loop gains round
 * toward 0 (mul_tz: every pass shrinks a non-zero value), a cell of -1 halves to 0 (half_ap). */
/* (mul_tz: dsp_common.h) */
AINL int32_t fx_step(int32_t a, int32_t b)              /* a * b / 2^15, floor; 1 for 0 < a * b < 2^15 */
{
    int32_t p = a * b;
    return p > 0 && p < 0x8000 ? 1 : p >> 15;
}
AINL int32_t half_ap(int32_t x) { return x == -1 ? 0 : x >> 1; }   /* x / 2, floor; -1 -> 0 */

/* one sample of each bus (inlined into the bus' loop); *wr collects the values written into the
 * bus' lines (0: it wrote only 0) */
#define FX_STEP static inline __attribute__((always_inline))
/* chorus: two modulated short delays, 5..15 ms, the LFO half a turn apart: left and right move apart.
 * r0, r1: the two read points, Q8 samples back */
FX_STEP int32_t cho_step(int32_t in, int32_t r0, int32_t r1, int32_t *yr, int32_t *wr)
{
    int32_t v = clamp(in >> 1, -32768, 32767), c0, c1, d0, d1;
    uint32_t i0 = (uint32_t)r0 >> 8, i1 = (uint32_t)r1 >> 8;
    cho_buf[fx.cho_w & (CHO_LEN - 1u)] = (int16_t)v;
    *wr |= v;
    c0 = cho_buf[(fx.cho_w - i0) & (CHO_LEN - 1u)], c1 = cho_buf[(fx.cho_w - i0 - 1u) & (CHO_LEN - 1u)];
    d0 = cho_buf[(fx.cho_w - i1) & (CHO_LEN - 1u)], d1 = cho_buf[(fx.cho_w - i1 - 1u) & (CHO_LEN - 1u)];
    fx.cho_w++;
    *yr = (d0 + (((d1 - d0) * (r1 & 255)) >> 8)) << 1;
    return (c0 + (((c1 - c0) * (r0 & 255)) >> 8)) << 1;
}

/* FDBK (0..120, shown 0..100 %) -> the delay's loop gain, Q15. Up to 85 % (102) the line it always had, 230 a step
 * (the default 60: 0.421 a repeat); above it 1 - gain shrinks with the square of the distance to the top (continuous
 * at 102: 0.716, about 21 repeats to -60 dB; 95 %: 0.968, 213; 99 %: 0.9991, ~8000) to exactly 1 at 100 %: the
 * repeats never fade (dub). At 32768 mul_tz is exact (lp x 2^15 >> 15) and the COLOR low-pass reaches its input
 * exactly (its step keeps its remainder, dly_step), so the DC gain of the loop is 1: the lows hold for ever, the highs
 * still fade by COLOR.
 * (tests/delay_test.c measures the gain of each repeat over FDBK and COLOR.) */
#define DLY_FB_KNEE 102
#if defined(MAC_DFDBK_MAX)
_Static_assert(MAC_DFDBK_MAX <= DLY_FB_KNEE, "macro.c: a macro alone never pushes the delay into its endless top");
#endif
AINL int32_t dly_fb(int32_t v)
{
    int32_t u = 120 - v;
    return v <= DLY_FB_KNEE ? v * 230 : 32768 - ((u * u * 7355) >> 8);   /* (7355 / 2^8 = 9308 / 18^2) */
}
/* a return level (core.h G_CRET G_DRET G_RRET, 0..127) -> its gain, Q15: 127 unity (32768: the mix of before, sample
 * for sample), below it v / 128 (64: one half exactly) */
#define FX_RETQ(v) ((v) >= 127 ? 32768 : (int32_t)(v) << 8)
#define FX_ZCHUNK 512u           /* a return at 0: its bus' line cleared this many samples a block (fx_zclear) */
static struct {
    uint32_t cclr, dclr;         /* the chorus' and the delay's line cleared up to here (0: not clearing) */
} fxz;
/* a return at 0 (fx_buses): its bus is not run, and what its line holds is cleared, FX_ZCHUNK samples a block (XIP; no
 * 64 KB clear in one half), before it may run again (no old echo when it comes back). b: the line, len samples; c:
 * cleared so far; returns the new count, 0 when the line is clear */
static __attribute__((noinline)) uint32_t fx_zclear(int16_t *b, uint32_t len, uint32_t c)
{
    uint32_t e = c + FX_ZCHUNK < len ? c + FX_ZCHUNK : len;
    for (; c < e; c++)
        b[c] = 0;
    return e < len ? e : 0u;
}
static __attribute__((noinline)) void cho_zero(void)   /* (CHO RET 0: a block, the line not clear yet) */
{
    if (fx.cho_q < CHO_LEN || fxz.cclr)
        if (!(fxz.cclr = fx_zclear(cho_buf, sizeof cho_buf / 2u, fxz.cclr)))
            fx.cho_q = FX_Q_MAX;                        /* (idle: every cell 0) */
}
static __attribute__((noinline)) void dly_zero(void)   /* (DLY RET 0: the same, and its low-pass) */
{
    if (fx.dly_q < DLY_LEN || fx.dly_lp || fxz.dclr) {
        fx.dly_lp = fx.dly_le = 0;
        if (!(fxz.dclr = fx_zclear(dly_buf, sizeof dly_buf / 2u, fxz.dclr)))
            fx.dly_q = FX_Q_MAX;
    }
}
/* the delay's values for a block: from XIP, through FAR (RAMTEXT is full; once a block, for a running delay): the
 * loop gain, COLOR's low-pass step, the wet level (its MIX times its return, DLY RET: exact at unity); returns the
 * length */
static __attribute__((noinline)) uint32_t dly_block(int32_t *fb, int32_t *col, int32_t *dmix)
{
    *fb = dly_fb(song.g[G_DFDBK]);
    *col = 2000 + song.g[G_DCOLOR] * 240;
    *dmix = (int32_t)((uint32_t)(song.g[G_DMIX] * 258) * (uint32_t)FX_RETQ(song.g[G_DRET]) >> 15);
    return delay_samples();
}
/* the loop's saturator (it was a clamp to int16): linear up to K = 28416 (-1.2 dB), above it a quadratic knee 8192
 * wide (slope 1 at the joint, 0 at its top) to 32512, under the int16 rails: no table, a few instructions only past
 * the knee. On a held loop (FDBK 100 %) more input saturates softly instead of clipping hard. (Measured on the golden
 * renders: the loud default-FDBK ones that reached the old clamp move least with this knee; one twice as wide moved
 * them 7 dB more.) */
#define DLY_SAT_S 14
#define DLY_SAT_H (1 << (DLY_SAT_S - 2))
#define DLY_SAT_K (32512 - DLY_SAT_H)
AINL int32_t dly_sat(int32_t v)
{
    int32_t a = v < 0 ? -v : v;
    if (a > DLY_SAT_K) {
        a -= DLY_SAT_K;
        a = a > 2 * DLY_SAT_H ? 2 * DLY_SAT_H : a;
        a = DLY_SAT_K + a - ((a * a) >> DLY_SAT_S);     /* e - e^2 / 4H: H at e = 2H */
        v = v < 0 ? -a : a;
    }
    return v;
}

/* delay with a low-passed feedback (in the middle) */
FX_STEP int32_t dly_step(int32_t in, uint32_t dl, int32_t col, int32_t fb, int32_t dmix, int32_t *wr)
{
    int32_t x = dly_buf[(fx.dly_w - dl) & (DLY_LEN - 1u)], v, e = (x - fx.dly_lp) * col + fx.dly_le;
    fx.dly_lp += e >> 15;                               /* (|x - lp| <= 65535, col < 32768: e fits) */
    fx.dly_le = e & 0x7FFF;
    v = dly_sat((in >> 1) + mul_tz(fx.dly_lp, fb));
    dly_buf[fx.dly_w & (DLY_LEN - 1u)] = (int16_t)v;
    *wr |= v;
    fx.dly_w++;
    return mulq15(x << 1, dmix);
}

#if FELUCCA_REV_ROOM || REV_ALT
#include "reverb/rev_math.c"          /* 2^-x (XIP: when SIZE / DAMP change) */
#endif
#if REV_ALT
#include "reverb/reverb_alt.c"        /* PLATE (rvp_*), FDN8 (rvf_*), the shared line buffer */
#if FELUCCA_REV_AIRWIN
#include "reverb/reverb_airwin.c"     /* AIRWIN (rva_*): Airwindows' VerbTiny in the same ring */
#else
#define RVA_BUSY() 0
#define rva_clear() ((void)0)
#endif
#define REV_Q (RV_Q > REV_Q_ROOM ? RV_Q : REV_Q_ROOM)   /* the longest a value stays in a built tank, output samples */
#define REV_LP_BUSY() (REV_LP_ROOM() | RV_LP_BUSY() | RVA_BUSY())
#else
#define REV_Q REV_Q_ROOM
#define REV_LP_BUSY() REV_LP_ROOM()
#endif

#if FELUCCA_REV_ROOM
/* reverb: two diffusers, then the four lines; r: line 0's read offset (Q8); returns left, *yr right */
#define REV_L0 (REV_N0 + REV_MOD + 2u)
#define REV_B1 REV_L0
#define REV_B2 (REV_B1 + REV_N1)
#define REV_B3 (REV_B2 + REV_N2)
/* fx_step for the lines' one-poles: their input (the Hadamard sum / 2) spans +-65535, so x - lp reaches +-131070
 * and a * b (b up to 32767 at DAMP 0) passed 2^31: the filter jumped far out of range, and after loud noise at a
 * low DAMP the loop held at the rails for ever. Here with b = 2 c + o: floor(a b / 2^15) = floor((a c + o (a >> 1))
 * / 2^14) (o a / 2 adds less than 1 / 2^14 past the floor), and |a c + (a >> 1)| < 131070 * 16383 + 65536 < 2^31.
 * fx_step's "at least 1" is max(p, min(a, 1)): 1 for a > 0, and for a <= 0 p >= a already (0 < b < 2^15). The
 * same value as fx_step wherever that did not overflow: bit-identical (every a, every b of both rates, checked).
 * (b is the block's, so b >> 1 and -(b & 1) are hoisted; with lpk = 32767 - 200 DAMP, odd, the mask folds away) */
AINL int32_t rev_lp_step(int32_t a, int32_t b)
{
    int32_t p = (a * (b >> 1) + ((a >> 1) & -(b & 1))) >> 14, m = a < 1 ? a : 1;
    return p > m ? p : m;
}
FX_STEP int32_t rev_step(int32_t in, int32_t r, int32_t g, int32_t lpk, int32_t *yr, int32_t *wr)
{
    int32_t a = mulq15(in, 13000), o0, o1, o2, o3;
    uint32_t k;
    {
        int16_t *c = rev_ap;
        for (k = 0; k < 2u; k++) {
            int32_t b = c[fx.ap_i[k]], v = a + half_ap(b), w = clamp(v, -32768, 32767);
            c[fx.ap_i[k]] = (int16_t)w;
            *wr |= w;
            a = b - (v >> 1);
            if (++fx.ap_i[k] >= REV_AP[k])
                fx.ap_i[k] = 0;
            c += REV_AP[k];
        }
    }
    {
        int16_t *c = rev_line;
        int32_t s0, s1, d0, d1, w0, w1, w2, w3;
        uint32_t ri = fx.line_i[0] + ((uint32_t)r >> 8), rj;
        if (ri >= REV_L0)                               /* (the oldest sample is at line_i: reading */
            ri -= REV_L0;                               /* past it shortens line 0 by 0..2 REV_MOD) */
        rj = ri + 1u >= REV_L0 ? 0u : ri + 1u;
        o0 = c[ri] + (((c[rj] - c[ri]) * (r & 255)) >> 8);
        o1 = c[REV_B1 + fx.line_i[1]];
        o2 = c[REV_B2 + fx.line_i[2]];
        o3 = c[REV_B3 + fx.line_i[3]];
        s0 = o0 + o1, d0 = o0 - o1, s1 = o2 + o3, d1 = o2 - o3;   /* Hadamard / 2: each feeds all four */
        fx.line_lp[0] += rev_lp_step(((s0 + s1) >> 1) - fx.line_lp[0], lpk);
        fx.line_lp[1] += rev_lp_step(((d0 + d1) >> 1) - fx.line_lp[1], lpk);
        fx.line_lp[2] += rev_lp_step(((s0 - s1) >> 1) - fx.line_lp[2], lpk);
        fx.line_lp[3] += rev_lp_step(((d0 - d1) >> 1) - fx.line_lp[3], lpk);
        w0 = clamp(mul_tz(fx.line_lp[0], g) + a, -32768, 32767);
        w1 = clamp(mul_tz(fx.line_lp[1], g) - a, -32768, 32767);
        w2 = clamp(mul_tz(fx.line_lp[2], g) + a, -32768, 32767);
        w3 = clamp(mul_tz(fx.line_lp[3], g) - a, -32768, 32767);
        c[fx.line_i[0]] = (int16_t)w0;
        c[REV_B1 + fx.line_i[1]] = (int16_t)w1;
        c[REV_B2 + fx.line_i[2]] = (int16_t)w2;
        c[REV_B3 + fx.line_i[3]] = (int16_t)w3;
        *wr |= w0 | w1 | w2 | w3;
        if (++fx.line_i[0] >= REV_L0) fx.line_i[0] = 0;
        if (++fx.line_i[1] >= REV_LINE[1]) fx.line_i[1] = 0;
        if (++fx.line_i[2] >= REV_LINE[2]) fx.line_i[2] = 0;
        if (++fx.line_i[3] >= REV_LINE[3]) fx.line_i[3] = 0;
    }
    *yr = o1 - o3;
    return o0 + o2;
}
/* The ROOM above SIZE 90 (room_long, below, sets its gains): rev_step with every rounding in the loop unbiased. The
 * one-poles' steps and the loop gains' products round at random while the value is RM_RT or more (floor of the
 * product plus a uniform 0 .. 2^14 - 1 / 2^15 - 1), toward 0 below, as FDN8's (reverb_alt.c rv_rnd); the Hadamard's
 * halving adds a random bit, line 0's read rounds to the nearest. Rounding toward 0 took ~1/2 LSB a pass: at a long
 * SIZE a quiet send's tail sank out early (SIZE 126, a burst 20 dB down: RT60 11 s against 23 s loud; 127: 14 s);
 * with only that unbiased, the halving's floor (-1/4 LSB a pass) held a DC in the loop (-12 at SIZE 120, for ever).
 * A 0 stays 0 and a lone small value dies, so the tail still rings out to exactly 0. The products fit: the
 * one-pole's as rev_lp_step's plus u < 2^14 (131070 x 16383 + 65535 + 16383 < 2^31), the gain's 65535 x 32767 +
 * 32767 < 2^31. One function (the default's rev_step stays inlined as it was; this copy costs a call a sample, only
 * above the knee); the send x rm.in, 2^(-doublings / 8) (as FDN8's: a long tail
 * builds up, its headroom) */
#define RM_RT 8                  /* (FDN8: 32; here 8: a quiet send's long tail rings ~1.5 x longer, the floor dies) */
static struct {
    int32_t key, g, lpk;         /* the SIZE / DAMP the gains are for (RM_KEY; 0: not yet), the gains */
    int32_t in;                  /* the send's gain, Q15 (13000 up to the knee) */
    uint32_t rq;                 /* the random rounding's generator */
} rm;                            /* (.bss: room_gains fills it at the first block) */
#define RM_KEY() ((song.g[G_RSIZE] << 8 | song.g[G_RDAMP]) + 1)
#define RM_BIG(a) ((uint32_t)(a) + (RM_RT - 1u) > 2u * RM_RT - 2u)   /* |a| >= RM_RT */
AINL int32_t rev_lp_rnd(int32_t a, int32_t b, int32_t u)   /* rev_lp_step, at random (u: 0 .. 2^14 - 1) */
{
    int big = RM_BIG(a), m = a < 1 ? a : 1, p = (a * (b >> 1) + ((a >> 1) & -(b & 1)) + (big ? u : 0)) >> 14;
    return big || p > m ? p : m;
}
AINL int32_t rev_mul_rnd(int32_t a, int32_t g, int32_t u)  /* mul_tz, at random (u: 0 .. 2^15 - 1) */
{
    int32_t p = a * g;
    return (p + (RM_BIG(a) ? u : p < 0 ? 0x7FFF : 0)) >> 15;
}
static HOT __attribute__((noinline)) int32_t room_step_long(int32_t in, int32_t r, int32_t g, int32_t lpk, int32_t *yr,
                                                            int32_t *wr)
{
    int32_t a = mulq15(in, rm.in), o0, o1, o2, o3, w, any = 0;
    uint32_t k, q = rm.rq, ri, rj;
    for (k = 0; k < 2u; k++) {                          /* (the diffusers: rev_step's) */
        int16_t *c = rev_ap + (k ? REV_A0 : 0u);
        int32_t b = c[fx.ap_i[k]], x = a + half_ap(b);
        w = clamp(x, -32768, 32767);
        c[fx.ap_i[k]] = (int16_t)w;
        any |= w;
        a = b - (x >> 1);
        if (++fx.ap_i[k] >= REV_AP[k])
            fx.ap_i[k] = 0;
    }
    ri = fx.line_i[0] + ((uint32_t)r >> 8);
    if (ri >= REV_L0)
        ri -= REV_L0;
    rj = ri + 1u >= REV_L0 ? 0u : ri + 1u;
    o0 = rev_line[ri] + (((rev_line[rj] - rev_line[ri]) * (r & 255) + 128) >> 8);   /* (to the nearest) */
    o1 = rev_line[REV_B1 + fx.line_i[1]];
    o2 = rev_line[REV_B2 + fx.line_i[2]];
    o3 = rev_line[REV_B3 + fx.line_i[3]];
    /* a line k (a constant: fx.line_i / line_lp stay apart, rev_step's code at the default as before): its one-pole
     * toward v / 2 (the Hadamard x 2), the gain, the send (+-a), written; its index on */
#define RM_LN(k, v, sa, base, len)                                                                              \
    q = q * 1664525u + 1013904223u;                                                                             \
    fx.line_lp[k] += rev_lp_rnd((((v) + (int32_t)((q >> 17) & 1u)) >> 1) - fx.line_lp[k], lpk, (int32_t)(q >> 18)); \
    w = clamp(rev_mul_rnd(fx.line_lp[k], g, (int32_t)((q >> 2) & 0x7FFFu)) + (sa), -32768, 32767);              \
    rev_line[(base) + fx.line_i[k]] = (int16_t)w;                                                               \
    any |= w;                                                                                                   \
    if (++fx.line_i[k] >= (len))                                                                                \
        fx.line_i[k] = 0
    RM_LN(0, o0 + o1 + o2 + o3, a, 0u, REV_L0);
    RM_LN(1, o0 - o1 + o2 - o3, -a, REV_B1, REV_N1);
    RM_LN(2, o0 + o1 - o2 - o3, a, REV_B2, REV_N2);
    RM_LN(3, o0 - o1 - o2 + o3, -a, REV_B3, REV_N3);
#undef RM_LN
    rm.rq = q;
    *wr |= any;
    *yr = o1 - o3;
    return o0 + o2;
}
#if !FELUCCA_REV_HALF
static HOT __attribute__((noinline)) void room_run_long(const int32_t *in, int32_t *wl, int32_t *wr, uint32_t n,
                                                        int32_t ma, int32_t mb, int32_t g, int32_t lpk, int32_t *wv)
{                                                       /* (a block at the full rate: out of fx_buses, whose ROOM */
    uint32_t i;                                         /*  loop keeps its code and registers) */
    for (i = 0; i < n; i++) {
        int32_t rr;
        wl[i] += room_step_long(in[i], ma + (((mb - ma) * (int32_t)i) >> CTL_LOG2), g, lpk, &rr, wv);
        wr[i] += rr;
    }
}
#endif
/* SIZE up to RM_KNEE (90, the default): the loop gain 17000 + 104 SIZE (RT60 0.5 .. 1.44 s at DAMP 60), as ever. Above
 * it, as FDN8 (reverb_alt.c): the decay time doubles every RM_STEPS / 10 steps (exponential in RT60, even to the ear)
 * and 127 adds RM_FRZ8 / 8 doublings, a near-freeze; the loop runs room_step_long (unbiased rounding: above). The
 * loop gain's loss a pass (dB) is the knee's times rho = 2^-doublings. DAMP's one-pole lp = o + c (lp - o) loses
 * 10 log10(1 + K q) dB at w (q = c / (1 - c)^2, K = 2 - 2 cos w): q goes to q rho, so its loss shrinks with the
 * decay too (exactly at a light DAMP, the treble a little darker than the ratio at a heavy one: DAMP 127 at rho 1/2,
 * 4 kHz loses 1.4 x the ideal; FDN8's exact rv_lpc did not fit in flash): the treble's RT60 keeps about its ratio to
 * the bass', DAMP still sets it, a long tail does not go dull. Both once when SIZE / DAMP change (XIP). Measured:
 * tests/reverb_test.c t_long. 5b5ad0c's overflow safety holds: lpk stays 1 .. 32767 (rev_lp_step), g below 32768 */
#define RM_KNEE 90
#define RM_LONG() (song.g[G_RSIZE] > RM_KNEE)   /* above the knee: room_step_long */
#define RM_STEPS 90              /* SIZE steps per doubling of the decay time above the knee, x 10 */
#define RM_FRZ8 10               /* SIZE 127: this many eighths of a doubling more */
#define RM_L90 (5266993u >> 8)   /* -log2(26360 / 32768), the knee's loop gain, Q16 */
#define RM_INSH 3                /* the send above the knee: x 2^(-doublings / 2^RM_INSH), as FDN8 (RV_INSH) */
AINL int32_t room_half_k(int32_t lpk)                  /* REV_HALF: the damping at half the rate (rev_half_run) */
{
    int32_t k = ((lpk * (58847 - ((26198 * lpk) >> 15))) >> 15) + 519;   /* -0.7995 lpk^2 + 1.7959 lpk + 0.0158 */
    return k > 32767 ? 32767 : k;
}
static uint32_t room_isqrt(uint32_t x)
{
    uint32_t r = 0, b = 1u << 30;
    for (; b; b >>= 2)
        if (x >= r + b)
            x -= r + b, r = (r >> 1) + b;
        else
            r >>= 1;
    return r;
}
static __attribute__((noinline)) void room_long(void)   /* the gains (XIP: rare; fx_buses calls it FAR) */
{
    int32_t s = song.g[G_RSIZE], d = song.g[G_RDAMP], y = s > RM_KNEE ? s - RM_KNEE : 0;
    uint32_t e = (uint32_t)y * ((10u << 24) / RM_STEPS) + (s >= 127 ? (uint32_t)RM_FRZ8 << 21 : 0u), rho, c, r, q;
    rm.key = RM_KEY();
    if (!y) {                                           /* up to the knee: as before the stretch */
        rm.g = 17000 + s * 104, rm.lpk = 32767 - d * 200, rm.in = 13000;
        return;
    }
    rho = (uint32_t)rv_exp2n(e);                        /* 2^(-e / 2^24), Q15 */
    rm.g = rv_exp2n((RM_L90 * rho) >> 7);               /* (Q16 x Q15 >> 7: Q24; < 2^31) */
#if REV_ROOM_HALF                                       /* (the half rate's fitted damping: the mapping of the full */
    c = 32768u - (uint32_t)room_half_k(32767 - 200 * d);   /* rate's stretched one clamps above lpk ~31500) */
#else
    c = 1u + 200u * (uint32_t)d;                        /* DAMP's pole (32768 - lpk), Q15: 1 .. 25401 */
#endif
    r = 32768u - c;                                     /* (>= 7367: r r >> 15 >= 1656) */
    q = (((c << 15) / ((r * r) >> 15)) * (rho >> 3)) >> 12;   /* q rho, Q15 (q <= 15.4 x 2^15, x rho / 8: < 2^31) */
    c = ((2u * q) << 7) / ((2u * q + 32768u + (room_isqrt((4u * q + 32768u) << 9) << 3)) >> 8);   /* 2q / (2q + 1 + */
    rm.lpk = c >= 32767u ? 1 : 32768 - (int32_t)c;      /* sqrt(4q + 1)): the pole for q, Q15 */
    rm.lpk = rm.lpk > 32767 ? 32767 : rm.lpk;
    rm.in = (13000 * rv_exp2n(e >> RM_INSH)) >> 15;
}
AINL void room_gains(int32_t *g, int32_t *lpk)          /* this block's loop gain and damping (fx_buses) */
{
    if (rm.key != RM_KEY())
        FAR(room_long)();
    *g = rm.g, *lpk = rm.lpk;
}
AINL void room_skip(uint32_t n)                          /* idle: the indices move on n tank samples (n <= CTL: */
{                                                        /* shorter than every line) */
    fx.ap_i[0] = fx_wrap(fx.ap_i[0], n, REV_AP[0]);
    fx.ap_i[1] = fx_wrap(fx.ap_i[1], n, REV_AP[1]);
    fx.line_i[0] = fx_wrap(fx.line_i[0], n, REV_L0);
    fx.line_i[1] = fx_wrap(fx.line_i[1], n, REV_LINE[1]);
    fx.line_i[2] = fx_wrap(fx.line_i[2], n, REV_LINE[2]);
    fx.line_i[3] = fx_wrap(fx.line_i[3], n, REV_LINE[3]);
}
#endif
/* every built tank's cells and filters but the shared line to 0: the ROOM's diffusers and low-passes, PLATE / FDN8's
 * low-passes and allpasses, AIRWIN's */
static void rev_cells_clear(void)
{
    uint32_t i;
#if REV_ALT
    rv_cells_clear();
    rva_clear();
#endif
#if FELUCCA_REV_ROOM
    for (i = 0; i < sizeof rev_ap / 2u; i++)
        rev_ap[i] = 0;
    for (i = 0; i < 4u; i++)
        fx.line_lp[i] = 0;
#endif
    (void)i;
}
/* every built tank's cells and filters to 0 (the bus then idle): the shared line and the rest */
static void rev_tank_clear(void)
{
    uint32_t i;
    for (i = 0; i < sizeof rev_line / 2u; i++)
        rev_line[i] = 0;
    rev_cells_clear();
}

#if REV_TANK_HALF
/* REV_HALF: rev_step once per two output samples, on lines half as long (above): half the RAM, about half the
 * work. Only the reverb's send and return change; the dry mix and the other buses stay at 44.1 kHz.
 *   in:  a half-band low-pass, 15 taps (-14, 0, 39, 0, -90, 0, 321, 512, 321, 0, -90, 0, 39, 0, -14) / 1024
 *        (least squares: +-0.12 dB to 8 kHz, -0.8 dB at 9 kHz, -6 dB at 11 kHz, -37 dB or less from 14.5 kHz),
 *        then every second sample;
 *   out: the same filter at gain 2 between each two tank samples, per side (the first output of a pair: the
 *        eight-tap sum; the second: the tank sample three back).
 * Four multiplies a filter per pair; the latency 14 samples (0.32 ms, a little more pre-delay). Same loop
 * gain per pass and lines as long in seconds: the same decay time. The damping (rev_half_run): the one-pole
 * whose response at 22.05 kHz, with line 0's linear interpolation, is nearest the full rate's from 50 Hz to
 * 9 kHz (least squares in dB, fitted as a quadratic in lpk; tests/reverb_test.c checks the RT60). Every value
 * is an FIR of the send or the tank: at 0 in, 0 out (the idle skip, above, also waits for rev_half_any() to be
 * 0). Blocks are CTL long: an even number of samples. */
_Static_assert((CTL & 1) == 0, "REV_HALF: the reverb takes the block in pairs");
static struct {
    int32_t e[16], l[16], r[16]; /* rings of 8, each value twice: h[j .. j + 7] newest first, no shifting. e: the */
    int32_t o[8];                /* send's newer sample of each pair, o (4): its older one; l, r: the tank's out */
    uint32_t j;
} rev_half;
AINL int32_t rev_half_any(void)                         /* any filter cell non-zero */
{
    return fx_any(rev_half.e, 16) | fx_any(rev_half.o, 8) | fx_any(rev_half.l, 16) | fx_any(rev_half.r, 16);
}
AINL int32_t rev_hb(const int32_t *h)                   /* the half-band's eight outer taps, x 1024 */
{
    return -14 * (h[0] + h[7]) + 39 * (h[1] + h[6]) - 90 * (h[2] + h[5]) + 321 * (h[3] + h[4]);
}
/* one sample of tank t at 22.05 kHz (t is a constant at every call: this folds to that tank's step) */
FX_STEP int32_t rev_tank_step(uint32_t t, int32_t y, int32_t r, int32_t g, int32_t lpk, int32_t *yr, int32_t *wv,
                              const int lng)
{
#if REV_ROOM_HALF
    if (t == RT_ROOM)
        return lng ? room_step_long(y, r, g, lpk, yr, wv) : rev_step(y, r, g, lpk, yr, wv);
#endif
#if FELUCCA_REV_PLATE
    if (t == RT_PLATE)
        return rvp_step(y, yr, wv);
#endif
#if FELUCCA_REV_FDN8
    if (t == RT_FDN8)
        return rvf_step(y, yr, wv);
#endif
#if FELUCCA_REV_AIRWIN
    if (t == RT_AIRWIN)
        return rva_step(y, yr, wv);
#endif
    (void)t, (void)y, (void)r, (void)g, (void)lpk, (void)wv;
    *yr = 0;
    return 0;
}
/* one pair: in[0], in[1] -> tank t -> w*[0], w*[1] (added) */
FX_STEP void rev_half_pair(uint32_t t, const int32_t *in, int32_t *wl, int32_t *wr, int32_t r, int32_t g, int32_t lpk,
                           int32_t *wv, const int lng)
{
    uint32_t j = rev_half.j = (rev_half.j - 1u) & 7u, q = j & 3u;
    int32_t y, ol, orr, *e = rev_half.e + j, *l = rev_half.l + j, *rr = rev_half.r + j;
    e[0] = e[8] = in[1];
    rev_half.o[q] = rev_half.o[q + 4u] = in[0];
    y = (rev_hb(e) + (rev_half.o[q + 3u] << 9)) >> 10;
    ol = rev_tank_step(t, y, r, g, lpk, &orr, wv, lng);
    l[0] = l[8] = ol;
    rr[0] = rr[8] = orr;
    wl[0] += rev_hb(l) >> 9;
    wr[0] += rev_hb(rr) >> 9;
    wl[1] += l[3];
    wr[1] += rr[3];
}
/* a block of tank t (fx_buses' run_r); ma, mb: line 0's modulation at both ends */
FX_STEP void rev_half_run(uint32_t t, const int32_t *rev_in, int32_t *wl, int32_t *wr, uint32_t n, int32_t ma,
                          int32_t mb, int32_t g, int32_t lpk, int32_t *wv)
{
    uint32_t i;
    int32_t k = lpk;
#if REV_ROOM_HALF
    if (t == RT_ROOM) {
        k = RM_LONG() ? lpk : room_half_k(lpk);         /* (above the knee room_long made it for this rate) */
    }
#endif
#if FELUCCA_REV_PLATE
    if (t == RT_PLATE) {
        if (song.g[G_RSIZE] != rv.size || song.g[G_RDAMP] != rv.damp)
            FAR(rvp_params)();                          /* (reverb_alt.c: XIP, only when SIZE / DAMP changed) */
        rvp_lfo();
    }
#endif
#if FELUCCA_REV_FDN8
    if (t == RT_FDN8) {
        if (song.g[G_RSIZE] != rv.size || song.g[G_RDAMP] != rv.damp)
            FAR(rvf_params)();
        rvf_lfo();
    }
#endif
#if FELUCCA_REV_AIRWIN
    if (t == RT_AIRWIN && (song.g[G_RSIZE] != rv.size || song.g[G_RDAMP] != rv.damp))
        FAR(rva_params)();
#endif
#if REV_ROOM_HALF
    if (t == RT_ROOM && RM_LONG()) {                    /* (above the knee: unbiased rounding, room_step_long) */
        for (i = 0; i < n; i += 2u)
            rev_half_pair(t, rev_in + i, wl + i, wr + i, ma + (((mb - ma) * (int32_t)i) >> CTL_LOG2), g, k, wv, 1);
        return;
    }
#endif
    for (i = 0; i < n; i += 2u)
        rev_half_pair(t, rev_in + i, wl + i, wr + i, ma + (((mb - ma) * (int32_t)i) >> CTL_LOG2), g, k, wv, 0);
}
FX_STEP void rev_half_skip(uint32_t t, uint32_t n)      /* idle: n output samples, n / 2 in the tank */
{
    n >>= 1;
#if REV_ROOM_HALF
    if (t == RT_ROOM) {
        room_skip(n);
        return;
    }
#endif
#if REV_ALT
    rv.p -= n;
#endif
    (void)t;
}
static void rev_half_clear(void)                        /* its cells to 0 */
{
    uint32_t i;
    for (i = 0; i < 16u; i++)
        rev_half.e[i] = rev_half.l[i] = rev_half.r[i] = rev_half.o[i & 7u] = 0;
}
#define REV_HALF_BUSY() rev_half_any()
#else
#define REV_HALF_BUSY() 0
#endif

#if FELUCCA_SPRING
#include "spring/spring.c"            /* SPRING (from Felucca 1.0) */
#endif

/* one block of tank t (a constant at every call); ma, mb: line 0's modulation at both ends of the block; run: the
 * tank is not idle (fx_buses: run_r), else its indices move on. SPRING keeps its own idle state (spring.c) */
FX_STEP void rev_tank_run(uint32_t t, const int32_t *rev_in, int32_t *wl, int32_t *wr, uint32_t n, int32_t ma,
                          int32_t mb, int32_t g, int32_t lpk, int run, int32_t *wv)
{
#if FELUCCA_SPRING
    if (t == RT_SPRING) {
        spring_run(rev_in, wl, wr, n);
        return;
    }
#endif
#if FELUCCA_REV_ROOM && !FELUCCA_REV_HALF
    if (t == RT_ROOM) {
        uint32_t i;
        if (run && RM_LONG()) {                         /* (above the knee: unbiased rounding, room_step_long) */
            room_run_long(rev_in, wl, wr, n, ma, mb, g, lpk, wv);
        } else if (run) {
            lpk |= 1;                                   /* (32767 - 200 DAMP up to the knee, odd already: said so, */
            for (i = 0; i < n; i++) {                   /*  rev_lp_step's mask folds away as before room_long) */
                int32_t rr;
                wl[i] += rev_step(rev_in[i], ma + (((mb - ma) * (int32_t)i) >> CTL_LOG2), g, lpk, &rr, wv);
                wr[i] += rr;
            }
        } else {
            room_skip(n);
        }
        return;
    }
#endif
#if REV_TANK_HALF
    if (run)
        rev_half_run(t, rev_in, wl, wr, n, ma, mb, g, lpk, wv);
    else
        rev_half_skip(t, n);
#endif
    (void)t, (void)rev_in, (void)wl, (void)wr, (void)n, (void)ma, (void)mb, (void)g, (void)lpk, (void)run, (void)wv;
}

/* the reverb bus for one block (fx_buses): the tank TYPE picks. FELUCCA_REV_PROFILE (a measurement build): a function
 * of its own, so the emulator's FM1_HOT counts it apart; else ROOM alone is inlined (RAM code), the others run from
 * main RAM (RAMTEXT is full) */
#if FELUCCA_REV_PROFILE
#define REV_BUS_FN static HOT __attribute__((noinline))
#elif REV_MULTI || !FELUCCA_REV_ROOM
#define REV_BUS_FN static HOT2 __attribute__((noinline))
#else
#define REV_BUS_FN FX_STEP
#endif
#if REV_MULTI
/* Several built: TYPE (rev_type.c) picks one. A change: the running tank fades out over REV_FADE blocks (~6 ms, as
 * a mute), then every tank is cleared (no old tail left in the shared line) and the new one starts from silence,
 * idle until it is sent something. An idle old tank (nothing in its lines) is switched at once. The tanks' blocks
 * run from main RAM, the change itself from XIP (rare) */
#define REV_FADE 8u
_Static_assert(REV_FADE * CTL == 256u, "the fade's gain: a shift");
static struct {
    uint8_t type, fade;          /* the tank running (RT_*), the fade's blocks left (0: none) */
} rsel = {REV_FIRST, 0};
static HOT2 __attribute__((noinline)) void rev_run(uint32_t t, const int32_t *rev_in, int32_t *wl, int32_t *wr,
                                                   uint32_t n, int32_t ma, int32_t mb, int32_t g, int32_t lpk, int run,
                                                   int32_t *wv)
{
#if FELUCCA_REV_ROOM
    if (t == RT_ROOM) {
        rev_tank_run(RT_ROOM, rev_in, wl, wr, n, ma, mb, g, lpk, run, wv);
        return;
    }
#endif
#if FELUCCA_REV_PLATE
    if (t == RT_PLATE) {
        rev_tank_run(RT_PLATE, rev_in, wl, wr, n, ma, mb, g, lpk, run, wv);
        return;
    }
#endif
#if FELUCCA_REV_FDN8
    if (t == RT_FDN8) {
        rev_tank_run(RT_FDN8, rev_in, wl, wr, n, ma, mb, g, lpk, run, wv);
        return;
    }
#endif
#if FELUCCA_REV_AIRWIN
    if (t == RT_AIRWIN) {
        rev_tank_run(RT_AIRWIN, rev_in, wl, wr, n, ma, mb, g, lpk, run, wv);
        return;
    }
#endif
#if FELUCCA_SPRING
    if (t == RT_SPRING)
        spring_run(rev_in, wl, wr, n);
#endif
}
AINL uint32_t rev_want(void)                            /* TYPE's algorithm (rev_type.c rev_cur, inlined: no call */
{                                                       /* from RAM code to XIP) */
    uint32_t i = (uint32_t)bp_set[BPS_RTYPE];
    return REV_ALGO[i < REV_NLIST ? i : 0u];
}
/* the change done (XIP: rare): every tank to silence, TYPE's from the next block */
static __attribute__((noinline)) void rev_switch(void)
{
    rev_tank_clear();
#if REV_TANK_HALF
    rev_half_clear();
#endif
#if FELUCCA_SPRING
    spring_clear();
#endif
#if REV_ALT
    rv.size = -1;                                       /* (the new tank's gains: its rv_params, next block) */
#endif
    fx.rev_q = FX_Q_MAX;                                /* (idle: every cell 0) */
    rev_seen();                                         /* (TYPE turned: a stand-in no more, rev_type.c) */
    rsel.type = (uint8_t)rev_cur();
}
#endif
REV_BUS_FN void rev_bus(const int32_t *rev_in, int32_t *wl, int32_t *wr, uint32_t n, int32_t ma, int32_t mb,
                        int32_t g, int32_t lpk, int run, int32_t *wv)
{
#if REV_MULTI
    uint32_t t = rsel.type, i, f;
    int32_t tl[CTL], tr[CTL];
    if (rev_want() != t && !rsel.fade) {
        if (!run && t != RT_SPRING) {                   /* (nothing in the old tank: the wet stays 0) */
            FAR(rev_switch)();
            return;
        }
        rsel.fade = REV_FADE;
    }
    if (!rsel.fade) {
        rev_run(t, rev_in, wl, wr, n, ma, mb, g, lpk, run, wv);
        return;
    }
    for (i = 0; i < n; i++)                             /* fading: the old tank's block, its gain going down */
        tl[i] = tr[i] = 0;
    rev_run(t, rev_in, tl, tr, n, ma, mb, g, lpk, run, wv);
    f = rsel.fade;
    for (i = 0; i < n; i++) {
        uint32_t gg = ((f << CTL_LOG2) - i) << 8;       /* 65536 .. 256 over the REV_FADE x CTL samples */
        wl[i] += mulq16(tl[i], gg);
        wr[i] += mulq16(tr[i], gg);
    }
    if (--rsel.fade == 0) {
        FAR(rev_switch)();
        *wv = 0;                                        /* (what it wrote is cleared: the bus idle) */
    }
#else
    rev_tank_run(REV_FIRST, rev_in, wl, wr, n, ma, mb, g, lpk, run, wv);
#endif
}

#if FELUCCA_FX_REVERB
/* The reverb bus' off state, for the CPU guard and for REV RET at 0 (2026-10-12: every build with the reverb; the
 * guard's level is the constant 0 without FELUCCA_CPU_GUARD).
 * The CPU guard's REVERB level (cpuguard.h CG_L_REVERB; docs/CPU-GUARD.md). An overloaded half is late, and a late
 * half clicks: with the reverb on, the tank alone costs about as much as the rest of an idle mix (FDN8 at SIZE 124
 * in the emulator at 96 MHz: 584 -> 1524 us a half), and a tail sounds on with nothing left to shed. From this level
 * on the bus' wet fades out over one half (REV_GFADE blocks, as the TYPE change), then the bus is off: its send not
 * heard, its tank not run; the line is cleared a chunk a block (REV_GCHUNK samples, from XIP: no 16 or 32 KB clear in
 * one half of an overloaded interrupt), then its filters. When the level comes off (the load the reverb saved fits
 * again: cpuguard.c, its gain) the send comes back into a silent tank, its tail building up anew. rev_live: what the
 * bus runs, or would, now (cpuguard.c cg_useful): a REV RET at 0 runs nothing, nothing to shed there.
 * REV RET at 0 (core.h G_RRET): the bus off at once (no fade: its gain is 0 already), the line cleared as the guard's,
 * then off; turned up again, the bus comes back into a silent tank, as after the guard */
#define CG_REV 1
#define REV_GFADE (HALF_FRAMES / CTL)
#define REV_GCHUNK 512u
_Static_assert(REV_GFADE * CTL == 256u, "the guard's fade: a shift");
enum { RG_ON, RG_FADE, RG_CLEAR, RG_OFF };
static struct {
    uint8_t st;                  /* RG_*: on, fading out, off while the line is cleared, off */
    uint8_t fade;                /* the fade's blocks left */
    uint32_t clr;                /* the line cleared up to here (RG_CLEAR) */
} rgd;
static volatile uint8_t rev_live;
static __attribute__((noinline)) void rev_gclear(void)  /* (XIP: a block while RG_CLEAR) */
{
    uint32_t i = rgd.clr, e = i + REV_GCHUNK, len = sizeof rev_line / 2u;
    e = e < len ? e : len;
    for (; i < e; i++)
        rev_line[i] = 0;
    rgd.clr = e;
    if (e < len)
        return;
    rev_cells_clear();
#if REV_TANK_HALF
    rev_half_clear();
#endif
#if FELUCCA_SPRING
    spring_clear();
#endif
    fx.rev_q = FX_Q_MAX;                                /* (idle: every cell 0) */
    rgd.st = RG_OFF;
}
/* a block's start: the state from the level (cpuguard.h) and what the bus would run -> RG_* for this block. Not
 * before the line is clear does the bus come back on */
AINL uint32_t rev_guard(int run)
{
    int z = !song.g[G_RRET];                            /* (REV RET at 0) */
    rev_live = (uint8_t)(run && !z);
    if (CG_LEVEL >= CG_L_REVERB || z) {
        if (rgd.st == RG_ON) {                          /* (an idle tank: every cell 0 already, nothing to clear) */
            rgd.st = !run ? RG_OFF : z ? RG_CLEAR : RG_FADE;
            rgd.fade = REV_GFADE;
            rgd.clr = 0;
        }
    } else if (rgd.st == RG_OFF) {
        rgd.st = RG_ON;
    }
    return rgd.st;
}
/* the reverb's block but the common one (the bus on, REV RET at unity: the tank straight into the wet), from main RAM
 * (HOT2: RAMTEXT is full). rev_pre: the wet so far (the chorus, the delay) into tl / tr and the wet to 0, so that the
 * tank's block (fx_buses, into the wet as ever) is the reverb's alone; rev_post: the guard fading (its gain down over
 * REV_GFADE blocks) and REV RET (Q15) together on it, tl / tr added back; off: no tank, the line cleared a chunk a
 * block. At REV RET unity the fade is the guard's as before, sample for sample */
static HOT2 __attribute__((noinline)) void rev_pre(uint32_t rg, int32_t *tl, int32_t *tr, int32_t *wl, int32_t *wr,
                                                   uint32_t n)
{
    uint32_t i;
    for (i = 0; (rg == RG_ON || rg == RG_FADE) && i < n; i++) {
        tl[i] = wl[i], tr[i] = wr[i];
        wl[i] = wr[i] = 0;
    }
}
static HOT2 __attribute__((noinline)) void rev_post(uint32_t rg, const int32_t *tl, const int32_t *tr, int32_t *wl,
                                                    int32_t *wr, uint32_t n, int32_t *wv)
{
    uint32_t i, f = rg == RG_FADE ? rgd.fade : 0u, rq = (uint32_t)FX_RETQ(song.g[G_RRET]);
    if (rg != RG_ON && rg != RG_FADE) {
        if (rg == RG_CLEAR)
            FAR(rev_gclear)();
        *wv = 0;
        return;
    }
    for (i = 0; i < n; i++) {
        uint32_t gg = f ? (((f << CTL_LOG2) - i) << 8) * rq >> 15 : rq << 1;   /* Q16: the fade 65536 .. 256, x REV RET */
        wl[i] = tl[i] + mulq16(wl[i], gg);
        wr[i] = tr[i] + mulq16(wr[i], gg);
    }
    if (f) {
        if (--rgd.fade == 0) {
            rgd.st = RG_CLEAR;
            rgd.clr = 0;
        }
        *wv = 1;                                        /* (the line holds the tail until it is cleared) */
    }
}
#else
#define CG_REV 0
#endif
/* the returns' part of a block's plan (fx_buses), from main RAM (HOT2: RAMTEXT is full; a call a block): run, the buses
 * with something to run (FXB_C the chorus, FXB_D the delay, FXB_R the reverb) -> those that run: a return at 0 not
 * (core.h G_CRET G_DRET; its line cleared from XIP first, cho_zero / dly_zero), the reverb as rev_guard says (the CPU
 * guard, REV RET at 0); FXB_CG: CHO RET below unity (cho_gain); FXB_RS: the reverb's slow path (rev_pre / rev_post);
 * the reverb's RG_* from bit FXB_RG on */
#define FXB_C 1u
#define FXB_D 2u
_Static_assert(FXB_C == 1u && FXB_D == 2u, "fx_ret_block: fxl_lent's bits (fx_slots.c) are FXB_C FXB_D");
#define FXB_R 4u
#define FXB_CG 8u
#define FXB_RS 16u
#define FXB_RG 8
static HOT2 __attribute__((noinline)) uint32_t fx_ret_block(uint32_t run)
{
#if INS_LIM
    run &= ~(uint32_t)fxl_lent;                        /* a line lent to the inserts (fxi.c, the main loop): its bus not
                                                        * run, its line untouched (no step, no RET-0 clear) until it is
                                                        * handed back (FXB_C FXB_D: fxl_lent's bits 0 1) */
#endif
#if CG_REV
    uint32_t rg = rev_guard((int)(run & FXB_R));
    if (rg != RG_ON && rg != RG_FADE)
        run &= ~FXB_R;                                  /* (off: no tank run, the wet not written by the bus) */
    run |= rg << FXB_RG | (rg != RG_ON || song.g[G_RRET] < 127 ? FXB_RS : 0u);
#endif
    if ((run & FXB_C) && (!song.g[G_CRET] || fxz.cclr)) {
        run &= ~FXB_C;
        FAR(cho_zero)();
    }
    if ((run & FXB_D) && (!song.g[G_DRET] || fxz.dclr)) {
        run &= ~FXB_D;
        FAR(dly_zero)();
    }
    return run | ((run & FXB_C) && song.g[G_CRET] < 127 ? FXB_CG : 0u);
}
/* CHO RET between 0 and unity (fx_buses; core.h G_CRET): the chorus' block (the wet so far, its alone) times its
 * return, Q15 (< 32768: no overflow), from main RAM (HOT2); returns the right's last (the returns' tap). Unity: not
 * called, the chorus as before */
static HOT2 __attribute__((noinline)) int32_t cho_gain(int32_t *wl, int32_t *wr, uint32_t n)
{
    int32_t g = FX_RETQ(song.g[G_CRET]);
    uint32_t i;
    for (i = 0; i < n; i++) {
        wl[i] = (wl[i] * g) >> 15;
        wr[i] = (wr[i] * g) >> 15;
    }
    return wr[n - 1u];
}
#if FELUCCA_UI == 1
/* the mixer's MASTER page (op_mixdraw.c): the FX returns' levels, CHO DLY REV. One sample of each return a block (its
 * last: the chorus' right, the delay's, the reverb's right as what it added to wet_r), the largest |value| kept, never
 * lowered here: the main loop empties it (ui/meters.c meter_tap), as trk.peak. Out of the per-sample loops and out of
 * RAMTEXT (HOT2: main RAM): fx_buses pays a call a block. 32767 = 0 dBFS, as a track's peak */
static volatile int32_t fx_ret_pk[3];
static int32_t ret_cd[CTL];                             /* the wet (right) before the reverb: the chorus and the delay's, to tell the reverb's own */
/* the tap: each return's largest |sample| of the block, as a track's peak (fx_mix_part: the block's largest, post level) */
static HOT2 __attribute__((noinline)) void fx_ret_tap(int32_t cpk, int32_t dpk, const int32_t *wr, uint32_t n)
{
    int32_t v[3], k, a;
    uint32_t i;
    v[0] = cpk, v[1] = dpk, v[2] = 0;
    for (i = 0; i < n; i++) {                           /* (the reverb: what it added to the right wet) */
        a = wr[i] - ret_cd[i];
        a = a < 0 ? -a : a;
        v[2] = a > v[2] ? a : v[2];
    }
    for (k = 0; k < 3; k++)
        if (v[k] > fx_ret_pk[k])
            fx_ret_pk[k] = v[k];
}
#endif
/* The reverb one block late on the second core (FELUCCA_DUAL 2, perf/mots-asm 2026-10-13; DUAL_REV=0: never). While
 * CPU1 is up (dual.c sets rev_late each block it posts a job), fx_buses does not run the reverb's block: it keeps the
 * send and the block's decisions (the LFO's read points, the ROOM's gains, fx_ret_block's plan: the guard, REV RET),
 * and the tank runs them during the next block's render, on whichever core dual.c's split gives it (rev_job), its
 * wet heard then: 32 samples (0.73 ms) later than the dry. The tank's state moves in the same order as inline (the
 * decision of a block, its tank, the next decision), so the wet is the inline wet one block later, sample for
 * sample. With CPU1 down (a DUAL build with 2ND CORE OFF, never up, or put down by a fault or a timeout) the reverb
 * runs inline as before; at the switch (CPU1 going down with a block kept) that block's tank runs on CPU0 and its
 * wet is skipped: the tail jumps one block (32 samples) once, in the block after the fault's own late half. Never in a single-core build: the
 * host's renders (tests/golden.txt) are unchanged */
#ifndef DUAL_REV
#define DUAL_REV 1
#endif
#define REV_LATE (DUAL_RENDER && DUAL_REV && FELUCCA_FX_REVERB)
#if REV_LATE
static struct {
    int32_t in[CTL];             /* the block's send */
    int32_t l[CTL], r[CTL];      /* its wet (rev_job) */
    int32_t ma, mb, g, lpk, wv;  /* the block's read points and ROOM gains; what the tank wrote (fx_q) */
    uint32_t plan;               /* fx_ret_block's plan: FXB_R, FXB_RS, the guard's RG_* */
    uint8_t pend, done;          /* a block kept, not heard yet; its tank run */
} rvj;
static uint8_t rev_late;         /* this block: keep the reverb's block for the next (dual.c: CPU1 has a job) */

/* the reverb's block added into l / r: the tank and the guard's / REV RET's gain on it (rev_pre / rev_post keep what
 * l / r held apart). fx_buses' inline block (on the wet) and the kept block's (rev_job, on zeroes) alike */
static HOT2 __attribute__((noinline)) void rev_block(const int32_t *in, int32_t *l, int32_t *r, int32_t ma,
                                                     int32_t mb, int32_t g, int32_t lpk, uint32_t plan, int32_t *wv)
{
#if CG_REV
    {
        int32_t tl[CTL], tr[CTL];
        uint32_t rg = plan >> FXB_RG;
        int slow = (int)(plan & FXB_RS);
        if (slow)
            rev_pre(rg, tl, tr, l, r, CTL);
        if (rg == RG_ON || rg == RG_FADE)
            rev_bus(in, l, r, CTL, ma, mb, g, lpk, (plan & FXB_R) != 0, wv);
        if (slow)
            rev_post(rg, tl, tr, l, r, CTL, wv);
    }
#else
    rev_bus(in, l, r, CTL, ma, mb, g, lpk, (plan & FXB_R) != 0, wv);
#endif
}
static HOT2 __attribute__((noinline)) void rev_job(void)   /* the kept block's tank (either core: dual.c dual_run) */
{
    uint32_t i;
    for (i = 0; i < CTL; i++)
        rvj.l[i] = rvj.r[i] = 0;
    rvj.wv = 0;
    rev_block(rvj.in, rvj.l, rvj.r, rvj.ma, rvj.mb, rvj.g, rvj.lpk, rvj.plan, &rvj.wv);
    rvj.done = 1;
}
#endif

/* process the three buses for one block; sends in, wet out (stereo). The LFOs (chorus, reverb line)
 * are computed per block and ramped: no sine per sample. Each bus runs in its own loop; an idle one
 * (see above) is skipped. Returns 0 when every bus was idle: wet_l / wet_r were not written (they would
 * be all 0; FELUCCA_SKIP), else 1 */
static HOT int fx_buses(const int32_t *cho_in, const int32_t *dly_in, const int32_t *rev_in, int32_t *wet_l,
                     int32_t *wet_r, uint32_t n)
{
    uint32_t i, dl;
    int32_t fb, col, dmix;                              /* (the delay's: dly_block) */
    int32_t g, lpk;                                     /* the ROOM's loop gain (RT60 0.5 s .. a near-freeze), damping */
    int32_t cdepth = song.g[G_CDEPTH] * 6;
    int32_t ca0, ca1, cb0, cb1, ma, mb, dca, dcb, wc = 0, wd = 0, wv = 0, yr, x;
    int run_c, run_d, run_r;
    uint32_t plan;
#if FELUCCA_UI == 1
    int32_t cpk = 0, dpk = 0;                           /* the chorus', the delay's largest |sample| of the block (the mixer's returns) */
#endif
#if REV_LATE
    int had = rvj.pend;                                 /* a kept block to hear now */
#else
    const int had = 0;
#endif
#if CG_REV && !REV_LATE
    uint32_t rg;
#endif
#if FELUCCA_UI == 1
    yr = x = 0;                                         /* (the returns' tap: a bus idle reads 0) */
#endif
    {   /* the chorus' two read points (Q8 samples back) and line 0's extra length, at both ends of the block */
        int32_t s0 = osc_sine(fx.cho_ph), s1, m0 = osc_sine(fx.rev_ph), m1;
        fx.cho_ph += LFO_INC[song.g[G_CRATE] & 127];
        fx.rev_ph += LFO_INC[30];
        s1 = osc_sine(fx.cho_ph);
        m1 = osc_sine(fx.rev_ph);
        ca0 = (400 << 8) + ((s0 + 32768) * cdepth >> 8), ca1 = (400 << 8) + ((s1 + 32768) * cdepth >> 8);
        cb0 = (400 << 8) + ((32767 - s0) * cdepth >> 8), cb1 = (400 << 8) + ((32767 - s1) * cdepth >> 8);
        ma = (REV_MOD << 8) + ((m0 * REV_MOD) >> 7), mb = (REV_MOD << 8) + ((m1 * REV_MOD) >> 7);
        dca = (ca1 - ca0) >> CTL_LOG2, dcb = (cb1 - cb0) >> CTL_LOG2;
    }
#if FELUCCA_REV_ROOM
    room_gains(&g, &lpk);
#else
    g = lpk = 0;                                        /* (only the ROOM's) */
#endif
#define CHO_R0 (ca0 + dca * (int32_t)i)
#define CHO_R1 (cb0 + dcb * (int32_t)i)
#if REV_LATE
    if (had && !rvj.done)                               /* (CPU1 down before it ran the kept block: here) */
        rev_job();
    if (had)                                            /* (the kept block's tank, before this block's decisions: */
        fx.rev_q = fx_q(fx.rev_q, rvj.wv, n);           /* the order inline has) */
#endif
    /* (the input scan only once the bus' lines are clear) */
    run_c = FELUCCA_FX_CHORUS && (fx.cho_q < CHO_LEN || fx_any(cho_in, n));   /* (registry.h: an FX not built) */
    run_d = FELUCCA_FX_DELAY && (fx.dly_q < DLY_LEN || fx.dly_lp || fx_any(dly_in, n));
    run_r = FELUCCA_FX_REVERB && (fx.rev_q < REV_Q || REV_LP_BUSY() ||
                                  REV_HALF_BUSY() || fx_any(rev_in, n));   /* (REV_HALF: its filters too) */
    plan = fx_ret_block((uint32_t)run_c | (uint32_t)run_d << 1 | (uint32_t)run_r << 2);   /* (the returns, the guard) */
    run_c = (plan & FXB_C) != 0, run_d = (plan & FXB_D) != 0, run_r = (plan & FXB_R) != 0;
#define FX_ALL_IDLE (FELUCCA_SKIP && !FELUCCA_SPRING && !(run_c | run_d | run_r | had))   /* (SPRING: its own state) */
    if (run_c) {
        for (i = 0; i < n; i++) {
            wet_l[i] = cho_step(cho_in[i], CHO_R0, CHO_R1, &yr, &wc);
            wet_r[i] = yr;
        }
        if (plan & FXB_CG)                              /* (CHO RET below unity: the chorus' wet alone so far) */
            yr = cho_gain(wet_l, wet_r, n);
#if FELUCCA_UI == 1
        for (i = 0; i < n; i++) {
            int32_t a = wet_r[i] < 0 ? -wet_r[i] : wet_r[i];
            cpk = a > cpk ? a : cpk;
        }
#endif
    } else {                                            /* idle: every cell holds 0, the output is 0 */
        if (!FX_ALL_IDLE)                               /* (all idle: the wet is not added, below) */
            for (i = 0; i < n; i++)
                wet_l[i] = wet_r[i] = 0;
        fx.cho_w += n;
    }
    if (run_d) {
        dl = FAR(dly_block)(&fb, &col, &dmix);          /* (its divide: only for a running delay) */
        for (i = 0; i < n; i++) {
            x = dly_step(dly_in[i], dl, col, fb, dmix, &wd);
            wet_l[i] += x;
            wet_r[i] += x;
#if FELUCCA_UI == 1
            dpk = (x < 0 ? -x : x) > dpk ? (x < 0 ? -x : x) : dpk;
#endif
        }
    } else {
        fx.dly_w += n;
    }
#if FELUCCA_UI == 1
    if (!FX_ALL_IDLE)
        for (i = 0; i < n; i++)
            ret_cd[i] = wet_r[i];
#endif
#if REV_LATE
    if (rev_late) {                                     /* kept for the next block (rev_job); the last one heard */
        for (i = 0; i < n; i++)
            rvj.in[i] = rev_in[i];
        if (had)
            for (i = 0; i < n; i++) {
                wet_l[i] += rvj.l[i];
                wet_r[i] += rvj.r[i];
            }
        rvj.ma = ma, rvj.mb = mb, rvj.g = g, rvj.lpk = lpk, rvj.plan = plan;
        rvj.pend = 1, rvj.done = 0;
        wv = 0;                                         /* (fx.rev_q: when its tank has run) */
    } else {                                            /* inline (the same code as the kept block's). CPU1 just */
        rvj.pend = 0;                                   /* gone down with a block kept: its tank ran (above), its */
        rev_block(rev_in, wet_l, wet_r, ma, mb, g, lpk, plan, &wv);   /* wet is skipped: the tail jumps a block */
    }
#elif CG_REV
    {   /* the bus on, REV RET at unity: the tank into the wet, as before; else (the guard fading or off, REV RET below
         * unity) rev_pre / rev_post around it */
        int32_t tl[CTL], tr[CTL];
        int slow = (int)(plan & FXB_RS);
        rg = plan >> FXB_RG;
        if (slow)
            rev_pre(rg, tl, tr, wet_l, wet_r, n);
        if (rg == RG_ON || rg == RG_FADE)
            rev_bus(rev_in, wet_l, wet_r, n, ma, mb, g, lpk, run_r, &wv);   /* (the tank TYPE picks) */
        if (slow)
            rev_post(rg, tl, tr, wet_l, wet_r, n, &wv);
    }
#else
    rev_bus(rev_in, wet_l, wet_r, n, ma, mb, g, lpk, run_r, &wv);   /* (the tank TYPE picks) */
#endif
#if FELUCCA_UI == 1
    if (!FX_ALL_IDLE)                                   /* (all idle: wet_r not written, every return 0) */
        fx_ret_tap(cpk, dpk, wet_r, n);
#endif
#undef CHO_R0
#undef CHO_R1
    fx.cho_q = fx_q(fx.cho_q, wc, n);
    fx.dly_q = fx_q(fx.dly_q, wd, n);
#if REV_LATE
    if (!rev_late)                                      /* (kept: counted when its tank runs, above) */
#endif
        fx.rev_q = fx_q(fx.rev_q, wv, n);
    return !FX_ALL_IDLE;
#undef FX_ALL_IDLE
}

/* one block of the whole mix (shared with tests/hostsim.c): events -> each part
 * -> dist -> SLICER -> level / pan / sends -> drums (-> SLICER) -> buses -> master; out: stereo Q15 */
static void events_block(uint32_t n);                    /* seq.c */
static int32_t send_c[CTL], send_d[CTL], send_r[CTL], wet_l[CTL], wet_r[CTL], mix_l[CTL], mix_r[CTL], part_buf[CTL];
#if FELUCCA_BENCH
static void bench_sig(uint32_t k, const int32_t *b, uint32_t n);   /* bench.c */
#define BENCH_SIG(k, b, n) bench_sig(k, b, n)
#else
#define BENCH_SIG(k, b, n) ((void)0)
#endif
#if DUAL_RENDER
/* dual core (dual.c): each core mixes its parts into its own accumulators; MX(x) names them */
typedef struct { int32_t send_c[CTL], send_d[CTL], send_r[CTL], mix_l[CTL], mix_r[CTL], part_buf[CTL]; } mixacc_t;
#define MX(x) (A->x)
#define MIXACC_PARAM , mixacc_t *A
#else
#define MX(x) x
#define MIXACC_PARAM
#endif

/* ---- mute / solo: a track that goes silent fades out over ~6 ms (and back in) */
#define MUTE_STEP 4096                                  /* Q15 per block: 8 blocks */
static HOT int32_t gain_next(track_t *t)                    /* the track's mute gain at the end of this block */
{
    int32_t to = trk_silent(t) ? 32767 : 0, a = t->att;
    t->att = a < to ? (to - a > MUTE_STEP ? a + MUTE_STEP : to) : (a - to > MUTE_STEP ? a - MUTE_STEP : to);
    return 32767 - t->att;
}

/* ---- DUCK: every kick (the drum track's KICK / KICK 2) dips the synth parts, which come back over an
 * eighth note: depth G_DUCK, the curve (1 - t / T)^2. drum_on sets drums.kick. */
static struct {
    uint32_t t;                                         /* units since the kick */
    int32_t g0, g1;                                     /* the parts' gain at the block start, end (Q15) */
} duck = {0xFFFFFFFFu, 32767, 32767};

static HOT void duck_block(uint32_t adv)
{
    int32_t depth = song.g[G_DUCK] * 258, x;
    uint32_t len = BEAT_U / 2u;
    duck.g0 = duck.g1;
    if (drums.kick) {
        drums.kick = 0;
        duck.t = 0;
    }
    if (!depth || duck.t >= len) {
        duck.g1 = 32767;
        return;
    }
    x = 32767 - (int32_t)((duck.t << 10) / (len >> 5));      /* 1 - t / T, Q15 (t < len < 2^21) */
    if (x < 0)
        x = 0;
    duck.g1 = 32767 - mulq15(depth, mulq15(x, x));
    duck.t = duck.t + adv < duck.t ? 0xFFFFFFFFu : duck.t + adv;
}

#if FELUCCA_GLIDE
/* Glides (FELUCCA_GLIDE, after X0X 0.10.1-beta by Charles Vestal, charlesvestal/fm1-x0x 49b1fc8, GPL-3.0: "part
 * volume, pan and sends, the master volume and every drum voice's pan glide over about 10 ms instead of jumping (no
 * zippering)"). X0X glides each gain per sample in float; here, in fixed point, a gain moves once a block by a one-pole
 * step toward its target (GLIDE_K: a time constant of ~10 ms at CTL frames a block) and is ramped linearly across
 * the block, so a knob's steps (a LEVEL or PAN detent, a send, MASTER) blend into one move instead of a step every
 * block. Settled, the gain is its target exactly and the sound is as before, sample for sample. A part that stops
 * sounding settles its gains at once (as X0X's silent kit). */
/* (GLIDE_K, glide_next: dsp.c) */
#endif

#if TFLT_ON
/* ---- the track FILTER (SLOOP 2.4, isod89/sloop-fm1 v2.4 8d3823f fx.c djf_block / tflt_run, GPL-3.0-only; P_TFLT,
 * FX > FILTER, FX + KNOB 4): the master DJ filter's (djf_process) on each track, v < 0 a low-pass closing, > 0 a
 * high-pass opening, 0 off: the cutoff glides to the knob (no zipper), back at 0 it opens fully, then the filter is
 * bypassed (one compare a block). A part: its mono signal after the SLICER, before LEVEL / PAN / the sends (as 2.4).
 * The drum track: unlike 2.4 (its one reverb send), the bus as the lanes sum into it, L, R and all three sends
 * (drum_sends.c: each lane its own REV / DLY / CHO), so a closed filter darkens the drums' echoes too. The FX bypass
 * (P_FXOFF) opens it (it glides open, then off). */
typedef struct {
    int32_t cut;                                        /* now, 0..127 << 8 (CUTOFF_HZ index) */
    int8_t mode;                                        /* -1 LP, 1 HP, 0 off */
    uint8_t seed;                                       /* bit per state: the LP starts from its next input (tflt_run) */
    int32_t z[5][2];                                    /* the SVF states: a part's [0]; the drums' L R REV DLY CHO */
} tflt_t;
static tflt_t tflt[NTRK + 2];                           /* (the tracks', then the master's: its DJ filter, fx-rework; the USB
                                                         * input's, stage 3) */

/* the SVF states to zero: a loop, not memset (memset is XIP code and this runs from RAM: build.py refuses the call) */
static HOT void tflt_clear(tflt_t *f)
{
    uint32_t i;
    for (i = 0; i < 5; i++)
        f->z[i][0] = f->z[i][1] = 0;
}

/* the knob v -> this block's coefficients in *c; 0 = bypassed (nothing to do) */
static HOT int tflt_block(tflt_t *f, int32_t v, tsvf_t *c)
{
    int32_t to;
    if (v < 0 && f->mode >= 0) {                        /* (switching side: from open) */
        f->mode = -1;
        f->cut = 127 << 8;
        tflt_clear(f);
        f->seed = 31;                                   /* (SLOOP 2.6: from rest the LP's output began at 0 under a
                                                         * sounding note, a click; it starts where the signal is) */
    } else if (v > 0 && f->mode <= 0) {
        f->mode = 1;
        f->cut = 0;
        tflt_clear(f);
        f->seed = 0;                                    /* (the HP from 0: its output is in - LP, 0 from rest) */
    }
    if (!f->mode)
        return 0;
    to = f->mode < 0 ? (v < 0 ? (127 << 8) + v * 90 * 4 : 127 << 8) : (v > 0 ? v * 90 * 4 : 0);
    f->cut += clamp(to - f->cut, -384, 384);            /* ~1.5 index a block */
    if (!v && f->cut == to) {
        f->mode = 0;                                    /* fully open again: off */
        return 0;
    }
    tsvf_coef(c, f->cut, 40);
    return 1;
}
/* n samples of b through state ch, at 2^sh below their level (the SVF's range: +-140000) */
static HOT void tflt_run(tflt_t *f, const tsvf_t *c, int32_t *b, uint32_t n, uint32_t ch, uint32_t sh)
{
    uint32_t i;
    int32_t *z = f->z[ch], lp = f->mode < 0;
    if ((f->seed >> ch) & 1u) {                         /* (SLOOP 2.6 d30d655 fx.c djf_run, GPL-3.0-only) the LP's first */
        f->seed &= (uint8_t)~(1u << ch);                /* block: at rest on its input, the output is the input (DC: */
        if (lp && n) {                                  /* exact), not 0 */
            z[0] = 0;
            z[1] = clamp(b[0] >> sh, -140000, 140000);
        }
    }
    for (i = 0; i < n; i++) {
        int32_t x = clamp(b[i] >> sh, -140000, 140000), y = tsvf_lp(c, x, &z[0], &z[1]);
        b[i] = (lp ? y : x - y) << sh;
    }
}
/* a part's (b: before its level, up to +-884000: 3 bits down) at amount v (0: it opens, then off) */
static HOT2 __attribute__((noinline)) void tflt_part(const track_t *t, int32_t v, int32_t *b, uint32_t n)
{
    tflt_t *f = &tflt[(uint32_t)(t - trk) % NTRK];
    tsvf_t c;
    if (tflt_block(f, v, &c))
        tflt_run(f, &c, b, n, 0, 3);
}
#endif

#if FELUCCA_MASTER_COMP
/* ---- the COMP insert (fx_slots.c, an FX slot type; the master COMP's mc_run, master_comp.c): a track's amount 0..127
 * is its threshold, -1 .. -30 dB under full scale, with make-up of half the static reduction at full scale; RATIO ATK
 * REL are the track's own (D6: fx_slots.c ins_cset, FX > CMP, the master COMP's lists; the drum sounds' the drum
 * track's). Pre-fader, in the track's insert slots (ins_part, dbus_run). At 0 it lets go (the reduction falls back),
 * then costs one compare a block */
static mc_t tcomp[NPART] = {{0, 0, 8192, 0}, {0, 0, 8192, 0}, {0, 0, 8192, 0}};   /* (the parts'; the drum bus's:
                                                        * dbus_comp below) */
static int32_t tcomp_sink[CTL];                         /* (a mono insert: mc_run's right side, written, never read) */
/* the settings of a COMP insert at amount amt (1..127), track k's RATIO ATK REL */
AINL void tcomp_settings(mc_set_t *s, int32_t amt, uint32_t k)
{
    const int16_t *cs = ins_cset[k % NTRK];
    int32_t thr = amt > 0 ? -1 - (amt - 1) * 29 / 126 : 0;
    mc_settings(s, thr, cs[0], cs[1], cs[2], cs[3] > 0 ? cs[3] - 1 : (-thr * MC_SLOPE[cs[0] & 7]) >> 15);   /* (GAIN 0: AUTO) */
}
AINL int tcomp_rest(const mc_t *c) { return !c->gr16 && c->g13 == 8192; }
/* DYN's limiter on a track (fx-rework, 2026-10-14: the compressor and the limiter one effect): track k's CEIL (its own,
 * fx_slots.c ins_cset[k][4]; OFF by default) after its compressor and gain, the master's lookahead brickwall at half
 * its lookahead (master_comp.c tlim_block) on the track's block (a part: mono; the drum bus: L / R). Off at amount 0 with
 * the rest of DYN. 0.73 ms of delay while it is on */
static tlim_t tlim[NTRK];
static int32_t tlim_dr[TLIM_N];                         /* (the drum bus's right line) */
#define TLIM_DR(k) ((k) % NTRK == TRK_DRUM ? tlim_dr : (int32_t *)0)
static HOT2 __attribute__((noinline)) void tcomp_run(mc_t *c, int32_t amt, int32_t *l, int32_t *r, uint32_t n, uint32_t k)
{
    mc_set_t s;
    tlim_set(&tlim[k % NTRK], TLIM_DR(k), amt ? ins_cset[k % NTRK][4] : 0);
    if (!amt && tcomp_rest(c))
        return;                                         /* (off and at rest) */
    tcomp_settings(&s, amt, k);
    mc_run(c, &s, l, r, l, r != l ? r : tcomp_sink, n);   /* (mono: the key both sides, the gain once) */
    if (tlim[k % NTRK].on)
        tlim_block(&tlim[k % NTRK], 0, l, 0, n);         /* (a part: mono) */
}

/* a part not heard this block (silent, or MUTE / SOLO): its insert is not on the mix, yet its state must not freeze
 * (the next note would start under the last one's reduction). In a slot it lets go on the cleared buffer (silence: the
 * key 0) until the reduction is gone; at 0 or in no slot, or once let go, it rests (nothing is heard: no step) */
static HOT2 __attribute__((noinline)) void tcomp_quiet(track_t *t, int32_t *b, uint32_t n)
{
    uint32_t k = (uint32_t)(t - trk) % NPART;
    mc_t *c = &tcomp[k];
    int32_t amt = fx_on(t) && INS_ON(k, FXT_COMP) ? t->p[P_TCOMP] : 0;
    if (amt && (c->gr16 | c->slow16)) {
        tcomp_run(c, amt, b, b, n, k);
        if (c->gr16 | c->slow16)
            return;
    }
    c->gr16 = c->slow16 = 0;
    c->g13 = 8192;
    tlim[k].on = 0;                                     /* (its lookahead starts empty with the next note) */
}
#if FELUCCA_UI == 1
/* the Optimist UI's mixer rows (ui/optimist/op_mixdraw.c): part k's COMP insert's gain reduction now, dB x 4 (as
 * master_comp.c mc_take_gr scales it); read only, one word, no lock (a block's value either side is as good) */
static uint32_t tcomp_gr_q4(uint32_t k)
{
    int32_t c = (tcomp[k % NPART].gr16 * 3083) >> 23;
    return (uint32_t)(c < 0 ? 0 : c > 127 ? 127 : c);
}
#endif
#endif

#if FELUCCA_UI == 1
/* The Optimist UI's SCOPE (ui/optimist/op_scope.c): the visualiser's ring below, one source at a time. scope_src, set
 * by the main loop only: 0 the master (the mix, as the visualiser takes it), 1..3 part 1..3's block after its inserts
 * (DIST, SLICER, FILTER, COMP; before its level and pan: mix_part), 4 the drum track (the mix after the drums less the
 * mix before them: mix_block). The ISR reads the byte once a part and a block; one ring, never five */
static volatile uint8_t scope_src;
static __attribute__((noinline)) void vis_tap_block(const int32_t *l, const int32_t *r, uint32_t n);
#define SCOPE_DR 4u
#endif

/* ---- DUST (an insert type since 2026-10-14, fx-rework: was the master's own): an old sampler and a record. Its amount
 * 0..127 turns up together: drive into a soft clip, a lower sample rate (held samples, 44.1 -> 11 kHz), fewer bits
 * (15 -> 8), a one-pole low-pass (open -> ~3 kHz), a little hiss and crackle. The hiss and the crackle are the record
 * turning: they fade in with PLAY and out (~0.1 s) at STOP, so a stopped SLOOP is silent. Stereo (r: 0, a part's mono
 * block: the left side's alone), ~25 ops a sample. The master's (mst_dust) is the master DUST of before, sample for
 * sample */
typedef struct {
    int32_t hl, hr, hn;                                 /* held samples, samples left to hold */
    int32_t ll, lr;                                     /* low-pass states */
    int32_t rnd, click;                                 /* noise state, a crackle decaying */
    int32_t bed;                                        /* hiss / crackle level, Q15: 0 stopped, 32767 playing */
} dust_t;
static dust_t mst_dust = {0, 0, 0, 0, 0, 0x2545F491, 0, 0};
#if FELUCCA_FX_DUST
static dust_t tdust[NTRK] = {{0, 0, 0, 0, 0, 0x2545F491, 0, 0}, {0, 0, 0, 0, 0, 0x1F123BB5, 0, 0},
                             {0, 0, 0, 0, 0, 0x369BF4C1, 0, 0}, {0, 0, 0, 0, 0, 0x5E2D58D9, 0, 0}};
#endif
static HOT2 __attribute__((noinline)) void dust_run(dust_t *s, int32_t d, int32_t *l, int32_t *r, uint32_t n)
{
    int32_t hold, shift, a, drive, hiss, i, bed0, bed1;
    uint32_t pc;
    if (!d) {
        s->bed = 0;
        return;
    }
    hold = 1 + d * 3 / 127;
    shift = d / 18;
    a = 32767 - d * 165;                                /* one-pole coefficient, Q15 */
    drive = 4096 + d * 24;                              /* Q12: 1x .. 1.75x */
    hiss = d * 2;
    pc = (uint32_t)d * 7u;                              /* crackle: chance per sample, x 2^-22 */
    bed0 = s->bed;                                      /* ~0.1 s from 0 to full (238 a block of 32) */
    bed1 = s->bed = clamp(s->bed + (song.playing ? 238 : -238), 0, 32767);
    for (i = 0; i < (int32_t)n; i++) {
        int32_t x = l[i], y = r ? r[i] : 0;
        int32_t bed = bed0 + (((bed1 - bed0) * i) >> CTL_LOG2);
        uint32_t nz = noise32(&s->rnd);
        if (--s->hn <= 0) {                             /* sample and hold, then the bits */
            s->hn = hold;
            s->hl = softclip(((x >> 2) * drive) >> 10);
            s->hr = r ? softclip(((y >> 2) * drive) >> 10) : 0;
            if (shift) {
                s->hl = crush_tz(s->hl, shift);         /* fewer bits, toward 0: no DC from tails */
                s->hr = crush_tz(s->hr, shift);
            }
        }
        s->ll += mulq15(s->hl - s->ll, a);
        if (r)
            s->lr += mulq15(s->hr - s->lr, a);
        if ((nz >> 10) < pc)                            /* a speck of dust */
            s->click = mulq15(((int32_t)(nz & 0x3FFu) - 512) * d / 8, bed);
        x = s->ll + s->click + mulq15(((int32_t)(nz >> 16) - 32768) * hiss >> 15, bed);
        y = s->lr + s->click + mulq15(((int32_t)(nz & 0xFFFFu) - 32768) * hiss >> 15, bed);
        s->click -= s->click >> 2;
        l[i] = x;
        if (r)
            r[i] = y;
    }
}

#include "insfx/insfx.c"            /* the further insert types FOLD FUZZ PHSR FLNG FRMT WIDE CHRS ECHO (ins_one, dbus_ins) */
#include "insfx/fxi.c"              /* the limited types' instances (CHRS ECHO): the main loop hands them out, fxi_poll */
#if DINS
/* ---- each drum sound's two insert slots (fx-rework 2026-10-14; FX slots phase 5 had DIST and COMP fixed): per drum
 * voice (NDRUM states a slot, not per lane: two hits of a sound may overlap), the types and amounts of the voice's lane
 * (fx_slots.c lin_slot, drum_sends.c dins_amt), S1 then S2, each state restarted when the voice is triggered
 * (dins_restart, drums.c drum_on). A voice whose lane has a slot with an amount and the drums' FX on (or whose state still
 * lets go: DYN's reduction, a fading insert) skips its own mix in drums_mix: its block (dsend_buf, the samples after its
 * level, its lane's CUT and DECAY) runs its slots here, then its pan, mix and sends, as drums_mix would have mixed it. So a
 * DYN's threshold is against the level heard (G_DRLVL, the velocity). DYN on a voice: its compressor, the drum track's
 * RATIO ATK REL, no CEIL. The X0X kits' channels mix apart (drum_x0x.c): no per-sound insert there */
typedef union {                                         /* a voice's slot: the state of the type it holds */
    dist_t d;
#if FELUCCA_MASTER_COMP
    mc_t c;
#endif
#if TFLT_ON
    tflt_t f;
#endif
    dust_t u;
#if FELUCCA_INS_FOLD || FELUCCA_INS_FUZZ
    ifx_drv_t v;
#endif
#if FELUCCA_INS_PHASER
    ifx_ph_t p;
#endif
#if FELUCCA_INS_FORMANT
    ifx_fm_t m;
#endif
#if FELUCCA_INS_EQ3
    eq_st_t e;
#endif
    int32_t w;
} dins_st_t;
static dins_st_t dins_st[NDRUM][INS_N];
static uint8_t dins_ty[NDRUM][INS_N];                    /* the type each state holds (its restart, the guard) */
static uint32_t dins_ran[NDRUM];                         /* the types (a bit each) that ran on each voice in the last block
                                                         * (cpuguard.c), and this block's */
static uint32_t dins_now[NDRUM];
/* voice k's slot s at rest for type t (as a new voice: a hit starts clean), wet at once at amount a (no fade in) */
static __attribute__((noinline)) void dins_rest(uint32_t k, uint32_t s, uint32_t t, int32_t a)
{
    dins_st_t *st = &dins_st[k % NDRUM][s & 1u];
    uint32_t i;
    for (i = 0; i < sizeof *st / 4u; i++)
        ((int32_t *)st)[i] = 0;
    dins_ty[k % NDRUM][s & 1u] = (uint8_t)t;
#if FELUCCA_MASTER_COMP
    if (t == FXT_DYN)
        st->c.g13 = 8192;
#endif
    if (t == FXT_DUST)
        st->u.rnd = 0x2545F491 ^ (int32_t)(k * 2654435761u + s);
#if FELUCCA_INS_FOLD || FELUCCA_INS_FUZZ
    if ((t == FXT_FOLD || t == FXT_FUZZ) && a)
        st->v.w = 32768, st->v.a = (uint8_t)a;
#endif
#if FELUCCA_INS_PHASER
    if (t == FXT_PHSR && a)
        st->p.w = (a > 64 ? 64 : a) * 512, st->p.a = (uint8_t)a;
#endif
#if FELUCCA_INS_FORMANT
    if (t == FXT_FRMT && a)
        st->m.w = 32768, st->m.a = (uint8_t)a;
#endif
#if FELUCCA_INS_EQ3
    if (t == FXT_EQ3 && a && !eq3_flat(eq3_v[TRK_DRUM]))
        eq3_engage(&st->e, eq3_v[TRK_DRUM]);         /* (a hit from silence: engaged at once, the drum track's bands) */
#endif
    (void)a;
}
static void dins_restart(uint32_t k)                    /* voice k triggered: its lane's slots from rest */
{
    uint32_t l = lane_of_note_i(drums.v[k % NDRUM].note), s;
    for (s = 0; s < INS_N; s++)
        dins_rest(k, s, l < DRUM_LANES ? lin_slot[l][s] : (uint32_t)FXT_NONE, l < DRUM_LANES ? lin_amt(l, s) : 0);
}
/* voice k's slot s still letting go at amount 0 (DYN's reduction, an insert's wet share fading) */
AINL int dins_busy(uint32_t k, uint32_t s)
{
    const dins_st_t *st = &dins_st[k][s];
    switch (dins_ty[k][s]) {
#if FELUCCA_MASTER_COMP
    case FXT_DYN: return !tcomp_rest(&st->c);
#endif
#if TFLT_ON
    case FXT_FILT: return st->f.mode != 0;
#endif
#if FELUCCA_INS_FOLD || FELUCCA_INS_FUZZ
    case FXT_FOLD: case FXT_FUZZ: return st->v.w != 0;
#endif
#if FELUCCA_INS_PHASER
    case FXT_PHSR: return st->p.w != 0;
#endif
#if FELUCCA_INS_FORMANT
    case FXT_FRMT: return st->m.w != 0;
#endif
#if FELUCCA_INS_EQ3
    case FXT_EQ3: return st->e.w != 0;
#endif
    default: return 0;
    }
    (void)st;
}
/* voice k playing note: do its inserts run this block? (on: the drums' FX) */
#if defined(FELUCCA_DUALCFG) && FELUCCA_DUALCFG && FELUCCA_DUAL
#define FX_INS_BYPASS fm1_ins_bypass                    /* (the second-core test image: dualcfg ins off, hal/fm1_dual.h) */
#else
#define FX_INS_BYPASS 0
#endif
static HOT2 __attribute__((noinline)) uint32_t dins_of(uint32_t k, uint32_t note, int32_t on)
{
    on = on && !FX_INS_BYPASS;                          /* (dualcfg ins off: the lanes' inserts fade to rest) */
    uint32_t l = note == 76u || note == 77u ? DRUM_LANES : lane_of_note_i(note), a = 0, s;
    for (s = 0; s < INS_N; s++) {
        if (l < DRUM_LANES && dins_ty[k][s] != lin_slot[l][s])
            FAR(dins_rest)(k, s, lin_slot[l][s], 0);    /* (the slot's type changed under the voice: from rest; XIP) */
        if (on && l < DRUM_LANES && (LIN_OK >> lin_slot[l][s] & 1u))
            a |= (uint32_t)(lin_amt(l, s) != 0);      /* (always heard: no master slot needed) */
        a |= (uint32_t)dins_busy(k, s);
    }
    return a;
}
/* type t at amount a on voice k's slot s, n samples of b */
static HOT2 __attribute__((noinline)) void dins_one(uint32_t k, uint32_t s, uint32_t t, int32_t a, int32_t *b, uint32_t n)
{
    dins_st_t *st = &dins_st[k][s];
    if (!a && !dins_busy(k, s))
        return;
    ASTAGE0(0xF0 + ((s & 1u) << 3) + (t & 7u));        /* (a drum lane's insert: slot s, type t's low bits; core.h) */
    dins_now[k] |= FXT_BIT(t);
    switch (t) {
#if FELUCCA_FX_DIST
    case FXT_DIST: dist_run(&st->d, a, b, n); break;
#endif
#if FELUCCA_MASTER_COMP
    case FXT_DYN: {
        mc_set_t c;
        tcomp_settings(&c, a, TRK_DRUM);
        mc_run(&st->c, &c, b, b, b, tcomp_sink, n);
        break;
    }
#endif
#if TFLT_ON
    case FXT_FILT: {
        tsvf_t c;
        if (tflt_block(&st->f, a, &c))
            tflt_run(&st->f, &c, b, n, 0, 2);
        break;
    }
#endif
#if FELUCCA_FX_DUST
    case FXT_DUST: dust_run(&st->u, a, b, 0, n); break;
#endif
#if FELUCCA_INS_FOLD
    case FXT_FOLD: ifx_drive(&st->v, 0, a, b, n); break;
#endif
#if FELUCCA_INS_FUZZ
    case FXT_FUZZ: ifx_drive(&st->v, 1, a, b, n); break;
#endif
#if FELUCCA_INS_PHASER
    case FXT_PHSR: ifx_phaser(&st->p, a, b, n, 3); break;
#endif
#if FELUCCA_INS_FORMANT
    case FXT_FRMT: ifx_formant(&st->m, a, b, n, 3); break;
#endif
#if FELUCCA_INS_EQ3
    case FXT_EQ3: (void)eq3_run(&st->e, eq3_v[TRK_DRUM], a, b, 0, n); break;   /* (the drum track's bands) */
#endif
    default: break;
    }
    (void)st, (void)b, (void)n;
}
static HOT2 __attribute__((noinline)) void dins_post(uint32_t k, uint32_t note, uint32_t i0, uint32_t i1, uint32_t n,
                                                    const dins_mix_t *x)
{
    uint32_t l = note == 76u || note == 77u ? DRUM_LANES : lane_of_note_i(note), i, s;
    int32_t *b = dsend_buf;
    for (i = 0; i < i0; i++)
        b[i] = 0;                                       /* (the block outside the voice: silence) */
    for (i = i1; i < n; i++)
        b[i] = 0;
    for (s = 0; s < INS_N; s++)                         /* S1, then S2 */
        dins_one(k, s, dins_ty[k][s], x->on && l < DRUM_LANES && (LIN_OK >> lin_slot[l][s] & 1u) ? lin_amt(l, s) : 0, b, n);
#if FELUCCA_USB_AUDIO
    dlt_add(l, b, 0, n);                                /* its lane's USB tap after the inserts (ua_tap.h) */
#endif
    for (i = 0; i < n; i++) {
        int32_t y = b[i], ys = clamp(y, -65535, 65535);   /* (the sends: mulq15 fits) */
        if (x->mono) {
            x->mono[i] += y;
            if (x->pre && x->r)
                send_r[i] += mulq15(ys, x->r);
        } else {
            x->ml[i] += (y * (x->gl0 + ((x->gld * (int32_t)i) >> CTL_LOG2))) >> 12;
            x->mr[i] += (y * (x->gr0 + ((x->grd * (int32_t)i) >> CTL_LOG2))) >> 12;
#if FELUCCA_USB_AUDIO
            track_capture[i * NTRK + TRK_DRUM] += y;
#endif
            if (x->r)
                x->rev[i] += mulq15(ys, x->r);
        }
        if (x->d)
            send_d[i] += mulq15(ys, x->d);
        if (x->c)
            send_c[i] += mulq15(ys, x->c);
    }
}
#if FELUCCA_MASTER_COMP
/* the meters (meters.c): voice k's DYN reduction (its slot holding DYN; 0 none) */
static int32_t dins_gr16(uint32_t k)
{
    uint32_t s;
    for (s = 0; s < INS_N; s++)
        if (dins_ty[k % NDRUM][s] == FXT_DYN)
            return dins_st[k % NDRUM][s].c.gr16;
    return 0;
}
#endif
#endif

/* the inserts a track's block calls while in none of its slots, at 0 (they let go, then rest): DIST FILTER COMP (their
 * own rest tests), the further types only while not at rest (insfx.c ifx_awake: built and in no slot, nothing a block) */
#if INS_FX
#define INS_IDLE(k) ((uint32_t)ifx_awake[(k) % NTRK])
#else
#define INS_IDLE(k) 0u
#endif
#define INS_OUT(k, s0, s1) (FXT_BUILT & FXT_INSERT & ((FXT_BIT(FXT_DIST) | FXT_BIT(FXT_FILT) | FXT_BIT(FXT_COMP) | FXT_BIT(FXT_DUST)) | \
                                                     INS_IDLE(k)) & ~(FXT_BIT(s0) | FXT_BIT(s1)))

/* ---- the drum bus (FX slots, design phase 4; the track insert slots, 2026-10-12): the drum track's own amounts, kept in
 * its P_DIST P_TFLT P_TCOMP (inserts) and P_CHOR P_DLY P_REV (its bus sends; the FX record carries them: fx_slots.c;
 * project_t's slots stay 0), each heard while the drum track's FX are on (P_FXOFF) and, an insert, while one of its two
 * slots holds it (fx_slots.c ins_slot). mix_block renders the drums first, alone on the accumulators, then the SLICER,
 * then here:
 *   S1, S2       the drum track's inserts in its slots' order (DIST, FILTER, COMP): on the bus, L, R and the three
 *                send accumulators the drum sounds filled (D7). DIST a state each (a send's only while it carries a
 *                signal, and one block after), FILTER an SVF state each, COMP one detector on L / R (stereo-linked)
 *                whose gain every channel takes, so the echoes duck with the drums. At the guard's QUALITY level they
 *                run on L / R only (the echoes take the bus before them: cpuguard.h, D9)
 *   CHO DLY REV  the drum track's bus sends, taken from the bus after its inserts (its mid, (L + R) / 2: a centred
 *                sound sends as a drum sound's own send at the same level), on top of each sound's own (D5); ramped
 *                over a block when the knob moves
 * At 0 each costs a compare a block; nothing here touches a sample then (the mix of before, sample for sample) */
static int32_t *const DBUS_CH[5] = {mix_l, mix_r, send_r, send_d, send_c};
#define DBUS_NC() (CG_LEVEL >= CG_L_QUALITY ? 2u : 5u)   /* the channels the inserts run on */
#if FELUCCA_FX_DIST
static dist_t dbus_dist[5];
static uint8_t dbus_live, dbus_don;                     /* the channels that carried a signal last block; DIST ran */
static HOT2 __attribute__((noinline)) void dbus_dist_run(int32_t d, uint32_t n, uint32_t nc)
{
    uint32_t c, i, live = 0;
    for (c = 0; c < nc; c++) {
        int32_t *b = DBUS_CH[c], any = 0;
        for (i = 0; i < n; i++)
            any |= b[i];
        live |= any ? 1u << c : 0u;
        if (any || (dbus_live >> c & 1u))
            dist_run(&dbus_dist[c], d, b, n);
        else
            dbus_dist[c].env = 0;                       /* (silent: it restarts with the next signal) */
    }
    for (; c < 5u; c++)
        dbus_dist[c].env = 0;                           /* (QUALITY: the sends' states restart when they come back) */
    dbus_live = (uint8_t)live;
}
#endif
#if FELUCCA_MASTER_COMP
static mc_t dbus_comp = {0, 0, 8192, 0};
static int32_t dbus_g[CTL];                             /* the detector's gain over the block, Q13 */
static HOT2 __attribute__((noinline)) void dbus_comp_run(int32_t amt, uint32_t n, uint32_t nc)
{
    mc_set_t s;
    uint32_t c, i;
    for (i = 0; i < n; i++)
        dbus_g[i] = 8192;                               /* (mc_mul13(8192, g) = g: mc_run writes the gain itself) */
    tcomp_settings(&s, amt, TRK_DRUM);
    mc_run(&dbus_comp, &s, mix_l, mix_r, dbus_g, tcomp_sink, n);
    for (c = 0; c < nc; c++) {
        int32_t *b = DBUS_CH[c];
        for (i = 0; i < n; i++)
            b[i] = mc_mul13(b[i], dbus_g[i]);           /* (as mc_run applies it: unity is exact) */
    }
}
static HOT2 __attribute__((noinline)) void dbus_dyn(int32_t a, uint32_t n, uint32_t nc)   /* DYN: COMP, then CEIL on L / R */
{
    tlim_set(&tlim[TRK_DRUM], tlim_dr, a ? ins_cset[TRK_DRUM][4] : 0);
    if (a || !tcomp_rest(&dbus_comp))                   /* (at 0 it lets go first) */
        dbus_comp_run(a, n, nc);
    if (tlim[TRK_DRUM].on)
        tlim_block(&tlim[TRK_DRUM], tlim_dr, mix_l, mix_r, n);
}
#endif
/* insert type t on the drum bus; live: in a slot and FX on (else at 0: DIST restarts, FILTER opens, COMP lets go) */
static HOT2 __attribute__((noinline)) void dbus_ins(uint32_t t, int32_t live, uint32_t n, uint32_t nc)
{
    const int16_t *p = TDRUM->p;
#if FELUCCA_FX_DIST
    if (t == FXT_DIST) {
        int32_t d = live ? p[P_DIST] : 0;
        if (d) {
            dbus_dist_run(d, n, nc);
            dbus_don = 1;
        } else if (dbus_don) {                          /* (off: every state restarts when it comes back) */
            uint32_t c;
            for (c = 0; c < 5u; c++)
                dbus_dist[c].env = 0;
            dbus_don = 0, dbus_live = 0;
        }
    }
#endif
#if TFLT_ON
    if (t == FXT_FILT) {                                /* the drum track's FILTER (2.4's): the bus holds the drums alone */
        tflt_t *f = &tflt[TRK_DRUM];
        tsvf_t c;
        uint32_t ch;
        if (tflt_block(f, live ? p[P_TFLT] : 0, &c))
            for (ch = 0; ch < nc; ch++)
                tflt_run(f, &c, DBUS_CH[ch], n, ch, 2);
    }
#endif
#if FELUCCA_MASTER_COMP
    if (t == FXT_COMP)
        dbus_dyn(live ? p[P_TCOMP] : 0, n, nc);
#endif
#if INS_FX
    if (IFX_TYPE(t) && FXT_AMT[t] != 0xFFu)   /* (insfx.c: L and R; at 0 it fades
                                                        * out, then rests; JUNO CHORUS: fxi_kern.c) */
        ifx_bus(t, live ? p[FXT_AMT[t]] : 0, mix_l, mix_r, n);
#endif
#if FELUCCA_FX_DUST
    if (t == FXT_DUST)                                  /* (the bus's L / R; at 0 its bed falls to 0) */
        dust_run(&tdust[TRK_DRUM], live ? p[P_TDUST] : 0, mix_l, mix_r, n);
#endif
    (void)p, (void)live, (void)n, (void)nc;
}
static int32_t dbus_snd[3];                             /* the sends CHO DLY REV at the end of the last block (Q15) */
static HOT2 __attribute__((noinline)) void dbus_sends(const int32_t *to, uint32_t n)
{
    uint32_t i, k;
    int32_t m = 0, xmax, d[3];
    for (k = 0; k < 3u; k++) {
        m = to[k] > m ? to[k] : m, m = dbus_snd[k] > m ? dbus_snd[k] : m;
        d[k] = to[k] - dbus_snd[k];
    }
    xmax = 0x7FFFFFFF / (m | 1);                        /* (mulq15 would overflow) */
    for (i = 0; i < n; i++) {
        int32_t x = clamp((mix_l[i] >> 1) + (mix_r[i] >> 1), -xmax, xmax);
        if (dbus_snd[0] | d[0])
            send_c[i] += mulq15(x, dbus_snd[0] + ((d[0] * (int32_t)(i + 1u)) >> CTL_LOG2));
        if (dbus_snd[1] | d[1])
            send_d[i] += mulq15(x, dbus_snd[1] + ((d[1] * (int32_t)(i + 1u)) >> CTL_LOG2));
        if (dbus_snd[2] | d[2])
            send_r[i] += mulq15(x, dbus_snd[2] + ((d[2] * (int32_t)(i + 1u)) >> CTL_LOG2));
    }
    for (k = 0; k < 3u; k++)
        dbus_snd[k] = to[k];
}
static HOT2 __attribute__((noinline)) void dbus_run(uint32_t n)
{
    const int16_t *p = TDRUM->p;
    int32_t on = fx_on(TDRUM), to[3];
    uint32_t s0 = ins_slot[TRK_DRUM][0], s1 = ins_slot[TRK_DRUM][1], t, m, nc = DBUS_NC();
    ASTAGE0(0x98);
#if DINS
    for (t = 0; t < NDRUM; t++)                         /* (the drums have rendered: the voices' inserts that ran) */
        dins_ran[t] = dins_now[t], dins_now[t] = 0;
#endif
    if (!FX_INS_BYPASS) {
        ASTAGE0(0xC0 + (s0 & 0x1Fu));                   /* (the drum bus's insert type running: core.h) */
        dbus_ins(s0, on && INS_ON(TRK_DRUM, s0), n, nc);   /* S1, S2 (an empty slot, a type not built: nothing) */
        ASTAGE0(0xC0 + (s1 & 0x1Fu));
        dbus_ins(s1, on && INS_ON(TRK_DRUM, s1), n, nc);
        for (t = 0, m = INS_OUT(TRK_DRUM, s0, s1); m; t++, m >>= 1)   /* the inserts in no slot: at 0 (INS_OUT) */
            if (m & 1u) {
                ASTAGE0(0xE0 + (t & 0x1Fu));
                dbus_ins(t, 0, n, nc);
            }
    }
    ASTAGE0(0x99);
    to[0] = on && FXS_ON(FXT_CHO) ? p[P_CHOR] * 258 : 0;
    to[1] = on && FXS_ON(FXT_DLY) ? p[P_DLY] * 258 : 0;
    to[2] = on && FXS_ON(FXT_REV) ? p[P_REV] * 258 : 0;
    if (to[0] | to[1] | to[2] | dbus_snd[0] | dbus_snd[1] | dbus_snd[2])
        dbus_sends(to, n);
    ASTAGE0(0x9A);
#if FELUCCA_USB_AUDIO
    ua_dbus_tap(mix_l, mix_r, n);                       /* the drum bus for a USB channel that takes it (ua_tap.h) */
#endif
}

/* ---- a part's two insert slots (2026-10-12: "two slot, configurable per track (filter, disto, comp etc)"): after
 * the SLICER (D2), S1 then S2 (fx_slots.c ins_slot), each its type's amount (P_DIST, P_TFLT, P_TCOMP) while the part's
 * FX are on; the inserts in no slot run at 0 (DIST rests, the FILTER glides open then rests, COMP lets go). In main
 * RAM (HOT2): mix_part (HOT) calls it once a block */
AINL void ins_one(track_t *t, uint32_t k, uint32_t ty, int32_t live, int32_t *b, uint32_t n)
{
#if FELUCCA_FX_DIST
    if (ty == FXT_DIST)
        dist_run(&t->dist, live ? t->p[P_DIST] : 0, b, n);
#endif
#if TFLT_ON
    if (ty == FXT_FILT)
        tflt_part(t, live ? t->p[P_TFLT] : 0, b, n);
#endif
#if FELUCCA_MASTER_COMP
    if (ty == FXT_COMP)
        tcomp_run(&tcomp[k], live ? t->p[P_TCOMP] : 0, b, b, n, k);
#endif
#if INS_FX
    if (IFX_TYPE(ty) && FXT_AMT[ty] != 0xFFu)   /* (insfx.c; at 0 it fades out,
                                                        * then rests; JUNO CHORUS: fxi_kern.c) */
        ifx_part(ty, k, live ? t->p[FXT_AMT[ty]] : 0, b, n);
#endif
#if FELUCCA_FX_DUST
    if (ty == FXT_DUST)
        dust_run(&tdust[k], live ? t->p[P_TDUST] : 0, b, 0, n);
#endif
    (void)t, (void)k, (void)ty, (void)live, (void)b, (void)n;
}
static HOT2 __attribute__((noinline)) void ins_part(track_t *t, int32_t *b, uint32_t n)
{
    uint32_t k = (uint32_t)(t - trk) % NPART, s0 = ins_slot[k][0], s1 = ins_slot[k][1], ty, m;
    int32_t on = fx_on(t);
    ins_one(t, k, s0, on && INS_ON(k, s0), b, n);
    ins_one(t, k, s1, on && INS_ON(k, s1), b, n);
    for (ty = 0, m = INS_OUT(k, s0, s1); m; ty++, m >>= 1)   /* (in no slot: INS_OUT) */
        if (m & 1u)
            ins_one(t, k, ty, 0, b, n);
}

/* one synth part into the dry mix and the sends; a part with no voice sounding costs
 * the LFO tick and a cleared buffer only (after the DIST tail has run out) */
static HOT void mix_part(track_t *t, uint32_t n MIXACC_PARAM)
{
    int32_t *b = MX(part_buf);
    uint32_t i;
    int32_t g0 = 32767 - t->att, g1 = gain_next(t);
    uint32_t nr = track_render(t, b, n);
    BENCH_SIG((uint32_t)(t - trk), b, n);
    if (nr)
        t->tail = 16;                                   /* blocks of DIST state to run out after the last voice */
    else if ((!t->tail || !t->p[P_DIST] || !fx_on(t) || !--t->tail) && !slicer_busy(t))
        g0 = g1 = 0;                                    /* silent: nothing to mix, as a muted part */
    if (!g0 && !g1) {                                   /* silent, or MUTE / SOLO (the voices run, nothing is heard) */
        slicer_track(t, 0, n);                          /* (the SLICER's step clock runs on) */
#if FELUCCA_MASTER_COMP
        tcomp_quiet(t, b, n);                           /* (its COMP insert lets go: no freeze) */
#endif
#if INS_FX
        ifx_quiet_part(t);                              /* (a further insert switched off meanwhile: at rest) */
#endif
#if FELUCCA_GLIDE
        t->gl_on = 0;                                   /* the gains settle at once */
#endif
        return;
    }
    {
        int32_t lvl = LEVEL_Q12[t->p[P_LEVEL] ? clamp(t->p[P_LEVEL] + t->p[P_ED_FX], 1, 127) : 0], pan = t->p[P_PAN];   /* (+ the sound's trim: 1/2 dB steps, as LEVEL's) */
        int32_t gl, gr;
        int32_t on = fx_on(t), pk = t->peak;          /* FX bypass: no sends (the buses' tails ring out) */
        pan_gains(pan, &gl, &gr);                     /* (dsp.c) */
        int32_t c = on && FXS_ON(FXT_CHO) ? t->p[P_CHOR] * 258 : 0, d = on && FXS_ON(FXT_DLY) ? t->p[P_DLY] * 258 : 0;   /* (a send in no FX slot: */
        int32_t r = on && FXS_ON(FXT_REV) ? t->p[P_REV] * 258 : 0;                                  /* none, fx_slots.c) */
        int32_t xmax = c > d ? c : d;
        int32_t ga = mulq15(g0, duck.g0), gb = mulq15(g1, duck.g1);   /* mute x duck, ramped over the block */
        xmax = 0x7FFFFFFF / ((xmax > r ? xmax : r) | 1);   /* sends: loud chords at a high LEVEL */
        slicer_track(t, b, n);                          /* slicer.c: first (D2), before the inserts, level, pan, sends */
        if ((FELUCCA_FX_DIST || TFLT_ON || FELUCCA_MASTER_COMP || INS_FX || FELUCCA_FX_DUST) && !FX_INS_BYPASS)
            ins_part(t, b, n);                          /* its two insert slots, S1 then S2 (above) */
#if FELUCCA_UI == 1
        if (scope_src && t == &trk[scope_src - 1u])     /* the SCOPE on this part: its block, both sides */
            FAR(vis_tap_block)(b, b, n);
#endif
#if FELUCCA_GLIDE
        int32_t lvl0, dl, gl0, gr0, c0, d0, r0, dgl, dgr, dc, dd, dr;
        if (!t->gl_on) {                                /* the first block (or after silence): at the targets */
            t->gl_on = 1;
            t->gl_v = lvl;
            t->gl_pl = gl, t->gl_pr = gr, t->gl_c = c, t->gl_d = d, t->gl_rv = r;
        }
        lvl0 = t->gl_v, gl0 = t->gl_pl, gr0 = t->gl_pr, c0 = t->gl_c, d0 = t->gl_d, r0 = t->gl_rv;
        t->gl_v = glide_next(lvl0, lvl);                /* where each gain is at the end of this block */
        t->gl_pl = glide_next(gl0, gl), t->gl_pr = glide_next(gr0, gr);
        t->gl_c = glide_next(c0, c), t->gl_d = glide_next(d0, d), t->gl_rv = glide_next(r0, r);
        dl = (t->gl_v - lvl0) >> CTL_LOG2;
        dgl = t->gl_pl - gl0, dgr = t->gl_pr - gr0, dc = t->gl_c - c0, dd = t->gl_d - d0, dr = t->gl_rv - r0;
        {   /* the sends' clamp: the largest send over the block (settled: max(c, d, r), as without) */
            int32_t m = c0 > t->gl_c ? c0 : t->gl_c, k = d0 > t->gl_d ? d0 : t->gl_d;
            m = k > m ? k : m;
            k = r0 > t->gl_rv ? r0 : t->gl_rv;
            xmax = 0x7FFFFFFF / ((k > m ? k : m) | 1);
        }
#if FELUCCA_USB_AUDIO
        int32_t *cap = track_capture + (t - trk);       /* this part's USB stem */
#endif
        t->lvl = lvl;
        for (i = 0; i < n; i++) {
            /* pre-shift: 8 loud voices; the input saturates where LEVEL would overflow (FM6 keeps Dexed's
             * headroom, 16 unit sines a voice: as Melodee's mix_part; no other engine gets there) */
            int32_t x = ((clamp(b[i], -884000, 884000) >> 2) * (lvl0 + dl * (int32_t)i)) >> 10, a;
            int32_t xs, g = ga + (((gb - ga) * (int32_t)i) >> CTL_LOG2);
            if (g < 32767)
                x = (x >> 4) * (g >> 3) >> 8;           /* (Q15 in two halves: no 32-bit overflow) */
            a = x < 0 ? -x : x;
            xs = clamp(x, -xmax, xmax);                 /* sends: mulq15 would overflow */
#if FELUCCA_USB_AUDIO
            cap[i * NTRK] = x;
#endif
            if (a > pk)
                pk = a;
            if (c0 | dc)
                MX(send_c)[i] += mulq15(xs, c0 + ((dc * (int32_t)i) >> CTL_LOG2));
            if (d0 | dd)
                MX(send_d)[i] += mulq15(xs, d0 + ((dd * (int32_t)i) >> CTL_LOG2));
            if (r0 | dr)
                MX(send_r)[i] += mulq15(xs, r0 + ((dr * (int32_t)i) >> CTL_LOG2));
            MX(mix_l)[i] += ((x >> 4) * (gl0 + ((dgl * (int32_t)i) >> CTL_LOG2))) >> 8;   /* (x may pass 2^19: >> 4 first) */
            MX(mix_r)[i] += ((x >> 4) * (gr0 + ((dgr * (int32_t)i) >> CTL_LOG2))) >> 8;
        }
        t->peak = pk;
#else
        int32_t lvl0 = t->lvl ? t->lvl : lvl, dl = (lvl - lvl0) >> CTL_LOG2;   /* a new sound's trim: ramped */
#if FELUCCA_USB_AUDIO
        int32_t *cap = track_capture + (t - trk);       /* this part's USB stem */
#endif
        t->lvl = lvl;
        for (i = 0; i < n; i++) {
            /* pre-shift: 8 loud voices; the input saturates where LEVEL would overflow (FM6 keeps Dexed's
             * headroom, 16 unit sines a voice: as Melodee's mix_part; no other engine gets there) */
            int32_t x = ((clamp(b[i], -884000, 884000) >> 2) * (lvl0 + dl * (int32_t)i)) >> 10, a;
            int32_t xs, g = ga + (((gb - ga) * (int32_t)i) >> CTL_LOG2);
            if (g < 32767)
                x = (x >> 4) * (g >> 3) >> 8;           /* (Q15 in two halves: no 32-bit overflow) */
            a = x < 0 ? -x : x;
            xs = clamp(x, -xmax, xmax);                 /* sends: mulq15 would overflow */
#if FELUCCA_USB_AUDIO
            cap[i * NTRK] = x;
#endif
            if (a > pk)
                pk = a;
            if (c)
                MX(send_c)[i] += mulq15(xs, c);
            if (d)
                MX(send_d)[i] += mulq15(xs, d);
            if (r)
                MX(send_r)[i] += mulq15(xs, r);
            MX(mix_l)[i] += ((x >> 4) * gl) >> 8;           /* (x may pass 2^19: >> 4 first) */
            MX(mix_r)[i] += ((x >> 4) * gr) >> 8;
        }
        t->peak = pk;
#endif
#if FELUCCA_INS_WIDTH
        if (ifx_sd[(uint32_t)(t - trk) % NPART])        /* WIDE's side (insfx.c): after the level, L+ R- */
            ifx_side_mix(ifx_side[(uint32_t)(t - trk) % NPART], lvl0, dl, ga, gb, MX(mix_l), MX(mix_r), n);
#endif
#if FELUCCA_INS_JCHORUS
        if (ifx_jsd[(uint32_t)(t - trk) % NPART])       /* JUNO CHORUS's side (fxi_kern.c): as WIDE's */
            ifx_side_mix(ifx_jside[(uint32_t)(t - trk) % NPART], lvl0, dl, ga, gb, MX(mix_l), MX(mix_r), n);
#endif
    }
}
#include "punch/punch.c"            /* PUNCH-IN FX on the whole mix (FX held + a white key) */

/* ---- the master's two insert slots (fx_slots.c mst_slot; fx-rework 2026-10-14): after the returns and PUNCH, S1 then
 * S2, before the master volume. FILT is the DJ filter of before (the track FILTER's code on L / R: tflt_run with no
 * shift is djf_process's, sample for sample), DUST the master DUST, DYN the master COMP (its limiter stays at the
 * output, mix_finish); the other types on L / R as on the drum bus. A type in no slot at 0: it lets go, then rests */
#if FELUCCA_FX_DIST
static dist_t mst_dist[2];
#endif
static HOT2 __attribute__((noinline)) void mst_ins(uint32_t t, int32_t live, int32_t *l, int32_t *r, uint32_t n)
{
#if FELUCCA_FX_DIST
    if (t == FXT_DIST) {
        int32_t d = live ? mst_amt[FXT_DIST] : 0;
        dist_run(&mst_dist[0], d, l, n);
        dist_run(&mst_dist[1], d, r, n);
    }
#endif
#if TFLT_ON
    if (t == FXT_FILT) {
        tflt_t *f = &tflt[NTRK];
        tsvf_t c;
        if (tflt_block(f, live ? song.g[G_FILT] : 0, &c)) {
            tflt_run(f, &c, l, n, 0, 0);
            tflt_run(f, &c, r, n, 1, 0);
        }
    }
#endif
#if FELUCCA_MASTER_COMP
    if (t == FXT_DYN)
        mc_master(live, l, r, n);                       /* COMP, GAIN; LIMIT's switch (master_comp.c; off: nothing) */
#endif
    if (t == FXT_DUST)
        dust_run(&mst_dust, live && FELUCCA_FX_DUST ? song.g[G_DUST] : 0, l, r, n);
#if INS_FX
    if (IFX_TYPE(t) && FXT_AMT[t] != 0xFFu)
        ifx_mst(t, live ? mst_amt[t] : 0, l, r, n);
#endif
    (void)live, (void)l, (void)r, (void)n;
}
#if INS_FX
#define MST_IDLE() ifx_awake[IFX_AW_MST]
#else
#define MST_IDLE() 0u
#endif
static HOT2 __attribute__((noinline)) void mst_run(int32_t *l, int32_t *r, uint32_t n)
{
    uint32_t s0 = mst_slot[0], s1 = mst_slot[1], t, m;
    mst_ins(s0, MST_ON(s0), l, r, n);
    mst_ins(s1, MST_ON(s1), l, r, n);
    m = MST_OK & ((FXT_BIT(FXT_DIST) | FXT_BIT(FXT_FILT) | FXT_BIT(FXT_DYN) | FXT_BIT(FXT_DUST)) | MST_IDLE()) &
        ~(FXT_BIT(s0) | FXT_BIT(s1));
    for (t = 0; m; t++, m >>= 1)                        /* (in no slot: at 0) */
        if (m & 1u)
            mst_ins(t, 0, l, r, n);
}

/* ---- a stereo owner's insert of any type (fx-pool 2026-10-15; was the USB input's ut_ins): the USB input track and the
 * returns R1 R2 R3 (rt_run). Its own DIST (a side each), FILTER, DYN (a compressor with the default RATIO ATK REL and AUTO
 * make-up, no CEIL), DUST; the further types on L and R (insfx.c ifx_st: i0 the owner's index, the pool's instances for the
 * limited ones). a: the amount (0: it lets go) */
typedef struct {
#if FELUCCA_FX_DIST
    dist_t d[2];
#endif
#if TFLT_ON
    tflt_t f;
#endif
#if FELUCCA_MASTER_COMP
    mc_t c;
#endif
    dust_t u;
} sto_t;
#if FELUCCA_MASTER_COMP
#define STO_C .c = {0, 0, 8192, 0},
#else
#define STO_C
#endif
#define STO_INIT(seed) {STO_C .u = {0, 0, 0, 0, 0, (int32_t)(seed), 0, 0}}
static HOT2 __attribute__((noinline)) void sto_ins(sto_t *o, uint32_t i0, uint32_t t, int32_t a, int32_t *l, int32_t *r,
                                                   uint32_t n)
{
#if FELUCCA_FX_DIST
    if (t == FXT_DIST) {
        dist_run(&o->d[0], a, l, n);
        dist_run(&o->d[1], a, r, n);
    }
#endif
#if TFLT_ON
    if (t == FXT_FILT) {
        tsvf_t c;
        if (tflt_block(&o->f, a, &c)) {
            tflt_run(&o->f, &c, l, n, 0, 0);
            tflt_run(&o->f, &c, r, n, 1, 0);
        }
    }
#endif
#if FELUCCA_MASTER_COMP
    if (t == FXT_DYN && (a || !tcomp_rest(&o->c))) {
        mc_set_t c;
        int32_t thr = a > 0 ? -1 - (a - 1) * 29 / 126 : 0;
        mc_settings(&c, thr, GP[G_CRAT].def, GP[G_CATK].def, GP[G_CREL].def, (-thr * MC_SLOPE[GP[G_CRAT].def & 7]) >> 15);
        mc_run(&o->c, &c, l, r, l, r, n);
    }
#endif
    if (t == FXT_DUST)
        dust_run(&o->u, FELUCCA_FX_DUST ? a : 0, l, r, n);
#if INS_FX
    if (IFX_TYPE(t) && FXT_AMT[t] != 0xFFu && (FXT_BUILT >> t & 1u))   /* (what the owner may hold: its slots' lists; GATER:
                                                        * the USB input's only, insfx.c ifx_st_run) */
        ifx_st(t, i0, a, l, r, n);
#endif
    (void)o, (void)i0, (void)a, (void)l, (void)r, (void)n;
}
/* the types an owner still runs at 0 out of its slots (they let go), i0's ifx_awake entry with them */
#if INS_FX
#define STO_AWAKE(i0) ifx_awake[ifx_aw(i0)]
#else
#define STO_AWAKE(i0) 0u
#endif
#define STO_IDLE(i0) ((FXT_BIT(FXT_DIST) | FXT_BIT(FXT_FILT) | FXT_BIT(FXT_DYN) | FXT_BIT(FXT_DUST) | STO_AWAKE(i0)) & RT_OK)

/* ---- the returns R1 R2 R3 holding a type that is not a send (fx_slots.c fxs_rty, fx-pool): each on its line's send (mono,
 * into L and R), its slot's amount, out times the line's return level into the mix, 100 % wet. The line's own send bus then
 * hears silence (its send zeroed here). A type gone from a return: its states let go on silence for RT_TAIL blocks */
#define RT_TAIL 64u                                     /* blocks (46 ms): every insert here is at rest by then */
static sto_t rt_sto[3] = {STO_INIT(0x2B9F11C5), STO_INIT(0x6A09E667), STO_INIT(0x3C6EF372)};
static uint8_t rt_tail[3];                              /* blocks of tail left */
static HOT2 __attribute__((noinline)) void rt_run(uint32_t n)
{
    int32_t *const snd[3] = {send_c, send_d, send_r};
    uint32_t j, i, t, m;
    for (j = 0; j < 3u; j++) {
        int32_t l[CTL], r[CTL], g = FX_RETQ(song.g[G_CRET + j]);
        t = fxs_rty[j];
        if (t == FXT_NONE && !rt_tail[j])
            continue;
        if (t != FXT_NONE)
            for (i = 0; i < n; i++)
                l[i] = r[i] = snd[j][i], snd[j][i] = 0;   /* (its bus: silence) */
        else
            for (i = 0; i < n; i++)
                l[i] = r[i] = 0;
        if (t != FXT_NONE)
            sto_ins(&rt_sto[j], IFX_RT(j), t, rt_amt(j), l, r, n);
        for (i = 0, m = STO_IDLE(IFX_RT(j)) & ~FXT_BIT(t); m; i++, m >>= 1)   /* (in no return: at 0, they let go) */
            if (m & 1u)
                sto_ins(&rt_sto[j], IFX_RT(j), i, 0, l, r, n);
        rt_tail[j] = t != FXT_NONE ? (uint8_t)RT_TAIL : (uint8_t)(rt_tail[j] - 1u);
        if (!g)
            continue;
#if FELUCCA_UI == 1
        int32_t pk = 0;
#endif
        for (i = 0; i < n; i++) {
            int32_t a = g >= 32768 ? l[i] : (l[i] >> 4) * (g >> 3) >> 8, b = g >= 32768 ? r[i] : (r[i] >> 4) * (g >> 3) >> 8;
            mix_l[i] += a, mix_r[i] += b;
#if FELUCCA_UI == 1
            pk = (b < 0 ? -b : b) > pk ? (b < 0 ? -b : b) : pk;
#endif
        }
#if FELUCCA_UI == 1
        if (pk > fx_ret_pk[j])
            fx_ret_pk[j] = pk;                          /* (the mixer's return meter: this line's block peak, after its return level) */
#endif
    }
}

#if FELUCCA_USB_AUDIO
/* ---- the USB input as a track (fx-rework stage 3; fx_slots.c ut): the computer's audio from the playback ring
 * (usb_audio_stream.c ua_take, a block, with the USB service held off for the copy only), its two insert slots on L and R
 * (sto_ins: any insert type, states of its own, the pool's instances for CHRS ECHO JUNO FLNG; DYN: a compressor with the
 * default RATIO ATK REL and AUTO make-up, no CEIL; a mono insert type runs on each side; GATER one gain on both), its LEVEL, MUTE (a block's fade), PAN (the balance: the pan law on each
 * side), FX on, its sends from its mid; then into the mix bus with the tracks, before the returns and the master. At its
 * defaults (unity, centre, no slot) the samples go in as they are: the mix as before but that the master's inserts now
 * take them too (they were added after the master's limiter) */
static uint32_t ua_take(int32_t *l, int32_t *r, uint32_t n);   /* usb_audio_stream.c (XIP: FAR) */
static int32_t ut_l[CTL], ut_r[CTL];
static sto_t ut_sto = STO_INIT(0x4F1BBCDD);
static int32_t ut_g = 32767, ut_lv = 4096, ut_snd[3];   /* the mute gain, the level and the sends at the end of the last block */
static volatile int32_t ut_pk;                          /* its peak since the mixer took it (op_mixdraw.c), after its level */
static volatile uint8_t ut_live;                        /* frames came in the last block (the mixer: "no stream") */
#define ut_ins(t, a, l, r, n) sto_ins(&ut_sto, IFX_UT, t, a, l, r, n)
/* the USB input into the mix (mix_block, dual.c mix_block_dual: after the parts, before mix_finish) */
static HOT2 __attribute__((noinline)) void ut_mix(uint32_t n)
{
    uint32_t i, s, t, m, got = FAR(ua_take)(ut_l, ut_r, n), on = ut.v[UTV_FXON] != 0;
    int32_t g1 = ut.v[UTV_MUTE] ? 0 : 32767, lv1 = LEVEL_Q12[clamp(ut.v[UTV_LVL], 0, 127)], gl, gr, to[3], mx = 0, xmax, pk = ut_pk;
    ut_live = (uint8_t)got;
    if (!got) {
        if (ut.slot[0] == FXT_NONE && ut.slot[1] == FXT_NONE)
            return;                                     /* (no stream, no insert: nothing to do) */
        for (i = 0; i < n; i++)
            ut_l[i] = ut_r[i] = 0;                      /* (the inserts' tails on silence) */
    }
    for (s = 0; s < INS_N; s++)
        ut_ins(ut.slot[s], on && (UT_OK >> ut.slot[s] & 1u) ? ut.amt[s] : 0, ut_l, ut_r, n);
    m = (FXT_BIT(FXT_DIST) | FXT_BIT(FXT_FILT) | FXT_BIT(FXT_DYN) | FXT_BIT(FXT_DUST)
#if INS_FX
         | ifx_awake[IFX_AW_UT]
#endif
        ) & UT_OK & ~(FXT_BIT(ut.slot[0]) | FXT_BIT(ut.slot[1]));
    for (t = 0; m; t++, m >>= 1)                        /* (in no slot: at 0, they let go) */
        if (m & 1u)
            ut_ins(t, 0, ut_l, ut_r, n);
    pan_gains(ut.v[UTV_PAN], &gl, &gr);
    to[0] = on && FXS_ON(FXT_CHO) ? ut.v[UTV_CHO] * 258 : 0;
    to[1] = on && FXS_ON(FXT_DLY) ? ut.v[UTV_DLY] * 258 : 0;
    to[2] = on && FXS_ON(FXT_REV) ? ut.v[UTV_REV] * 258 : 0;
    for (i = 0; i < 3u; i++)
        mx = to[i] > mx ? to[i] : mx, mx = ut_snd[i] > mx ? ut_snd[i] : mx;
    xmax = 0x7FFFFFFF / (mx | 1);
    for (i = 0; i < n; i++) {
        int32_t lv = ut_lv + (((lv1 - ut_lv) * (int32_t)i) >> CTL_LOG2), g = ut_g + (((g1 - ut_g) * (int32_t)i) >> CTL_LOG2);
        int32_t xl = clamp(ut_l[i], -200000, 200000), xr = clamp(ut_r[i], -200000, 200000), md;
        if (lv != 4096)
            xl = (xl * lv) >> 12, xr = (xr * lv) >> 12;
        if (g < 32767)
            xl = (xl * (g >> 3)) >> 12, xr = (xr * (g >> 3)) >> 12;
        md = clamp((xl >> 1) + (xr >> 1), -xmax, xmax);
        if (ut_snd[0] | to[0])
            send_c[i] += mulq15(md, ut_snd[0] + (((to[0] - ut_snd[0]) * (int32_t)(i + 1u)) >> CTL_LOG2));
        if (ut_snd[1] | to[1])
            send_d[i] += mulq15(md, ut_snd[1] + (((to[1] - ut_snd[1]) * (int32_t)(i + 1u)) >> CTL_LOG2));
        if (ut_snd[2] | to[2])
            send_r[i] += mulq15(md, ut_snd[2] + (((to[2] - ut_snd[2]) * (int32_t)(i + 1u)) >> CTL_LOG2));
        mix_l[i] += gl == 4096 ? xl : (xl * gl) >> 12;
        mix_r[i] += gr == 4096 ? xr : (xr * gr) >> 12;
        xl = xl < 0 ? -xl : xl, xr = xr < 0 ? -xr : xr;
        pk = xl > pk ? xl : xr > pk ? xr : pk;
    }
    ut_pk = pk;
    ut_g = g1, ut_lv = lv1;
    for (i = 0; i < 3u; i++)
        ut_snd[i] = to[i];
}
#endif
static int32_t master_cur = -1;                        /* the volume knob, ramped per sample (no zipper) */
#if FELUCCA_VIS || FELUCCA_UI == 1
/* The visualiser's tap (ui_vis.c; the Optimist UI's SCOPE and the mixer's master column, op_scope.c): each block's
 * mix, copied whole once a block (a word loop, both sides), as MASTER all the way up: after the buses and the master compressor, before the volume, the limiter and the knee (the
 * UI applies the knee). 1024 frames of each side: 23 ms at 44.1 kHz. In flash: called through FAR from the RAM code.
 * (A reader may see a block half written: a picture, not a measurement.) */
#define VIS_RING 1024u                                  /* a multiple of CTL */
static int32_t vis_pcm[2][VIS_RING] __attribute__((section(".pool")));   /* (the pool: main RAM keeps the undo history) */
static volatile uint32_t vis_wr;
static __attribute__((noinline)) void vis_tap_block(const int32_t *l, const int32_t *r, uint32_t n)
{
    uint32_t i, w = vis_wr & (VIS_RING - 1u);
    for (i = 0; i < n; i++) {                           /* (words: libc.c's memcpy goes a byte at a time, 4x the cost) */
        vis_pcm[0][w + i] = l[i];
        vis_pcm[1][w + i] = r[i];
    }
    vis_wr += n;
}
#endif
#if FELUCCA_UI == 1
/* the SCOPE on the drum track (scope_src SCOPE_DR): the drums add into the mix, so the ring's next block is the mix
 * after them less the mix before them; mark before the drums, take after (mix_block; nothing when not chosen) */
static __attribute__((noinline)) void scope_drums_mark(uint32_t n)
{
    uint32_t i, w = vis_wr & (VIS_RING - 1u);
    for (i = 0; i < n; i++) {
        vis_pcm[0][w + i] = -mix_l[i];
        vis_pcm[1][w + i] = -mix_r[i];
    }
}
static __attribute__((noinline)) void scope_drums_take(uint32_t n)
{
    uint32_t i, w = vis_wr & (VIS_RING - 1u);
    for (i = 0; i < n; i++) {
        vis_pcm[0][w + i] += mix_l[i];
        vis_pcm[1][w + i] += mix_r[i];
    }
    vis_wr += n;
}
#define SCOPE_DRUMS(f) do { if (scope_src == SCOPE_DR) FAR(f)(n); } while (0)
#else
#define SCOPE_DRUMS(f) ((void)0)
#endif
/* the buses, the master chain and the output (mix_block, and dual.c's mix_block_dual) */
static inline __attribute__((always_inline)) void mix_finish(int32_t *out, uint32_t n)
{
    uint32_t i;
    int32_t m0, m1;
    BENCH_SIG(4, mix_l, n);
    BENCH_SIG(5, send_r, n);
    if (fxs_rty[0] != FXT_NONE || fxs_rty[1] != FXT_NONE || fxs_rty[2] != FXT_NONE || rt_tail[0] | rt_tail[1] | rt_tail[2])
        rt_run(n);                                      /* the returns holding another type (above): first, their sends */
    if (fx_buses(send_c, send_d, send_r, wet_l, wet_r, n)) {  /* (0: every bus idle, nothing to add) */
        BENCH_SIG(6, wet_l, n);
        for (i = 0; i < n; i++) {
            mix_l[i] += wet_l[i];
            mix_r[i] += wet_r[i];
        }
    }
    if (FELUCCA_FX_PUNCH)
        punch_process(mix_l, mix_r, n);
    mst_run(mix_l, mix_r, n);                           /* the master's insert slots (above) */
#if FELUCCA_VIS
    FAR(vis_tap_block)(mix_l, mix_r, n);                /* the visualiser: this block's mix, copied (ui_vis.c) */
#elif FELUCCA_UI == 1
    if (!scope_src)                                     /* the SCOPE on the master, the mixer's master column */
        FAR(vis_tap_block)(mix_l, mix_r, n);
#endif
#if FELUCCA_GLIDE
    m0 = master_cur < 0 ? (int32_t)song.master_q12 : master_cur;   /* MASTER glides (~10 ms; X0X 0.10.1) */
    m1 = glide_next(m0, (int32_t)song.master_q12);
#else
    m1 = (int32_t)song.master_q12;
    m0 = master_cur < 0 ? m1 : master_cur;
#endif
    master_cur = m1;
#if FELUCCA_MASTER_COMP
    if (mlim.on) {                                      /* LIMIT > CEIL: the volume, master_pre, the lookahead brickwall */
        mlim_block(mix_l, mix_r, out, n, m0, m1);       /* instead of the limiter and the knee (master_comp.c) */
        return;
    }
#endif
    for (i = 0; i < n; i++) {
        int32_t m = m0 + (((m1 - m0) * (int32_t)i) >> CTL_LOG2);
        int32_t l = ((mix_l[i] >> 2) * m) >> 10;
        int32_t r = ((mix_r[i] >> 2) * m) >> 10;
        master_out(&l, &r);
        out[2u * i] = l;
        out[2u * i + 1u] = r;
    }
}

#if FELUCCA_BENCH
static void bench_block(void);                          /* bench.c */
#define BENCH_BLOCK() FAR(bench_block)()               /* (bench.c: XIP) */
#else
#define BENCH_BLOCK() ((void)0)
#endif
#if FELUCCA_USB_AUDIO
/* the USB stems, cleared for the block's parts to write (and the drums to add into). Nobody takes them
 * (FELUCCA_SKIP, track_capture_on 0): cleared one block in 64 only, so the drum stem's sum stays bounded */
AINL void capture_clear(uint32_t n)
{
    static uint8_t tick;
    uint32_t i;
    if (!FELUCCA_SKIP || track_capture_on || !(++tick & 63u))
        for (i = 0; i < n * NTRK; i++)
            track_capture[i] = 0;
    dlt_block(track_capture_on, n);                     /* the lane / drum bus taps a channel takes (ua_tap.h) */
}
#endif
#if !DUAL_RENDER                                       /* (dual.c: mix_block_dual) */
static HOT void mix_block(int32_t *out, uint32_t n)
{
    uint32_t i;
#if FELUCCA_USB_AUDIO
    capture_clear(n);
#endif
    for (i = 0; i < n; i++)
        send_c[i] = send_d[i] = send_r[i] = mix_l[i] = mix_r[i] = 0;
    FAR(events_block)(n);                               /* (the sequencer stays in XIP) */
    BENCH_BLOCK();
#if FELUCCA_MACROS
    FAR(mac_pre)();                                     /* the macros' values in (macro.c, XIP) */
#endif
    if (FELUCCA_FX_DUCK)
        duck_block(n * (uint32_t)song.g[G_BPM]);
    drums.a0 = TDRUM->att;                              /* the drums first, alone on the bus (the sums come out the */
    drums.a1 = 32767 - gain_next(TDRUM);                /* same in either order): the drum track's mute / solo fade, */
    SCOPE_DRUMS(scope_drums_mark);                      /* (the SCOPE on the drums: their part of the mix) */
    slicer_drums(mix_l, mix_r, send_r, n);              /* drums_render through the SLICER when on, */
    dbus_run(n);                                        /* its two insert slots and its own sends (above) */
    SCOPE_DRUMS(scope_drums_take);
    {
        uint32_t sw = pold_any() ? FAR(pold_in)() : 0u; /* (SLOOP 2.6) the parts fading out after a sound load: this */
        for (i = 0; i < NPART; i++)                     /* block with their old sound (voice.c sound_change) */
            mix_part(&trk[i], n);
        if (sw)
            FAR(pold_out)(sw);                          /* (and the new one back) */
    }
#if FELUCCA_USB_AUDIO
    ut_mix(n);                                          /* the USB input, a track (above) */
#endif
    mix_finish(out, n);
#if FELUCCA_MACROS
    FAR(mac_post)();                                    /* the authored values back */
#endif
}
#endif
