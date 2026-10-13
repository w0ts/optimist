/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, GLOBAL and SYSTEM in logical pages (feat/settings-pages, 2026-10-13; the user: "the global and
 * system screen should be grouped in logical pages, dont cut a page in the middle if possible"). op_global.c GL_GROUPS
 * makes the pages from the lines the build has. Checked in every variant:
 *   no row (a section's cells) straddles a page; a section of up to 4 lines is on one page; a page is 1..4 lines
 *   the pages cover every line once, in order; the header's n/N counts the pages
 *   knob k is line k of its page (the cell, the turn); a knob past the page's last line does nothing
 *   SELECT walks the pages one by one and ALGORITHM reaches every line
 * GLPAGES_DUMP=1 in the environment prints the layout (docs: optimist-work/glpages/LAYOUT.md). Included after
 * ui_optimist_glwalk.h. */
static int gl_same(uint32_t s, uint32_t i, uint32_t j) { return gl_sid(s, gl_ent(s, i)) == gl_sid(s, gl_ent(s, j)); }   /* lines i, j in one section */
static uint32_t glp_sect_len(uint32_t s, uint32_t i)   /* the lines of line i's section (its name, whole) */
{
    uint32_t a = i, b = i;
    while (a > 0u && gl_same(s, a - 1u, i))
        a--;
    while (b + 1u < gl_n(s) && gl_same(s, b + 1u, i))
        b++;
    return b - a + 1u;
}
static void glp_dump(uint32_t s)
{
    uint32_t p, i, np = gl_npg(s);
    char nm[12], at[24];
    cell_t c;
    for (p = 0; p < np; p++) {
        printf("  glpages: %s page %u/%u:", s ? "SYSTEM" : "GLOBAL", p + 1u, np);
        for (i = gl_pfirst(s, p); i < gl_pfirst(s, p + 1u); i++) {
            gl_sect(s, i, nm);
            gl_cell(s, i, &c);
            snprintf(at, sizeof at, "%s", i == gl_pfirst(s, p) || !gl_same(s, i - 1u, i) ? nm : "");
            printf("%s%s%s%s", at[0] ? " [" : " ", at, at[0] ? "]" : "", c.label ? c.label : "-");
        }
        printf("\n");
    }
}
static int glp_layout(uint32_t s)                      /* the pages' shape */
{
    uint32_t p, i, np = gl_npg(s), ok = np > 0u && gl_pfirst(s, 0) == 0u && gl_pfirst(s, np) == gl_n(s);
    for (p = 0; p < np && ok; p++) {
        uint32_t f = gl_pfirst(s, p), e = gl_pfirst(s, p + 1u);
        ok &= e > f && e - f <= GL_PAGE;                /* (1..4 lines) */
        for (i = f; i < e && ok; i++)
            ok &= gl_pg(s, i) == p && gl_first(s, i) == f && gl_end(s, i) == e;
        if (f > 0u)                                     /* (a row's cells never continue on the next page) */
            ok &= (gl_ent(s, f) >> 2) != (gl_ent(s, f - 1u) >> 2);
    }
    for (i = 0; i < gl_n(s) && ok; i++) {               /* a section of up to 4 lines: all on one page */
        uint32_t a = i, n = glp_sect_len(s, i);
        while (a > 0u && gl_same(s, a - 1u, i))
            a--;
        if (n <= GL_PAGE)
            ok &= gl_pg(s, a) == gl_pg(s, a + n - 1u);
    }
    if (!ok)
        printf("  glpages: screen %u layout wrong\n", s);
    return (int)ok;
}
static int glp_knobs(uint32_t s)                       /* knob k = line k of the page; the empty ones do nothing */
{
    uint32_t scr = s ? SCR_SYSTEM : SCR_GLOBAL, p, k, np = gl_npg(s), ok = 1;
    cell_t c, d;
    for (p = 0; p < np; p++) {
        uint32_t f = gl_pfirst(s, p), e = gl_pfirst(s, p + 1u);
        op_enter(scr);
        ui.row[scr] = (uint8_t)f;
        frame();
        for (k = 0; k < GL_PAGE; k++) {
            SCREENS[scr].cell(f, k, &c);
            if (f + k < e) {
                gl_cell(s, f + k, &d);
                ok &= c.kind == d.kind && c.label == d.label && !strcmp(c.val, d.val) && c.kind != CK_NONE;
            } else {
                uint32_t h0 = glw_state(), row = ui.row[scr];
                ok &= c.kind == CK_NONE;
                turn(EN_K1 + k, 3);
                turn(EN_K1 + k, -3);
                ok &= glw_state() == h0 && ui.row[scr] == row;
                SCREENS[scr].turn(f, k, 1, 0);
                ok &= glw_state() == h0;
            }
        }
    }
    if (!ok)
        printf("  glpages: screen %u knobs wrong\n", s);
    return (int)ok;
}
static int glp_walk(uint32_t s)                        /* SELECT page by page, ALGORITHM line by line, the n/N */
{
    uint32_t scr = s ? SCR_SYSTEM : SCR_GLOBAL, p, i, np = gl_npg(s), ok = 1;
    char pos[12], want[12];
    op_enter(scr);
    ui.row[scr] = 0;
    frame();
    for (p = 0; p < np; p++) {
        head_pos(scr, ui.row[scr], pos);
        snprintf(want, sizeof want, " %u/%u", p + 1u, np);
        ok &= ui.row[scr] == gl_pfirst(s, p) && (np < 2u ? !pos[0] : !strcmp(pos, want));
        if (p + 1u < np)
            turn(EN_SELECT, 1);
    }
    turn(EN_SELECT, 5);
    ok &= ui.row[scr] == gl_pfirst(s, np - 1u);            /* (stops at the last page) */
    turn(EN_SELECT, -50);
    ok &= ui.row[scr] == 0u;
    for (i = 1; i < gl_n(s); i++) {                     /* every line, the page shown its own */
        turn(EN_ALGO, 1);
        head_pos(scr, ui.row[scr], pos);
        snprintf(want, sizeof want, " %u/%u", gl_pg(s, i) + 1u, np);
        ok &= ui.row[scr] == i && ui.hot == i - gl_first(s, i) && (np < 2u ? !pos[0] : !strcmp(pos, want));
    }
    if (!ok)
        printf("  glpages: screen %u walk wrong\n", s);
    return (int)ok;
}
static void glpages_tests(void)
{
    uint32_t s;
    reset_ui();
    song.sel = 0;
    for (s = 0; s < 2u; s++) {
        if (getenv("GLPAGES_DUMP"))
            glp_dump(s);
        check(glp_layout(s), s ? "SYSTEM's pages: 1..4 lines, whole rows, a section of up to 4 lines on one page, one section a page, every line once"
                               : "GLOBAL's pages: 1..4 lines, whole rows, a section of up to 4 lines on one page, one section a page, every line once");
        check(glp_knobs(s), s ? "SYSTEM: knob k is line k of its page; the knobs past a short page's end do nothing"
                              : "GLOBAL: knob k is line k of its page; the knobs past a short page's end do nothing");
        check(glp_walk(s), s ? "SYSTEM: SELECT page by page (n/N), ALGORITHM reaches every line, the cursor's knob its place"
                             : "GLOBAL: SELECT page by page (n/N), ALGORITHM reaches every line, the cursor's knob its place");
    }
    reset_ui();
}
