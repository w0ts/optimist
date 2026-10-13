/* SPDX-License-Identifier: GPL-3.0-only */
/* The instances of the limited insert types (2026-10-13; fxi_kern.c: CHRS ECHO, a builder count each, registry.h
 * FELUCCA_INS_CHORUS FELUCCA_INS_DELAY). Included by fx.c after insfx.c. The main loop's side: no instance is handed
 * out or cleared in the audio ISR.
 *   - An instance belongs to a track and a type (a type is once a track at most, D3): a swap of S1 and S2 keeps it.
 *   - A change of slots (fx_slots.c ins_put, which may run anywhere: FXI_ASK(1)) is followed in fxi_poll (main.c, every
 *     pass of the main loop): a track that no longer holds the type gives its instance back once its wet share has
 *     faded to 0 (no click; until then it fades as any insert in no slot); a track holding the type and none yet takes
 *     a free one, the tracks in order T1 T2 T3 DR. The instance is zeroed here, then published with the interrupts off.
 *   - A project load (fx_rec.c fxr_decode: FXI_ASK(2)): the tracks holding the type in track order get the instances
 *     while they last (a track that keeps the type keeps its own), every other instance is given back at once: the same
 *     project always plays the same way, whatever came before.
 *   - A track holding the type with no instance left: the type is bypassed there (fxi_state FXI_FULL, for the UI),
 *     until one is given back.
 * The send buses (CHO DLY REV) are one instance each, always theirs: the instances here are the inserts' only, their
 * pool beside the sends' lines (the builder counts both).
 *
 * The UI's queries (main loop):
 *   fxi_cap(t)       instances built of type t: FXI_ANY a type on every track (not limited), 0 not built
 *   fxi_used(t)      handed out now (FXI_ANY not limited)
 *   fxi_free(t)      fxi_cap - fxi_used (FXI_ANY not limited)
 *   fxi_state(k, s)  track k's slot s: FXI_NA (empty, or a type not limited), FXI_OK (its instance), FXI_WAIT (asked,
 *                    the next pass gives it), FXI_FULL (none free: bypassed)
 *   fxi_can(k, t)    1: type t picked on track k would be heard (not limited, held there with an instance, or one free) */
#define FXI_ANY 0xFFu
enum { FXI_NA, FXI_OK, FXI_WAIT, FXI_FULL };
#if INS_LIM
_Static_assert(NTRK == 4, "fxi_own: an instance a track at most");
static uint8_t fxi_own[2][NTRK] = {{FXI_NONE, FXI_NONE, FXI_NONE, FXI_NONE}, {FXI_NONE, FXI_NONE, FXI_NONE, FXI_NONE}};
                                                        /* each instance's track (FXI_NONE: free); the main loop's only */
static uint8_t fxi_full[2];                             /* the tracks (a bit each) holding the type with none left */
static uint8_t fxi_pend;                                /* an instance waits for its fade-out before it is given back */

AINL uint32_t fxi_holds(uint32_t k, uint32_t ty)        /* track k holds type ty in a slot (and it is built) */
{
    return (ins_slot[k][0] == ty || ins_slot[k][1] == ty) && (FXT_BUILT >> ty & 1u);
}
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
static void fxi_clear(uint32_t j, uint32_t i)           /* instance i zeroed: no old line, at rest */
{
#if FELUCCA_INS_CHORUS
    if (j == 0u)
        memset(&ifx_chrs[i], 0, sizeof ifx_chrs[i]);
#endif
#if FELUCCA_INS_DELAY
    if (j == 1u)
        memset(&ifx_echo[i], 0, sizeof ifx_echo[i]);
#endif
    (void)i;
}
/* one type's instances after a change (load: a project load) */
static void fxi_sync(uint32_t j, int load)
{
    uint32_t ty = FXT_LIM0 + j, k, i, cap = FXI_CAP[j], want = 0, n = 0;
    for (k = 0; k < NTRK; k++)                          /* (a load: the first cap tracks holding it) */
        if (fxi_holds(k, ty) && (!load || n++ < cap))
            want |= 1u << k;
    for (i = 0; i < cap; i++) {                         /* instances whose track no longer wants them: back */
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
        for (i = 0; i < cap && fxi_own[j][i] != FXI_NONE; i++)
            ;
        if (i == cap) {
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
        if (FXI_CAP[j])
            fxi_sync(j, (d & 2u) != 0u);
    if (fxi_pend || (fxi_full[0] | fxi_full[1]))
        for (j = 0; j < FXI_NL; j++)                    /* (a starved track takes an instance given back meanwhile) */
            if (FXI_CAP[j] && fxi_full[j])
                fxi_pend = 1;
}
/* boot, the host tests: every instance free */
static void fxi_init(void)
{
    uint32_t j, k;
    for (j = 0; j < 2u; j++)
        for (k = 0; k < NTRK; k++)
            fxi_own[j][k] = FXI_NONE, fxi_at[k][j] = FXI_NONE;
    fxi_full[0] = fxi_full[1] = 0;
    fxi_pend = 0;
    fxi_dirty = 2;
}
#define FXI_LIM(t) ((t) >= FXT_LIM0 && (t) < FXT_N)
static uint32_t fxi_cap(uint32_t t) { return FXI_LIM(t) ? FXI_CAP[t - FXT_LIM0] : t < FXT_N && (FXT_BUILT >> t & 1u) ? FXI_ANY : 0u; }
static uint32_t fxi_used(uint32_t t)
{
    uint32_t i, n = 0;
    if (!FXI_LIM(t))
        return fxi_cap(t) ? FXI_ANY : 0u;
    for (i = 0; i < FXI_CAP[t - FXT_LIM0]; i++)
        n += fxi_own[t - FXT_LIM0][i] != FXI_NONE;
    return n;
}
static uint32_t fxi_free(uint32_t t) { return FXI_LIM(t) ? fxi_cap(t) - fxi_used(t) : fxi_cap(t) ? FXI_ANY : 0u; }
static uint32_t fxi_state(uint32_t k, uint32_t s)
{
    uint32_t t = ins_slot[k % NTRK][s & 1u];
    if (!FXI_LIM(t) || !FXI_CAP[t - FXT_LIM0])
        return FXI_NA;
    if (fxi_at[k % NTRK][t - FXT_LIM0] != FXI_NONE)
        return FXI_OK;
    return (fxi_full[t - FXT_LIM0] >> (k % NTRK) & 1u) && !fxi_dirty ? FXI_FULL : FXI_WAIT;
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
#define fxi_state(k, s) ((void)(k), (void)(s), (uint32_t)FXI_NA)
#define fxi_can(k, t) ((void)(k), fxi_cap(t) != 0u)
#endif
