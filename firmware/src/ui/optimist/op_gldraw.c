/* SPDX-License-Identifier: GPL-3.0-only */
/* GLOBAL and SYSTEM drawn as a list (op_global.c; docs/UI-OPTIMIST-DESIGN.md section 20): no cards, the page's 4 lines
 * from under the header to the screen's foot, under the page's title in the large face ("Channels 5/5": the section's name,
 * or a joined pair's, and the page n/N; the header says only Global or System). The 4 lines share the room down to the help
 * line evenly (gl_pitch). A line: its knob's number (1..4, in the knob's colour with KNOB COLORS; dim on a read-out, which
 * no knob turns), the setting's name, its value on the right as the cards write it (amber: a read-out, or an action's
 * word: gl_val), the value's form under it: a bar over the screen's width, a segmented one for a short list, an on / off
 * value a square mark at the right instead (filled on, outlined off). The cursor line: a bar in the track's colour, ink
 * on it. Lazy, as every band: drawn again when its signature changes. */
#define GL_TOP OH_HEAD                  /* the list: under the header, to the screen's foot */
#define GL_BAND (240 - OH_HEAD)
#if FELUCCA_PARAM_HELP
#define GL_FOOT 18                      /* the help line's rows (op_help.c HL_H) */
#else
#define GL_FOOT 0
#endif
#define GL_TOP_Y 3                      /* the page title's top, in the band */
#define GL_END (GL_BAND - GL_FOOT - 2)  /* the lines' room ends here */
#define GL_BX 6                         /* the value bar: the screen's width less a margin each side */
#define GL_BW 228
#define GL_BH 4                         /* its thickness */
#define GL_TW 228                       /* the title's room */
#define GL_CK 10                        /* the on / off mark's side */
static const felucca_font_t *font_big(void);            /* op_graph.c (the large face with lower case) */
static struct {
    uint32_t s, first, end, n, cur;
    uint16_t bar;
} gld;                                                  /* what gl_paint draws (gl_draw) */
/* the line pitch: 4 lines fill the room between the page title and the help line; a page of fewer lines keeps it */
static int32_t gl_pitch(void)
{
    return (GL_END - (GL_TOP_Y + (int32_t)font_big()->h + 3)) / (int32_t)GL_PAGE;
}
/* a line's rows from its top y: the text, then the bar under it, centred in the pitch */
static int32_t gl_ty(int32_t y, int32_t bh) { return y + (gl_pitch() - 2 - (int32_t)FONT_S.h - 3 - bh) / 2; }
static int32_t gl_by(int32_t y) { return gl_ty(y, GL_BH) + (int32_t)FONT_S.h + 3; }
static int32_t gl_y0(void) { return GL_TOP_Y + (int32_t)font_big()->h + 3; }   /* the first line's top */
/* a line's value as drawn: an action says what it does (the user, 2026-10-13: lines showing "YES" read as dead), RUN
 * when its cell names nothing; YES runs it (op_global.c gl_yes) */
static const char *gl_val(const cell_t *c) { return c->kind != CK_ACT ? c->val : c->val[0] ? c->val : "RUN"; }
/* the text a line draws: the long form where the list has the room (value_long: SYNC AUTO "Auto: USB"), else gl_val's;
 * lv (14) holds it */
static const char *gl_text(const cell_t *c, char *lv)
{
    return c->d && c->vp && value_long(c->d, *c->vp, lv) ? lv : gl_val(c);
}
/* an on / off mark: a filled square when on, an outlined one when off, in the line's colour */
static void gl_check(int32_t x, int32_t y, uint32_t on, uint16_t col, uint16_t back)
{
    cv_rect(x, y, GL_CK, GL_CK, col);
    if (!on)
        cv_rect(x + 2, y + 2, GL_CK - 4, GL_CK - 4, back);
}
/* an on / off value (ON, OFF, and the radio's LINK, NO CAL): the mark; any other two-way choice (Notes / Clock, Keys / Seq) a
 * bar of two segments, like the other lists */
static uint32_t gl_tog(const cell_t *c)
{
    return c->kind == CK_VAL && c->gk == GK_PILL &&
           (str_eq(c->val, "ON") || str_eq(c->val, "OFF") || str_eq(c->val, "LINK") || str_eq(c->val, "NO CAL"));
}
static void gl_line_draw(uint32_t k, int32_t y, const cell_t *c, uint32_t on)
{
    char b[16], m[2], lv[14];
    const char *v = gl_text(c, lv);
    uint16_t vc = c->kind == CK_RO || c->kind == CK_ACT ? (c->col ? c->col : C_AMB) : knob_col(k, c->col ? c->col : C_HI);
    int32_t uw = c->kind != CK_ACT && c->unit[0] ? text_w(&FONT_S, c->unit) : 0, x, ph = gl_pitch(), ty, by;
    uint32_t tog = gl_tog(c);                           /* on / off: the mark, no bar */
    uint32_t bar = c->kind != CK_ACT && c->gk != GK_NONE && !tog;
    uint16_t bg = cur_bg(knob_col(k, gld.bar)), ink = on ? C_WHITE : vc;
    ty = gl_ty(y, GL_BH);                               /* (the text, then the bar, centred in the line) */
    by = gl_by(y);
    m[0] = (char)('1' + k);
    m[1] = 0;
    if (on)
        cv_rect(0, y, 240, ph - 2, bg);
    cv_text(6, ty, &FONT_S, m, on ? C_WHITE : c->kind == CK_RO ? C_LINE : knob_col(k, C_HI));
    cv_text(24, ty, &FONT_S, cut(b, op_label(b, c->label ? c->label : "", sizeof b), 12), on ? C_WHITE : C_GRAY);
    cut(b, v, 13);
    x = (tog ? 236 - GL_CK - 6 : 236) - uw - (uw ? 4 : 0) - text_w(&FONT_S, b);   /* (a unit: 4 px after the value) */
    cv_text(x, ty, &FONT_S, b, ink);
    if (uw)
        cv_text(236 - uw, ty, &FONT_S, c->unit, on ? C_WHITE : C_DIM);
    if (tog)
        gl_check(236 - GL_CK, ty + ((int32_t)FONT_S.h - GL_CK) / 2, (uint32_t)(c->gv > c->gmin), ink, on ? bg : C_BLACK);
    else if (bar) {
        cell_t g = *c;
        if (g.gk == GK_PILL)
            g.gk = GK_DOTS;                             /* (a two-way choice: two segments, the chosen one lit) */
        draw_gauge(GL_BX, by, GL_BW, GL_BH, &g, ink, on ? cur_trough(bg) : C_LINE);
    }
}
/* SYSTEM's first page, INFO: not 4 lines but every read-out at once, compact: the name left, the value right, a bar
 * under it over the screen's width (the clock, the load of each core, the memory in use of the running image). No knob
 * does anything here and no line is the cursor */
static uint32_t gl_info(void)
{
    uint32_t e = gld.s && gld.first < gl_n(1) ? gl_ent(1, gld.first) : 0u;
    return gld.s && sys_item(e >> 2, e & 3u) == SI_INFO;
}
static void gl_info_draw(int32_t y0)
{
    uint32_t i;
    int32_t ph = (GL_END - y0) / (int32_t)INFO_N, ty, uw, x;
    char b[16], lv[14];
    cell_t c;
    for (i = 0; i < INFO_N; i++) {
        int32_t y = y0 + (int32_t)i * ph;
        uint16_t vc;
        sys_info_cell(i, &c);
        vc = c.col ? c.col : C_AMB;
        ty = y + (ph - (int32_t)FONT_S.h - 3 - 3) / 2;
        uw = c.unit[0] ? text_w(&FONT_S, c.unit) : 0;
        cv_text(6, ty, &FONT_S, op_label(b, c.label, sizeof b), C_GRAY);
        x = 236 - uw - (uw ? 4 : 0) - text_w(&FONT_S, c.val);
        cv_text(x, ty, &FONT_S, cut(lv, c.val, 13), vc);
        if (uw)
            cv_text(236 - uw, ty, &FONT_S, c.unit, C_DIM);
        if (c.gk != GK_NONE)
            draw_gauge(GL_BX, ty + (int32_t)FONT_S.h + 3, GL_BW, 3, &c, vc, C_LINE);
    }
}
static void gl_paint(void)
{
    uint32_t i;
    int32_t y = gl_y0(), w;
    char t[24];
    cell_t c;
    gl_title(gld.s, gld.first, t, sizeof t);            /* the page's title: the name, its n/N dim; the large face, the small one */
    for (w = (int32_t)str_len(t); w > 0 && t[w - 1] != ' '; w--)    /* when a joined pair's name is too long for it (16 px a letter) */
        ;
    if (w < 1 || gl_npg(gld.s) < 2u) {
        cv_text(6, GL_TOP_Y, font_big(), t, C_HI);
    } else {
        const felucca_font_t *f = text_w(font_big(), t) <= GL_TW ? font_big() : &FONT_S;
        int32_t ty = GL_TOP_Y + ((int32_t)font_big()->h - (int32_t)f->h) / 2;
        t[w - 1] = 0;
        w = cv_text(6, ty, f, t, C_HI);
        cv_text(w + text_w(f, " "), ty, f, t + str_len(t) + 1, C_DIM);
    }
    if (gl_info()) {
        gl_info_draw(y);
        return;
    }
    for (i = gld.first; i < gld.end; i++) {
        gl_cell(gld.s, i, &c);
        gl_line_draw(i - gld.first, y, &c, i == gld.cur);
        y += gl_pitch();
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
    if (gl_info())
        for (i = 0; i < INFO_N; i++) {                  /* (the info page: its read-outs) */
            sys_info_cell(i, &c);
            sig = hc(sig, &c);
        }
    if (sig == ui.sig[2])
        return;
    ui.sig[2] = sig;
    cv_tall(GL_TOP, GL_BAND, C_BLACK, gl_paint);
}
