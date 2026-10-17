/* SPDX-License-Identifier: GPL-3.0-only */
/* The mixer, horizontal (docs/UI-OPTIMIST-DESIGN.md section 4.1; the user, 2026-10-08: "Horizontal mixer is better"):
 * the rows are the tracks, the four knobs four values of the SELECTED row, shown on the cards (1x4: the mixer keeps them).
 *   rows       MASTER (above T1), T1 T2 T3, DR, then the drum track's 16 lanes; the panel shows the cursor's group
 *              only (the user, 2026-10-11): MASTER alone, the four channels, or the 16 drum sounds; each row its VU
 *              meter (the compressor's reduction pushing in from the right) and its sequence (op_mixdraw.c)
 *   ALGORITHM  walks the rows, MASTER, T1 .. DR .. the 16th lane and back (it stops at the ends; op_walk.c, the same
 *              walk as every page's): the track selected follows (song.sel), a lane row selects the lane (lane_sel, ui.lane)
 *   SELECT     the row's knob sets, its pages, in the same order on every row (the user, 2026-10-11): the mix
 *              (MUTE VOLUME PAN), the inserts (DRIVE or DIST, FILTER, COMP), the sends (A B C's: REV DLY CHO by
 *              default, fx_slots.c fxs_card) (no FX on, no SOUND card: 2026-10-14); a lane MUTE LEVEL CUT | DRIVE | its
 *              sends; DR MUTE VOLUME | FILTER; MASTER FILT DUST DUCK (those built) | THRS RATIO GAIN CEIL | the returns,
 *              A B C (REV DLY CHO RET by default), those holding something (batch D;
 *              2026-10-12 had a card a slot, "-" for an insert or an empty one). Each group on a set of its own. Values only: the screens are not sets.
 *              SELECT pages the sets (GLO tapped opens GLOBAL since 2026-10-12). The header names the set
 *   PRESETS    the row's sound: a track's preset, DR's kit, a lane's own sound (op_walk.c lane_sound_step)
 *   SAVE       PROJECT (always: the mixer has no YES, D6); MUTE is a knob, the SOUND row is EDIT's first
 * The inserts in the order the audio runs them (fx_slots.c: DIST, FILTER, COMP): DRIVE when DIST is in no slot (the
 * engine's drive), FILTER (P_TFLT), COMP only in a slot; the sends only in a slot, REV DLY CHO. The drum track's row:
 * its LEVEL (GLO > DRUMS), FILTER, FX on, the kit; its lanes carry the sounds' own values (SOUND 2 and 3: the offsets
 * and the sends). */
enum { MXR_MASTER, MXR_T1, MXR_DR = MXR_T1 + TRK_DRUM, MXR_LANE0, MXR_LN = MXR_LANE0 + DRUM_UILANES,
#if FELUCCA_USB_AUDIO
       MXR_USB = MXR_LN,                         /* the USB input, a track after the drum tracks (fx-rework stage 3; fx.c ut_mix) */
       MXR_N
#else
       MXR_N = MXR_LN
#endif
};
static int mx_is_lane(uint32_t r) { return r >= MXR_LANE0 && r < MXR_LN; }
#if FELUCCA_USB_AUDIO
static int mx_is_usb(uint32_t r) { return r == MXR_USB; }
#else
#define mx_is_usb(r) 0
#endif
#define MXR_ACC (MXR_LANE0 + DRUM_ACC)           /* the ACCENT lane's row, past the 16 sounds (core.h DRUM_UILANES) */
static int mx_is_acc(uint32_t r) { return ACC_UI && r == MXR_ACC; }
enum { MI_NONE, MI_TRK, MI_GLOB, MI_LANE, MI_LMUTE, MI_RET, MI_INST, MI_INSA, MI_DKS, MI_ACC, MI_AMUTE, MI_MINST, MI_MINSA,
       MI_UT, MI_UTST, MI_UTSA, MI_UEQ, MI_RSND };
/* MI_RSND: a send between the returns (fx_slots.c rs_amt's index: A -> B, A -> C, B -> C; MASTER's A SENDS, B SENDS) */
/* MI_UT: the USB input's value id (fx_slots.c ut_desc: VOLUME PAN MUTE FX ON REV DLY CHO, GATER's PAT RATE SHAPE, JUNO's
 * MODE), MI_UTST / MI_UTSA its slot id's TYPE / amount, MI_UEQ its EQ3's value id (fx_slots.c FXS_ID_UEQ + id) */
/* MI_MINST / MI_MINSA: MASTER's insert slot id's TYPE / amount (fx_slots.c mst_slot, fx-rework 2026-10-14) */
/* MI_LMUTE: a lane.s mute (the sequencer mute, mx_muted); MI_RET: MASTER's FX slot id's return; MI_INST / MI_INSA: the row's
 * insert slot id's TYPE / amount (fx_slots.c); MI_DKS: the drum track's send level id (drum_sends.c dsend_kit: 0 REV,
 * 1 DLY, 2 CHO); MI_ACC: the ACCENT amount (core.h P_DACC), the ACCENT lane's one card */
/* the knob sets' groups, in this order on every row (the user, 2026-10-11): the mix (VOLUME PAN; a lane LEVEL CUT;
 * MASTER its FILT DUST DUCK), the insert effects (DRIVE / DIST, FILTER, COMP; MASTER: its compressor), the sends (REV DLY
 * CHO), then the rest (FX on, the sound or the kit). A group starts a set of its own where the padding fits MX_MAXI */
enum { MG_MIX, MG_INS, MG_SEND, MG_LAST,
       MG_UEQ, MG_UEQF, MG_UGT, MG_UJC,                /* (the USB input's insert settings: a set each, named by mix_name) */
       MG_RSA, MG_RSB, MG_RSC };                       /* (MASTER: return A's sends, B's, C's (RET_FB): a set each) */              /* (the USB input's insert settings: a set each, named by mix_name) */
typedef struct {
    const char *label;                                  /* 0: the descriptor's */
    uint8_t kind, id;                                   /* MI_*; a P_* / G_* / lane value id / screen */
    uint8_t grp;                                        /* MG_* */
} mitem_t;
#define MX_MAXI 32u                                     /* (the USB row: 4 + 4, two types' settings, 4; MASTER: 4 + 4, the
                                                        * returns 4, A's B's (C's: RET_FB) sends 4 each, DUCK) */
static struct {
    uint8_t set;                                        /* the knob set shown (SELECT) */
    uint8_t lit[DRUM_LANES];                            /* frames a lane's hit stays lit (falling) */
} mx;

/* a list value's cell: its label, its text, the dots of 0..max at v (here: the first of the files that show one) */
static void cell_list(cell_t *c, const char *label, const char *text, int32_t max, int32_t v)
{
    c->label = label;
    str_cpy(c->val, text, sizeof c->val);
    cell_gauge(c, 1, 0, max, v);
}
static uint32_t mx_row(void) { return ui.row[SCR_HOME] % MXR_N; }
/* the mixer on its MASTER row: master has no instrument, so the track buttons (ENV LFO EDIT ARP SCL SEQ) are dark and
 * do nothing there, FX is the master FX (the user, 2026-10-12: "many buttons like env lfo are active while on master
 * and send to track 1. wrong") */
static int mx_on_master(void) { return ui.scr == SCR_HOME && mx_row() == MXR_MASTER; }
/* the mixer on its USB row: no instrument either (the track buttons dark, the keys silent), not the master */
static int mx_on_usb(void) { return ui.scr == SCR_HOME && mx_is_usb(mx_row()); }
/* MASTER selected, on any page (the user, 2026-10-12: "i want uniformity"): the mixer's row, the master FX screen, a
 * layer's ui.master. The track buttons are dark and do nothing there */
static int master_sel(void) { return mx_on_master() || ui.scr == SCR_FX || ui.master; }
#define MX_TRK_BTNS (1u << B_ENV | 1u << B_LFO | 1u << B_EDIT | 1u << B_ARP | 1u << B_SCL | 1u << B_SEQ)   /* the track buttons */
static uint32_t mx_dead_mask(void)                      /* the buttons that do nothing here: a bit per B_* */
{
    return (master_sel() ? MX_TRK_BTNS : 0u) | (mx_on_usb() ? MX_TRK_BTNS | 1u << B_FX : 0u);
}
static int mx_master_dead(uint32_t b) { return b < 32u && (mx_dead_mask() >> b & 1u); }
static uint32_t mx_trk_of(uint32_t r) { return r >= MXR_DR ? TRK_DRUM : r > MXR_MASTER ? r - MXR_T1 : 0u; }   /* (MASTER: T1, unused) */
/* row r muted. One mute (the user, 2026-10-16): a track's, a lane's and the ACCENT lane's is the sequencer mute (seq_mute.c:
 * its steps stop, the tails ring), SHIFT + its key or SHIFT + its volume card; the USB input's is its own (no sequencer).
 * A sound mute an older project or the SLOOP UI left (P_MUTE, the lane's own, drum_mix.c) counts too, so that it shows
 * and the toggle clears it: drawn struck through in the mixer, the header and the volume card */
static uint32_t mx_muted(uint32_t r)
{
    if (r == MXR_MASTER)
        return 0u;
#if FELUCCA_USB_AUDIO
    if (mx_is_usb(r))
        return ut.v[UTV_MUTE] != 0;
#endif
    if (mx_is_lane(r))
        return (uint32_t)(smute_lane_on(r - MXR_LANE0) || (!mx_is_acc(r) && dlm_muted(r - MXR_LANE0)));
    return (uint32_t)(smute_trk_on(mx_trk_of(r)) || trk[mx_trk_of(r)].p[P_MUTE] != 0);
}
/* row r's mute set (on) or cleared: the sequencer mute; a sound mute is cleared either way */
static void mx_mute_set(uint32_t r, int on)
{
#if FELUCCA_USB_AUDIO
    if (mx_is_usb(r)) {
        ut.v[UTV_MUTE] = (int16_t)(on != 0);
        return;
    }
#endif
    if (r == MXR_MASTER)
        return;
    if (mx_is_lane(r)) {
        smute_set_lane(r - MXR_LANE0, on);
        if (!mx_is_acc(r))
            dlm_set_mute(r - MXR_LANE0, 0);
        return;
    }
    smute_set_trk(mx_trk_of(r), on);
    trk[mx_trk_of(r)].p[P_MUTE] = 0;
}
static int mx_mute_flip(uint32_t r)                     /* row r's mute toggled; -> on */
{
    int on = !mx_muted(r);
    mx_mute_set(r, on);
    return on;
}
static uint32_t mx_row_of_smute(int32_t id)             /* a smute_key value's mixer row */
{
    return id == SMUTE_ACC ? MXR_LANE0 + DRUM_ACC : id >= SMUTE_T0 ? MXR_T1 + (uint32_t)(id - SMUTE_T0) : MXR_LANE0 + (uint32_t)id;
}
/* a drum lane selected, not DR itself (ui.lane): on the mixer its row says it (a lane row), elsewhere ui.lane while the
 * drum track is selected. PRESETS then steps the lane's sound, not the kit (op_walk.c) */
static int lane_picked(void)
{
    if (ui.scr == SCR_HOME)
        return mx_is_lane(ui.row[SCR_HOME] % MXR_N);
    return ui.lane && is_drum(TSEL);
}
static uint32_t mx_sel_row(void)                        /* the row of the selection (the track, DR or its lane) */
{
    return !is_drum(TSEL) ? MXR_T1 + song.sel : ui.lane ? MXR_LANE0 + lane_selected() : MXR_DR;   /* (lane 16: MXR_ACC) */
}
static uint16_t mx_col(uint32_t r)                      /* the row's colour: its engine's, its lane's source */
{
    return r == MXR_MASTER || mx_is_usb(r) ? C_HI : mx_is_lane(r) ? op_lane_col(r - MXR_LANE0) : trk_col(mx_trk_of(r));
}

static uint32_t mx_add(mitem_t *it, uint32_t n, uint32_t grp, const char *label, uint32_t kind, uint32_t id)
{
    if (n < MX_MAXI) {
        it[n].label = label;
        it[n].kind = (uint8_t)kind;
        it[n].id = (uint8_t)id;
        it[n].grp = (uint8_t)grp;
    }
    return n + 1u;
}
static uint32_t mx_adds(mitem_t *it, uint32_t n, const mitem_t *a, uint32_t m)   /* m items of a table, as mx_add each */
{
    for (; m; m--, a++, n++)
        if (n < MX_MAXI)
            it[n] = *a;
    return n;
}
/* the INS set (the track insert slots, 2026-10-12): S1 TYPE, S1's amount, S2 TYPE, S2's amount, as SOUND > INSERTS */
static const mitem_t MX_INS[4] = {{0, MI_INST, 0, MG_INS}, {0, MI_INSA, 0, MG_INS}, {0, MI_INST, 1, MG_INS}, {0, MI_INSA, 1, MG_INS}};
static uint32_t mx_items_ins(mitem_t *it, uint32_t n)
{
    if (!(FXT_BUILT & FXT_INSERT))
        return n;
    return mx_adds(it, n, MX_INS, 4);
}
/* a synth track: MUTE VOLUME PAN | its insert slots (S1 TYPE, amount, S2 TYPE, amount) | the sends, in the returns'
 * order */
/* the sends set, as the SENDS page has it (the user, 2026-10-14: "we have 3 sends, card 4 should be pan"): cards 1..3
 * returns A B C's sends (fx_slots.c fxs_card, a blank card for an empty return), PAN card 4. ids, lbl: the sends' ids
 * in kind's own numbering, REV DLY CHO (FXS_DSI) */
static uint32_t mx_items_sends(mitem_t *it, uint32_t n, const char *const *lbl, uint32_t kind, const uint8_t *ids,
                               uint32_t pan_kind, uint32_t pan_id)
{
    uint32_t k, t;
    for (k = 0; k < 3u; k++) {
        t = fxs_card(k);
        n = t != FXT_NONE ? mx_add(it, n, MG_SEND, lbl ? lbl[FXS_DSI(t)] : 0, kind, ids[FXS_DSI(t)]) :
                            mx_add(it, n, MG_SEND, 0, MI_NONE, 0);
    }
    return mx_add(it, n, MG_SEND, "PAN", pan_kind, pan_id);
}
static uint32_t mx_items_synth(mitem_t *it)
{
    static const uint8_t S[3] = {P_REV, P_DLY, P_CHOR};
    uint32_t n = 0;
    n = mx_add(it, n, MG_MIX, "VOLUME", MI_TRK, P_LEVEL);
    n = mx_add(it, n, MG_MIX, "PAN", MI_TRK, P_PAN);
    n = mx_items_ins(it, n);
    n = mx_items_sends(it, n, 0, MI_TRK, S, MI_TRK, P_PAN);
    return n;                                           /* (no FX ON and no SOUND card: the user, 2026-10-14; PRESETS is a button) */
}
/* MASTER's Returns (2026-10-12, the user: "on the mixer screen Master, we should have a volume control for the return
 * tracks"): a card a return, A B C (fx_slots.c fxs_card: the one order, as the master FX's RETURNS page, op_screens.c
 * FX_RET), "REV RET" ..; each turns its line's return level (core.h G_CRET G_DRET G_RRET, fx.c). An empty return has
 * no card (the audit's #7: no "-" card; was a card a slot, "S1 -"). No return holding anything: no set */
static uint32_t mx_items_ret(mitem_t *it, uint32_t n)
{
    uint32_t k, t;
    for (k = 0; k < 3u; k++)
        if ((t = fxs_card(k)) != FXT_NONE)
            n = mx_add(it, n, MG_SEND, fxs_ret_label(FXS_LN(t)), MI_RET, G_CRET + FXS_LN(t));
    return n;
}
/* MASTER's return sends (2026-10-16, fx_slots.c rs_amt): a set a return that may send, A SENDS then B SENDS, in a track's
 * sends grammar: cards 1..3 the returns A B C (the card of the return a send goes to, named by what it holds; blank: the
 * return itself, one to its left, an empty one), card 4 blank (a return has no PAN). C sends to none: no set. A return
 * holding nothing, or nothing to its right: no set. With RET_FB (the feedback sends) each return sends to the other two
 * (A: B C, B: A C, C: A B), C SENDS too; blank: the return itself, an empty one */
static uint32_t mx_items_rsend(mitem_t *it, uint32_t n)
{
    uint32_t s, k, i, any;
    int16_t *vp;
    for (s = 1; s < (FELUCCA_RET_FB ? 4u : 3u); s++) {   /* (RET_FB: C SENDS too, and B's to A, C's to A and B) */
        for (k = 0, any = 0; rs_of(s, k) < RS_N; k++)
            any |= rs_desc(rs_of(s, k), &vp) != 0;
        if (!any)
            continue;
        for (k = 0; k < 4u; k++) {
            for (i = 0; rs_of(s, i) < RS_N && RS_DST[rs_of(s, i)] != k + 1u; i++)
                ;
            n = rs_of(s, i) < RS_N ? mx_add(it, n, MG_RSA + s - 1u, 0, MI_RSND, rs_of(s, i)) :
                                     mx_add(it, n, MG_RSA + s - 1u, 0, MI_NONE, 0);
        }
    }
    return n;
}
#if FELUCCA_USB_AUDIO
/* the USB input's insert settings, while one of its slots holds the type (the input has no SOUND pages: they are its row's
 * sets, after its slots, as MASTER's DYN): EQ3's LO MID HI, then FREQ L FREQ H (USB EQ3, USB EQ3 F); GATER's PAT RATE
 * SHAPE (USB GATER); JUNO CHORUS's MODE (USB JUNO). Its amount, the slot's card (EQ3's ON, GATER's DEPTH, JUNO's MIX) */
static uint32_t mx_items_ut_x(mitem_t *it, uint32_t n)
{
    uint32_t i;
#if FELUCCA_INS_EQ3
    if (ut_has(FXT_EQ3)) {
        for (i = 0; i < 3u; i++)
            n = mx_add(it, n, MG_UEQ, 0, MI_UEQ, i);
        for (i = 4; i < 6u; i++)
            n = mx_add(it, n, MG_UEQF, 0, MI_UEQ, i);
    }
#endif
#if FELUCCA_INS_GATER
    if (ut_has(FXT_GATE))
        for (i = UTV_GPAT; i <= UTV_GSHP; i++)
            n = mx_add(it, n, MG_UGT, 0, MI_UT, i);
#endif
#if FELUCCA_INS_JCHORUS
    if (ut_has(FXT_JUNO))
        n = mx_add(it, n, MG_UJC, 0, MI_UT, UTV_JMODE);
#endif
    (void)i;
    return n;
}
#endif
static uint32_t mx_items_value(uint32_t r, mitem_t *it)   /* the row's value items, unpadded */
{
    uint32_t n = 0;
    if (r == MXR_MASTER) {                              /* its insert slots; DYN's; the returns; DUCK (the master FX screen's order) */
        static const mitem_t INS[4] = {{0, MI_MINST, 0, MG_INS}, {0, MI_MINSA, 0, MG_INS}, {0, MI_MINST, 1, MG_INS},
                                       {0, MI_MINSA, 1, MG_INS}};
        if (MST_OK)                                     /* (fx-rework 2026-10-14: FILT DUST DYN are insert types) */
            n = mx_adds(it, n, INS, 4);
#if FELUCCA_MASTER_COMP
        if (MST_ON(FXT_DYN)) {                          /* (DYN in a master slot: its compressor and limiter) */
            static const mitem_t DYN[4] = {{"THRS", MI_GLOB, G_CTHR, MG_INS}, {"RATIO", MI_GLOB, G_CRAT, MG_INS},
                                           {"GAIN", MI_GLOB, G_CGAIN, MG_INS}, {"CEIL", MI_GLOB, G_CCEIL, MG_INS}};
            n = mx_adds(it, n, DYN, 4);
        }
#endif
        n = mx_items_ret(it, n);
        n = mx_items_rsend(it, n);
        if (g_built(G_DUCK))
            n = mx_add(it, n, MG_LAST, "DUCK", MI_GLOB, G_DUCK);
        return n;
    }
    if (mx_is_acc(r)) {                                 /* the ACCENT lane: a lane's layout, a silent lane's cards: MUTE its sequencer
                                                         * mute (seq_mute.c SMUTE_ACC: no sound to mute), LEVEL the ACCENT amount,
                                                         * no CUT, DRIVE, DST, CMP, sends or sound */
        return mx_add(it, n, MG_MIX, "ACCENT", MI_ACC, 0);   /* (the amount, as the SOUND row's ACCENT card: a velocity, not dB) */
    }
#if FELUCCA_USB_AUDIO
    if (mx_is_usb(r)) {                                 /* the USB input: VOLUME PAN FX ON | its insert slots | its sends
                                                         * (MUTE: SHIFT2 on VOLUME, as every row) */
        static const mitem_t UT[7] = {{0, MI_UT, UTV_LVL, MG_MIX}, {0, MI_UT, UTV_PAN, MG_MIX}, {0, MI_UT, UTV_FXON, MG_MIX},
                                      {0, MI_UTST, 0, MG_INS}, {0, MI_UTSA, 0, MG_INS}, {0, MI_UTST, 1, MG_INS},
                                      {0, MI_UTSA, 1, MG_INS}};
        uint32_t k, t;
        n = mx_adds(it, n, UT, UT_OK ? 7u : 3u);
        n = mx_items_ut_x(it, n);
        for (k = 0; k < 3u; k++)                        /* (its sends in the returns' order: fx_slots.c fxs_card) */
            if ((t = fxs_card(k)) != FXT_NONE)
                n = mx_add(it, n, MG_SEND, 0, MI_UT, UTV_REV + FXS_DSI(t));
        return n;
    }
#endif
    if (r >= MXR_LANE0) {                               /* a lane: the sound's own values (SOUND 2's offsets, 3's sends) */
        static const mitem_t LN[7] = {{"LEVEL", MI_LANE, DE_LEVEL, MG_MIX}, {"CUT", MI_LANE, DE_CUT, MG_MIX},
                                      {"DRIVE", MI_LANE, DE_DRIVE, MG_MIX},   /* (its PAN is the sends set's card 4; DRIVE the
                                                         * sound's own drive: a SOUND value) */
                                      {0, MI_LANE, 21u, MG_INS}, {0, MI_LANE, 19u, MG_INS}, {0, MI_LANE, 22u, MG_INS},
                                      {0, MI_LANE, 20u, MG_INS}};   /* (its two insert slots, fx-rework: were DST CMP, as a track's) */
        n = mx_adds(it, n, LN, LIN_OK ? 7u : 3u);
        {
            static const uint8_t S[3] = {16u, 17u, 18u};
            static const char *const L[3] = {"REV", "DLY", "CHO"};
            return mx_items_sends(it, n, L, MI_LANE, S, MI_LANE, LPAN_VID);
        }
    }
    if (r == MXR_DR) {                                  /* the drum track: its level | its insert slots | its send levels
                                                         * (2026-10-12, the user: "it should be like any other track") |
                                                         */
        n = mx_add(it, n, MG_MIX, "VOLUME", MI_GLOB, G_DRLVL);
        n = mx_items_ins(it, n);
        {
            static const uint8_t S[3] = {0u, 1u, 2u};
            return mx_items_sends(it, n, 0, MI_DKS, S, MI_TRK, P_PAN);
        }
    }
    return mx_items_synth(it);
}
static uint32_t mx_pad(mitem_t *it, uint32_t n, uint32_t grp)   /* empty cells to the end of a set of four */
{
    while (n % 4u)
        n = mx_add(it, n, grp, 0, MI_NONE, 0);
    return n;
}
/* row r's items, in sets of four: its values only (the screens have their own gestures, op_input.c op_tap); a group
 * starts a new set when the empty cells that takes still leave room for the items after it */
static uint32_t mx_items(uint32_t r, mitem_t *it)
{
    mitem_t raw[MX_MAXI];
    uint32_t nr = mx_items_value(r, raw), i, n = 0;
    nr = nr < MX_MAXI ? nr : MX_MAXI;
    for (i = 0; i < nr; i++) {
        if (i && raw[i].grp != raw[i - 1u].grp && n % 4u && n + (4u - n % 4u) + (nr - i) <= MX_MAXI)
            n = mx_pad(it, n, raw[i - 1u].grp);
        if (n < MX_MAXI)
            it[n++] = raw[i];
    }
    n = mx_pad(it, n, nr ? raw[nr - 1u].grp : MG_MIX);
    return n < MX_MAXI ? n : MX_MAXI;
}
static uint32_t mx_sets(uint32_t r)
{
    mitem_t it[MX_MAXI];
    return mx_items(r, it) / 4u;
}
static uint32_t mx_set(uint32_t r)                      /* the set shown on row r (kept within its count) */
{
    uint32_t n = mx_sets(r);
    return mx.set < n ? mx.set : n - 1u;
}
static int mx_item(uint32_t r, uint32_t k, mitem_t *out)   /* row r's cell k in the set shown */
{
    mitem_t it[MX_MAXI];
    uint32_t n = mx_items(r, it), i = mx_set(r) * 4u + (k & 3u);
    if (i >= n)
        return 0;
    *out = it[i];
    return out->kind != MI_NONE;
}
static const param_desc_t MX_LMUTE = PE("MUTE", N_ONOFF, 0);
static int16_t mx_lmute_v;
/* an item's descriptor and value on row r (0: none) */
static const param_desc_t *mx_desc(uint32_t r, const mitem_t *m, int16_t **vp)
{
    const param_desc_t *d;
    *vp = 0;
    switch (m->kind) {
    case MI_TRK:
        *vp = &trk[mx_trk_of(r)].p[m->id];
        return m->id == P_CHOR || m->id == P_DLY || m->id == P_REV ? fxs_send_d(m->id) : &TP[m->id];   /* (a send: by its
                                                        * line's return, fx-pool) */
    case MI_GLOB:
        *vp = &song.g[m->id];
        return &GP[m->id];
    case MI_RET:                                        /* (a send's return: its G_* id) */
        *vp = &song.g[m->id];
        return &GP[m->id];
    case MI_RSND:                                       /* (a send between the returns: none, a blank card) */
        return rs_desc(m->id, vp);
    case MI_ACC:                                        /* the ACCENT amount: the drum track's own value */
        *vp = &TDRUM->p[P_DACC];
        return &DRUM_ACC_DESC;
    case MI_LANE:
        if (m->id == LPAN_VID)
            return lane_pan_desc(r - MXR_LANE0, vp);
        d = dsnd_desc_lane(r - MXR_LANE0, m->id, vp);
        if (d)
            dsv_rec(r - MXR_LANE0, m->id);              /* (recording: the hold the knob goes on from) */
        return d && !PARAM_HIDDEN(d) ? d : 0;
#if FELUCCA_USB_AUDIO
    case MI_UT:
        return ut_desc(m->id, vp);
    case MI_UTST:
        return ut_desc(UT_ID_TY + m->id, vp);
    case MI_UTSA:
        return ut_desc(UT_ID_AMT + m->id, vp);
#if FELUCCA_INS_EQ3
    case MI_UEQ:
        return fxs_desc(FXS_ID_UEQ + m->id, vp);
#endif
#endif
    case MI_MINST:                                      /* (an index into fx_slots.c mst_list) */
        return fxs_desc(FXS_ID_MST + m->id, vp);
    case MI_MINSA:                                      /* (an empty slot: "-") */
        return fxs_desc(FXS_ID_MST + INS_N + m->id, vp);
    case MI_INST:                                       /* (an index into fx_slots.c ins_list: mix_turn loads it) */
        return ins_desc(mx_trk_of(r), m->id, vp);
    case MI_INSA: {                                     /* (an empty slot: "-") */
        uint32_t id = ins_amt(mx_trk_of(r), m->id);
        if (id == 0xFFu)
            return 0;
        *vp = &trk[mx_trk_of(r)].p[id];
        return &TP[id];
    }
    case MI_DKS: {
        static const param_desc_t DKS_D[3] = {PD("REV", F_PCT, 0, 127, DKS_DEF), PD("DLY", F_PCT, 0, 127, DKS_DEF),
                                              PD("CHO", F_PCT, 0, 127, DKS_DEF)};
        *vp = &dsend_kit[m->id % 3u];
        return &DKS_D[m->id % 3u];
    }
    case MI_AMUTE:                                      /* a lane's MUTE, the ACCENT lane's: the sequencer mute (a copy of mx_muted) */
    case MI_LMUTE:
        mx_lmute_v = (int16_t)mx_muted(r);
        *vp = &mx_lmute_v;
        return &MX_LMUTE;
    default:
        return 0;
    }
}

/* track k's value id as SCOPE's cards show it: the drum track has its own LEVEL (GLO > DRUMS) and no PAN, DRIVE or
 * sends of its own (its lanes have them) */
static const param_desc_t *mix_desc(uint32_t id, uint32_t k, int16_t **vp)
{
    *vp = 0;
    if (k == TRK_DRUM) {
        if (id == P_LEVEL) {
            *vp = &song.g[G_DRLVL];
            return &GP[G_DRLVL];
        }
        if (id != P_FXOFF FIF(FELUCCA_TRK_FILT)(&& id != P_TFLT))
            return 0;
    }
    *vp = &trk[k % NTRK].p[id];
    return &TP[id];
}

/* ---- the screen's rows */
static uint32_t mix_rows(void) { return MXR_N; }
/* the set's name, its first item's group: "MIX" "INSERT" "SENDS" "SOUND" (DR: "KIT"); MASTER "INSERT" "INS DYN"
 * "RETURNS" "DUCK": the header's "T2 insert"; on MASTER the badge MASTER and "Insert", "Returns" */
static void mix_name(uint32_t r, char *b)
{
    static const char *const G[8] = {"MIX", "INSERT", "SENDS", "SOUND", "USB EQ3", "USB EQ3 F", "USB GATER", "USB JUNO"};
    mitem_t it[MX_MAXI];
    uint32_t g;
    r %= MXR_N;
    mx_items(r, it);
    g = it[mx_set(r) * 4u].grp;
    if (r == MXR_MASTER)                                /* (as the master FX's pages; the badge says MASTER) */
        str_cpy(b, g == MG_SEND ? "RETURNS" : g == MG_RSA ? "A SENDS" : g == MG_RSB ? "B SENDS" : g == MG_RSC ? "C SENDS" : g == MG_LAST ? "DUCK" :
                   it[mx_set(r) * 4u].kind == MI_GLOB ? "INS DYN" : "INSERT", 12);
    else
        str_cpy(b, G[g & 7u], 12);
}
static int mx_is_vol(const mitem_t *m);
static void mix_cell(uint32_t r, uint32_t k, cell_t *c)
{
    mitem_t m;
    int16_t *vp;
    cell_clear(c);
    r %= MXR_N;
    if (!mx_item(r, k, &m))
        return;
    switch (m.kind) {
    default:
        cell_param(c, mx_desc(r, &m, &vp), vp);
        if (!c->d)                                      /* (an empty slot's amount: the knob's frame alone, audit #7) */
            return;
        if (m.kind == MI_INST)
            cell_ins_note(c, mx_trk_of(r), m.id);
#if FELUCCA_USB_AUDIO
        if (m.kind == MI_UTST)
            cell_own_note(c, FXI_UT_OWN, ut.slot[m.id & 1u], m.id);   /* (a pooled type: free / FULL, as a track's) */
#endif
        if (m.kind == MI_MINST)
            cell_own_note(c, FXI_MST_OWN, mst_slot[m.id & 1u], m.id);
        break;
    }
    if (m.label)
        c->label = m.label;
    c->strike |= (uint8_t)(mx_is_vol(&m) && mx_muted(r));   /* (the volume cards of a muted row) */
    c->strike |= (uint8_t)(m.kind == MI_RET && (fx_rmute >> (m.id - G_CRET) & 1u));   /* (a muted return's card) */
    if (m.kind == MI_ACC)
        c->hlp = "ACCENT LEVEL";
    if (m.kind == MI_RSND)
        c->hlp = "RET SEND";                            /* (a send between the returns: one line, whatever the target holds) */                        /* (op_help.c: its own line, the amount) */
    c->col = mx_col(r);
}
static void mx_lane_set(uint32_t l, uint32_t id, int32_t v)   /* lane l's value id (the offsets and the sends) */
{
    if (id == LPAN_VID)
        dlm_set_pan(l, v);
    else if (id >= 16u)
        dsend_set(l, id - 16u, v);
    else if (id < DE_N && !lane_value_rec(l % DRUM_LANES, id, v))   /* (REC playing: the row's lane's motion, D12) */
        dl.ofs[l % DRUM_LANES][id] = (int8_t)v;
}
/* SHIFT (OCT- held: off, the shifts' language, 2026-10-16) on the volume card (VOLUME, a lane's LEVEL, the ACCENT lane's
 * amount, the USB input's VOLUME) or a return's card: its mute toggles, once per hold (the MUTE card is gone, 2026-10-14).
 * 1: the card is one of those (the turn is taken, nothing else changes; its reset is no gesture here) */
static uint8_t mx_s2_done;                              /* toggled in this hold (ui_input clears it when OCT- is let go) */
static int mx_is_vol(const mitem_t *m)                  /* the volume card: VOLUME, a lane's LEVEL, the ACCENT amount, the USB VOLUME */
{
    return (m->kind == MI_TRK && m->id == P_LEVEL) || (m->kind == MI_GLOB && m->id == G_DRLVL) ||
           (m->kind == MI_LANE && m->id == DE_LEVEL) || m->kind == MI_ACC || (m->kind == MI_UT && m->id == 0u);
}
static const char *mx_code(uint32_t r);                 /* op_mixdraw.c */
static void mx_say(uint32_t r, const char *what)        /* the toast: the row's code (the USB input's: its name), what happened to it */
{
    ui_say(mx_is_usb(r) ? "USB" : mx_code(r), what);
}
/* a muted row whose volume card was turned (a value change, or HOME / OCT- + the knob's reset that moved it) is unmuted */
static void mx_unmute(uint32_t r, const mitem_t *m)
{
    if (!mx_muted(r))
        return;
    (void)m;
    mx_mute_set(r, 0);
    mx_say(r, " ON");
}
/* a return's mute (MASTER's RET cards, fx.c fx_rmute: its output into the master is silent, its bus runs on): SHIFT on the card toggles it, once per hold */
static int mx_ret_mute(const mitem_t *m)
{
    uint32_t ln = (uint32_t)(m->id - G_CRET) % 3u;
    if (mx_s2_done)
        return 1;
    mx_s2_done = 1;
    fx_rmute = (uint8_t)(fx_rmute ^ (1u << ln));
    ui_say(fxs_ret_label(ln), (fx_rmute >> ln & 1u) ? " MUTE" : " ON");
    return 1;
}
static int mx_mute_toggle(uint32_t r, const mitem_t *m)
{
    if (m->kind == MI_RET)
        return mx_ret_mute(m);
    if (!mx_is_vol(m))
        return 0;
    if (mx_s2_done)
        return 1;
    mx_s2_done = 1;
    mx_say(r, mx_mute_flip(r) ? " MUTE" : " ON");
    return 1;
}
static void mix_turn(uint32_t r, uint32_t k, int32_t s, int fine)
{
    mitem_t m;
    int16_t *vp;
    const param_desc_t *d;
    r %= MXR_N;
    if (!mx_item(r, k, &m))
        return;
    if (op_shift(fm1_in.buttons) && mx_mute_toggle(r, &m))
        return;
    d = mx_desc(r, &m, &vp);
    if (!d || !vp || d->max == d->min)
        return;
    if (m.kind == MI_AMUTE || m.kind == MI_LMUTE)
        mx_mute_set(r, s == OP_RESET ? 0 : param_step(d, *vp, s) != 0);
#if FELUCCA_USB_AUDIO
    else if (m.kind == MI_UTST) {                       /* the USB input's slot TYPE: as a track's (no reset) */
        if (s != OP_RESET)
            ut_load(m.id, ut_list[(uint32_t)param_step(d, *vp, fine ? s : accel(EN_K1 + k, s, accel_range(d))) % FXT_N]);
    }
#endif
    else if (m.kind == MI_MINST) {                      /* MASTER's slot TYPE: as a track's */
        if (s != OP_RESET)
            mst_load(m.id, mst_list[(uint32_t)param_step(d, *vp, s) % MST_NLIST]);
    } else if (m.kind == MI_INST) {                       /* a slot's TYPE: skips the other slot's type and the unavailable (no reset) */
        if (s != OP_RESET)
            ins_load(mx_trk_of(r), m.id,
                     ins_list[(uint32_t)ins_skip(mx_trk_of(r), m.id, *vp, param_step(d, *vp, s)) % INS_NLIST]);
    } else if (m.kind == MI_LANE && (m.id == 21u || m.id == 22u)) {   /* a lane slot's TYPE: no reset (as a track's) */
        if (s != OP_RESET)
            mx_lane_set(r - MXR_LANE0, m.id, param_step(d, *vp, fine ? s : accel(EN_K1 + k, s, accel_range(d))));
    } else if (m.kind == MI_LANE) {
        int8_t was = dl.ofs[(r - MXR_LANE0) % DRUM_LANES][DE_LEVEL];   /* (the lane's offset, where mx_lane_set writes it) */
        mx_lane_set(r - MXR_LANE0, m.id, s == OP_RESET ? d->def : param_step(d, *vp, fine ? s : accel(EN_K1 + k, s, accel_range(d))));
        if (m.id == DE_LEVEL && (dl.ofs[(r - MXR_LANE0) % DRUM_LANES][DE_LEVEL] != was || s == OP_RESET))
            mx_unmute(r, &m);
    }
    else if (m.kind == MI_ACC) {
        int16_t was = *vp;
        acc_turn(k, s, fine);                           /* (the ACCENT amount: motion too) */
        if (*vp != was)
            mx_unmute(r, &m);
    }
    else {
        int16_t was = *vp;
        val_turn(d, vp, k, s, fine);
        if (mx_is_vol(&m) && (*vp != was || s == OP_RESET))
            mx_unmute(r, &m);                           /* (a reset counts, whatever the value was) */
        else if (m.kind == MI_RET && (*vp != was || s == OP_RESET) && (fx_rmute >> ((uint32_t)(m.id - G_CRET) % 3u) & 1u)) {
            fx_rmute = (uint8_t)(fx_rmute & ~(1u << ((uint32_t)(m.id - G_CRET) % 3u)));   /* (a turned level unmutes it, as a volume knob) */
            ui_say(fxs_ret_label((uint32_t)(m.id - G_CRET) % 3u), " ON");
        }
    }
}
/* YES on the mixer: none (the user, 2026-10-12, D6: SAVE tapped there is PROJECT, always, op_input.c). FX on and MUTE are
 * knobs; a row's SOUND row is EDIT's first; MASTER's effects are FX on its row */
static int mix_yes(uint32_t r, uint32_t k, uint32_t ok)
{
    (void)r, (void)k, (void)ok;
    return 0;
}

/* ---- ALGORITHM, SELECT on the mixer (op_input.c) */
static void lane_pick(uint32_t l);                      /* op_input.c: the lane selected (its preview) */
static void track_select(uint32_t i);                   /* op_input.c */
static void mx_apply(uint32_t r)                        /* the cursor on row r: the track (and the lane) selected */
{
    ui.row[SCR_HOME] = (uint8_t)r;
    if (r == MXR_MASTER)
        return;
    if (mx_is_usb(r)) {                                 /* (the USB input: no track to select, as MASTER) */
        op_repaint();
        return;
    }
    ui.lane = (uint8_t)(r >= MXR_LANE0);                /* (DR itself or a lane) */
    track_select(mx_trk_of(r));
    if (r >= MXR_LANE0)
        lane_pick(r - MXR_LANE0);
    op_repaint();                                       /* (the walk: in place, no clear) */
}
static void mx_page(int32_t d)                         /* SELECT: the knob sets, stopping at the ends (GLO tapped opens
                                                         * GLOBAL since 2026-10-12: op_global.c) */
{
    uint32_t n = mx_sets(mx_row()), s = mx_set(mx_row());
    s = (uint32_t)clamp((int32_t)s + d, 0, (int32_t)n - 1);
    mx.set = (uint8_t)s;
    ui.hot = 0;
    ui.hot_lit = 0;
}
/* once a frame: the cursor follows the track selected elsewhere (ALGORITHM on another screen, the editor) and the
 * lane picked (HOME + a key); the lanes' hits (drums.c drums.hits, as SLOOP's kit pads take them) */
static void mx_tick(void)
{
    uint32_t i, hits, r = mx_row();
    fm1_irq_off();
    hits = drums.hits;
    drums.hits = 0;
    fm1_irq_on();
    for (i = 0; i < DRUM_LANES; i++)
        mx.lit[i] = (uint8_t)((hits >> i) & 1u ? 12u : mx.lit[i] ? mx.lit[i] - 1u : 0u);
    if (r == MXR_MASTER || (ui.scr == SCR_HOME && mx_is_usb(r)))
        return;
    if (ui.scr != SCR_HOME || mx_trk_of(r) != song.sel)
        ui.row[SCR_HOME] = (uint8_t)mx_sel_row();      /* (off the mixer, another track: the row follows the selection) */
    else if (mx_is_lane(r) && r - MXR_LANE0 != lane_selected())
        ui.row[SCR_HOME] = (uint8_t)(MXR_LANE0 + lane_selected());
    if (ui.scr == SCR_HOME && is_drum(TSEL))
        ui.lane = (uint8_t)mx_is_lane(ui.row[SCR_HOME] % MXR_N);   /* (on the mixer its row is the selection) */
}

/* the SCOPE screen's rows (op_scope.c, after the renderer: its trace draws with it): for the screen table */
static uint32_t scope_rows(void);
static void scope_name(uint32_t r, char *b);
static void scope_cell(uint32_t r, uint32_t k, cell_t *c);
static void scope_turn(uint32_t r, uint32_t k, int32_t s, int fine);
static int scope_yes(uint32_t r, uint32_t k, uint32_t ok);
