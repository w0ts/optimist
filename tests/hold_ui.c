/* SPDX-License-Identifier: GPL-3.0-only */
/* HOLD (HOME menu > SYSTEM; firmware/src/core/hold.h), included by ui_pages_test.c after menu_ui.c. A layer button held
 * shows its map only after HOLD ms (default 350, or 250 / 500); a press shorter than that is a tap (its pages); the layer is
 * active from the press, so a key touched while it is held acts at once and the map shows then. Frames are 16 ms. */
static void hold_press_for(uint32_t b, uint32_t ms)       /* press b and keep it down about ms (the release not sent) */
{
    press(b);
    frames((ms - 1u) / 16u);
}
static void hold_ui_tests(void)
{
    uint32_t sel;
    uint8_t was = hold_sel;
    hold_sel = 0;
    check(HOLD_MS == 350u && TAP_MS == 350u && SHOW_MS == 350u, "HOLD: the default is 350 ms, for the tap and the map alike");
    for (sel = 0; sel < HOLD_N; sel++) {
        static const uint32_t MS[HOLD_N] = {350, 250, 500};
        uint32_t ms = MS[sel], click = ms - 100u, long_hold = ms + 100u;
        char what[96];
        hold_sel = (uint8_t)sel;
        check(HOLD_MS == ms, "HOLD: the setting gives its milliseconds");
        /* a click: no map, the layer's pages on release */
        go_home(); ui.force = 1; frame();
        hold_press_for(B_FX, click);
        snprintf(what, sizeof what, "HOLD %u: FX held %u ms: no map yet, the punch is armed", ms, click);
        check(ui.layer == LY_PLAY && punch.hold, what);
        release(B_FX);
        snprintf(what, sizeof what, "HOLD %u: a %u ms click opens the FX pages (a tap)", ms, click);
        check(cur_fam() == FAM_FX && ui.layer == LY_PLAY, what);
        /* a hold: the map shows, and the release is no tap */
        go_home(); ui.force = 1; frame();
        hold_press_for(B_FX, long_hold);
        snprintf(what, sizeof what, "HOLD %u: FX held %u ms: the map shows", ms, long_hold);
        check(ui.layer == LY_FX && punch.hold, what);
        release(B_FX);
        snprintf(what, sizeof what, "HOLD %u: ... and the release is no tap (no FX pages)", ms);
        check(ui.layer == LY_PLAY && cur_fam() != FAM_FX, what);
        frames(20);
        /* held + a key: acts at once, the map with it, long before the threshold */
        go_home(); ui.force = 1; frame();
        punch.keybit = 0; punch.req = -1;
        press(B_FX);
        fm1_in.notes = 1u << 4; frame();
        snprintf(what, sizeof what, "HOLD %u: FX + a key straight away: the effect acts and the map shows", ms);
        check(punch.req == 2 && ui.layer == LY_FX, what);
        fm1_in.notes = 0; frame();
        release(B_FX);
        snprintf(what, sizeof what, "HOLD %u: ... and the release after a key is no tap", ms);
        check(cur_fam() != FAM_FX, what);
        frames(20);
    }
    hold_sel = was;
}
