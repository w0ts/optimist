/* SPDX-License-Identifier: GPL-3.0-only */
/* The instances of the limited insert types (2026-10-13; fxi_kern.c: CHRS ECHO JUNO CHORUS FLNG XDLY, each built or not,
 * registry.h FELUCCA_INS_*) and their lines in the effects memory (fxi_mem.h, 2026-10-23: one memory for the delay send's
 * line and every insert's; how many of a type play at once is what it holds). An owner is a track, the master (FXI_MST:
 * its insert slots), the USB input (FXI_UT) or a return (FXI_RT0 + j). Included by fx.c after insfx.c. The main loop's
 * side: no instance is placed, handed out or cleared in the audio ISR.
 *   - An instance belongs to an owner and a type (a type is once an owner at most, D3): a swap of S1 and S2 keeps it.
 *   - A change of slots (fx_slots.c ins_put, which may run anywhere: FXI_ASK(1)) is followed in fxi_poll (main.c, every
 *     pass of the main loop): an owner that no longer holds the type gives its instance back once its wet share has
 *     faded to 0 (no click; until then it fades as any insert in no slot), its line's units free again; an owner holding
 *     the type and none yet takes a free instance and units for its line (fxa_place: the memory's own room first, then
 *     a send's line while lent), the types in order CHRS ECHO JUNO FLNG XDLY, the owners in order T1 T2 T3 DR, the
 *     master, the USB input, A B C. The instance and its line are zeroed here, then published with the interrupts off.
 *   - A project load (fx_rec.c fxr_decode: FXI_ASK(2)): every instance is given back at once, then handed out again in
 *     that order while the memory has room: the same project always plays the same way, whatever came before.
 *   - An owner holding the type with no room left: the type is bypassed there (fxi_state FXI_FULL, for the UI), until
 *     some is given back.
 *
 * The memory's units (fxi_mem.h): the delay send's line (units 0 .. FXA_S - 1), the inserts' own (.. FXA_N - 1), the
 * chorus send's line (.. FXA_T - 1). A bitmap marks the units in use (fxa_bm): never two lines on a unit. A line is a
 * run of free units in one region (the own room: XDLY from the bottom, the smaller lines from the top, so they do not
 * cut each other's room; a lent line: from its start).
 *
 * Lending (2026-10-13). A send whose type is in no master slot (FX > SLOTS: CHO or DLY) is not heard, and its line
 * idles: it lends it to the inserts (any type whose line fits: CHO's 2048 samples hold CHRS JUNO FLNG, DLY's every
 * type). Only on demand: when an owner holding a type that fits has none left.
 * Each line has one owner at a time, the send's bus or the inserts, and the owner changes only here:
 *   SEND -> LENT  the send in no master slot and its bus idle (a full line of zeros written, no clear pending: fx.c
 *                 fx_buses' own idle test), checked and the gate set (fx_slots.c fxl_lent) in one interrupts-off
 *                 section: the bus has not run since its test and runs no more (fx.c fx_ret_block: gated, no step,
 *                 no clear). Then lines are placed on it, zeroed and published as any instance.
 *   LENT -> BACK  the send back in a master slot: the inserts on its line fade out (their evict flag: amount 0, 2.9 ms),
 *                 the bus stays gated (silent) meanwhile
 *   BACK -> SEND  every lent instance faded and given back (or LENT with none in use): the units handed out since it was
 *                 lent zeroed (the idle state the bus expects), then the gate cleared with the interrupts off; the bus
 *                 starts from silence
 *   BACK -> LENT  the send out of the slots again before that: the evict flags off, the inserts fade back in
 * Two cores: the second core runs any part's mix_part, its inserts with it, inside CPU0's audio interrupt (dual.c
 * mix_block_dual: fork, join, then fx_buses on CPU0); it never runs outside it. A section here with the interrupts off
 * is therefore between two blocks for both cores: the gate and every fxi_at change fall on a block boundary, and in no
 * block do the bus and an insert both touch a line. The instances' lines and states are written here only while no
 * fxi_at points at them (not yet published, or given back), so neither core is using them. (CPU1's writes reach memory
 * before its done flag: dual_join's fm1_dual_sync; ours before a block's post: mix_block_dual's.)
 *
 * The UI's queries (main loop):
 *   fxi_cap(t)       instances of type t now: those out and those the memory can still place (fxi_free); FXI_ANY a
 *                    type on every track (not limited), 0 not built
 *   fxi_used(t)      handed out now (FXI_ANY not limited)
 *   fxi_free(t)      how many more the memory can place now: its own room, and a send's line not in a master slot
 *   fxi_lent(t)      of those used, on a send's line
 *   fxi_state(k, s)  track k's slot s: FXI_NA (empty, or a type not limited), FXI_OK (its instance), FXI_WAIT (asked,
 *                    a pass gives it: the next, or once the send's tail has ended), FXI_FULL (no room: bypassed)
 *   fxi_can(k, t)    1: type t picked on track k would be heard (not limited, held there with an instance, or room) */
#define FXI_ANY 0xFFu
enum { FXI_NA, FXI_OK, FXI_WAIT, FXI_FULL };
#if INS_LIM
enum { FXL_SEND, FXL_LENT, FXL_BACK };
#define FXL_NS 2u                                       /* the send lines: 0 the chorus's, 1 the delay's */
#define FXL_LO(s) ((s) ? 0u : FXA_N)                    /* (send line s's units: FXL_LO .. FXL_HI - 1) */
#define FXL_HI(s) ((s) ? FXA_S : FXA_T)
#define FXA_NO 0xFFFFu
#define FXA_BMW ((FXA_T + 31u) / 32u)
/* the main loop's only, in the pool (main RAM is scarcer): each instance's owner (FXI_NONE: free) and its line's first
 * unit (while handed out), the units in use (a bit each), each send line's units handed out since it was lent (from its
 * start: zeroed when it goes back) */
static uint8_t fxi_own[FXI_NL][FXI_IMAX] IN_POOL;
static uint16_t fxa_u[FXI_NL][FXI_IMAX] IN_POOL;
static uint32_t fxa_bm[FXA_BMW ? FXA_BMW : 1u] IN_POOL;
static uint16_t fxa_hi[FXL_NS] IN_POOL;
static uint16_t fxi_full[FXI_NL];                       /* the owners (a bit each) holding the type with none left */
static uint8_t fxi_pend;                                /* a pass is due: a fade-out, a tail, a starved owner */
static uint8_t fxl_st[FXL_NS];                          /* each send line's owner (FXL_*: above); the main loop's only */
#if FXA_ROOM                                             /* the own room in the arena (fxi_mem.h; fx/fx_arena.c after) */
static void fxm_ins_refused(void);
static void fxm_ins_ok(void);
#define FXR_K (NPART + 3u)                              /* (its arena client: fx_arena.c FXM_K(FXM_INS)) */
#define FXR_UB (FXA_U * 2u)                             /* (a unit's bytes) */
static uint16_t fxr_lo, fxr_hi;                         /* the room: the arena's units fxr_lo .. fxr_hi - 1 (none: equal) */
static uint32_t fxr_pk;                                 /* arn_room() + 1 when a pack was last planned for it (0 never) */
static uint8_t fxr_no;                                  /* fxa_own's last: refused (no room, nothing to move) */
#endif
_Static_assert(FXI_TN0 <= FXI_IMAX && FXI_TN1 <= FXI_IMAX && FXI_TN2 <= FXI_IMAX && FXI_TN3 <= FXI_IMAX &&
               FXI_TN4 <= FXI_IMAX && FXI_NK <= 16u, "fxi_own: FXI_IMAX instances; fxi_full: an owner a bit");

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
/* send line s in a master slot */
AINL uint32_t fxl_send_on(uint32_t s) { return FXS_HAS(s ? FXT_DLY : FXT_CHO); }
/* the region of unit u: a send line s (0, 1), or the own room (FXL_NS) */
AINL uint32_t fxa_reg(uint32_t u) { return u < FXA_S ? 1u : u >= FXA_N ? 0u : FXL_NS; }
/* instance i of type j is on a send's line */
AINL uint32_t fxi_onl(uint32_t j, uint32_t i) { return fxa_reg(fxa_u[j][i]) < FXL_NS; }
/* send line s holds a line of type j (built, and its line no longer than the send's) */
AINL uint32_t fxl_fits(uint32_t s, uint32_t j) { return FXI_TN[j] && FXA_SZ(j) <= FXL_HI(s) - FXL_LO(s); }
static uint32_t fxl_lends(uint32_t s)                   /* send line s can lend to a type built */
{
    uint32_t j, n = 0;
    for (j = 0; j < FXI_NL; j++)
        n |= fxl_fits(s, j);
    return n;
}
/* ---- The units (fxa_bm) */
AINL uint32_t fxa_busy(uint32_t u) { return fxa_bm[u >> 5] >> (u & 31u) & 1u; }
static void fxa_mark(uint32_t u, uint32_t n, uint32_t v)   /* units u .. u + n - 1 in use (v 1) or free (v 0) */
{
    for (; n; n--, u++) {
        if (v)
            fxa_bm[u >> 5] |= 1u << (u & 31u);
        else
            fxa_bm[u >> 5] &= ~(1u << (u & 31u));
    }
}
/* n free units in a row in lo .. hi - 1: the lowest (up) or the highest; FXA_NO: none */
static uint32_t fxa_fit(uint32_t lo, uint32_t hi, uint32_t n, uint32_t up)
{
    uint32_t u, run = 0;
    if (up) {
        for (u = lo; u < hi; u++)
            if ((run = fxa_busy(u) ? 0u : run + 1u) == n)
                return u + 1u - n;
    } else {
        for (u = hi; u-- > lo;)
            if ((run = fxa_busy(u) ? 0u : run + 1u) == n)
                return u;
    }
    return FXA_NO;
}
static uint32_t fxa_fits(uint32_t lo, uint32_t hi, uint32_t n)   /* how many lines of n units the free runs hold */
{
    uint32_t u, run = 0, m = 0;
    for (u = lo; u < hi; u++)
        if ((run = fxa_busy(u) ? 0u : run + 1u) == n)
            m++, run = 0;
    return m;
}
/* send line s takes lines now (lent, not on its way back) */
AINL uint32_t fxl_open(uint32_t s) { return fxl_st[s] == FXL_LENT && !fxl_send_on(s); }
#if FXA_ROOM
/* The own room in the arena (fxi_mem.h): the arena's units free outside the room (a bit each: a copy of the arena
 * taken with the interrupts off, read with them on), under the send lines' places (fx_arena.c fxm_lim) */
#define FXR_BMW ((FXA_I + 31u) / 32u)
#ifndef FXR_LIM
#define FXR_LIM ARN_LIM                                 /* (the room under the send lines' places) */
#endif
static uint32_t fxr_map(uint32_t *m)                    /* -> the units under the places; m: those free */
{
    arn_st_t T;
    uint32_t u, lim = FXR_LIM / FXR_UB;
    fm1_irq_off();
    T = arn;
    fm1_irq_on();
    memset(m, 0, FXR_BMW * 4u);
    for (u = 0; u < lim; u++)
        if ((u >= fxr_lo && u < fxr_hi) || arn_clear(&T, u * FXR_UB, FXR_UB, 1u << FXR_K))
            m[u >> 5] |= 1u << (u & 31u);
    return lim;
}
/* unit u free for a line: in the room, not in use; outside it, nothing of the arena there (m: fxr_map) */
AINL uint32_t fxr_free(const uint32_t *m, uint32_t u)
{
    if (u >= fxr_lo && u < fxr_hi)
        return !fxa_busy(FXA_S + u);
    return m[u >> 5] >> (u & 31u) & 1u;
}
static uint8_t fxr_again;                               /* a trim due once the room has moved (fxr_moved) */
/* the room after a line given back: its free units at either edge back to the arena (none left: the block back) */
static void fxr_trim(void)
{
    if (FXR_MOVING()) {
        fxr_again = 1;                                  /* (its place changes: after the move, fxi_poll) */
        return;
    }
    fxr_again = 0;
    while (fxr_lo < fxr_hi && !fxa_busy(FXA_S + fxr_lo))
        fxr_lo++;
    while (fxr_hi > fxr_lo && !fxa_busy(FXA_S + fxr_hi - 1u))
        fxr_hi--;
    fm1_irq_off();
    if (fxr_hi > fxr_lo)
        (void)arn_resize(FXR_K, fxr_lo * FXR_UB, (uint32_t)(fxr_hi - fxr_lo) * FXR_UB);   /* (smaller: always fits) */
    else
        arn_give(FXR_K), fxr_lo = fxr_hi = 0;
    fm1_irq_on();
}
/* the arena's free units in a row next to unit u: those under it (down), or from it up to lim (m: fxr_map) */
static uint32_t fxr_run(const uint32_t *m, uint32_t u, uint32_t lim, uint32_t down)
{
    uint32_t r = 0, v;
    for (;; r++) {
        if (down ? r >= u : u + r >= lim)
            return r;
        v = down ? u - r - 1u : u + r;
        if (!(m[v >> 5] >> (v & 31u) & 1u))
            return r;
    }
}
/* a line of n units in the own room: in its free units (XDLY from the bottom, the smaller from the top), else the room
 * grown at an edge by the fewest units (its free units there counted; of two alike the top), no room yet: n units at
 * the highest place under the send lines' places; its block resized. None: the arena's parts moved when the free room
 * holds it (core/arena.c arn_pack: tried again after), else refused (fxr_no: FULL, MEM FULL INSERTS once). FXA_NO: none
 * now. (Measured, tests/arena_frag_test.c: against one fixed block of one line of each type the owners without their
 * line 24 % against 66 % in 96 KB) */
static uint32_t fxa_own(uint32_t n)
{
    uint32_t m[FXR_BMW ? FXR_BMW : 1u], lim, p = FXA_NO, a = 0, b = 0, eb = 0, et = 0, room, ok;
    uint32_t has = fxr_hi > fxr_lo;
    fxr_no = 0;
    if (FXR_MOVING() || PART_MEM_SHORT() || arn_busy()) {
        fxi_pend = 1;                                   /* (the room moving, a part's refused change first, a move) */
        return FXA_NO;
    }
    if (has && (p = fxa_fit(FXA_S + fxr_lo, FXA_S + fxr_hi, n, n > FXA_Z1)) != FXA_NO)
        return p;
    lim = fxr_map(m);
    if (has) {
        while (eb < n && fxr_lo + eb < fxr_hi && !fxa_busy(FXA_S + fxr_lo + eb))
            eb++;                                       /* (its free units at the bottom, then at the top) */
        while (et < n && fxr_hi - et > fxr_lo && !fxa_busy(FXA_S + fxr_hi - 1u - et))
            et++;
        if (fxr_run(m, fxr_hi, lim, 0) >= n - et && (et >= eb || fxr_run(m, fxr_lo, lim, 1) < n - eb))
            a = fxr_lo, b = fxr_hi + n - et, p = fxr_hi - et;
        else if (fxr_run(m, fxr_lo, lim, 1) >= n - eb)
            a = fxr_lo - (n - eb), b = fxr_hi, p = a;
    } else {
        for (b = lim; b >= n && fxr_run(m, b, lim, 1) < n; b--)
            ;
        a = b - n, p = a;
        if (b < n)
            p = FXA_NO;
    }
    if (p != FXA_NO) {
        fm1_irq_off();
        ok = !arn_resize(FXR_K, a * FXR_UB, (b - a) * FXR_UB);   /* (checked again: the audio may have reserved) */
        fm1_irq_on();
        if (ok) {
            fxr_lo = (uint16_t)a, fxr_hi = (uint16_t)b, fxr_pk = 0;
            return FXA_S + p;
        }
        fxi_pend = 1;
        return FXA_NO;
    }
    room = arn_room() + 1u;
    if (fxr_pk != room) {
        fxr_pk = room;
        if (has ? fxr_lo >= n - eb && arn_pack(FXR_K, (n - eb) * FXR_UB, (fxr_lo - (n - eb)) * FXR_UB)
                : arn_pack(FXR_K, (n + 1u) * FXR_UB, ARN_BYTES)) {
            fxi_pend = 1;                               /* (the parts' blocks moved: again after) */
            return FXA_NO;
        }
    }
    fxr_no = 1;
    return FXA_NO;
}
#else
#define fxa_own(n) fxa_fit(FXA_S, FXA_N, (n), (n) > FXA_Z1)
#define fxr_no 0
#endif
/* a place for a line of type j: the own room (XDLY from the bottom, the smaller lines from the top), else a send's line
 * while lent (the chorus's first: the smaller); FXA_NO: no room */
static uint32_t fxa_place(uint32_t j)
{
    uint32_t n = FXA_SZ(j), p = fxa_own(n), s;
    for (s = 0; s < FXL_NS && p == FXA_NO; s++)
        if (fxl_open(s) && fxl_fits(s, j))
            p = fxa_fit(FXL_LO(s), FXL_HI(s), n, 1);
#if FXA_ROOM
    if (p == FXA_NO && fxr_no)
        fxm_ins_refused();                              /* (no room, nothing to move, no lent line: MEM FULL once) */
#endif
    return p;
}

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
    fxa_mark(fxa_u[j][i], FXA_SZ(j), 0);                /* (its units free: no core reads them now) */
#if FXA_ROOM
    if (fxa_reg(fxa_u[j][i]) == FXL_NS)
        fxr_trim();                                     /* (the room's free edges back to the arena) */
#endif
}
/* instance i's state (not published: no core uses it): the evict flag */
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
#if FELUCCA_INS_JCHORUS
    if (j == 2u)
        ifx_jcho[i].evict = (uint8_t)v;
#endif
#if FELUCCA_INS_FLANGER
    if (j == 3u)
        ifx_flng[i].evict = (uint8_t)v;
#endif
#if FELUCCA_INS_XDLY
    if (j == 4u)
        ifx_xdly[i].evict = (uint8_t)v;
#endif
    (void)i, (void)v;
}
static void fxi_clear(uint32_t j, uint32_t i)           /* instance i zeroed, at rest, on its line (zeroed too) */
{
    int16_t *ln = fxa_line(fxa_u[j][i]);
    memset(ln, 0, FXA_SZ(j) * FXA_U * sizeof *ln);
#if FELUCCA_INS_CHORUS
    if (j == 0u)
        memset(&ifx_chrs[i], 0, sizeof ifx_chrs[i]), ifx_chrs[i].line = ln;
#endif
#if FELUCCA_INS_DELAY
    if (j == 1u)
        memset(&ifx_echo[i], 0, sizeof ifx_echo[i]), ifx_echo[i].line = ln;
#endif
#if FELUCCA_INS_JCHORUS
    if (j == 2u)
        memset(&ifx_jcho[i], 0, sizeof ifx_jcho[i]), ifx_jcho[i].c.ln = (juno_cho_s *)(void *)ln;   /* (4-byte aligned) */
#endif
#if FELUCCA_INS_FLANGER
    if (j == 3u)
        memset(&ifx_flng[i], 0, sizeof ifx_flng[i]), ifx_flng[i].line = ln;
#endif
#if FELUCCA_INS_XDLY
    if (j == 4u)
        memset(&ifx_xdly[i], 0, sizeof ifx_xdly[i]), ifx_xdly[i].line = ln;
#endif
    (void)i;
}
#if FXA_ROOM
/* (core/arena.c, the interrupts off, the room's block copied to p: engines.c ARN_MV_MOVED) its units, its instances'
 * units and lines, the units in use: where they are now; nothing else changes (the kernels skipped it meanwhile) */
static void fxr_moved(void *p)
{
    uint32_t j, i, u, z = (uint32_t)((uint8_t *)p - arn_mem) / FXR_UB, lo = fxr_lo, hi = fxr_hi, d = z - lo;
    for (u = lo; u < hi; u++)
        fxa_mark(FXA_S + u, 1u, 0);
    for (j = 0; j < FXI_NL; j++)
        for (i = 0; i < FXI_TN[j]; i++) {
            int16_t *ln;
            if (fxi_own[j][i] == FXI_NONE || fxa_reg(fxa_u[j][i]) != FXL_NS)
                continue;
            fxa_u[j][i] = (uint16_t)(fxa_u[j][i] + d);  /* (unsigned: a move down wraps back) */
            ln = fxa_line(fxa_u[j][i]);
#if FELUCCA_INS_CHORUS
            if (j == 0u)
                ifx_chrs[i].line = ln;
#endif
#if FELUCCA_INS_DELAY
            if (j == 1u)
                ifx_echo[i].line = ln;
#endif
#if FELUCCA_INS_JCHORUS
            if (j == 2u)
                ifx_jcho[i].c.ln = (juno_cho_s *)(void *)ln;
#endif
#if FELUCCA_INS_FLANGER
            if (j == 3u)
                ifx_flng[i].line = ln;
#endif
#if FELUCCA_INS_XDLY
            if (j == 4u)
                ifx_xdly[i].line = ln;
#endif
            (void)ln;
        }
    fxr_lo = (uint16_t)z, fxr_hi = (uint16_t)(z + hi - lo);
    for (j = 0; j < FXI_NL; j++)
        for (i = 0; i < FXI_TN[j]; i++)
            if (fxi_own[j][i] != FXI_NONE && fxa_reg(fxa_u[j][i]) == FXL_NS)
                fxa_mark(fxa_u[j][i], FXA_SZ(j), 1);
    fxi_pend = 1;                                       /* (a pass: a trim asked meanwhile, the owners waiting) */
}
#endif
/* a free instance of type FXI_TY(j) and its line placed (fxa_place), its units taken; FXI_NONE: none, or no room */
static uint32_t fxi_find(uint32_t j)
{
    uint32_t i, f = FXI_NONE, p, s;
    for (i = 0; i < FXI_TN[j] && f == FXI_NONE; i++)
        if (fxi_own[j][i] == FXI_NONE)
            f = i;
    if (f == FXI_NONE || (p = fxa_place(j)) == FXA_NO)
        return FXI_NONE;
    fxa_u[j][f] = (uint16_t)p;
    fxa_mark(p, FXA_SZ(j), 1);
    s = fxa_reg(p);
    if (s < FXL_NS && p + FXA_SZ(j) - FXL_LO(s) > fxa_hi[s])
        fxa_hi[s] = (uint16_t)(p + FXA_SZ(j) - FXL_LO(s));
    return f;
}
/* one type's instances after a change (load: a project load, every instance already given back) */
static void fxi_sync(uint32_t j, int load)
{
    uint32_t ty = FXI_TY(j), k, i, want = 0;
    for (k = 0; k < FXI_NK; k++)                        /* (the tracks, then the master, the USB input, the returns) */
        if (fxi_holds(k, ty))
            want |= 1u << k;
    for (i = 0; i < FXI_TN[j]; i++) {                   /* instances whose owner no longer wants them: back */
        k = fxi_own[j][i];
        if (k == FXI_NONE || (want >> k & 1u))
            continue;
        if (load || !ifx_lim_w(ty, fxi_aix(k)))
            fxi_give(j, i);
        else
            fxi_pend = 1;                               /* (fading out: the next pass) */
    }
    fxi_full[j] = 0;
    for (k = 0; k < FXI_NK; k++) {                      /* owners wanting one and without: a free one, in owner order */
        if (!(want >> k & 1u) || fxi_at[k][j] != FXI_NONE)
            continue;
        i = fxi_find(j);
        if (i == FXI_NONE) {
            fxi_full[j] |= (uint16_t)(1u << k);         /* (no room left: bypassed) */
            continue;
        }
        fxi_clear(j, i);                                /* (not in use: the audio does not see it) */
        fxi_own[j][i] = (uint8_t)k;
        fxi_publish(k, j, i);
    }
}

/* ---- Lending (above): the send line s (0 CHO, 1 DLY) */
/* the bus idle and not wanted: the gate set, in one section with the interrupts off (no block in between); 1 lent */
static uint32_t fxl_take(uint32_t s)
{
    uint32_t ok;
    fm1_irq_off();
    ok = !fxl_send_on(s) && (s ? fx.dly_q >= DLY_LEN && !DLY_FST() && !fxz.dclr : fx.cho_q >= CHO_LEN && !fxz.cclr);
#if FXM_NEED
    ok = ok && !(fxm_off >> s & 1u);                    /* (its line in the arena: fx_arena.c places it for a starved type) */
#endif
    if (ok)
        fxl_lent |= (uint8_t)(1u << s);
    fm1_irq_on();
    return ok;
}
/* the units it lent zeroed (the bus's idle state: its line all zeros), the gate cleared: the bus's again */
static void fxl_return(uint32_t s)
{
    if (fxa_hi[s]) {
        int16_t *ln = fxa_line(FXL_LO(s));
        memset(ln, 0, fxa_hi[s] * FXA_U * sizeof *ln);   /* (the units handed out since it was lent) */
        fxa_hi[s] = 0;
    }
    fm1_irq_off();
    fxl_lent &= (uint8_t)~(1u << s);
    fm1_irq_on();
    fxl_st[s] = FXL_SEND;
}
static uint32_t fxi_nused(uint32_t j)                   /* type j's instances handed out */
{
    uint32_t i, n = 0;
    for (i = 0; i < FXI_TN[j]; i++)
        n += fxi_own[j][i] != FXI_NONE;
    return n;
}
/* send line s would serve an owner left without: a type it holds starved, one of its instances free */
static uint32_t fxl_starved(uint32_t s)
{
    uint32_t j, want = 0;
    for (j = 0; j < FXI_NL; j++)
        want |= fxl_fits(s, j) && fxi_full[j] && fxi_nused(j) < FXI_TN[j];
    return want;
}
/* the line's owner after the pass's sync; 1: just lent (a sync again places lines on it) */
static uint32_t fxl_step(uint32_t s)
{
    uint32_t i, j, held = 0, want, on = fxl_send_on(s);
    if (!fxl_lends(s))
        return 0;
    if (fxl_st[s] == FXL_SEND) {
        want = fxl_starved(s);
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
        for (i = 0; i < FXI_TN[j]; i++) {
            uint32_t k = fxi_own[j][i];
            if (k == FXI_NONE || fxa_reg(fxa_u[j][i]) != s)
                continue;
            fxi_evict(j, i, fxl_st[s] == FXL_BACK);
            if (fxl_st[s] == FXL_BACK && !ifx_lim_w(FXI_TY(j), fxi_aix(k)))
                fxi_give(j, i);                         /* (faded out: its units back) */
            else
                held = 1;
        }
    if (held) {
        if (fxl_st[s] == FXL_BACK)
            fxi_pend = 1;                               /* (fading: the next pass) */
        return 0;
    }
    fxl_return(s);                                      /* (none in use: the line back to its bus) */
    fxi_pend = 1;                                       /* (a pass more: an owner it served asks again, FULL or own) */
    return 0;
}

/* the main loop, every pass (main.c): the instances after a change of slots or a load; nothing to do costs a compare */
static void fxi_poll(void)
{
    uint32_t d, j, s, i;
    if (!fxi_dirty && !fxi_pend)
        return;
    fm1_irq_off();
    d = fxi_dirty;
    fxi_dirty = 0;
    fm1_irq_on();
    fxi_pend = 0;
    if (d & 2u)                                         /* (a load: every instance back, the memory empty, then in order) */
        for (j = 0; j < FXI_NL; j++)
            for (i = 0; i < FXI_TN[j]; i++)
                if (fxi_own[j][i] != FXI_NONE)
                    fxi_give(j, i);
    for (j = 0; j < FXI_NL; j++)
        if (FXI_TN[j])
            fxi_sync(j, (d & 2u) != 0u);
    for (s = 0; s < FXL_NS; s++)
        if (fxl_step(s))
            for (j = 0; j < FXI_NL; j++)
                if (FXI_TN[j] && fxl_fits(s, j))
                    fxi_sync(j, 0);                     /* (just lent: lines on it for the owners waiting, in type order) */
    for (j = 0; j < FXI_NL; j++)
        if (FXI_TN[j] && fxi_full[j])
            fxi_pend = 1;                               /* (a starved owner takes the room given back) */
    for (s = 0; s < FXL_NS; s++)
        if (fxl_st[s] != FXL_SEND)
            fxi_pend = 1;                               /* (a lent line is watched: its send back in a slot, none using it) */
#if FXA_ROOM
    if (fxr_again)
        fxr_trim();                                     /* (a line given back while the room moved) */
    for (j = 0; j < FXI_NL && !fxi_full[j]; j++)
        ;
    if (j == FXI_NL)
        fxm_ins_ok();                                   /* (none left without a line: a refusal said again later) */
#endif
}
/* boot, the host tests: every instance free, every line the send's, the memory empty */
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
    memset(fxa_bm, 0, sizeof fxa_bm);
#if FXA_ROOM
    if (fxr_hi > fxr_lo)
        arn_give(FXR_K);                                /* (the own room empty: back to the arena) */
    fxr_lo = fxr_hi = 0, fxr_pk = 0;
#endif
    fxi_pend = 0;
    fxi_dirty = 2;
}
#define FXI_LIM(t) FXT_IS_LIM(t)
/* a starved owner of type j gets one once a send's tail has ended (WAIT, not FULL): a send line not lent yet, not in a
 * master slot, that holds its line, and an instance free */
static uint32_t fxl_wait(uint32_t j)
{
    uint32_t s, w = 0;
    for (s = 0; s < FXL_NS; s++)
        w |= fxl_fits(s, j) && fxl_st[s] == FXL_SEND && !fxl_send_on(s);
    return w && fxi_nused(j) < FXI_TN[j];
}
/* the lines of type j the memory can place now: the own room, and each send line not in a master slot (lent, or to be
 * lent once its tail ends) */
static uint32_t fxa_room(uint32_t j)
{
    uint32_t n = FXA_SZ(j), r, s;
#if FXA_ROOM
    {   /* (the room's free units and the arena's free ones under the places: what it would hold grown, its parts
         * moved) */
        uint32_t m[FXR_BMW ? FXR_BMW : 1u], lim = fxr_map(m), u, run = 0;
        for (u = 0, r = 0; u < lim; u++)
            if ((run = fxr_free(m, u) ? run + 1u : 0u) == n)
                r++, run = 0;
    }
#else
    r = fxa_fits(FXA_S, FXA_N, n);
#endif
    for (s = 0; s < FXL_NS; s++)
        if (fxl_fits(s, j) && !fxl_send_on(s) && fxl_st[s] != FXL_BACK)
            r += fxa_fits(FXL_LO(s), FXL_HI(s), n);
    return r;
}
static uint32_t fxi_used(uint32_t t)
{
    if (!FXI_LIM(t))
        return t < FXT_N && (FXT_BUILT >> t & 1u) ? FXI_ANY : 0u;
    return fxi_nused(FXI_J(t));
}
static uint32_t fxi_free(uint32_t t)                    /* (no more than the instances left and the memory's room) */
{
    uint32_t j = FXI_J(t), u;
    if (!FXI_LIM(t))
        return t < FXT_N && (FXT_BUILT >> t & 1u) ? FXI_ANY : 0u;
    if (!FXI_TN[j])
        return 0u;
    u = fxi_nused(j);
    return FXL_MIN(FXI_TN[j] - u, fxa_room(j));
}
static uint32_t fxi_cap(uint32_t t)
{
    if (!FXI_LIM(t))
        return fxi_used(t);
    return FXI_TN[FXI_J(t)] ? fxi_nused(FXI_J(t)) + fxi_free(t) : 0u;
}
static uint32_t fxi_lent(uint32_t t)                    /* of those used, on a send's line */
{
    uint32_t j = FXI_J(t), i, n = 0;
    for (i = 0; FXI_LIM(t) && i < FXI_TN[j]; i++)
        n += fxi_own[j][i] != FXI_NONE && fxi_onl(j, i);
    return n;
}
/* owner o's (FXI_MST, FXI_UT, FXI_RT0 + j, a track) type t: FXI_NA .. FXI_FULL (as fxi_state) */
static uint32_t fxi_ostate(uint32_t o, uint32_t t)
{
    uint32_t j = FXI_J(t);
    o %= FXI_NK;
    if (!FXI_LIM(t) || !FXI_TN[j])
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
    return FXI_TN[FXI_J(t)] && (fxi_at[k % FXI_NK][FXI_J(t)] != FXI_NONE || fxi_free(t) > 0u);
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
