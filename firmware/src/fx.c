/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Effects: per-track DIST insert, then sends into three shared buses (chorus, tempo delay, reverb).
 * Stereo dry mix; the chorus and the reverb come back in stereo, the delay in the middle. */
#define DLY_LEN 65536u           /* 1.49 s: 1/4 at 40 BPM fits */
#define CHO_LEN 2048u
static int16_t dly_buf[DLY_LEN] __attribute__((section(".pool")));
static int16_t cho_buf[CHO_LEN] __attribute__((section(".pool")));
/* the reverb: two input diffusers, then four delay lines mixed by a Hadamard matrix (a feedback delay
 * network: every echo feeds all four, so it thickens instead of ringing like a comb), damped in the
 * loop, one line slowly modulated (no metallic tone on long tails); left and right take different lines */
#define REV_MOD 12               /* samples the modulated line moves (+-) */
static const uint16_t REV_LINE[4] = {1559, 1931, 2389, 2791};   /* 35..63 ms, coprime */
static const uint16_t REV_AP[2] = {556, 441};
static int16_t rev_line[1559 + 1931 + 2389 + 2791 + REV_MOD + 2];   /* (.bss: the pool is full) */
static int16_t rev_ap[556 + 441] __attribute__((section(".pool")));
static struct {
    uint32_t dly_w, cho_w, cho_ph, rev_ph;
    int32_t dly_lp;
    uint16_t line_i[4], ap_i[2];
    int32_t line_lp[4];
    uint32_t cho_q, dly_q, rev_q;   /* samples since the bus last wrote a non-zero value into its lines */
} fx;

/* An idle bus is skipped only when its output is exactly 0 and stays 0, never on a threshold: every
 * tail rings out to the last LSB. A bus is idle when (1) its send block is all 0, (2) for at least its
 * longest line it has written nothing but 0, so every line and diffuser cell holds 0, and (3) its
 * filters are at 0. From that state a 0 input writes 0, reads 0 and outputs 0, so skipping a block
 * only has to move the write / read indices on (the LFOs move on per block anyway). Resuming from it is
 * bit-identical to never having skipped. (The rounding of mulq15 may hold a tail at -1 for ever: then
 * the bus keeps running, as before.) */
#define FX_Q_MAX 0x40000000u
#define REV_Q (2791u > 1559u + REV_MOD + 2u ? 2791u : 1559u + REV_MOD + 2u)   /* the longest line */
static inline uint32_t fx_q(uint32_t q, int32_t wrote, uint32_t n)   /* the zero-write count after a block */
{
    return wrote ? 0u : q < FX_Q_MAX ? q + n : q;
}
static inline uint16_t fx_wrap(uint32_t i, uint32_t n, uint32_t len)   /* (i + n) mod len, n < len */
{
    i += n;
    return (uint16_t)(i >= len ? i - len : i);
}
static inline int32_t fx_any(const int32_t *x, uint32_t n)   /* any non-zero sample */
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
static void track_dist(track_t *t, int32_t *b, uint32_t n)
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
static inline int32_t dc_block(int32_t x, int32_t *dc, int32_t *err)
{
    int32_t e = (x << 6) - *dc + *err, d = e >> 12;
    *err = e - (d << 12);
    *dc += d;
    return x - ((*dc + 32) >> 6);
}

static int32_t lce[4];
static inline int32_t lowcut1(int32_t x, int32_t *lc, int32_t *err)   /* x minus its one-pole low-pass */
{
    int32_t e = x - *lc + *err, d = e >> 6;
    *err = e - (d << 6);
    *lc += d;
    return x - *lc;
}

/* the output stage: linear up to KNEE (a clean low end: no tanh harmonics on a loud sine), above it
 * a tanh knee with the same slope at the joint, to full scale */
#define KNEE 16384
static inline int32_t knee(int32_t x)
{
    int32_t a = x < 0 ? -x : x;
    if (a <= KNEE)
        return x;
    a = KNEE + (softclip((a - KNEE) * 2) >> 1);
    return x < 0 ? -a : a;
}

static inline void master_out(int32_t *l, int32_t *r)
{
    int32_t al, ar, a;
    *l = dc_block(*l, &dc_l, &dce_l);
    *r = dc_block(*r, &dc_r, &dce_r);
    if (fx_lowcut) {                  /* two one-pole high-passes, error feedback as dc_block (the */
        *l = lowcut1(*l, &lc_l1, &lce[0]);          /* rounded step stopped at |x - lc| < 32: an offset) */
        *l = lowcut1(*l, &lc_l2, &lce[1]);
        *r = lowcut1(*r, &lc_r1, &lce[2]);
        *r = lowcut1(*r, &lc_r2, &lce[3]);
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



static uint32_t delay_samples(void)
{
    uint32_t s = div_samples((uint32_t)song.g[G_DTIME]);
    return s < 16u ? 16u : s >= DLY_LEN ? DLY_LEN - 1u : s;
}

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
    fx.dly_lp += mulq15(x - fx.dly_lp, col);
    v = clamp((in >> 1) + mulq15(fx.dly_lp, fb), -32768, 32767);
    dly_buf[fx.dly_w & (DLY_LEN - 1u)] = (int16_t)v;
    *wr |= v;
    fx.dly_w++;
    return mulq15(x << 1, dmix);
}

/* reverb: two diffusers, then the four lines; r: line 0's read offset (Q8); returns left, *yr right */
#define REV_L0 (1559u + REV_MOD + 2u)
#define REV_B1 REV_L0
#define REV_B2 (REV_B1 + 1931u)
#define REV_B3 (REV_B2 + 2389u)
FX_STEP int32_t rev_step(int32_t in, int32_t r, int32_t g, int32_t lpk, int32_t *yr, int32_t *wr)
{
    int32_t a = mulq15(in, 13000), o0, o1, o2, o3;
    uint32_t k;
    {
        int16_t *c = rev_ap;
        for (k = 0; k < 2u; k++) {
            int32_t b = c[fx.ap_i[k]], v = a + (b >> 1), w = clamp(v, -32768, 32767);
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
        fx.line_lp[0] += mulq15(((s0 + s1) >> 1) - fx.line_lp[0], lpk);
        fx.line_lp[1] += mulq15(((d0 + d1) >> 1) - fx.line_lp[1], lpk);
        fx.line_lp[2] += mulq15(((s0 - s1) >> 1) - fx.line_lp[2], lpk);
        fx.line_lp[3] += mulq15(((d0 - d1) >> 1) - fx.line_lp[3], lpk);
        w0 = clamp(mulq15(fx.line_lp[0], g) + a, -32768, 32767);
        w1 = clamp(mulq15(fx.line_lp[1], g) - a, -32768, 32767);
        w2 = clamp(mulq15(fx.line_lp[2], g) + a, -32768, 32767);
        w3 = clamp(mulq15(fx.line_lp[3], g) - a, -32768, 32767);
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

/* process the three buses for one block; sends in, wet out (stereo). The LFOs (chorus, reverb line)
 * are computed per block and ramped: no sine per sample. Each bus runs in its own loop; an idle one
 * (see above) is skipped. */
static void fx_buses(const int32_t *cho_in, const int32_t *dly_in, const int32_t *rev_in, int32_t *wet_l,
                     int32_t *wet_r, uint32_t n)
{
    uint32_t i, dl = delay_samples();
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
    run_c = fx.cho_q < CHO_LEN || fx_any(cho_in, n);
    run_d = fx.dly_q < DLY_LEN || fx.dly_lp || fx_any(dly_in, n);
    run_r = fx.rev_q < REV_Q || (fx.line_lp[0] | fx.line_lp[1] | fx.line_lp[2] | fx.line_lp[3]) || fx_any(rev_in, n);
    if (run_c) {
        for (i = 0; i < n; i++) {
            wet_l[i] = cho_step(cho_in[i], CHO_R0, CHO_R1, &yr, &wc);
            wet_r[i] = yr;
        }
    } else {                                            /* idle: every cell holds 0, the output is 0 */
        for (i = 0; i < n; i++)
            wet_l[i] = wet_r[i] = 0;
        fx.cho_w += n;
    }
    if (run_d) {
        for (i = 0; i < n; i++) {
            x = dly_step(dly_in[i], dl, col, fb, dmix, &wd);
            wet_l[i] += x;
            wet_r[i] += x;
        }
    } else {
        fx.dly_w += n;
    }
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
#undef CHO_R0
#undef CHO_R1
#undef REV_R
    fx.cho_q = fx_q(fx.cho_q, wc, n);
    fx.dly_q = fx_q(fx.dly_q, wd, n);
    fx.rev_q = fx_q(fx.rev_q, wv, n);
}

/* one block of the whole mix (shared with tests/hostsim.c): events -> each part
 * -> dist -> SLICER -> level / pan / sends -> drums (-> SLICER) -> buses -> master; out: stereo Q15 */
static void events_block(uint32_t n);                    /* seq.c */
static int32_t send_c[CTL], send_d[CTL], send_r[CTL], wet_l[CTL], wet_r[CTL], mix_l[CTL], mix_r[CTL], part_buf[CTL];

/* ---- mute / solo: a track that goes silent fades out over ~6 ms (and back in) */
#define MUTE_STEP 4096                                  /* Q15 per block: 8 blocks */
static int32_t gain_next(track_t *t)                    /* the track's mute gain at the end of this block */
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

static void duck_block(uint32_t adv)
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

/* one synth part into the dry mix and the sends; a part with no voice sounding costs
 * the LFO tick and a cleared buffer only (after the DIST tail has run out) */
static void mix_part(track_t *t, uint32_t n)
{
    int32_t *b = part_buf;
    uint32_t i;
    int32_t g0 = 32767 - t->att, g1 = gain_next(t);
    if (track_render(t, b, n))
        t->tail = 16;                                   /* blocks of DIST state to run out after the last voice */
    else if ((!t->tail || !t->p[P_DIST] || !fx_on(t) || !--t->tail) && !slicer_busy(t)) {
        slicer_track(t, 0, n);                          /* (the SLICER's step clock runs on) */
        return;
    }
    if (!g0 && !g1) {                                   /* silent (MUTE / SOLO): the voices run, nothing is heard */
        slicer_track(t, 0, n);
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
        track_dist(t, b, n);
        slicer_track(t, b, n);                          /* slicer.c: before the level, pan and sends */
        int32_t lvl0 = t->lvl ? t->lvl : lvl, dl = (lvl - lvl0) >> CTL_LOG2;   /* a new sound's trim: ramped */
        t->lvl = lvl;
        for (i = 0; i < n; i++) {
            int32_t x = ((b[i] >> 2) * (lvl0 + dl * (int32_t)i)) >> 10, a;   /* pre-shift: 8 loud voices */
            int32_t xs, g = ga + (((gb - ga) * (int32_t)i) >> CTL_LOG2);
            if (g < 32767)
                x = (x >> 4) * (g >> 3) >> 8;           /* (Q15 in two halves: no 32-bit overflow) */
            a = x < 0 ? -x : x;
            xs = clamp(x, -xmax, xmax);                 /* sends: mulq15 would overflow */
            if (a > pk)
                pk = a;
            if (c)
                send_c[i] += mulq15(xs, c);
            if (d)
                send_d[i] += mulq15(xs, d);
            if (r)
                send_r[i] += mulq15(xs, r);
            mix_l[i] += ((x >> 4) * gl) >> 8;           /* (x may pass 2^19: >> 4 first) */
            mix_r[i] += ((x >> 4) * gr) >> 8;
        }
        t->peak = pk;
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

static int32_t crush_bits(int32_t v, int32_t shift)    /* fewer bits, rounded toward 0: no DC from tails */
{
    return v >= 0 ? (v >> shift) << shift : -((-v >> shift) << shift);
}

static void dust_process(int32_t *l, int32_t *r, uint32_t n)
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
                dust.hl = crush_bits(dust.hl, shift);
                dust.hr = crush_bits(dust.hr, shift);
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

static void djf_process(int32_t *l, int32_t *r, uint32_t n)
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
static void mix_block(int32_t *out, uint32_t n)
{
    uint32_t i;
    int32_t m0, m1;
    for (i = 0; i < n; i++)
        send_c[i] = send_d[i] = send_r[i] = mix_l[i] = mix_r[i] = 0;
    events_block(n);
    duck_block(n * (uint32_t)song.g[G_BPM]);
    for (i = 0; i < NPART; i++)
        mix_part(&trk[i], n);
    drums.a0 = TDRUM->att;                              /* the drum track's mute / solo fade */
    drums.a1 = 32767 - gain_next(TDRUM);
    slicer_drums(mix_l, mix_r, send_r, n);              /* drums_render, through the SLICER when on */
    fx_buses(send_c, send_d, send_r, wet_l, wet_r, n);
    for (i = 0; i < n; i++) {
        mix_l[i] += wet_l[i];
        mix_r[i] += wet_r[i];
    }
    dust_process(mix_l, mix_r, n);
    punch_process(mix_l, mix_r, n);
    djf_process(mix_l, mix_r, n);
    m1 = (int32_t)song.master_q12;
    m0 = master_cur < 0 ? m1 : master_cur;
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
