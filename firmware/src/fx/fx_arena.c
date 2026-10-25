/* SPDX-License-Identifier: GPL-3.0-only */
/* The effects' lines in the arena (docs/SD-MODE-DESIGN.md 6b, phase 3b; FELUCCA_FX_ARENA with an arena placed by need,
 * FELUCCA_ARENA_BYTES: engines.c). The chorus, delay and reverb sends' lines (fx.c cho_buf dly_buf, rev_line; the ROOM's
 * diffusers rev_ap after the reverb's line, in its block) and the
 * limited inserts' own room (fxi_mem.h fxa_ins) are blocks of the parts' arena (core/arena.c, the clients after the
 * parts: FXM_K), held only while the project uses them:
 *   - a send's line while its type is in a master slot (FXS_HAS), or while it is lent to the inserts (fxi.c "Lending";
 *     placed too for an owner starved of a type the line would hold: fxl_take then lends it);
 *   - the inserts' own room while an owner holds a limited type; given back (fxi_poll's end, fxm_ins_post) once no
 *     type is held and the last instance in it has faded out.
 * The main loop only (fxm_poll every pass, before fxi_poll); the audio reads the pointers, nothing is placed or freed
 * in it. A send's line is placed (arn_reserve + arn_take with the interrupts off: a project's apply may reserve a
 * part's memory inside the audio ISR), zeroed and its bus' state reset while the bus is gated (fx.c fxm_off:
 * fx_ret_block runs no bus whose line is not placed, rev_guard keeps the reverb off), then the gate cleared. No longer
 * used: gated, and given back two blocks later (fxm_tick: CPU1's kept reverb block, REV_LATE, has run by then); used
 * again meanwhile: cleared again before the gate is cleared (no stale tail).
 * No room (the user, 2026-10-25: refuse, then propose what to drop; a dropped part is a silent placeholder): a refused
 * effect is bypassed where it is (a send's return adds nothing, the dry signal passes; a limited insert FULL as when
 * the own room is full, fxi.c), kept in the project as it is, shown dim (FULL: op_cells.c) with a MEM FULL message
 * once (op_input.c), and placed by itself as soon as there is room: tried again every pass, except while a part's
 * refused change waits (part_mem_refused: a part's sound first). What to drop is proposed among the parts only
 * (engines.c ARN_DROPPABLE); a part's change refused while an effect's line is on its way back waits for it (model.c
 * mem_retry, fxm_pending). Included by fx.c after fxi.c */
#if FXM_NEED
enum { FXM_CHO, FXM_DLY, FXM_REV, FXM_INS };
enum { FXM_NONE, FXM_ON, FXM_GOING };                   /* a send line's: not placed, placed, gated to be given back */
#define FXM_K(c) (NPART + (uint32_t)(c))                /* (its arena client) */
#if INS_LIM && FXA_I
#define FXM_INS_BYTES ((uint32_t)FXA_I * FXA_U * 2u)
#else
#define FXM_INS_BYTES 0u
#endif
#if FELUCCA_REV_ROOM
#define FXM_REV_AP (REV_AP_N * 2u)                      /* (the ROOM's diffusers, rev_ap: after the line, in its block) */
#else
#define FXM_REV_AP 0u
#endif
static const uint32_t FXM_BYTES[FXM_NC] = {FELUCCA_FX_CHORUS ? CHO_LEN * 2u : 0u, FELUCCA_FX_DELAY ? DLY_LEN * 2u : 0u,
                                           FELUCCA_FX_REVERB ? REV_LINE_N * 2u + FXM_REV_AP : 0u, FXM_INS_BYTES};
static const uint8_t FXM_TY[3] = {FXT_CHO, FXT_DLY, FXT_REV};
static uint8_t fxm_st[3];                               /* FXM_NONE .. FXM_GOING, by send line */
static uint8_t fxm_t0[3];                               /* fxm_tick when it was gated */
static uint8_t fxm_full;                                /* wanted, no room (a bit by FXM_*): bypassed, the UI dims it */
static uint8_t fxm_news;                                /* the refusals the UI has not said yet (op_input.c mem_tick) */
static uint8_t fxm_mv;                                  /* the send lines gated to be moved (fxm_poll: room cut up) */

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
#if INS_LIM && FXA_I
    else
        fxa_ins = (int16_t *)p;
#endif
}
/* client c's block placed (no room: 0); the interrupts off around the arena (a part's reservation may run in the ISR) */
static int fxm_place(uint32_t c)
{
    void *p = 0;
    int r;
    fm1_irq_off();
    r = arn_reserve(FXM_K(c), FXM_BYTES[c]);
    if (!r)
        p = arn_take(FXM_K(c));
    fm1_irq_on();
    if (r)
        return 0;
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
/* send line c's bus silent (every cell 0, no clear under way) and not lent: moving it loses nothing (fxm_poll) */
static uint32_t fxm_still(uint32_t c)
{
#if INS_LIM
    if (c < FXM_REV && fxl_st[c] != FXL_SEND)
        return 0;
#endif
    if (c == FXM_CHO)
        return fx.cho_q >= CHO_LEN && !fxz.cclr;
    if (c == FXM_DLY)
        return fx.dly_q >= DLY_LEN && !DLY_FST() && !fxz.dclr;
#if FELUCCA_FX_REVERB && !FELUCCA_SPRING                /* (SPRING keeps its own state: never moved) */
    return fx.rev_q >= REV_Q && !REV_LP_BUSY() && !REV_HALF_BUSY() && (rgd.st == RG_ON || rgd.st == RG_OFF)
#if REV_MULTI
           && !rsel.fade
#endif
           ;
#else
    return 0;
#endif
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
            fxm_give(c);                                /* (moved: placed again below, with the others) */
            fxm_st[c] = FXM_NONE;
        }
        fxm_mv &= (uint8_t)~(1u << c);
        return;
    }
    if (!w)
        fxm_full &= (uint8_t)~(1u << c);                /* (refused, then let go: nothing to place) */
}
#if INS_LIM && FXA_I
static uint32_t fxm_ins_want(void)                      /* an owner holds a limited type */
{
    uint32_t j, k;
    for (j = 0; j < FXI_NL; j++)
        for (k = 0; FXI_TN[j] && k < FXI_NK; k++)
            if (fxi_holds(k, FXI_TY(j)))
                return 1;
    return 0;
}
/* fxi_poll's end: no type held and no instance left in the own room (each faded out and given back): the room back,
 * its units marked taken (fxa_place does not see them) */
static void fxm_ins_post(void)
{
    uint32_t j, i;
    if (!fxa_ins || fxm_ins_want())
        return;
    for (j = 0; j < FXI_NL; j++)
        for (i = 0; i < FXI_TN[j]; i++)
            if (fxi_own[j][i] != FXI_NONE && fxa_reg(fxa_u[j][i]) == FXL_NS)
                return;                                 /* (still fading: fxi_pend brings a pass) */
    fxm_give(FXM_INS);
    fxa_mark(FXA_S, FXA_I, 1);
}
static void fxm_ins_init(void)                          /* (fxi_init, the units cleared) */
{
    if (!fxa_ins)
        fxa_mark(FXA_S, FXA_I, 1);
}
/* the UI's count (fxi.c fxa_room): the own room not placed counts as empty while the arena would take it */
static int fxm_ins_could(void)
{
    return fxa_ins || arn_fits(FXM_K(FXM_INS), FXM_INS_BYTES);
}
#define FXM_INS_NEEDS() (!fxa_ins && fxm_ins_want())
#else
#define fxm_ins_want() 0u
#define FXM_INS_NEEDS() 0u
#endif
/* client c wants a block it has not: a send line not placed and used, the own room not placed and a type held */
static uint32_t fxm_needs(uint32_t c)
{
    if (!FXM_BYTES[c])
        return 0;
    if (c == FXM_INS)
        return FXM_INS_NEEDS();
    return fxm_st[c] == FXM_NONE && fxm_want(c);
}
static void fxm_placed(uint32_t c)                      /* client c's block just placed: in use */
{
#if INS_LIM && FXA_I
    if (c == FXM_INS) {
        fxa_mark(FXA_S, FXA_I, 0);                      /* (its units free) */
        fxi_pend = 1;                                   /* (a pass: the owners waiting take their lines in it) */
        return;
    }
#endif
    fxm_open(c);
    fxm_st[c] = FXM_ON;
}
static void fxm_ord(uint8_t *o)                         /* the clients, the largest block first (it cuts up the room least) */
{
    uint32_t i, j;
    for (i = 0; i < FXM_NC; i++)
        o[i] = (uint8_t)i;
    for (i = 1; i < FXM_NC; i++)
        for (j = i; j > 0 && FXM_BYTES[o[j - 1u]] < FXM_BYTES[o[j]]; j--) {
            uint8_t t = o[j];
            o[j] = o[j - 1u], o[j - 1u] = t;
        }
}
/* 1: with the blocks of mv given back, every client of need placed, the largest first (tried on the arena itself, the
 * interrupts off, then put back as it was: nothing changed) */
static int fxm_plan(uint32_t mv, uint32_t need)
{
    arn_blk_t cur[ARN_PARTS], nxt[ARN_PARTS];
    uint8_t hn[ARN_PARTS], o[FXM_NC];
    uint32_t i, c;
    int ok = 1;
    fxm_ord(o);
    fm1_irq_off();
    memcpy(cur, arn_cur, sizeof cur), memcpy(nxt, arn_next, sizeof nxt), memcpy(hn, arn_has_next, sizeof hn);
    for (c = 0; c < FXM_NC; c++)
        if (mv >> c & 1u)
            arn_cur[FXM_K(c)].len = 0;
    for (i = 0; i < FXM_NC && ok; i++)
        if (need >> o[i] & 1u)
            ok = !arn_reserve(FXM_K(o[i]), FXM_BYTES[o[i]]) && arn_take(FXM_K(o[i])) != 0;
    memcpy(arn_cur, cur, sizeof cur), memcpy(arn_next, nxt, sizeof nxt), memcpy(arn_has_next, hn, sizeof hn);
    fm1_irq_on();
    return ok;
}

/* the main loop, every pass (main.c, before fxi_poll): the lines after a change. The send lines no longer used gated
 * (then given back), those wanted placed, the largest first; one that finds no room while the free room is only cut
 * up: the send lines whose buses are silent (fxm_still: nothing heard is lost) gated, given back and placed again with
 * it, the largest first, when that makes it fit (fxm_plan); else refused (bypassed, tried again every pass) */
static uint32_t fxm_seen = 0xFFFFFFFFu;                 /* fxs_live at the last pass (a change of the master slots) */
static void fxm_poll(void)
{
    uint8_t o[FXM_NC];
    uint32_t i, c, need = 0, mv = 0;
    if (fxs_live == fxm_seen && !fxm_full && !fxm_mv && fxm_st[0] != FXM_GOING && fxm_st[1] != FXM_GOING &&
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
    if (!fxm_ins_want())
        fxm_full &= (uint8_t)~(1u << FXM_INS);          /* (refused, then let go) */
    if (PART_MEM_SHORT() || fxm_mv)
        return;                                         /* (a part's refused change first, a load's too; a move under way) */
    fxm_ord(o);
    for (i = 0; i < FXM_NC; i++)
        if (fxm_needs(o[i])) {
            if (fxm_place(o[i]))
                fxm_placed(o[i]);
            else
                need |= 1u << o[i];
        }
    if (!need)
        return;
    for (c = 0; c < 3u; c++)
        if (fxm_st[c] == FXM_ON && fxm_want(c) && fxm_still(c))
            mv |= 1u << c;
    if (mv && fxm_plan(mv, need | mv)) {
        for (c = 0; c < 3u; c++)
            if (mv >> c & 1u) {
                fxm_mv |= (uint8_t)(1u << c);
                fxm_gate(c);
            }
        return;
    }
    for (c = 0; c < FXM_NC; c++)
        if (need >> c & 1u)
            fxm_refuse(c);
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
#if INS_LIM && FXA_I
    if (fxa_ins && !fxm_ins_want())
        return 1;
#endif
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
 * placed, its bus' own (not lent) and not in asked (a bit by FXM_*), the largest first; 0 none. The own room is not offered (its instances'
 * owners hold their types: they would only take it again) */
static int fxm_offer(uint32_t asked, uint32_t *out)
{
    uint8_t o[FXM_NC];
    uint32_t i;
    fxm_ord(o);
    for (i = 0; i < FXM_NC; i++)
        if (o[i] < 3u && fxm_st[o[i]] == FXM_ON && !(asked >> o[i] & 1u) && fxm_heard(o[i])) {
            *out = o[i];
            return 1;
        }
    return 0;
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
#else
#define fxm_poll() ((void)0)
#define fxm_pending() 0
#endif
