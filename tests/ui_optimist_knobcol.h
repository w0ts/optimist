/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, SYSTEM > SCREEN > KNOB COLORS (core/knobcol.h, settings word bit 25): off by default; the KNOB COLORS line
 * of SYSTEM's Screen section (or YES on it) sets it; with it ON the four cards' labels and values take the knob caps' colours (1x4 and
 * 2x2); OFF every card is as before. Screens opt-knobcol-*-off / -on.ppm. */
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
}
