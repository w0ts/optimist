/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, the user's ruling of 2026-10-15: a muted track is struck through, in the mixer (its name), the
 * header (the badge) and its volume card (label and value); a line in the text's own colour through the glyphs' middle.
 * Unmuted: no line. Included after ui_optimist_mute.h. */
static int strike_run(int32_t x, int32_t y, int32_t w, int solid_col)   /* the row's w px one colour (solid_col: that colour, else any but black) */
{
    int32_t i;
    uint16_t c0 = screen[(uint32_t)y * 240u + (uint32_t)x];
    if (w <= 0 || (solid_col >= 0 && c0 != swap16((uint16_t)solid_col)) || (solid_col < 0 && c0 == swap16(C_BLACK)))
        return 0;
    for (i = 1; i < w; i++)
        if (screen[(uint32_t)y * 240u + (uint32_t)(x + i)] != c0)
            return 0;
    return 1;
}
static void strike_tests(void)
{
    cell_t c;
    char en[10], pr[16];
    uint32_t i, r = MXR_T1 + 1u;
    int32_t sy, w, bw;
    for (i = 0; i < NTRK; i++)
        trk[i].p[P_MUTE] = 0;
    reset_ui();
    song.sel = 1;
    ui.scr = SCR_HOME;
    ui.row[SCR_HOME] = r;
    mx.set = 0;
    mx_names(r, en, pr);
    w = text_w(&FONT_S, en);
    sy = mx_row_y(r) + MXL_TY + (int32_t)FONT_S.h / 2 - 1;
    frames(2);
    mix_cell(r, 0, &c);
    check(!c.strike && !strike_run(MXL_VX, sy, w, -1), "unmuted: the mixer row's name and the volume card are not struck");
    trk[1].p[P_MUTE] = 1;
    frames(2);
    mix_cell(r, 0, &c);
    check(strike_run(MXL_VX, sy, w, -1), "muted: the mixer row's engine name has its line (one colour across the text)");
    check(c.strike && c.label && !strcmp(c.label, "VOLUME"), "muted: the VOLUME card carries the strike");
    sy = OY_CARD + 3 + (int32_t)FONT_S.h / 2 - 1;
    check(strike_run(CARD_X(0) + 3, sy, 16, -1), "muted: the card's label is struck (pixels)");
    sy = OY_CARD + 20 + (int32_t)FONT_S.h / 2 - 1;
    check(strike_run(CARD_X(0) + 3, sy, text_w(&FONT_S, c.val), -1), "muted: the card's value is struck (pixels)");
    op_enter(SCR_SOUND);
    tf.t = 0;                                           /* (the track-name flash over: the page's own header) */
    frames(2);
    bw = text_w(&FONT_S, head_badge((char[24]){0}, 24)) ;
    sy = 4 + (int32_t)FONT_S.h / 2 - 1;
    check(strike_run(2 + HEAD_PAD, sy, bw, -1), "muted: the header badge is struck, its colour across the text (no fill, 2026-10-18)");
    trk[1].p[P_MUTE] = 0;
    frames(2);
    check(!strike_run(2 + HEAD_PAD, sy, bw, -1), "unmuted: the header badge has no line");
    op_enter(SCR_HOME);
    ui.row[SCR_HOME] = r;
    frames(2);
    mix_cell(r, 0, &c);
    check(!c.strike, "unmuted again: the card is clean");
    trk[1].p[P_MUTE] = 0;
    reset_ui();
}
