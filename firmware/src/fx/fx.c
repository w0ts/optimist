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
static int16_t rev_line[FELUCCA_FX_REVERB && REV_LINE_OWN ? REV_LINE_OWN : 1] REV_SECTION;
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
    while ((e >> sh) >= 16384)
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
static HOT void track_dist(track_t *t, int32_t *b, uint32_t n)
{
    dist_run(&t->dist, fx_on(t) && FXS_ON(FXT_DIST) ? t->p[P_DIST] : 0, b, n);   /* (bypassed, or in no FX slot: off, value kept) */
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
#if FELUCCA_DLY_HALVE
    while (s >= DLY_LEN)
        s >>= 1;                                        /* longer than the line: half of it, still on the beat
                                                         * (after X0X 892a3b5); cutting it short is not */
#endif
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
/* the delay's values for a block: from XIP, through FAR (RAMTEXT is full; once a block, for a running delay): the
 * loop gain, COLOR's low-pass step, the wet level (the master strip's return knob); returns the length */
static __attribute__((noinline)) uint32_t dly_block(int32_t *fb, int32_t *col, int32_t *dmix)
{
    *fb = dly_fb(song.g[G_DFDBK]);
    *col = 2000 + song.g[G_DCOLOR] * 240;
    *dmix = song.g[G_DMIX] * 258;
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
/* every built tank's cells and filters to 0 (the bus then idle): the shared line, the ROOM's diffusers and
 * low-passes, PLATE / FDN8's low-passes */
static void rev_tank_clear(void)
{
    uint32_t i;
#if REV_ALT
    rv_clear();
    rva_clear();
#else
    for (i = 0; i < sizeof rev_line / 2u; i++)
        rev_line[i] = 0;
#endif
#if FELUCCA_REV_ROOM
    for (i = 0; i < sizeof rev_ap / 2u; i++)
        rev_ap[i] = 0;
    for (i = 0; i < 4u; i++)
        fx.line_lp[i] = 0;
#endif
    (void)i;
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
    /* (the input scan only once the bus' lines are clear) */
    run_c = FELUCCA_FX_CHORUS && (fx.cho_q < CHO_LEN || fx_any(cho_in, n));   /* (registry.h: an FX not built) */
    run_d = FELUCCA_FX_DELAY && (fx.dly_q < DLY_LEN || fx.dly_lp || fx_any(dly_in, n));
    run_r = FELUCCA_FX_REVERB && (fx.rev_q < REV_Q || REV_LP_BUSY() ||
                                  REV_HALF_BUSY() || fx_any(rev_in, n));   /* (REV_HALF: its filters too) */
#define FX_ALL_IDLE (FELUCCA_SKIP && !FELUCCA_SPRING && !(run_c | run_d | run_r))   /* (SPRING: its own state) */
    if (run_c) {
        for (i = 0; i < n; i++) {
            wet_l[i] = cho_step(cho_in[i], CHO_R0, CHO_R1, &yr, &wc);
            wet_r[i] = yr;
        }
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
        }
    } else {
        fx.dly_w += n;
    }
    rev_bus(rev_in, wet_l, wet_r, n, ma, mb, g, lpk, run_r, &wv);   /* (the tank TYPE picks) */
#undef CHO_R0
#undef CHO_R1
    fx.cho_q = fx_q(fx.cho_q, wc, n);
    fx.dly_q = fx_q(fx.dly_q, wd, n);
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
#if FELUCCA_DUAL >= 2
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

#if FELUCCA_TRK_FILT
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
    int32_t z[5][2];                                    /* the SVF states: a part's [0]; the drums' L R REV DLY CHO */
} tflt_t;
static tflt_t tflt[NTRK];

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
    } else if (v > 0 && f->mode <= 0) {
        f->mode = 1;
        f->cut = 0;
        tflt_clear(f);
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
    for (i = 0; i < n; i++) {
        int32_t x = clamp(b[i] >> sh, -140000, 140000), y = tsvf_lp(c, x, &z[0], &z[1]);
        b[i] = (lp ? y : x - y) << sh;
    }
}
/* a part's (b: before its level, up to +-884000: 3 bits down) */
static HOT void tflt_part(const track_t *t, int32_t *b, uint32_t n)
{
    tflt_t *f = &tflt[(uint32_t)(t - trk) % NTRK];
    tsvf_t c;
    if (tflt_block(f, fx_on(t) && FXS_ON(FXT_FILT) ? t->p[P_TFLT] : 0, &c))   /* (in no FX slot: it opens, then off) */
        tflt_run(f, &c, b, n, 0, 3);
}
/* the drum track's: the bus accumulators hold the drums alone (mix_block renders them before the parts) */
static HOT void tflt_drums(uint32_t n)
{
    tflt_t *f = &tflt[TRK_DRUM];
    tsvf_t c;
    if (!tflt_block(f, fx_on(TDRUM) && FXS_ON(FXT_FILT) ? TDRUM->p[P_TFLT] : 0, &c))
        return;
    tflt_run(f, &c, mix_l, n, 0, 2);
    tflt_run(f, &c, mix_r, n, 1, 2);
    tflt_run(f, &c, send_r, n, 2, 2);
    tflt_run(f, &c, send_d, n, 3, 2);
    tflt_run(f, &c, send_c, n, 4, 2);
}
#endif

#if FELUCCA_MASTER_COMP
/* ---- the COMP insert (fx_slots.c, an FX slot type; the master COMP's mc_run, master_comp.c): a track's amount 0..127
 * is its threshold, -1 .. -30 dB under full scale, with make-up of half the static reduction at full scale; RATIO ATK
 * REL are every track's (FX > CMP: fxs_cset, the master COMP's lists). Pre-fader, after the FILTER (D4). At 0 it lets
 * go (the reduction falls back), then costs one compare a block */
static mc_t tcomp[NPART] = {{0, 0, 8192, 0}, {0, 0, 8192, 0}, {0, 0, 8192, 0}};   /* (the parts': the drum bus has none
                                                        * yet, design phase 4) */
static int32_t tcomp_sink[CTL];                         /* (a mono insert: mc_run's right side, written, never read) */
static HOT2 __attribute__((noinline)) void tcomp_run(mc_t *c, int32_t amt, int32_t *l, int32_t *r, uint32_t n)
{
    mc_set_t s;
    int32_t thr = amt > 0 ? -1 - (amt - 1) * 29 / 126 : 0;
    if (!amt && !c->gr16 && c->g13 == 8192)
        return;                                         /* (off and at rest) */
    mc_settings(&s, thr, fxs_cset[0], fxs_cset[1], fxs_cset[2], (-thr * MC_SLOPE[fxs_cset[0] & 7]) >> 15);
    mc_run(c, &s, l, r, l, r != l ? r : tcomp_sink, n);   /* (mono: the key both sides, the gain once) */
}

/* a part not heard this block (silent, or MUTE / SOLO): its insert is not on the mix, yet its state must not freeze
 * (the next note would start under the last one's reduction). In a slot it lets go on the cleared buffer (silence: the
 * key 0) until the reduction is gone; at 0 or in no slot, or once let go, it rests (nothing is heard: no step) */
AINL void tcomp_quiet(track_t *t, int32_t *b, uint32_t n)
{
    mc_t *c = &tcomp[(uint32_t)(t - trk) % NPART];
    int32_t amt = fx_on(t) && FXS_ON(FXT_COMP) ? t->p[P_TCOMP] : 0;
    if (amt && (c->gr16 | c->slow16)) {
        tcomp_run(c, amt, b, b, n);
        if (c->gr16 | c->slow16)
            return;
    }
    c->gr16 = c->slow16 = 0;
    c->g13 = 8192;
}
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
        if (FELUCCA_FX_DIST)
            track_dist(t, b, n);
        slicer_track(t, b, n);                          /* slicer.c: before the level, pan and sends */
#if FELUCCA_TRK_FILT
        tflt_part(t, b, n);                             /* the track's FILTER, after the SLICER (2.4) */
#endif
#if FELUCCA_MASTER_COMP
        tcomp_run(&tcomp[(uint32_t)(t - trk) % NPART], on && FXS_ON(FXT_COMP) ? t->p[P_TCOMP] : 0, b, b, n);   /* COMP */
#endif
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
    }
}

/* ---- DUST: the master through an old sampler and a record. G_DUST 0..127 turns up together: drive
 * into a soft clip, a lower sample rate (held samples, 44.1 -> 11 kHz), fewer bits (15 -> 8), a
 * one-pole low-pass (open -> ~3 kHz), a little hiss and crackle. The hiss and the crackle are the
 * record turning: they fade in with PLAY and out (~0.1 s) at STOP, so a stopped SLOOP is silent.
 * Stereo, ~25 ops a sample. */
static struct {
    int32_t hl, hr, hn;                                 /* held samples, samples left to hold */
    int32_t ll, lr;                                     /* low-pass states */
    int32_t rnd, click;                                 /* noise state, a crackle decaying */
    int32_t bed;                                        /* hiss / crackle level, Q15: 0 stopped, 32767 playing */
} dust = {0, 0, 0, 0, 0, 0x2545F491, 0, 0};


static HOT void dust_process(int32_t *l, int32_t *r, uint32_t n)
{
    int32_t d = song.g[G_DUST], hold, shift, a, drive, hiss, i, bed0, bed1;
    uint32_t pc;
    if (!d) {
        dust.bed = 0;
        return;
    }
    hold = 1 + d * 3 / 127;
    shift = d / 18;
    a = 32767 - d * 165;                                /* one-pole coefficient, Q15 */
    drive = 4096 + d * 24;                              /* Q12: 1x .. 1.75x */
    hiss = d * 2;
    pc = (uint32_t)d * 7u;                              /* crackle: chance per sample, x 2^-22 */
    bed0 = dust.bed;                                    /* ~0.1 s from 0 to full (238 a block of 32) */
    bed1 = dust.bed = clamp(dust.bed + (song.playing ? 238 : -238), 0, 32767);
    for (i = 0; i < (int32_t)n; i++) {
        int32_t x = l[i], y = r[i];
        int32_t bed = bed0 + (((bed1 - bed0) * i) >> CTL_LOG2);
        uint32_t nz = noise32(&dust.rnd);
        if (--dust.hn <= 0) {                           /* sample and hold, then the bits */
            dust.hn = hold;
            dust.hl = softclip(((x >> 2) * drive) >> 10);
            dust.hr = softclip(((y >> 2) * drive) >> 10);
            if (shift) {
                dust.hl = crush_tz(dust.hl, shift);     /* fewer bits, toward 0: no DC from tails */
                dust.hr = crush_tz(dust.hr, shift);
            }
        }
        dust.ll += mulq15(dust.hl - dust.ll, a);
        dust.lr += mulq15(dust.hr - dust.lr, a);
        if ((nz >> 10) < pc)                            /* a speck of dust */
            dust.click = mulq15(((int32_t)(nz & 0x3FFu) - 512) * d / 8, bed);
        x = dust.ll + dust.click + mulq15(((int32_t)(nz >> 16) - 32768) * hiss >> 15, bed);
        y = dust.lr + dust.click + mulq15(((int32_t)(nz & 0xFFFFu) - 32768) * hiss >> 15, bed);
        dust.click -= dust.click >> 2;
        l[i] = x;
        r[i] = y;
    }
}

/* ---- the DJ filter on the master: G_FILT < 0 a low-pass closing, > 0 a high-pass opening, 0 off.
 * The cutoff glides to the knob (no zipper); at 0 it opens fully, then the filter is bypassed. */
static struct {
    int32_t cut;                                        /* now, 0..127 << 8 (CUTOFF_HZ index) */
    int8_t mode;                                        /* -1 LP, 1 HP, 0 off */
    int32_t l1, l2, r1, r2;
} djf;

static HOT void djf_process(int32_t *l, int32_t *r, uint32_t n)
{
    int32_t v = song.g[G_FILT], to, i;
    tsvf_t c;
    if (v < 0 && djf.mode >= 0) {                       /* (switching side: from open) */
        djf.mode = -1;
        djf.cut = 127 << 8;
        djf.l1 = djf.l2 = djf.r1 = djf.r2 = 0;
    } else if (v > 0 && djf.mode <= 0) {
        djf.mode = 1;
        djf.cut = 0;
        djf.l1 = djf.l2 = djf.r1 = djf.r2 = 0;
    }
    if (!djf.mode)
        return;
    to = djf.mode < 0 ? (v < 0 ? (127 << 8) + v * 90 * 4 : 127 << 8) : (v > 0 ? v * 90 * 4 : 0);
    djf.cut += clamp(to - djf.cut, -384, 384);          /* ~1.5 index a block */
    if (!v && djf.cut == to) {
        djf.mode = 0;                                   /* fully open again: off */
        return;
    }
    tsvf_coef(&c, djf.cut, 40);
    for (i = 0; i < (int32_t)n; i++) {
        int32_t x = clamp(l[i], -140000, 140000), y = clamp(r[i], -140000, 140000);
        int32_t fl = tsvf_lp(&c, x, &djf.l1, &djf.l2), fr = tsvf_lp(&c, y, &djf.r1, &djf.r2);
        l[i] = djf.mode < 0 ? fl : x - fl;
        r[i] = djf.mode < 0 ? fr : y - fr;
    }
}

#include "punch/punch.c"            /* PUNCH-IN FX on the whole mix (FX held + a white key) */
static int32_t master_cur = -1;                        /* the volume knob, ramped per sample (no zipper) */
#if FELUCCA_VIS || FELUCCA_UI == 1
/* The visualiser's tap (ui_vis.c; the Optimist UI's SCOPE and the mixer's master column, op_scope.c): each block's
 * mix, copied whole once a block (a word loop, both sides), as MASTER all the way up: after the buses and the master compressor, before the volume, the limiter and the knee (the
 * UI applies the knee). 1024 frames of each side: 23 ms at 44.1 kHz. In flash: called through FAR from the RAM code.
 * (A reader may see a block half written: a picture, not a measurement.) */
#define VIS_RING 1024u                                  /* a multiple of CTL */
static int32_t vis_pcm[2][VIS_RING];
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
    if (fx_buses(send_c, send_d, send_r, wet_l, wet_r, n))   /* (0: every bus idle, nothing to add) */
        for (i = 0; i < n; i++) {
            mix_l[i] += wet_l[i];
            mix_r[i] += wet_r[i];
        }
    if (FELUCCA_FX_DUST)
        dust_process(mix_l, mix_r, n);
    if (FELUCCA_FX_PUNCH)
        punch_process(mix_l, mix_r, n);
    if (FELUCCA_FX_DJF)
        djf_process(mix_l, mix_r, n);
#if FELUCCA_MASTER_COMP
    mc_master(mix_l, mix_r, n);                         /* COMP, GAIN; LIMIT's switch (master_comp.c; off: nothing) */
#endif
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
}
#endif
#if FELUCCA_DUAL < 2                                    /* (dual.c: mix_block_dual) */
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
#if FELUCCA_TRK_FILT
    drums.a0 = TDRUM->att;                              /* the drums first, alone on the bus: their FILTER (the sums */
    drums.a1 = 32767 - gain_next(TDRUM);                /* come out the same in either order) */
    SCOPE_DRUMS(scope_drums_mark);                      /* (the SCOPE on the drums: their part of the mix) */
    slicer_drums(mix_l, mix_r, send_r, n);
    tflt_drums(n);
    SCOPE_DRUMS(scope_drums_take);
    for (i = 0; i < NPART; i++)
        mix_part(&trk[i], n);
#else
    for (i = 0; i < NPART; i++)
        mix_part(&trk[i], n);
    drums.a0 = TDRUM->att;                              /* the drum track's mute / solo fade */
    drums.a1 = 32767 - gain_next(TDRUM);
    SCOPE_DRUMS(scope_drums_mark);                      /* (the SCOPE on the drums: their part of the mix) */
    slicer_drums(mix_l, mix_r, send_r, n);              /* drums_render, through the SLICER when on */
    SCOPE_DRUMS(scope_drums_take);
#endif
    mix_finish(out, n);
#if FELUCCA_MACROS
    FAR(mac_post)();                                    /* the authored values back */
#endif
}
#endif
