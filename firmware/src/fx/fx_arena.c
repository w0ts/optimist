/* SPDX-License-Identifier: GPL-3.0-only */
/* The effects' lines in the arena (docs/SD-MODE-DESIGN.md 6b, phase 3b; FELUCCA_FX_ARENA: every build with an arena,
 * FELUCCA_ARENA_BYTES: engines.c). The chorus, delay and reverb sends' lines (fx.c cho_buf dly_buf, rev_line; the
 * ROOM's diffusers rev_ap after the reverb's line, in its block) are blocks of the parts' arena (core/arena.c, the
 * clients after the parts: FXM_K), each at its fixed place at the arena's top (FXM_AT, the user, 2026-10-29: REV the
 * highest, then DLY, then CHO; a line never moves). A send line's place is free for a part's block while the line is
 * not placed (the engines are placed from the bottom: only a full arena reaches it); the line wanted again, that block
 * is moved away (core/arena.c arn_pack). The ARN_TOPS clients (the other buffers, the inserts' room) stay under the
 * places (fxm_lim). A line is held only while the project uses it:
 *   - a send's line while its type is in a master slot (FXS_HAS), or while it is lent to the inserts (fxi.c "Lending";
 *     placed too for an owner starved of a type the line would hold: fxl_take then lends it).
 * The limited inserts' own room (one block, FXM_INS) is fx/insfx/fxi.c's: it grows and shrinks at its edges.
 * The main loop only (fxm_poll every pass, before fxi_poll); the audio reads the pointers, nothing is placed or freed
 * in it. A send's line is placed (arn_reserve_at + arn_take with the interrupts off: a project's apply may reserve a
 * part's memory inside the audio ISR), zeroed and its bus' state reset while the bus is gated (fx.c fxm_off:
 * fx_ret_block runs no bus whose line is not placed, rev_guard keeps the reverb off), then the gate cleared. No longer
 * used: gated, and given back two blocks later (fxm_tick: CPU1's kept reverb block, REV_LATE, has run by then); used
 * again meanwhile: cleared again before the gate is cleared (no stale tail).
 * No room (the user, 2026-10-25: refuse, then propose what to drop; a dropped part is a silent placeholder): a part's
 * block in the way is moved when the free room holds it (arn_pack, once until the room changes); else a refused
 * effect is bypassed where it is (a send's return adds nothing, the dry signal passes; a limited insert FULL, fxi.c),
 * kept in the project as it is, shown dim (FULL: op_cells.c) with a MEM FULL message once (op_input.c), and placed by
 * itself as soon as there is room: tried again every pass, except while a part's refused change waits
 * (part_mem_refused: a part's sound first). What to drop is proposed among the parts first (model.c mem_offer), then
 * the send lines (fxm_offer); a part's change refused while an effect's line is on its way back waits for it (model.c
 * mem_retry, fxm_pending). Included by fx.c after fxi.c */
#if FXM_NEED
enum { FXM_CHO, FXM_DLY, FXM_REV, FXM_INS };
enum { FXM_NONE, FXM_ON, FXM_GOING };                   /* a send line's: not placed, placed, gated to be given back */
#define FXM_K(c) (NPART + (uint32_t)(c))                /* (its arena client) */
#if FELUCCA_REV_ROOM
#define FXM_REV_AP (REV_AP_N * 2u)                      /* (the ROOM's diffusers, rev_ap: after the line, in its block) */
#else
#define FXM_REV_AP 0u
#endif
#define FXM_R(n) (((uint32_t)(n) + ARN_ALIGN - 1u) & ~(ARN_ALIGN - 1u))
#define FXM_B0 (FELUCCA_FX_CHORUS ? CHO_LEN * 2u : 0u)
#define FXM_B1 (FELUCCA_FX_DELAY ? DLY_LEN * 2u : 0u)
#define FXM_B2 (FELUCCA_FX_REVERB ? REV_LINE_N * 2u + FXM_REV_AP : 0u)
static const uint32_t FXM_BYTES[3] = {FXM_B0, FXM_B1, FXM_B2};
/* the fixed places: REV at the arena's end, DLY under it, CHO under that; fxm_lim the lowest (ARN_LIM, engines.c) */
#define FXM_AT2 (ARN_BYTES - FXM_R(FXM_B2))
#define FXM_AT1 (FXM_AT2 - FXM_R(FXM_B1))
#define FXM_AT0 (FXM_AT1 - FXM_R(FXM_B0))
_Static_assert(FXM_R(FXM_B0) + FXM_R(FXM_B1) + FXM_R(FXM_B2) <= ARN_BYTES,
               "FELUCCA_ARENA_KB: the arena holds the three send lines together (tools/builder/core.py refuses less)");
static const uint32_t FXM_AT[3] = {FXM_AT0, FXM_AT1, FXM_AT2};
static uint32_t fxm_lim(void) { return FXM_AT0; }
static const uint8_t FXM_TY[3] = {FXT_CHO, FXT_DLY, FXT_REV};
static uint8_t fxm_st[3];                               /* FXM_NONE .. FXM_GOING, by send line */
static uint8_t fxm_t0[3];                               /* fxm_tick when it was gated */
static uint8_t fxm_full;                                /* wanted, no room (a bit by FXM_*): bypassed, the UI dims it */
static uint8_t fxm_news;                                /* the refusals the UI has not said yet (op_input.c mem_tick) */
static uint8_t fxm_mv;                                  /* the send lines dropped: given back at the gate's end */
static uint32_t fxm_pk[3];                              /* arn_room() + 1 when a pack was last planned for it (0 never) */

/* the audio's end of a block (core/arena.c ARN_MV_SYNC, engines.c): a move starts just after one, so it is usually
 * done before the next; the device only (the host's audio runs between the main loop's passes) */
static void fxm_sync(void)
{
#if defined(FM1_TICKS_PER_US) && !defined(PLAT_HOST)    /* (fm1-base's host: its clock stands still in the app) */
    uint8_t t = fxm_tick;
    uint32_t t0 = fm1_ticks();
    while (fxm_tick == t && (uint32_t)(fm1_ticks() - t0) < 1500u * FM1_TICKS_PER_US)
        ;                                               /* (two block periods at most: the audio may be stopped) */
#endif
}

static void fxm_set(uint32_t c, void *p)                /* the line's pointer (the audio's; gated, it is not read) */
{
    if (c == FXM_CHO)
        cho_buf = (int16_t *)p;
    else if (c == FXM_DLY)
        dly_buf = (int16_t *)p;
    else if (c == FXM_REV) {
        rev_line = (int16_t *)p;
#if FELUCCA_REV_ROOM
        rev_ap = p ? (int16_t *)p + REV_LINE_N : 0;     /* (the diffusers after the line: one block) */
#endif
    }
}
/* send line c's block placed at its place (no room: 0; a part's block there and room for it below: moved first, 0
 * meanwhile, *wait set); the interrupts off around the arena (a part's reservation may run in the ISR) */
static int fxm_place(uint32_t c, int *wait)
{
    void *p = 0;
    int r;
    uint32_t room;
    fm1_irq_off();
    r = arn_reserve_at(FXM_K(c), FXM_AT[c], FXM_BYTES[c]);
    if (!r)
        p = arn_take(FXM_K(c));
    fm1_irq_on();
    if (r) {
        room = arn_room() + 1u;
        if (arn_busy() || (fxm_pk[c] != room && (fxm_pk[c] = room, arn_pack(FXM_K(c), FXM_BYTES[c], FXM_AT[c]))))
            *wait = 1;                                  /* (a part's block moved away first: again after) */
        return 0;
    }
    fxm_pk[c] = 0;
    fxm_full &= (uint8_t)~(1u << c);
    fxm_set(c, p);
    return 1;
}
static void fxm_refuse(uint32_t c)                      /* client c wanted, no room: bypassed, said once */
{
    fxm_news |= (uint8_t)(fxm_full >> c & 1u ? 0u : 1u << c);
    fxm_full |= (uint8_t)(1u << c);
}
static void fxm_give(uint32_t c)
{
    fm1_irq_off();
    arn_give(FXM_K(c));
    fm1_irq_on();
    fxm_set(c, 0);
}
#if INS_LIM
/* send line s (0 the chorus', 1 the delay's) is wanted by the inserts: lent, on its way back, or an owner starved of a
 * type it would hold while its send is in no master slot */
static uint32_t fxm_lent(uint32_t s)
{
    return fxl_lends(s) && (fxl_st[s] != FXL_SEND || (!fxl_send_on(s) && fxl_starved(s)));
}
#else
#define fxm_lent(s) ((void)(s), 0u)
#endif
static uint32_t fxm_want(uint32_t c)                    /* send line c is used now */
{
    if (!FXM_BYTES[c])
        return 0;
    return FXS_HAS(FXM_TY[c]) || (c < FXM_REV && fxm_lent(c));
}
/* send line c zeroed and its bus at rest (gated: the audio does not run it), then the gate cleared */
static void fxm_open(uint32_t c)
{
    if (c == FXM_REV) {
#if FELUCCA_FX_REVERB
        rev_tank_clear();                               /* (the line and every tank's cells, as a TYPE change) */
#if REV_TANK_HALF
        rev_half_clear();
#endif
#if FELUCCA_SPRING
        spring_clear();
#endif
#if REV_ALT
        rv.size = -1;                                   /* (the tank's gains again, its next block) */
#endif
#if REV_MULTI
        rsel.fade = 0;                                  /* (a fade cut by the gate: the tank TYPE wants starts clean) */
#endif
#endif
    } else {
        memset(c == FXM_CHO ? cho_buf : dly_buf, 0, FXM_BYTES[c]);
    }
    fm1_irq_off();
    if (c == FXM_CHO) {
        fx.cho_q = FX_Q_MAX;                            /* (idle: every cell 0) */
        fxz.cclr = 0;
    } else if (c == FXM_DLY) {
        memset(fx.dly_f, 0, sizeof fx.dly_f);
        fx.dly_ec = 0;
        fx.dly_q = FX_Q_MAX;
        fxz.dclr = 0;
    } else {
        fx.rev_q = FX_Q_MAX;
    }
    fxm_off &= (uint8_t)~(1u << c);
    fm1_irq_on();
}
static void fxm_gate(uint32_t c)                        /* send line c gated now, given back two blocks later */
{
    fm1_irq_off();
    fxm_off |= (uint8_t)(1u << c);
    fxm_t0[c] = fxm_tick;
    fm1_irq_on();
    fxm_st[c] = FXM_GOING;
}
static void fxm_send(uint32_t c)                        /* send line c after the pass's changes, but a placement */
{
    uint32_t w = fxm_want(c);
    if (fxm_st[c] == FXM_ON) {
        if (!w)
            fxm_gate(c);                                /* (no longer used) */
        return;
    }
    if (fxm_st[c] == FXM_GOING) {
        if ((uint8_t)(fxm_tick - fxm_t0[c]) < 2u)
            return;                                     /* (the audio's last block of it may still run) */
        if (w && !(fxm_mv >> c & 1u)) {
            fxm_open(c);                                /* (used again: the same block, cleared) */
            fxm_st[c] = FXM_ON;
        } else {
            fxm_give(c);                                /* (dropped: placed again by itself once there is room) */
            fxm_st[c] = FXM_NONE;
        }
        fxm_mv &= (uint8_t)~(1u << c);
        return;
    }
    if (!w)
        fxm_full &= (uint8_t)~(1u << c), fxm_pk[c] = 0; /* (refused, then let go: nothing to place) */
}
/* the main loop, every pass (main.c, before fxi_poll): the arena's moves (core/arena.c arn_poll), then the lines after
 * a change. The send lines no longer used gated (then given back), those wanted placed, the largest first; one that
 * finds a part's block at its place waits while that block is moved away (arn_pack), else is refused (bypassed, tried
 * again every pass) */
static uint32_t fxm_seen = 0xFFFFFFFFu;                 /* fxs_live at the last pass (a change of the master slots) */
static void fxm_poll(void)
{
    static const uint8_t ORD[3] = {FXM_REV, FXM_DLY, FXM_CHO};   /* (the largest first, as fxm_offer) */
    uint32_t i, c;
    int wait;
    arn_poll();
    if (fxs_live == fxm_seen && !fxm_full && fxm_st[0] != FXM_GOING && fxm_st[1] != FXM_GOING &&
        fxm_st[2] != FXM_GOING
#if INS_LIM
        && !fxi_dirty && !fxi_pend
#endif
        )
        return;                                         /* (nothing changed: a few compares a pass) */
    fxm_seen = fxs_live;
    for (c = 0; c < 3u; c++)
        if (FXM_BYTES[c])
            fxm_send(c);
    if (PART_MEM_SHORT())
        return;                                         /* (a part's refused change first, a load's too) */
    for (i = 0; i < 3u; i++) {
        c = ORD[i];
        if (!FXM_BYTES[c] || fxm_st[c] != FXM_NONE || !fxm_want(c))
            continue;
        wait = 0;
        if (fxm_place(c, &wait)) {
            fxm_open(c);
            fxm_st[c] = FXM_ON;
        } else if (wait) {
            fxm_full |= (uint8_t)(1u << c);             /* (bypassed meanwhile, not said: a pass or two) */
        } else {
            fxm_refuse(c);
        }
    }
}
static int arb_pending(void);                           /* (core/arena_bufs.c: the other buffers) */
/* an effect's line (or another buffer, arena_bufs.c) still held though nothing uses it: given back within a pass or a
 * fade (model.c mem_retry waits) */
static int fxm_pending(void)
{
    uint32_t c;
    if (arb_pending())
        return 1;
    for (c = 0; c < 3u; c++)
        if (fxm_st[c] == FXM_GOING || (fxm_st[c] == FXM_ON && !fxm_want(c)))
            return 1;
    return 0;
}
/* send line c is its bus' own (not lent to the inserts: their instances on it cannot be dropped with it) */
static uint32_t fxm_heard(uint32_t c)
{
#if INS_LIM
    if (c < FXM_REV && fxl_st[c] != FXL_SEND)
        return 0;
#endif
    return 1;
}
/* a part's refused change, every part offered (model.c mem_offer): the next effect's line to propose, a send line
 * placed, its bus' own (not lent) and not in asked (a bit by FXM_*), the largest first; 0 none. The inserts' room is
 * not offered (its instances' owners hold their types: they would only take it again) */
static int fxm_offer(uint32_t asked, uint32_t *out)
{
    uint32_t c, best = 3u;
    for (c = 0; c < 3u; c++)                            /* (the largest first: it gives back the most) */
        if (FXM_BYTES[c] && fxm_st[c] == FXM_ON && !(asked >> c & 1u) && fxm_heard(c) &&
            (best == 3u || FXM_BYTES[c] > FXM_BYTES[best]))
            best = c;
    *out = best;
    return best < 3u;
}
/* send line c dropped (the confirm, model.c mem_drop): bypassed as when refused (FULL, kept in its slot), gated now,
 * given back two blocks later; placed again by itself once there is room (after the part's change: fxm_poll waits) */
static void fxm_drop(uint32_t c)
{
    if (c >= 3u || fxm_st[c] != FXM_ON || !fxm_heard(c))
        return;
    fxm_full |= (uint8_t)(1u << c);                     /* (no MEM FULL: the user dropped it) */
    fxm_mv |= (uint8_t)(1u << c);                       /* (at the gate's end: given back, not opened again) */
    fxm_gate(c);
}
/* send type t (CHO DLY REV) in a master slot, refused: bypassed (op_cells.c dims it) */
static int fxm_refused(uint32_t t)
{
    uint32_t c = t == FXT_CHO ? FXM_CHO : t == FXT_DLY ? FXM_DLY : t == FXT_REV ? FXM_REV : 3u;
    return c < 3u && (fxm_full >> c & 1u) && fxm_st[c] != FXM_ON;   /* (refused, or dropped and going back) */
}
#if INS_LIM
static void fxm_ins_refused(void) { fxm_refuse(FXM_INS); }   /* (fxi.c: an insert's line found no room: said once) */
static void fxm_ins_ok(void) { fxm_full &= (uint8_t)~(1u << FXM_INS); }   /* (.. none left without: said again later) */
#endif
#elif PART_DROPS
#define fxm_poll() arn_poll()                           /* (the parts alone in the arena: their moves) */
#define fxm_pending() 0
#else
#define fxm_poll() ((void)0)
#define fxm_pending() 0
#endif
