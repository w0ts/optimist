/* SPDX-License-Identifier: GPL-3.0-only */
/* The limited insert types (2026-10-13): ECHO, an insert copy of a short delay for the insert slots (fx_slots.c S1 S2),
 * JUNO CHORUS, FLNG (insfx.c) and XDLY (xdly.c), each a builder item (registry.h FELUCCA_INS_*: built or not), and the
 * CHO DLY REV inserts (fxi_send.c: built with their effect; CHO took CHRS's place, 2026-10-29). Each instance's line is in the effects memory (fxi_mem.h, 2026-10-23): how many play at
 * once is what the memory holds. Included by insfx.c (the audio side); the main loop's side, which places an instance's
 * line and hands it to an owner holding the type: fxi.c.
 *   ECHO  a line of 4096 samples (93 ms, 8 KB), one repeat at 75 ms added to the dry: 1..64 its
 *         level (0 .. 0.70), 64..127 its feedback (0 .. 0.60, rounded toward 0: a tail ends at exact 0); the drum bus
 *         feeds its mid, the echo goes to L and R
 * Each fades its wet share in and out over 2.9 ms (as insfx.c's). Starting from rest nothing the line held is read: a
 * count of the samples written since (fill) masks the rest, so a restart needs no clear in the ISR; fxi.c zeroes an
 * instance before it hands it out. Once the line holds only zeros and the input is silent the block is skipped (the
 * output is the input, bit for bit). No divide, no packed 16-bit SIMD.
 * An instance's line is a pointer (fxi.c sets it before it hands the instance out): units of the effects memory, its own
 * room or a send's line lent while the send is in no master slot (fxi.c "Lending"). Its evict flag (fxi.c, the line
 * going back to the send) fades it out as at amount 0. */
#if INS_LIM
/* The limited types by index j (FXI_J): 0 CHO (CHRS's until 2026-10-29), 1 ECHO, 2 JUNO CHORUS (2026-10-14), 3 FLNG
 * (2026-10-15), 4 XDLY (2026-10-22), 5 REV, 6 DLY (2026-10-29: fxi_send.c). The owners of an instance (fxi.c, FXI_O*): the tracks 0 .. NTRK - 1, the master (its insert slots), the
 * USB input track, the returns A B C (fx_slots.c rt_slot) */
#define FXI_NL 7u                                       /* (the tables hold every type; a type not built: 0 instances) */
#define FXI_J(t) ((t) == FXT_CHO ? 0u : (t) == FXT_ECHO ? 1u : (t) == FXT_JUNO ? 2u : (t) == FXT_FLNG ? 3u : \
                  (t) == FXT_REV ? 5u : (t) == FXT_DLY ? 6u : 4u)
static const uint8_t FXI_TYS[FXI_NL] = {FXT_CHO, FXT_ECHO, FXT_JUNO, FXT_FLNG, FXT_XDLY, FXT_REV, FXT_DLY};
#define FXI_TY(j) ((uint32_t)FXI_TYS[(j) % FXI_NL])     /* (index j's type) */
#define FXI_MST NTRK                                    /* the owners: T1 T2 T3 DR, then these (FXI_NK: fxi_mem.h) */
#define FXI_UT (NTRK + 1u)
#define FXI_RT0 (NTRK + 2u)
#define FXI_NONE 0xFFu
/* each type's instance table (fxi_mem.h FXI_TN*: an owner each at most, no more than the memory holds) and its line's
 * units (by j) */
static const uint8_t FXI_TN[FXI_NL] = {FXI_TN0, FXI_TN1, FXI_TN2, FXI_TN3, FXI_TN4, FXI_TN5, FXI_TN6};
static const uint8_t FXA_SZS[FXI_NL] = {FXA_Z0, FXA_Z1, FXA_Z2, FXA_Z3, FXA_Z4, FXA_Z5, FXA_Z6};
#if FXZ_XDL_N > 1u                                       /* (XDLY: FX MEMORY HALF, fx_memsz.h) */
#define FXA_SZ(j) ((j) % FXI_NL == 4u ? FXA_Z4 >> fxsz_xdl : (uint32_t)FXA_SZS[(j) % FXI_NL])
#else
#define FXA_SZ(j) ((uint32_t)FXA_SZS[(j) % FXI_NL])
#endif
_Static_assert(FXA_Z4 <= 255u, "FXA_SZS: a byte");
#define FXI_IMAX FXI_NK                                 /* (the most instances of a type) */
static uint16_t fxa_u[FXI_NL][FXI_IMAX] IN_POOL;      /* each instance's first unit (fxi.c; the main loop writes it) */
/* the instance each owner's type plays on (the audio reads it once a block; fxi.c writes it with the interrupts off),
 * FXI_NONE: none (the type bypassed there); fxi_init sets every entry FXI_NONE before the audio starts */
#define FXI_AT0 {FXI_NONE, FXI_NONE, FXI_NONE, FXI_NONE, FXI_NONE, FXI_NONE, FXI_NONE}
static volatile uint8_t fxi_at[FXI_NK][FXI_NL] = {FXI_AT0, FXI_AT0, FXI_AT0, FXI_AT0, FXI_AT0, FXI_AT0, FXI_AT0, FXI_AT0, FXI_AT0};
_Static_assert(NTRK == 4u && FXI_NK == 9u, "fxi_at: T1 T2 T3 DR, the master, the USB input, A B C");
/* the audio's index k of an owner (insfx.c: a part, the drum bus IFX_BUS, IFX_MST, IFX_UT, IFX_RT(j)) -> the owner */
#define fxi_own_of(k) ifx_owner(k)                      /* (fx_slots.c: the same numbering) */
AINL uint32_t fxi_aix(uint32_t o)                       /* an owner -> the audio's index (its L for a stereo one) */
{
    return o < NTRK ? o : o == FXI_MST ? IFX_MST : o == FXI_UT ? IFX_UT : IFX_RT(o - FXI_RT0);
}

/* ---- ECHO */
#if FELUCCA_INS_DELAY
#define IFX_EC_LEN 4096u
#define IFX_EC_D 3308u                                  /* 75 ms */
typedef struct {
    int32_t w;                                          /* the echo's level (Q15) */
    uint16_t wp, fill, q;                               /* (fill and q <= IFX_EC_LEN) */
    uint8_t a, on, evict;                               /* the last amount (a fade-out runs at it), running, its line going back */
    int16_t *line;                                      /* IFX_EC_LEN samples: units of the effects memory */
} ifx_ec_t;
_Static_assert(IFX_EC_LEN == FXA_Z1 * FXA_U, "ECHO: its line in units of the effects memory");
static ifx_ec_t ifx_echo[FXI_TN1] __attribute__((section(".pool")));
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

/* ---- JUNO CHORUS (fx/junocho/juno_cho.h): a 1024-sample line (4 KB of the effects memory an instance) and its state; MIX fades in and out
 * over ~11 ms (its step a block: 2048 of 127 << 8), MODE the owner's (a track's P_TJMODE, the master's mst_jmode). A part
 * (mono) gets the mid, its side into ifx_jside (mixed after its level as WIDE's: fx.c mix_part); the drum bus and the
 * master their L and R, the line fed the mid. From rest the line is zeroed (a word loop, once). Runs from flash (XIP).
 * Its evict flag (fxi.c: its line goes back to a send) fades it out as at MIX 0 */
#if FELUCCA_INS_JCHORUS
#define JUNO_CHO_WIDE 1                                 /* (its line at 32 bits: 4 KB of the effects memory, fxi_mem.h FXA_Z2; the builder's JUNO_FINE, always on since 2026-10-29) */
#include "../junocho/juno_cho.h"
#if FELUCCA_USB_AUDIO
#define FXI_UT_JMODE(k) ((k) == IFX_UT ? ut.v[UTV_JMODE] : mst_jmode)   /* (the USB input: its own, fx_slots.c ut) */
#else
#define FXI_UT_JMODE(k) mst_jmode
#endif
typedef struct {
    juno_cho_t c;
    int32_t w;                                          /* the MIX now, << 8 */
    uint8_t on, evict;                                  /* running (0: at rest); evict: its line goes back to a send */
} ifx_jc_t;
_Static_assert(JUNO_CHO_LEN * sizeof(juno_cho_s) == FXA_Z2 * FXA_U * 2u, "JUNO CHORUS: its line in units of the effects memory");
static ifx_jc_t ifx_jcho[FXI_TN2] __attribute__((section(".pool"), aligned(4)));
#define IFX_JC_STEP 2048                                /* the MIX's step a block (Q8) */
static __attribute__((noinline)) void ifx_jchorus(ifx_jc_t *s, uint32_t k, int32_t a, int32_t *l, int32_t *r, uint32_t n,
                                                       uint32_t sh)
{
    int32_t to, w = s->w;
    uint32_t mode = (uint32_t)(k < NTRK ? trk[k].p[P_TJMODE] : FXI_UT_JMODE(k)) % 3u;   /* (a stereo owner but the drum bus
                                                        * and the USB input: the master's MODE) */
    if (s->evict)
        a = 0;                                          /* (its line goes back to a send: out, then at rest) */
    to = a << 8;
    if (!a && !w) {
        s->on = 0;
        if (!r)
            ifx_jsd[k % NPART] = 0;                     /* (a part: no side) */
        return;
    }
    if (!s->on) {
        s->c.wr = 0, s->c.ph = 0;                       /* (starting: a clean line, the LFO and the low-passes at 0) */
        s->c.f[0][0] = s->c.f[0][1] = s->c.f[1][0] = s->c.f[1][1] = 0;
        ifx_zero(s->c.ln, JUNO_CHO_LEN * sizeof *s->c.ln);
        s->on = 1;
    }
    w += clamp(to - w, -IFX_JC_STEP, IFX_JC_STEP);
    s->w = w;
    sh = 0;                                             /* (the 32-bit line keeps the owner's scale) */
    if (r)
        juno_cho_st(&s->c, mode, (uint32_t)w >> 8, l, r, n, sh);
    else {
        juno_cho_ms(&s->c, mode, (uint32_t)w >> 8, l, ifx_jside[k % NPART], n, sh);
        ifx_jsd[k % NPART] = 1;
    }
}
#endif

#include "xdly.c"                                       /* XDLY: the cross-modulated dual delay (2026-10-22) */
#include "fxi_send.c"                                   /* CHO REV DLY: the sends as inserts (2026-10-29) */
/* part k's XDLY still repeats (fx.c mix_part: a part whose voices have ended stays heard until its lines are clear) */
AINL int ifx_ringing(uint32_t k)
{
    int on = 0;
    uint32_t i;
#if FELUCCA_INS_XDLY
    i = fxi_at[k % NPART][FXI_J(FXT_XDLY)];
    on |= i < FXI_TN4 && ifx_xdly[i].on && ifx_xdly[i].w && ifx_xdly[i].q < XD_LEN;
#endif
#if INS_REV                                             /* (REV's tail, DLY's repeats: fxi_send.c) */
    i = fxi_at[k % NPART][FXI_J(FXT_REV)];
    on |= i < FXI_TN5 && ifx_room[i].on && ifx_room[i].w && (ifx_room[i].q < REV_Q_TANK || ifx_rm_lp(&ifx_room[i]));
#endif
#if INS_DLY
    i = fxi_at[k % NPART][FXI_J(FXT_DLY)];
    on |= i < FXI_TN6 && ifx_tdly[i].on && ifx_tdly[i].w && (ifx_tdly[i].q < DLY_LEN || ifx_td_fst(&ifx_tdly[i]));
#endif
    (void)k, (void)i;
    return on;
}

/* owner k's instance of limited type ty (FXI_NONE: none); k: a track, or the master (IFX_MST) */
AINL uint32_t ifx_lim_at(uint32_t ty, uint32_t k) { return fxi_at[fxi_own_of(k)][FXI_J(ty)]; }
/* its wet share now (0: none, or at rest) */
AINL int32_t ifx_lim_w(uint32_t ty, uint32_t k)
{
    uint32_t i = ifx_lim_at(ty, k);
    if (i == FXI_NONE)
        return 0;
#if INS_CHO
    if (ty == FXT_CHO)
        return ifx_choi[i].w;
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
#if FELUCCA_INS_XDLY
    if (ty == FXT_XDLY)
        return ifx_xdly[i].w;
#endif
#if INS_REV
    if (ty == FXT_REV)
        return ifx_room[i].w;
#endif
#if INS_DLY
    if (ty == FXT_DLY)
        return ifx_tdly[i].w;
#endif
    return 0;
}
/* track k's instance of type ty is fading out to give its lent line back (fxi.c): as off (a silent part: at rest) */
AINL int ifx_lim_ev(uint32_t ty, uint32_t k)
{
    uint32_t i;
    if (!FXT_IS_LIM(ty) || (i = ifx_lim_at(ty, k)) == FXI_NONE)
        return 0;
#if INS_CHO
    if (ty == FXT_CHO)
        return ifx_choi[i].evict;
#endif
#if FELUCCA_INS_DELAY
    if (ty == FXT_ECHO)
        return ifx_echo[i].evict;
#endif
#if FELUCCA_INS_JCHORUS
    if (ty == FXT_JUNO)
        return ifx_jcho[i].evict;
#endif
#if FELUCCA_INS_FLANGER
    if (ty == FXT_FLNG)
        return ifx_flng[i].evict;
#endif
#if FELUCCA_INS_XDLY
    if (ty == FXT_XDLY)
        return ifx_xdly[i].evict;
#endif
#if INS_REV
    if (ty == FXT_REV)
        return ifx_room[i].evict;
#endif
#if INS_DLY
    if (ty == FXT_DLY)
        return ifx_tdly[i].evict;
#endif
    return 0;
}
/* to rest at once (a part not heard: insfx.c ifx_quiet_part); it starts again from a clean line */
AINL void ifx_lim_rest(uint32_t ty, uint32_t k)
{
    uint32_t i = ifx_lim_at(ty, k);
    if (i == FXI_NONE)
        return;
#if INS_CHO
    if (ty == FXT_CHO) {
        ifx_choi[i].w = 0, ifx_choi[i].on = 0;          /* (its line: not read when it starts again) */
        if (k < NPART)
            ifx_csd[k] = 0;
    }
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
#if FELUCCA_INS_XDLY
    if (ty == FXT_XDLY)
        ifx_xdly[i].w = 0, ifx_xdly[i].on = 0;          /* (its lines: not read when it starts again) */
#endif
#if INS_REV
    if (ty == FXT_REV) {
        ifx_room[i].w = 0, ifx_room[i].on = 0;          /* (its lines: cleared when it starts again, unless 0 already) */
        if (k < NPART)
            ifx_rsd[k] = 0;
    }
#endif
#if INS_DLY
    if (ty == FXT_DLY)
        ifx_tdly[i].w = 0, ifx_tdly[i].on = 0;          /* (its line: not read when it starts again) */
#endif
}
/* type ty on track k at amount a: a part's b (r 0), the drum bus's l and r; no instance: nothing (bypassed) */
static HOT2 __attribute__((noinline)) void ifx_lim_run(uint32_t ty, uint32_t k, int32_t a, int32_t *l, int32_t *r,
                                                       uint32_t n, uint32_t sh)
{
    uint32_t i = ifx_lim_at(ty, k);
    if (i == FXI_NONE || FXR_MOVING() || FXI_CP_MOVING(FXI_J(ty), i)) {
#if INS_REV
        if (ty == FXT_REV && !r)
            ifx_rsd[k % NPART] = 0;                     /* (no side this block) */
#endif
#if INS_CHO
        if (ty == FXT_CHO && !r)
            ifx_csd[k % NPART] = 0;
#endif
        return;                                         /* (none; or the own room moving, core/arena.c: this block
                                                         * bypassed, its lines untouched) */
    }
#if INS_CHO
    if (ty == FXT_CHO && i < FXI_TN0)
        FAR(ifx_cho_run)(&ifx_choi[i], k, a, l, r, n);   /* (flash code, XIP: the RAM text has no room) */
#endif
#if FELUCCA_INS_DELAY
    if (ty == FXT_ECHO && i < FXI_TN1)
        ifx_echo_run(&ifx_echo[i], a, l, r, n, sh);
#endif
#if FELUCCA_INS_JCHORUS
    if (ty == FXT_JUNO && i < FXI_TN2)
        FAR(ifx_jchorus)(&ifx_jcho[i], k, a, l, r, n, sh);   /* (flash code, XIP: the RAM text has no room) */
#endif
#if FELUCCA_INS_FLANGER
    if (ty == FXT_FLNG && i < FXI_TN3)
        ifx_flanger(&ifx_flng[i], a, l, r, n, sh);
#endif
#if FELUCCA_INS_XDLY
    if (ty == FXT_XDLY && i < FXI_TN4)
        ifx_xdly_run(&ifx_xdly[i], k, a, l, r, n, sh);
#endif
#if INS_REV
    if (ty == FXT_REV && i < FXI_TN5)
        FAR(ifx_room_run)(&ifx_room[i], k, a, l, r, n);   /* (flash code, XIP: the RAM text has no room) */
#endif
#if INS_DLY
    if (ty == FXT_DLY && i < FXI_TN6)
        FAR(ifx_tdly_run)(&ifx_tdly[i], a, l, r, n);
#endif
    (void)a, (void)l, (void)r, (void)n, (void)sh;
}
#endif
