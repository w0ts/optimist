/* SPDX-License-Identifier: GPL-3.0-only */
/* The instances of the limited insert types (2026-10-13; fxi_kern.c: CHRS ECHO, and JUNO CHORUS since 2026-10-14, a
 * builder count each, registry.h FELUCCA_INS_CHORUS FELUCCA_INS_DELAY FELUCCA_INS_JCHORUS). An owner is a track, or the
 * master (FXI_MST: its insert slots; JUNO CHORUS only, fx_slots.c MST_OK). Included by fx.c after insfx.c. The main loop's side: no instance is handed
 * out or cleared in the audio ISR.
 *   - An instance belongs to a track and a type (a type is once a track at most, D3): a swap of S1 and S2 keeps it.
 *   - A change of slots (fx_slots.c ins_put, which may run anywhere: FXI_ASK(1)) is followed in fxi_poll (main.c, every
 *     pass of the main loop): a track that no longer holds the type gives its instance back once its wet share has
 *     faded to 0 (no click; until then it fades as any insert in no slot); a track holding the type and none yet takes
 *     a free one, the tracks in order T1 T2 T3 DR, the builder's own first. The instance is zeroed here, then published
 *     with the interrupts off.
 *   - A project load (fx_rec.c fxr_decode: FXI_ASK(2)): the tracks holding the type in track order get the instances
 *     while they last (a track that keeps the type keeps its own), every other instance is given back at once: the same
 *     project always plays the same way, whatever came before.
 *   - A track holding the type with no instance left: the type is bypassed there (fxi_state FXI_FULL, for the UI),
 *     until one is given back.
 *
 * Lending (2026-10-13). A send whose type is in no master slot (FX > SLOTS: CHO or DLY) is not heard, and its line
 * idles: it lends it to its insert copies, CHO's 2048 samples as 2 CHRS lines, DLY's as ECHO lines of 4096 and XDLY's
 * pairs of lines (fxi_kern.c FXI_LND: up to a line a track beside the builder's own; DLY's line is the delay arena's first
 * units, 2026-10-22: an ECHO or XDLY instance may sit there or in the arena's own units, fxa_find, one type's or the
 * other's, never both on a unit). Only on demand: when a track holding the type has none left.
 * Each line has one owner at a time, the send's bus or the inserts, and the owner changes only here:
 *   SEND -> LENT  the send in no master slot and its bus idle (a full line of zeros written, no clear pending: fx.c
 *                 fx_buses' own idle test), checked and the gate set (fx_slots.c fxl_lent) in one interrupts-off
 *                 section: the bus has not run since its test and runs no more (fx.c fx_ret_block: gated, no step,
 *                 no clear). Then the slices are zeroed and published as any instance.
 *   LENT -> BACK  the send back in a master slot: the inserts on its line fade out (their evict flag: amount 0, 2.9 ms),
 *                 the bus stays gated (silent) meanwhile
 *   BACK -> SEND  every lent instance faded and given back (or LENT with none in use): the line zeroed (the idle state
 *                 the bus expects), then the gate cleared with the interrupts off; the bus starts from silence
 *   BACK -> LENT  the send out of the slots again before that: the evict flags off, the inserts fade back in
 * Two cores: the second core runs any part's mix_part, its inserts with it, inside CPU0's audio interrupt (dual.c
 * mix_block_dual: fork, join, then fx_buses on CPU0); it never runs outside it. A section here with the interrupts off
 * is therefore between two blocks for both cores: the gate and every fxi_at change fall on a block boundary, and in no
 * block do the bus and an insert both touch a line. The instances' lines and states are written here only while no
 * fxi_at points at them (not yet published, or given back), so neither core is using them. (CPU1's writes reach memory
 * before its done flag: dual_join's fm1_dual_sync; ours before a block's post: mix_block_dual's.)
 *
 * The UI's queries (main loop):
 *   fxi_cap(t)       instances of type t now: the builder's own and what its send can lend now (in no master slot);
 *                    FXI_ANY a type on every track (not limited), 0 not built
 *   fxi_used(t)      handed out now (FXI_ANY not limited)
 *   fxi_free(t)      fxi_cap - fxi_used (FXI_ANY not limited)
 *   fxi_lent(t)      of those used, on the send's line
 *   fxi_state(k, s)  track k's slot s: FXI_NA (empty, or a type not limited), FXI_OK (its instance), FXI_WAIT (asked,
 *                    a pass gives it: the next, or once the send's tail has ended), FXI_FULL (none free: bypassed)
 *   fxi_can(k, t)    1: type t picked on track k would be heard (not limited, held there with an instance, or one free) */
#define FXI_ANY 0xFFu
enum { FXI_NA, FXI_OK, FXI_WAIT, FXI_FULL };
#if INS_LIM
enum { FXL_SEND, FXL_LENT, FXL_BACK };
#define FXL_NS 2u                                       /* the send lines: 0 the chorus's (CHRS), 1 the delay's (ECHO, XDLY) */
#define FXL_OF(j) ((j) == 0u ? 0u : FXA_IS(j) ? 1u : FXL_NS)   /* (type j's send line; FXL_NS: none) */
#define FXA_NO 0xFFu
static uint8_t fxi_own[FXI_NL][FXI_IMAX];               /* each instance's owner (FXI_NONE: free); the main loop's only */
static uint16_t fxi_full[FXI_NL];                       /* the owners (a bit each) holding the type with none left */
static uint8_t fxi_pend;                                /* a pass is due: a fade-out, a tail, a starved track */
static uint8_t fxl_st[FXL_NS];                          /* each send line's owner (FXL_*: above); the main loop's only */
/* the delay arena (fxi_kern.c FXA_*), the main loop's only: each ECHO / XDLY instance's first unit (while handed out),
 * the units in use, and the send's units handed out since its line was lent (0 .. fxa_hi - 1: zeroed when it goes back) */
static uint8_t fxa_u[FXI_NL][FXI_IMAX];
static uint8_t fxa_busy[FXA_N ? FXA_N : 1u];
static uint8_t fxa_hi;
_Static_assert(FXL_N0 + FELUCCA_INS_CHORUS <= FXI_IMAX && FXL_N1 + FELUCCA_INS_DELAY <= FXI_IMAX &&
               FXL_N4 + FELUCCA_INS_XDLY <= FXI_IMAX && FXI_NK <= 16u, "fxi_own: FXI_IMAX instances; fxi_full: an owner a bit");

AINL uint32_t fxi_holds(uint32_t k, uint32_t ty)        /* owner k holds type ty in a slot (and it is built) */
{
    if (k == FXI_MST)
        return (mst_slot[0] == ty || mst_slot[1] == ty) && (MST_OK >> ty & 1u);
#if FELUCCA_USB_AUDIO
    if (k == FXI_UT)
        return (ut.slot[0] == ty || ut.slot[1] == ty) && (UT_OK >> ty & 1u);
#endif
    if (k >= FXI_RT0)
        return FXI_RT_HOLDS(k - FXI_RT0, ty);
    return k < NTRK && (ins_slot[k][0] == ty || ins_slot[k][1] == ty) && (FXT_BUILT >> ty & 1u);
}
/* send line s (FXL_OF) in a master slot (none: as if always in one) */
AINL uint32_t fxl_send_on(uint32_t s) { return s >= FXL_NS ? 1u : FXS_HAS(s ? FXT_DLY : FXT_CHO); }
/* send line s lends to its types at all (the chorus's: FXL_N0 lines; the delay's: its line is in the arena) */
#define fxl_lends(s) ((s) == 0u ? FXL_N0 != 0u : (s) == 1u && FXA_S && (FELUCCA_INS_DELAY || FELUCCA_INS_XDLY))
/* instance i of type j is on its send's line: CHRS past the builder's count, ECHO / XDLY in the send's units */
AINL uint32_t fxi_onl(uint32_t j, uint32_t i) { return FXA_IS(j) ? fxa_u[j][i] < FXA_S : i >= FXI_CAP[j]; }
static void fxi_publish(uint32_t k, uint32_t j, uint32_t i)   /* owner k's instance of type FXI_TY(j): i */
{
    fm1_irq_off();
    fxi_at[k][j] = (uint8_t)i;
    fm1_irq_on();
}
static void fxi_give(uint32_t j, uint32_t i)            /* instance i of type FXI_TY(j) back (its owner's link gone) */
{
    fxi_publish(fxi_own[j][i], j, FXI_NONE);
    fxi_own[j][i] = FXI_NONE;
    if (FXA_IS(j))
        memset(fxa_busy + fxa_u[j][i], 0, FXA_SZ(j));   /* (its units free: no core reads them now) */
}

/* ---- The delay arena's places (fxi_kern.c FXA_*) */
/* n free units at lo + a multiple of n, below hi: the lowest (up) or the highest; FXA_NO: none */
static uint32_t fxa_fit(uint32_t lo, uint32_t hi, uint32_t n, uint32_t up)
{
    uint32_t c = (hi - lo) / n, k, p, u, ok;
    for (k = 0; k < c; k++) {
        p = lo + (up ? k : c - 1u - k) * n;
        for (ok = 1, u = 0; u < n; u++)
            ok &= !fxa_busy[p + u];
        if (ok)
            return p;
    }
    return FXA_NO;
}
static uint32_t fxa_fits(uint32_t lo, uint32_t hi, uint32_t n)   /* how many such runs are free */
{
    uint32_t p, u, ok, m = 0;
    for (p = lo; p + n <= hi; p += n) {
        for (ok = 1, u = 0; u < n; u++)
            ok &= !fxa_busy[p + u];
        m += ok;
    }
    return m;
}
static uint32_t fxa_ownn(uint32_t j)                    /* type j's instances out in the inserts' own units */
{
    uint32_t i, m = 0;
    for (i = 0; i < FXI_TOT(j); i++)
        m += fxi_own[j][i] != FXI_NONE && fxa_u[j][i] >= FXA_S;
    return m;
}
/* a free ECHO / XDLY instance placed: up to the builder's count in the own units (an XDLY from the bottom, an ECHO from the
 * top), else on the send's line while lent; its units taken; FXI_NONE: none free, or no room */
static uint32_t fxa_find(uint32_t j)
{
    uint32_t i, f = FXI_NONE, n = FXA_SZ(j), p = FXA_NO;
    for (i = 0; i < FXI_TOT(j) && f == FXI_NONE; i++)
        if (fxi_own[j][i] == FXI_NONE)
            f = i;
    if (f == FXI_NONE)
        return FXI_NONE;
    if (fxa_ownn(j) < FXI_CAP[j])
        p = fxa_fit(FXA_S, FXA_N, n, n > 1u);
    if (p == FXA_NO && fxl_st[1] == FXL_LENT)
        p = fxa_fit(0, FXA_S, n, 1);
    if (p == FXA_NO)
        return FXI_NONE;
    fxa_u[j][f] = (uint8_t)p;
    memset(fxa_busy + p, 1, n);
    if (p < FXA_S && p + n > fxa_hi)
        fxa_hi = (uint8_t)(p + n);
    return f;
}
/* instance i's state and line (not published: no core uses them): the evict flag */
static void fxi_evict(uint32_t j, uint32_t i, uint32_t v)
{
#if FELUCCA_INS_CHORUS
    if (j == 0u)
        ifx_chrs[i].evict = (uint8_t)v;
#endif
#if FELUCCA_INS_DELAY
    if (j == 1u)
        ifx_echo[i].evict = (uint8_t)v;
#endif
#if FELUCCA_INS_XDLY
    if (j == 4u)
        ifx_xdly[i].evict = (uint8_t)v;
#endif
    (void)i, (void)v;                                   /* (JUNO CHORUS, FLNG: no lent line, never evicted) */
}
static void fxi_clear(uint32_t j, uint32_t i)           /* instance i zeroed: its line (own or lent), at rest */
{
#if FELUCCA_INS_CHORUS
    if (j == 0u) {
        int16_t *ln = i < FELUCCA_INS_CHORUS ? ifx_chrs_ln[i] : cho_buf + (i - FELUCCA_INS_CHORUS) * IFX_CH_LEN;
        memset(&ifx_chrs[i], 0, sizeof ifx_chrs[i]);
        memset(ln, 0, IFX_CH_LEN * sizeof *ln);
        ifx_chrs[i].line = ln;
    }
#endif
#if FELUCCA_INS_DELAY
    if (j == 1u) {
        int16_t *ln = fxa_line(fxa_u[1][i]);
        memset(&ifx_echo[i], 0, sizeof ifx_echo[i]);
        memset(ln, 0, IFX_EC_LEN * sizeof *ln);
        ifx_echo[i].line = ln;
    }
#endif
#if FELUCCA_INS_JCHORUS
    if (j == 2u)
        memset(&ifx_jcho[i], 0, sizeof ifx_jcho[i]);
#endif
#if FELUCCA_INS_FLANGER
    if (j == 3u)
        memset(&ifx_flng[i], 0, sizeof ifx_flng[i]);
#endif
#if FELUCCA_INS_XDLY
    if (j == 4u) {
        int16_t *ln = fxa_line(fxa_u[4][i]);
        memset(&ifx_xdly[i], 0, sizeof ifx_xdly[i]);
        memset(ln, 0, 2u * XD_LEN * sizeof *ln);
        ifx_xdly[i].line = ln;
    }
#endif
    (void)i;
}
/* a free instance of type FXI_TY(j): the builder's own first, then the send's line while lent (ECHO, XDLY: placed in
 * the delay arena, fxa_find); FXI_NONE: none */
static uint32_t fxi_find(uint32_t j)
{
    uint32_t i, n = j == 0u && fxl_st[0] == FXL_LENT ? FXI_TOT(j) : FXI_CAP[j];
    if (FXA_IS(j))
        return fxa_find(j);
    for (i = 0; i < n; i++)
        if (fxi_own[j][i] == FXI_NONE)
            return i;
    return FXI_NONE;
}
/* one type's instances after a change (load: a project load) */
static void fxi_sync(uint32_t j, int load)
{
    uint32_t ty = FXI_TY(j), k, i, want = 0, n = 0;
    uint32_t avail = FXI_CAP[j] + (fxl_send_on(FXL_OF(j)) ? 0u : FXI_LND[j]);   /* (a load: the first avail tracks holding it) */
    for (k = 0; k < FXI_NK; k++)                        /* (the tracks, then the master) */
        if (fxi_holds(k, ty) && (!load || n++ < avail))
            want |= 1u << k;
    for (i = 0; i < FXI_TOT(j); i++) {                  /* instances whose track no longer wants them: back */
        k = fxi_own[j][i];
        if (k == FXI_NONE || (want >> k & 1u))
            continue;
        if (load || !ifx_lim_w(ty, fxi_aix(k)))
            fxi_give(j, i);
        else
            fxi_pend = 1;                               /* (fading out: the next pass) */
    }
    fxi_full[j] = 0;
    for (k = 0; k < FXI_NK; k++) {                      /* owners wanting one and without: a free one, in track order */
        if (!(want >> k & 1u) || fxi_at[k][j] != FXI_NONE)
            continue;
        i = fxi_find(j);
        if (i == FXI_NONE) {
            fxi_full[j] |= (uint16_t)(1u << k);         /* (none left: bypassed) */
            continue;
        }
        fxi_clear(j, i);                                /* (not in use: the audio does not see it) */
        fxi_own[j][i] = (uint8_t)k;
        fxi_publish(k, j, i);
    }
    if (load)                                           /* (a track past the count: asks again when one is given back) */
        for (k = 0; k < FXI_NK; k++)
            if (fxi_holds(k, ty) && fxi_at[k][j] == FXI_NONE)
                fxi_full[j] |= (uint16_t)(1u << k);
}

/* ---- Lending (above): the send line s (0 CHO -> CHRS, 1 DLY -> ECHO and XDLY, in the delay arena) */
/* the bus idle and not wanted: the gate set, in one section with the interrupts off (no block in between); 1 lent */
static uint32_t fxl_take(uint32_t s)
{
    uint32_t ok;
    fm1_irq_off();
    ok = !fxl_send_on(s) && (s ? fx.dly_q >= DLY_LEN && !DLY_FST() && !fxz.dclr : fx.cho_q >= CHO_LEN && !fxz.cclr);
    if (ok)
        fxl_lent |= (uint8_t)(1u << s);
    fm1_irq_on();
    return ok;
}
/* every slice it lent zeroed (the bus's idle state: its line all zeros), the gate cleared: the bus's again */
static void fxl_return(uint32_t s)
{
#if FELUCCA_INS_CHORUS
    if (s == 0u)
        memset(cho_buf, 0, FXI_LND[0] * IFX_CH_LEN * sizeof cho_buf[0]);
#endif
    if (s == 1u && fxa_hi) {
        memset(dly_buf, 0, fxa_hi * FXA_U * sizeof dly_buf[0]);   /* (the units handed out since it was lent) */
        fxa_hi = 0;
    }
    fm1_irq_off();
    fxl_lent &= (uint8_t)~(1u << s);
    fm1_irq_on();
    fxl_st[s] = FXL_SEND;
}
static uint32_t fxi_nused(uint32_t j)                   /* type j's instances handed out */
{
    uint32_t i, n = 0;
    for (i = 0; i < FXI_TOT(j); i++)
        n += fxi_own[j][i] != FXI_NONE;
    return n;
}
/* the line's owner after the pass's sync; 1: just lent (a sync again hands its slices out) */
static uint32_t fxl_step(uint32_t s)
{
    uint32_t i, j, held = 0, want = 0, on = fxl_send_on(s);
    if (!fxl_lends(s))
        return 0;
    if (fxl_st[s] == FXL_SEND) {
        for (j = 0; j < FXI_NL; j++)                    /* (a type of this line starved, one of its instances free) */
            want |= FXL_OF(j) == s && fxi_full[j] && fxi_nused(j) < FXI_TOT(j);
        if (!want || on)
            return 0;
        if (fxl_take(s)) {
            fxl_st[s] = FXL_LENT;
            return 1;
        }
        fxi_pend = 1;                                   /* (its tail still rings: a later pass) */
        return 0;
    }
    if (fxl_st[s] == FXL_BACK && !on)
        fxl_st[s] = FXL_LENT;                           /* (out of the slots again: the inserts stay, fade back in) */
    else if (fxl_st[s] == FXL_LENT && on)
        fxl_st[s] = FXL_BACK;
    for (j = 0; j < FXI_NL; j++)
        for (i = 0; FXL_OF(j) == s && i < FXI_TOT(j); i++) {
            uint32_t k = fxi_own[j][i];
            if (k == FXI_NONE || !fxi_onl(j, i))
                continue;
            fxi_evict(j, i, fxl_st[s] == FXL_BACK);
            if (fxl_st[s] == FXL_BACK && !ifx_lim_w(FXI_TY(j), fxi_aix(k)))
                fxi_give(j, i);                         /* (faded out: its slice back) */
            else
                held = 1;
        }
    if (held) {
        if (fxl_st[s] == FXL_BACK)
            fxi_pend = 1;                               /* (fading: the next pass) */
        return 0;
    }
    fxl_return(s);                                      /* (none in use: the line back to its bus) */
    fxi_pend = 1;                                       /* (a pass more: a track it served asks again, FULL or own) */
    return 0;
}

/* the main loop, every pass (main.c): the instances after a change of slots or a load; nothing to do costs a compare */
static void fxi_poll(void)
{
    uint32_t d, j, s;
    if (!fxi_dirty && !fxi_pend)
        return;
    fm1_irq_off();
    d = fxi_dirty;
    fxi_dirty = 0;
    fm1_irq_on();
    fxi_pend = 0;
    for (j = 0; j < FXI_NL; j++)
        if (FXI_CAP[j])
            fxi_sync(j, (d & 2u) != 0u);
    for (s = 0; s < FXL_NS; s++)
        if (fxl_step(s))
            for (j = 0; j < FXI_NL; j++)
                if (FXI_CAP[j] && FXL_OF(j) == s)
                    fxi_sync(j, 0);                     /* (just lent: its slices to the owners waiting, in type order) */
    for (j = 0; j < FXI_NL; j++)
        if (FXI_CAP[j] && fxi_full[j])
            fxi_pend = 1;                               /* (a starved track takes one given back) */
    for (s = 0; s < FXL_NS; s++)
        if (fxl_st[s] != FXL_SEND)
            fxi_pend = 1;                               /* (a lent line is watched: its send back in a slot, none using it) */
}
/* boot, the host tests: every instance free, every line the send's, the delay arena empty */
static void fxi_init(void)
{
    uint32_t j, k;
    for (j = 0; j < FXI_NL; j++) {
        for (k = 0; k < FXI_IMAX; k++)
            fxi_own[j][k] = FXI_NONE;
        for (k = 0; k < FXI_NK; k++)
            fxi_at[k][j] = FXI_NONE;
        fxi_full[j] = 0;
    }
    for (j = 0; j < FXL_NS; j++)
        if (fxl_st[j] != FXL_SEND)
            fxl_return(j);
    memset(fxa_busy, 0, sizeof fxa_busy);
    fxi_pend = 0;
    fxi_dirty = 2;
}
#define FXI_LIM(t) FXT_IS_LIM(t)
/* what the send's line can lend type j now (0: in a master slot, or taking it back) */
AINL uint32_t fxl_can(uint32_t j)
{
    uint32_t s = FXL_OF(j);
    return s >= FXL_NS || fxl_send_on(s) || fxl_st[s] == FXL_BACK ? 0u : FXI_LND[j];
}
/* a starved owner of type j gets one once the send's tail has ended (WAIT, not FULL): the line not lent yet and room
 * on it (ECHO, XDLY: an instance free and its units within the send's line) */
static uint32_t fxl_wait(uint32_t j)
{
    if (FXA_IS(j))
        return fxl_lends(1) && fxl_st[1] == FXL_SEND && !fxl_send_on(1) && FXA_S >= FXA_SZ(j) && fxi_nused(j) < FXI_TOT(j);
    return j == 0u && fxl_st[0] == FXL_SEND && fxl_can(0);
}
/* ECHO, XDLY: the instances of type j the arena can place now (fxa_find's rule: up to the count in the own units, then on
 * the send's line if it can lend) */
static uint32_t fxa_room(uint32_t j)
{
    uint32_t n = FXA_SZ(j), own = fxa_ownn(j), r;
    r = own < FXI_CAP[j] ? FXL_MIN(FXI_CAP[j] - own, fxa_fits(FXA_S, FXA_N, n)) : 0u;
    if (fxl_lends(1) && !fxl_send_on(1) && fxl_st[1] != FXL_BACK)
        r += fxa_fits(0, FXA_S, n);
    return r;
}
static uint32_t fxi_cap(uint32_t t)
{
    if (!FXI_LIM(t))
        return t < FXT_N && (FXT_BUILT >> t & 1u) ? FXI_ANY : 0u;
    return FXI_CAP[FXI_J(t)] ? FXI_CAP[FXI_J(t)] + fxl_can(FXI_J(t)) : 0u;
}
static uint32_t fxi_used(uint32_t t) { return FXI_LIM(t) ? fxi_nused(FXI_J(t)) : fxi_cap(t) ? FXI_ANY : 0u; }
static uint32_t fxi_lent(uint32_t t)                    /* of those used, on the send's line */
{
    uint32_t j = FXI_J(t), i, n = 0;
    for (i = 0; FXI_LIM(t) && i < FXI_TOT(j); i++)
        n += fxi_own[j][i] != FXI_NONE && fxi_onl(j, i);
    return n;
}
static uint32_t fxi_free(uint32_t t)                    /* (ECHO, XDLY: no more than the arena has room for) */
{
    uint32_t c = fxi_cap(t), u = fxi_used(t), j = FXI_J(t);
    if (!FXI_LIM(t))
        return c ? FXI_ANY : 0u;
    c = c > u ? c - u : 0u;
    return FXA_IS(j) && c ? FXL_MIN(c, fxa_room(j)) : c;
}
/* owner o's (FXI_MST, FXI_UT, FXI_RT0 + j, a track) type t: FXI_NA .. FXI_FULL (as fxi_state) */
static uint32_t fxi_ostate(uint32_t o, uint32_t t)
{
    uint32_t j = FXI_J(t);
    o %= FXI_NK;
    if (!FXI_LIM(t) || !FXI_CAP[j])
        return FXI_NA;
    if (fxi_at[o][j] != FXI_NONE)
        return FXI_OK;
    if (fxi_dirty || fxl_wait(j))
        return FXI_WAIT;                                /* (the next pass, or the send's tail ending) */
    return fxi_full[j] >> o & 1u ? FXI_FULL : FXI_WAIT;
}
static uint32_t fxi_state(uint32_t k, uint32_t s) { return fxi_ostate(k % NTRK, ins_slot[k % NTRK][s & 1u]); }
static int fxi_can(uint32_t k, uint32_t t)
{
    if (!FXI_LIM(t))
        return t < FXT_N && (FXT_BUILT >> t & 1u);
    return FXI_CAP[FXI_J(t)] && (fxi_at[k % FXI_NK][FXI_J(t)] != FXI_NONE || fxi_free(t) > 0u);
}
#else
#define fxi_poll() ((void)0)
#define fxi_init() ((void)0)
static uint32_t fxi_cap(uint32_t t) { return t < FXT_N && (FXT_BUILT >> t & 1u) ? FXI_ANY : 0u; }
#define fxi_used(t) fxi_cap(t)
#define fxi_free(t) fxi_cap(t)
#define fxi_lent(t) ((void)(t), 0u)
#define fxi_state(k, s) ((void)(k), (void)(s), (uint32_t)FXI_NA)
#define fxi_ostate(o, t) ((void)(o), (void)(t), (uint32_t)FXI_NA)
#define fxi_can(k, t) ((void)(k), fxi_cap(t) != 0u)
#endif
