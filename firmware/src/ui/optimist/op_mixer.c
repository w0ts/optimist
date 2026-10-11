/* SPDX-License-Identifier: GPL-3.0-only */
/* The mixer, horizontal (docs/UI-OPTIMIST-DESIGN.md section 4.1; the user, 2026-10-08: "Horizontal mixer is better"):
 * the rows are the tracks, the four knobs four values of the SELECTED row, shown on the cards (1x4 or 2x2).
 *   rows       MASTER (above T1, out of view until the cursor goes up to it), T1 T2 T3, DR, then the drum track's 16
 *              lanes (indented, each named in its source's colour); each row its VU meter (the compressor's
 *              reduction pushing in from the right) and its sequence (op_mixdraw.c)
 *   ALGORITHM  walks the rows, T1 .. DR .. the 16th lane and back, MASTER above T1 (it stops at the ends): the track
 *              selected follows (song.sel), a lane row selects the lane (lane_sel; its sound previews when stopped)
 *   SELECT     the row's knob sets, its pages, in the same order on every row (the user, 2026-10-11): the mix
 *              (VOLUME PAN), the inserts (DRIVE or DIST, FILTER, COMP), the sends (REV DLY CHO), then FX on and the
 *              SOUND; a lane LEVEL CUT | DRIVE | REV DLY CHO | SOUND; DR VOLUME | FILTER | FX on, KIT; MASTER FILT
 *              DUST DUCK | THRS RATIO GAIN CEIL. Each group on a set of its own. Values only: the screens are not sets.
 *              GLO tapped: the mixer; again, the next set (past the last, the first). The header names the set
 *   PRESETS    the hot cell one unit (on SOUND: the next / previous sound)
 *   YES        (a cell turned last; with none, SAVE tapped opens PROJECT, op_input.c) a toggle toggles (FX on), any other cell opens the row's SOUND rows
 * The inserts in the order the audio runs them (fx_slots.c: DIST, FILTER, COMP): DRIVE when DIST is in no slot (the
 * engine's drive), FILTER (P_TFLT), COMP only in a slot; the sends only in a slot, REV DLY CHO. The drum track's row:
 * its LEVEL (GLO > DRUMS), FILTER, FX on, the kit; its lanes carry the sounds' own values (SOUND 2 and 3: the offsets
 * and the sends). */
enum { MXR_MASTER, MXR_T1, MXR_DR = MXR_T1 + TRK_DRUM, MXR_LANE0, MXR_N = MXR_LANE0 + DRUM_LANES };
enum { MI_NONE, MI_TRK, MI_GLOB, MI_LANE, MI_SOUND };
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
    uint8_t set;                                        /* the knob set shown (SELECT, GLO again) */
    uint8_t lit[DRUM_LANES];                            /* frames a lane's hit stays lit (falling) */
} mx;

static uint32_t mx_row(void) { return ui.row[SCR_HOME] % MXR_N; }
static uint32_t mx_trk_of(uint32_t r) { return r >= MXR_DR ? TRK_DRUM : r > MXR_MASTER ? r - MXR_T1 : 0u; }   /* (MASTER: T1, unused) */
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
/* a synth track: VOLUME PAN | DRIVE (DIST in a slot) FILTER COMP (in a slot) | the sends in a slot, REV DLY CHO |
 * FX ON, SOUND. The inserts in the order the audio runs them (fx_slots.c: DIST, FILTER, COMP), whatever the slots */
static uint32_t mx_items_synth(mitem_t *it)
{
    uint32_t n = 0;
    n = mx_add(it, n, MG_MIX, "VOLUME", MI_TRK, P_LEVEL);
    n = mx_add(it, n, MG_MIX, "PAN", MI_TRK, P_PAN);
#if FELUCCA_FX_DIST
    n = mx_add(it, n, MG_INS, FXS_ON(FXT_DIST) ? 0 : "DRIVE", MI_TRK, P_DIST);
#endif
#if FELUCCA_TRK_FILT
    n = mx_add(it, n, MG_INS, FXS_ON(FXT_FILT) ? 0 : "FILTER", MI_TRK, P_TFLT);
#endif
#if FELUCCA_MASTER_COMP
    if (FXS_ON(FXT_COMP))
        n = mx_add(it, n, MG_INS, 0, MI_TRK, P_TCOMP);
#endif
    if (FXS_ON(FXT_REV))
        n = mx_add(it, n, MG_SEND, 0, MI_TRK, P_REV);
    if (FXS_ON(FXT_DLY))
        n = mx_add(it, n, MG_SEND, 0, MI_TRK, P_DLY);
    if (FXS_ON(FXT_CHO))
        n = mx_add(it, n, MG_SEND, 0, MI_TRK, P_CHOR);
    n = mx_add(it, n, MG_LAST, "FX ON", MI_TRK, P_FXOFF);
    return mx_add(it, n, MG_LAST, "SOUND", MI_SOUND, 0);
}
static uint32_t mx_items_value(uint32_t r, mitem_t *it)   /* the row's value items, unpadded */
{
    uint32_t n = 0;
    if (r == MXR_MASTER) {                              /* filter, dust, duck; then the compressor */
        n = mx_add(it, n, MG_MIX, "FILT", MI_GLOB, G_FILT);
        n = mx_add(it, n, MG_MIX, "DUST", MI_GLOB, G_DUST);
        n = mx_add(it, n, MG_MIX, "DUCK", MI_GLOB, G_DUCK);
#if FELUCCA_MASTER_COMP
        n = mx_add(it, n, MG_INS, "THRS", MI_GLOB, G_CTHR);
        n = mx_add(it, n, MG_INS, "RATIO", MI_GLOB, G_CRAT);
        n = mx_add(it, n, MG_INS, "GAIN", MI_GLOB, G_CGAIN);
        n = mx_add(it, n, MG_INS, "CEIL", MI_GLOB, G_CCEIL);
#endif
        return n;
    }
    if (r >= MXR_LANE0) {                               /* a lane: the sound's own values (SOUND 2's offsets, 3's sends) */
        n = mx_add(it, n, MG_MIX, "LEVEL", MI_LANE, DE_LEVEL);
        n = mx_add(it, n, MG_MIX, "CUT", MI_LANE, DE_CUT);   /* (PAN's place: a sound has no pan) */
#if FELUCCA_FX_DIST
        n = mx_add(it, n, MG_INS, "DRIVE", MI_LANE, DE_DRIVE);
#endif
        if (FXS_ON(FXT_REV))
            n = mx_add(it, n, MG_SEND, "REV", MI_LANE, 16u);
        if (FXS_ON(FXT_DLY))
            n = mx_add(it, n, MG_SEND, "DLY", MI_LANE, 17u);
        if (FXS_ON(FXT_CHO))
            n = mx_add(it, n, MG_SEND, "CHO", MI_LANE, 18u);
        return mx_add(it, n, MG_LAST, "SOUND", MI_SOUND, 0);
    }
    if (r == MXR_DR) {                                  /* the drum track: its level | FILTER | FX, the kit */
        n = mx_add(it, n, MG_MIX, "VOLUME", MI_GLOB, G_DRLVL);
#if FELUCCA_TRK_FILT
        n = mx_add(it, n, MG_INS, "FILTER", MI_TRK, P_TFLT);
#endif
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
    case MI_LANE:
        d = dsnd_desc_lane(r - MXR_LANE0, m->id, vp);
        return d && !PARAM_HIDDEN(d) ? d : 0;
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
/* the set's name, its first item's group: "MIX" "INSERT" "SENDS" "SOUND" (DR: "KIT"); MASTER "MASTER" "COMP": the
 * header's "T2 insert", "Mix comp" */
static void mix_name(uint32_t r, char *b)
{
    static const char *const G[4] = {"MIX", "INSERT", "SENDS", "SOUND"};
    mitem_t it[MX_MAXI];
    uint32_t g;
    r %= MXR_N;
    mx_items(r, it);
    g = it[mx_set(r) * 4u].grp & 3u;
    if (r == MXR_MASTER)
        str_cpy(b, g ? "COMP" : "MASTER", 12);
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
            str_cpy(c->val, LANE_NAME[r - MXR_LANE0], sizeof c->val);
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
    else if (id < DE_N)
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
    if (m.kind == MI_LANE)
        mx_lane_set(r - MXR_LANE0, m.id, s == OP_RESET ? d->def : param_step(d, *vp, fine ? s : accel(EN_K1 + k, s, accel_range(d))));
    else
        val_turn(d, vp, k, s, fine);
}
static void mx_open_sound(uint32_t r)                   /* the row's SOUND rows (a lane: that lane's) */
{
    if (r == MXR_MASTER) {
        op_enter(SCR_FX);                               /* (the master's: the FX screen) */
        return;
    }
    if (r >= MXR_LANE0)
        lane_select(r - MXR_LANE0);
    op_enter(SCR_SOUND);
    ui.row[SCR_SOUND] = 0;                              /* (the SOUND row first: the sound, then its pages) */
}
static int mix_yes(uint32_t r, uint32_t k, uint32_t ok)
{
    mitem_t m;
    int16_t *vp;
    (void)ok;
    r %= MXR_N;
    if (mx_item(r, k, &m)) {
        if ((m.kind == MI_TRK || m.kind == MI_GLOB) && val_toggle(mx_desc(r, &m, &vp), vp))
            return 1;                                   /* (FX: on / dry) */
    }
    mx_open_sound(r);
    return 1;
}

/* ---- ALGORITHM, SELECT, GLO on the mixer (op_input.c) */
static void lane_pick(uint32_t l);                      /* op_input.c: the lane selected (its preview) */
static void track_select(uint32_t i);                   /* op_input.c */
static void mx_apply(uint32_t r)                        /* the cursor on row r: the track (and the lane) selected */
{
    ui.row[SCR_HOME] = (uint8_t)r;
    if (r == MXR_MASTER)
        return;
    track_select(mx_trk_of(r));
    if (r >= MXR_LANE0)
        lane_pick(r - MXR_LANE0);
    op_repaint();                                       /* (the walk: in place, no clear) */
}
static void mx_walk(int32_t d)                          /* ALGORITHM: a row a detent, stopping at the ends */
{
    int32_t r = clamp((int32_t)mx_row() + d, 0, MXR_N - 1);
    if ((uint32_t)r != mx_row())
        mx_apply((uint32_t)r);
}
static void mx_page(int32_t d, int round)               /* SELECT (round 0: stops), GLO again (round 1: wraps) */
{
    uint32_t n = mx_sets(mx_row()), s = mx_set(mx_row());
    if (round)
        s = s + 1u < n ? s + 1u : 0u;
    else
        s = (uint32_t)clamp((int32_t)s + d, 0, (int32_t)n - 1);
    mx.set = (uint8_t)s;
    ui.hot = 0;
    ui.hot_lit = 0;
}
static void mx_glo_tap(void)                            /* GLO tapped: the mixer; on it, the next set */
{
    if (ui.scr == SCR_HOME) {
        mx_page(1, 1);
        return;
    }
    op_enter(SCR_HOME);
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
    if (mx_trk_of(r) != song.sel)
        ui.row[SCR_HOME] = (uint8_t)(MXR_T1 + song.sel);
    else if (r >= MXR_LANE0 && r - MXR_LANE0 != lane_selected())
        ui.row[SCR_HOME] = (uint8_t)(MXR_LANE0 + lane_selected());
}

/* the SCOPE screen's rows (op_scope.c, after the renderer: its trace draws with it): for the screen table */
static uint32_t scope_rows(void);
static void scope_name(uint32_t r, char *b);
static void scope_cell(uint32_t r, uint32_t k, cell_t *c);
static void scope_turn(uint32_t r, uint32_t k, int32_t s, int fine);
static int scope_yes(uint32_t r, uint32_t k, uint32_t ok);
