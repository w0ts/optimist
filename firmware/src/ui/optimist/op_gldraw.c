/* SPDX-License-Identifier: GPL-3.0-only */
/* GLOBAL and SYSTEM drawn as a list (op_global.c; docs/UI-OPTIMIST-DESIGN.md section 20): no cards, the page's 4 lines
 * from under the header to the screen's foot, a section's name over its first line on the page (a page that starts
 * inside a section names it again). A line: its knob's number (1..4, in the knob's colour with KNOB COLORS; dim on a
 * read-out, which no knob turns), the setting's name, its value on the right as the cards write it (amber: a read-out,
 * or an action's word: gl_val), the value's form under it. The cursor line: a bar in the track's colour, ink on it. Lazy, as
 * every band: drawn again when its signature changes. */
#define GL_TOP OH_HEAD                  /* the list: under the header, to the screen's foot */
#define GL_BAND (240 - OH_HEAD)
#define GL_SEC_H 20                     /* a section's name */
#if FELUCCA_PARAM_HELP
#define GL_LINE_H 29                    /* a line: 4 lines and 4 names fit over the help line (4 x 29 + 4 x 20 = 196, op_help.c) */
#else
#define GL_LINE_H 33                    /* a line: 4 lines and 4 names fit (4 x 33 + 4 x 20 = 212) */
#endif
#define GL_GX 140                       /* the value's form: x, width */
#define GL_GW 96
static struct {
    uint32_t s, first, end, n, cur;
    uint16_t bar;
} gld;                                                  /* what gl_paint draws (gl_draw) */
/* a line's value as drawn: an action says what it does (the user, 2026-10-13: lines showing "YES" read as dead), RUN
 * when its cell names nothing; YES or PRESETS runs it (op_global.c gl_presets) */
static const char *gl_val(const cell_t *c) { return c->kind != CK_ACT ? c->val : c->val[0] ? c->val : "RUN"; }
static void gl_line_draw(uint32_t k, int32_t y, const cell_t *c, uint32_t on)
{
    char b[16], m[2];
    const char *v = gl_val(c);
    uint16_t vc = c->kind == CK_RO || c->kind == CK_ACT ? (c->col ? c->col : C_AMB) : knob_col(k, c->col ? c->col : C_HI);
    int32_t uw = c->kind != CK_ACT && c->unit[0] ? text_w(&FONT_S, c->unit) : 0, x;
    m[0] = (char)('1' + k);
    m[1] = 0;
    if (on)
        cv_rect(0, y, 240, GL_LINE_H - 2, gld.bar);
    cv_text(6, y + 4, &FONT_S, m, on ? C_BLACK : c->kind == CK_RO ? C_LINE : knob_col(k, C_HI));
    cv_text(24, y + 4, &FONT_S, cut(b, op_label(b, c->label ? c->label : "", sizeof b), 12), on ? C_BLACK : C_GRAY);
    cut(b, v, 13);
    x = 236 - uw - (uw ? 4 : 0) - text_w(&FONT_S, b);   /* (a unit: 4 px after the value) */
    cv_text(x, y + 4, &FONT_S, b, on ? C_BLACK : vc);
    if (uw)
        cv_text(236 - uw, y + 4, &FONT_S, c->unit, on ? C_BLACK : C_DIM);
    if (c->kind != CK_ACT && c->gk != GK_NONE)
        draw_gauge(GL_GX, y + 23, GL_GW, 3, c, on ? C_BLACK : vc, on ? col_shade(gld.bar, 5u) : C_LINE);
}
static void gl_paint(void)
{
    uint32_t i;
    int32_t y = 0;
    char sec[12], prev[12], b[24], lab[24];
    cell_t c;
    prev[0] = 0;
    for (i = gld.first; i < gld.end; i++) {
        gl_sect(gld.s, i, sec);
        if (!str_eq(sec, prev)) {                       /* a section: its name, dim, a rule after it */
            int32_t w;
            str_cpy(lab, op_case(b, sec, sizeof b), sizeof lab);
            if (i == gld.first)
                gl_split(gld.s, i, lab);  /* (a section split over pages: "REC 1/2") */
            w = cv_text(4, y + 2, &FONT_S, lab, C_DIM) + 6;
            cv_rect(w, y + 10, 236 - w, 1, C_LINE);
            y += GL_SEC_H;
            str_cpy(prev, sec, sizeof prev);
        }
        gl_cell(gld.s, i, &c);
        gl_line_draw(i - gld.first, y, &c, i == gld.cur);
        y += GL_LINE_H;
    }
}
static void gl_draw(void)
{
    uint32_t i, sig;
    char nm[12];
    cell_t c;
    gld.s = ui.scr == SCR_SYSTEM;
    gld.n = gl_n(gld.s);
    gld.cur = ui.row[ui.scr];
    gld.first = gl_first(gld.s, gld.cur);
    gld.end = gl_end(gld.s, gld.cur);
    gld.bar = trk_col(song.sel);
    sig = hu(hu(hu(hu(0x6C15u, gld.s), gld.cur), gld.bar + settings.palette * 65536u), knob_colors);
    for (i = gld.first; i < gld.end; i++) {
        gl_sect(gld.s, i, nm);
        gl_cell(gld.s, i, &c);
        sig = hc(hs(sig, nm), &c);
    }
    if (sig == ui.sig[2])
        return;
    ui.sig[2] = sig;
    cv_tall(GL_TOP, GL_BAND, C_BLACK, gl_paint);
}
