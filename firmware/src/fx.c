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
 * loop, one line slowly modulated (no metallic tone on long tails); left and right take different lines */
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
#if FELUCCA_REV_POOL             /* REV_POOL: the lines in the pool (main RAM is the scarcer); the same code */
#define REV_SECTION __attribute__((section(".pool")))
#else
#define REV_SECTION              /* (.bss) */
#endif
static const uint16_t REV_LINE[4] = {REV_N0, REV_N1, REV_N2, REV_N3};
static const uint16_t REV_AP[2] = {REV_A0, REV_A1};
static int16_t rev_line[FELUCCA_FX_REVERB ? REV_N0 + REV_N1 + REV_N2 + REV_N3 + REV_MOD + 2 : 1] REV_SECTION;
static int16_t rev_ap[FELUCCA_FX_REVERB ? REV_A0 + REV_A1 : 1] __attribute__((section(".pool")));
#define FX_Q_MAX 0x40000000u     /* (fx_q, below: the zero-write counts stop here) */
static struct {
    uint32_t dly_w, cho_w, cho_ph, rev_ph;
    int32_t dly_lp;
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
 * input reaches exactly 0: mul_tz, fx_step, half_ap below.) */
#define REV_LONGEST (REV_N3 > REV_N0 + REV_MOD + 2u ? REV_N3 : REV_N0 + REV_MOD + 2u)
#define REV_Q (REV_LONGEST * (FELUCCA_REV_HALF ? 2u : 1u))   /* the longest line, in output samples */
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

/* DIST: low cut -> drive (1x..8x, exponential) -> asymmetric soft clip
 * (a little bias = even harmonics) -> tone low-pass that closes with drive ->
 * make-up gain. State per part (track_t dist_*). */
static HOT void track_dist(track_t *t, int32_t *b, uint32_t n)
{
    int32_t d = fx_on(t) ? t->p[P_DIST] : 0, i, g, k, mk, bias = 2400, b0;   /* (bypassed: off, value kept) */
    if (!d) {
        t->dist_on = 0;
        return;
    }
    if (!t->dist_on) {                                  /* coming on: no stale high-pass state (a thump) */
        t->dist_on = 1;
        t->dist_hp = b[0];
        t->dist_lp1 = t->dist_lp2 = 0;
    }
    g = 4096 + d * d * 2;                                /* Q12: 1x .. ~9x, gentle at first */
    k = 32000 - d * 95;                                  /* tone: transparent at low drive .. ~3 kHz, Q15 */
    mk = 30000 - d * 120;                                /* make-up */
    b0 = softclip(bias);
    for (i = 0; i < (int32_t)n; i++) {
        int32_t x = b[i], y;
        t->dist_hp += (x - t->dist_hp + 64) >> 7;           /* ~55 Hz low cut: keep the bass out of the clipper */
        x = clamp(x - t->dist_hp, -230000, 230000);         /* (x >> 2) * g fits 32 bits; the clip is flat out there */
        y = softclip((((x >> 2) * g) >> 10) + bias) - b0;   /* >> 2 first: no overflow for loud poly */
        t->dist_lp1 += mulq15(y - t->dist_lp1, k);         /* two poles: tames the fizz */
        t->dist_lp2 += mulq15(t->dist_lp1 - t->dist_lp2, k);
        b[i] = mulq15(t->dist_lp2, mk);
    }
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
#include "bassplus.c"          /* the menu's LOWCUT: BASS+ for the small speaker (from Felucca 1.0) */
#endif
static inline HOT void master_out(int32_t *l, int32_t *r)
{
    int32_t al, ar, a;
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



AINL uint32_t delay_samples(void)
{
    uint32_t s = div_samples((uint32_t)song.g[G_DTIME]);
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

/* delay with a low-passed feedback (in the middle) */
FX_STEP int32_t dly_step(int32_t in, uint32_t dl, int32_t col, int32_t fb, int32_t dmix, int32_t *wr)
{
    int32_t x = dly_buf[(fx.dly_w - dl) & (DLY_LEN - 1u)], v;
    fx.dly_lp += fx_step(x - fx.dly_lp, col);
    v = clamp((in >> 1) + mul_tz(fx.dly_lp, fb), -32768, 32767);
    dly_buf[fx.dly_w & (DLY_LEN - 1u)] = (int16_t)v;
    *wr |= v;
    fx.dly_w++;
    return mulq15(x << 1, dmix);
}

/* reverb: two diffusers, then the four lines; r: line 0's read offset (Q8); returns left, *yr right */
#define REV_L0 (REV_N0 + REV_MOD + 2u)
#define REV_B1 REV_L0
#define REV_B2 (REV_B1 + REV_N1)
#define REV_B3 (REV_B2 + REV_N2)
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
        fx.line_lp[0] += fx_step(((s0 + s1) >> 1) - fx.line_lp[0], lpk);
        fx.line_lp[1] += fx_step(((d0 + d1) >> 1) - fx.line_lp[1], lpk);
        fx.line_lp[2] += fx_step(((s0 - s1) >> 1) - fx.line_lp[2], lpk);
        fx.line_lp[3] += fx_step(((d0 - d1) >> 1) - fx.line_lp[3], lpk);
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

#if FELUCCA_REV_HALF
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
/* one pair: in[0], in[1] -> the tank -> w*[0], w*[1] (added) */
FX_STEP void rev_half_pair(const int32_t *in, int32_t *wl, int32_t *wr, int32_t r, int32_t g, int32_t lpk,
                           int32_t *wv)
{
    uint32_t j = rev_half.j = (rev_half.j - 1u) & 7u, q = j & 3u;
    int32_t y, ol, orr, *e = rev_half.e + j, *l = rev_half.l + j, *rr = rev_half.r + j;
    e[0] = e[8] = in[1];
    rev_half.o[q] = rev_half.o[q + 4u] = in[0];
    y = (rev_hb(e) + (rev_half.o[q + 3u] << 9)) >> 10;
    ol = rev_step(y, r, g, lpk, &orr, wv);
    l[0] = l[8] = ol;
    rr[0] = rr[8] = orr;
    wl[0] += rev_hb(l) >> 9;
    wr[0] += rev_hb(rr) >> 9;
    wl[1] += l[3];
    wr[1] += rr[3];
}
/* a block of the bus (fx_buses' run_r); ma, mb: line 0's modulation at both ends */
FX_STEP void rev_half_run(const int32_t *rev_in, int32_t *wl, int32_t *wr, uint32_t n, int32_t ma, int32_t mb,
                          int32_t g, int32_t lpk, int32_t *wv)
{
    uint32_t i;
    int32_t k = ((lpk * (58847 - ((26198 * lpk) >> 15))) >> 15) + 519;   /* the damping at half the rate: */
                                                        /* -0.7995 lpk^2 + 1.7959 lpk + 0.0158 (see above) */
    k = k > 32767 ? 32767 : k;
    for (i = 0; i < n; i += 2u)
        rev_half_pair(rev_in + i, wl + i, wr + i, ma + (((mb - ma) * (int32_t)i) >> CTL_LOG2), g, k, wv);
}
FX_STEP void rev_half_skip(uint32_t n)                  /* idle: n output samples, n / 2 in the tank */
{
    n >>= 1;
    fx.ap_i[0] = fx_wrap(fx.ap_i[0], n, REV_AP[0]);
    fx.ap_i[1] = fx_wrap(fx.ap_i[1], n, REV_AP[1]);
    fx.line_i[0] = fx_wrap(fx.line_i[0], n, REV_L0);
    fx.line_i[1] = fx_wrap(fx.line_i[1], n, REV_LINE[1]);
    fx.line_i[2] = fx_wrap(fx.line_i[2], n, REV_LINE[2]);
    fx.line_i[3] = fx_wrap(fx.line_i[3], n, REV_LINE[3]);
}
#define REV_HALF_BUSY() rev_half_any()
#else
#define REV_HALF_BUSY() 0
#endif

#if FELUCCA_SPRING
#include "spring.c"            /* REVERB > TYPE SPRING (from Felucca 1.0) */
#endif

/* process the three buses for one block; sends in, wet out (stereo). The LFOs (chorus, reverb line)
 * are computed per block and ramped: no sine per sample. Each bus runs in its own loop; an idle one
 * (see above) is skipped. Returns 0 when every bus was idle: wet_l / wet_r were not written (they would
 * be all 0; FELUCCA_SKIP), else 1 */
static HOT int fx_buses(const int32_t *cho_in, const int32_t *dly_in, const int32_t *rev_in, int32_t *wet_l,
                     int32_t *wet_r, uint32_t n)
{
    uint32_t i, dl;
    int32_t fb = song.g[G_DFDBK] * 230, col = 2000 + song.g[G_DCOLOR] * 240;
    int32_t dmix = song.g[G_DMIX] * 258;
    int32_t g = 17000 + song.g[G_RSIZE] * 104, lpk = 32767 - song.g[G_RDAMP] * 200;   /* loop gain (RT60 ~0.4..4 s), damping */
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
#define CHO_R0 (ca0 + dca * (int32_t)i)
#define CHO_R1 (cb0 + dcb * (int32_t)i)
#define REV_R (ma + (((mb - ma) * (int32_t)i) >> CTL_LOG2))   /* (between the two: never below 0) */
    /* (the input scan only once the bus' lines are clear) */
    run_c = FELUCCA_FX_CHORUS && (fx.cho_q < CHO_LEN || fx_any(cho_in, n));   /* (registry.h: an FX not built) */
    run_d = FELUCCA_FX_DELAY && (fx.dly_q < DLY_LEN || fx.dly_lp || fx_any(dly_in, n));
    run_r = FELUCCA_FX_REVERB && (fx.rev_q < REV_Q || (fx.line_lp[0] | fx.line_lp[1] | fx.line_lp[2] | fx.line_lp[3]) ||
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
        dl = delay_samples();                           /* (its divide: only for a running delay) */
        for (i = 0; i < n; i++) {
            x = dly_step(dly_in[i], dl, col, fb, dmix, &wd);
            wet_l[i] += x;
            wet_r[i] += x;
        }
    } else {
        fx.dly_w += n;
    }
#if FELUCCA_SPRING
    spring_bus(rev_in, wet_l, wet_r, n, ma, mb, g, lpk, run_r, &wv);   /* ROOM / SPRING (spring.c) */
#elif FELUCCA_REV_HALF
    if (run_r)
        rev_half_run(rev_in, wet_l, wet_r, n, ma, mb, g, lpk, &wv);
    else
        rev_half_skip(n);
#else
    if (run_r) {
        for (i = 0; i < n; i++) {
            int32_t rr;
            wet_l[i] += rev_step(rev_in[i], REV_R, g, lpk, &rr, &wv);
            wet_r[i] += rr;
        }
    } else {
        fx.ap_i[0] = fx_wrap(fx.ap_i[0], n, REV_AP[0]);       /* (n <= CTL: shorter than every line) */
        fx.ap_i[1] = fx_wrap(fx.ap_i[1], n, REV_AP[1]);
        fx.line_i[0] = fx_wrap(fx.line_i[0], n, REV_L0);
        fx.line_i[1] = fx_wrap(fx.line_i[1], n, REV_LINE[1]);
        fx.line_i[2] = fx_wrap(fx.line_i[2], n, REV_LINE[2]);
        fx.line_i[3] = fx_wrap(fx.line_i[3], n, REV_LINE[3]);
    }
#endif
#undef CHO_R0
#undef CHO_R1
#undef REV_R
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
    else if ((!t->tail || !t->p[P_DIST] || !fx_on(t) || !--t->tail) && !slicer_busy(t)) {
        slicer_track(t, 0, n);                          /* (the SLICER's step clock runs on) */
#if FELUCCA_GLIDE
        t->gl_on = 0;                                   /* silent: the gains settle at once */
#endif
        return;
    }
    if (!g0 && !g1) {                                   /* silent (MUTE / SOLO): the voices run, nothing is heard */
        slicer_track(t, 0, n);
#if FELUCCA_GLIDE
        t->gl_on = 0;
#endif
        return;
    }
    {
        int32_t lvl = LEVEL_Q12[t->p[P_LEVEL] ? clamp(t->p[P_LEVEL] + t->p[P_ED_FX], 1, 127) : 0], pan = t->p[P_PAN];   /* (+ the sound's trim: 1/2 dB steps, as LEVEL's) */
        int32_t gl = 4096 - (pan > 0 ? pan * 64 : 0), gr = 4096 + (pan < 0 ? pan * 64 : 0);
        int32_t on = fx_on(t), pk = t->peak;          /* FX bypass: no sends (the buses' tails ring out) */
        int32_t c = on ? t->p[P_CHOR] * 258 : 0, d = on ? t->p[P_DLY] * 258 : 0, r = on ? t->p[P_REV] * 258 : 0;
        int32_t xmax = c > d ? c : d;
        int32_t ga = mulq15(g0, duck.g0), gb = mulq15(g1, duck.g1);   /* mute x duck, ramped over the block */
        xmax = 0x7FFFFFFF / ((xmax > r ? xmax : r) | 1);   /* sends: loud chords at a high LEVEL */
        if (FELUCCA_FX_DIST)
            track_dist(t, b, n);
        slicer_track(t, b, n);                          /* slicer.c: before the level, pan and sends */
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

#include "punch.c"            /* PUNCH-IN FX on the whole mix (FX held + a white key) */
static int32_t master_cur = -1;                        /* the volume knob, ramped per sample (no zipper) */
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
#if FELUCCA_GLIDE
    m0 = master_cur < 0 ? (int32_t)song.master_q12 : master_cur;   /* MASTER glides (~10 ms; X0X 0.10.1) */
    m1 = glide_next(m0, (int32_t)song.master_q12);
#else
    m1 = (int32_t)song.master_q12;
    m0 = master_cur < 0 ? m1 : master_cur;
#endif
    master_cur = m1;
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
    for (i = 0; i < NPART; i++)
        mix_part(&trk[i], n);
    drums.a0 = TDRUM->att;                              /* the drum track's mute / solo fade */
    drums.a1 = 32767 - gain_next(TDRUM);
    slicer_drums(mix_l, mix_r, send_r, n);              /* drums_render, through the SLICER when on */
    mix_finish(out, n);
#if FELUCCA_MACROS
    FAR(mac_post)();                                    /* the authored values back */
#endif
}
#endif
