/* SPDX-License-Identifier: GPL-3.0-only */
/* KNOB COLORS (core/knobcol.h; HOME menu > SYSTEM, the HOLD screen), included by ui_pages_test.c after menu_ui.c: knob_col
 * gives def when OFF and the cap's colour when ON; the menu row toggles it (knob and OCT+); the settings word's bit 25: tests/midi_seq_test.c; screens knobcol-*-off / -on.ppm: a sound page, a SEQ step held (its dials: te_dials, the mixer stream's). */
static void knobcol_ui_tests(void)
{
    uint32_t k, ok = 1;
    uint8_t was = knob_colors;
    char v[12];
    uint16_t c;
    knob_colors = 0;
    for (k = 0; k < 6u; k++)
        ok &= knob_col(k, 0x1234u) == 0x1234u;
    check(ok, "KNOB COLORS off: knob_col is the default for every knob");
    knob_colors = 1;
    for (k = 0, ok = 1; k < KNOBCOL_N; k++)
        ok &= knob_col(k, 0x1234u) == KNOB_COL[k] && KNOB_COL[k] != 0u;
    ok &= knob_col(4, 0x1234u) == 0x1234u && knob_col(200, 7u) == 7u;
    check(ok, "KNOB COLORS on: knob_col is the table's colour for knobs 1..4, the default beyond");
    ok = KNOB_COL[0] == 0x1C5Cu && KNOB_COL[1] == 0xF626u && KNOB_COL[2] == 0xE111u && KNOB_COL[3] == 0xFAC3u;
    check(ok, "KNOB COLORS: blue #1E88E5, yellow #F4C430, pink #E0218A, orange #FF5A1F (RGB565)");
    {   /* te_dials (the mixer, layers, drums, FM6, tempo dials): KNOB COLORS is in its cache signature, dial_col is knob_col */
        static const char *const lab[4] = {"a", "b", "c", "d"}, *const val[4] = {"1", "2", "3", "4"};
        static const int32_t ratio[4] = {0, 250, 500, 1000};
        uint32_t cache = 0, off_sig, on_sig, f = ui.force;
        ui.force = 0;
        knob_colors = 0;
        te_dials(184, lab, val, ratio, 77u, &cache, TE_G4, 0xFu);
        off_sig = cache;
        knob_colors = 1;
        te_dials(184, lab, val, ratio, 77u, &cache, TE_G4, 0xFu);
        on_sig = cache;
        check(off_sig != on_sig, "te_dials: turning KNOB COLORS on redraws the dials (it is in the cache signature)");
        check(dial_col(0, 0x1234u) == KNOB_COL[0] && dial_col(3, 0x1234u) == KNOB_COL[3], "te_dials: dial_col is knob_col (ON)");
        knob_colors = 0;
        check(dial_col(2, 0x1234u) == 0x1234u, "te_dials: dial_col is the screen's colour (OFF)");
        ui.force = f;
    }
    knob_colors = 0;
    check(!knob_colors && !strcmp(mi_value(MI_KCOL, v, &c), "OFF"), "menu: KNOB COLORS is OFF by default");
    menu_open(MI_KCOL);
    check(menu_screen() == mi_screen_of(MI_HOLD) && mi_row(MI_KCOL) == 1u && mi_screen_of(MI_KCOL) != mi_screen_of(MI_USB),
          "menu: KNOB COLORS is row 2 of the HOLD screen (SYSTEM), not the last (BLUETOOTH) screen");
    encs[MKNOB()] = 0; tap(B_OCTUP); frames(2);
    check(knob_colors == 1u && !strcmp(mi_value(MI_KCOL, v, &c), "ON"), "menu: OCT+ on KNOB COLORS: ON");
    tap(B_OCTUP); frames(2);
    check(knob_colors == 0u, "menu: OCT+ again: OFF");
    encs[panel.enc[EN_K1 + 1]] = 1; frames(2);
    check(knob_colors == 1u && ui.menu_sel == MI_KCOL, "menu: KNOB 2 right sets row 2 ON, the cursor follows");
    encs[panel.enc[EN_K1 + 1]] = -1; frames(2);
    check(knob_colors == 0u, "menu: KNOB 2 left: OFF");
    ui.menu_sel = MI_KCOL; knob_colors = 1; ui.force = 1; frame(); ppm("knobcol-menu");
    knob_colors = 0;
    menu_close();
    frames(2);
    /* the screens: a sound page and a step held, off and on */
#if DL_UI
    song.sel = TRK_DRUM; song.playing = 0; transport_req = 0; go_home(); frames(2);
    key(key_of_white(2));
    tap(B_EDIT); frames(2);                                   /* the drum SOUND page: four gauged cards */
    song.g[G_VIEW] = 0; settings.view = 0; ui.force = 1; frames(2);
    ui.force = 1; frame(); ppm("knobcol-sound-off");
    knob_colors = 1; ui.force = 1; frame(); frame(); ppm("knobcol-sound-on");
    knob_colors = 0;
#endif
    song.sel = 0;
    go_home(); frames(2);
    steps_clear(&trk[0]); trk[0].p[P_SLEN] = 16;
    press(B_SEQ); frames(10);
    key(0);
    fm1_in.notes = 1u << 0; frame();
    ui.force = 1; frame(); ppm("knobcol-held-off");
    knob_colors = 1; ui.force = 1; frame(); frame(); ppm("knobcol-held-on");
    knob_colors = 0;
    fm1_in.notes = 0; frame();
    release(B_SEQ); frames(2);
    steps_clear(&trk[0]);
    go_home(); frames(2);
    knob_colors = was;
}
