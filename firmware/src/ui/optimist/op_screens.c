/* SPDX-License-Identifier: GPL-3.0-only */
/* The screens as lists of rows (docs/UI-OPTIMIST-DESIGN.md sections 4.1, 4.3, 4.4, 4.6, 4.7). A screen gives its
 * rows' count, names and cells, and what a knob, PRESETS and YES do on them; op_input.c and op_draw.c know nothing
 * else of a screen.
 *   HOME (MIX)  the mixer: op_mixer.c (the rows are the tracks, the knobs the selected row's values)
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
static void name_user_free(void);                       /* op_name.c: NAME for the first free user preset slot */
static void pre_toast(void);                            /* op_preset.c: a preset's name and engine, a toast */

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
        sound_change(t);                                /* (SLOOP 2.6: what sounds fades out with its own sound) */
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
static void op_clear_track(uint32_t i)                  /* the REC ring and TOOLS' CLRSQ: the track's pattern (undo brings it back) */
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
        ed_done();
        ui_message("SOUND INIT");
        ui.force = 1;
        return 1;
    case G_NEWPRJ:
        op_project_new();
        work_mark();                                    /* (the work as made: nothing to ask about) */
        ui_message("NEW PROJECT");
        return 1;
    default:
        return 0;
    }
}
/* another sound on the same screen (PRESETS, the SOUND row's KNOB 1): every band drawn again over itself, in place
 * (op_state.c op_repaint), never ui.force's clear to black: that flashed the whole screen on every detent (the user,
 * 2026-10-11: "the screen flashes in mixer when i select drum lane presets"). The rows may change (another engine):
 * they are fixed before the panel draws again */
static void op_rows_fix(void);                          /* op_input.c */
static void op_preset_shown(uint32_t was)               /* was: ui.force before the load (a clear asked already stays) */
{
    ui.force = (uint8_t)was;
    op_rows_fix();
    op_repaint();
}
/* list index n of the preset list (model.c BANK, the user presets) into the selected track */
static void op_preset_go(uint32_t n)
{
    uint32_t k, e = preset_at(n, &k), was = ui.force;
    if (is_drum(TSEL))
        return;
    if (e == NENGINES)
        up_load(k);                                     /* (the stores ask for ui.force: the screen stays, repainted below) */
#if FELUCCA_NATIVE_BANKS
    else if (e == NB_LIST)
        nb_load(k);
#endif
    else {
        if (e != TSEL->eng_req)
            set_engine(e);
        apply_preset(k);
    }
    op_preset_shown(was);
}
#if FELUCCA_FAVORITES
/* FAVORITES (docs section 37): the selected track's sound is a favorite. The mark is a settings change: written once the
 * audio is quiet and the panel idle (settings_later; project.c sections_flush), never on a detent */
static int op_fav_here(void)
{
    uint32_t e, k;
    return fav_cur(&e, &k) && fav_entry(e, k);
}
static void op_fav_mark(int on)
{
    uint32_t e, k;
    int r;
    if (!fav_cur(&e, &k))
        return;
    r = e == NENGINES ? fav_user_set(k, on) : fav_set(eng_uid(e), preset_name(e, k), on);
    if (r < 0) {
        ui_message("FAVORITES FULL");
    } else if (r) {
        settings_later = 1;
        ui.force = 1;
    }
}
static void op_fav_list(uint32_t on)                    /* LIST: the marked sounds only / the whole list */
{
    if (fav_list != (uint8_t)on) {
        fav_list = (uint8_t)on;
        ui.force = 1;
    }
}
#endif
static void op_preset_step(int32_t s)                   /* the next / previous sound: one a detent */
{
    uint32_t total, cur;
    int32_t t;
    if (ed_ask(EK_PRESET, s))
        return;                                         /* (edited: "Discard T2 edits?" first, op_edits.c) */
    if (is_drum(TSEL)) {
        uint32_t was = ui.force;
        drum_kit_step(s);                               /* (the kits, then the user kits) */
        op_preset_shown(was);
        ed_done();
        return;
    }
    cur = preset_pos(&total);
    t = (int32_t)total;
    if (!total) {
        if (fav_list)
            ui_message("NO FAVORITES");
    } else if (cur >= total) {                          /* (LIST FAV, a sound not marked: the first / last marked) */
        op_preset_go(s > 0 ? (uint32_t)((s - 1) % t) : (uint32_t)(t - 1 - (-s - 1) % t));
    } else {
        op_preset_go((uint32_t)(((int32_t)cur + s % t + t) % t));
    }
    ed_done();
}
/* HOME held + PRESETS: the next / previous instrument, its first sound (the user, 2026-10-12: "home held (shift) +
 * preset should navigate instruments, not presets"). The factory and INIT part of the list is grouped by engine
 * (bank_resolve), so a step skips the selected engine's group, round; the drum track: its kits, as PRESETS. LIST FAV:
 * the same over the marked sounds */
static uint32_t list_eng(uint32_t n)
{
    uint32_t k;
    return preset_at(n, &k);
}
static void op_engine_step(int32_t s)
{
    uint32_t total, i, j, e0 = TSEL->eng_req, et, n;
    if (ed_ask(EK_ENGINE, s))
        return;
    if (is_drum(TSEL)) {
        op_preset_step(s);
        return;
    }
    i = preset_pos(&total);
    n = fav_list ? total : NLIST;
    if (n == 0u) {
        op_preset_step(s);
        return;
    }
    if (i >= n)                                         /* (a user sound: from its engine's group, or the top) */
        for (i = 0; i < n - 1u && list_eng(i) != e0; i++)
            ;
    for (j = 0; j < n && list_eng(i) == e0; j++)        /* past the selected engine's group */
        i = s > 0 ? (i + 1u) % n : (i + n - 1u) % n;
    et = list_eng(i);
    for (j = 0; j < n && list_eng((i + n - 1u) % n) == et && (i + n - 1u) % n != i; j++)
        i = (i + n - 1u) % n;                           /* (back to that group's first sound) */
    op_preset_go(i);
    ed_done();
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

/* ---- SOUND: the SOUND row, then the track's pages */
/* A page button tapped shows only its family's rows (the user, 2026-10-08: "why I see env2 and slicer on the lfo
 * screen?"): LFO the LFO and LFO DEST rows, ENV the envelopes and their DEST rows, FX the track's effects, EDIT the
 * engine's (the drum track: the lane's) rows... (params.c PAGES' fam). EDIT's rows start with the SOUND row (PRESET
 * ENGINE INIT SAVE AS; the drum track KIT LANE: the user, 2026-10-12, D7), as does the whole list (ALGORITHM back from
 * MASTER). snd_fam: the family shown, SND_ALL every row */
#define SND_ALL 0xFFu
static uint8_t snd_fam = SND_ALL;
static uint8_t snd_ix[OP_MAXROWS];
static int snd_row_page(const page_t *pg) { return sound_page(pg) && (snd_fam == SND_ALL || pg->fam == snd_fam); }
/* FAVORITES (docs section 37): the row after the SOUND row of a synth track, KNOB 1 FAV and KNOB 2 LIST (the SOUND row's four
 * cards are taken); the drum track has no mark */
static int snd_fav_row(void)
{
#if FELUCCA_FAVORITES
    return (snd_fam == SND_ALL || snd_fam == FAM_EDIT) && !is_drum(TSEL);
#else
    return 0;
#endif
}
static uint32_t snd_first(void) { return snd_fam == SND_ALL || snd_fam == FAM_EDIT ? 1u + (uint32_t)snd_fav_row() : 0u; }   /* the SOUND row (and FAVORITES) */
/* (snd_fam SND_FM6: FM6's operator rows, op_fm6.c, on an FM6 track: ENV tapped) */
static uint32_t snd_rows(void) { return snd_fam == SND_FM6 ? fm6_rows() : snd_first() + page_rows(snd_row_page, snd_ix); }
static const page_t *snd_page(uint32_t r)
{
    return snd_fam == SND_FM6 ? 0 : r >= snd_first() ? op_pg(snd_ix[(r - snd_first()) % OP_MAXROWS]) : 0;
}
/* a picked lane has its own INSERTS and SENDS (op_cells.c LANE_FX_PG): the drum track's of those names are "DR INSERT" and "DR SENDS" */
static const char *dr_title(const page_t *pg)
{
    return is_drum(TSEL) && pg->scope == SC_TRACK ? (pg->graph == GR_INS ? "DR INSERT" : pg->graph == GR_FX ? "DR SENDS" : pg->title) : pg->title;
}
static void snd_name_row(uint32_t r, char *b)
{
    if (snd_fam == SND_FM6)
        fm6_row_name(r, b);
    else
        str_cpy(b, snd_page(r) ? (lane_picked() ? dr_title(snd_page(r)) : snd_page(r)->title) : r == 1u && snd_fav_row() ? "FAVS" : "SOUND", 12);
}
/* the drum track's pages as the list and the header title them (the audit's #19, #20): the shared names stay what the
 * help line keys on ("SOUND", "SOUND 2", "SENDS"), the titles say what each page holds: the SOUND row (KIT LANE SRC
 * ACCENT) "Lane", then "Body" (TUNE DECAY SNAP CLICK), "Tone" (BEND CUT DRIVE LEVEL), "Sends" (the lane's), "Kit"; FX's
 * two send pages "Kit sends" (the kit's send levels, every lane's: "Lane sends" is past the titles' 9 letters) and
 * "Bus sends" (the drum bus's own; op_names.c), on a picked lane too (next to its own "Sends"). 0: no alias */
static const char *dr_alias(const page_t *pg)
{
    if (!is_drum(TSEL) || !pg)
        return 0;
    if (pg->scope == SC_DSND && pg->fam == FAM_EDIT)
        return pg->id[0] == 0u ? "BODY" : pg->id[0] == 4u ? "TONE" : pg->id[0] == 16u ? "SENDS" : 0;
    return pg->scope == SC_TRACK && pg->graph == GR_FX ? "KIT SENDS" : 0;
}
static void snd_title_row(uint32_t r, char *b)          /* row r's name as titled (b holds 12): dr_alias, else the name */
{
    const char *a;
    snd_name_row(r, b);
    if (snd_fam == SND_FM6 || !is_drum(TSEL))
        return;
    a = snd_page(r) ? dr_alias(snd_page(r)) : "LANE";
    if (a)
        str_cpy(b, a, 12);
}
/* the synth SOUND row as its list row shows it (the audit's #36): the preset's name over the ENGINE cell's place (the
 * badge names the engine), then INIT, then SAVE AS in the room left (its 4-letter form SAVE, op_draw.c row_layout); a
 * name too long for that (10 letters or more) keeps its place whole and SAVE AS stays the card's alone. The cards keep
 * all four */
static void snd_list_cells(uint32_t r, cell_t *c)
{
    if (snd_fam == SND_FM6 || snd_page(r) || is_drum(TSEL) || (r == 1u && snd_fav_row()))
        return;
    cell_clear(&c[1]);
    if (str_len(c[0].val) > 9u)
        cell_clear(&c[3]);
}
static void snd_family(uint32_t fam)                    /* the rows of family fam only (SND_ALL: every row) */
{
    if (fam == SND_FM6) {
        snd_fam = (uint8_t)(fm6_sel() ? SND_FM6 : SND_ALL);   /* (another engine: every row) */
        return;
    }
    snd_fam = (uint8_t)fam;
    if (fam != SND_ALL && !page_rows(snd_row_page, snd_ix))
        snd_fam = SND_ALL;                              /* (none on this track: every row) */
}
/* SOUND remembers its page (the user, 2026-10-13: "screen should remember its page"): per track, since the pages differ by
 * engine, and per family: the row a family showed last on this track (snd_mem_tick, once a frame, while SOUND is up). A
 * page button entering the family reopens it there; a row the track no longer has (another engine) is the nearest, the
 * last. Zero is the first row, what a track never looked at gets. RAM only: power-on clears it, no project holds it */
static uint8_t snd_mem[NTRK][8];                        /* (slot 0: a family no button opens) */
static uint32_t snd_slot(uint32_t fam)
{
    static const uint8_t SLOT[FAM_COUNT] = {[FAM_ENV] = 1, [FAM_LFO] = 2, [FAM_FX] = 3, [FAM_SCL] = 4, [FAM_EDIT] = 5, [FAM_ARP] = 6};
    return fam == SND_FM6 ? 7u : SLOT[fam % FAM_COUNT];
}
static void snd_mem_tick(void)
{
    if (ui.scr == SCR_SOUND && snd_fam != SND_ALL && ui.row[SCR_SOUND] < 0xFFu)
        snd_mem[song.sel % NTRK][snd_slot(snd_fam)] = ui.row[SCR_SOUND];
}
static uint32_t snd_mem_row(uint32_t fam)               /* the row of family fam to open on (snd_fam is fam already) */
{
    uint32_t r = snd_mem[song.sel % NTRK][snd_slot(fam)], n = snd_rows();
    return r < n ? r : n ? n - 1u : 0u;
}
/* the first card of a lane that is not on the KIT: its PRESET, its sample SLOT, or KIT dim "-" with nothing to pick */
static void snd_lane_card(cell_t *c)
{
    uint32_t f = lane_card_form(), n, i;
    if (f == LCF_NONE) {
        c->label = "KIT";
        c->kind = CK_RO;
        c->dim = 1;
        c->hlp = "NO KIT";
        str_cpy(c->val, "-", sizeof c->val);
        return;
    }
    c->label = f == LCF_SLOT ? "SLOT" : "PRESET";
    c->kind = CK_VAL;
    c->hlp = f == LCF_SLOT ? "LANE SLOT" : "LANE PRESET";
    str_cpy(c->val, lane_sound_name(lane_selected()), sizeof c->val);
    c->col = op_lane_col(lane_selected());
    i = lane_card_pos(&n);
    cell_gauge(c, 1, 0, (int32_t)n - 1, (int32_t)i);
}
#if FELUCCA_FAVORITES
/* the FAVORITES row: FAV (this sound is marked; "--" where it has no mark: the engines' own collections) and LIST */
static void snd_fav_cell(uint32_t k, cell_t *c)
{
    uint32_t e, e2;
    int can = fav_cur(&e, &e2), on = can && op_fav_here();
    if (k == 0u) {
        c->label = "FAV";
        c->kind = can ? CK_VAL : CK_RO;
        c->dim = (uint8_t)!can;
        str_cpy(c->val, can ? (on ? "ON" : "OFF") : "--", sizeof c->val);
        cell_gauge(c, 1, 0, 1, on);
    } else if (k == 1u) {
        c->label = "LIST";
        c->kind = CK_VAL;
        str_cpy(c->val, fav_list ? "FAV" : "ALL", sizeof c->val);
        cell_gauge(c, 1, 0, 1, fav_list);
    }
}
static void snd_fav_turn(uint32_t k, int32_t s)         /* a set, not a toggle: right ON, left OFF, HOME held + the knob: OFF / ALL */
{
    int on = s != OP_RESET && s > 0;
    if (k == 0u)
        op_fav_mark(on);
    else if (k == 1u)
        op_fav_list((uint32_t)on);
}
static int snd_fav_yes(uint32_t k)                      /* YES toggles the card */
{
    if (k == 0u)
        op_fav_mark(!op_fav_here());
    else if (k == 1u)
        op_fav_list(!fav_list);
    return k < 2u;
}
#endif
static void snd_cell(uint32_t r, uint32_t k, cell_t *c)
{
    if (snd_fam == SND_FM6) {
        fm6_cell(r, k, c);
        return;
    }
    if (snd_page(r)) {
        page_cell(snd_page(r), k, c);
        return;
    }
    cell_clear(c);
#if FELUCCA_FAVORITES
    if (r == 1u && snd_fav_row()) {
        snd_fav_cell(k, c);
        return;
    }
#endif
    if (is_drum(TSEL)) {                                /* the drum track: the kit, the selected lane */
        if (k == 2u) {                                  /* the lane's SRC (the user, 2026-10-13); empty where a build has none */
            if (lane_snd())                             /* (the ACCENT lane has no sound: no SRC) */
                page_cell(&LANE_SRC_PG, k, c);
            return;
        }
        if (k == 3u) {                                  /* the ACCENT amount, next to the KIT (every lane: the track's own) */
            if (ACC_UI) {
                acc_cell(c);
                c->dr = (uint8_t)(lane_picked() && lane_snd());   /* (the ACCENT lane's own amount: its lane's) */
            }
            return;
        }
        if (k != 0u)
            return;                                     /* (card 2 was the LANE read-out: gone, the user 2026-10-14; the SOUND row is the
                                                         * lane's source: this card follows it, SRC is the third) */
        if (lane_card_form() != LCF_KIT) {              /* the lane's own sound, not the kit (op_walk.c lane_card_form) */
            snd_lane_card(c);
            return;
        }
        c->label = "KIT";
        c->kind = CK_VAL;
        str_cpy(c->val, drum_kit_name(), sizeof c->val);
        c->col = kit_col();
        c->dr = (uint8_t)lane_picked();                 /* (the kit is the track's) */
        cell_gauge(c, 1, 0, (int32_t)drum_kit_total() - 1, (int32_t)drum_kit_pos());
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
        cell_act(c, "SOUND", "INIT", 0, 1);
        break;
    default:
        cell_act(c, "SOUND", "SAVE AS", 0, 0);
        break;
    }
}
static void snd_turn(uint32_t r, uint32_t k, int32_t s, int fine)
{
    if (snd_fam == SND_FM6) {
        fm6_turn(r, k, s, fine);
        return;
    }
    if (snd_page(r)) {
        page_turn(snd_page(r), k, s, fine);
        return;
    }
#if FELUCCA_FAVORITES
    if (r == 1u && snd_fav_row()) {
        snd_fav_turn(k, s);
        return;
    }
#endif
    if (is_drum(TSEL) && k == 2u) {                     /* the lane's SRC (page_turn: no reset to a default) */
        if (lane_snd())
            page_turn(&LANE_SRC_PG, k, s, fine);
        return;
    }
    if (is_drum(TSEL) && k == 3u) {                     /* the ACCENT amount */
        if (ACC_UI)
            acc_turn(k, s, fine);
        return;
    }
    if (is_drum(TSEL) && k == 1u)                       /* (no second card on the drum SOUND row) */
        return;
    if (is_drum(TSEL) && k == 0u && lane_card_form() != LCF_KIT) {   /* a lane off the KIT: its own PRESET / SLOT, never the kit */
        if (s == OP_RESET)
            lane_card_first();
        else
            lane_card_turn(lane_card_form(), s);
        return;
    }
    if (s == OP_RESET)
        return;
    if (k == 0u) {
        op_preset_step(s);                              /* the one row where a turn changes the sound [D] */
    } else if (k == 1u) {
        op_engine_step(s);                              /* = HOME + PRESETS: the next engine's first sound (the user, 2026-10-13) */
    }
}
static int snd_yes(uint32_t r, uint32_t k, uint32_t ok)
{
    if (snd_fam == SND_FM6)
        return fm6_yes(r, k, ok);
    if (snd_page(r))
        return page_yes(SCR_SOUND, r, snd_page(r), k, ok);
#if FELUCCA_FAVORITES
    if (r == 1u && snd_fav_row())
        return snd_fav_yes(k);
#endif
    if (is_drum(TSEL) || k < 2u)
        return 0;
    if (k == 2u) {                                      /* INIT: the engine's defaults, the edits go */
        if (!ok) {                                      /* (asked always, the user 2026-10-13) */
            op_arm(SCR_SOUND, r, k, "INIT", trk_tag(song.sel), 1);
            return 1;
        }
        return op_global_go(G_INITSND);
    }
    if (!ok) {
        op_arm(SCR_SOUND, r, k, "SAVE AS", "PRESET", 0);
        return 1;
    }
    name_user_free();                                   /* SAVE AS: the first free user preset slot, NAME first */
    return 1;
}

/* ---- MASTER (SCR_FX): the global effects' pages, in the signal's order (the user, 2026-10-14: "we should have inserts,
 * then send returns, then fx slots, then macro"): INSERTS (the master's two insert slots, fx-rework 2026-10-14: its FILT,
 * DUST and DYN are insert types now), INS DYN 1 INS DYN 2 (DYN's settings, while a master slot holds it: the compressor
 * THRS RATIO ATK REL, then GAIN CEIL and the reduction), DUCK (the kick ducking the parts: MASTER's page without its
 * DUST and FILT, now insert amounts), RETURNS
 * RET DELAY RET REV (the return levels, then the effects they return; REVERB's TYPE is RETURNS' fourth card), SLOTS, MACRO. PAGES keeps SLOOP's
 * order and titles (its UI draws them: ui/sloop/ui_draw.c); the order and names shown are this table's (FX_ORDER, a
 * title each; RETURNS, which PAGES has no page for, is fx_ix's FX_RET). TUNE, whose page (PAGES' GLOBAL) is gone, is
 * GLOBAL's Tempo list's fourth line (op_global.c; it sat on LIMIT's free card until fx-rework) */
static const char *const FX_ALIAS[][2] = {{"DLY", "RET DELAY"}, {"REV/CHO", "RET REV"}, {"REVERB", "RET TYPE"},
                                          {"MST INS", "INSERTS"}, {"COMP", "INS DYN 1"}, {"LIMIT", "INS DYN 2"},
                                          {"MASTER", "DUCK"}, {"MST JUNO", "INS JUNO"}};
static const char *const FX_ORDER[] = {"MST INS", "COMP", "LIMIT", "MST JUNO", "MASTER", "RETURNS", "DLY", "REV/CHO", "REVERB", "SLOTS",
                                       "MACRO"};
static const page_t FX_LIMIT = {"LIMIT", FAM_GLO, SC_GLOBAL, GR_NONE, {G_CGAIN, G_CCEIL, G_CGR, 0xFF}};
static const page_t FX_RET = {"RETURNS", FAM_FX, SC_GLOBAL, GR_NONE, {G_RRET, G_DRET, G_CRET, 0xFF}};   /* (REV DLY CHO: the one order) */
#define FX_RET_IX 0xFEu                                 /* fx_ix's entry for FX_RET (past PAGES) */
static uint8_t fx_ix[OP_MAXROWS];
static const page_t *fx_pg_of(uint32_t ix)
{
    const page_t *pg = ix == FX_RET_IX ? &FX_RET : &PAGES[ix];
    return pg->scope == SC_GLOBAL && pg->id[0] == G_CGAIN ? &FX_LIMIT : pg;
}
static const page_t *fx_pg(uint32_t r) { return fx_pg_of(fx_ix[r % OP_MAXROWS]); }
static uint32_t fx_rank(uint32_t ix)
{
    const char *t = (ix == FX_RET_IX ? &FX_RET : &PAGES[ix])->title;
    uint32_t i;
    for (i = 0; i < sizeof FX_ORDER / sizeof FX_ORDER[0]; i++)
        if (str_eq(t, FX_ORDER[i]))
            break;
    return i;
}
/* REVERB's TYPE (REV_MULTI: the reverb's algorithm, its page's one card) is RETURNS' fourth card (the audit's #21: no page of
 * one card; the reverb's return beside the reverb's kind), never a row of its own */
static int fx_type_pg(const page_t *pg)
{
#if REV_MULTI
    return pg->scope == SC_BPSET && pg->id[0] == BPS_RTYPE;
#else
    (void)pg;
    return 0;
#endif
}
static const page_t *fx_type_page(void)
{
    uint32_t i;
    for (i = 0; i < NPAGES; i++)
        if (fx_type_pg(&PAGES[i]))
            return &PAGES[i];
    return 0;
}
static uint32_t fxs_rows(void)
{
    uint32_t n = page_rows(fx_page, fx_ix), i, j;
    for (i = j = 0; i < n; i++)                         /* (REVERB's TYPE: on RETURNS) */
        if (!fx_type_pg(fx_pg_of(fx_ix[i])))
            fx_ix[j++] = fx_ix[i];
    n = j;
    if (n < OP_MAXROWS)
        fx_ix[n++] = FX_RET_IX;
    for (i = 1; i < n; i++) {                           /* PAGES' order to this one's (stable insertion sort) */
        uint8_t v = fx_ix[i];
        for (j = i; j && fx_rank(fx_ix[j - 1u]) > fx_rank(v); j--)
            fx_ix[j] = fx_ix[j - 1u];
        fx_ix[j] = v;
    }
    return n;
}
static void fxs_name(uint32_t r, char *b)
{
    const char *t = fx_pg(r)->title;
    uint32_t i;
    for (i = 0; i < sizeof FX_ALIAS / sizeof FX_ALIAS[0]; i++)
        if (str_eq(t, FX_ALIAS[i][0])) {
            t = FX_ALIAS[i][1];
            break;
        }
    str_cpy(b, t, 12);
}
/* row r's card k as its knob: the page and its cell (*j), 0 none. The values this build has come first, in their order, a
 * value it lacks (INS TONE's DUST without FX_DUST) leaves its frame at the end (the audit's #22: no empty card at the head
 * of a page); RETURNS: the returns of the sends a slot holds (as the mixer's), then REVERB's TYPE where it is built and shown */
static const page_t *fxs_at(uint32_t r, uint32_t k, uint32_t *j)
{
    static const uint8_t RT[3] = {FXT_REV, FXT_DLY, FXT_CHO};
    const page_t *pg = fx_pg(r), *ty = pg == &FX_RET ? fx_type_page() : 0;
    uint32_t n = 0;
    for (*j = 0; *j < 4u; (*j)++) {
        uint32_t id = page_id(pg, *j);
        if (pg == &FX_RET && *j == 3u && ty && page_shown(ty)) {
            if (n == (k & 3u)) {
                *j = 0;
                return ty;
            }
            break;
        }
        if (id != 0xFFu && pg->scope == SC_GLOBAL && (!cell_built(pg, id) || (pg == &FX_RET && *j < 3u && !FXS_ON(RT[*j])) ||
                                                      id == G_DUST || id == G_FILT))   /* (DUST FILT: INSERTS' amounts) */
            continue;
        if (n++ == (k & 3u))
            return pg;
    }
    return 0;
}
static void fxs_cell(uint32_t r, uint32_t k, cell_t *c)
{
    static const char *const RL[3] = {"REV RET", "DLY RET", "CHO RET"};
    uint32_t j;
    const page_t *pg = fxs_at(r, k, &j);
    cell_clear(c);
    if (!pg)
        return;
    page_cell(pg, j, c);
    if (pg == &FX_RET && j < 3u)
        c->label = RL[j];
}
static void fxs_turn(uint32_t r, uint32_t k, int32_t s, int fine)
{
    uint32_t j;
    const page_t *pg = fxs_at(r, k, &j);
    if (pg)
        page_turn(pg, j, s, fine);
}
static int fxs_yes(uint32_t r, uint32_t k, uint32_t ok)
{
    uint32_t j;
    const page_t *pg = fxs_at(r, k, &j);
    return pg && page_yes(SCR_FX, r, pg, j, ok);
}
