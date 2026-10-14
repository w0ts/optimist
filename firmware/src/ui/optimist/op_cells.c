/* SPDX-License-Identifier: GPL-3.0-only */
/* Cells and the rows made of today's pages (docs/UI-OPTIMIST-DESIGN.md sections 2, 4.3): a row is up to four
 * cells, a cell a value a knob turns (by its descriptor: core/params.c TP, GP, the engines' edit[], the drum lanes'
 * DSD), an action YES does, a row YES enters, or a read-out. The SOUND screen's rows are the selected track's
 * PAGES (params.c), in their order, as page_shown and page_for_drum leave them for this track; the drum track's are
 * the selected lane's (drums/dsnd_desc.c lane_sel). The FX screen's are the global effects' pages. */
enum { CK_NONE, CK_VAL, CK_RO, CK_ACT, CK_ENTER };
/* every value is drawn with a form beside its number (the user, 2026-10-08: "almost no place should have a number
 * value with no graph representation"), chosen from its range alone (op_draw.c draw_gauge):
 *   GK_BAR   unipolar, filled from the left          GK_BIP   bipolar (min < 0 < max), from the centre
 *   GK_DOTS  a list of up to 12 entries: one lit     GK_POS   a longer list: a tick at its place
 *   GK_PILL  on / off (two entries): filled or empty GK_NONE  text only (names, actions) */
enum { GK_NONE, GK_BAR, GK_BIP, GK_DOTS, GK_POS, GK_PILL };
#define OP_RESET 0x7FFF                 /* a turn's steps: the value back to its default (HOME held + the knob) */
#define OP_MAXROWS 40u
static int op_global_go(uint32_t id);                  /* op_screens.c: TOOLS' GO buttons */

typedef struct {
    const char *label;                  /* the card's label (0: an empty cell) */
    char val[14];                       /* the value as text */
    const char *unit;
    uint8_t kind;                       /* CK_* */
    uint16_t col;                       /* the value's colour, 0 = the palette's */
    const param_desc_t *d;              /* a value by its descriptor: what a turn steps */
    int16_t *vp;
    uint8_t mark;                       /* an event on the step held (STEP: op_step.c): 1 a lock, 2 a hold, a mark */
    uint8_t gk;                         /* its form (GK_*) over gmin..gmax, at gv */
    int16_t gmin, gmax, gv;
    uint8_t ak;                         /* the actual value's bar (op_effval.c, the cards only): its form, GK_NONE none */
    int16_t amin, amax, av;             /* ... over amin..amax, at av */
    uint8_t dim;                        /* drawn dim: a slot with no instance left (fxi.c, FULL / in use) */
    const char *hlp;                    /* a help key (op_help.c, ctx INSERTS) before the label's: the limited types */
    const char *obj;                    /* an action's object on its card ("PROJECT"; the user, 2026-10-13: the card shows the verb) */
    uint8_t danger;                     /* an action that destroys or replaces: its verb in the alarm colour */
    uint8_t dr;                         /* the drum track's own value, shown with a drum lane picked: drawn tagged "DR" (op_draw.c) */
} cell_t;

static void cell_clear(cell_t *c)
{
    memset(c, 0, sizeof *c);
    c->unit = "";
}
/* an action's card: the verb is its val when set, else its label (the list's narrow columns draw the label); the small
 * label above is obj, else the label over a val ("PANEL" over CALIBRATE), else none (op_draw.c act_text) */
static void cell_act(cell_t *c, const char *obj, const char *label, const char *verb, uint32_t danger)
{
    c->kind = CK_ACT;
    c->obj = obj;
    c->label = label;
    c->danger = (uint8_t)(danger != 0u);
    if (verb)
        str_cpy(c->val, verb, sizeof c->val);
}
/* recording is a thing that runs, not one that destroys (style guide 7: red only destroys or replaces): green while it
 * records, amber (pending) while it waits for the first note or the bar; the song's REC: 2 recording, 1 armed */
static uint16_t rec_col(uint32_t recording, uint32_t waiting) { return recording ? C_OK : waiting ? C_WARN : 0; }
static uint16_t srec_col(void) { return rec_col(srec == 2u, srec == 1u); }
static int is_go(const param_desc_t *d)             /* a GO button: YES does it, a turn does nothing */
{
#if BP_SET_ANY
    if (d->names == N_BPGO)                             /* (ACID GEN > GEN) */
        return 1;
#endif
    return d->fmt == F_ENUM && d->names == N_GO;
}
/* a GO button that destroys or replaces work is red, card and modal alike (style guide 7): every one but a SAVE (into a
 * free slot: neutral) and the ACID line's GEN (a new line, the undo keeps the old one) */
static uint32_t go_danger(const param_desc_t *d) { return !str_eq(d->label, "SAVE") && !str_eq(d->label, "GEN"); }
static int is_toggle(const param_desc_t *d){ return d->fmt == F_ONOFF || (d->fmt == F_ENUM && d->max - d->min == 1); }
/* the form of a value v in min..max: a list (enum) or a number, by its range */
static void cell_gauge(cell_t *c, uint32_t list, int32_t min, int32_t max, int32_t v)
{
    c->gmin = (int16_t)min;
    c->gmax = (int16_t)max;
    c->gv = (int16_t)clamp(v, min, max);
    c->gk = max <= min ? GK_NONE : list ? (max - min == 1 ? GK_PILL : max - min < 12 ? GK_DOTS : GK_POS)
                                        : min < 0 && max > 0 ? GK_BIP : GK_BAR;
}

/* the gauge of a parameter at v. LFO RATE (-LSYNC_N .. 127): a free rate (0 .. 127) is the bar from 0; a synced value
 * (< 0, a division: 4BAR slowest .. 1/32) is the list form, one of ten cells lit, left to right slow to fast */
static void cell_gauge_d(cell_t *c, const param_desc_t *d, int32_t v)
{
    if (!is_lrate(d) && d != &DLFO_DESC[DLF_RATE])   /* (a drum lane's LFO RATE: the same range, lane_play.c) */
        cell_gauge(c, d->fmt == F_ENUM || d->fmt == F_ONOFF, d->min, d->max, v);
    else if (v < 0)
        cell_gauge(c, 1, 0, LSYNC_N - 1, -v - 1);
    else
        cell_gauge(c, 0, 0, d->max, v);
}

/* the drum track's level (G_DRLVL, linear: drums.c drums_mix, a gain of v * 200 / 32768) in dB as a synth track's LEVEL
 * reads (params.c F_DB): the nearest of LEVEL_Q12's half-dB steps (gen_tables.py), "OFF" at 0. val holds 14 */
static void drlvl_db(int32_t v, char *val, const char **unit)
{
    int32_t g = v * 25, i, best = 1;                    /* (Q15 v * 200 is Q12 v * 25) */
    if (v <= 0) {
        str_cpy(val, "OFF", 14);
        *unit = "";
        return;
    }
    for (i = 1; i < 128; i++)
        if ((LEVEL_Q12[i] > g ? LEVEL_Q12[i] - g : g - LEVEL_Q12[i]) < (LEVEL_Q12[best] > g ? LEVEL_Q12[best] - g : g - LEVEL_Q12[best]))
            best = i;
    fmt_fix(val, LEVEL_DB_X10[best], 1);
    *unit = "dB";
}
/* a song value (G_*) this build has (params.c cell_built: DUST, DUCK, FILT, the compressor...): what is not built never
 * shows as a card, anywhere (the audit's #22: the master's DUST on the mixer, the FX layer) */
static int g_built(uint32_t id)
{
    static const page_t P = {"", FAM_GLO, SC_GLOBAL, GR_NONE, {0xFF, 0xFF, 0xFF, 0xFF}};
    return cell_built(&P, id);
}
/* a list entry that names nothing ("----" an empty FX slot, "--" an empty kit slot): the cell says EMPTY, dim (style
 * guide 5, the audit's #7: one word for a slot with nothing in it; a list cell shows it "-", op_names.c) */
static void cell_empty_slot(cell_t *c)
{
    if (c->val[0] == '-' && c->val[1] == '-' && (!c->val[2] || (c->val[2] == '-' && c->val[3] == '-' && !c->val[4]))) {
        str_cpy(c->val, "EMPTY", sizeof c->val);
        c->dim = 1;
    }
}
/* a cell from a descriptor and its value: none when the page does not show it (the knob's frame alone, no label, no
 * dash: style guide 6), an action for a GO button, a read-out for a value with no range (LIMIT > GR) */
static void cell_param(cell_t *c, const param_desc_t *d, int16_t *vp)
{
    cell_clear(c);
    if (!d || PARAM_HIDDEN(d) || !vp)
        return;
    c->label = d->label;
    c->d = d;
    c->vp = vp;
    if (is_go(d)) {
        const char *l = d->label;                       /* (the object and the verb a card shows; the label stays the list's) */
        cell_act(c, str_eq(l, "NEW") ? "PROJECT" : str_eq(l, "CLRSQ") ? "PATTERN" : str_eq(l, "INIT") ? "SOUND" :
                    str_eq(l, "GEN") ? "ACID LINE" : "KIT", l, str_eq(l, "CLRSQ") ? "CLEAR" : 0,
                 go_danger(d));
        return;
    }
    c->kind = d->max == d->min ? CK_RO : CK_VAL;
    param_format(d, *vp, c->val, &c->unit);
    {
        uint32_t z = c->val[0] == '-' || c->val[0] == '+';   /* (".24" -> "0.24": a leading zero, style guide 5) */
        if (c->val[z] == '.' && str_len(c->val) + 2u <= sizeof c->val) {
            memmove(c->val + z + 1u, c->val + z, str_len(c->val + z) + 1u);
            c->val[z] = '0';
        }
    }
    if (d->names == ins_names || d->names == fxs_names) {   /* an insert's type, a slot's: its name as the cards name it */
        char b[16];                                     /* (DST -> DIST, FILT -> FILTER, CMP -> COMP: op_names.c) */
        str_cpy(c->val, nm_card(c->val, 0, b, sizeof b), sizeof c->val);
    }
    if (d->fmt == F_ENUM)
        cell_empty_slot(c);
    if (d->fmt == F_STEPS)
        c->unit = "steps";                              /* (the style guide's unit: never "ST", "STEP") */
    else if (d->fmt == F_FILT)
        c->unit = "";                                   /* ("LP20": the side and how far, no unit after it) */
    else if (d == &GP[G_DRLVL])
        drlvl_db(*vp, c->val, &c->unit);                /* (the drum track's LEVEL in dB, as the synths': style guide 4) */
    else if (d >= DSEND_DESC && d < DSEND_DESC + 3) {   /* a lane's REV DLY CHO (0 .. 31): in %, as every send reads */
        fmt_int(c->val, (*vp * 100 + (int32_t)DSEND_MAX / 2) / (int32_t)DSEND_MAX);
        c->unit = "%";
    }
    if (d >= FXS_DESC && d < FXS_DESC + FX_NSLOT) {     /* a master FX slot: its role in the unit's place (the user, */
        uint32_t t = fxs_slot[d - FXS_DESC];            /* 2026-10-12: "slot 1 is insert, right ? it is not clear"): */
        c->unit = t == FXT_CHO || t == FXT_DLY || t == FXT_REV ? "SEND" : t == FXT_NONE ? "" : "INS";   /* by type */
    }
    cell_gauge_d(c, d, *vp);
}

/* the long form of a value where the field is wide (the GLOBAL / SYSTEM list, the tempo panel): SYNC AUTO as "Auto: USB",
 * the clock it follows now (G_MIDI), a fixed SYNC as "Int" / "USB" / "TRS"; MIDI OUT and IN as words ("Keys" "Seq", "Notes" "Clock"). The cards keep
 * param_format's 5 characters ("A:USB", "KEYS"). 1: b (14) holds it */
static int value_long(const param_desc_t *d, int32_t v, char *b)
{
    static const char *const CK[3] = {"Auto: Int", "Auto: USB", "Auto: TRS"};
    if (d == &GP[G_SYNC]) {                             /* a fixed clock: "Int", to match "Auto: Int" */
        static const char *const FX[3] = {"Int", "USB", "TRS"};
        str_cpy(b, v == SYNC_AUTO ? CK[(uint32_t)song.g[G_MIDI] % 3u] : FX[(uint32_t)v % 3u], 14);
        return 1;
    }
#if BP_SET_ANY
    if ((d->names == N_MOUT || d->names == N_MIN) && v >= 0 && v <= d->max) {   /* KEYS -> Keys */
        uint32_t i;
        str_cpy(b, d->names[v], 14);
        for (i = 1; b[i]; i++)
            b[i] = (char)(b[i] >= 'A' && b[i] <= 'Z' ? b[i] + 32 : b[i]);
        return 1;
    }
#endif
    return 0;
}

/* the sound a track plays, by name (b holds 14) */
static void snd_name(uint32_t c, char *b)
{
    const track_t *t = &trk[c % NTRK];
    const engine_t *e = ENGINES[t->eng_req % NENGINES];
    if (c == TRK_DRUM)
        str_cpy(b, drum_kit_name(), 14);
    else if (user_of(t) < UP_SLOTS)
        up_name(user_of(t), b);
    else
        str_cpy(b, preset_name(t->eng_req % NENGINES, eng_first_playable(e) == PRESET_INIT ? PRESET_INIT : t->preset), 14);
}

/* ---- the drum lanes' values: the selected lane (SLOOP's ui_drums.c edits the lane played last) */
/* the drum track recording (playing, its REC bit): a lane value's card shows the hold in force (the knob's motion, seq/
 * auto_lane.c lane_knob: heard through the overlay, the patch dl.ofs not written), so the next detent goes on from it,
 * as a synth card does from t->p (motion_knob); else the patch, as stored. dsv[id] already holds the patch */
static void dsv_rec(uint32_t l, uint32_t id)
{
#if DLA_ON && FELUCCA_MOTION
    if (id < DE_N && song.playing && ((song.rec >> TRK_DRUM) & 1u) && ((dla.hm[l % DRUM_LANES] >> id) & 1u))
        dsv[id] = dla.hv[l % DRUM_LANES][id];
#else
    (void)l, (void)id;
#endif
}
/* a lane knob set lane l's value id to v (the SOUND rows SOUND, SOUND 2, ENV; the mixer's LEVEL CUT DRIVE): REC on the
 * drum track while playing records it as motion (lane_knob: a hold event, heard at once); 1 done so (the patch stays),
 * 0 the caller writes the patch as ever (and a hold in force on it let go, so the knob is heard) */
static int lane_value_rec(uint32_t l, uint32_t id, int32_t v)
{
#if DLA_ON && FELUCCA_MOTION
    int rc;
    if (id >= DE_N)
        return 0;
    rc = lane_knob(l, id, v);
    if (rc == 2) {
        motion_full = 0;                                /* (lane_knob raised it: said here, as page_set does) */
        ui_message("MOTION FULL");
    }
    return rc == 1 || rc == 2;
#else
    (void)l, (void)id, (void)v;
    return 0;
#endif
}
/* a drum lane's LFO (drums/lane_play.c, the user's D6: one a lane): the LFO button with a lane selected (not DR itself)
 * opens LFO (RATE WAVE PHS FADE) and LFO DEST (DEST AMT DEST2 AMT2). This UI's pages, not PAGES' (SLOOP's UI has none,
 * and stays as it is): SC_DSND values DL_LFO0 + DLF_* (op_dsnd_desc), in the page lists as NPAGES + i (op_pg) */
#define LANE_LFO_UI (DLFO_ON && DL_UI)
#define DL_LFO0 24u
#if LANE_LFO_UI
static const page_t LANE_LFO_PG[2] = {{"LFO", FAM_LFO, SC_DSND, GR_LFO, {24, 25, 26, 27}},
                                      {"LFO DEST", FAM_LFO, SC_DSND, GR_NONE, {28, 29, 30, 31}}};
static int16_t dlfv[DLF_N];                             /* the cells' values (dlfo_field: the lane's LFO, 8 bytes) */
static int lane_lfo_pg(const page_t *pg) { return pg == &LANE_LFO_PG[0] || pg == &LANE_LFO_PG[1]; }
#define LFO_PGN 2u
#else
#define lane_lfo_pg(pg) 0
#define LFO_PGN 0u
#endif
/* a picked lane's FX family (the user, 2026-10-14: "on a lane i want to see the lane"): its INSERTS (the sound's own
 * DRIVE, DST, CMP) and SENDS (its REV DLY CHO, those the master slots hold: LSEND0 + k, mapped by lane_send_vid), in place
 * of the drum track's, which stay as "DR INSERT", "DR SENDS" and the rest, tagged DR (cell_t dr). Never on DR itself */
#define LSEND0 0x40u
#if DL_UI
static const page_t LANE_FX_PG[2] = {{"INSERTS", FAM_FX, SC_DSND, GR_DSND, {19, 20, DE_DRIVE, 0xFF}},
                                     {"SENDS", FAM_FX, SC_DSND, GR_DSND, {LSEND0, LSEND0 + 1, LSEND0 + 2, 0xFF}}};
static int lane_fx_pg(const page_t *pg) { return pg == &LANE_FX_PG[0] || pg == &LANE_FX_PG[1]; }
#define FX_PGN 2u
#else
#define lane_fx_pg(pg) 0
#define FX_PGN 0u
#endif
#define OP_NPG (NPAGES + LFO_PGN + FX_PGN)
/* a drum lane's SRC is the SOUND row's knob 3 (the user, 2026-10-13: "this should be on third knob on page 1"): LANE_SRC_PG
 * is that cell (snd_cell, never a row of its own). PAGES' SOURCE (SRC HIT START LEN) is then only HIT START LEN, a user
 * sample's: this UI shows it as LANE_SMP_PG, on a lane playing one, and on no other lane (SLOOP's UI keeps PAGES' as it is) */
static const page_t LANE_SRC_PG = {"SOUND", FAM_EDIT, SC_DSND, GR_DSND, {8, 0xFF, 0xFF, 0xFF}};
static const page_t LANE_SMP_PG = {"SOURCE", FAM_EDIT, SC_DSND, GR_DSND, {9, 10, 11, 0xFF}};
static int src_page(const page_t *pg) { return pg->scope == SC_DSND && pg->fam == FAM_EDIT && pg->id[0] == 8u; }   /* PAGES' SOURCE */
static int smp_pg(const page_t *pg) { return pg == &LANE_SMP_PG; }
static uint32_t src_page_ix(void)                       /* PAGES' SOURCE (the breadcrumb of LANE_SMP_PG) */
{
    uint32_t i;
    for (i = 0; i < NPAGES; i++)
        if (src_page(&PAGES[i]))
            return i;
    return 0;
}
/* one-card pages merged into a neighbour (the audit's #21; SLOOP's UI keeps PAGES' as they are):
 *   FLT 2 (ANALOG's FTYP alone) on OSC 2's free card: OSC 2 is OP_OSC2 here, FLT 2 no row (merged_away)
 *   FILTER (the track filter's amount alone) no row: INSERTS shows the same value, the amount card of the slot holding it */
#if FELUCCA_ANALOG2
static const page_t OP_OSC2 = {"OSC 2", FAM_EDIT, SC_TRACK, GR_NONE, {P_A2WAVE, P_A2SEMI, P_A2SYNC, P_A2FTYP}};
#define OSC2_PAGE(pg) ((pg)->scope == SC_TRACK && (pg)->id[0] == P_A2WAVE)
#else
#define OSC2_PAGE(pg) 0
#endif
static int merged_away(const page_t *pg)
{
    if (pg->scope != SC_TRACK)
        return 0;
#if FELUCCA_ANALOG2
    if (pg->id[0] == P_A2FTYP)
        return 1;
#endif
#if FELUCCA_TRK_FILT
    if (pg->id[0] == P_TFLT)
        return 1;
#endif
    return 0;
}
static const page_t *op_pg(uint32_t i)
{
#if FELUCCA_ANALOG2
    if (i < NPAGES && OSC2_PAGE(&PAGES[i]))
        return &OP_OSC2;
#endif
    if (i < NPAGES)
        return src_page(&PAGES[i]) ? &LANE_SMP_PG : &PAGES[i];
    i -= NPAGES;
#if LANE_LFO_UI
    if (i < LFO_PGN)
        return &LANE_LFO_PG[i & 1u];
    i -= LFO_PGN;
#endif
#if DL_UI
    if (i < FX_PGN)
        return &LANE_FX_PG[i & 1u];
#endif
    return &PAGES[0];
}
/* the order the rows come in: a picked lane's INSERTS and SENDS just before PAGES' INSERTS (the FX family opens on the
 * lane's own), the lane's LFO last, every other page as PAGES has them. j: the place in the order, the result an op_pg index */
static uint32_t op_pg_at(uint32_t j)
{
#if DL_UI
    uint32_t ins = 0, i;
    for (i = 0; i < NPAGES; i++)
        if (PAGES[i].graph == GR_INS)
            ins = i;
    if (j >= ins && j < ins + FX_PGN)
        return NPAGES + LFO_PGN + (j - ins);
    if (j >= ins + FX_PGN && j < NPAGES + FX_PGN)
        return j - FX_PGN;
    if (j >= NPAGES + FX_PGN)
        return NPAGES + (j - NPAGES - FX_PGN);
#endif
    return j;
}
_Static_assert(OP_NPG < 0xFFu, "the page lists' indexes: a byte");
/* a lane's value id of SENDS' card k (REV, DLY, CHO), 0xFF none (no master slot holds it): REV 16, DLY 17, CHO 18 */
static uint32_t lane_send_vid(uint32_t k)             /* (REV DLY CHO, the one order: a type no slot holds, none) */
{
    static const uint8_t T[3] = {FXT_REV, FXT_DLY, FXT_CHO};
    uint32_t i;
    for (i = 0; k < 3u && i < 3u; i++)
        if (fxs_send(i) == T[k])
            return 16u + k;
    return 0xFFu;
}
static const param_desc_t *op_dsnd_desc(uint32_t id, int16_t **vp)
{
    const param_desc_t *d;
#if LANE_LFO_UI
    if (id >= DL_LFO0 && id < DL_LFO0 + DLF_N) {
        dlfv[id - DL_LFO0] = (int16_t)dlfo_field(lane_selected(), id - DL_LFO0);
        *vp = &dlfv[id - DL_LFO0];
        return &DLFO_DESC[id - DL_LFO0];
    }
#endif
    if (!lane_snd()) {                                  /* the ACCENT lane has no sound: no value (its pages are not shown) */
        *vp = 0;
        return 0;
    }
    if (id >= LSEND0) {                                 /* SENDS: card k's send (REV DLY CHO), this lane's */
        id = lane_send_vid(id - LSEND0);
        if (id == 0xFFu) {
            *vp = 0;
            return 0;
        }
    }
    d = dsnd_desc_lane(lane_selected(), id, vp);
    if (d)
        dsv_rec(lane_selected(), id & 31u);
    return d;
}
static const param_desc_t *(*dsnd_desc_fn)(uint32_t id, int16_t **vp) = op_dsnd_desc;   /* (params.c page_desc) */

static void op_dsnd_set(uint32_t id, int32_t v, int32_t steps)   /* as ui_drums.c dsnd_set, without its confirm */
{
    uint32_t l = lane_selected();
    uint8_t *r = dl.ref[l % DRUM_LANES];
    if (!lane_snd())
        return;                                         /* (the ACCENT lane: no sound to set) */
    if (id >= LSEND0 && (id = lane_send_vid(id - LSEND0)) == 0xFFu)
        return;
#if LANE_LFO_UI
    if (id >= DL_LFO0) {                                /* the lane's LFO */
        dlfo_field_set(l, id - DL_LFO0, v);
        return;
    }
#endif
    if (id >= 16u) {                                    /* SOUND 3: the sends */
        dsend_set(l, id - 16u, v);
        return;
    }
    if (id < DE_N) {
        dl.ofs[l][id] = (int8_t)v;
        return;
    }
    switch (id) {
    case 8:                                             /* SRC (without USR: past USR1..3) */
        if (!FELUCCA_DRUM_USR && v >= 1 && v <= 3)
            v = steps > 0 ? 4 : 0;
        if (!FELUCCA_DRUM_KITS && v > 3)
            v = 3;
        dl.src[l] = (uint8_t)dsnd_idx_src((uint32_t)v);
        ui.force = 1;
        break;
    case 9:
        dl_set_ref(r, (uint32_t)v - 1u, dl_start(r), dl_len(r));
        break;
    case 10:
        dl_set_ref(r, dl_hit(r), (uint32_t)v << 3, dl_len(r));
        break;
    case 11:
        dl_set_ref(r, dl_hit(r), dl_start(r), ((uint32_t)v + 1u) << 3);
        break;
    case 12:
        dsnd_slot = (uint8_t)v;
        break;
    default:
        break;
    }
}
/* once a frame (as ui_drums.c dsnd_tick): another kit set elsewhere drops a user kit's lanes; user samples' states */
static void op_dsnd_tick(void)
{
    if (TDRUM->p[P_E0] != dl_e0) {
        if (dl.ukit && dl_e0 >= 0)
            dl_reset_lanes();
        dl_e0 = TDRUM->p[P_E0];
    }
    dl_tick();
}

/* ---- rows made of PAGES */
#define CMP_PAGE(pg) ((pg)->scope == SC_FXSLOT && (pg)->id[0] >= FX_NSLOT)   /* FX > CMP: the track COMP's RATIO ATK REL */
static int sound_page(const page_t *pg)              /* a row of the SOUND screen: the track's sound and pattern */
{
    if (pg->scope == SC_DSND && is_drum(TSEL) && lane_acc())
        return 0;                                       /* (the ACCENT lane has no sound: no SOUND, SOURCE, KIT, ENV or LFO pages) */
    if (merged_away(pg))
        return 0;                                       /* (FLT 2, FILTER: their card is on a neighbour, #21) */
    if (is_drum(TSEL) && lane_picked() && lane_snd() && pg->fam == FAM_FX && (pg->scope == SC_TRACK || CMP_PAGE(pg)))
        return 0;                                       /* (a lane is uniform with the other tracks: its INSERTS and SENDS only, not the drum
                                                         * track's kit sends, bus sends, slicer, gater or comp; the user 2026-10-14) */
    if (lane_lfo_pg(pg))                                /* (a lane's LFO: a lane selected, never DR itself) */
        return is_drum(TSEL) && lane_picked();
    if (lane_fx_pg(pg))                                 /* (a lane's INSERTS and SENDS: a lane selected, never DR itself) */
        return is_drum(TSEL) && lane_picked() && page_shown(pg);
    if (smp_pg(pg))                                     /* (a user sample's HIT START LEN: that lane only) */
        return is_drum(TSEL) && page_shown(pg) && dl_usr_of(lane_selected());
    if (pg->scope == SC_DSND && pg->fam == FAM_ENV && is_drum(TSEL) && !lane_picked())
        return 0;                                       /* (the lane's envelope: DR itself has none, a lane must be picked) */
    if (pg->scope != SC_TRACK && pg->scope != SC_ENGINE && pg->scope != SC_DSND && !CMP_PAGE(pg)   /* (CMP: the track's own COMP) */
#if BP_SET_ANY
        && !(pg->scope == SC_BPSET && pg->fam == FAM_EDIT)   /* (ACID GEN; REVERB TYPE is the FX screen's) */
#endif
    )
        return 0;
    return page_shown(pg) && (!is_drum(TSEL) || page_for_drum(pg));
}
/* a row of the master FX screen, MASTER: the global effects, the master. Not PAGES' GLOBAL (BPM SWING CLICK TUNE: the
 * first three are GLOBAL's TEMPO and REC rows, TUNE is on LIMIT, op_screens.c) nor DRUMS (the level: the mixer's DR row):
 * the user, 2026-10-12, D3 */
static int fx_page(const page_t *pg)
{
    if (!page_shown(pg) || pg->fam == FAM_SAVE ||
        (pg->scope == SC_GLOBAL && pg->fam == FAM_GLO && (pg->id[0] == G_BPM || pg->id[0] == G_DRLVL)))
        return 0;
    return (pg->scope == SC_GLOBAL && (pg->fam == FAM_FX || pg->fam == FAM_GLO)) || (pg->scope == SC_FXSLOT && !CMP_PAGE(pg))
#if BP_SET_ANY
           || (pg->scope == SC_BPSET && pg->fam == FAM_FX)
#endif
#if FELUCCA_MACROS
           || pg->scope == SC_MACRO
#endif
        ;
}
/* the PAGES indexes of a screen's page rows, in their order (past NPAGES: this UI's own, op_pg); returns how many */
static uint32_t page_rows(int (*want)(const page_t *), uint8_t *ix)
{
    uint32_t i, j, n = 0;
    for (j = 0; j < OP_NPG && n < OP_MAXROWS; j++) {
        i = op_pg_at(j);
        if (want(op_pg(i)))
            ix[n++] = (uint8_t)i;
    }
    return n;
}

/* the limited insert types' instances (fxi.c, main loop) on a slot's TYPE cell, track k's slot s: the free count beside
 * the name ("CHRS 2/4": free of available: built plus lent), and a slot with none left drawn dim as "S1 CHRS" over FULL (bypassed; "IN USE"
 * the same pass the type is picked, FXI_WAIT with fxi_can 0, before the main loop marks it FULL), "WAIT" while the
 * next pass hands one out (a pass: not seen). A type on every track (cap FXI_ANY) or not limited: unchanged */
static void cell_ins_note(cell_t *c, uint32_t k, uint32_t s)
{
    static char lab[2][8], un[2][4];
    uint32_t t = ins_slot[k % NTRK][s & 1u], cap = fxi_cap(t), st = fxi_state(k, s), a = t < FXT_N ? FXT_AMT[t] : 0xFFu;
    const char *v = 0;
    if (a == 0xFFu || cap == 0u || cap == FXI_ANY || st == FXI_NA)
        return;
    c->hlp = t == FXT_CHRS ? "CHRS" : "ECHO";
    if (st == FXI_OK) {
        un[s & 1u][0] = (char)('0' + fxi_free(t)), un[s & 1u][1] = '/', un[s & 1u][2] = (char)('0' + cap), un[s & 1u][3] = 0;
        c->unit = un[s & 1u];
        return;
    }
    if (st == FXI_WAIT && fxi_can(k, t))
        v = "WAIT";
    else {
        v = st == FXI_FULL ? "FULL" : "IN USE";
        c->dim = 1;
        c->hlp = t == FXT_CHRS ? "FULL CHRS" : "FULL ECHO";
    }
    str_cpy(lab[s & 1u], s ? "S2 " : "S1 ", sizeof lab[0]);
    str_cpy(lab[s & 1u] + 3, TP[a].label, sizeof lab[0] - 3u);
    c->label = lab[s & 1u];
    str_cpy(c->val, v, sizeof c->val);
    c->gk = GK_NONE;
}

/* with a drum lane picked, a value of the drum track itself (the user, 2026-10-14: "remember on a lane i want to see the
 * lane"): the track's pages (its bus inserts and sends, FILTER, SLICER, GATER, COMP, PATTERN) and the KIT page's slot
 * actions have no lane of their own, and are tagged DR on the card so they are not taken for the lane's. DR itself: no tag */
static int dr_value(const page_t *pg, uint32_t k)
{
    if (!is_drum(TSEL) || !lane_picked())
        return 0;
    if (pg->scope == SC_TRACK || CMP_PAGE(pg))
        return 1;
    return pg->scope == SC_DSND && pg->fam == FAM_EDIT && pg->id[0] == 12u && k < 3u;   /* KIT: SLOT SAVE ERASE (RESET is the lane's) */
}
static void page_cell(const page_t *pg, uint32_t k, cell_t *c)
{
    int16_t *vp;
    uint32_t id = page_id(pg, k);
    cell_param(c, page_desc(pg, k, &vp), vp);
    if (id >= PID_INST && id < PID_INST + INS_N)        /* INSERTS: S1 / S2 TYPE (the selected track's) */
        cell_ins_note(c, song.sel, id - PID_INST);
    c->dr = (uint8_t)(c->label && c->label[0] && dr_value(pg, k));
}

/* page pg's value k set to v (steps: the turn that set it): what else a value set by a knob does, as SLOOP's
 * ui_input.c edit_param (motion recording, an FX slot's type, a drum lane's value) */
static void page_set(const page_t *pg, uint32_t k, int16_t *vp, int32_t v, int32_t steps)
{
    uint32_t id = page_id(pg, k);
    *vp = (int16_t)v;
    if (id >= PID_INST && id < PID_INST + INS_N) {      /* INSERTS: S1 / S2 TYPE (the other slot's type swaps them) */
        ins_load(song.sel, id - PID_INST, ins_list[v % INS_NLIST]);
        return;
    }
#if FELUCCA_MOTION
    if ((pg->scope == SC_TRACK || pg->scope == SC_ENGINE) && vp >= TSEL->p && vp < TSEL->p + P_COUNT) {   /* recording: a step event (motion.c) */
        motion_knob(TSEL, (uint32_t)(vp - TSEL->p), v);
        if (motion_full) {
            motion_full = 0;
            ui_message("MOTION FULL");
        }
    }
#endif                                                  /* (a MACRO's motion: SLOOP's macro_ui.c, not here yet) */
    if (pg->scope == SC_FXSLOT && id < FX_NSLOT)        /* FX > SLOTS: load the type (a type held elsewhere swaps) */
        fxs_load(id, fxs_list[v]);
    if (pg->scope == SC_DSND && !lane_value_rec(lane_selected(), id, v))
        op_dsnd_set(id, v, steps);
}

/* LEN, coarse (the user, 2026-10-08): a detent to the next / previous of 1 2 4 8 16 32 64 within min..max; from a
 * value off the list, the next list value in the turn's direction (12: up 16, down 8) */
static int32_t len_pow2(int32_t v, int32_t s, int32_t min, int32_t max)
{
    int32_t p;
    for (; s > 0; s--) {
        for (p = 1; p <= v; p <<= 1)
            ;
        v = p > max ? max : p;
    }
    for (; s < 0; s++) {
        for (p = 1; p * 2 < v; p <<= 1)
            ;
        v = p < min ? min : p;
    }
    return v;
}
static int len_cell(const param_desc_t *d) { return d == &TP[P_SLEN]; }   /* (the PATTERN row's LEN: SOUND, STEP) */

/* a knob (fine: PRESETS, one unit a detent) or OP_RESET on page pg's value k. LEN moves in powers of two unless
 * fine or SHIFT (ui.shift, the LFO button held): then by one; every other value as ever */
static void page_turn(const page_t *pg, uint32_t k, int32_t s, int fine)
{
    int16_t *vp;
    const param_desc_t *d = page_desc(pg, k, &vp);
    uint32_t id = page_id(pg, k);
    if (!d || !vp || PARAM_HIDDEN(d) || is_go(d) || d->max == d->min)
        return;
#if FELUCCA_MISSING_WARN
    if (pg->scope == SC_GLOBAL && id == G_MISS) {       /* TOOLS > MISS: the items one by one (miss.c) */
        if (s != OP_RESET)
            miss_knob(s);
        return;
    }
#endif
    if (s == OP_RESET) {
        if (pg->scope == SC_FXSLOT || (id >= PID_INST && id < PID_INST + INS_N) || (pg->scope == SC_DSND && id >= 8u && id < 16u))
            return;                                     /* (a slot's type, a lane's source: no default to go back to) */
        page_set(pg, k, vp, d->def, 0);
        return;
    }
    if (len_cell(d))
        page_set(pg, k, vp, fine || ui.shift ? clamp(*vp + s, d->min, d->max) : len_pow2(*vp, s, d->min, d->max), s);
    else
        page_set(pg, k, vp, param_step(d, *vp, fine ? s : accel(EN_K1 + k, s, accel_range(d))), s);
}

/* the ACCENT amount (core.h P_DACC), the drum track's own value (not a lane's: the SOUND row's fourth card on every
 * lane, the mixer's ACCENT row): its card and its turn, which records as motion as the track's values do */
static void acc_cell(cell_t *c) { cell_param(c, &DRUM_ACC_DESC, &TDRUM->p[P_DACC]); }
static void acc_turn(uint32_t k, int32_t s, int fine)
{
    const param_desc_t *d = &DRUM_ACC_DESC;
    int32_t v = s == OP_RESET ? d->def : param_step(d, TDRUM->p[P_DACC], fine ? s : accel(EN_K1 + k, s, accel_range(d)));
    TDRUM->p[P_DACC] = (int16_t)v;
#if FELUCCA_MOTION
    motion_knob(TDRUM, P_DACC, v);
    if (motion_full) {
        motion_full = 0;
        ui_message("MOTION FULL");
    }
#endif
}

/* YES on page pg's value k: toggle an on / off value, do a GO button (ok: confirmed; the destructive ones ask
 * first). Returns 1 when it did or asked something */
static int page_yes(uint32_t scr, uint32_t row, const page_t *pg, uint32_t k, uint32_t ok)
{
    int16_t *vp;
    const param_desc_t *d = page_desc(pg, k, &vp);
    uint32_t id = page_id(pg, k);
    if (!d || !vp || PARAM_HIDDEN(d))
        return 0;
    if (!is_go(d)) {
        if (!is_toggle(d) || d->max == d->min)
            return 0;
        page_set(pg, k, vp, *vp == d->max ? d->min : d->max, 1);
        return 1;
    }
    if (!ok) {                                          /* every GO here asks (INIT: over an unedited sound too) */
        char kit_arg[8] = "KIT ";
        const char *arg = trk_tag(song.sel);            /* "CLRSQ T2?", "INIT T2?" */
        if (pg->scope == SC_DSND)                       /* "RESET SNARE?"; SAVE / ERASE name the user kit: "SAVE KIT 3?" */
            arg = id == 15u ? LANE_NAME[lane_selected()] : (fmt_int(kit_arg + 4, (int32_t)dsnd_slot + 1), kit_arg);
        else if (pg->scope == SC_GLOBAL && id == G_NEWPRJ)
            arg = "";                                   /* "NEW?" */
        op_arm(scr, row, k, d->label, arg, go_danger(d));
        return 1;
    }
    if (pg->scope == SC_DSND) {
        uint32_t l = lane_selected();
        if (id == 15u) {                                /* RESET: the sound as the kit has it */
            fm1_irq_off();
            memset(dl.ofs[l], 0, sizeof dl.ofs[l]);
            dl.src[l] = DL_KIT;
            memset(dl.ref[l], 0, sizeof dl.ref[l]);
            dsend[l] = 0;
            fm1_irq_on();
            ui_say("RESET ", LANE_NAME[l]);
        } else {
            ukit_ui(id - 13u, dsnd_slot);               /* 13 SAVE, 14 ERASE the user kit slot (drum_kits.c) */
        }
        ui.force = 1;
        return 1;
    }
#if FELUCCA_ENG_ACID
    if (pg->scope == SC_BPSET && id == BPS_GGO) {       /* ACID GEN > GO: a new line */
        undo_mark(TSEL, (undo_sess += 4u) | 3u);
        fm1_irq_off();
        acid_generate(TSEL, (uint32_t)bp_set[BPS_GDENS], (uint32_t)bp_set[BPS_GACC], (uint32_t)bp_set[BPS_GSLD],
                      fm1_ms * 2654435761u ^ rng());
        fm1_irq_on();
        ui_message("NEW LINE");
        return 1;
    }
#endif
    return op_global_go(id);                            /* TOOLS: op_screens.c */
}
