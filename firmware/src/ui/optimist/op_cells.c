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
    uint8_t mark;                       /* a lock on the step held (STEP: op_step.c), drawn as a mark on its card */
    uint8_t gk;                         /* its form (GK_*) over gmin..gmax, at gv */
    int16_t gmin, gmax, gv;
} cell_t;

static void cell_clear(cell_t *c)
{
    memset(c, 0, sizeof *c);
    c->unit = "";
}
static int is_go(const param_desc_t *d)              /* a GO button: YES does it, a turn does nothing */
{
#if BP_SET_ANY
    if (d->names == N_BPGO)                             /* (ACID GEN > GEN) */
        return 1;
#endif
    return d->fmt == F_ENUM && d->names == N_GO;
}
static int is_toggle(const param_desc_t *d) { return d->fmt == F_ONOFF || (d->fmt == F_ENUM && d->max - d->min == 1); }
/* the form of a value v in min..max: a list (enum) or a number, by its range */
static void cell_gauge(cell_t *c, uint32_t list, int32_t min, int32_t max, int32_t v)
{
    c->gmin = (int16_t)min;
    c->gmax = (int16_t)max;
    c->gv = (int16_t)clamp(v, min, max);
    c->gk = max <= min ? GK_NONE : list ? (max - min == 1 ? GK_PILL : max - min < 12 ? GK_DOTS : GK_POS)
                                        : min < 0 && max > 0 ? GK_BIP : GK_BAR;
}

/* a cell from a descriptor and its value: "-" when the page does not show it, an action for a GO button, a
 * read-out for a value with no range (LIMIT > GR) */
static void cell_param(cell_t *c, const param_desc_t *d, int16_t *vp)
{
    cell_clear(c);
    if (!d)
        return;
    if (PARAM_HIDDEN(d) || !vp) {
        c->label = "";
        str_cpy(c->val, "-", sizeof c->val);
        return;
    }
    c->label = d->label;
    c->d = d;
    c->vp = vp;
    if (is_go(d)) {
        c->kind = CK_ACT;
        return;
    }
    c->kind = d->max == d->min ? CK_RO : CK_VAL;
    param_format(d, *vp, c->val, &c->unit);
    cell_gauge(c, d->fmt == F_ENUM || d->fmt == F_ONOFF, d->min, d->max, *vp);
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
static const param_desc_t *op_dsnd_desc(uint32_t id, int16_t **vp) { return dsnd_desc_lane(lane_selected(), id, vp); }
static const param_desc_t *(*dsnd_desc_fn)(uint32_t id, int16_t **vp) = op_dsnd_desc;   /* (params.c page_desc) */

static void op_dsnd_set(uint32_t id, int32_t v, int32_t steps)   /* as ui_drums.c dsnd_set, without its confirm */
{
    uint32_t l = lane_selected();
    uint8_t *r = dl.ref[l];
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
static int sound_page(const page_t *pg)              /* a row of the SOUND screen: the track's sound and pattern */
{
    if (pg->scope != SC_TRACK && pg->scope != SC_ENGINE && pg->scope != SC_DSND
#if BP_SET_ANY
        && !(pg->scope == SC_BPSET && pg->fam == FAM_EDIT)   /* (ACID GEN; REVERB TYPE is the FX screen's) */
#endif
    )
        return 0;
    return page_shown(pg) && (!is_drum(TSEL) || page_for_drum(pg));
}
static int fx_page(const page_t *pg)                 /* a row of the FX screen: the global effects, the master */
{
    if (!page_shown(pg) || pg->fam == FAM_SAVE)
        return 0;
    return (pg->scope == SC_GLOBAL && (pg->fam == FAM_FX || pg->fam == FAM_GLO)) || pg->scope == SC_FXSLOT
#if BP_SET_ANY
           || (pg->scope == SC_BPSET && pg->fam == FAM_FX)
#endif
#if FELUCCA_MACROS
           || pg->scope == SC_MACRO
#endif
        ;
}
/* the PAGES indexes of a screen's page rows, in their order; returns how many */
static uint32_t page_rows(int (*want)(const page_t *), uint8_t *ix)
{
    uint32_t i, n = 0;
    for (i = 0; i < NPAGES && n < OP_MAXROWS; i++)
        if (want(&PAGES[i]))
            ix[n++] = (uint8_t)i;
    return n;
}

static void page_cell(const page_t *pg, uint32_t k, cell_t *c)
{
    int16_t *vp;
    cell_param(c, page_desc(pg, k, &vp), vp);
}

/* page pg's value k set to v (steps: the turn that set it): what else a value set by a knob does, as SLOOP's
 * ui_input.c edit_param (motion recording, an FX slot's type, a drum lane's value) */
static void page_set(const page_t *pg, uint32_t k, int16_t *vp, int32_t v, int32_t steps)
{
    uint32_t id = page_id(pg, k);
    *vp = (int16_t)v;
#if FELUCCA_MOTION
    if (pg->scope == SC_TRACK || pg->scope == SC_ENGINE) {   /* recording: a step event (motion.c) */
        motion_knob(TSEL, (uint32_t)(vp - TSEL->p), v);
        if (motion_full) {
            motion_full = 0;
            ui_message("MOTION FULL");
        }
    }
#endif                                                  /* (a MACRO's motion: SLOOP's macro_ui.c, not here yet) */
    if (pg->scope == SC_FXSLOT && id < FX_NSLOT)        /* FX > SLOTS: load the type (a type held elsewhere swaps) */
        fxs_load(id, fxs_list[v]);
    if (pg->scope == SC_DSND)
        op_dsnd_set(id, v, steps);
}

/* a knob (fine: PRESETS, one unit a detent) or OP_RESET on page pg's value k */
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
        if (pg->scope == SC_FXSLOT || (pg->scope == SC_DSND && id >= 8u && id < 16u))
            return;                                     /* (a slot's type, a lane's source: no default to go back to) */
        page_set(pg, k, vp, d->def, 0);
        return;
    }
    page_set(pg, k, vp, param_step(d, *vp, fine ? s : accel(EN_K1 + k, s, accel_range(d))), s);
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
    if (!ok) {                                          /* every GO here destroys something: ask */
        const char *arg = trk_tag(song.sel);            /* "CLRSQ T2?", "INIT T2?" */
        if (pg->scope == SC_DSND)
            arg = id == 15u ? LANE_NAME[lane_selected()] : "";   /* "RESET SNARE?", "SAVE?" */
        else if (pg->scope == SC_GLOBAL && id == G_NEWPRJ)
            arg = "";                                   /* "NEW?" */
        op_arm(scr, row, k, d->label, arg, 1);
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
