/* SPDX-License-Identifier: GPL-3.0-only */
/* The screens as lists of rows (docs/UI-OPTIMIST-DESIGN.md sections 4.1, 4.3, 4.4, 4.6, 4.7). A screen gives its
 * rows' count, names and cells, and what a knob, PRESETS and YES do on them; op_input.c and op_draw.c know nothing
 * else of a screen.
 *   HOME (MIX)  the mixer: the columns are the tracks (KNOB k = track k, T1 T2 T3 DR) on every row but MASTER
 *   SOUND       the selected track's sound: the SOUND row (preset, engine, INIT, SAVE AS), then its pages
 *   FX          the global effects and the master: the FX and GLO pages
 *   PROJECT     the project slots, snapshots, user presets, TOOLS, SLOOP 2.4's autosave
 *   SYSTEM      the settings of the FM-1 (SLOOP's HOME-held menu), the calibration, ABOUT */
typedef struct {
    uint32_t (*rows)(void);
    void (*name)(uint32_t r, char *b);                  /* the row's name (b holds 12) */
    void (*cell)(uint32_t r, uint32_t k, cell_t *c);
    void (*turn)(uint32_t r, uint32_t k, int32_t s, int fine);   /* a knob, PRESETS (fine) or OP_RESET */
    int (*yes)(uint32_t r, uint32_t k, uint32_t ok);    /* YES on the row, k the hot cell, ok: confirmed */
} screen_t;
static void op_enter(uint32_t scr);                     /* op_input.c */

/* ---- shared actions */
/* a new project: every track empty, the default sounds (as SLOOP's ui_input.c project_new, TOOLS > NEW) */
static void op_project_new(void)
{
    uint32_t i, sess = (undo_sess += 4u) | 3u;          /* (one session: the history undoes NEW at once) */
    transport_req = 2;
    panic_req = (1u << NTRK) - 1u;
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        undo_mark(t, sess);
        fm1_irq_off();
        track_defaults(t);
        if (i < NPART) {
            set_engine_of(t, trk_def_engine(i));
            apply_preset_to(t, trk_def_preset(i));
        }
        fm1_irq_on();
    }
    TDRUM->p[P_E0] = DRUM_DEFAULT_KIT;
    for (i = 0; i < G_COUNT; i++)
        if (i != G_SLOT && i != G_DRCH && i != G_VIEW && i != G_SYNC)
            song.g[i] = GP[i].def;
    song.solo = 0;
    song.octave = 0;
#if BP_SET_ANY
    bps_defaults();
#endif
    rev_defaults();
    sync_reload = 1;
    ui.force = 1;
}
static void op_clear_track(uint32_t i)                  /* HOME + REC, YES: the track's pattern (undo brings it back) */
{
    track_t *t = &trk[i % NTRK];
    undo_mark(t, (undo_sess += 4u) | 3u);
    fm1_irq_off();
    track_defaults_steps(t);
    t->nheld = 0;                                       /* and the latched arp chord */
    t->arp_phys = 0;
    fm1_irq_on();
    ui_say(trk_tag(i), " CLEARED");
    ui.force = 1;
}
/* TOOLS' GO buttons (op_cells.c page_yes, confirmed) */
static int op_global_go(uint32_t id)
{
    switch (id) {
    case G_CLRSEQ:
        op_clear_track(song.sel);
        return 1;
    case G_INITSND:
        set_engine(TSEL->eng_req);                      /* the engine's defaults and its first preset */
        ui_message("SOUND INIT");
        ui.force = 1;
        return 1;
    case G_NEWPRJ:
        op_project_new();
        ui_message("NEW PROJECT");
        return 1;
    default:
        return 0;
    }
}
/* list index n of the preset list (model.c BANK, the user presets) into the selected track */
static void op_preset_go(uint32_t n)
{
    uint32_t k, e = preset_at(n, &k);
    if (is_drum(TSEL))
        return;
    if (e == NENGINES) {
        up_load(k);
        return;
    }
#if FELUCCA_NATIVE_BANKS
    if (e == NB_LIST) {
        nb_load(k);
        return;
    }
#endif
    if (e != TSEL->eng_req)
        set_engine(e);
    apply_preset(k);
    ui.force = 1;
}
static void op_preset_step(int32_t s)                   /* the next / previous sound: one a detent */
{
    uint32_t total, cur;
    if (is_drum(TSEL)) {
        drum_kit_step(s);                               /* (the kits, then the user kits) */
        ui.force = 1;
        return;
    }
    cur = preset_pos(&total);
    if (total)
        op_preset_go((uint32_t)(((int32_t)cur + s % (int32_t)total + (int32_t)total) % (int32_t)total));
}
/* a value of descriptor d at vp: a knob (accelerated), PRESETS (fine), or OP_RESET */
static void val_turn(const param_desc_t *d, int16_t *vp, uint32_t k, int32_t s, int fine)
{
    if (!d || !vp || PARAM_HIDDEN(d) || is_go(d) || d->max == d->min)
        return;
    *vp = (int16_t)(s == OP_RESET ? d->def : param_step(d, *vp, fine ? s : accel(EN_K1 + k, s, accel_range(d))));
}
static int val_toggle(const param_desc_t *d, int16_t *vp)   /* YES on an on / off value */
{
    if (!d || !vp || PARAM_HIDDEN(d) || !is_toggle(d) || is_go(d))
        return 0;
    *vp = *vp == d->max ? d->min : d->max;
    return 1;
}

/* ---- HOME: the mixer */
enum { MK_TRK, MK_SOUND, MK_ENTER };
/* The mixer has no cards (the user, 2026-10-08: "on the mixer view, no top four cards; instead we highlight fader,
 * the pan, etc. We should scroll with SELECT from volume to pan, to send, etc."): SELECT walks the strips' controls
 * in this order, the same control lit on the four strips (op_draw.c draw_mixer), then the entries to the other
 * screens. No master values either ("in the mixer view remove the bottom 4 cards please, let's make better use of
 * the space"): BPM and SWING are the TEMPO page's (PLAY held), FILT DUST DUCK the FX layer's knobs and the FX
 * screen's MASTER row; the master LEVEL is the analog knob */
static const struct { const char *name; uint8_t kind, id; } MIX[] = {   /* id: the track value, or the screen */
    {"VOLUME", MK_TRK, P_LEVEL},                        /* (the first MK_TRK row is the strips' fader) */
    {"PAN", MK_TRK, P_PAN},
#if FELUCCA_FX_REVERB
    {"REV", MK_TRK, P_REV},
#endif
#if FELUCCA_FX_DELAY
    {"DLY", MK_TRK, P_DLY},
#endif
#if FELUCCA_FX_CHORUS
    {"CHO", MK_TRK, P_CHOR},
#endif
#if FELUCCA_FX_DIST
    {"DRIVE", MK_TRK, P_DIST},
#endif
#if FELUCCA_TRK_FILT
    {"FILTER", MK_TRK, P_TFLT},
#endif
    {"FX ON", MK_TRK, P_FXOFF},
    {"SOUND", MK_SOUND, SCR_SOUND},
    {"FX", MK_ENTER, SCR_FX},
    {"SONG", MK_ENTER, SCR_SONG},
    {"PROJECT", MK_ENTER, SCR_PROJECT},
    {"SYSTEM", MK_ENTER, SCR_SYSTEM},
};
#define NMIX (sizeof MIX / sizeof MIX[0])
static uint32_t mix_rows(void) { return NMIX; }
static void mix_name(uint32_t r, char *b) { str_cpy(b, MIX[r % NMIX].name, 12); }
/* track k's value of a mixer row: the drum track has its own LEVEL (GLO > DRUMS) and no PAN, DRIVE or sends of its
 * own (each sound has them: the DRUM MIXER, later) */
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
    *vp = &trk[k].p[id];
    return &TP[id];
}
static void mix_cell(uint32_t r, uint32_t k, cell_t *c)
{
    int16_t *vp;
    cell_clear(c);
    switch (MIX[r % NMIX].kind) {
    case MK_TRK:
        cell_param(c, mix_desc(MIX[r % NMIX].id, k, &vp), vp);
        if (!c->d)
            str_cpy(c->val, "-", sizeof c->val);
        break;
    case MK_SOUND:
        c->kind = CK_RO;
        snd_name(k, c->val);
        break;
    default:
        return;
    }
    c->label = trk_tag(k);                              /* KNOB k is track k */
    c->col = trk_col(k);
}
static void mix_turn(uint32_t r, uint32_t k, int32_t s, int fine)
{
    int16_t *vp;
    const param_desc_t *d;
    switch (MIX[r % NMIX].kind) {
    case MK_TRK:
        d = mix_desc(MIX[r % NMIX].id, k, &vp);
        break;
    case MK_SOUND:
        if (fine && s != OP_RESET)
            op_preset_step(s);                          /* PRESETS browses the selected track's sounds */
        return;
    default:
        return;
    }
    val_turn(d, vp, k, s, fine);
}
static int mix_yes(uint32_t r, uint32_t k, uint32_t ok)
{
    int16_t *vp;
    (void)ok;
    switch (MIX[r % NMIX].kind) {
    case MK_TRK: {
        const param_desc_t *d = mix_desc(MIX[r % NMIX].id, k, &vp);
        return val_toggle(d, vp);                       /* FX: on / dry */
    }
    case MK_SOUND:
    case MK_ENTER:
        op_enter(MIX[r % NMIX].id);
        return 1;
    default:
        return 0;
    }
}

/* ---- SOUND: the SOUND row, then the track's pages */
/* A page button tapped shows only its family's rows (the user, 2026-10-08: "why I see env2 and slicer on the lfo
 * screen?"): LFO the LFO and LFO DEST rows, ENV the envelopes and their DEST rows, FX the track's effects, EDIT the
 * engine's (the drum track: the lane's) rows... (params.c PAGES' fam). The mixer's SOUND row opens every row, the
 * SOUND row first, as before. snd_fam: the family shown, SND_ALL every row */
#define SND_ALL 0xFFu
static uint8_t snd_fam = SND_ALL;
static uint8_t snd_ix[OP_MAXROWS];
static int snd_row_page(const page_t *pg) { return sound_page(pg) && (snd_fam == SND_ALL || pg->fam == snd_fam); }
static uint32_t snd_first(void) { return snd_fam == SND_ALL ? 1u : 0u; }   /* the SOUND row: only in the whole list */
static uint32_t snd_rows(void) { return snd_first() + page_rows(snd_row_page, snd_ix); }
static const page_t *snd_page(uint32_t r)
{
    return r >= snd_first() ? &PAGES[snd_ix[(r - snd_first()) % OP_MAXROWS]] : 0;
}
static void snd_name_row(uint32_t r, char *b) { str_cpy(b, snd_page(r) ? snd_page(r)->title : "SOUND", 12); }
static void snd_family(uint32_t fam)                    /* the rows of family fam only (SND_ALL: every row) */
{
    snd_fam = (uint8_t)fam;
    if (fam != SND_ALL && !page_rows(snd_row_page, snd_ix))
        snd_fam = SND_ALL;                              /* (none on this track: the drum track has no ENV) */
}
static void snd_cell(uint32_t r, uint32_t k, cell_t *c)
{
    if (snd_page(r)) {
        page_cell(snd_page(r), k, c);
        return;
    }
    cell_clear(c);
    if (is_drum(TSEL)) {                                /* the drum track: the kit, the selected lane */
        static const char *const L[2] = {"KIT", "LANE"};
        if (k > 1u)
            return;
        c->label = L[k];
        c->kind = CK_VAL;
        str_cpy(c->val, k ? LANE_NAME[lane_selected()] : drum_kit_name(), sizeof c->val);
        c->col = k ? lane_col(lane_selected()) : kit_col();
        cell_gauge(c, 1, 0, k ? DRUM_LANES - 1 : (int32_t)drum_kit_total() - 1, k ? (int32_t)lane_selected() :
                   (int32_t)drum_kit_pos());
        return;
    }
    switch (k) {
    case 0:
        c->label = "PRESET";
        c->kind = CK_VAL;
        snd_name(song.sel, c->val);
        break;
    case 1:
        c->label = "ENGINE";
        c->kind = CK_VAL;
        str_cpy(c->val, ENGINES[TSEL->eng_req % NENGINES]->name, sizeof c->val);
        c->col = ENG_COL[TSEL->eng_req % NENGINES];
        cell_gauge(c, 1, 0, NENGINES - 1, TSEL->eng_req % NENGINES);
        break;
    case 2:
        c->label = "INIT";
        c->kind = CK_ACT;
        break;
    default:
        c->label = "SAVE AS";
        c->kind = CK_ACT;
        break;
    }
}
static void snd_turn(uint32_t r, uint32_t k, int32_t s, int fine)
{
    if (snd_page(r)) {
        page_turn(snd_page(r), k, s, fine);
        return;
    }
    if (s == OP_RESET)
        return;
    if (k == 0u) {
        op_preset_step(s);                              /* the one row where a turn changes the sound [D] */
    } else if (k == 1u && is_drum(TSEL)) {
        lane_select((uint32_t)clamp((int32_t)lane_selected() + (s > 0 ? 1 : -1), 0, DRUM_LANES - 1));
    } else if (k == 1u) {
        uint32_t e = TSEL->eng_req;
        do                                              /* (stepping over the numbers kept free: engines.c) */
            e = (e + (s > 0 ? 1u : NENGINES - 1u)) % NENGINES;
        while (eng_free(e));
        set_engine(e);
        ui.force = 1;
    }
}
static int snd_yes(uint32_t r, uint32_t k, uint32_t ok)
{
    uint32_t i;
    if (snd_page(r))
        return page_yes(SCR_SOUND, r, snd_page(r), k, ok);
    if (is_drum(TSEL) || k < 2u)
        return 0;
    if (k == 2u) {                                      /* INIT: the engine's defaults, the edits go */
        if (!ok) {
            op_arm(SCR_SOUND, r, k, "INIT", trk_tag(song.sel), 1);
            return 1;
        }
        return op_global_go(G_INITSND);
    }
    for (i = 0; i < UP_SLOTS && up_used(i); i++)        /* SAVE AS: the first free user preset slot (the NAME */
        ;                                               /* screen comes later) */
    if (i == UP_SLOTS)
        ui_message("USER PRESETS FULL");
    else
        up_ui(2u, i);
    return 1;
}

/* ---- FX: the global effects' pages */
static uint8_t fx_ix[OP_MAXROWS];
static uint32_t fxs_rows(void) { return page_rows(fx_page, fx_ix); }
static void fxs_name(uint32_t r, char *b) { str_cpy(b, PAGES[fx_ix[r % OP_MAXROWS]].title, 12); }
static void fxs_cell(uint32_t r, uint32_t k, cell_t *c) { page_cell(&PAGES[fx_ix[r % OP_MAXROWS]], k, c); }
static void fxs_turn(uint32_t r, uint32_t k, int32_t s, int fine) { page_turn(&PAGES[fx_ix[r % OP_MAXROWS]], k, s, fine); }
static int fxs_yes(uint32_t r, uint32_t k, uint32_t ok) { return page_yes(SCR_FX, r, &PAGES[fx_ix[r % OP_MAXROWS]], k, ok); }
