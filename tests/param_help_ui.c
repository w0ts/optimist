/* SPDX-License-Identifier: GPL-3.0-only */
/* The one-line parameter help (FELUCCA_PARAM_HELP, param_help.c; tools/param_help.json), included by
 * tests/ui_pages_test.c:
 *   coverage  every value a knob reaches on a page of this build has its line: each page with each engine (every
 *             mode label an engine's desc hook gives: each EDIT value swept over its range), the drum track's pages
 *             and SOUND pages (each kit, lane and source), the FM6 operator editor, the live screens (TRACKS, DRUM
 *             GRID / KIT, SONG, REC); each line fits the top bar (text_w of the UI font <= 232 px)
 *   display   the line shows on the top bar (VIEW PAGE, VIEW ALL), the live screens' header and the song screen
 *             while the knob turns, and goes with the knob's white value; a message still wins; screenshots
 *             (DIR/help-*.ppm)
 * PHELP_DUMP=1 in the environment: every key on stdout, "kind ctx label ok|MISSING text" (tab separated). */
#if FELUCCA_PARAM_HELP
#define PH_MAXK 1600
static struct { uint8_t kind; char ctx[16], label[12]; } ph_keys[PH_MAXK];
static uint32_t ph_nkeys, ph_missing, ph_wide;
static int ph_dump;
static const char *const PH_KIND[4] = {"?", "page", "eng", "fm6"};

static void ph_need(uint32_t kind, const char *ctx, const char *label, const char *shown)
{
    uint32_t i, off;
    const char *t;
    for (i = 0; i < ph_nkeys; i++)
        if (ph_keys[i].kind == kind && !strcmp(ph_keys[i].ctx, ctx) && !strcmp(ph_keys[i].label, label))
            return;
    assert(ph_nkeys < PH_MAXK);
    ph_keys[ph_nkeys].kind = (uint8_t)kind;
    snprintf(ph_keys[ph_nkeys].ctx, sizeof ph_keys[0].ctx, "%s", ctx);
    snprintf(ph_keys[ph_nkeys].label, sizeof ph_keys[0].label, "%s", label);
    ph_nkeys++;
    off = ph_find(kind, ctx, label);
    t = off ? PH_BLOB + off : "";
    if (!off) {
        ph_missing++;
        if (!ph_dump)
            printf("ui: help: no line for %s \"%s\" / \"%s\" (%s)\n", PH_KIND[kind & 3u], ctx, label, shown);
    } else if (text_w(&FONT_S, t) > 232) {
        ph_wide++;
        printf("ui: help: %s \"%s\" / \"%s\": \"%s\" is %d px (> 232)\n", PH_KIND[kind & 3u], ctx, label, t,
               (int)text_w(&FONT_S, t));
    }
    if (ph_dump)
        printf("%s\t%s\t%s\t%s\t%s\t%s\n", PH_KIND[kind & 3u], ctx, label, shown, off ? "ok" : "MISSING", t);
}
static void ph_slot_need(const char *ctx, uint32_t k, const char *shown)
{
    char l[2] = {(char)('1' + k), 0};
    ph_need(PH_PAGE, ctx, l, shown);
}

/* the knobs of the page shown now on track TSEL (as ui_input.c: the knobs that light their value) */
static void ph_page_keys(const page_t *pg)
{
    uint32_t k;
    if (pg->scope == SC_FM6K || pg->scope == SC_DRUM || pg->scope == SC_SONG || !page_shown(pg))
        return;
    for (k = 0; k < 4u; k++) {
        int16_t *vp;
        const param_desc_t *d = page_desc(pg, k, &vp);
        if (d && d->label && d->label[0] && d->label[0] != '-')
            ph_need(pg->scope == SC_ENGINE ? PH_ENG : PH_PAGE,
                    pg->scope == SC_ENGINE ? ENGINES[TSEL->eng_req % NENGINES]->name : pg->title, d->label, pg->title);
        else if (!d && !(is_drum(TSEL) && !page_for_drum(pg)) &&
                 (((pg->scope == SC_STEP || pg->scope == SC_TRK) && pg->id[k] != 0xFFu) ||
                  ((pg->graph == GR_USER || pg->graph == GR_SNAP) && !k)))
            ph_slot_need(pg->title, k, pg->title);
    }
}

static void ph_engine_keys(uint32_t e)
{
    track_t *t = TSEL;
    uint32_t i, j, k;
    int32_t v;
    int16_t keep[8];
    set_engine_of(t, e);
    t->engine = t->eng_req;
    for (i = 0; i < NPAGES; i++)
        ph_page_keys(&PAGES[i]);
    memcpy(keep, &t->p[P_E0], sizeof keep);
    for (j = 0; j < 8u; j++)                              /* the mode-dependent labels (engine_t.desc) */
        for (v = ENGINES[e]->edit[j].min; v <= ENGINES[e]->edit[j].max; v++) {
            t->p[P_E0 + j] = (int16_t)v;
            for (k = 0; k < 8u; k++) {
                const param_desc_t *d = track_desc(t, P_E0 + k);
                if (d->label && d->label[0] && d->label[0] != '-' && d->max != d->min)
                    ph_need(PH_ENG, ENGINES[e]->name, d->label, k < 4u ? "EDIT 1" : "EDIT 2");
            }
            t->p[P_E0 + j] = keep[j];
        }
}

static void ph_drum_keys(void)
{
    uint32_t i, kit, l;
    song.sel = TRK_DRUM;
    for (i = 0; i < NPAGES; i++)
        if (PAGES[i].scope != SC_DSND)
            ph_page_keys(&PAGES[i]);
#if DL_UI
    for (kit = 0; kit < DRUM_KITS; kit++) {
        TDRUM->p[P_E0] = (int16_t)kit;
        for (l = 0; l < DRUM_LANES; l++) {
            uint32_t src, nsrc = FELUCCA_DRUM_KITS ? 4u + DRUM_KITS + DS_SRC_XN : 4u;
            for (src = 0; src < nsrc; src++) {
                memset(&dl, 0, sizeof dl);
                dl.src[l] = (uint8_t)dsnd_idx_src(src);
                pen_lane = (uint8_t)l;
                for (i = 0; i < NPAGES; i++)
                    if (PAGES[i].scope == SC_DSND)
                        ph_page_keys(&PAGES[i]);
            }
        }
    }
    memset(&dl, 0, sizeof dl);
    pen_lane = 0;
#endif
    TDRUM->p[P_E0] = DRUM_DEFAULT_KIT;
    song.sel = 0;
}

static void ph_coverage(void)
{
    uint32_t e, k, keep_e = TSEL->eng_req;
    int16_t keep_p[P_COUNT];
    memcpy(keep_p, TSEL->p, sizeof keep_p);
    song.sel = 0;
    for (e = 0; e < NENGINES; e++)
        if (!eng_free(e))
            ph_engine_keys(e);
    set_engine_of(TSEL, keep_e);
    TSEL->engine = TSEL->eng_req;
    memcpy(TSEL->p, keep_p, sizeof keep_p);
    ph_drum_keys();
#if FELUCCA_ENG_FM6 && FELUCCA_FM6_KEYS
    {
        static const fm6k_page_t *const G[3] = {FMK_OP_PAGES, FMK_PIT_PAGES, FMK_GLO_PAGES};
        static const uint8_t N[3] = {FMK_NPG(FMK_OP_PAGES), FMK_NPG(FMK_PIT_PAGES), FMK_NPG(FMK_GLO_PAGES)};
        uint32_t g, p;
        for (g = 0; g < 3u; g++)
            for (p = 0; p < N[g]; p++)
                for (k = 0; k < 4u; k++)
                    if (G[g][p].p[k].kind != FK_NONE)
                        ph_need(PH_FM6, G[g][p].name, G[g][p].p[k].lab, "FM6");
    }
#endif
    for (k = 0; k < 4u; k++) {
        ph_slot_need("DRUM GRID", k, "DRUMS grid");
        ph_slot_need("DRUM KIT", k, "DRUMS kit");
#if FELUCCA_ARRANGER
        ph_slot_need("SONG", k, "SONG");
#endif
#if FELUCCA_REC_MODES
        if (k < 3u)
            ph_slot_need("REC", k, "REC armed");
#endif
    }
    check(!ph_missing, "help: every value a knob reaches has its line (tools/param_help.json)");
    check(!ph_wide, "help: every line fits the top bar (232 px of the UI font)");
    printf("ui: help: %u keys checked\n", ph_nkeys);
}

/* the line (kind, ctx, label) as the table has it, "" none */
static const char *ph_text(uint32_t kind, const char *ctx, const char *label)
{
    uint32_t off = ph_find(kind, ctx, label);
    return off ? PH_BLOB + off : "";
}
static int ph_shows(const char *want) { return ph_line() && want[0] && !strcmp(ph_line(), want); }
static void ph_view(uint32_t v) { song.g[G_VIEW] = (int16_t)v; settings.view = v; ui.force = 1; frame(); }
static void ph_turn(uint32_t knob, int32_t s) { encs[panel.enc[EN_K1 + knob]] = s; frame(); }
/* to the page of this title (the family's last page is whatever an earlier test left) */
static void ph_goto(const char *title)
{
    uint32_t i;
    for (i = 0; i < NPAGES && strcmp(PAGES[i].title, title); i++)
        ;
    ui.page = (uint8_t)i; ui.hot_t = 0; ui.force = 1; frame();
}

static void ph_display(void)
{
    song.sel = 0; song.playing = 0; transport_req = 0;
    set_engine_of(TSEL, 0); TSEL->engine = TSEL->eng_req; apply_preset_to(TSEL, 0);
    go_home(); frame();
    check(!ph_line(), "help: none on a page entered (HOME)");
    tap(B_ENV); frames(2);
    check(!ph_line(), "help: none on a page chosen with its button (ENV)");
    ph_view(0);
    tap(B_ENV); frames(2);
    check(!ph_line(), "help: none on the next page of the family (ENV tapped again)");
    tap(B_ENV); frames(2);
    while (cur_page()->scope != SC_TRACK || cur_page()->id[0] != P_ATK) { tap(B_ENV); frames(1); }
    ph_turn(1, 1);
    check(ph_shows(ph_text(PH_PAGE, "ENV", "DEC")), "help: the first detent of KNOB 2 shows its line");
    {
        uint32_t i, kept = 1;
        for (i = 0; i < 6u; i++) { frames(PH_HOT - 10u); ph_turn(1, i & 1u ? 1 : -1); kept &= ph_shows(ph_text(PH_PAGE, "ENV", "DEC")); }
        check(kept, "help: ... it stays while the knob keeps turning (a detent every ~0.9 s)");
    }
    tap(B_LFO); frames(1);
    check(!ph_line(), "help: ... a button pressed takes it away at once (LFO)");
    tap(B_ENV); frames(1);
    while (cur_page()->scope != SC_TRACK || cur_page()->id[0] != P_ATK) { tap(B_ENV); frames(1); }
    ph_turn(1, 1);
    encs[panel.enc[EN_SELECT]] = 1; frame();
    check(!ph_line(), "help: ... and the tempo knob (SELECT) too: only a parameter knob shows it");
    ph_turn(1, 2);
    check(ph_shows(ph_text(PH_PAGE, "ENV", "DEC")), "help: ENV, KNOB 2 turned: its line on the top bar");
    ppm("help-env");
    frames(PH_HOT + 4);
    check(!ph_line(), "help: ... gone with the knob's white value (PH_HOT frames, ~1 s)");
    ph_goto("EDIT 1");
    ph_turn(0, 1);
    check(ph_shows(ph_text(PH_ENG, ENGINES[TSEL->eng_req]->name, track_desc(TSEL, P_E0)->label)),
          "help: EDIT 1, KNOB 1: the engine's own line");
    ppm("help-edit");
    ph_view(1);
    ph_goto("FX");
    ph_turn(0, 3);                                        /* (DST: the FX built differ; the page is "FX") */
    check(ph_shows(ph_text(PH_PAGE, "FX", "DST")) || !FELUCCA_FX_DIST, "help: VIEW ALL, FX, KNOB 1: the same top bar line");
    ppm("help-overview-fx");
    ui_message("SAVED");
    ph_turn(0, -1);
    check((ph_line() && ui.msg_t) || !FELUCCA_FX_DIST, "help: a message up wins over the line (the top bar keeps it)");
    frames(PH_HOT + 4);
    ph_view(0);
    go_home(); frame();
    ph_turn(1, 1);
    check(ph_shows(ph_text(PH_PAGE, "TRACKS", "2")), "help: TRACKS, KNOB 2: its line in the live header");
    ppm("help-tracks");
    frames(PH_HOT + 4);
#if FELUCCA_ENG_FM6 && FELUCCA_FM6_KEYS
    {
        uint32_t keep = TSEL->eng_req;
        set_engine_of(TSEL, ENG_IX_FM6); frames(3);
        tap(B_ENV); frames(1);
        check(on_fm6k_page() && !ph_line(), "help: none on the FM6 editor entered (ENV)");
        ph_turn(1, 1);
        check(ph_shows(ph_text(PH_FM6, fm6k_cur_page(0)->name, fm6k_cur_page(0)->p[1].lab)),
              "help: FM6 editor, KNOB 2: its line in the live header");
        ppm("help-fm6");
        frames(PH_HOT + 4);
        set_engine_of(TSEL, keep); TSEL->engine = TSEL->eng_req;
        go_home(); frame();
    }
#endif
#if FELUCCA_ARRANGER
    studio_open(SC_SONG); frame();
    check(!ph_line(), "help: none on the song screen opened");
    ph_turn(2, 1); ph_turn(2, -1);
    check(ph_shows(ph_text(PH_PAGE, "SONG", "3")), "help: song screen, KNOB 3: its line over the knob labels");
    ppm("help-song");
    frames(PH_HOT + 4);
    go_home(); frame();
#endif
#if DL_UI && FELUCCA_DRUM_EDIT
    song.sel = TRK_DRUM; go_home(); frame();
    key(key_of_white(2));
    open_family(FAM_EDIT); ph_view(0);
    ph_turn(1, -2);
    check(cur_page()->scope == SC_DSND && ph_shows(ph_text(PH_PAGE, cur_page()->title, "DECAY")),
          "help: drum SOUND page, KNOB 2 DECAY: its line");
    ppm("help-drum-sound");
    frames(PH_HOT + 4);
    memset(&dl, 0, sizeof dl);
    song.sel = 0; go_home(); frame();
#endif
#if FELUCCA_MACROS
    {
        uint32_t i;
        for (i = 0; i < NPAGES && !(PAGES[i].scope == SC_MACRO); i++)
            ;
        ui.page = (uint8_t)i; ui.force = 1; frame();
        ph_turn(0, 2);
        check(ph_shows(ph_text(PH_PAGE, "MACRO", "COLOR")), "help: GLO > MACRO, KNOB 1: its line");
        ppm("help-macro");
        frames(PH_HOT + 4);
        TDRUM->p[MAC_ID[0]] = 0;
        go_home(); frame();
    }
#endif
}

static void param_help_tests(void)
{
    ph_dump = getenv("PHELP_DUMP") != 0;
    ph_coverage();
    if (!ph_dump)
        ph_display();
}
#else
static void param_help_tests(void) {}
#endif
