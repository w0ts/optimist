/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, SYSTEM > SCREEN > KNOB COLORS (core/knobcol.h, settings word bit 25): off by default; the KNOB COLORS line
 * of SYSTEM's Screen section (or YES on it) sets it; with it ON the four cards' labels and values take the knob caps' colours (1x4 and
 * 2x2); OFF every card is as before. Screens opt-knobcol-*-off / -on.ppm. */
/* the lines of GLOBAL / SYSTEM pages: the left word (x 6..40) of line j (knob j). OFF: white on the non-cursor lines, no knob colour. ON: the cursor
 * line's label is knob j's colour, every other line's label is that colour at 5/8 (knob_col_dim), and no label has a white pixel */
static int knobcol_line_ok(uint32_t j, uint32_t cur, uint32_t on)   /* line j's label box (x 6..40), j = knob j */
{
    uint32_t y = (uint32_t)(OH_HEAD + gl_y0()) + j * (uint32_t)gl_pitch(), n = (uint32_t)gl_pitch();
    uint16_t full = KNOB_COL[j], dim = knob_col_dim(j, C_WHITE);
    if (!on)
        return !px_in(6, y, 34, n, full) && (cur || px_in(6, y, 34, n, C_WHITE));
    if (cur)
        return px_in(6, y, 34, n, full) && !px_in(6, y, 34, n, C_WHITE);
    return px_in(6, y, 34, n, dim) && !px_in(6, y, 34, n, C_WHITE);   /* (the bar under the line is in full colour: only the label's dim is asked) */
}
static int knobcol_labels(uint32_t scr, uint32_t on)
{
    uint32_t j, ok = 1, seen = 0;
    knob_colors = on;
    op_enter(scr);
    ui.force = 1; frame(); frame();
    for (j = 0; j < 4u && gld.first + j < gld.end; j++) {
        cell_t c;
        gl_cell(gld.s, gld.first + j, &c);
        if (!c.label)
            continue;
        seen++;
        ok &= knobcol_line_ok(j, gld.first + j == gld.cur, on);
    }
    return ok && seen >= 2u;
}
/* the PAGE_LINES build (engine and FX pages as lines): KNOB COLORS on, KNOB 1 turned (line 1 the cursor): its label full colour, lines 2..4 dimmed,
 * none white; OFF the labels are white. Shots opt-knobcol-<page>-on.ppm (the engine page, the FX page) */
static void knobcol_page_lines(uint32_t scr, const char *name)
{
    uint32_t j, on, ok;
    cell_t c;
    for (on = 1; on < 2u; on--) {
        knob_colors = on;
        reset_ui();
        song.sel = 0;
        if (scr == SCR_FX) tap(B_FX); else tap(B_EDIT);
        if (scr == SCR_FX) tap(B_FX);           /* (the family's next page: the sends, levels: a turn opens no popup over the lines) */
        ui.force = 1;
        frames(2);
        ok = 1;
        SCR->cell(ui.row[ui.scr], 0, &c);
        if (c.label && c.kind != CK_RO && c.kind != CK_ACT) { turn(EN_K1, 1); frames(2); }
        for (j = 0; j < 4u; j++) {
            cell_t d;
            SCR->cell(ui.row[ui.scr], j, &d);
            if (d.label)
                ok &= knobcol_line_ok(j, ui.hot_lit && ui.hot == j, on);
        }
        check(ok, on ? "KNOB COLORS on, lines of the page: the hot line's label full colour, the others dimmed, none white" : "KNOB COLORS off, lines of the page: no knob colour on a label");
        if (on) ppm(name);
    }
    knob_colors = 0;
}
static void knobcol_label_tests(void)
{
    uint32_t s;
    for (s = 0; s < 2u; s++) {
        uint32_t scr = s ? SCR_SYSTEM : SCR_GLOBAL;
        check(knobcol_labels(scr, 1), s ? "KNOB COLORS on, SYSTEM lines: the active label in full knob colour, the others dimmed (5/8), none white" : "KNOB COLORS on, GLOBAL lines: the active label in full knob colour, the others dimmed (5/8), none white");
        if (s) { knob_colors = 1; op_enter(SCR_SYSTEM); ui.row[SCR_SYSTEM] = (uint8_t)line_named(SCR_SYSTEM, "KNOB COLORS"); ui.force = 1; frames(2); ppm("opt-knobcol-system-on"); }
        check(knobcol_labels(scr, 0), s ? "KNOB COLORS off, SYSTEM lines: labels white, no knob colour (as before)" : "KNOB COLORS off, GLOBAL lines: labels white, no knob colour (as before)");
    }
    knob_colors = 0;
    reset_ui();
}
static void knobcol_tests(void)
{
    uint32_t k, ok = 1;
    reset_ui();
    knob_colors = 0;
    check(knob_colors == 0u && ((bp23_word() >> 25) & 1u) == 0u, "KNOB COLORS: off by default, bit 25 clear");
    k = line_named(SCR_SYSTEM, "KNOB COLORS");             /* (Screen's KNOB COLORS line) */
    ui.force = 1;
    frame();
    turn(LINE_KNOB(k), 1);
    check(knob_colors == 1u && ((bp23_word() >> 25) & 1u) == 1u, "SYSTEM Screen: the KNOB COLORS line's knob turns KNOB COLORS on (bit 25)");
    ppm("opt-knobcol-system");
    turn(LINE_KNOB(k), -1);
    check(knob_colors == 0u, "SYSTEM Screen: its knob back: off");
    reset_ui();
    song.sel = 0;
    op_enter(SCR_SOUND);
    tap(B_ENV);
    ui.force = 1; frame(); frame();
    for (k = 0; k < 4u; k++) {
        uint32_t x = k & 1u ? 121u : 3u, y = OY_CARD + (k & 2u ? BIG_H + 1u : 0u);
        ok &= !px_in(x + 5u, y + 2u, BIG_W - 10u, 10, KNOB_COL[k]);
    }
    check(ok, "KNOB COLORS off: no card carries a knob colour");
    ppm("opt-knobcol-sound-off");
    knob_colors = 1;
    ui.force = 1; frame(); frame();
    for (k = 0, ok = 1; k < 4u; k++) {
        uint32_t x = k & 1u ? 121u : 3u, y = OY_CARD + (k & 2u ? BIG_H + 1u : 0u);
        ok &= px_in(x + 5u, y + 2u, BIG_W - 10u, 10, KNOB_COL[k]);     /* (the label) */
    }
    check(ok, "KNOB COLORS on: card k's label is knob k's colour (the 2x2 cards)");
    ppm("opt-knobcol-sound-on-2x2");
    knob_colors = 0;
    ui.force = 1; frame();
    reset_ui();
    knobcol_label_tests();
}
