/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, SYSTEM > General > FONT (the builder's FONT_* items, tools/gen_font.py FACES; gfx.c font_use).
 * More than one face built (tests/run_tests.sh: a header made with FELUCCA_FONTS): the FONT line is there, next to KNOB
 * COLORS; its knob changes the face every screen draws (the pixels differ, FONT_L follows), YES steps round, SHIFT +
 * PRESETS (factory) is the first face; a stored face this build lacks (gfx.c font_use_id) is the first built; the settings
 * record keeps the face's stable number (project.c persist_t font: font_rec out, font_use_id in). One face: no FONT
 * line. */
static void font_tests(void)
{
#if FONT_N > 1
    static uint16_t shot[240 * 240];
    uint32_t k, n, f;
    font_use(0);
    reset_ui();
    k = line_named(SCR_SYSTEM, "FONT");
    n = SCREENS[SCR_SYSTEM].rows();
    check(k < n && line_named(SCR_SYSTEM, "KNOB COLORS") == k + 1u,
          "FONT (more than one face built): a SYSTEM line, just before KNOB COLORS");
    k = line_named(SCR_SYSTEM, "FONT");
    ui.force = 1;
    frame();
    frame();
    memcpy(shot, screen, sizeof shot);
    check(font_sel == 0u && FONT_S.data == FONT_DATA[0] && FONT_L.data == FONT_DATA[0],
          "FONT: the first face built is drawn at power-on");
    turn(LINE_KNOB(k), 1);
    ui.force = 1;
    frame();
    frame();
    check(font_sel == 1u && FONT_S.data == FONT_DATA[1] && FONT_L.data == FONT_DATA[1] && sys_dirty,
          "FONT: its knob picks the next face, FONT_S and FONT_L both (a setting: saved when SYSTEM is left)");
    check(memcmp(shot, screen, sizeof shot) != 0, "FONT: the screen drawn in the other face differs");
    ppm("opt-font-system-turned");
    for (f = 0; f < FONT_N + 2u; f++)
        turn(LINE_KNOB(k), 1);
    check(font_sel == FONT_N - 1u, "FONT: the knob stops at the last face");
    sys_set(SI_FONT, 0);
    check(font_sel == 0u, "FONT: YES steps round, past the last to the first");
    font_use(1);
    check(sys_factory(SI_FONT) >= 0 && font_sel == 0u, "FONT: the factory value is the first face built");
    {   /* the settings record's byte (project.c: settings_save writes font_rec, persist_boot reads it with font_use_id) */
        uint8_t rec;
        font_use(FONT_N - 1u);
        rec = font_rec();
        font_use(0);
        font_use_id(rec);
        check(rec == FONT_ID[FONT_N - 1u] && font_sel == FONT_N - 1u && FONT_S.data == FONT_DATA[FONT_N - 1u],
              "FONT: kept as the face's stable number; read back, the same face is drawn");
    }
    font_use_id(250u);
    check(font_sel == 0u && FONT_S.data == FONT_DATA[0], "FONT: a stored face this build lacks: the first built");
    font_use(7u + FONT_N);
    check(font_sel == 0u, "FONT: an index past the faces built: the first");
    {
        cell_t c;
        uint32_t ok = 1;
        line_at(SCR_SYSTEM, k);
        for (f = 0; f < FONT_N; f++) {
            font_use(f);
            SCREENS[SCR_SYSTEM].cell(k, gl_kn(SCR_SYSTEM, k), &c);
            ok &= c.label && !strcmp(c.val, FONT_NAME[f]);
        }
        check(ok, "FONT: the line shows each face's name whole");
    }
    font_use(0);
    reset_ui();
#else
    reset_ui();
    check(line_named(SCR_SYSTEM, "FONT") == SCREENS[SCR_SYSTEM].rows(), "FONT: one face built: no FONT line");
    reset_ui();
#endif
}
