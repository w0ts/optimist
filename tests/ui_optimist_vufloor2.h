/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, SYSTEM > METERS > VU FLOOR and KNOB COLORS: a value turned does not clear the whole screen to
 * black (the user, 2026-10-31: "when i change the vumeters floor setting the screen flashes"). Counted by full_fills;
 * the screen after must equal the one a full ui.force redraw gives. Included after reset_ui / line_named / turn */

static void vufloor_blink_tests(void)
{
    uint32_t w, n, h, ok = 1u, same = 1u, i, k, f0 = vu_floor_i, kc = knob_colors;
    reset_ui();
    song.sel = 0;
    op_enter(SCR_SYSTEM);
    frames(3);
    for (k = 0; k < 2u; k++) {
        w = line_named(SCR_SYSTEM, k ? "KNOB COLORS" : "VU FLOOR");
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
        check(ok, k ? "KNOB COLORS turned: no fill of the whole screen" : "VU FLOOR turned: no fill of the whole screen");
        check(same, "... the screen as a full redraw gives it");
    }
    vu_floor_i = (uint8_t)f0;
    knob_colors = (uint8_t)kc;
    reset_ui();
}
