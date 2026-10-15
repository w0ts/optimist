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
/* The limited types by index j (FXI_J): 0 CHRS, 1 ECHO, 2 JUNO CHORUS (2026-10-14), 3 FLNG (2026-10-15, fx-pool: its 2 x 256
 * sample lines were one copy per owner, 5.3 KB; now a builder count). The owners of an instance (fxi.c, FXI_O*): the tracks
 * 0 .. NTRK - 1, the master (its insert slots), the USB input track, the returns R1 R2 R3 (fx_slots.c rt_slot) */
#define FXI_NL 4u                                       /* (the tables hold every type; a type not built: a count of 0) */
#define FXI_J(t) ((t) == FXT_CHRS ? 0u : (t) == FXT_ECHO ? 1u : (t) == FXT_JUNO ? 2u : 3u)   /* (a limited type t's index) */
static const uint8_t FXI_TYS[FXI_NL] = {FXT_CHRS, FXT_ECHO, FXT_JUNO, FXT_FLNG};
#define FXI_TY(j) ((uint32_t)FXI_TYS[(j) % FXI_NL])     /* (index j's type) */
#define FXI_MST NTRK                                    /* the owners: T1 T2 T3 DR, then these */
#define FXI_UT (NTRK + 1u)
#define FXI_RT0 (NTRK + 2u)
#define FXI_NK (NTRK + 5u)
#define FXI_NONE 0xFFu
_Static_assert(FELUCCA_INS_CHORUS >= 0 && FELUCCA_INS_CHORUS <= NTRK && FELUCCA_INS_DELAY >= 0 && FELUCCA_INS_DELAY <= NTRK &&
               FELUCCA_INS_JCHORUS >= 0 && FELUCCA_INS_JCHORUS <= NTRK && FELUCCA_INS_FLANGER >= 0 && FELUCCA_INS_FLANGER <= NTRK,
               "an instance count: 0 .. NTRK");
static const uint8_t FXI_CAP[FXI_NL] = {FELUCCA_INS_CHORUS, FELUCCA_INS_DELAY, FELUCCA_INS_JCHORUS, FELUCCA_INS_FLANGER};   /* (by j) */
/* the instances a send's line can lend (fxi.c, "Lending"): CHRS in the chorus send's 2048 samples (2 lines of 1024),
 * ECHO in the delay send's (its lines of 4096: up to 4 here). Instance i of a type: its own pool line below FXI_CAP, then
 * the send's line's slices FXI_CAP .. FXI_CAP + FXI_LND - 1 */
#define FXL_MIN(a, b) ((a) < (b) ? (a) : (b))
#define FXL_N0 (FELUCCA_INS_CHORUS && FELUCCA_FX_CHORUS ? FXL_MIN(CHO_LEN / 1024u, NTRK - FELUCCA_INS_CHORUS) : 0u)
#define FXL_N1 (FELUCCA_INS_DELAY && FELUCCA_FX_DELAY ? FXL_MIN(DLY_LEN / 4096u, NTRK - FELUCCA_INS_DELAY) : 0u)
static const uint8_t FXI_LND[FXI_NL] = {FXL_N0, FXL_N1, 0, 0};   /* (JUNO CHORUS, FLNG: their own lines only) */
#define FXI_TOT(j) ((uint32_t)FXI_CAP[j] + FXI_LND[j])
#define FXI_IMAX (2u * NTRK)                            /* (the most instances of a type: its count and its lent lines) */
/* the instance each owner's type plays on (the audio reads it once a block; fxi.c writes it with the interrupts off),
 * FXI_NONE: none (the type bypassed there); fxi_init sets every entry FXI_NONE before the audio starts */
static volatile uint8_t fxi_at[FXI_NK][FXI_NL] = {
    {FXI_NONE, FXI_NONE, FXI_NONE, FXI_NONE}, {FXI_NONE, FXI_NONE, FXI_NONE, FXI_NONE}, {FXI_NONE, FXI_NONE, FXI_NONE, FXI_NONE},
    {FXI_NONE, FXI_NONE, FXI_NONE, FXI_NONE}, {FXI_NONE, FXI_NONE, FXI_NONE, FXI_NONE}, {FXI_NONE, FXI_NONE, FXI_NONE, FXI_NONE},
    {FXI_NONE, FXI_NONE, FXI_NONE, FXI_NONE}, {FXI_NONE, FXI_NONE, FXI_NONE, FXI_NONE}, {FXI_NONE, FXI_NONE, FXI_NONE, FXI_NONE}};
_Static_assert(NTRK == 4u && FXI_NK == 9u, "fxi_at: T1 T2 T3 DR, the master, the USB input, R1 R2 R3");
/* the audio's index k of an owner (insfx.c: a part, the drum bus IFX_BUS, IFX_MST, IFX_UT, IFX_RT(j)) -> the owner */
#define fxi_own_of(k) ifx_owner(k)                      /* (fx_slots.c: the same numbering) */
AINL uint32_t fxi_aix(uint32_t o)                       /* an owner -> the audio's index (its L for a stereo one) */
{
    return o < NTRK ? o : o == FXI_MST ? IFX_MST : o == FXI_UT ? IFX_UT : IFX_RT(o - FXI_RT0);
}

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

/* ---- JUNO CHORUS (fx/junocho/juno_cho.h): a 1024-sample line (2 KB of pool an instance) and its state; MIX fades in and out
 * over ~11 ms (its step a block: 2048 of 127 << 8), MODE the owner's (a track's P_TJMODE, the master's mst_jmode). A part
 * (mono) gets the mid, its side into ifx_jside (mixed after its level as WIDE's: fx.c mix_part); the drum bus and the
 * master their L and R, the line fed the mid. From rest the line is zeroed (a word loop, once). Runs from flash (XIP) */
#if FELUCCA_INS_JCHORUS
#include "../junocho/juno_cho.h"
typedef struct {
    juno_cho_t c;
    int32_t w;                                          /* the MIX now, << 8 */
    uint8_t on, evict;                                  /* running (0: at rest); evict: unused (no lent line), kept 0 */
} ifx_jc_t;
static ifx_jc_t ifx_jcho[FELUCCA_INS_JCHORUS] __attribute__((section(".pool"), aligned(4)));
#define IFX_JC_STEP 2048                                /* the MIX's step a block (Q8) */
static __attribute__((noinline)) void ifx_jchorus(ifx_jc_t *s, uint32_t k, int32_t a, int32_t *l, int32_t *r, uint32_t n,
                                                       uint32_t sh)
{
    int32_t to = a << 8, w = s->w;
    uint32_t mode = (uint32_t)(k < NTRK ? trk[k].p[P_TJMODE] : mst_jmode) % 3u;   /* (a stereo owner but the drum bus: the
                                                        * master's MODE) */
    if (!a && !w) {
        s->on = 0;
        if (!r)
            ifx_jsd[k % NPART] = 0;                     /* (a part: no side) */
        return;
    }
    if (!s->on) {
        ifx_zero(&s->c, sizeof s->c);                   /* (starting: a clean line) */
        s->on = 1;
    }
    w += clamp(to - w, -IFX_JC_STEP, IFX_JC_STEP);
    s->w = w;
    if (r)
        juno_cho_st(&s->c, mode, (uint32_t)w >> 8, l, r, n, sh);
    else {
        juno_cho_ms(&s->c, mode, (uint32_t)w >> 8, l, ifx_jside[k % NPART], n, sh);
        ifx_jsd[k % NPART] = 1;
    }
}
#endif

/* owner k's instance of limited type ty (FXI_NONE: none); k: a track, or the master (IFX_MST) */
AINL uint32_t ifx_lim_at(uint32_t ty, uint32_t k) { return fxi_at[fxi_own_of(k)][FXI_J(ty)]; }
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
#if FELUCCA_INS_JCHORUS
    if (ty == FXT_JUNO)
        return ifx_jcho[i].w;
#endif
#if FELUCCA_INS_FLANGER
    if (ty == FXT_FLNG)
        return ifx_flng[i].w;
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
    return 0;                                           /* (JUNO CHORUS: never on a lent line) */
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
#if FELUCCA_INS_JCHORUS
    if (ty == FXT_JUNO) {
        ifx_jcho[i].w = 0, ifx_jcho[i].on = 0;
        if (k < NPART)
            ifx_jsd[k] = 0;
    }
#endif
#if FELUCCA_INS_FLANGER
    if (ty == FXT_FLNG)
        ifx_flng[i].w = 0;                              /* (its lines: cleared when it starts again) */
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
#if FELUCCA_INS_JCHORUS
    if (ty == FXT_JUNO && i < FELUCCA_INS_JCHORUS)
        FAR(ifx_jchorus)(&ifx_jcho[i], k, a, l, r, n, sh);   /* (flash code, XIP: the RAM text has no room) */
#endif
#if FELUCCA_INS_FLANGER
    if (ty == FXT_FLNG && i < FELUCCA_INS_FLANGER)
        ifx_flanger(&ifx_flng[i], a, l, r, n, sh);
#endif
    (void)a, (void)l, (void)r, (void)n, (void)sh;
}
#endif
