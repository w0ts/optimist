/* SPDX-License-Identifier: GPL-3.0-only */
/* The generic FX slots (docs: fm1-firmware FX-SLOTS-INSERTS-DESIGN.md, and the per-track insert slots' design,
 * 2026-10-12, its design D1..D9 in docs/UI-OPTIMIST-DESIGN.md "Insert slots"; included by params.c after rev_type.c).
 *   - Effect TYPES: DIST, FILTER, COMP are inserts; CHO, DLY, REV send buses. A type's amount belongs to the type, not
 *     to a slot (D4): a track's P_DIST P_TFLT P_TCOMP, its sends P_CHOR P_DLY P_REV, so presets, locks, motion, MACRO
 *     and SLOOP 2.4 keep their meaning whatever the slots. A type in no slot is not heard and keeps its amounts.
 *   - The four master slots hold the SENDS only (D1): FX > SLOTS, the record's header. One instance per type: a type
 *     held elsewhere swaps the two slots. Default ---- REV DLY CHO (FXS_ORDER).
 *   - Each track (T1 T2 T3 DR) has two INSERT slots, S1 then S2, run in that order after the SLICER (D2): fx.c
 *     ins_part, dbus_run. One type at most once on a track: picking the other slot's type swaps them (D3). A new
 *     track's: S1 DIST, S2 FILTER (D5), both at 0 (bypassed: a compare a block). Old projects: fx_rec.c ins_map.
 *   - Each track's COMP has its own RATIO ATK REL (D6, ins_cset); the drum sounds' COMP takes the drum track's.
 *   - The drum sounds' own DST and CMP (SOUND 3) are always heard, 0 off (D7).
 * Type ids are stored (the FX record): never reuse one. */
enum { FXT_NONE, FXT_DIST, FXT_CHO, FXT_DLY, FXT_REV, FXT_COMP, FXT_FILT,
       FXT_FOLD, FXT_FUZZ, FXT_PHSR, FXT_FLNG, FXT_FRMT, FXT_WIDE,   /* (2026-10-13: fx/insfx/insfx.c, a builder item each) */
       FXT_CHRS, FXT_ECHO,   /* (2026-10-13: insfx.c, the chorus's and a short delay's insert copies: a builder count, fxi.c) */
       FXT_GATE,             /* (2026-10-14: insfx/gater.c, a tempo-synced gate, any number) */
       FXT_DUST,             /* (2026-10-14, fx-rework: the master's DUST, now an insert on any slot: fx.c dust_run) */
       FXT_JUNO,             /* (2026-10-14: JUNO CHORUS, fx/junocho/juno_cho.h; a builder count, fxi.c, as CHRS ECHO) */
       FXT_EQ3,              /* (2026-10-15, fx-pool: EQ3, the three-band DJ EQ, fx/insfx/eq3.c, any number) */
       FXT_N };
/* fx-rework (2026-10-14): COMP is DYN, the compressor, its make-up and the brickwall limiter as one effect (the
 * master's COMP + LIMIT: master_comp.c), on any insert slot; FILT is the track FILTER and the master DJ filter in one
 * (core.h TFLT_ON); DUST an insert too. The master has two insert slots of its own (mst_slot, below) */
#define FXT_DYN FXT_COMP
#define FXT_NEW0 FXT_FOLD                              /* the further insert types FXT_NEW0 .. FXT_NEWN - 1 (insfx.c) */
#define FXT_NEWN (FXT_GATE + 1)
#define FXT_LIM0 FXT_CHRS                              /* the limited ones FXT_LIM0 .. FXT_LIMN - 1 (fxi.c: an instance each) */
#define FXT_LIMN (FXT_ECHO + 1)                        /* (GATER after them: on every track, not limited) */
#define FXT_IS_LIM(t) ((uint32_t)(t) - FXT_LIM0 < (uint32_t)(FXT_LIMN - FXT_LIM0) || (FELUCCA_INS_JCHORUS && (t) == FXT_JUNO) || \
                       (FELUCCA_INS_FLANGER && (t) == FXT_FLNG))   /* (FLNG since fx-pool 2026-10-15) */
#define FX_NSLOT 4u
#define FXT_BIT(x) (1u << (x))
#define FXT_INSERT (FXT_BIT(FXT_DIST) | FXT_BIT(FXT_COMP) | FXT_BIT(FXT_FILT) | FXT_BIT(FXT_FOLD) | FXT_BIT(FXT_FUZZ) | \
                    FXT_BIT(FXT_PHSR) | FXT_BIT(FXT_FLNG) | FXT_BIT(FXT_FRMT) | FXT_BIT(FXT_WIDE) | FXT_BIT(FXT_CHRS) | \
                    FXT_BIT(FXT_ECHO) | FXT_BIT(FXT_GATE) | FXT_BIT(FXT_DUST) | FXT_BIT(FXT_JUNO) | \
                    FXT_BIT(FXT_EQ3))   /* the others: sends */
#define FXT_BUILT ((FELUCCA_FX_DIST ? FXT_BIT(FXT_DIST) : 0u) | (FELUCCA_FX_CHORUS ? FXT_BIT(FXT_CHO) : 0u) | \
                   (FELUCCA_FX_DELAY ? FXT_BIT(FXT_DLY) : 0u) | (FELUCCA_FX_REVERB ? FXT_BIT(FXT_REV) : 0u) |  \
                   (TFLT_ON ? FXT_BIT(FXT_FILT) : 0u) | (FELUCCA_MASTER_COMP ? FXT_BIT(FXT_COMP) : 0u) |   \
                   (FELUCCA_INS_FOLD ? FXT_BIT(FXT_FOLD) : 0u) | (FELUCCA_INS_FUZZ ? FXT_BIT(FXT_FUZZ) : 0u) |      \
                   (FELUCCA_INS_PHASER ? FXT_BIT(FXT_PHSR) : 0u) | (FELUCCA_INS_FLANGER ? FXT_BIT(FXT_FLNG) : 0u) | \
                   (FELUCCA_INS_FORMANT ? FXT_BIT(FXT_FRMT) : 0u) | (FELUCCA_INS_WIDTH ? FXT_BIT(FXT_WIDE) : 0u) |  \
                   (FELUCCA_INS_CHORUS > 0 ? FXT_BIT(FXT_CHRS) : 0u) | (FELUCCA_INS_DELAY > 0 ? FXT_BIT(FXT_ECHO) : 0u) | \
                   (FELUCCA_INS_GATER ? FXT_BIT(FXT_GATE) : 0u) | (FELUCCA_FX_DUST ? FXT_BIT(FXT_DUST) : 0u) | \
                   (FELUCCA_INS_JCHORUS > 0 ? FXT_BIT(FXT_JUNO) : 0u) | (FELUCCA_INS_EQ3 ? FXT_BIT(FXT_EQ3) : 0u))
/* a type insfx.c runs (ifx_part, ifx_bus, ifx_st): the further ones, JUNO CHORUS, EQ3 */
#define IFX_TYPE(t) (((t) >= FXT_NEW0 && (t) < FXT_NEWN) || (FELUCCA_INS_JCHORUS && (t) == FXT_JUNO) || (FELUCCA_INS_EQ3 && (t) == FXT_EQ3))
_Static_assert(FXT_N <= 32, "the type masks (ins_live): 32 bits");
#define FXT_SEND_OK(t) ((t) < FXT_N && !(FXT_INSERT >> (t) & 1u) && (t) != FXT_NONE)
/* each type's per-track amount (a P_* id; its label names the slot), 0xFF: none in this build */
static const uint8_t FXT_AMT[FXT_N] = {
    0xFF, P_DIST, P_CHOR, P_DLY, P_REV,
#if FELUCCA_MASTER_COMP
    P_TCOMP,
#else
    0xFF,
#endif
#if TFLT_ON
    P_TFLT,
#else
    0xFF,
#endif
#define FXT_AMT_NEW(on, id) ((on) ? (uint8_t)(id) : 0xFFu)   /* (the ids exist when any further type is built: core.h) */
#if INS_FX
    FXT_AMT_NEW(FELUCCA_INS_FOLD, P_TFOLD), FXT_AMT_NEW(FELUCCA_INS_FUZZ, P_TFUZZ), FXT_AMT_NEW(FELUCCA_INS_PHASER, P_TPHSR),
    FXT_AMT_NEW(FELUCCA_INS_FLANGER, P_TFLNG), FXT_AMT_NEW(FELUCCA_INS_FORMANT, P_TFRMT), FXT_AMT_NEW(FELUCCA_INS_WIDTH, P_TWIDE),
    FXT_AMT_NEW(FELUCCA_INS_CHORUS > 0, P_TCHRS), FXT_AMT_NEW(FELUCCA_INS_DELAY > 0, P_TECHO),
    FXT_AMT_NEW(FELUCCA_INS_GATER, P_TGATE),
#else
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
#endif
#if FELUCCA_FX_DUST
    P_TDUST,
#else
    0xFF,
#endif
#if FELUCCA_INS_JCHORUS
    P_TJCHO,
#else
    0xFF,
#endif
#if FELUCCA_INS_EQ3
    P_TEQ,
#else
    0xFF,
#endif
};
/* THE send order (2026-10-15, the user: "match the order between master return and the send"): REV DLY CHO. The
 * returns A B C hold them by default, FX > SLOTS lists them so, and every screen shows the sends and the returns in the
 * returns' order (fxs_card: card k is return R(k+1), whatever it holds), so a send's card sits on its return's knob */
#define FXS_ORDER FXT_REV, FXT_DLY, FXT_CHO
static const uint8_t FXS_ORD[3] = {FXS_ORDER};
/* the header of the default layout, and the layout of a project with no FX record: DIST, then the sends (fx_rec.c ins_map
 * reads a layout's inserts; a record's header is written in it as far as it can: fx_rec.c fxr_head) */
static const uint8_t FXS_OLD[FX_NSLOT] = {FXT_DIST, FXS_ORDER};
static const uint8_t FXS_DEF[FX_NSLOT] = {FXT_NONE, FXS_ORDER};
static uint8_t fxs_slot[FX_NSLOT] = {FXT_NONE, FXS_ORDER};   /* (an id this build lacks: kept, unheard) */
/* ---- the returns A B C (fx-pool 2026-10-15: "any effect type ... AND a return-bus slot"). Three send LINES, the buses
 * every track's sends feed (fx.c send_c send_d send_r: a track's P_CHOR, P_DLY, P_REV, the drum lanes' and the USB input's
 * sends), named by the send type that owns each: line 0 CHO, 1 DLY, 2 REV. Each return slot (1..3) holds a type and owns one
 * line (fxs_line). A send type (CHO DLY REV: the shared chorus line, the long delay, the reverb tank, each one buffer) is
 * pinned to its own line, so the default layout and every project of before play the same code (golden renders unchanged).
 * Any other type (RT_OK: every insert type but GATER, which needs a track's tempo values) runs on its line's send, mono in,
 * L = R, as a stereo owner (fx.c rt_run: its own states, the pool's instances for the limited ones: fxi.c FXI_RT0), 100 %
 * wet: the return outputs only the processed send (never a track's dry), times the line's return level (G_CRET + line: CHO
 * RET, DLY RET, REV RET are each line's), its amount the slot's own (fxs_ramt, RT_DEF when loaded). That line's send bus
 * (the chorus, the delay, the reverb) then hears silence */
#define RT_OK (FXT_BUILT & FXT_INSERT & ~FXT_BIT(FXT_GATE))
/* the audio's owner indexes (fx.c, insfx.c: a state a part, then the stereo owners' L, R): the parts 0 .. NPART - 1, the
 * drum bus, the master, the USB input, the returns A B C */
#define IFX_BUS NPART
#define IFX_MST (NPART + 2u)
#define IFX_UT (NPART + 4u)
#define IFX_RT0 (NPART + 6u)
#define IFX_RT(j) (IFX_RT0 + 2u * (uint32_t)(j))
/* the audio's index k -> its owner: T1 T2 T3 DR 0..3, the master 4, the USB input 5, the returns' lines 6 7 8 (fxi.c, eq3.c) */
AINL uint32_t ifx_owner(uint32_t k)
{
    return k < NTRK ? k : k == IFX_MST ? NTRK : k == IFX_UT ? NTRK + 1u : k >= IFX_RT0 ? NTRK + 2u + ((k - IFX_RT0) >> 1) % 3u : 0u;
}
#define FXS_LN(t) ((t) == FXT_CHO ? 0u : (t) == FXT_DLY ? 1u : (t) == FXT_REV ? 2u : 3u)   /* a send type's line */
#define FXS_GEN(t) ((t) < FXT_N && (RT_OK >> (t) & 1u))  /* a type a return may hold that is not a send */
static const uint8_t FXS_LT[3] = {FXT_CHO, FXT_DLY, FXT_REV};   /* line j's send type (its track amount: P_CHOR P_DLY P_REV) */
/* what the audio reads (fxs_fix writes them): the types heard in a return (built, in a slot: FXS_HAS), the lines with a
 * return (FXS_ON(CHO DLY REV): its sends are heard), each line's other type (fxs_rty, FXT_NONE: a send type or none), the
 * slot holding it (fxs_rsl), and the drum lanes' send level masks REV DLY CHO (drum_sends.c: 31, or 0 while the line has no
 * return: level 0 sends nothing) */
#define FXS_LIVE_DEF (FXT_BUILT & (FXT_BIT(FXT_CHO) | FXT_BIT(FXT_DLY) | FXT_BIT(FXT_REV)))
static uint32_t fxs_live = FXS_LIVE_DEF;
static uint8_t fxs_lnl = (uint8_t)((FELUCCA_FX_CHORUS ? 1u : 0u) | (FELUCCA_FX_DELAY ? 2u : 0u) | (FELUCCA_FX_REVERB ? 4u : 0u));
static uint8_t fxs_line[FX_NSLOT] = {0xFF, FXS_LN(FXT_REV), FXS_LN(FXT_DLY), FXS_LN(FXT_CHO)};   /* slot k's line (0xFF:
                                                        * none; FXS_DEF's) */
static int16_t fxs_ramt[FX_NSLOT];                     /* a return's amount (a type not a send) */
static volatile uint8_t fxs_rty[3] = {FXT_NONE, FXT_NONE, FXT_NONE};
static volatile uint8_t fxs_rsl[3] = {3, 2, 1};       /* (line j's slot: FXS_DEF's, CHO in C, DLY B, REV A) */
static uint8_t fxs_lm[3] = {31, 31, 31};
#define FXS_HAS(x) (fxs_live >> (x) & 1u)
#define FXS_ON(x) (fxs_lnl >> FXS_LN(x) & 1u)
#define FXI_RT_HOLDS(j, t) (fxs_rty[(j) % 3u] == (t) && FXS_GEN(t))   /* (fxi.c: a return owning a limited type) */
/* a type's amount on a return when loaded: the full-wet point (the share / feedback types' middle; JUNO CHORUS's MIX 127;
 * FILT's open centre) */
AINL int16_t rt_def(uint32_t t) { return t == FXT_JUNO ? 127 : t == FXT_FILT ? 0 : t == FXT_EQ3 ? 1 : 64; }   /* (EQ3: ON) */
#if INS_LIM
/* the limited types' instances (fx/insfx/fxi.c) follow the slots in the main loop: a change of slots asks for it (1), a
 * project load for every instance handed out again in track order (2: deterministic). Written anywhere (a load may run
 * in the ISR), read and cleared by fxi_poll only */
static volatile uint8_t fxi_dirty;
#define FXI_ASK(v) (fxi_dirty |= (uint8_t)(v))
/* the send lines lent to the limited inserts (fxi.c, "Lending"): bit 0 the chorus send's (cho_buf: CHRS), bit 1 the
 * delay send's (dly_buf: ECHO). Set, its bus does not run nor clear its line (fx.c fx_ret_block): the line is the
 * inserts'. Written by fxi_poll only, with the interrupts off; the audio reads it once a block */
static volatile uint8_t fxl_lent;
#else
#define FXI_ASK(v) ((void)0)
#endif

/* the lines after a change of the return slots: a send type on its own line; another type keeps its line while free,
 * else takes the first free one; then what the audio reads */
static void fxs_fix(void)
{
    uint32_t k, t, used = 0, live = 0, lnl = 0;
    uint8_t rty[3] = {FXT_NONE, FXT_NONE, FXT_NONE}, rsl[3] = {0, 0, 0};
    for (k = 1; k < FX_NSLOT; k++)
        if (FXS_LN(fxs_slot[k]) < 3u)
            fxs_line[k] = (uint8_t)FXS_LN(fxs_slot[k]), used |= 1u << fxs_line[k];
    for (k = 1; k < FX_NSLOT; k++) {
        t = fxs_slot[k];
        if (FXS_LN(t) < 3u)
            continue;
        if (t == FXT_NONE) {
            fxs_line[k] = 0xFF;
            continue;
        }
        if (fxs_line[k] >= 3u || (used >> fxs_line[k] & 1u)) {
            uint32_t j;
            for (j = 0; j < 3u && (used >> j & 1u); j++)
                ;
            fxs_line[k] = (uint8_t)j;                  /* (three slots, three lines: always one) */
        }
        used |= 1u << fxs_line[k];
    }
    for (k = 1; k < FX_NSLOT; k++) {
        t = fxs_slot[k];
        if (t >= FXT_N || !(FXT_BUILT >> t & 1u) || fxs_line[k] >= 3u)
            continue;
        if (FXS_LN(t) < 3u || FXS_GEN(t)) {
            live |= FXT_BIT(t), lnl |= 1u << fxs_line[k];
            if (FXS_GEN(t))
                rty[fxs_line[k]] = (uint8_t)t;
            rsl[fxs_line[k]] = (uint8_t)k;
        }
    }
    IRQ_SAVE(ie);
    fxs_live = live, fxs_lnl = (uint8_t)lnl;
    for (k = 0; k < 3u; k++)
        fxs_rty[k] = rty[k], fxs_rsl[k] = rsl[k];
    fxs_lm[0] = lnl >> 2 & 1u ? 31u : 0u;              /* (REV DLY CHO: lines 2 1 0) */
    fxs_lm[1] = lnl >> 1 & 1u ? 31u : 0u;
    fxs_lm[2] = lnl & 1u ? 31u : 0u;
    IRQ_RESTORE(ie);
    FXI_ASK(1u);                                       /* (a send back in a slot: its line, if lent, back; a return's
                                                        * limited type: its instance, fxi.c) */
}

/* a layout s (the record's header, the SLOTS page of before, the editor) into the master slots: sends only (an insert id:
 * empty, its place is the tracks' now: fx_rec.c ins_map; a return's other type comes from the record's RET_TLV, rt_untlv);
 * a type a second time leaves its slot empty. The master has 3 return buses (fx-rework 2026-10-14): A B C are slots
 * 1..3, slot 0 stays empty (the stored layout keeps its 4 places, so every project of before reads as it was); a send an
 * old layout holds in slot 0 takes the first empty return (3 sends, one instance each: there is always one) */
static void fxs_set(const uint8_t *s)
{
    uint32_t k, j;
    for (k = 0; k < FX_NSLOT; k++) {
        uint32_t t = s[k] < FXT_N && !FXT_SEND_OK(s[k]) ? (uint32_t)FXT_NONE : s[k];   /* (an id this build lacks:
                                                        * kept, unheard) */
        for (j = 0; j < k; j++)
            if (fxs_slot[j] == t)
                t = FXT_NONE;
        fxs_slot[k] = (uint8_t)t, fxs_ramt[k] = 0;
    }
    if (fxs_slot[0] != FXT_NONE) {                     /* (slot 0's send into the first empty return) */
        for (k = 1; k < FX_NSLOT && fxs_slot[k] != FXT_NONE; k++)
            ;
        if (k < FX_NSLOT)
            fxs_slot[k] = fxs_slot[0];
        fxs_slot[0] = FXT_NONE;                        /* (a fourth send type: no return for it) */
    }
    fxs_fix();
}

/* slot k's amount id (0xFF: empty, or a type this build lacks): a send's track amount; a return's other type: its line's
 * send (P_CHOR P_DLY P_REV: what the tracks send to it) */
static uint32_t fxs_amt(uint32_t k)
{
    uint32_t t = fxs_slot[k & 3u];
    if (t < FXT_N && FXS_GEN(t) && fxs_line[k & 3u] < 3u)
        return FXT_AMT[FXS_LT[fxs_line[k & 3u]]];
    return t < FXT_N && (FXT_BUILT >> t & 1u) ? FXT_AMT[t] : 0xFFu;
}
/* the i-th send line heard, in the master slots' order (SENDS, the SLOOP FX page's K2..K4): its send type (CHO DLY REV:
 * the line's name, its track amount), FXT_NONE past them */
static uint32_t fxs_send(uint32_t i)
{
    uint32_t k;
    for (k = 1; k < FX_NSLOT; k++)
        if (fxs_line[k] < 3u && (fxs_lnl >> fxs_line[k] & 1u) && !i--)
            return FXS_LT[fxs_line[k]];
    return FXT_NONE;
}
#define fxs_send_amt(i) (fxs_send(i) == FXT_NONE ? 0xFFu : (uint32_t)FXT_AMT[fxs_send(i)])
/* card k (0..2) of every sends / returns set (the SENDS pages, the mixer's sends and returns, RETURNS): return R(k+1)'s
 * send line, as its send type (CHO DLY REV: the line's track amount P_CHOR P_DLY P_REV, its return level G_CRET + FXS_LN;
 * a return holding another type: its line's), FXT_NONE: an empty return (a blank card) */
static uint32_t fxs_card(uint32_t k)
{
    uint32_t j = k < 3u ? fxs_line[k + 1u] : 0xFFu;
    return j < 3u && (fxs_lnl >> j & 1u) ? FXS_LT[j] : FXT_NONE;
}
#define FXS_DSI(t) (2u - FXS_LN(t))                    /* a send type's index in the REV DLY CHO tables (drum_sends.c's
                                                        * fields, the kit's levels, the USB input's UTV_REV ..) */
/* line j's name (the type its return holds: a send's, or the other type's), "" none */
static const char *fxs_line_name(uint32_t j)
{
    uint32_t k;
    for (k = 1; k < FX_NSLOT; k++)
        if (fxs_line[k] == j % 3u && fxs_slot[k] < FXT_N && fxs_slot[k] != FXT_NONE && FXT_AMT[fxs_slot[k]] != 0xFFu)
            return fxs_slot[k] == FXT_DYN ? "DYN" : TP[FXT_AMT[fxs_slot[k]]].label;
    return "";
}
/* a track's send amount's descriptor (id P_CHOR P_DLY P_REV: named by its line's return when that holds another type, so
 * the SENDS cards say where they go) */
static const param_desc_t *fxs_send_d(uint32_t id)
{
    static param_desc_t d[3];
    uint32_t j = id == P_CHOR ? 0u : id == P_DLY ? 1u : id == P_REV ? 2u : 3u;
    if (j >= 3u || fxs_rty[j] == FXT_NONE)
        return &TP[id];
    d[j] = TP[id];
    d[j].label = fxs_line_name(j);
    return &d[j];
}
/* line j's return level's card name: "CHO RET" .. for a send, "<type> RET" (a long name: "<type> RT") for another type */
static const char *fxs_ret_label(uint32_t j)
{
    static const char *const L[3] = {"CHO RET", "DLY RET", "REV RET"};
    static char b[3][9];
    const char *t = fxs_line_name(j % 3u);
    uint32_t n = 0;
    if (fxs_rty[j % 3u] == FXT_NONE)
        return L[j % 3u];
    while (t[n] && n < 4u)
        b[j % 3u][n] = t[n], n++;
    b[j % 3u][n++] = ' ', b[j % 3u][n++] = 'R';
    if (n < 6u)
        b[j % 3u][n++] = 'E';
    b[j % 3u][n++] = 'T', b[j % 3u][n] = 0;
    return b[j % 3u];
}
/* a return's amount (slot k, its other type) as the audio reads it */
AINL int32_t rt_amt(uint32_t j) { return fxs_ramt[fxs_rsl[j % 3u] % FX_NSLOT]; }

/* SOUND 3's cell k (the drum sound picked: drum_sends.c dsend_desc's value id + 16): its DST, then its sends REV DLY
 * CHO in the master slots' order (a line with a return: its send), then its CMP while there is room (DST and CMP always
 * heard: D7); 0xFF none. The default: DST CHO DLY REV, SLOOP's page as it was */
static uint32_t fxs_lane_id(uint32_t k)
{
    static const uint8_t LID[FXT_N] = {0xFF, 19, 18, 17, 16, 20, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};   /* (by type: NONE DIST
                                                        * CHO DLY REV COMP FILT, the further inserts, EQ3 too: none) */
    _Static_assert(sizeof LID == FXT_N, "LID: one entry per FX type (a new type needs its entry, 0xFF: none)");
    uint32_t i, n = 0;
    if (FELUCCA_FX_DIST && n++ == (k & 3u))
        return 19u;
    for (i = 0; i < 3u && fxs_send(i) != FXT_NONE; i++)
        if (n++ == (k & 3u))
            return LID[fxs_send(i)];
    if (FELUCCA_MASTER_COMP && n == (k & 3u))
        return 20u;
    return 0xFFu;
}

/* type t into master slot k (FX > SLOTS, the editor): a send, or (fx-pool) any other type a return may hold (RT_OK); a
 * type another slot holds swaps places with this slot's (its line and amount with it); a type new to the returns: its
 * amount rt_def. Slot 0: no return */
static void fxs_load(uint32_t k, uint32_t t)
{
    uint32_t j;
    k &= 3u;
    if ((t != FXT_NONE && !FXT_SEND_OK(t) && !FXS_GEN(t)) || !k || fxs_slot[k] == t)
        return;
    for (j = 1; j < FX_NSLOT; j++)
        if (fxs_slot[j] == t && j != k && t != FXT_NONE) {
            uint8_t ln = fxs_line[j];
            int16_t am = fxs_ramt[j];
            fxs_slot[j] = fxs_slot[k], fxs_line[j] = fxs_line[k], fxs_ramt[j] = fxs_ramt[k];
            fxs_slot[k] = (uint8_t)t, fxs_line[k] = ln, fxs_ramt[k] = am;
            fxs_fix();
            return;
        }
    fxs_slot[k] = (uint8_t)t;
    fxs_ramt[k] = FXS_GEN(t) ? rt_def(t) : 0;
    fxs_fix();
}
/* the returns' TLV (fx_rec.c RET_TLV): slots 1..3's type, line, amount; 0: no return holds another type (the header says
 * it all) */
#define RT_TLV_N 9u
static uint32_t rt_tlv(uint8_t *o)
{
    uint32_t k, any = 0;
    for (k = 1; k < FX_NSLOT; k++) {
        any |= FXS_GEN(fxs_slot[k]);
        o[3u * (k - 1u)] = fxs_slot[k], o[3u * (k - 1u) + 1u] = fxs_line[k];
        o[3u * (k - 1u) + 2u] = (uint8_t)(int8_t)fxs_ramt[k];
    }
    return any ? RT_TLV_N : 0u;
}
static void rt_untlv(const uint8_t *a, uint32_t n)      /* (after fxs_set: the header's sends; a short one: as none) */
{
    uint32_t k, j;
    if (n < RT_TLV_N)
        return;
    for (k = 1; k < FX_NSLOT; k++) {
        uint32_t t = a[3u * (k - 1u)];
        if (!(t < FXT_N && (FXT_SEND_OK(t) || FXS_GEN(t))))
            t = FXT_NONE;                               /* (a type this build cannot hold there: empty) */
        for (j = 1; j < k; j++)
            if (fxs_slot[j] == t)
                t = FXT_NONE;
        fxs_slot[k] = (uint8_t)t, fxs_line[k] = a[3u * (k - 1u) + 1u];
        fxs_ramt[k] = t != FXT_NONE && FXT_AMT[t] != 0xFFu ?
                      (int16_t)clamp((int8_t)a[3u * (k - 1u) + 2u], TP[FXT_AMT[t]].min, TP[FXT_AMT[t]].max) : 0;
    }
    fxs_fix();
}

/* ---- the sends between the returns (2026-10-16, the user: "ideally A should have send B, C"): left to right, A -> B,
 * A -> C, B -> C, so the returns render in one order (A, then B with A's send in its input, then C with A's and B's: fx.c
 * rs_block, fx_buses) and no such send can come back. The builder item RET_FB adds the sends right to left, B -> A,
 * C -> A, C -> B (the feedback: "feedback is good :)"): each takes its return's output of the block before (a block's
 * copy, fx.c rs_fbv: the loop's latency, 32 samples), DC-blocked and soft-clipped, at half a forward send's gain (127:
 * 0.5), so no loop runs away and nothing in one block depends on itself. A send takes its return's output as heard (after its RET
 * level, the reverb's one block late on the second core as it is heard), mono, times its amount (0..127, as a track's send),
 * into the line of the return it goes to. Positional: A's send to B stays A's to B whatever the two hold (a swap of types
 * does not move it). 0: nothing runs for it. A project keeps them in its FX record (fx_rec.c RSND_TLV: none = all 0) */
enum { RS_AB, RS_AC, RS_BC, RS_BA, RS_CA, RS_CB, RS_NMAX };   /* (the stored form's order: the forward ones first) */
#define RS_NF 3u                                        /* the forward sends; RS_NF .. RS_N - 1 the feedback ones */
#define RS_N (FELUCCA_RET_FB ? (uint32_t)RS_NMAX : RS_NF)
static const uint8_t RS_SRC[RS_NMAX] = {1, 1, 2, 2, 3, 3}, RS_DST[RS_NMAX] = {2, 3, 3, 1, 1, 2};   /* the master slots
                                                        * (A B C = 1 2 3) */
static int16_t rs_amt[RS_NMAX];
/* the i-th send of return slot s (1..3), in the order of the returns it goes to (A B C), its index into rs_amt; RS_N: none */
static uint32_t rs_of(uint32_t s, uint32_t i)
{
    uint32_t k, d;
    for (d = 1; d < FX_NSLOT; d++)
        for (k = 0; k < RS_N; k++)
            if (RS_SRC[k] == s && RS_DST[k] == d && !i--)
                return k;
    return RS_N;
}
AINL int32_t rs_any(void)                              /* a send between the returns set (fx.c rets_mix) */
{
    uint32_t k;
    int32_t a = 0;
    for (k = 0; k < RS_N; k++)
        a |= rs_amt[k];
    return a;
}
/* send k (rs_amt's index) as a knob: its value and descriptor, named by the return it goes to (that return's type: its line's
 * name, as a track's send card); 0: its source or its target holds nothing (a blank card) */
static const param_desc_t *rs_desc(uint32_t k, int16_t **vp)
{
    static param_desc_t d[RS_N];
    uint32_t a, b;
    *vp = 0;
    if (k >= RS_N)
        return 0;
    a = fxs_line[RS_SRC[k]], b = fxs_line[RS_DST[k]];
    if (a >= 3u || b >= 3u || !(fxs_lnl >> a & 1u) || !(fxs_lnl >> b & 1u))
        return 0;
    d[k] = TP[P_REV];
    d[k].label = fxs_line_name(b);
    *vp = &rs_amt[k];
    return &d[k];
}
/* the stored form (fx_rec.c RSND_TLV): A -> B, A -> C, B -> C (with RET_FB then B -> A, C -> A, C -> B), a byte each; 0:
 * every send at 0 (nothing to keep) */
static uint32_t rs_tlv(uint8_t *o)
{
    uint32_t k, any = 0;
    for (k = 0; k < RS_N; k++)
        o[k] = (uint8_t)rs_amt[k], any |= (uint32_t)rs_amt[k];
    return any ? RS_N : 0u;
}
static void rs_untlv(const uint8_t *a, uint32_t n)      /* (0, 0: none, every send at 0; a longer form's further sends: skipped) */
{
    uint32_t k;
    for (k = 0; k < RS_N; k++)
        rs_amt[k] = (int16_t)(k < n ? clamp(a[k], 0, 127) : 0);
}


#if FELUCCA_INS_EQ3
/* ---- EQ3's settings (fx/insfx/eq3.c: the kernel) */
#define EQ_NO (NTRK + 5u)                               /* the owners (ifx_owner): T1 T2 T3 DR, MST, USB, the returns' lines */
enum { EQV_LO, EQV_MID, EQV_HI, EQV_FL, EQV_FH, EQV_N };
#define EQ_KILL (-31)
#define EQ_FL_DEF 32                                    /* 250 Hz */
#define EQ_FH_DEF 28                                    /* 2.5 kHz */
#define EQ_FMAX 60
#define EQ_V_DEF {0, 0, 0, EQ_FL_DEF, EQ_FH_DEF}
static int16_t eq3_v[EQ_NO][EQV_N] = {EQ_V_DEF, EQ_V_DEF, EQ_V_DEF, EQ_V_DEF, EQ_V_DEF, EQ_V_DEF, EQ_V_DEF, EQ_V_DEF, EQ_V_DEF};
_Static_assert(EQ_NO == 9u, "eq3_v: nine owners");
/* (made by a few lines of Python: 250 x 2^((p - 32) / 14) Hz, 2500 x 2^((p - 28) / 14) Hz; G = g / (1 + g), g = tan(pi f / 44100), Q16 (a TPT one-pole's: eq3.c);
 * the gains 10^(dB / 20), Q15, [0] KILL, [31] 0 dB) */
static const uint16_t EQ_FL_HZ[61] = {
    51, 54, 57, 59, 62, 66, 69, 73, 76, 80, 84, 88, 93, 98, 103,
    108, 113, 119, 125, 131, 138, 145, 152, 160, 168, 177, 186, 195, 205, 215,
    226, 238, 250, 263, 276, 290, 305, 320, 336, 354, 371, 390, 410, 431, 453,
    476, 500, 525, 552, 580, 610, 640, 673, 707, 743, 781, 820, 862, 906, 952,
    1000,
};
static const uint16_t EQ_FL_K[61] = {
    238, 251, 263, 277, 290, 305, 321, 337, 354, 372, 390, 410, 431, 452, 475,
    499, 524, 551, 578, 608, 638, 670, 704, 739, 776, 815, 856, 899, 944, 991,
    1040, 1092, 1147, 1204, 1264, 1327, 1393, 1462, 1534, 1610, 1690, 1774, 1861, 1953, 2049,
    2150, 2255, 2365, 2481, 2602, 2729, 2861, 3000, 3145, 3297, 3456, 3622, 3796, 3977, 4167,
    4365,
};
static const uint16_t EQ_FH_HZ[61] = {
    625, 657, 690, 725, 762, 801, 841, 884, 929, 976, 1025, 1077, 1132, 1190, 1250,
    1313, 1380, 1450, 1524, 1601, 1682, 1768, 1857, 1952, 2051, 2155, 2264, 2379, 2500, 2627,
    2760, 2900, 3048, 3202, 3365, 3536, 3715, 3904, 4102, 4310, 4529, 4758, 5000, 5254, 5520,
    5801, 6095, 6404, 6730, 7071, 7430, 7807, 8203, 8620, 9057, 9517, 10000, 10508, 11041, 11601,
    12190,
};
static const uint16_t EQ_FH_K[61] = {
    2795, 2931, 3073, 3222, 3377, 3539, 3709, 3887, 4072, 4266, 4469, 4680, 4901, 5131, 5372,
    5622, 5884, 6156, 6440, 6736, 7044, 7365, 7698, 8045, 8406, 8782, 9172, 9577, 9997, 10434,
    10887, 11356, 11843, 12348, 12871, 13413, 13974, 14554, 15155, 15776, 16419, 17084, 17771, 18482, 19217,
    19977, 20762, 21575, 22416, 23286, 24188, 25122, 26091, 27098, 28144, 29234, 30371, 31560, 32805, 34114,
    35494,
};
static const uint16_t EQ_G[38] = {
    0, 1036, 1163, 1305, 1464, 1642, 1843, 2068, 2320, 2603, 2920, 3277, 3677,
    4125, 4629, 5193, 5827, 6538, 7336, 8231, 9235, 10362, 11627, 13045, 14637, 16423,
    18427, 20675, 23198, 26029, 29205, 32768, 36766, 41252, 46286, 51934, 58271, 65381,
};
_Static_assert(FS == 44100, "EQ3's one-pole coefficients are for 44.1 kHz");
static int eq3_dflt(void)                               /* every owner's settings the defaults */
{
    uint32_t o, i;
    static const int16_t D[EQV_N] = EQ_V_DEF;
    for (o = 0; o < EQ_NO; o++)
        for (i = 0; i < EQV_N; i++)
            if (eq3_v[o][i] != D[i])
                return 0;
    return 1;
}
static void eq3_defaults(void)
{
    uint32_t o, i;
    static const int16_t D[EQV_N] = EQ_V_DEF;
    for (o = 0; o < EQ_NO; o++)
        for (i = 0; i < EQV_N; i++)
            eq3_v[o][i] = D[i];
}
/* EQ3's descriptors: the gains (KILL, -30 .. +6 dB), FREQ LO, FREQ HI (an index into EQ_FL_HZ / EQ_FH_HZ); fxs_fmt shows
 * them (params.c param_format) */
static const char EQ_TAG_DB[] = "dB", EQ_TAG_FL[] = "Hz", EQ_TAG_FH[] = "Hz";
static const param_desc_t EQ_D[EQV_N] = {
    {"LO", F_INT, EQ_KILL, 6, 0, 0, EQ_TAG_DB}, {"MID", F_INT, EQ_KILL, 6, 0, 0, EQ_TAG_DB}, {"HI", F_INT, EQ_KILL, 6, 0, 0, EQ_TAG_DB},
    {"FREQ L", F_INT, 0, EQ_FMAX, EQ_FL_DEF, 0, EQ_TAG_FL}, {"FREQ H", F_INT, 0, EQ_FMAX, EQ_FH_DEF, 0, EQ_TAG_FH}};
static int fxs_fmt(const param_desc_t *d, int32_t v, char *val, const char **unit)
{
    uint32_t hz;
    if (d < EQ_D || d >= EQ_D + EQV_N)
        return 0;
    if (d->unit == EQ_TAG_DB) {
        if (v <= EQ_KILL) {
            str_cpy(val, "KILL", 6);
            *unit = "";
        } else {
            if (v > 0)
                val[0] = '+', fmt_int(val + 1, v);
            else
                fmt_int(val, v);
            *unit = "dB";
        }
        return 1;
    }
    hz = (d->unit == EQ_TAG_FL ? EQ_FL_HZ : EQ_FH_HZ)[clamp(v, 0, EQ_FMAX)];
    if (hz < 1000u) {
        fmt_int(val, (int32_t)hz);
        *unit = "Hz";
    } else if (hz < 10000u) {
        fmt_fix(val, (int32_t)((hz + 50u) / 100u), 1);
        *unit = "kHz";
    } else {
        fmt_int(val, (int32_t)((hz + 500u) / 1000u));
        *unit = "kHz";
    }
    return 1;
}
#endif

/* ---- each track's two insert slots (S1, S2; T1 T2 T3 DR). The audio reads ins_slot once a block (fx.c ins_part,
 * dbus_run); the main loop writes them with the interrupts off (a swap is two bytes) */
#define INS_N 2u
#define INS_DEF0 (FELUCCA_FX_DIST ? FXT_DIST : FXT_NONE)
#define INS_DEF1 (TFLT_ON ? FXT_FILT : FXT_NONE)
_Static_assert(NTRK == 4, "ins_slot: T1 T2 T3 DR");
static uint8_t ins_slot[NTRK][INS_N] = {{INS_DEF0, INS_DEF1}, {INS_DEF0, INS_DEF1}, {INS_DEF0, INS_DEF1},
                                        {INS_DEF0, INS_DEF1}};
/* the types each track's slots hold and this build has (what is heard): the guard, the pages, the 2.4 export */
#define INS_LIVE_DEF ((FXT_BIT(INS_DEF0) | FXT_BIT(INS_DEF1)) & FXT_BUILT & FXT_INSERT)
static uint32_t ins_live[NTRK] = {INS_LIVE_DEF, INS_LIVE_DEF, INS_LIVE_DEF, INS_LIVE_DEF};
#define INS_ON(k, x) (ins_live[(k) % NTRK] >> (x) & 1u)
static uint8_t ins_drop;                               /* the tracks (a bit each) whose third insert the last load's
                                                        * mapping dropped (fx_rec.c ins_map; TOOLS > MISS: miss.c) */

/* track k's slots from s (the record, the editor, ins_load): an insert id (kept even when this build lacks it,
 * unheard), else empty; the same type a second time: empty */
static void ins_put(uint32_t k, const uint8_t *s)
{
    uint32_t a = s[0] < FXT_N && (FXT_INSERT >> s[0] & 1u) ? s[0] : (uint32_t)FXT_NONE;
    uint32_t b = s[1] < FXT_N && (FXT_INSERT >> s[1] & 1u) && s[1] != a ? s[1] : (uint32_t)FXT_NONE;
    k %= NTRK;
    IRQ_SAVE(ie);
    ins_slot[k][0] = (uint8_t)a, ins_slot[k][1] = (uint8_t)b;
    ins_live[k] = (FXT_BIT(a) | FXT_BIT(b)) & FXT_BUILT & FXT_INSERT;
    FXI_ASK(1u);                                       /* (a limited type's instance: fxi.c, the main loop) */
    IRQ_RESTORE(ie);
}
/* track k's slot holding type t: 1 S1, 2 S2, 0 none (or not built) */
static uint32_t ins_has(uint32_t k, uint32_t t)
{
    k %= NTRK;
    return !(t < FXT_N && INS_ON(k, t)) ? 0u : ins_slot[k][0] == t ? 1u : 2u;
}
/* type t into track k's slot s (INSERTS, the mixer, the editor): the type the other slot holds swaps them (D3) */
static void ins_load(uint32_t k, uint32_t s, uint32_t t)
{
    uint8_t v[INS_N];
    k %= NTRK, s &= 1u;
    v[0] = ins_slot[k][0], v[1] = ins_slot[k][1];
    if (t != FXT_NONE && v[s ^ 1u] == t)
        v[s ^ 1u] = v[s];
    v[s] = (uint8_t)t;
    ins_put(k, v);
}
/* track k's slot s's amount (a P_* id: the type's), 0xFF: empty or not built */
static uint32_t ins_amt(uint32_t k, uint32_t s)
{
    uint32_t t = ins_slot[k % NTRK][s & 1u];
    return t < FXT_N && (FXT_BUILT >> t & 1u) ? FXT_AMT[t] : 0xFFu;
}
/* every track's slots the default (a new project, the host tests) */
static void mst_defaults(void);
static void lin_defaults(void);
static void ins_defaults(void)
{
    static const uint8_t D[INS_N] = {INS_DEF0, INS_DEF1};
    uint32_t k;
    for (k = 0; k < NTRK; k++)
        ins_put(k, D);
    mst_defaults();
    lin_defaults();
}

/* ---- the master's two insert slots (fx-rework, 2026-10-14: "each track should have two inserts (including master ...)"):
 * after the returns and PUNCH, S1 then S2, before the master volume (fx.c mix_finish). Any insert type but those held by a
 * track instance count or a track's tempo (CHRS ECHO GATER: not on the master yet). The master's amounts: FILT's G_FILT (the
 * DJ filter), DUST's G_DUST, DYN's G_CTHR (its THRS; RATIO ATK REL GAIN CEIL its G_C*: GLO > COMP / LIMIT as before), each
 * other type's in mst_amt (the FX record's MST_TLV). A new project: S1 FILT (0: bypassed), S2 DYN (CEIL -3 dB); an old one
 * (no MST_TLV): the master effects it used, in their old order (fx_rec.c mst_map) */
#define MST_OK (FXT_BUILT & FXT_INSERT & ~FXT_BIT(FXT_GATE))   /* (fx-pool 2026-10-15: CHRS ECHO too, an instance of the pool;
                                                        * GATER reads a track's tempo values: the tracks' only) */
#define MST_DEF0 (TFLT_ON ? FXT_FILT : FELUCCA_MASTER_COMP ? FXT_DYN : FXT_NONE)   /* (fx_rec.c mst_map's order) */
#define MST_DEF1 (TFLT_ON && FELUCCA_MASTER_COMP ? FXT_DYN : FXT_NONE)
static uint8_t mst_slot[INS_N] = {MST_DEF0, MST_DEF1};
static uint32_t mst_live = (FXT_BIT(MST_DEF0) | FXT_BIT(MST_DEF1)) & MST_OK;
#define MST_ON(x) (mst_live >> (x) & 1u)
static int16_t mst_amt[FXT_N] = {[FXT_EQ3] = 1};       /* the master's amount of each type with no G_* of its own (EQ3's
                                                        * ON: 1) */
/* the master's amount of type t: its value (0: none) and its descriptor */
static int16_t *mst_amt_p(uint32_t t)
{
    return t == FXT_FILT ? &song.g[G_FILT] : t == FXT_DUST ? &song.g[G_DUST] : t == FXT_DYN ? &song.g[G_CTHR] :
           &mst_amt[t < FXT_N ? t : 0u];
}
static const param_desc_t *mst_amt_d(uint32_t t)
{
    return t == FXT_FILT ? &GP[G_FILT] : t == FXT_DUST ? &GP[G_DUST] : t == FXT_DYN ? &GP[G_CTHR] :
           t < FXT_N && FXT_AMT[t] != 0xFFu ? &TP[FXT_AMT[t]] : 0;
}
static void mst_put(const uint8_t *s)                  /* the master's slots from s, as ins_put */
{
    uint32_t a = s[0] < FXT_N && (FXT_INSERT >> s[0] & 1u) ? s[0] : (uint32_t)FXT_NONE;
    uint32_t b = s[1] < FXT_N && (FXT_INSERT >> s[1] & 1u) && s[1] != a ? s[1] : (uint32_t)FXT_NONE;
    IRQ_SAVE(ie);
    mst_slot[0] = (uint8_t)a, mst_slot[1] = (uint8_t)b;
    mst_live = (FXT_BIT(a) | FXT_BIT(b)) & MST_OK;
    IRQ_RESTORE(ie);
    FXI_ASK(1u);                                       /* (JUNO CHORUS: the master's instance, fxi.c) */
}
static void mst_load(uint32_t s, uint32_t t)           /* type t into the master's slot s (the other's type swaps them) */
{
    uint8_t v[INS_N];
    s &= 1u;
    v[0] = mst_slot[0], v[1] = mst_slot[1];
    if (t != FXT_NONE && v[s ^ 1u] == t)
        v[s ^ 1u] = v[s];
    v[s] = (uint8_t)t;
    mst_put(v);
}
static void mst_defaults(void)
{
    static const uint8_t D[INS_N] = {MST_DEF0, MST_DEF1};
    mst_put(D);
}

#if FELUCCA_USB_AUDIO
/* ---- the USB input as a track (fx-rework stage 3, 2026-10-15: "audio usb in ... would appear as a track after the drum
 * tracks"): the computer's audio (the playback ring, usb_audio_stream.c ua_take) mixed as a stereo track, its LEVEL PAN
 * MUTE, FX on, its two insert slots on its L and R, its sends REV DLY CHO from its mid. Unity, centre, no insert, no send by
 * default: the computer's audio as it was mixed before (into the output, now into the mix bus before the master's inserts).
 * Kept in the FX record (fx_rec.c UT_TLV; EQ3's bands in EQ3's own TLV, owner USB), a load's. The mixer's USB row
 * (op_mixer.c) edits it.
 * Its slots take every insert type a track's do (2026-10-15, the user: "any FX insert on the USB IN track, like any other
 * track"): the pooled ones (CHRS ECHO JUNO FLNG) an instance of the pool (fxi.c owner FXI_UT; none free: FULL, bypassed),
 * EQ3 its own bands (eq3_v's USB owner), JUNO CHORUS its own MODE, GATER its own PATTERN RATE SHAPE (the input has no
 * track values: these three are the input's, the phase the transport's clock at the song tempo, as a track's; the swing
 * the global SWING on RATE 1/16, the song's step) */
#define UT_OK (FXT_BUILT & FXT_INSERT)
#define UT_LVL_DEF 112                                  /* (LEVEL_Q12[112] = 4096: unity) */
enum { UTV_LVL, UTV_PAN, UTV_MUTE, UTV_FXON, UTV_REV, UTV_DLY, UTV_CHO,
       UTV_GPAT, UTV_GRATE, UTV_GSHP,                   /* GATER's PATTERN RATE SHAPE (its DEPTH: the slot's amount) */
       UTV_JMODE,                                       /* JUNO CHORUS's MODE (its MIX: the slot's amount) */
       UTV_N };
#define UTV_N0 (UTV_CHO + 1u)                           /* (the values UT_TLV had first: its layout keeps them there) */
#define UT_ID_TY 16u                                    /* ut_desc ids: 0 .. UTV_N - 1 the values, 16 17 the TYPEs, 18 19 the */
#define UT_ID_AMT 18u                                   /* amounts */
static struct {
    int16_t v[UTV_N];                                   /* LEVEL PAN MUTE FX ON, the sends REV DLY CHO, GATER's, JUNO's */
    uint8_t slot[INS_N];                                /* its insert slots' types */
    int16_t amt[INS_N];                                 /* their amounts (the slot's: a type change resets it) */
} ut = {{UT_LVL_DEF, 0, 0, 1, 0, 0, 0, 8, 1, 0, 0}, {FXT_NONE, FXT_NONE}, {0, 0}};
static const param_desc_t UT_DESC[UTV_N] = {
    PD("VOLUME", F_DB, 0, 127, UT_LVL_DEF), PD("PAN", F_BIPCT, -64, 63, 0), PE("MUTE", N_ONOFF, 0), PE("FX ON", N_ONOFF, 1),
    PD("REV", F_PCT, 0, 127, 0), PD("DLY", F_PCT, 0, 127, 0), PD("CHO", F_PCT, 0, 127, 0),
    PE("PAT", N_TGPAT, 8), PE("RATE", N_SLDIV, 1), PD("SHAPE", F_PCT, 0, 127, 0),   /* (as a track's GATER: params.c TP) */
    PE("MODE", N_TJMODE, 0)};                           /* (as a track's JUNO CHORUS) */
#define ut_vd(i) (&UT_DESC[(i) % UTV_N])                /* value i's descriptor */
static void ut_defaults(void)
{
    uint32_t i;
    for (i = 0; i < UTV_N; i++)
        ut.v[i] = ut_vd(i)->def;
    ut.slot[0] = ut.slot[1] = FXT_NONE;
    ut.amt[0] = ut.amt[1] = 0;
}
static int ut_dflt(void)
{
    uint32_t i;
    for (i = 0; i < UTV_N; i++)
        if (ut.v[i] != ut_vd(i)->def)
            return 0;
    return ut.slot[0] == FXT_NONE && ut.slot[1] == FXT_NONE;
}
/* slot s's TYPE (an index into ut_list, as the tracks') and its amount (the type's track amount descriptor) */
static const char *ut_names[FXT_N];
static uint8_t ut_list[FXT_N];
static int16_t ut_tv[INS_N];
static param_desc_t ut_td[INS_N] = {{"S1", F_ENUM, 0, 0, 0, ut_names, 0}, {"S2", F_ENUM, 0, 0, 0, ut_names, 0}};
static const param_desc_t *ut_desc(uint32_t id, int16_t **vp)   /* (ids: UT_ID_TY above) */
{
    uint32_t t, n = 0, s = id & 1u;
    if (id < UTV_N) {
        *vp = &ut.v[id];
        return ut_vd(id);
    }
    if (id >= UT_ID_AMT) {
        t = ut.slot[s];
        if (!(t < FXT_N && (UT_OK >> t & 1u)) || FXT_AMT[t] == 0xFFu)
            return 0;                                  /* (an empty slot: none) */
        *vp = &ut.amt[s];
        return &TP[FXT_AMT[t]];
    }
    if (id < UT_ID_TY)
        return 0;
    ut_tv[s] = 0;
    for (t = 0; t < FXT_N; t++)
        if (t == FXT_NONE || UT_OK >> t & 1u) {
            ut_names[n] = t == FXT_NONE ? "----" : TP[FXT_AMT[t]].label;
            if (ut.slot[s] == t)
                ut_tv[s] = (int16_t)n;
            ut_list[n++] = (uint8_t)t;
        }
    ut_td[s].max = (int16_t)(n - 1u);
    *vp = &ut_tv[s];
    return &ut_td[s];
}
static uint32_t ut_has(uint32_t t)                      /* the input's slot holding type t: 1 S1, 2 S2, 0 none (or not built) */
{
    return !(t < FXT_N && (UT_OK >> t & 1u)) ? 0u : ut.slot[0] == t ? 1u : ut.slot[1] == t ? 2u : 0u;
}
static void ut_load(uint32_t s, uint32_t t)            /* type t into slot s (the other's type swaps them, amounts too) */
{
    uint8_t w[INS_N];
    s &= 1u;
    if (t != FXT_NONE && !(UT_OK >> t & 1u))
        return;
    w[0] = ut.slot[0], w[1] = ut.slot[1];
    if (t == w[s])
        return;
    IRQ_SAVE(ie);
    if (t != FXT_NONE && w[s ^ 1u] == t) {
        int16_t a = ut.amt[0];
        ut.amt[0] = ut.amt[1], ut.amt[1] = a;
        ut.slot[s ^ 1u] = w[s];
    } else
        ut.amt[s] = t == FXT_EQ3 ? 1 : 0;                /* (EQ3: ON; another type from 0) */
    ut.slot[s] = (uint8_t)t;
    IRQ_RESTORE(ie);
    FXI_ASK(1u);                                       /* (a pooled type: the input's instance, fxi.c) */
}
#endif

/* ---- each drum sound's two insert slots (fx-rework 2026-10-14: "each track should have two inserts (including ...
 * drum sub tracks)"; were a fixed DST and CMP, D7). Run per drum voice (fx.c dins_post), S1 then S2, on the voice's
 * block. The types a voice can hold: those whose state is a few words (LIN_T; a lane has no stereo, no line, no
 * tempo): DIST, DYN (its compressor, the drum track's RATIO ATK REL, no CEIL), FILT, DUST, FOLD, FUZZ, PHSR, FRMT. The
 * amounts are the slot's (drum_sends.c dins_amt[s][l]; a type change resets it: the ranges differ), stored in the FX
 * record as before (DIST's and COMP's TLVs: S1's, S2's), the slots in LIN_TLV (fx_rec.c) only when not the default
 * S1 DIST, S2 DYN: every project of before plays as it did */
static const uint8_t LIN_T[] = {FXT_NONE, FXT_DIST, FXT_DYN, FXT_FILT, FXT_DUST, FXT_FOLD, FXT_FUZZ, FXT_PHSR, FXT_FRMT,
                                FXT_EQ3};   /* (EQ3, fx-pool: the drum track's bands, the lane's amount its on switch) */   /* (the
                                                        * stored index: never reorder) */
#define LIN_NT ((uint32_t)sizeof LIN_T)
#define LIN_OK (FXT_BUILT & (FXT_BIT(FXT_DIST) | FXT_BIT(FXT_DYN) | FXT_BIT(FXT_FILT) | FXT_BIT(FXT_DUST) | FXT_BIT(FXT_FOLD) | \
                             FXT_BIT(FXT_FUZZ) | FXT_BIT(FXT_PHSR) | FXT_BIT(FXT_FRMT) | FXT_BIT(FXT_EQ3)))
#define LIN_DEF0 (FELUCCA_FX_DIST ? FXT_DIST : FXT_NONE)
#define LIN_DEF1 (FELUCCA_MASTER_COMP ? FXT_DYN : FXT_NONE)
#define LIN_D2 {LIN_DEF0, LIN_DEF1}
#define LIN_D8 LIN_D2, LIN_D2, LIN_D2, LIN_D2, LIN_D2, LIN_D2, LIN_D2, LIN_D2
_Static_assert(DRUM_LANES == 10, "lin_slot's initialiser: 10 lanes");
static uint8_t lin_slot[DRUM_LANES][INS_N] = {LIN_D8, LIN_D2, LIN_D2};
static void lin_put(uint32_t l, const uint8_t *s)      /* lane l's slots (a type not of LIN_T: empty; twice: S2 empty) */
{
    uint32_t a = s[0] < FXT_N && (LIN_OK >> s[0] & 1u) ? s[0] : (uint32_t)FXT_NONE;
    uint32_t b = s[1] < FXT_N && (LIN_OK >> s[1] & 1u) && s[1] != a ? s[1] : (uint32_t)FXT_NONE;
    IRQ_SAVE(ie);
    lin_slot[l % DRUM_LANES][0] = (uint8_t)a, lin_slot[l % DRUM_LANES][1] = (uint8_t)b;
    IRQ_RESTORE(ie);
}
static void lin_defaults(void)
{
    static const uint8_t D[INS_N] = {LIN_DEF0, LIN_DEF1};
    uint32_t l;
    for (l = 0; l < DRUM_LANES; l++)
        lin_put(l, D);
}
static int lin_has(uint32_t t)                         /* a lane's slot holds type t */
{
    uint32_t l;
    for (l = 0; l < DRUM_LANES; l++)
        if (lin_slot[l][0] == t || lin_slot[l][1] == t)
            return 1;
    return 0;
}
static int lin_dflt(void)                              /* every lane's slots the default */
{
    uint32_t l;
    for (l = 0; l < DRUM_LANES; l++)
        if (lin_slot[l][0] != LIN_DEF0 || lin_slot[l][1] != LIN_DEF1)
            return 0;
    return 1;
}
/* the amount of lane l's slot s as the audio reads it (the FILTER's bipolar) */
AINL int32_t lin_amt(uint32_t l, uint32_t s)
{
    int32_t v = dins_amt[s & 1u][l % DRUM_LANES];
    return lin_slot[l % DRUM_LANES][s & 1u] == FXT_FILT ? (int32_t)(int8_t)v : v > 127 ? 127 : v;   /* (a record's byte: raw) */
}
static int16_t lin_v[4];
static const char *lin_names[LIN_NT];
static uint8_t lin_list[LIN_NT];
static param_desc_t lin_td[INS_N] = {{"S1", F_ENUM, 0, 0, 0, lin_names, 0}, {"S2", F_ENUM, 0, 0, 0, lin_names, 0}};
/* lane l's value id: 0, 1 slot S1's / S2's amount (its type's track amount descriptor; empty: none), 2, 3 their TYPE */
static const param_desc_t *lin_desc(uint32_t l, uint32_t id, int16_t **vp)
{
    uint32_t s = id & 1u, t = lin_slot[l % DRUM_LANES][s], i, n = 0;
    if (id < 2u) {
        if (!(t < FXT_N && (LIN_OK >> t & 1u)) || FXT_AMT[t] == 0xFFu)
            return 0;
        lin_v[s] = (int16_t)lin_amt(l, s);
        *vp = &lin_v[s];
        return &TP[FXT_AMT[t]];
    }
    lin_v[2u + s] = 0;
    for (i = 0; i < LIN_NT; i++)
        if (LIN_T[i] == FXT_NONE || LIN_OK >> LIN_T[i] & 1u) {
            lin_names[n] = LIN_T[i] == FXT_NONE ? "----" : TP[FXT_AMT[LIN_T[i]]].label;
            if (LIN_T[i] == t)
                lin_v[2u + s] = (int16_t)n;
            lin_list[n++] = LIN_T[i];
        }
    lin_td[s].max = (int16_t)(n - 1u);
    *vp = &lin_v[2u + s];
    return &lin_td[s];
}
/* lane l's value id set to v (a TYPE: a list index; the other slot's type swaps the two, their amounts with them) */
static void lin_set(uint32_t l, uint32_t id, int32_t v)
{
    uint32_t s = id & 1u, t;
    uint8_t w[INS_N];
    l %= DRUM_LANES;
    if (id < 2u) {
        t = lin_slot[l][s];
        if (t < FXT_N && FXT_AMT[t] != 0xFFu)
            dins_amt[s][l] = (uint8_t)(int8_t)clamp(v, TP[FXT_AMT[t]].min, TP[FXT_AMT[t]].max);
        return;
    }
    {
        int16_t *vp;
        (void)lin_desc(l, id, &vp);                    /* (the list as shown: the types built) */
    }
    t = lin_list[(uint32_t)v % LIN_NT];
    w[0] = lin_slot[l][0], w[1] = lin_slot[l][1];
    if (t == w[s])
        return;
    if (t != FXT_NONE && w[s ^ 1u] == t) {             /* (swapped with its amount) */
        uint8_t a = dins_amt[0][l];
        dins_amt[0][l] = dins_amt[1][l], dins_amt[1][l] = a;
        w[s ^ 1u] = w[s];
    } else
        dins_amt[s][l] = t == FXT_EQ3 ? 1u : 0u;       /* (another type: from 0; EQ3 ON) */
    w[s] = (uint8_t)t;
    lin_put(l, w);
}

/* ---- what the FX record keeps beside the layout: per type, a TLV (fx_rec.c). The drum track's amounts (the drum bus:
 * fx.c dbus_run, design phase 4) are its P_DIST P_CHOR P_DLY P_REV P_TCOMP, kept here only (project_t's drum slots are
 * stored 0: project.c proj_capture; a project from before reads 0 there whatever its slots held)
 *   COMP   the drum bus's amount, the parts', a RATIO ATK REL (before the per-track settings, every track's: now T1's,
 *          for an older reader; the tracks' own are in the track slots' TLV, fx_rec.c), then the 16 drum sounds'
 *   DIST   the drum bus's amount (the parts' are P_DIST in project_t), then the 16 drum sounds' (as COMP's)
 *   CHO DLY REV   the drum bus's send */
#if FELUCCA_MASTER_COMP
_Static_assert(TRK_DRUM == NPART && NTRK == NPART + 1, "the COMP TLV: the drum bus, then the parts 0 .. NPART - 1");
/* each track's DYN RATIO ATK REL (D6; GP's defaults: params.c G_CRAT G_CATK G_CREL), then (fx-rework, 2026-10-14) its
 * GAIN (0 AUTO: half the static reduction at full scale, the COMP insert of before; 1..16: 0..15 dB) and CEIL (the
 * limiter after it, N_CCEIL: 0 OFF, the default and every project of before); the drum sounds' the drum track's */
#define TDYN_N 5u
static int16_t ins_cset[NTRK][TDYN_N] = {{1, 4, 6, 0, 0}, {1, 4, 6, 0, 0}, {1, 4, 6, 0, 0}, {1, 4, 6, 0, 0}};
static const char *const N_TDGAIN[] = {"AUTO", "0dB", "1dB", "2dB", "3dB", "4dB", "5dB", "6dB", "7dB", "8dB", "9dB",
                                       "10dB", "11dB", "12dB", "13dB", "14dB", "15dB"};
/* setting i's descriptor (RATIO ATK REL: the master's lists; GAIN CEIL: the track's, CEIL OFF by default) */
static const param_desc_t TDYN_GC[2] = {PE("GAIN", N_TDGAIN, 0), PE("CEIL", N_CCEIL, 0)};
static const param_desc_t *tdyn_d(uint32_t i) { return i < 3u ? &GP[G_CRAT + i] : &TDYN_GC[(i - 3u) & 1u]; }
#endif
/* the drum bus's amount of type t: its id in the drum track's values (0xFF: none) */
static const uint8_t FXT_BUS[FXT_N] = {0xFF, P_DIST, P_CHOR, P_DLY, P_REV,
#if FELUCCA_MASTER_COMP
                                       P_TCOMP,
#else
                                       0xFF,
#endif
                                       0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};   /* (FILT; the further inserts, DUST, JUNO: their
                                                        * own TLV, every track's amount: fxs_tlv) */
/* JUNO CHORUS's TLV (2026-10-14): T1 T2 T3 DR's MIX, their MODE, the master's MIX and MODE (mst_amt, mst_jmode), when any
 * is set; a shorter one: the rest 0 */
#define FXT_TLV_JUNO (2u * NTRK + 2u)                  /* (in FXT_TLV_SUM: fx_rec.c's FXR_MAX check) */
#if FELUCCA_INS_JCHORUS
static int16_t mst_jmode;                              /* the master's JUNO CHORUS MODE (its MIX: mst_amt[FXT_JUNO]) */
#endif
/* GATER's TLV (2026-10-14): its four amounts, then each track's PATTERN, RATE, SHAPE (P_TGPAT .. P_TGSHP: 4 bytes each,
 * T1 T2 T3 DR) when any is off its default; a TLV of 4 (or a reader of before, which stops at the amounts): the defaults */
#define FXT_TLV_GX (3u * NTRK)
#if INS_FX
_Static_assert(P_TGPAT == P_TGATE + 1 && P_TGSHP == P_TGATE + 3, "GATER's values: after its amount");
static void fxs_gx_def(uint32_t k)                     /* track k's GATER values the defaults */
{
    uint32_t i;
    for (i = 1; i <= 3u; i++)
        trk[k % NTRK].p[P_TGATE + i] = TP[P_TGATE + i].def;
}
#endif
/* type t's TLV payload -> o, its length; 0: nothing to keep. FXT_TLV_SUM: every type's longest, summed (COMP: the
 * drum bus, 3 parts, 3 settings, 16 drum sounds; DIST: the bus, 16 sounds; CHO DLY REV: the bus). The drum sounds'
 * amounts (phase 5: drum_sends.c dins_amt) come last and only when one is set: a reader of before takes the rest */
#define FXT_TLV_EQ3 (NTRK + 1u + 5u * (NTRK + 5u))     /* EQ3's: the tracks' ON, the master's, every owner's 5 settings */
#define FXT_TLV_SUM (23u + 17u + 3u + (FXT_NEWN - FXT_NEW0 + 1u) * NTRK + FXT_TLV_GX + FXT_TLV_JUNO + FXT_TLV_EQ3)   /* (+ the further inserts' and
                                                        * DUST's: 4 each) */
#define FXT_TLV_COMP (NTRK + 3u)                       /* (COMP's TLV before the drum sounds' amounts) */
static uint32_t fxs_lanes(const uint8_t *a, uint8_t *o)   /* the 16 drum sounds' amounts -> o; any set */
{
    uint32_t k, any = 0;
    for (k = 0; k < DRUM_LANES; k++)
        any |= (o[k] = a[k]);
    return any;
}
static uint32_t fxs_tlv(uint32_t t, uint8_t *o)
{
    uint32_t k, any = 0;
#if FELUCCA_INS_EQ3
    if (t == FXT_EQ3) {                                /* EQ3 (fx-pool): T1 T2 T3 DR's ON, the master's, then every owner's
                                                        * LO MID HI FREQ L FREQ H (eq3_v), when any is off its default */
        for (k = 0; k < NTRK; k++)
            any |= (o[k] = (uint8_t)trk[k].p[P_TEQ]) != 1u;
        any |= (o[NTRK] = (uint8_t)mst_amt[FXT_EQ3]) != 1u;
        for (k = 0; k < EQ_NO * EQV_N; k++)
            o[NTRK + 1u + k] = (uint8_t)eq3_v[k / EQV_N][k % EQV_N];
        return any || !eq3_dflt() ? FXT_TLV_EQ3 : 0u;
    }
#endif
#if FELUCCA_FX_DUST
    if (t == FXT_DUST) {                               /* DUST (2026-10-14): T1 T2 T3 DR's amounts, any set */
        for (k = 0; k < NTRK; k++)
            any |= (o[k] = (uint8_t)trk[k].p[P_TDUST]);
        return any ? NTRK : 0u;
    }
#endif
#if FELUCCA_INS_JCHORUS
    if (t == FXT_JUNO) {                               /* JUNO CHORUS (above) */
        for (k = 0; k < NTRK; k++) {
            any |= (o[k] = (uint8_t)trk[k].p[P_TJCHO]);
            any |= (o[NTRK + k] = (uint8_t)trk[k].p[P_TJMODE]);
        }
        any |= (o[2u * NTRK] = (uint8_t)mst_amt[FXT_JUNO]);
        any |= (o[2u * NTRK + 1u] = (uint8_t)mst_jmode);
        return any ? FXT_TLV_JUNO : 0u;
    }
#endif
#if INS_FX
    if (t >= FXT_NEW0 && t < FXT_NEWN) {                  /* a further insert (insfx.c): T1 T2 T3 DR's amounts, any set */
        if (FXT_AMT[t] == 0xFFu)
            return 0;                                  /* (not built: its amounts are not here to keep) */
        for (k = 0; k < NTRK; k++)
            any |= (o[k] = (uint8_t)trk[k].p[FXT_AMT[t]]);
        if (t == FXT_GATE) {                           /* (its PATTERN RATE SHAPE: only when one is off its default) */
            uint32_t gx = 0;
            for (k = 0; k < FXT_TLV_GX; k++) {
                uint32_t id = P_TGPAT + k / NTRK;
                o[NTRK + k] = (uint8_t)trk[k % NTRK].p[id];
                gx |= trk[k % NTRK].p[id] != TP[id].def;
            }
            return gx ? NTRK + FXT_TLV_GX : any ? NTRK : 0u;
        }
        return any ? NTRK : 0u;
    }
#endif
    if (t >= FXT_N || FXT_BUS[t] == 0xFFu)
        return 0;
    any = (uint32_t)(o[0] = (uint8_t)TDRUM->p[FXT_BUS[t]]);   /* (the drum bus first: every type that has one) */
    if (t == FXT_DIST)
        return fxs_lanes(dins_amt[0], o + 1) ? 1u + DRUM_LANES : any ? 1u : 0u;
#if FELUCCA_MASTER_COMP
    if (t == FXT_COMP) {
        for (k = 1; k < NTRK; k++)
            any |= (uint32_t)(o[k] = (uint8_t)trk[k - 1u].p[P_TCOMP]);
        for (k = 0; k < 3u; k++)
            any |= (uint32_t)((o[NTRK + k] = (uint8_t)ins_cset[0][k]) != (uint8_t)GP[G_CRAT + k].def);
        return fxs_lanes(dins_amt[1], o + FXT_TLV_COMP) ? FXT_TLV_COMP + DRUM_LANES : any ? FXT_TLV_COMP : 0u;
    }
#endif
    (void)k;
    return any ? 1u : 0u;
}
/* the values the TLVs keep, as a project without them has them (all: the settings too) */
static void fxs_untlv_none(int all)
{
    uint32_t k;
    for (k = 0; k < FXT_N; k++)
        if (FXT_BUS[k] != 0xFFu)
            TDRUM->p[FXT_BUS[k]] = 0;
    memset(dins_amt, 0, sizeof dins_amt);
#if INS_FX
    for (k = 0; k < NTRK * (FXT_NEWN - FXT_NEW0); k++)
        trk[k % NTRK].p[P_TFOLD + k / NTRK] = 0;      /* (the further inserts' amounts: built or not) */
    for (k = 0; k < NTRK; k++)
        fxs_gx_def(k);                                 /* (GATER's PATTERN RATE SHAPE) */
#endif
#if FELUCCA_FX_DUST
    for (k = 0; k < NTRK; k++)
        trk[k].p[P_TDUST] = 0;
#endif
#if FELUCCA_INS_JCHORUS
    for (k = 0; k < NTRK; k++)
        trk[k].p[P_TJCHO] = trk[k].p[P_TJMODE] = 0;
    mst_amt[FXT_JUNO] = mst_jmode = 0;
#endif
#if FELUCCA_INS_EQ3
    for (k = 0; k < NTRK; k++)
        trk[k].p[P_TEQ] = 1;                           /* (ON: a band moved is heard) */
    mst_amt[FXT_EQ3] = 1;
    eq3_defaults();
#endif
#if FELUCCA_MASTER_COMP
    for (k = 0; k < NTRK; k++)
        trk[k].p[P_TCOMP] = 0;
    for (k = 0; all && k < TDYN_N * NTRK; k++)
        ins_cset[k / TDYN_N][k % TDYN_N] = tdyn_d(k % TDYN_N)->def;
#endif
    (void)all;
}
/* type t's TLV (n bytes at a) back; a type unknown here, or a shorter TLV's missing part: as none */
static void fxs_untlv(uint32_t t, const uint8_t *a, uint32_t n, int all)
{
    uint32_t k;
#if FELUCCA_FX_DUST
    if (t == FXT_DUST)
        for (k = 0; k < NTRK; k++)
            trk[k].p[P_TDUST] = (int16_t)(k < n ? (a[k] > 127u ? 127u : a[k]) : 0u);
#endif
#if INS_FX
    if (t >= FXT_NEW0 && t < FXT_NEWN && FXT_AMT[t] != 0xFFu)
        for (k = 0; k < NTRK; k++)                     /* (a shorter TLV: the rest 0) */
            trk[k].p[FXT_AMT[t]] = (int16_t)(k < n ? (a[k] > 127u ? 127u : a[k]) : 0u);
    if (t == FXT_GATE && FXT_AMT[t] != 0xFFu)
        for (k = 0; k < FXT_TLV_GX; k++) {             /* (a TLV of 4: the defaults) */
            uint32_t id = P_TGPAT + k / NTRK;
            trk[k % NTRK].p[id] = (int16_t)(n >= NTRK + FXT_TLV_GX ? clamp(a[NTRK + k], TP[id].min, TP[id].max) : TP[id].def);
        }
#endif
#if FELUCCA_INS_JCHORUS
    if (t == FXT_JUNO) {
        for (k = 0; k < NTRK; k++) {
            trk[k].p[P_TJCHO] = (int16_t)(k < n ? (a[k] > 127u ? 127u : a[k]) : 0u);
            trk[k].p[P_TJMODE] = (int16_t)(NTRK + k < n && a[NTRK + k] <= 2u ? a[NTRK + k] : 0u);
        }
        mst_amt[FXT_JUNO] = (int16_t)(2u * NTRK < n ? (a[2u * NTRK] > 127u ? 127u : a[2u * NTRK]) : 0u);
        mst_jmode = (int16_t)(2u * NTRK + 1u < n && a[2u * NTRK + 1u] <= 2u ? a[2u * NTRK + 1u] : 0u);
    }
#endif
#if FELUCCA_INS_EQ3
    if (t == FXT_EQ3) {                                /* (a shorter one: the rest the defaults, as none) */
        for (k = 0; k < NTRK; k++)
            trk[k].p[P_TEQ] = (int16_t)(k < n ? a[k] != 0u : 1u);
        mst_amt[FXT_EQ3] = (int16_t)(NTRK < n ? a[NTRK] != 0u : 1u);
        for (k = 0; k < EQ_NO * EQV_N && NTRK + 1u + k < n; k++)
            eq3_v[k / EQV_N][k % EQV_N] = (int16_t)clamp((int8_t)a[NTRK + 1u + k], EQ_D[k % EQV_N].min, EQ_D[k % EQV_N].max);
    }
#endif
    if (t < FXT_N && FXT_BUS[t] != 0xFFu && n >= 1u)
        TDRUM->p[FXT_BUS[t]] = (int16_t)(a[0] > 127u ? 127u : a[0]);
    if (t == FXT_DIST && n >= 1u + DRUM_LANES)
        for (k = 0; k < DRUM_LANES; k++)
            dins_amt[0][k] = a[1u + k];                /* (S1's amount: raw, lin_amt reads it by its type) */
#if FELUCCA_MASTER_COMP
    if (t == FXT_COMP && n >= FXT_TLV_COMP) {
        for (k = 1; k < NTRK; k++)
            trk[k - 1u].p[P_TCOMP] = (int16_t)(a[k] > 127u ? 127u : a[k]);
        for (k = 0; all && k < 3u * NTRK; k++)       /* (every track's: the track slots' TLV may follow) */
            ins_cset[k / 3u][k % 3u] = (int16_t)clamp(a[NTRK + k % 3u], GP[G_CRAT + k % 3u].min, GP[G_CRAT + k % 3u].max);
        for (k = 0; n >= FXT_TLV_COMP + DRUM_LANES && k < DRUM_LANES; k++)
            dins_amt[1][k] = a[FXT_TLV_COMP + k];      /* (S2's) */
    }
#endif
    (void)t, (void)a, (void)n, (void)all, (void)k;
}

/* FX > SLOTS: A B C, each an enum of ---- (empty), the sends built and (fx-pool) every other type a return may hold */
#define FXS_NLIST FXT_N
static const char *fxs_names[FXS_NLIST];
static uint8_t fxs_list[FXS_NLIST];
static int16_t fxs_v[FX_NSLOT];
static param_desc_t FXS_DESC[FX_NSLOT] = {   /* (slot 0: no return since fx-rework; A B C the master's returns) */
    {"S1", F_ENUM, 0, 0, 0, fxs_names, 0}, {"A", F_ENUM, 0, 0, 0, fxs_names, 0},
    {"B", F_ENUM, 0, 0, 0, fxs_names, 0}, {"C", F_ENUM, 0, 0, 0, fxs_names, 0}};
#define FXS_ID_RAMT 24u                                /* 24, 25, 26: A B C's amount (a return holding another type) */
static int rt_any(void)                                /* a return holds a type that is not a send */
{
    return fxs_rty[0] != FXT_NONE || fxs_rty[1] != FXT_NONE || fxs_rty[2] != FXT_NONE;
}
/* INSERTS' S1 and S2 TYPE (the selected track's): an enum of the inserts built and ---- (empty) */
#define INS_NLIST (1 + FELUCCA_FX_DIST + TFLT_ON + FELUCCA_MASTER_COMP + FELUCCA_FX_DUST + FELUCCA_INS_FOLD + FELUCCA_INS_FUZZ + \
                   FELUCCA_INS_PHASER + (FELUCCA_INS_FLANGER > 0) + FELUCCA_INS_FORMANT + FELUCCA_INS_WIDTH + \
                   (FELUCCA_INS_CHORUS > 0) + (FELUCCA_INS_DELAY > 0) + FELUCCA_INS_GATER + (FELUCCA_INS_JCHORUS > 0) + \
                   FELUCCA_INS_EQ3)
static const char *ins_names[INS_NLIST];
static uint8_t ins_list[INS_NLIST];
static int16_t ins_v[INS_N];
static const param_desc_t INS_DESC[INS_N] = {{"S1", F_ENUM, 0, INS_NLIST - 1, 0, ins_names, 0},
                                             {"S2", F_ENUM, 0, INS_NLIST - 1, 0, ins_names, 0}};
#define FXS_ID_CMP FX_NSLOT                            /* fxs_desc ids: 0..3 the master slots, 4..8 DYN RATIO ATK REL GAIN CEIL */
#define FXS_ID_TAMT 9u                                 /* 9: the selected track's DYN amount (P_TCOMP), on DYN's page */
#define FXS_ID_INS 16u                                 /* 16, 17: the selected track's S1, S2 TYPE (INSERTS, the mixer) */
#define FXS_ID_MST 18u                                 /* 18, 19: the master's S1, S2 TYPE; 20, 21: their amounts */
#define FXS_ID_JMST 22u                                /* 22, 23: the master's JUNO CHORUS MIX and MODE (MST JUNO) */
#define FXS_ID_TEQ 10u                                 /* 10..15: the selected track's EQ3: LO MID HI, ON, FREQ L, FREQ H
                                                        * (the track's FX pages: CMP_PAGE's range) */
#define FXS_ID_MEQ 32u                                 /* 32..37: the master's EQ3: LO MID HI, ON, FREQ L, FREQ H */
#define FXS_ID_REQ 40u                                 /* 40..45: the EQ3 of the return holding it: LO MID HI, ON, FREQ L, FREQ H */
#define FXS_ID_UEQ 48u                                 /* 48..53: the USB input's EQ3: LO MID HI, ON, FREQ L, FREQ H (the mixer's
                                                        * USB row: USB EQ3, USB EQ3 F) */
#define FXS_IS_EQ3(k) (((k) >= FXS_ID_TEQ && (k) < FXS_ID_TEQ + 6u) || ((k) >= FXS_ID_MEQ && (k) < FXS_ID_MEQ + 6u) || \
                       ((k) >= FXS_ID_REQ && (k) < FXS_ID_REQ + 6u) || ((k) >= FXS_ID_UEQ && (k) < FXS_ID_UEQ + 6u))
#if FELUCCA_INS_EQ3
static int rt_eq3(void)                                /* the line whose return holds EQ3, 3: none */
{
    uint32_t j;
    for (j = 0; j < 3u && fxs_rty[j] != FXT_EQ3; j++)
        ;
    return (int)j;
}
/* an EQ3 page's cell id (FXS_ID_TEQ / MEQ / REQ + 0..5): its value and descriptor (0: none) */
static const param_desc_t *eq3_desc(uint32_t k, int16_t **vp)
{
    static const param_desc_t RON = PE("EQ3", N_ONOFF, 1);
    uint32_t base = k >= FXS_ID_UEQ ? FXS_ID_UEQ : k >= FXS_ID_REQ ? FXS_ID_REQ : k >= FXS_ID_MEQ ? FXS_ID_MEQ : FXS_ID_TEQ;
    uint32_t f = k - base, o;
    o = base == FXS_ID_TEQ ? song.sel % NTRK : base == FXS_ID_MEQ ? NTRK : base == FXS_ID_UEQ ? NTRK + 1u : NTRK + 2u + (uint32_t)rt_eq3();
#if FELUCCA_USB_AUDIO
    if (base == FXS_ID_UEQ && !ut_has(FXT_EQ3))
        o = EQ_NO;                                     /* (no slot of the input holds it: none) */
#else
    if (base == FXS_ID_UEQ)
        o = EQ_NO;
#endif
    if (f > 5u || o >= EQ_NO) {
        *vp = 0;
        return 0;
    }
    if (f == 3u) {                                     /* ON: the owner's amount */
        if (base == FXS_ID_TEQ) {
            *vp = &trk[o].p[P_TEQ];
            return &TP[P_TEQ];
        }
#if FELUCCA_USB_AUDIO
        if (base == FXS_ID_UEQ) {
            *vp = &ut.amt[ut_has(FXT_EQ3) - 1u];       /* (the slot holding it: its amount is its ON) */
            return &RON;
        }
#endif
        *vp = base == FXS_ID_MEQ ? &mst_amt[FXT_EQ3] : &fxs_ramt[fxs_rsl[o - NTRK - 2u]];
        return &RON;
    }
    f = f > 3u ? f - 1u : f;                            /* (4 5: FREQ L, FREQ H) */
    *vp = &eq3_v[o][f];
    return &EQ_D[f];
}
#endif
/* the master's slot s's TYPE as a list entry (the types MST_OK allows) */
#define MST_NLIST INS_NLIST
static const char *mst_names[MST_NLIST];
static uint8_t mst_list[MST_NLIST];
static int16_t mst_v[INS_N];
static param_desc_t mst_d[INS_N] = {{"S1", F_ENUM, 0, 0, 0, mst_names, 0}, {"S2", F_ENUM, 0, 0, 0, mst_names, 0}};
static const param_desc_t *mst_desc(uint32_t s, int16_t **vp)
{
    uint32_t t, n = 0;
    mst_v[s & 1u] = 0;                                 /* (a type this build lacks: shown empty, kept) */
    for (t = 0; t < FXT_N; t++)
        if (t == FXT_NONE || MST_OK >> t & 1u) {
            mst_names[n] = t == FXT_NONE ? "----" : t == FXT_DYN ? "DYN" : mst_amt_d(t)->label;   /* (DYN: by its name, not THRS) */
            if (mst_slot[s & 1u] == t)
                mst_v[s & 1u] = (int16_t)n;
            mst_list[n++] = (uint8_t)t;
        }
    mst_d[s & 1u].max = (int16_t)(n - 1u);
    *vp = &mst_v[s & 1u];
    return &mst_d[s & 1u];
}
/* track k's slot s's TYPE as a list entry (*vp: its index in ins_list) */
static const param_desc_t *ins_desc(uint32_t k, uint32_t s, int16_t **vp)
{
    uint32_t t, n = 0;
    ins_v[s & 1u] = 0;                                 /* (a type this build lacks: shown empty, kept) */
    for (t = 0; t < FXT_N; t++)
        if (t == FXT_NONE || (FXT_BUILT & FXT_INSERT) >> t & 1u) {
            ins_names[n] = t == FXT_NONE ? "----" : TP[FXT_AMT[t]].label;
            if (ins_slot[k % NTRK][s & 1u] == t)
                ins_v[s & 1u] = (int16_t)n;
            ins_list[n++] = (uint8_t)t;
        }
    *vp = &ins_v[s & 1u];
    return &INS_DESC[s & 1u];
}
static const param_desc_t *fxs_desc(uint32_t k, int16_t **vp)
{
    uint32_t t, n = 0, i;
#if FELUCCA_INS_EQ3
    if (FXS_IS_EQ3(k))
        return eq3_desc(k, vp);
#endif
    if (k >= FXS_ID_RAMT && k < FXS_ID_RAMT + 3u) {    /* a return's amount: its other type's (a send's: none) */
        uint32_t s = k - FXS_ID_RAMT + 1u;
        t = fxs_slot[s];
        if (!FXS_GEN(t) || FXT_AMT[t] == 0xFFu) {
            *vp = 0;
            return 0;
        }
        *vp = &fxs_ramt[s];
        return &TP[FXT_AMT[t]];
    }
#if FELUCCA_INS_JCHORUS
    if (k == FXS_ID_JMST || k == FXS_ID_JMST + 1u) {   /* MST JUNO: the master's JUNO CHORUS MIX, MODE */
        *vp = k == FXS_ID_JMST ? &mst_amt[FXT_JUNO] : &mst_jmode;
        return &TP[k == FXS_ID_JMST ? P_TJCHO : P_TJMODE];
    }
#endif
    if (k >= FXS_ID_MST + INS_N) {                     /* the master's S1 / S2 amount (none: an empty slot) */
        t = mst_slot[(k - FXS_ID_MST) & 1u];
        if (!(t < FXT_N && MST_ON(t) && mst_amt_d(t))) {
            *vp = 0;
            return 0;
        }
        *vp = mst_amt_p(t);
        return mst_amt_d(t);
    }
    if (k >= FXS_ID_MST)
        return mst_desc(k - FXS_ID_MST, vp);
    if (k >= FXS_ID_INS)
        return ins_desc(song.sel, k - FXS_ID_INS, vp);
#if FELUCCA_MASTER_COMP
    if (k == FXS_ID_TAMT) {
        *vp = &trk[song.sel % NTRK].p[P_TCOMP];
        return &TP[P_TCOMP];
    }
    if (k >= FX_NSLOT) {                               /* DYN's RATIO ATK REL GAIN CEIL, the selected track's */
        *vp = &ins_cset[song.sel % NTRK][(k - FX_NSLOT) % TDYN_N];
        return tdyn_d((k - FX_NSLOT) % TDYN_N);
    }
#endif
    fxs_v[k & 3u] = 0;                                 /* (a type this build lacks: shown empty, kept) */
    for (i = 0; i < 4u + FXT_N; i++) {                /* (the list from the table: each type's amount names it): ----
                                                        * and the sends first, in FXS_ORD, then the others */
        t = i == 0 ? FXT_NONE : i < 4u ? FXS_ORD[i - 1u] : i - 4u;
        if (i < 4u ? t == FXT_NONE || (FXT_BUILT >> t & 1u) : FXS_GEN(t)) {
            fxs_names[n] = t == FXT_NONE ? "----" : t == FXT_DYN ? "DYN" : TP[FXT_AMT[t]].label;
            if (fxs_slot[k & 3u] == t)
                fxs_v[k & 3u] = (int16_t)n;
            fxs_list[n++] = (uint8_t)t;
        }
    }
    FXS_DESC[k & 3u].max = (int16_t)(n - 1u);
    *vp = &fxs_v[k & 3u];
    return &FXS_DESC[k & 3u];
}
/* a TYPE cell of SC_FXSLOT (id) set to list entry v: the master's slots (MST INS); 1 done */
static int fxs_type_set(uint32_t id, int32_t v)
{
    if (id >= FXS_ID_MST && id < FXS_ID_MST + INS_N) {
        mst_load(id - FXS_ID_MST, mst_list[(uint32_t)v % MST_NLIST]);
        return 1;
    }
    return 0;
}
