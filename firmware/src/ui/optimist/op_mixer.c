/* SPDX-License-Identifier: GPL-3.0-only */
/* The mixer, horizontal (docs/UI-OPTIMIST-DESIGN.md section 4.1; the user, 2026-10-08: "Horizontal mixer is better"):
 * the rows are the tracks, the four knobs four values of the SELECTED row, shown on the cards (1x4: the mixer keeps them).
 *   rows       MASTER (above T1), T1 T2 T3, DR, then the drum track's 16 lanes; the panel shows the cursor's group
 *              only (the user, 2026-10-11): MASTER alone, the four channels, or the 16 drum sounds; each row its VU
 *              meter (the compressor's reduction pushing in from the right) and its sequence (op_mixdraw.c)
 *   ALGORITHM  walks the rows, MASTER, T1 .. DR .. the 16th lane and back (it stops at the ends; op_walk.c, the same
 *              walk as every page's): the track selected follows (song.sel), a lane row selects the lane (lane_sel, ui.lane)
 *   SELECT     the row's knob sets, its pages, in the same order on every row (the user, 2026-10-11): the mix
 *              (MUTE VOLUME PAN), the inserts (DRIVE or DIST, FILTER, COMP), the sends (REV DLY CHO), then FX on and the
 *              SOUND; a lane MUTE LEVEL CUT | DRIVE | REV DLY CHO | SOUND; DR MUTE VOLUME | FILTER | FX on, KIT; MASTER FILT
 *              DUST DUCK | the returns, a card a FX slot (S1..S4: a send's return level, "-" else; 2026-10-12) | THRS
 *              RATIO GAIN CEIL. Each group on a set of its own. Values only: the screens are not sets.
 *              SELECT pages the sets (GLO tapped opens GLOBAL since 2026-10-12). The header names the set
 *   PRESETS    the row's sound: a track's preset, DR's kit, a lane's own sound (op_walk.c lane_sound_step)
 *   SAVE       PROJECT (always: the mixer has no YES, D6); FX on and MUTE are knobs, the SOUND row is EDIT's first
 * The inserts in the order the audio runs them (fx_slots.c: DIST, FILTER, COMP): DRIVE when DIST is in no slot (the
 * engine's drive), FILTER (P_TFLT), COMP only in a slot; the sends only in a slot, REV DLY CHO. The drum track's row:
 * its LEVEL (GLO > DRUMS), FILTER, FX on, the kit; its lanes carry the sounds' own values (SOUND 2 and 3: the offsets
 * and the sends). */
enum { MXR_MASTER, MXR_T1, MXR_DR = MXR_T1 + TRK_DRUM, MXR_LANE0, MXR_N = MXR_LANE0 + DRUM_LANES };
enum { MI_NONE, MI_TRK, MI_GLOB, MI_LANE, MI_SOUND, MI_LMUTE, MI_RET, MI_INST, MI_INSA, MI_DKS };
/* MI_LMUTE: a lane's mute (drum_mix.c dlm_mute); MI_RET: MASTER's FX slot id's return; MI_INST / MI_INSA: the row's
 * insert slot id's TYPE / amount (fx_slots.c); MI_DKS: the drum track's send level id (drum_sends.c dsend_kit: 0 REV,
 * 1 DLY, 2 CHO) */
/* the knob sets' groups, in this order on every row (the user, 2026-10-11): the mix (VOLUME PAN; a lane LEVEL CUT;
 * MASTER its FILT DUST DUCK), the insert effects (DRIVE / DIST, FILTER, COMP; MASTER: its compressor), the sends (REV DLY
 * CHO), then the rest (FX on, the sound or the kit). A group starts a set of its own where the padding fits MX_MAXI */
enum { MG_MIX, MG_INS, MG_SEND, MG_LAST };
typedef struct {
    const char *label;                                  /* 0: the descriptor's */
    uint8_t kind, id;                                   /* MI_*; a P_* / G_* / lane value id / screen */
    uint8_t grp;                                        /* MG_* */
} mitem_t;
#define MX_MAXI 20u
static struct {
    uint8_t set;                                        /* the knob set shown (SELECT) */
    uint8_t lit[DRUM_LANES];                            /* frames a lane's hit stays lit (falling) */
} mx;

static uint32_t mx_row(void) { return ui.row[SCR_HOME] % MXR_N; }
/* the mixer on its MASTER row: master has no instrument, so the track buttons (ENV LFO EDIT ARP SCL SEQ) are dark and
 * do nothing there, FX is the master FX (the user, 2026-10-12: "many buttons like env lfo are active while on master
 * and send to track 1. wrong") */
static int mx_on_master(void) { return ui.scr == SCR_HOME && mx_row() == MXR_MASTER; }
/* MASTER selected, on any page (the user, 2026-10-12: "i want uniformity"): the mixer's row, the master FX screen, a
 * layer's ui.master. The track buttons are dark and do nothing there */
static int master_sel(void) { return mx_on_master() || ui.scr == SCR_FX || ui.master; }
static int mx_master_dead(uint32_t b)
{
    return master_sel() && (b == B_ENV || b == B_LFO || b == B_EDIT || b == B_ARP || b == B_SCL || b == B_SEQ);
}
static uint32_t mx_trk_of(uint32_t r) { return r >= MXR_DR ? TRK_DRUM : r > MXR_MASTER ? r - MXR_T1 : 0u; }   /* (MASTER: T1, unused) */
/* a drum lane selected, not DR itself (ui.lane): on the mixer its row says it (a lane row), elsewhere ui.lane while the
 * drum track is selected. PRESETS then steps the lane's sound, not the kit (op_walk.c) */
static int lane_picked(void)
{
    if (ui.scr == SCR_HOME)
        return ui.row[SCR_HOME] % MXR_N >= MXR_LANE0;
    return ui.lane && is_drum(TSEL);
}
static uint32_t mx_sel_row(void)                        /* the row of the selection (the track, DR or its lane) */
{
    return !is_drum(TSEL) ? MXR_T1 + song.sel : ui.lane ? MXR_LANE0 + lane_selected() : MXR_DR;
}
static uint16_t mx_col(uint32_t r)                      /* the row's colour: its engine's, its lane's source */
{
    return r == MXR_MASTER ? C_HI : r >= MXR_LANE0 ? lane_col(r - MXR_LANE0) : trk_col(mx_trk_of(r));
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
/* the INS set (the track insert slots, 2026-10-12): S1 TYPE, S1's amount, S2 TYPE, S2's amount, as SOUND > INSERTS */
static uint32_t mx_items_ins(mitem_t *it, uint32_t n)
{
    if (!(FXT_BUILT & FXT_INSERT))
        return n;
    n = mx_add(it, n, MG_INS, 0, MI_INST, 0);
    n = mx_add(it, n, MG_INS, 0, MI_INSA, 0);
    n = mx_add(it, n, MG_INS, 0, MI_INST, 1);
    return mx_add(it, n, MG_INS, 0, MI_INSA, 1);
}
/* a synth track: MUTE VOLUME PAN | its insert slots (S1 TYPE, amount, S2 TYPE, amount) | the sends in a slot, REV DLY
 * CHO | FX ON, SOUND */
static uint32_t mx_items_synth(mitem_t *it)
{
    uint32_t n = 0;
    n = mx_add(it, n, MG_MIX, "MUTE", MI_TRK, P_MUTE);   /* (the user, 2026-10-12: "a card for muting the track, first card") */
    n = mx_add(it, n, MG_MIX, "VOLUME", MI_TRK, P_LEVEL);
    n = mx_add(it, n, MG_MIX, "PAN", MI_TRK, P_PAN);
    n = mx_items_ins(it, n);
    if (FXS_ON(FXT_REV))
        n = mx_add(it, n, MG_SEND, 0, MI_TRK, P_REV);
    if (FXS_ON(FXT_DLY))
        n = mx_add(it, n, MG_SEND, 0, MI_TRK, P_DLY);
    if (FXS_ON(FXT_CHO))
        n = mx_add(it, n, MG_SEND, 0, MI_TRK, P_CHOR);
    n = mx_add(it, n, MG_LAST, "FX ON", MI_TRK, P_FXOFF);
    return mx_add(it, n, MG_LAST, "SOUND", MI_SOUND, 0);
}
/* MASTER's Returns (2026-10-12, the user: "on the mixer screen Master, we should have a volume control for the return
 * tracks"): a card a FX slot, in the slots' order (FX > SLOTS), named after it: "S2 CHO"; a send's (CHO DLY REV) turns
 * its return (core.h G_CRET G_DRET G_RRET, fx.c), an insert or an empty slot shows "-". One instance per type (fx_slots.c
 * fxs_set): one return per send. No send in any slot: no set */
static char mx_rlab[FX_NSLOT][8];
static uint32_t mx_ret_id(uint32_t k)                   /* slot k's return (a G_* id), 0xFF: none (an insert, empty) */
{
    uint32_t t = fxs_slot[k % FX_NSLOT];
    return (t == FXT_CHO || t == FXT_DLY || t == FXT_REV) && FXS_ON(t) ? G_CRET + (t - FXT_CHO) : 0xFFu;
}
static uint32_t mx_items_ret(mitem_t *it, uint32_t n)
{
    uint32_t k, any = 0;
    for (k = 0; k < FX_NSLOT; k++)
        any |= mx_ret_id(k) != 0xFFu;
    for (k = 0; any && k < FX_NSLOT; k++) {
        uint32_t t = fxs_slot[k], a = t < FXT_N && (FXT_BUILT >> t & 1u) ? FXT_AMT[t] : 0xFFu;
        mx_rlab[k][0] = 'S', mx_rlab[k][1] = (char)('1' + k), mx_rlab[k][2] = 0;
        if (a != 0xFFu) {
            mx_rlab[k][2] = ' ';
            str_cpy(mx_rlab[k] + 3, TP[a].label, sizeof mx_rlab[k] - 3u);
        }
        n = mx_add(it, n, MG_SEND, mx_rlab[k], MI_RET, k);
    }
    return n;
}
static uint32_t mx_items_value(uint32_t r, mitem_t *it)   /* the row's value items, unpadded */
{
    uint32_t n = 0;
    if (r == MXR_MASTER) {                              /* filter, dust, duck; the returns; then the compressor */
        n = mx_add(it, n, MG_MIX, "FILT", MI_GLOB, G_FILT);
        n = mx_add(it, n, MG_MIX, "DUST", MI_GLOB, G_DUST);
        n = mx_add(it, n, MG_MIX, "DUCK", MI_GLOB, G_DUCK);
        n = mx_items_ret(it, n);
#if FELUCCA_MASTER_COMP
        n = mx_add(it, n, MG_INS, "THRS", MI_GLOB, G_CTHR);
        n = mx_add(it, n, MG_INS, "RATIO", MI_GLOB, G_CRAT);
        n = mx_add(it, n, MG_INS, "GAIN", MI_GLOB, G_CGAIN);
        n = mx_add(it, n, MG_INS, "CEIL", MI_GLOB, G_CCEIL);
#endif
        return n;
    }
    if (r >= MXR_LANE0) {                               /* a lane: the sound's own values (SOUND 2's offsets, 3's sends) */
        n = mx_add(it, n, MG_MIX, "MUTE", MI_LMUTE, 0);   /* (the lane's own: GLO's lane mutes, SLOOP's kit pads) */
        n = mx_add(it, n, MG_MIX, "LEVEL", MI_LANE, DE_LEVEL);
        n = mx_add(it, n, MG_MIX, "CUT", MI_LANE, DE_CUT);   /* (PAN's place: a sound has no pan) */
#if FELUCCA_FX_DIST
        n = mx_add(it, n, MG_INS, "DRIVE", MI_LANE, DE_DRIVE);
        n = mx_add(it, n, MG_INS, "DST", MI_LANE, 19u);   /* (the sound's own DIST and COMP: always heard, D7) */
#endif
#if FELUCCA_MASTER_COMP
        n = mx_add(it, n, MG_INS, "CMP", MI_LANE, 20u);
#endif
        if (FXS_ON(FXT_REV))
            n = mx_add(it, n, MG_SEND, "REV", MI_LANE, 16u);
        if (FXS_ON(FXT_DLY))
            n = mx_add(it, n, MG_SEND, "DLY", MI_LANE, 17u);
        if (FXS_ON(FXT_CHO))
            n = mx_add(it, n, MG_SEND, "CHO", MI_LANE, 18u);
        return mx_add(it, n, MG_LAST, "SOUND", MI_SOUND, 0);
    }
    if (r == MXR_DR) {                                  /* the drum track: its level | its insert slots | its send levels
                                                         * (2026-10-12, the user: "it should be like any other track") |
                                                         * FX ON, the kit */
        n = mx_add(it, n, MG_MIX, "MUTE", MI_TRK, P_MUTE);
        n = mx_add(it, n, MG_MIX, "VOLUME", MI_GLOB, G_DRLVL);
        n = mx_items_ins(it, n);
        if (FXS_ON(FXT_REV))
            n = mx_add(it, n, MG_SEND, 0, MI_DKS, 0);
        if (FXS_ON(FXT_DLY))
            n = mx_add(it, n, MG_SEND, 0, MI_DKS, 1);
        if (FXS_ON(FXT_CHO))
            n = mx_add(it, n, MG_SEND, 0, MI_DKS, 2);
        n = mx_add(it, n, MG_LAST, "FX ON", MI_TRK, P_FXOFF);
        return mx_add(it, n, MG_LAST, "KIT", MI_SOUND, 0);
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
        return &TP[m->id];
    case MI_GLOB:
        *vp = &song.g[m->id];
        return &GP[m->id];
    case MI_RET:                                        /* (an insert, an empty slot: none, "-") */
        if (mx_ret_id(m->id) == 0xFFu)
            return 0;
        *vp = &song.g[mx_ret_id(m->id)];
        return &GP[mx_ret_id(m->id)];
    case MI_LANE:
        d = dsnd_desc_lane(r - MXR_LANE0, m->id, vp);
        if (d)
            dsv_rec(r - MXR_LANE0, m->id);              /* (recording: the hold the knob goes on from) */
        return d && !PARAM_HIDDEN(d) ? d : 0;
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
    case MI_LMUTE:                                      /* (a copy of the mask's bit: mx_lane_mute writes it) */
        mx_lmute_v = (int16_t)dlm_muted(r - MXR_LANE0);
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
/* the set's name, its first item's group: "MIX" "INSERT" "SENDS" "SOUND" (DR: "KIT"); MASTER "MASTER" "RETURNS"
 * "COMP": the header's "T2 insert", "Mix returns", "Mix comp" */
static void mix_name(uint32_t r, char *b)
{
    static const char *const G[4] = {"MIX", "INSERT", "SENDS", "SOUND"};
    mitem_t it[MX_MAXI];
    uint32_t g;
    r %= MXR_N;
    mx_items(r, it);
    g = it[mx_set(r) * 4u].grp & 3u;
    if (r == MXR_MASTER)
        str_cpy(b, g == MG_SEND ? "RETURNS" : g ? "COMP" : "MASTER", 12);
    else
        str_cpy(b, r == MXR_DR && g == MG_LAST ? "KIT" : G[g], 12);
}
static void mix_cell(uint32_t r, uint32_t k, cell_t *c)
{
    mitem_t m;
    int16_t *vp;
    cell_clear(c);
    r %= MXR_N;
    if (!mx_item(r, k, &m))
        return;
    switch (m.kind) {
    case MI_SOUND:
        c->kind = CK_RO;
        if (r >= MXR_LANE0)
            str_cpy(c->val, lane_sound_name(r - MXR_LANE0), sizeof c->val);   /* (what PRESETS steps there) */
        else
            snd_name(mx_trk_of(r), c->val);
        break;
    default:
        cell_param(c, mx_desc(r, &m, &vp), vp);
        if (!c->d)
            str_cpy(c->val, "-", sizeof c->val);
        break;
    }
    if (m.label)
        c->label = m.label;
    c->col = mx_col(r);
}
static void mx_lane_set(uint32_t l, uint32_t id, int32_t v)   /* lane l's value id (the offsets and the sends) */
{
    if (id >= 16u)
        dsend_set(l, id - 16u, v);
    else if (id < DE_N && !lane_value_rec(l % DRUM_LANES, id, v))   /* (REC playing: the row's lane's motion, D12) */
        dl.ofs[l % DRUM_LANES][id] = (int8_t)v;
}
static void mix_turn(uint32_t r, uint32_t k, int32_t s, int fine)
{
    mitem_t m;
    int16_t *vp;
    const param_desc_t *d;
    r %= MXR_N;
    if (!mx_item(r, k, &m))
        return;
    if (m.kind == MI_SOUND) {
        return;                                         /* (the names are read-outs; PRESETS is op_input.c's) */
    }
    d = mx_desc(r, &m, &vp);
    if (!d || !vp || d->max == d->min)
        return;
    if (m.kind == MI_LMUTE)
        dlm_set_mute(r - MXR_LANE0, s == OP_RESET ? 0 : param_step(d, *vp, s) != 0);
    else if (m.kind == MI_INST) {                       /* a slot's TYPE: the other slot's swaps them (no reset) */
        if (s != OP_RESET)
            ins_load(mx_trk_of(r), m.id, ins_list[(uint32_t)param_step(d, *vp, s) % INS_NLIST]);
    } else if (m.kind == MI_LANE)
        mx_lane_set(r - MXR_LANE0, m.id, s == OP_RESET ? d->def : param_step(d, *vp, fine ? s : accel(EN_K1 + k, s, accel_range(d))));
    else
        val_turn(d, vp, k, s, fine);
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
    if (r == MXR_MASTER)
        return;
    if (ui.scr != SCR_HOME || mx_trk_of(r) != song.sel)
        ui.row[SCR_HOME] = (uint8_t)mx_sel_row();      /* (off the mixer, another track: the row follows the selection) */
    else if (r >= MXR_LANE0 && r - MXR_LANE0 != lane_selected())
        ui.row[SCR_HOME] = (uint8_t)(MXR_LANE0 + lane_selected());
    if (ui.scr == SCR_HOME && is_drum(TSEL))
        ui.lane = (uint8_t)(ui.row[SCR_HOME] % MXR_N >= MXR_LANE0);   /* (on the mixer its row is the selection) */
}

/* the SCOPE screen's rows (op_scope.c, after the renderer: its trace draws with it): for the screen table */
static uint32_t scope_rows(void);
static void scope_name(uint32_t r, char *b);
static void scope_cell(uint32_t r, uint32_t k, cell_t *c);
static void scope_turn(uint32_t r, uint32_t k, int32_t s, int fine);
static int scope_yes(uint32_t r, uint32_t k, uint32_t ok);
