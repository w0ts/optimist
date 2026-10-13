/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, the help line (op_help.c, FELUCCA_PARAM_HELP; the user, 2026-10-13: "a help text on the
 * bottom last line ... displayed everywhere when i modify a value or in the vertical menus"):
 *   menus     GLOBAL, SYSTEM: every line has its text, the table's, fitting the line (232 px); PROJECT: every cell
 *   a turn    SOUND, the mixer: the line shows on a value's turn, the bands keep off its rows, it goes ~2 s after
 *   missing   every SOUND / MASTER / mixer / STEP / SONG / TEMPO value without a line is listed (information)
 * Included after ui_optimist_glwalk.h. A build without PARAM_HELP: none of it. */
#if FELUCCA_PARAM_HELP
static int hlt_strip_lit(void)                          /* the line's rows hold something besides black */
{
    uint32_t i;
    for (i = (uint32_t)HL_Y * 240u; i < 240u * 240u; i++)
        if (screen[i])
            return 1;
    return 0;
}
static int hlt_text_is(const char *want)                /* the line now says want (the table's words) */
{
    char b[PH_LINE];
    uint32_t s = hl_text();
    return hl.on && s && !strcmp(ph_text(s, b), want);
}
static uint32_t hlt_missing;
static track_t hlt_t0;
static void hlt_scan(uint32_t scr)                      /* every value of the screen's rows: its line, else listed */
{
    uint32_t r, k, n = SCREENS[scr].rows();
    cell_t c;
    char nm[16];
    for (r = 0; r < n && r < 64u; r++)
        for (k = 0; k < 4u; k++) {
            SCREENS[scr].cell(r, k, &c);
            if (c.kind != CK_VAL && c.kind != CK_ACT)
                continue;
            if (!hl_find(scr, r, &c)) {
                SCREENS[scr].name(r, nm);
                printf("  help: no line for %s / %s / %s (%s)\n", SCR_NAME[scr], nm, c.label ? c.label : "-",
                       is_drum(TSEL) ? "DRUMS" : ENGINES[TSEL->eng_req % NENGINES]->name);
                hlt_missing++;
            }
        }
}
static void help_tests(void)
{
    uint32_t s, i, n, ok = 1, fit = 1;
    cell_t c;
    char nm[16];
    reset_ui();
    for (s = 0; s < 2u; s++) {                          /* the settings lists: every line, its own text */
        uint32_t scr = s ? SCR_SYSTEM : SCR_GLOBAL;
        op_enter(scr);
        frame();
        n = SCREENS[scr].rows();
        for (i = 0; i < n; i++) {
            uint32_t t;
            char tb[PH_LINE];
            turn(EN_ALGO, (int32_t)i - (int32_t)ui.row[scr]);
            gl_cell(s, i, &c);
            t = hl_text();
            gl_sect(s, i, nm);
            if (!t || !hl.on || !hlt_strip_lit()) {
                printf("  help: %s line %u %s: no help line\n", SCR_NAME[scr], i, c.label ? c.label : "-");
                ok = 0;
            } else if (text_w(&FONT_S, ph_text(t, tb)) > 232) {
                fit = 0;
            }
            if (t && ph_find(PH_OP, nm, c.label) && t != ph_find(PH_OP, nm, c.label))
                ok = 0, printf("  help: %s line %u %s: %s (row %u)\n", SCR_NAME[scr], i, nm, ph_text(t, tb), ui.row[scr]);   /* (the Optimist table's line, by its section) */
        }
    }
    check(ok, "GLOBAL / SYSTEM: the cursor line's help on the screen's last line, every line, the table's text");
    check(fit, "... each fits the line (232 px)");
    line_named(SCR_GLOBAL, "BPM");
    frame();
    check(hlt_text_is("Tempo of the song"), "... GLOBAL BPM: \"Tempo of the song\"");
    line_named(SCR_SYSTEM, "PANEL");
    frame();
    check(hlt_text_is("Teach buttons, knobs (asks)"), "... SYSTEM PANEL names its question: \"Teach buttons, knobs (asks)\"");
    op_enter(SCR_PROJECT);                              /* PROJECT: the hot cell's line, every cell, always there */
    frame();
    for (i = 0, ok = 1; i < SCREENS[SCR_PROJECT].rows(); i++)
        for (s = 0; s < 4u; s++) {
            SCREENS[SCR_PROJECT].cell(i, s, &c);
            if (c.kind == CK_NONE || !c.label)
                continue;
            ui.row[SCR_PROJECT] = (uint8_t)i;
            ui.hot = (uint8_t)s;
            frame();
            char tb[PH_LINE];
            if (!hl_text() || !hl.on || text_w(&FONT_S, ph_text(hl_text(), tb)) > 232) {
                printf("  help: PROJECT row %u %s: no help line\n", i, c.label);
                ok = 0;
            }
        }
    check(ok, "PROJECT: every cell's help on the last line (an action that asks says so)");
    ui.row[SCR_PROJECT] = 0;
    ui.hot = 1;                                         /* (the PROJECT row's LOAD) */
    frame();
    check(hlt_text_is("Load (work changed: asks)"), "... PROJECT LOAD: \"Load (work changed: asks)\"");
    check(OP_PY + LIST_H == HL_Y, "... PROJECT's rows end over the help line");
    op_enter(SCR_SOUND);                                /* a value turned: the line ~2 s, then gone */
    ui.row[SCR_SOUND] = 1;
    frames(3);
    check(!hl.on, "SOUND: no help line until a value turns");
    SCREENS[SCR_SOUND].cell(ui.row[SCR_SOUND], 0, &c);
    turn(EN_K1, 1);
    frame();
    check(hl.on && hl_text() && hlt_strip_lit() && hl_text() == hl_find(SCR_SOUND, ui.row[SCR_SOUND], &c),
          "SOUND: KNOB 1 turned, its value's help on the last line (SLOOP's table, by its page)");
    frames(HL_FRAMES / 2u);
    turn(EN_K1, -1);
    frames(HL_FRAMES / 2u + 10u);
    check(hl.on, "... still there while it turns and ~2 s after the last detent");
    frames(HL_FRAMES / 2u);
    check(!hl.on && cv_clip == 240u, "... then gone, the bands drawn over its rows again");
    turn(EN_K1, 1);
    frame();
    op_enter(SCR_HOME);
    frame();
    check(!hl.on, "... another screen lets it go");
    turn(EN_K1, 1);                                     /* the mixer: a level */
    frame();
    check(hl.on && hl_text(), "the mixer: a value turned, its help line");
    turn(EN_K1, -1);
    frames(HL_FRAMES + 5u);
    check(!hl.on, "... gone after ~2 s");
    /* what has no line yet: information for the report (tools/param_help.json) */
    memcpy(&hlt_t0, &trk[0], sizeof hlt_t0);
    for (s = 0; s < NTRK; s++) {
        uint32_t e;
        song.sel = (uint8_t)s;
        if (s == TRK_DRUM) {
            op_enter(SCR_SOUND);
            hlt_scan(SCR_SOUND);
            continue;
        }
        for (e = 0; e < NENGINES && s == 0u; e++) {
            if (eng_free(e))
                continue;
            set_engine(e);
            op_enter(SCR_SOUND);
            hlt_scan(SCR_SOUND);
        }
    }
    song.sel = 0;
    memcpy(&trk[0], &hlt_t0, sizeof hlt_t0);             /* (T1's sound as it was: the engines walked) */
    sync_reload = 1;
    op_enter(SCR_FX);
    hlt_scan(SCR_FX);
    op_enter(SCR_HOME);
    hlt_scan(SCR_HOME);
    op_enter(SCR_STEP);
    hlt_scan(SCR_STEP);
    op_enter(SCR_SONG);
    hlt_scan(SCR_SONG);
    hlt_scan(SCR_TEMPO);
    printf("  help: %u values without a line (listed above)\n", hlt_missing);
    check(!hlt_missing, "every value of every screen has its help line (LFO 2 and LFO2 DEST too, with LFO2)");
    reset_ui();
}
#else
static void help_tests(void) {}
#endif
