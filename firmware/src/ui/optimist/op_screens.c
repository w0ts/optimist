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
static void op_preset_step(int32_t s)                   /* the next / previous sound: one a detent */
{
    uint32_t total, cur;
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
    if (total)
        op_preset_go((uint32_t)(((int32_t)cur + s % (int32_t)total + (int32_t)total) % (int32_t)total));
    ed_done();
}
/* HOME held + PRESETS: the next / previous instrument, its first sound (the user, 2026-10-12: "home held (shift) +
 * preset should navigate instruments, not presets"). The factory and INIT part of the list is grouped by engine
 * (bank_resolve), so a step skips the selected engine's group, round; the drum track: its kits, as PRESETS */
static uint32_t list_eng(uint32_t n)
{
    uint32_t k;
    return preset_at(n, &k);
}
static void op_engine_step(int32_t s)
{
    uint32_t total, i, j, e0 = TSEL->eng_req, et, n = NLIST;
    if (ed_ask(EK_ENGINE, s))
        return;
    if (is_drum(TSEL) || n == 0u) {
        op_preset_step(s);
        return;
    }
    i = preset_pos(&total);
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
static uint32_t snd_first(void) { return snd_fam == SND_ALL || snd_fam == FAM_EDIT ? 1u : 0u; }   /* the SOUND row */
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
        str_cpy(b, snd_page(r) ? (lane_picked() ? dr_title(snd_page(r)) : snd_page(r)->title) : "SOUND", 12);
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
    if (is_drum(TSEL)) {                                /* the drum track: the kit, the selected lane */
        static const char *const L[2] = {"KIT", "LANE"};
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
        if (k > 1u)
            return;
        c->label = L[k];
        c->kind = k ? CK_RO : CK_VAL;                   /* (LANE: the selected lane, ALGORITHM's or HOME + a key's) */
        str_cpy(c->val, k ? lane_name(lane_selected()) : drum_kit_name(), sizeof c->val);
        if (k && lane_acc())
            c->hlp = "ACCENT LANE";                  /* (its help line: op_help.c, ctx INSERTS' key) */
        c->col = k ? op_lane_col(lane_selected()) : kit_col();
        c->dr = (uint8_t)(!k && lane_picked());         /* (the kit is the track's) */
        cell_gauge(c, 1, 0, k ? (int32_t)DRUM_UILANES - 1 : (int32_t)drum_kit_total() - 1, k ? (int32_t)lane_selected() :
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

/* ---- MASTER (SCR_FX): the global effects' pages (the user, 2026-10-12, D3): DELAY REVERB SLOTS TONE COMP LIMIT MACRO.
 * PAGES keeps SLOOP's titles (its UI draws them: ui/sloop/ui_draw.c); the names shown are this table's. TUNE, whose
 * page (PAGES' GLOBAL) is gone, sits on LIMIT's free card: GAIN CEIL TUNE GR */
static const char *const FX_ALIAS[][2] = {{"DLY", "DELAY"}, {"REV/CHO", "REVERB"}, {"REVERB", "REV TYPE"},
                                          {"MASTER", "TONE"}, {"CMP", "INS COMP"}};
static const page_t FX_LIMIT = {"LIMIT", FAM_GLO, SC_GLOBAL, GR_NONE, {G_CGAIN, G_CCEIL, G_TUNE, G_CGR}};
static uint8_t fx_ix[OP_MAXROWS];
static const page_t *fx_pg(uint32_t r)
{
    const page_t *pg = &PAGES[fx_ix[r % OP_MAXROWS]];
    return pg->scope == SC_GLOBAL && pg->id[0] == G_CGAIN ? &FX_LIMIT : pg;
}
static uint32_t fxs_rows(void) { return page_rows(fx_page, fx_ix); }
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
static void fxs_cell(uint32_t r, uint32_t k, cell_t *c) { page_cell(fx_pg(r), k, c); }
static void fxs_turn(uint32_t r, uint32_t k, int32_t s, int fine) { page_turn(fx_pg(r), k, s, fine); }
static int fxs_yes(uint32_t r, uint32_t k, uint32_t ok) { return page_yes(SCR_FX, r, fx_pg(r), k, ok); }
