/* SPDX-License-Identifier: GPL-3.0-only */
/* The mixer, horizontal (docs/UI-OPTIMIST-DESIGN.md section 4.1; the user, 2026-10-08: "Horizontal mixer is better"):
 * the rows are the tracks, the four knobs four values of the SELECTED row, shown on the cards (1x4 or 2x2).
 *   rows       MASTER (above T1, out of view until the cursor goes up to it), T1 T2 T3, DR, then the drum track's 16
 *              lanes (indented, each named in its source's colour); each row its VU meter (the compressor's
 *              reduction pushing in from the right) and its sequence (op_mixdraw.c)
 *   ALGORITHM  walks the rows, T1 .. DR .. the 16th lane and back, MASTER above T1 (it stops at the ends): the track
 *              selected follows (song.sel), a lane row selects the lane (lane_sel; its sound previews when stopped)
 *   SELECT     the row's knob sets, its pages: a track VOLUME INSERT SEND PAN, then the rest (the other effects in
 *              their slots' order, FILTER, FX on / dry, SOUND); a lane LEVEL DRIVE REV CUT, then DLY CHO SOUND;
 *              MASTER FILT THRS RATIO DUCK, then DUST GAIN CEIL. Values only: the screens are not sets.
 *              GLO tapped: the mixer; again, the next set (stopping at the last). The header names the set
 *   PRESETS    the hot cell one unit (on SOUND: the next / previous sound)
 *   YES        (a cell turned last; with none, SAVE tapped opens PROJECT, op_input.c) a toggle toggles (FX on), any other cell opens the row's SOUND rows
 * INSERT is the track's first insert effect in the slots' order (DIST, COMP, FILTER: fx_slots.c FXT_INSERT), its
 * amount (the slot's one value, FXT_AMT: the core's "main parameter"); with none in a slot, DRIVE. SEND is REV when
 * it is in a slot, else the first send in the slots' order. The drum track's row: its LEVEL (GLO > DRUMS), FILTER,
 * FX on, the kit; its lanes carry the sounds' own values (SOUND 2 and 3: the offsets and the sends). */
enum { MXR_MASTER, MXR_T1, MXR_DR = MXR_T1 + TRK_DRUM, MXR_LANE0, MXR_N = MXR_LANE0 + DRUM_LANES };
enum { MI_NONE, MI_TRK, MI_GLOB, MI_LANE, MI_SOUND };
typedef struct {
    const char *label;                                  /* 0: the descriptor's */
    uint8_t kind, id;                                   /* MI_*; a P_* / G_* / lane value id / screen */
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

/* the track's INSERT and SEND amounts (P_* ids; 0xFF none) */
static uint32_t mx_insert(void)
{
    uint32_t k;
    for (k = 0; k < FX_NSLOT; k++)
        if ((FXT_INSERT >> fxs_slot[k] & 1u) && FXS_ON(fxs_slot[k]) && fxs_amt(k) != 0xFFu)
            return fxs_amt(k);
    return FELUCCA_FX_DIST ? (uint32_t)P_DIST : 0xFFu;  /* (no insert in a slot: DRIVE) */
}
static uint32_t mx_send(void)
{
    uint32_t k;
    if (FXS_ON(FXT_REV))
        return P_REV;
    for (k = 0; k < FX_NSLOT; k++)
        if (!(FXT_INSERT >> fxs_slot[k] & 1u) && FXS_ON(fxs_slot[k]) && fxs_amt(k) != 0xFFu)
            return fxs_amt(k);
    return FELUCCA_FX_REVERB ? (uint32_t)P_REV : 0xFFu;
}
static uint32_t mx_add(mitem_t *it, uint32_t n, const char *label, uint32_t kind, uint32_t id)
{
    if (n < MX_MAXI) {
        it[n].label = label;
        it[n].kind = (uint8_t)kind;
        it[n].id = (uint8_t)id;
    }
    return n + 1u;
}
static uint32_t mx_pad(mitem_t *it, uint32_t n)         /* to the end of a set of four */
{
    while (n % 4u)
        n = mx_add(it, n, 0, MI_NONE, 0);
    return n;
}
static uint32_t mx_items_synth(mitem_t *it)
{
    uint32_t n = 0, k, ins = mx_insert(), snd = mx_send();
    n = mx_add(it, n, "VOLUME", MI_TRK, P_LEVEL);
    n = mx_add(it, n, ins == P_DIST && !FXS_ON(FXT_DIST) ? "DRIVE" : 0, ins == 0xFFu ? MI_NONE : MI_TRK, ins);
    n = mx_add(it, n, 0, snd == 0xFFu ? MI_NONE : MI_TRK, snd);
    n = mx_add(it, n, "PAN", MI_TRK, P_PAN);
    for (k = 0; k < FX_NSLOT; k++) {                    /* the rest: the other effects in their slots' order */
        uint32_t a = fxs_amt(k);
        if (a != 0xFFu && a != ins && a != snd && FXS_ON(fxs_slot[k]))
            n = mx_add(it, n, 0, MI_TRK, a);
    }
#if FELUCCA_TRK_FILT
    if (ins != P_TFLT && !FXS_ON(FXT_FILT))
        n = mx_add(it, n, "FILTER", MI_TRK, P_TFLT);
#endif
    n = mx_add(it, n, "FX ON", MI_TRK, P_FXOFF);
    return mx_add(it, n, "SOUND", MI_SOUND, 0);
}
static uint32_t mx_items_value(uint32_t r, mitem_t *it)   /* the row's value items (before the screens) */
{
    uint32_t n = 0;
    if (r == MXR_MASTER) {
        n = mx_add(it, n, "FILT", MI_GLOB, G_FILT);
#if FELUCCA_MASTER_COMP
        n = mx_add(it, n, "THRS", MI_GLOB, G_CTHR);
        n = mx_add(it, n, "RATIO", MI_GLOB, G_CRAT);
        n = mx_add(it, n, "DUCK", MI_GLOB, G_DUCK);
        n = mx_add(it, n, "DUST", MI_GLOB, G_DUST);
        n = mx_add(it, n, "GAIN", MI_GLOB, G_CGAIN);
        return mx_add(it, n, "CEIL", MI_GLOB, G_CCEIL);
#else
        n = mx_add(it, n, "DUST", MI_GLOB, G_DUST);
        return mx_add(it, n, "DUCK", MI_GLOB, G_DUCK);
#endif
    }
    if (r >= MXR_LANE0) {                               /* a lane: the sound's own values */
        n = mx_add(it, n, "LEVEL", MI_LANE, DE_LEVEL);
        n = mx_add(it, n, "DRIVE", FELUCCA_FX_DIST ? MI_LANE : MI_NONE, DE_DRIVE);
        n = mx_add(it, n, "REV", FELUCCA_FX_REVERB ? MI_LANE : MI_NONE, 16u);
        n = mx_add(it, n, "CUT", MI_LANE, DE_CUT);      /* (PAN's place: a sound has no pan) */
        n = mx_add(it, n, "DLY", FELUCCA_FX_DELAY ? MI_LANE : MI_NONE, 17u);
        n = mx_add(it, n, "CHO", FELUCCA_FX_CHORUS ? MI_LANE : MI_NONE, 18u);
        return mx_add(it, n, "SOUND", MI_SOUND, 0);
    }
    if (r == MXR_DR) {                                  /* the drum track: its level, FILTER, FX, the kit */
        n = mx_add(it, n, "VOLUME", MI_GLOB, G_DRLVL);
        n = mx_add(it, n, "FILTER", FELUCCA_TRK_FILT ? MI_TRK : MI_NONE, FIF(FELUCCA_TRK_FILT)(P_TFLT) + 0);
        n = mx_add(it, n, "FX ON", MI_TRK, P_FXOFF);
        return mx_add(it, n, "KIT", MI_SOUND, 0);
    }
    return mx_items_synth(it);
}
/* row r's items, in sets of four: its values only (the screens have their own gestures, op_input.c op_tap) */
static uint32_t mx_items(uint32_t r, mitem_t *it)
{
    uint32_t n = mx_pad(it, mx_items_value(r, it));
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
/* the set's name ("LEVELS", "MORE"; MASTER's "MASTER", "MASTER 2"): the header's "Mix levels" */
static void mix_name(uint32_t r, char *b)
{
    uint32_t s = mx_set(r % MXR_N), n = mx_sets(r % MXR_N);
    (void)n;
    if (r % MXR_N == MXR_MASTER)
        str_cpy(b, s ? "MASTER 2" : "MASTER", 12);
    else
        str_cpy(b, s ? "MORE" : "LEVELS", 12);
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
        if (fine && s != OP_RESET && r < MXR_LANE0) {
            op_preset_step(s);                          /* PRESETS browses the row's sounds (its track is selected) */
            pre_toast();
        }
        return;
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
    ui.force = 1;
}
static void mx_walk(int32_t d)                          /* ALGORITHM: a row a detent, stopping at the ends */
{
    int32_t r = clamp((int32_t)mx_row() + d, 0, MXR_N - 1);
    if ((uint32_t)r != mx_row())
        mx_apply((uint32_t)r);
}
static void mx_page(int32_t d, int round)               /* SELECT (round 0: stops), GLO again (round 1, stops too) */
{
    uint32_t n = mx_sets(mx_row()), s = mx_set(mx_row());
    if (round)
        s = s + 1u < n ? s + 1u : s;
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
