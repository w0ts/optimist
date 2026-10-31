/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, the cursor row of every vertical list (fix/cursor-line, 2026-10-14; the user: the orange bar with
 * grey text was hard to read, then: the dark tint is not recognisable). The row's background is the colour the context uses at
 * FULL strength (the track's colour on a track list), and the row's text is its ink, black or white, whichever contrasts more
 * (gfx.c ink_on; the user, 2026-10-18: white on a light colour was hard to read). ink_on's choice is checked against WCAG's,
 * computed here exactly from the RGB565 values, for every colour that can be that background (engines, kit kinds, knobs). */
static double cl_lin(uint32_t v8)                       /* an 8-bit sRGB channel, linear */
{
    double v = (double)v8 / 255.0;
    return v <= 0.03928 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4);
}
static double cl_lum(uint16_t c)                        /* c's relative luminance (the 565 fields widened as the panel does) */
{
    uint32_t r = (c >> 11) & 31u, g = (c >> 5) & 63u, b = c & 31u;
    return 0.2126 * cl_lin(r << 3 | r >> 2) + 0.7152 * cl_lin(g << 2 | g >> 4) + 0.0722 * cl_lin(b << 3 | b >> 2);
}
/* ink_on(c) is the ink of the higher contrast, but where the two are within 5 % (either reads the same) */
static int cl_ink_ok(uint16_t c)
{
    double l = cl_lum(c), w = 1.05 / (l + 0.05), k = (l + 0.05) / 0.05;
    uint16_t want = k > w ? C_BLACK : C_WHITE;
    int ok = ink_on(c) == want || (w > k ? w / k : k / w) < 1.05;
    if (!ok)
        printf("  curline: colour %04X: white %.2f, black %.2f, ink_on %s\n", c, w, k, ink_on(c) == C_BLACK ? "black" : "white");
    return ok;
}
static int cl_palette_contrast(void)
{
    uint32_t i;
    uint16_t kinds[4] = {COL_KIND_SYNTH, COL_KIND_SAMPLED, COL_KIND_X0X, COL_KIND_USERKIT};
    int ok = ink_on(C_BLACK) == C_WHITE && ink_on(C_WHITE) == C_BLACK;
    for (i = 0; i < NENGINES; i++)
        ok &= cl_ink_ok(ENG_COL[i]);
    for (i = 0; i < 4u; i++)
        ok &= cl_ink_ok(kinds[i]) & cl_ink_ok(KNOB_COL[i]);
    for (i = 0; i < 65536u; i += 97u)                   /* (a sweep of the whole 565 space) */
        ok &= cl_ink_ok((uint16_t)i);
    return ok;
}
static uint32_t cl_count(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t col)   /* pixels of col in the box */
{
    int32_t x, y;
    uint32_t n = 0;
    for (y = y0; y < y1; y++)
        for (x = x0; x < x1; x++)
            n += screen[y * 240 + x] == swap16(col);
    return n;
}
/* the row at screen y, h px high: its left margin is the full colour, its text the colour's ink (and some of it), none of
 * the other ink */
static int cl_row(const char *what, int32_t y, int32_t h, uint16_t full, uint16_t bg)
{
    uint16_t ink = ink_on(full), other = ink == C_WHITE ? C_BLACK : C_WHITE;
    int ok = bg == full && screen[(y + 1) * 240 + 1] == swap16(full) && cl_count(0, y, 3, y + h, full) == (uint32_t)(3 * h) &&
             cl_count(4, y, 236, y + h, ink) > 20u && cl_count(4, y, 236, y + h, other) == 0u;
    if (!ok)
        printf("  curline: %s: bg %04X full %04X, margin %04X, white %u, black %u, full-colour margin px %u\n", what, bg, full,
               swap16(screen[(y + 1) * 240 + 1]), cl_count(4, y, 236, y + h, C_WHITE), cl_count(4, y, 236, y + h, C_BLACK),
               cl_count(0, y, 3, y + h, full));
    return ok;
}
/* GLOBAL, SYSTEM (the user, 2026-10-14, "invert the highlight"): the cursor line has no fill, its text (number, name, value) is the
 * line's colour; the other lines' names stay grey. The label's columns (x 24..120) hold the line's colour on the cursor line only */
static int cl_gl(uint32_t scr, uint32_t colors)
{
    uint32_t s = scr == SCR_SYSTEM, i, ok = 1;
    uint32_t keep = knob_colors;
    knob_colors = colors;
    op_enter(scr);
    for (i = 0; i < gl_n(s) && ok; i++) {
        uint32_t f, k, j;
        ui.row[scr] = (uint8_t)i;
        ui.force = 1;
        frame();
        f = gld.first;
        k = i - f;
        if (gl_textpage() || k >= GL_PAGE)
            continue;
        for (j = 0; j < gld.end - f; j++) {
            int32_t y = OH_HEAD + gl_y0() + (int32_t)j * gl_pitch(), h = gl_pitch() - 2;
            uint16_t lc = knob_col(j, trk_col(song.sel));
            cell_t c;
            gl_lrow_t lr;
            int32_t ty, te, nx;
            uint32_t lab, fill;
            gl_cell(s, f + j, &c);
            gl_lrow(&c, gl_y0() + (int32_t)j * gl_pitch(), &lr);   /* (the name's rows and columns: a large value may reach left of x 120) */
            ty = OH_HEAD + lr.ty, te = ty + (int32_t)FONT_S.h, nx = 6 + text_w(&FONT_S, lr.nm);
            lab = cl_count(6, ty, nx, te, lc), fill = cl_count(0, y, 3, y + h, lc) + cl_count(0, y, 3, y + h, C_WHITE);
            ok &= fill == 0u && screen[(y + 1) * 240 + 1] == swap16(C_BLACK);   /* (no fill, whatever the line) */
            ok &= j == k ? (lab > 8u || lr.nm[0] == ' ') && cl_count(6, ty, nx, te, C_WHITE) == 0u : lab == 0u;   /* (the name in the line's colour on
                                                         * the cursor, white elsewhere; ABOUT's link line has a blank name) */
            if (!ok)
                printf("  curline: %s line %u (cursor %u): label px %u, fill px %u\n", scr == SCR_SYSTEM ? "SYSTEM" : "GLOBAL", f + j, i, lab, fill);
        }
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
    check(cl_palette_contrast(), "cursor colours: ink_on picks WCAG's better ink on every engine, kit-kind and knob colour, and over the 565 space");
    song.sel = 0;
    check(cl_gl(SCR_GLOBAL, 0u) && cl_gl(SCR_GLOBAL, 1u), "GLOBAL: the cursor line has no fill, its text is the line's colour (the others' names grey)");
    check(cl_gl(SCR_SYSTEM, 0u) && cl_gl(SCR_SYSTEM, 1u), "SYSTEM: the cursor line has no fill, its text is the line's colour (the others' names grey)");
    check(cl_list("PROJECT", SCR_PROJECT, 0u, 4u), "PROJECT: the cursor row is the track's colour at full strength, its text the colour's ink");
    for (r = 0; r < 4u; r++) {                          /* a SOUND list: the first rows of SOUND with no picture */
        uint32_t i;
        op_enter(SCR_SOUND);
        for (i = 0; i < SCREENS[SCR_SOUND].rows() && !found; i++) {
            ui.row[SCR_SOUND] = (uint8_t)i;
            ui.force = 1;
            frame();
            if (lst.shown > 0u && lst.pic == 0u) {
                found = 1;
                check(cl_list("SOUND", SCR_SOUND, i, 3u), "SOUND: a row list's cursor row is the track's colour at full strength, its text the colour's ink");
            }
        }
        if (found)
            break;
        song.sel = (uint8_t)(song.sel + 1u);            /* (another track's sound: other rows) */
    }
    check(found, "SOUND: some track has a row list with no picture to check");
    reset_ui();
}
