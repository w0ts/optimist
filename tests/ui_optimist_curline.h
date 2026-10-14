/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, the cursor row of every vertical list (fix/cursor-line, 2026-10-14; the user: the orange bar with
 * grey text was hard to read). The row's background is a dark tint of the colour the context uses (cur_bg: 3/8 of the
 * track's colour on a track list, of the knob's colour on a GLOBAL / SYSTEM line with KNOB COLORS, else the track's), and the
 * row's text is white: no black ink, no full-colour bar. Checked on GLOBAL, SYSTEM, PROJECT and a SOUND list. */
static uint32_t cl_count(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t col)   /* pixels of col in the box */
{
    int32_t x, y;
    uint32_t n = 0;
    for (y = y0; y < y1; y++)
        for (x = x0; x < x1; x++)
            n += screen[y * 240 + x] == swap16(col);
    return n;
}
/* the row at screen y, h px high: its left margin is the tint, none of it the full colour, its text white and not black */
static int cl_row(const char *what, int32_t y, int32_t h, uint16_t full, uint16_t tint)
{
    int ok = tint != full && tint != C_BLACK && screen[(y + 1) * 240 + 1] == swap16(tint) && cl_count(0, y, 3, y + h, full) == 0u &&
             cl_count(4, y, 236, y + h, C_WHITE) > 20u && cl_count(4, y, 236, y + h, C_BLACK) == 0u;
    if (!ok)
        printf("  curline: %s: tint %04X full %04X, margin %04X, white %u, black %u, full-colour %u\n", what, tint, full,
               swap16(screen[(y + 1) * 240 + 1]), cl_count(4, y, 236, y + h, C_WHITE), cl_count(4, y, 236, y + h, C_BLACK),
               cl_count(0, y, 3, y + h, full));
    return ok;
}
static int cl_gl(uint32_t scr, uint32_t colors)
{
    uint32_t s = scr == SCR_SYSTEM, i, ok = 1;
    uint32_t keep = knob_colors;
    knob_colors = colors;
    op_enter(scr);
    for (i = 0; i < gl_n(s) && ok; i++) {
        uint32_t f, k;
        ui.row[scr] = (uint8_t)i;
        ui.force = 1;
        frame();
        f = gld.first;
        k = i - f;
        if (gl_info() || k >= GL_PAGE)
            continue;
        ok &= cl_row(scr == SCR_SYSTEM ? "SYSTEM" : "GLOBAL", OH_HEAD + gl_y0() + (int32_t)k * gl_pitch(), gl_pitch() - 2,
                     knob_col(k, trk_col(song.sel)), cur_bg(knob_col(k, trk_col(song.sel))));
    }
    knob_colors = (uint8_t)keep;
    return ok;
}
/* a draw_list screen (rows under the cards): the cursor row on each row of the window */
static int cl_list(const char *what, uint32_t scr, uint32_t first_row, uint32_t want_rows)
{
    uint32_t i, n, ok = 1, seen = 0;
    op_enter(scr);
    n = SCREENS[scr].rows();
    for (i = first_row; i < n && seen < want_rows && ok; i++) {
        ui.row[scr] = (uint8_t)i;
        ui.force = 1;
        frame();
        if (lst.shown == 0u || i < lst.first || i >= lst.first + lst.shown)
            continue;
        seen++;
        ok &= cl_row(what, OP_PY + lst.top + (int32_t)(i - lst.first) * lst.rh, lst.rh - 1, trk_col(song.sel), cur_bg(trk_col(song.sel)));
    }
    return ok && seen > 0u;
}
static void curline_tests(void)
{
    uint32_t r, found = 0;
    reset_ui();
    song.sel = 0;
    check(cl_gl(SCR_GLOBAL, 0u) && cl_gl(SCR_GLOBAL, 1u), "GLOBAL: the cursor line is a dark tint of its line's colour, its text white");
    check(cl_gl(SCR_SYSTEM, 0u) && cl_gl(SCR_SYSTEM, 1u), "SYSTEM: the cursor line is a dark tint of its line's colour, its text white");
    check(cl_list("PROJECT", SCR_PROJECT, 0u, 4u), "PROJECT: the cursor row is a dark tint of the track's colour, its text white");
    for (r = 0; r < 4u; r++) {                          /* a SOUND list: the first rows of SOUND with no picture */
        uint32_t i;
        op_enter(SCR_SOUND);
        for (i = 0; i < SCREENS[SCR_SOUND].rows() && !found; i++) {
            ui.row[SCR_SOUND] = (uint8_t)i;
            ui.force = 1;
            frame();
            if (lst.shown > 0u && lst.pic == 0u) {
                found = 1;
                check(cl_list("SOUND", SCR_SOUND, i, 3u), "SOUND: a row list's cursor row is a dark tint of the track's colour, its text white");
            }
        }
        if (found)
            break;
        song.sel = (uint8_t)(song.sel + 1u);            /* (another track's sound: other rows) */
    }
    check(found, "SOUND: some track has a row list with no picture to check");
    reset_ui();
}
