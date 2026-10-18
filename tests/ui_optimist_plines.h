/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c built with FELUCCA_PAGE_LINES=1 (the builder's default; tests/run_tests.sh's lines build): a
 * screen of rows with no picture shows its cursor row as GLOBAL's four lines (op_draw.c pl_on, op_gldraw.c pl_draw), no
 * 2x2 cards and no row list; a row with a picture, the mixer, STEP, GLOBAL keep their own (the user, 2026-10-18: "replace
 * the screen where we have 2*2 cards and the list of pages below, by the same layout as system and glo"). The rest of the
 * suite assumes the cards: this build runs these checks and the random frames only. */
static uint32_t pl_find(int want_pic)                   /* the first row of the screen with (or without) a picture, rows(): none */
{
    uint32_t r, n = SCR->rows();
    for (r = 0; r < n; r++)
        if ((row_pic(r) != 0u) == (want_pic != 0))
            return r;
    return n;
}
static void plines_tests(void)
{
    uint32_t r, pitch = (uint32_t)gl_pitch(), y1 = (uint32_t)(OH_HEAD + gl_y0());
    cell_t c;
    reset_ui();
    song.sel = 0;
    tap(B_EDIT);
    frames(2);
    r = pl_find(0);
    check(ui.scr == SCR_SOUND && r < SCR->rows(), "PAGE_LINES: SOUND has a row with no picture");
    ui.row[ui.scr] = r;
    ui.force = 1;
    frames(2);
    SCR->cell(r, 0, &c);
    check(pl_on() && ui.sig[1] == 0u, "... it is lines: no 2x2 cards drawn");
    check(!c.label || px_in(6, y1, 120, pitch, C_WHITE), "... line 1: its name in white (no knob turned)");
    check(!px_in(0, (uint32_t)OY_PANEL_2X2, 240, 40, cur_bg(trk_col(song.sel))), "... no list cursor row under it");
    ppm("opt-plines-edit");
    if (c.label && c.kind != CK_RO && c.kind != CK_ACT) {
        turn(EN_K1, 1);
        frames(2);
        check(ui.hot == 0u && ui.hot_lit && px_in(6, y1, 120, pitch, knob_col(0, trk_col(song.sel))),
              "... KNOB 1 turned: line 1 the cursor, in the line's colour, as GLOBAL's");
        turn(EN_K1, -1);
    }
    tap(B_FX);                                          /* the FX family: the slots' needles repeat the values, no picture */
    frames(2);
    {
        uint32_t n = SCR->rows(), found = 0, ok = 1;
        for (r = 0; r < n; r++) {
            const page_t *gp = snd_graph_page(r);
            if (!gp || (gp->graph != GR_INS && gp->graph != GR_FX))
                continue;
            found++;
            ui.row[ui.scr] = r;
            frames(1);
            ok &= pl_on();
        }
        check(found && ok, "... a synth track's FX pages (INSERTS, SENDS): lines, no needles under 2x2 cards (2026-10-18)");
    }
    tap(B_ENV);
    frames(2);
    check(row_pic(ui.row[ui.scr]) && !pl_on() && cards_band_is(BAND_2X2), "... ENV (a picture) keeps the 2x2 cards over it");
    song.sel = TRK_DRUM;                                /* the drum track, a lane picked: its EDIT pages (the lane's picture,
                                                         * its name and kit, is no picture here) */
    lane_select(0);
    ui.lane = 1;
    tap(B_EDIT);
    frames(2);
    {
        uint32_t n = SCR->rows(), found = 0, ok = 1;
        for (r = 0; r < n; r++) {
            const page_t *gp = snd_graph_page(r);
            if (!gp || gp->graph != GR_DSND)
                continue;
            found++;
            ui.row[ui.scr] = r;
            frames(1);
            ok &= pl_on();
        }
        check(found && ok, "... a drum lane's pages: lines, not the lane's name and kit under 2x2 cards");
    }
    ppm("opt-plines-lane");
    song.sel = 0;
    op_enter(SCR_HOME);
    frames(2);
    check(!pl_on(), "... the mixer keeps its 1x4 cards and rows");
    op_enter(SCR_GLOBAL);
    frames(2);
    check(!pl_on() && glo_cycle(), "... GLOBAL is its own list");
    op_enter(SCR_PROJECT);
    frames(2);
    r = SCR->rows();
    {
        uint32_t i, ok = 1;
        for (i = 0; i < r; i++) {
            ui.row[SCR_PROJECT] = i;
            frames(1);
            ok &= pl_on() != prj_listing();
        }
        check(ok, "... PROJECT: every row lines, but the slot listings");
    }
    reset_ui();
}
