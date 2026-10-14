/* SPDX-License-Identifier: GPL-3.0-only */
/* The instances of the limited insert types (2026-10-13; fxi_kern.c: CHRS ECHO, a builder count each, registry.h
 * FELUCCA_INS_CHORUS FELUCCA_INS_DELAY). Included by fx.c after insfx.c. The main loop's side: no instance is handed
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
 * idles: it lends it to its insert copies, CHO's 2048 samples as 2 CHRS lines, DLY's as ECHO lines of 4096 (fxi_kern.c
 * FXI_LND: up to a line a track beside the builder's own). Only on demand: when a track holding the type has none left.
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
_Static_assert(NTRK == 4, "fxi_own: an instance a track at most");
static uint8_t fxi_own[2][NTRK] = {{FXI_NONE, FXI_NONE, FXI_NONE, FXI_NONE}, {FXI_NONE, FXI_NONE, FXI_NONE, FXI_NONE}};
                                                        /* each instance's track (FXI_NONE: free); the main loop's only */
static uint8_t fxi_full[2];                             /* the tracks (a bit each) holding the type with none left */
static uint8_t fxi_pend;                                /* a pass is due: a fade-out, a tail, a starved track */
static uint8_t fxl_st[2];                               /* each send line's owner (FXL_*: above); the main loop's only */
_Static_assert(FXL_N0 + FELUCCA_INS_CHORUS <= NTRK && FXL_N1 + FELUCCA_INS_DELAY <= NTRK, "fxi_own: NTRK instances");

AINL uint32_t fxi_holds(uint32_t k, uint32_t ty)        /* track k holds type ty in a slot (and it is built) */
{
    return (ins_slot[k][0] == ty || ins_slot[k][1] == ty) && (FXT_BUILT >> ty & 1u);
}
AINL uint32_t fxl_send_on(uint32_t j) { return FXS_ON(j ? FXT_DLY : FXT_CHO); }   /* the send in a master slot */
static void fxi_publish(uint32_t k, uint32_t j, uint32_t i)   /* track k's instance of type FXT_LIM0 + j: i */
{
    fm1_irq_off();
    fxi_at[k][j] = (uint8_t)i;
    fm1_irq_on();
}
static void fxi_give(uint32_t j, uint32_t i)            /* instance i of type FXT_LIM0 + j back (its track's link gone) */
{
    fxi_publish(fxi_own[j][i], j, FXI_NONE);
    fxi_own[j][i] = FXI_NONE;
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
    (void)i, (void)v;
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
        int16_t *ln = i < FELUCCA_INS_DELAY ? ifx_echo_ln[i] : dly_buf + (i - FELUCCA_INS_DELAY) * IFX_EC_LEN;
        memset(&ifx_echo[i], 0, sizeof ifx_echo[i]);
        memset(ln, 0, IFX_EC_LEN * sizeof *ln);
        ifx_echo[i].line = ln;
    }
#endif
    (void)i;
}
/* a free instance of type FXT_LIM0 + j: the builder's own first, then the send's line while lent; FXI_NONE: none */
static uint32_t fxi_find(uint32_t j)
{
    uint32_t i, n = fxl_st[j] == FXL_LENT ? FXI_TOT(j) : FXI_CAP[j];
    for (i = 0; i < n; i++)
        if (fxi_own[j][i] == FXI_NONE)
            return i;
    return FXI_NONE;
}
/* one type's instances after a change (load: a project load) */
static void fxi_sync(uint32_t j, int load)
{
    uint32_t ty = FXT_LIM0 + j, k, i, want = 0, n = 0;
    uint32_t avail = FXI_CAP[j] + (fxl_send_on(j) ? 0u : FXI_LND[j]);   /* (a load: the first avail tracks holding it) */
    for (k = 0; k < NTRK; k++)
        if (fxi_holds(k, ty) && (!load || n++ < avail))
            want |= 1u << k;
    for (i = 0; i < FXI_TOT(j); i++) {                  /* instances whose track no longer wants them: back */
        k = fxi_own[j][i];
        if (k == FXI_NONE || (want >> k & 1u))
            continue;
        if (load || !ifx_lim_w(ty, k))
            fxi_give(j, i);
        else
            fxi_pend = 1;                               /* (fading out: the next pass) */
    }
    fxi_full[j] = 0;
    for (k = 0; k < NTRK; k++) {                        /* tracks wanting one and without: a free one, in track order */
        if (!(want >> k & 1u) || fxi_at[k][j] != FXI_NONE)
            continue;
        i = fxi_find(j);
        if (i == FXI_NONE) {
            fxi_full[j] |= (uint8_t)(1u << k);          /* (none left: bypassed) */
            continue;
        }
        fxi_clear(j, i);                                /* (not in use: the audio does not see it) */
        fxi_own[j][i] = (uint8_t)k;
        fxi_publish(k, j, i);
    }
    if (load)                                           /* (a track past the count: asks again when one is given back) */
        for (k = 0; k < NTRK; k++)
            if (fxi_holds(k, ty) && fxi_at[k][j] == FXI_NONE)
                fxi_full[j] |= (uint8_t)(1u << k);
}

/* ---- Lending (above): the send line j (0 CHO -> CHRS, 1 DLY -> ECHO) */
/* the bus idle and not wanted: the gate set, in one section with the interrupts off (no block in between); 1 lent */
static uint32_t fxl_take(uint32_t j)
{
    uint32_t ok;
    fm1_irq_off();
    ok = !fxl_send_on(j) && (j ? fx.dly_q >= DLY_LEN && !fx.dly_lp && !fxz.dclr : fx.cho_q >= CHO_LEN && !fxz.cclr);
    if (ok)
        fxl_lent |= (uint8_t)(1u << j);
    fm1_irq_on();
    return ok;
}
/* every slice it lent zeroed (the bus's idle state: its line all zeros), the gate cleared: the bus's again */
static void fxl_return(uint32_t j)
{
#if FELUCCA_INS_CHORUS
    if (j == 0u)
        memset(cho_buf, 0, FXI_LND[0] * IFX_CH_LEN * sizeof cho_buf[0]);
#endif
#if FELUCCA_INS_DELAY
    if (j == 1u)
        memset(dly_buf, 0, FXI_LND[1] * IFX_EC_LEN * sizeof dly_buf[0]);
#endif
    fm1_irq_off();
    fxl_lent &= (uint8_t)~(1u << j);
    fm1_irq_on();
    fxl_st[j] = FXL_SEND;
}
/* the line's owner after the pass's sync; 1: just lent (a sync again hands its slices out) */
static uint32_t fxl_step(uint32_t j)
{
    uint32_t i, ty = FXT_LIM0 + j, held = 0, on = fxl_send_on(j);
    if (!FXI_LND[j])
        return 0;
    if (fxl_st[j] == FXL_SEND) {
        if (!fxi_full[j] || on)
            return 0;
        if (fxl_take(j)) {
            fxl_st[j] = FXL_LENT;
            return 1;
        }
        fxi_pend = 1;                                   /* (its tail still rings: a later pass) */
        return 0;
    }
    if (fxl_st[j] == FXL_BACK && !on)
        fxl_st[j] = FXL_LENT;                           /* (out of the slots again: the inserts stay, fade back in) */
    else if (fxl_st[j] == FXL_LENT && on)
        fxl_st[j] = FXL_BACK;
    for (i = FXI_CAP[j]; i < FXI_TOT(j); i++) {
        uint32_t k = fxi_own[j][i];
        if (k == FXI_NONE)
            continue;
        fxi_evict(j, i, fxl_st[j] == FXL_BACK);
        if (fxl_st[j] == FXL_BACK && !ifx_lim_w(ty, k))
            fxi_give(j, i);                             /* (faded out: its slice back) */
        else
            held = 1;
    }
    if (held) {
        if (fxl_st[j] == FXL_BACK)
            fxi_pend = 1;                               /* (fading: the next pass) */
        return 0;
    }
    fxl_return(j);                                      /* (none in use: the line back to its bus) */
    fxi_pend = 1;                                       /* (a pass more: a track it served asks again, FULL or own) */
    return 0;
}

/* the main loop, every pass (main.c): the instances after a change of slots or a load; nothing to do costs a compare */
static void fxi_poll(void)
{
    uint32_t d, j;
    if (!fxi_dirty && !fxi_pend)
        return;
    fm1_irq_off();
    d = fxi_dirty;
    fxi_dirty = 0;
    fm1_irq_on();
    fxi_pend = 0;
    for (j = 0; j < FXI_NL; j++)
        if (FXI_CAP[j]) {
            fxi_sync(j, (d & 2u) != 0u);
            if (fxl_step(j))
                fxi_sync(j, 0);                         /* (just lent: its slices to the tracks waiting) */
        }
    for (j = 0; j < FXI_NL; j++)
        if (FXI_CAP[j] && (fxi_full[j] || fxl_st[j] != FXL_SEND))
            fxi_pend = 1;                               /* (a starved track takes one given back; a lent line is
                                                         * watched: its send back in a slot, none using it) */
}
/* boot, the host tests: every instance free, every line the send's */
static void fxi_init(void)
{
    uint32_t j, k;
    for (j = 0; j < 2u; j++) {
        for (k = 0; k < NTRK; k++)
            fxi_own[j][k] = FXI_NONE, fxi_at[k][j] = FXI_NONE;
        if (fxl_st[j] != FXL_SEND)
            fxl_return(j);
    }
    fxi_full[0] = fxi_full[1] = 0;
    fxi_pend = 0;
    fxi_dirty = 2;
}
#define FXI_LIM(t) FXT_IS_LIM(t)
/* what the send's line can lend now (0: in a master slot, or taking it back) */
AINL uint32_t fxl_can(uint32_t j) { return fxl_send_on(j) || fxl_st[j] == FXL_BACK ? 0u : FXI_LND[j]; }
static uint32_t fxi_cap(uint32_t t)
{
    if (!FXI_LIM(t))
        return t < FXT_N && (FXT_BUILT >> t & 1u) ? FXI_ANY : 0u;
    return FXI_CAP[t - FXT_LIM0] ? FXI_CAP[t - FXT_LIM0] + fxl_can(t - FXT_LIM0) : 0u;
}
static uint32_t fxi_count(uint32_t t, uint32_t from)    /* instances from `from` on handed out */
{
    uint32_t i, n = 0;
    for (i = from; i < FXI_TOT(t - FXT_LIM0); i++)
        n += fxi_own[t - FXT_LIM0][i] != FXI_NONE;
    return n;
}
static uint32_t fxi_used(uint32_t t) { return FXI_LIM(t) ? fxi_count(t, 0) : fxi_cap(t) ? FXI_ANY : 0u; }
static uint32_t fxi_lent(uint32_t t) { return FXI_LIM(t) ? fxi_count(t, FXI_CAP[t - FXT_LIM0]) : 0u; }
static uint32_t fxi_free(uint32_t t)
{
    uint32_t c = fxi_cap(t), u = fxi_used(t);
    return !FXI_LIM(t) ? (c ? FXI_ANY : 0u) : c > u ? c - u : 0u;
}
static uint32_t fxi_state(uint32_t k, uint32_t s)
{
    uint32_t t = ins_slot[k % NTRK][s & 1u], j = t - FXT_LIM0;
    if (!FXI_LIM(t) || !FXI_CAP[j])
        return FXI_NA;
    if (fxi_at[k % NTRK][j] != FXI_NONE)
        return FXI_OK;
    if (fxi_dirty || (fxl_st[j] == FXL_SEND && fxl_can(j) && fxi_lent(t) < FXI_LND[j]))
        return FXI_WAIT;                                /* (the next pass, or the send's tail ending) */
    return fxi_full[j] >> (k % NTRK) & 1u ? FXI_FULL : FXI_WAIT;
}
static int fxi_can(uint32_t k, uint32_t t)
{
    if (!FXI_LIM(t))
        return t < FXT_N && (FXT_BUILT >> t & 1u);
    return FXI_CAP[t - FXT_LIM0] && (fxi_at[k % NTRK][t - FXT_LIM0] != FXI_NONE || fxi_free(t) > 0u);
}
#else
#define fxi_poll() ((void)0)
#define fxi_init() ((void)0)
static uint32_t fxi_cap(uint32_t t) { return t < FXT_N && (FXT_BUILT >> t & 1u) ? FXI_ANY : 0u; }
#define fxi_used(t) fxi_cap(t)
#define fxi_free(t) fxi_cap(t)
#define fxi_lent(t) ((void)(t), 0u)
#define fxi_state(k, s) ((void)(k), (void)(s), (uint32_t)FXI_NA)
#define fxi_can(k, t) ((void)(k), fxi_cap(t) != 0u)
#endif
