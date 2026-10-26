/* SPDX-License-Identifier: GPL-3.0-only */
/* The arena's other buffers (docs/SD-MODE-DESIGN.md 6b; FELUCCA_FX_ARENA with an arena placed by need,
 * FELUCCA_ARENA_BYTES: engines.c ARB_*). Beside the parts' engine states and the effects' lines (fx/fx_arena.c), the
 * big buffers only some projects use are blocks of the same arena, held only while the project uses their feature:
 *   - ARB_SL0 + k (FX_SLICER): track k's SLICER recording (slicer.c sl_bp, SL_LEN x 2 B), while its SLICER is on STUT
 *     or a repeat of it still plays (GATE and OFF record nothing);
 *   - ARB_WAVE (ENG_WAVE): WAVE's table, its band-limited copies and the frame builder's scratch (eng_wave.c wt_mip
 *     wt_fr wt_fi), while a track plays WAVE;
 *   - ARB_X909 / ARB_X808 (DRUM_X0X909 / DRUM_X0X808): the X0X machine's state (drums/x0x x0x_mem), while the kit or a
 *     lane plays the machine, or one of its voices still sounds;
 *   - ARB_PUNCH (FELUCCA_PUNCH_SW): the punch-in FX's ring of the mix (fx/punch/punch.c), while the project's PUNCH switch
 *     is on (GLOBAL > Rec; a punch needs the past, so the ring records from the switch on, not from the key), or an
 *     effect still fades out. FX + a key while it is not placed: let go, PUNCH OFF said (the switch off; refused: the
 *     MEM FULL PUNCH of its refusal).
 * Only those built have a client. The main loop only (arb_poll every pass, after fxm_poll); nothing is placed or freed
 * in the audio, which reads the pointers. Placed: reserved and taken with the interrupts off (a part's reservation may
 * run in the audio ISR), zeroed, the feature's state reset, then its pointer set with the interrupts off (the audio
 * never sees a half-made block). No longer used: its pointer 0 at once (the interrupts off), the block given back two
 * audio blocks later (fx.c fxm_tick, as the effects' lines); used again meanwhile: the same block, zeroed again.
 * No room (the rulings for the effects carried over, the user 2026-10-25): the feature is bypassed where it is and kept
 * in the project (STUT plays the live sound, as OFF; WAVE's voices are silent, as while a table is built; the X0X kit's
 * hits play its stand-in kit, as the notes the machine lacks), MEM FULL said once (op_input.c mem_tick, arb_said), and
 * it is placed by itself as soon as there is room: tried every pass, except while a part's refused change waits (a
 * part's sound first) or the effects' lines are being moved (fx_arena.c fxm_mv). The largest first. A part's change
 * refused while one of these is on its way back waits for it (fxm_pending). They are never proposed to drop.
 * WAVE's table is first a part's: a change to WAVE sets its room aside (engines.c part_mem_aside) or is refused like any
 * engine change (the drop proposal; on a load the list page), so its refusal here is only the fallback. A refused
 * SLICER's card reads FULL, dim, struck through (op_cells.c, arb_sl_refused), as a refused master FX slot
 * Included by fx.c after fx_arena.c */
#if FXM_NEED
#define ARB_K(c) (NPART + FXM_NC + (uint32_t)(c))       /* (its arena client) */
#if FELUCCA_PUNCH_SW                                    /* (fx/punch/punch.c, included after) */
static uint32_t punch_used(void);
static void punch_mem(void *p);
static int punch_asked(void);
static uint8_t arb_poff;                                /* FX + a key with the switch off: PUNCH OFF to say */
#endif
#if ARB_ANY
static uint8_t arb_st[ARB_NC];                          /* FXM_NONE, FXM_ON, FXM_GOING (fx_arena.c), by client */
static uint8_t arb_t0[ARB_NC];                          /* fxm_tick when its pointer went to 0 */
static uint32_t arb_full;                               /* wanted, no room (a bit by client): bypassed */
static uint32_t arb_news;                               /* the refusals the UI has not said yet (arb_said) */

static uint32_t arb_bytes(uint32_t c)                   /* client c's block */
{
#if FELUCCA_FX_SLICER
    if (c < (uint32_t)ARB_SL0 + NTRK)
        return SL_LEN * 2u;
#endif
#if FELUCCA_ENG_WAVE
    if (c == (uint32_t)ARB_WAVE)
        return WT_MEM_BYTES;
#endif
#if FELUCCA_DRUM_X909
    if (c == (uint32_t)ARB_X909)
        return x0x_mem_bytes(0);
#endif
#if FELUCCA_DRUM_X808
    if (c == (uint32_t)ARB_X808)
        return x0x_mem_bytes(1);
#endif
#if FELUCCA_PUNCH_SW
    if (c == (uint32_t)ARB_PUNCH)
        return (uint32_t)FELUCCA_PUNCH_N * 2u;
#endif
    (void)c;
    return 0;
}
#if DRUM_X0X
/* machine m (DRUM_UID_X909 / X808) is used: the kit or a lane plays it, or one of its voices still sounds (its tail:
 * given back only once it is silent) */
static uint32_t arb_x0x_want(uint32_t m)
{
    uint32_t k = drum_kit(), l, j, live = m == DRUM_UID_X909 ? (1u << X0X_CH808) - 1u : ~((1u << X0X_CH808) - 1u);
    if (xc.live & live)
        return 1;
    if (DRUM_IS_X0X(k) && DRUM_UID_XMACH(k) == m)
        return 1;
#if DL_ANY
    for (l = 0; l < DRUM_LANES; l++) {
        j = dl_kit_of(l, k);
        if (DRUM_IS_X0X(j) && DRUM_UID_XMACH(j) == m)
            return 1;
    }
#endif
    (void)l, (void)j;
    return 0;
}
#endif
static uint32_t arb_want(uint32_t c)                    /* client c's feature is used now */
{
#if FELUCCA_FX_SLICER
    if (c < (uint32_t)ARB_SL0 + NTRK) {
        const sl_t *s = &sl[c - ARB_SL0];
        return trk[c - ARB_SL0].p[P_SLCR] == SL_STUT || s->loop || s->rec_on;
    }
#endif
#if FELUCCA_ENG_WAVE
    if (c == (uint32_t)ARB_WAVE) {                      /* (a part on WAVE, or switching to it: engines.c set its
                                                         * table's room aside, part_mem_aside) */
        uint32_t k;
        for (k = 0; k < NPART; k++)
            if (trk[k].eng_req == (uint8_t)ENG_SLOT_WAVE)
                return 1;
        return wt_wanted() >= 0;
    }
#endif
#if FELUCCA_DRUM_X909
    if (c == (uint32_t)ARB_X909)
        return arb_x0x_want(DRUM_UID_X909);
#endif
#if FELUCCA_DRUM_X808
    if (c == (uint32_t)ARB_X808)
        return arb_x0x_want(DRUM_UID_X808);
#endif
#if FELUCCA_PUNCH_SW
    if (c == (uint32_t)ARB_PUNCH)
        return punch_used();
#endif
    (void)c;
    return 0;
}
/* client c's pointer to p (0: not placed), the feature's state reset with it; the interrupts off around it */
static void arb_set(uint32_t c, void *p)
{
    fm1_irq_off();
#if FELUCCA_FX_SLICER
    if (c < (uint32_t)ARB_SL0 + NTRK) {
        sl_t *s = &sl[c - ARB_SL0];
        s->rec = s->loop = s->rp = 0;                   /* (nothing recorded: the next live step records) */
        s->rec_on = 0;
        sl_bp[c - ARB_SL0] = (int16_t *)p;
    }
#endif
#if FELUCCA_ENG_WAVE
    if (c == (uint32_t)ARB_WAVE) {
        wt_have = wt_building = -1;                     /* (no table: wt_service builds the wanted one in it) */
        wt_mip = (int16_t (*)[WT_PTS])p;
        wt_fr = p ? (int32_t *)((uint8_t *)p + WT_NF * WT_PTS * 2u) : 0;
        wt_fi = p ? wt_fr + 1024 : 0;
    }
#endif
#if FELUCCA_DRUM_X909
    if (c == (uint32_t)ARB_X909) {
        x0x_mem(0, p);
        xc.live &= ~((1u << X0X_CH808) - 1u);
    }
#endif
#if FELUCCA_DRUM_X808
    if (c == (uint32_t)ARB_X808) {
        x0x_mem(1, p);
        xc.live &= (1u << X0X_CH808) - 1u;
    }
#endif
#if FELUCCA_PUNCH_SW
    if (c == (uint32_t)ARB_PUNCH)
        punch_mem(p);
#endif
    fm1_irq_on();
}
static void arb_open(uint32_t c, void *p)               /* client c placed at p: zeroed, then in use */
{
    memset(p, 0, arb_bytes(c));
    arb_set(c, p);
    arb_st[c] = FXM_ON;
    arb_full &= ~(1u << c);
}
static void arb_ord(uint8_t *o)                         /* the clients, the largest block first */
{
    uint32_t i, j;
    for (i = 0; i < ARB_NC; i++)
        o[i] = (uint8_t)i;
    for (i = 1; i < ARB_NC; i++)
        for (j = i; j > 0 && arb_bytes(o[j - 1u]) < arb_bytes(o[j]); j--) {
            uint8_t t = o[j];
            o[j] = o[j - 1u], o[j - 1u] = t;
        }
}
/* the main loop, every pass (main.c, after fxm_poll): the buffers after a change. Those no longer used: their pointer 0
 * now, given back two blocks later; those wanted placed, the largest first; one that finds no room refused (bypassed,
 * said once, tried again every pass) */
static void arb_poll(void)
{
    uint8_t o[ARB_NC];
    uint32_t i, c, w = 0;
    for (c = 0; c < ARB_NC; c++) {
        uint32_t u = arb_want(c);
        w |= u << c;
        if (arb_st[c] == FXM_ON && !u) {
            arb_set(c, 0);                              /* (the audio no longer reads it) */
            arb_t0[c] = fxm_tick;
            arb_st[c] = FXM_GOING;
        } else if (arb_st[c] == FXM_GOING && (uint8_t)(fxm_tick - arb_t0[c]) >= 2u) {
            if (u) {
                arb_open(c, arn_ptr(ARB_K(c)));         /* (used again: the same block, zeroed) */
            } else {
                fm1_irq_off();
                arn_give(ARB_K(c));
                fm1_irq_on();
                arb_st[c] = FXM_NONE;
            }
        }
        if (!u)
            arb_full &= ~(1u << c);                     /* (refused, then let go: nothing to place) */
        if (!u && arb_st[c] == FXM_NONE && arn_has_next[ARB_K(c)]) {
            fm1_irq_off();
            arn_has_next[ARB_K(c)] = 0;                 /* (set aside for a change since undone: let go) */
            fm1_irq_on();
        }
    }
#if FELUCCA_PUNCH_SW
    if (punch_asked())
        arb_poff |= (uint8_t)!punch_used();             /* (refused: its MEM FULL PUNCH was said) */
#endif
    if (PART_MEM_SHORT() || fxm_mv)
        return;                                         /* (a part's refused change first; the lines being moved) */
    arb_ord(o);
    for (i = 0; i < ARB_NC; i++) {
        void *p = 0;
        int r;
        c = o[i];
        if (arb_st[c] != FXM_NONE || !(w >> c & 1u) || !arb_bytes(c))
            continue;
        fm1_irq_off();
        r = arn_reserve(ARB_K(c), arb_bytes(c));
        if (!r)
            p = arn_take(ARB_K(c));
        fm1_irq_on();
        if (!r && p) {
            arb_open(c, p);
        } else {
            arb_news |= arb_full >> c & 1u ? 0u : 1u << c;
            arb_full |= 1u << c;
        }
    }
}
#if FELUCCA_FX_SLICER
/* the UI (op_cells.c): vp, a track's SLICER mode, is on STUT and its recording refused (bypassed: FULL, dim, struck) */
static int arb_sl_refused(const int16_t *vp)
{
    uint32_t k;
    for (k = 0; k < NTRK; k++)
        if (vp == &trk[k].p[P_SLCR])
            return *vp == SL_STUT && !sl_bp[k] && (arb_full >> (ARB_SL0 + k) & 1u);
    return 0;
}
#endif
/* one of them still held though nothing uses it: given back within a pass and two blocks (model.c mem_retry waits) */
static int arb_pending(void)
{
    uint32_t c;
    for (c = 0; c < ARB_NC; c++)
        if (arb_st[c] == FXM_GOING || (arb_st[c] == FXM_ON && !arb_want(c)))
            return 1;
    return 0;
}
/* the UI (op_input.c mem_tick): the message for the refusals not said yet, 0 none; said, they are not said again */
static const char *arb_said(void)
{
    uint32_t c, n = arb_news;
#if FELUCCA_PUNCH_SW
    if (arb_poff) {
        arb_poff = 0;
        return "PUNCH OFF";
    }
#endif
    arb_news = 0;
    for (c = 0; c < ARB_NC; c++) {
        if (!(n >> c & 1u))
            continue;
#if FELUCCA_FX_SLICER
        if (c < (uint32_t)ARB_SL0 + NTRK)
            return "MEM FULL SLICER";
#endif
#if FELUCCA_ENG_WAVE
        if (c == (uint32_t)ARB_WAVE)
            return "MEM FULL WAVE";
#endif
#if FELUCCA_PUNCH_SW
        if (c == (uint32_t)ARB_PUNCH)
            return "MEM FULL PUNCH";
#endif
        return "MEM FULL X0X";
    }
    return 0;
}
#else
#define arb_poll() ((void)0)
static int arb_pending(void) { return 0; }
#define arb_said() ((const char *)0)
#endif
#else
#define arb_poll() ((void)0)
#endif
