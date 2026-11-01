/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, SYSTEM > General > KNOB COLORS: a value turned does not clear the whole screen to
 * black (the user, 2026-10-31: "when i change the vumeters floor setting the screen flashes"; that setting is gone, the check stays on KNOB COLORS). Counted by full_fills;
 * the screen after must equal the one a full ui.force redraw gives. Included after reset_ui / line_named / turn */

static void knobcolors_blink_tests(void)
{
    uint32_t w, n, h, ok = 1u, same = 1u, i, kc = knob_colors;
    reset_ui();
    song.sel = 0;
    op_enter(SCR_SYSTEM);
    frames(3);
    w = line_named(SCR_SYSTEM, "KNOB COLORS");
    for (i = 0; i < 3u; i++) {
        n = full_fills;
        turn(LINE_KNOB(w), 1);
        frames(3);
        ok &= full_fills == n;
        h = screen_hash();
        ui.force = 1;
        frames(3);
        same &= screen_hash() == h;
    }
    check(ok, "KNOB COLORS turned: no fill of the whole screen");
    check(same, "... the screen as a full redraw gives it");
    knob_colors = (uint8_t)kc;
    reset_ui();
}
