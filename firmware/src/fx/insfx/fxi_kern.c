/* SPDX-License-Identifier: GPL-3.0-only */
/* The limited insert types (2026-10-13): CHRS and ECHO, insert copies of the chorus and of a short delay for the tracks'
 * insert slots (fx_slots.c S1 S2), each with a builder count of instances (registry.h FELUCCA_INS_CHORUS,
 * FELUCCA_INS_DELAY: 0 not built .. NTRK). Included by insfx.c (the audio side); the main loop's side, which hands an
 * instance to a track holding the type: fxi.c.
 *   CHRS  a line of 1024 samples (23 ms, 2 KB of pool an instance), two taps around 7 ms moved half a turn apart by a
 *         ~0.8 Hz sine: a part (mono) gets (dry + the taps' mean) / 2; the drum bus feeds its mid and gets one tap on
 *         L, the other on R (stereo from the mid, as the chorus send). 1..64 its share, 64..127 its depth (2 .. 7.7 ms)
 *   ECHO  a line of 4096 samples (93 ms, 8 KB of pool an instance), one repeat at 75 ms added to the dry: 1..64 its
 *         level (0 .. 0.70), 64..127 its feedback (0 .. 0.60, rounded toward 0: a tail ends at exact 0); the drum bus
 *         feeds its mid, the echo goes to L and R
 * Each fades its wet share in and out over 2.9 ms (as insfx.c's). Starting from rest nothing the line held is read: a
 * count of the samples written since (fill) masks the rest, so a restart needs no clear in the ISR; fxi.c zeroes an
 * instance before it hands it out. Once the line holds only zeros and the input is silent the block is skipped (the
 * output is the input, bit for bit). No divide, no packed 16-bit SIMD.
 * An instance's line is a pointer (fxi.c sets it before it hands the instance out): its own pool line, or a slice of
 * its send's line lent while the send is in no master slot (fxi.c "Lending"; FXI_LND below). Its evict flag (fxi.c,
 * the line going back to the send) fades it out as at amount 0. */
#if INS_LIM
#define FXI_NL ((uint32_t)(FXT_LIMN - FXT_LIM0))        /* the limited types: CHRS ECHO */
#define FXI_NONE 0xFFu
_Static_assert(FELUCCA_INS_CHORUS >= 0 && FELUCCA_INS_CHORUS <= NTRK && FELUCCA_INS_DELAY >= 0 && FELUCCA_INS_DELAY <= NTRK,
               "an instance count: 0 .. a track each");
_Static_assert(FXT_LIM0 == FXT_LIMN - 2 && FXT_ECHO == FXT_CHRS + 1, "fxi: CHRS ECHO the two limited types");
static const uint8_t FXI_CAP[2] = {FELUCCA_INS_CHORUS, FELUCCA_INS_DELAY};   /* (by type, from FXT_LIM0) */
/* the instances a send's line can lend (fxi.c, "Lending"): CHRS in the chorus send's 2048 samples (2 lines of 1024),
 * ECHO in the delay send's (its lines of 4096: 4 at any DLY_LEN, 16384 the shortest), never more than the tracks left
 * after the builder's own (a type is once a track). Instance i of a type: its own pool line below FXI_CAP, then the
 * send's line's slices FXI_CAP .. FXI_CAP + FXI_LND - 1 */
#define FXL_MIN(a, b) ((a) < (b) ? (a) : (b))
#define FXL_N0 (FELUCCA_INS_CHORUS && FELUCCA_FX_CHORUS ? FXL_MIN(CHO_LEN / 1024u, NTRK - FELUCCA_INS_CHORUS) : 0u)
#define FXL_N1 (FELUCCA_INS_DELAY && FELUCCA_FX_DELAY ? FXL_MIN(DLY_LEN / 4096u, NTRK - FELUCCA_INS_DELAY) : 0u)
static const uint8_t FXI_LND[2] = {FXL_N0, FXL_N1};
#define FXI_TOT(j) ((uint32_t)FXI_CAP[j] + FXI_LND[j])
/* the instance each track's type plays on (the audio reads it once a block; fxi.c writes it with the interrupts off),
 * FXI_NONE: none (the type bypassed on that track) */
static volatile uint8_t fxi_at[NTRK][2] = {{FXI_NONE, FXI_NONE}, {FXI_NONE, FXI_NONE}, {FXI_NONE, FXI_NONE},
                                           {FXI_NONE, FXI_NONE}};

/* ---- CHRS */
#if FELUCCA_INS_CHORUS
#define IFX_CH_LEN 1024u
#define IFX_CH_BASE (309 << 8)                          /* the taps' centre, Q8 samples: 7 ms */
#define IFX_CH_RATE 53                                  /* ~0.8 Hz (IFX_INC) */
typedef struct {
    int32_t w;                                          /* the wet share (Q15) */
    uint32_t ph;                                        /* the LFO */
    uint16_t wp, fill, q;                               /* the write place, samples written since the start (<= LEN),
                                                         * zeros written in a row (<= LEN) */
    uint8_t a, on;                                      /* the last amount (a fade-out runs at it), running (0: at rest) */
    uint8_t evict;                                      /* its line goes back to the send (fxi.c): fading out, at 0 */
    int16_t *line;                                      /* IFX_CH_LEN samples: its own pool line, or a lent slice */
} ifx_ch_t;
static int16_t ifx_chrs_ln[FELUCCA_INS_CHORUS][IFX_CH_LEN] __attribute__((section(".pool")));
static ifx_ch_t ifx_chrs[FELUCCA_INS_CHORUS + FXL_N0] __attribute__((section(".pool")));   /* (main RAM is scarcer) */
/* AINL: a tap d (Q8 samples back) after the sample at wp was written; 0 past what was written since the start */
AINL int32_t ifx_ch_tap(const int16_t *ln, uint32_t wp, uint32_t fill, int32_t d)
{
    uint32_t i = (uint32_t)d >> 8;
    int32_t y0, y1;
    if (i + 1u >= fill)
        return 0;
    y0 = ln[(wp - i) & (IFX_CH_LEN - 1u)], y1 = ln[(wp - i - 1u) & (IFX_CH_LEN - 1u)];
    return y0 + (((y1 - y0) * (d & 255)) >> 8);
}
/* n samples: a part's b (r 0: mono), or the drum bus's l and r (fed their mid); sh: the headroom (a part's 3, the bus's 2) */
static HOT2 __attribute__((noinline)) void ifx_chorus(ifx_ch_t *s, int32_t a, int32_t *l, int32_t *r, uint32_t n, uint32_t sh)
{
    uint32_t i, wp, fill, q, inc = IFX_INC(IFX_CH_RATE);
    int32_t w = s->w, to, dep, s0, s1, pa, pb, da, db;
    if (s->evict)
        a = 0;                                          /* (its line goes back to the send: out, then at rest) */
    if (!a && !w) {
        s->on = 0;                                      /* (at rest: it starts again from a clean line) */
        return;
    }
    if (!s->on)
        s->fill = 0, s->q = 0, s->on = 1;               /* starting: what the line holds is not read */
    if (a)
        s->a = (uint8_t)a;
    to = a ? (a > 64 ? 64 : a) * 512 : 0;
    if (s->q >= IFX_CH_LEN && !fx_any(l, n) && (!r || !fx_any(r, n))) {
        s->w = to, s->ph += n * inc;                    /* (a clear line, silence in: silence out) */
        return;
    }
    dep = s->a > 64 ? 88 + (s->a - 64) * 4 : 88;        /* the sweep, samples: 2 .. 7.7 ms */
    s0 = osc_sine(s->ph);
    s->ph += n * inc;
    s1 = osc_sine(s->ph);
    pa = IFX_CH_BASE + (((s0 + 32768) * dep) >> 8), da = IFX_CH_BASE + (((s1 + 32768) * dep) >> 8) - pa;
    pb = IFX_CH_BASE + (((32767 - s0) * dep) >> 8), db = IFX_CH_BASE + (((32767 - s1) * dep) >> 8) - pb;
    wp = s->wp, fill = s->fill, q = s->q;
    for (i = 0; i < n; i++) {
        int32_t x = r ? (l[i] >> 1) + (r[i] >> 1) : l[i], v = clamp(x >> sh, -32767, 32767), ta, tb;
        s->line[wp] = (int16_t)v;
        q = v ? 0u : q < IFX_CH_LEN ? q + 1u : q;
        fill += fill < IFX_CH_LEN;
        ta = ifx_ch_tap(s->line, wp, fill, pa + ((da * (int32_t)i) >> CTL_LOG2));
        tb = ifx_ch_tap(s->line, wp, fill, pb + ((db * (int32_t)i) >> CTL_LOG2));
        wp = (wp + 1u) & (IFX_CH_LEN - 1u);
        if (fill > (IFX_CH_BASE >> 8) + 2u)             /* (starting: the fade-in once both taps read the input, no dip) */
            w += clamp(to - w, -IFX_SLOPE, IFX_SLOPE);
        if (r) {
            l[i] = ifx_mix(l[i], (l[i] >> 1) + (ta << (sh - 1u)), w);
            r[i] = ifx_mix(r[i], (r[i] >> 1) + (tb << (sh - 1u)), w);
        } else {
            l[i] = ifx_mix(x, (x >> 1) + ((ta + tb) << (sh - 2u)), w);
        }
    }
    s->wp = (uint16_t)wp, s->fill = (uint16_t)fill, s->q = (uint16_t)q, s->w = w;
}
#endif

/* ---- ECHO */
#if FELUCCA_INS_DELAY
#define IFX_EC_LEN 4096u
#define IFX_EC_D 3308u                                  /* 75 ms */
typedef struct {
    int32_t w;                                          /* the echo's level (Q15) */
    uint16_t wp, fill, q;                               /* (as CHRS's: fill and q <= IFX_EC_LEN) */
    uint8_t a, on, evict;                               /* (as CHRS's) */
    int16_t *line;                                      /* IFX_EC_LEN samples: its own pool line, or a lent slice */
} ifx_ec_t;
static int16_t ifx_echo_ln[FELUCCA_INS_DELAY][IFX_EC_LEN] __attribute__((section(".pool")));
static ifx_ec_t ifx_echo[FELUCCA_INS_DELAY + FXL_N1] __attribute__((section(".pool")));
static HOT2 __attribute__((noinline)) void ifx_echo_run(ifx_ec_t *s, int32_t a, int32_t *l, int32_t *r, uint32_t n, uint32_t sh)
{
    uint32_t i, wp, fill, q;
    int32_t w = s->w, to, fb;
    if (s->evict)
        a = 0;
    if (!a && !w) {
        s->on = 0;
        return;
    }
    if (!s->on)
        s->fill = 0, s->q = 0, s->on = 1;               /* starting: no old echo */
    if (a)
        s->a = (uint8_t)a;
    to = a ? (a > 64 ? 64 : a) * 360 : 0;               /* 0 .. 0.70 */
    fb = s->a > 64 ? (s->a - 64) * 312 : 0;             /* 0 .. 0.60, Q15 */
    if (s->q >= IFX_EC_D && !fx_any(l, n) && (!r || !fx_any(r, n))) {
        s->w = to;                                      /* (the next IFX_EC_D reads are zeros, silence in: silence out) */
        return;
    }
    wp = s->wp, fill = s->fill, q = s->q;
    for (i = 0; i < n; i++) {
        int32_t x = r ? (l[i] >> 1) + (r[i] >> 1) : l[i], rd, v, e;
        rd = fill >= IFX_EC_D ? s->line[(wp - IFX_EC_D) & (IFX_EC_LEN - 1u)] : 0;
        v = clamp((x >> sh) + mul_tz(rd, fb), -32767, 32767);
        s->line[wp] = (int16_t)v;
        q = v ? 0u : q < IFX_EC_LEN ? q + 1u : q;
        fill += fill < IFX_EC_LEN;
        wp = (wp + 1u) & (IFX_EC_LEN - 1u);
        w += clamp(to - w, -IFX_SLOPE, IFX_SLOPE);
        e = (rd * w) >> (15 - sh);                      /* (|rd| < 2^15, w <= 2^15: fits) */
        l[i] += e;
        if (r)
            r[i] += e;
    }
    s->wp = (uint16_t)wp, s->fill = (uint16_t)fill, s->q = (uint16_t)q, s->w = w;
}
#endif

/* track k's instance of limited type ty (FXI_NONE: none) */
AINL uint32_t ifx_lim_at(uint32_t ty, uint32_t k) { return fxi_at[k % NTRK][(ty - FXT_LIM0) & 1u]; }
/* its wet share now (0: none, or at rest) */
AINL int32_t ifx_lim_w(uint32_t ty, uint32_t k)
{
    uint32_t i = ifx_lim_at(ty, k);
    if (i == FXI_NONE)
        return 0;
#if FELUCCA_INS_CHORUS
    if (ty == FXT_CHRS)
        return ifx_chrs[i].w;
#endif
#if FELUCCA_INS_DELAY
    if (ty == FXT_ECHO)
        return ifx_echo[i].w;
#endif
    return 0;
}
/* track k's instance of type ty is fading out to give its lent line back (fxi.c): as off (a silent part: at rest) */
AINL int ifx_lim_ev(uint32_t ty, uint32_t k)
{
    uint32_t i;
    if (!FXT_IS_LIM(ty) || (i = ifx_lim_at(ty, k)) == FXI_NONE)
        return 0;
#if FELUCCA_INS_CHORUS
    if (ty == FXT_CHRS)
        return ifx_chrs[i].evict;
#endif
#if FELUCCA_INS_DELAY
    if (ty == FXT_ECHO)
        return ifx_echo[i].evict;
#endif
    return 0;
}
/* to rest at once (a part not heard: insfx.c ifx_quiet_part); it starts again from a clean line */
AINL void ifx_lim_rest(uint32_t ty, uint32_t k)
{
    uint32_t i = ifx_lim_at(ty, k);
    if (i == FXI_NONE)
        return;
#if FELUCCA_INS_CHORUS
    if (ty == FXT_CHRS)
        ifx_chrs[i].w = 0, ifx_chrs[i].on = 0;
#endif
#if FELUCCA_INS_DELAY
    if (ty == FXT_ECHO)
        ifx_echo[i].w = 0, ifx_echo[i].on = 0;
#endif
}
/* type ty on track k at amount a: a part's b (r 0), the drum bus's l and r; no instance: nothing (bypassed) */
static HOT2 __attribute__((noinline)) void ifx_lim_run(uint32_t ty, uint32_t k, int32_t a, int32_t *l, int32_t *r,
                                                       uint32_t n, uint32_t sh)
{
    uint32_t i = ifx_lim_at(ty, k);
    if (i == FXI_NONE)
        return;
#if FELUCCA_INS_CHORUS
    if (ty == FXT_CHRS && i < FELUCCA_INS_CHORUS + FXL_N0)
        ifx_chorus(&ifx_chrs[i], a, l, r, n, sh);
#endif
#if FELUCCA_INS_DELAY
    if (ty == FXT_ECHO && i < FELUCCA_INS_DELAY + FXL_N1)
        ifx_echo_run(&ifx_echo[i], a, l, r, n, sh);
#endif
    (void)a, (void)l, (void)r, (void)n, (void)sh;
}
#endif
