/* SPDX-License-Identifier: GPL-3.0-only */
/* The sends as inserts (2026-10-29, the user: "chorus, delay and reverb can go on normal tracks as INSERTS with a dry/wet
 * amount, like any other insert"): two limited insert types (fxi.c j 5, 6) that run the send buses' own code (fx.c
 * room_core, room_long_core, dly_core) on a state of their own. Included by fxi_kern.c.
 *   ROOM  the reverb send's ROOM tank (two diffusers, four lines, the Hadamard feedback): SIZE and DAMP are the reverb's
 *         (FX > REV/CHO, its loop gains fx.c rm: above SIZE 90 the unbiased long tail, with a generator of its own), line
 *         0's slow LFO its own. 9681 samples an instance (19 units of the effects memory, fxi_mem.h FXA_Z5). The ROOM
 *         only: PLATE, FDN8 and VTINY run at 22.05 kHz behind the send's half-band filters and FDN8's ring alone is
 *         32 KB (REV_POOL; 16 KB without), so TYPE stays the send's own
 *   TDLY  the delay send's delay: its TIME (tempo-synced divisions, DLY_DOT), FDBK (to the endless 100 %), the filter in
 *         the loop (FREQ RES TYPE SLOPE: DLY F), the saturator and the 16-bit float line, all the send's (fx.c
 *         dly_block); a line as long as the send's, FELUCCA_DLY_LEN samples (fxi_mem.h FXA_Z6). The send's MIX and DLY
 *         RET are the send's: the insert's level is its own MIX
 * The amount is the MIX (P_TROOM, P_TTDLY; the master's mst_amt): 0 dry (the owner's samples untouched), 127 all wet,
 * faded over 2.9 ms (ifx_mix). The input is the owner's block at the send's scale (a part's at its unity LEVEL is what
 * its send would carry; the wet as the send's return at unity), so a track's ROOM at 127 sounds as its send at REV 127
 * with the dry muted. A part (mono): TDLY's repeats into its block; ROOM's left and right as mid (its block) and side
 * (ifx_rside, mixed after its level as WIDE's: fx.c mix_part). A stereo owner (the drum bus, the master): fed its mid,
 * ROOM's left and right to L and R, TDLY's to both.
 * From rest: TDLY reads nothing its line held (fill, as ECHO's); ROOM zeroes its lines unless it came to rest by
 * itself (every cell 0 already). Idle (every value written 0 for its longest line, its filters at 0) with a silent
 * input: the block is skipped, the indices moved on (silence out, as the sends' idle skip). Its evict flag (fxi.c: a lent
 * line going back) fades it out as at MIX 0. Both run from flash (XIP, FAR: the RAM text has no room) */
#if INS_ROOM || INS_TDLY
#define IFX_SND_W(a) ((a) >= 127 ? 32768 : (a) * 258)  /* the MIX's wet share, Q15: 127 all wet */
#endif

/* ---- ROOM */
#if INS_ROOM
#define IFX_RM_N (REV_ROOM_LEN + REV_AP_N)              /* the lines, then the diffusers' cells, in one run of units */
#define CG_COST_ROOM 280u                               /* the CPU guard's cost (cpuguard.c): host instructions a sample of
                                                         * an instance running (tests/insrev_test.c measures, checks it) */
typedef struct {
    int32_t w;                                          /* the MIX now (Q15) */
    int32_t lp[4];                                      /* the lines' one-poles */
    uint32_t ph, rq;                                    /* line 0's LFO; the long tail's random rounding */
    uint32_t q;                                         /* samples since a non-zero value was written (FX_Q_MAX: clean) */
    uint16_t ai[2], li[4];                              /* the diffusers' and the lines' indices */
    uint8_t on, evict;                                  /* running (0: at rest); evict: its line goes back to a send (fxi.c) */
    int16_t *line;                                      /* IFX_RM_N samples: units of the effects memory (fxi.c sets it) */
} ifx_rm_t;
_Static_assert(IFX_RM_N <= FXA_Z5 * FXA_U && (FXA_Z5 * FXA_U * 2u) % 4u == 0u, "ROOM: its lines in units of the effects memory");
static ifx_rm_t ifx_room[FXI_TN5] __attribute__((section(".pool")));
static int32_t ifx_rside[NPART][CTL];                   /* ROOM's side of a part this block (mixed after its level) */
static uint8_t ifx_rsd[NPART];                          /* set this block */
AINL int ifx_rm_lp(const ifx_rm_t *s) { return (s->lp[0] | s->lp[1] | s->lp[2] | s->lp[3]) != 0; }
/* instance s of owner k (the audio's index) at MIX a: a part's b (r 0), a stereo owner's l and r */
static __attribute__((noinline)) void ifx_room_run(ifx_rm_t *s, uint32_t k, int32_t a, int32_t *l, int32_t *r, uint32_t n)
{
    room_st_t t;
    int32_t w = s->w, to, ma, mb, m0, m1, g, lpk, wv = 0;
    uint32_t i, lng;
    if (s->evict)
        a = 0;
    if (!a && !w) {
        s->on = 0;                                      /* (at rest: its lines as they are, cleared when it starts unless 0) */
        if (!r)
            ifx_rsd[k % NPART] = 0;
        return;
    }
    if (!s->on) {
        if (s->q < REV_Q_ROOM || ifx_rm_lp(s))
            ifx_zero(s->line, FXA_Z5 * FXA_U * sizeof *s->line);   /* (stopped mid-tail: a clean tank) */
        s->ai[0] = s->ai[1] = 0, s->li[0] = s->li[1] = s->li[2] = s->li[3] = 0;
        s->lp[0] = s->lp[1] = s->lp[2] = s->lp[3] = 0;
        s->q = FX_Q_MAX, s->on = 1;
    }
    to = IFX_SND_W(a);
    m0 = osc_sine(s->ph);
    s->ph += LFO_INC[30];                               /* (the send's line-0 rate: fx_buses) */
    m1 = osc_sine(s->ph);
    if (s->q >= REV_Q_ROOM && !ifx_rm_lp(s) && !fx_any(l, n) && (!r || !fx_any(r, n))) {
        s->w = to;                                      /* (a clean tank, silence in: silence out) */
        room_skip_ix(s->ai, s->li, n);
        if (!r)
            ifx_rsd[k % NPART] = 0;
        return;
    }
    ma = (REV_MOD << 8) + ((m0 * REV_MOD) >> 7), mb = (REV_MOD << 8) + ((m1 * REV_MOD) >> 7);
    g = rm.g, lpk = rm.lpk | 1, lng = RM_LONG();        /* (the send's SIZE DAMP: fx.c room_gains, once a block) */
    t.ap = s->line + REV_ROOM_LEN, t.ln = s->line, t.ai = s->ai, t.li = s->li, t.lp = s->lp;
    for (i = 0; i < n; i++) {
        int32_t x = r ? (l[i] >> 1) + (r[i] >> 1) : l[i], yl, yr, rr = ma + (((mb - ma) * (int32_t)i) >> CTL_LOG2);
        yl = lng ? room_long_core(&t, rm.in, &s->rq, x, rr, g, lpk, &yr, &wv) : room_core(&t, x, rr, g, lpk, &yr, &wv);
        w += clamp(to - w, -IFX_SLOPE, IFX_SLOPE);
        if (r) {
            l[i] = ifx_mix(l[i], yl, w);
            r[i] = ifx_mix(r[i], yr, w);
        } else {
            l[i] = ifx_mix(x, (yl + yr) >> 1, w);
            ifx_rside[k % NPART][i] = mulq15((yl - yr) >> 1, w > 32767 ? 32767 : w);
        }
    }
    if (!r)
        ifx_rsd[k % NPART] = 1;
    s->q = fx_q(s->q, wv, n), s->w = w;
}
#endif

/* ---- TDLY */
#if INS_TDLY
#define CG_COST_TDLY 230u                               /* (the CPU guard's: as CG_COST_ROOM) */
typedef struct {
    int32_t w;                                          /* the MIX now (Q15) */
    int32_t f[8], ec;                                   /* the loop filter's states, the line's carried rounding */
    uint32_t wp, fill, q;                               /* the write count, samples written since the start (<= DLY_LEN),
                                                         * since a non-zero value was written */
    uint8_t on, evict;                                  /* (as ROOM's) */
    int16_t *line;                                      /* DLY_LEN samples: units of the effects memory (fxi.c sets it) */
} ifx_td_t;
_Static_assert(DLY_LEN == FXA_Z6 * FXA_U, "TDLY: its line in units of the effects memory");
static ifx_td_t ifx_tdly[FXI_TN6] __attribute__((section(".pool")));
AINL int ifx_td_fst(const ifx_td_t *s) { return (s->f[0] | s->f[1] | s->f[4] | s->f[5]) != 0; }
static __attribute__((noinline)) void ifx_tdly_run(ifx_td_t *s, int32_t a, int32_t *l, int32_t *r, uint32_t n)
{
    int32_t w = s->w, to, fb, dmix, wv = 0;
    uint32_t i, dl, wp, fill;
    dlyf_t f;
    if (s->evict)
        a = 0;
    if (!a && !w) {
        s->on = 0;                                      /* (at rest: what the line holds is not read when it starts again) */
        return;
    }
    if (!s->on) {
        s->fill = 0, s->q = 0, s->ec = 0, s->on = 1;
        memset(s->f, 0, sizeof s->f);
    }
    to = IFX_SND_W(a);
    dl = dly_block(&fb, &f, &dmix);                     /* (the send's TIME FDBK and filter; its MIX and RET not used here) */
    if (s->q >= DLY_LEN && !ifx_td_fst(s) && !fx_any(l, n) && (!r || !fx_any(r, n))) {
        s->w = to, s->wp += n;                          /* (a clean line, silence in: silence out) */
        return;
    }
    wp = s->wp, fill = s->fill;
    for (i = 0; i < n; i++) {
        int32_t x = r ? (l[i] >> 1) + (r[i] >> 1) : l[i], y;
        y = dly_core(s->line, &wp, &s->ec, s->f, fill >= dl, x, dl, &f, fb, 32767, &wv);   /* (from rest: what the line
                                                        * held before is not read) */
        fill += fill < DLY_LEN;
        w += clamp(to - w, -IFX_SLOPE, IFX_SLOPE);
        l[i] = ifx_mix(r ? l[i] : x, y, w);
        if (r)
            r[i] = ifx_mix(r[i], y, w);
    }
    s->wp = wp, s->fill = fill, s->q = fx_q(s->q, wv, n), s->w = w;
}
#endif
