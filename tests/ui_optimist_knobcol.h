/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, SYSTEM > SCREEN > KNOB COLORS (core/knobcol.h, settings word bit 25): off by default; KNOB 4 on
 * the SCREEN row (or YES on it) sets it; with it ON the four cards' labels and values take the knob caps' colours (1x4 and
 * 2x2); OFF every card is as before. Screens opt-knobcol-*-off / -on.ppm. */
static void knobcol_tests(void)
{
    uint32_t k, ok = 1;
    reset_ui();
    knob_colors = 0;
    check(knob_colors == 0u && ((bp23_word() >> 25) & 1u) == 0u, "KNOB COLORS: off by default, bit 25 clear");
    op_enter(SCR_SYSTEM);
    ui.force = 1;
    frame();
    turn(EN_K4, 1);
    check(knob_colors == 1u && ((bp23_word() >> 25) & 1u) == 1u, "SYSTEM SCREEN: KNOB 4 turns KNOB COLORS on (bit 25)");
    check(op_cards == CARDS_LINE, "SYSTEM: ... the CARDS row beside it is as it was");
    ppm("opt-knobcol-system");
    turn(EN_K4, -1);
    check(knob_colors == 0u, "SYSTEM SCREEN: KNOB 4 back: off");
    reset_ui();
    song.sel = 0;
    op_enter(SCR_SOUND);
    tap(B_ENV);
    ui.force = 1; frame(); frame();
    for (k = 0; k < 4u; k++)
        ok &= !px_in(CARD_X(k) + 2u, OY_CARD + 2u, 50, 11, KNOB_COL[k]);
    check(ok, "KNOB COLORS off: no card carries a knob colour");
    ppm("opt-knobcol-sound-off");
    knob_colors = 1;
    ui.force = 1; frame(); frame();
    for (k = 0, ok = 1; k < 4u; k++)
        ok &= px_in(CARD_X(k) + 2u, OY_CARD + 2u, 50, 11, KNOB_COL[k]);     /* (the label) */
    check(ok, "KNOB COLORS on: card k's label is knob k's colour (1x4)");
    ppm("opt-knobcol-sound-on");
    op_cards = CARDS_2X2;
    ui.force = 1; frame(); frame();
    for (k = 0, ok = 1; k < 4u; k++) {
        uint32_t x = k & 1u ? 121u : 3u, y = OY_CARD + (k & 2u ? BIG_H + 1u : 0u);
        ok &= px_in(x + 5u, y + 2u, BIG_W - 10u, 10, KNOB_COL[k]);
    }
    check(ok, "KNOB COLORS on: the 2x2 cards' labels too");
    ppm("opt-knobcol-sound-on-2x2");
    op_cards = CARDS_LINE;
    knob_colors = 0;
    ui.force = 1; frame();
    reset_ui();
}
