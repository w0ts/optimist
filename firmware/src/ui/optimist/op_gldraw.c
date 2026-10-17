/* SPDX-License-Identifier: GPL-3.0-only */
/* GLOBAL and SYSTEM drawn as a list (op_global.c; docs/UI-OPTIMIST-DESIGN.md section 20): no cards, the page's 4 lines
 * from under the header to the screen's foot (the page's title is the header's, next to the GLOBAL / SYSTEM badge:
 * "Channels 5/5", op_draw.c head_other; no title line here, the user, 2026-10-14). The 4 lines share the room down to the
 * help line evenly (gl_pitch). A line: the setting's name (no knob numbers, the user, 2026-10-15), its value on the right as the cards write it (amber: a read-out, or an action's
 * word: gl_val) in the large face when it fits (gl_big, 2026-10-17), the value's form under it: a bar over the screen's width, a segmented one for a short list, an on / off
 * value a square mark at the right instead (filled on, outlined off). The cursor line: no fill, its text (the number, the name,
 * the value) in the line's colour (the knob's with KNOB COLORS, else the track's; the user, 2026-10-14, "invert the highlight"). Lazy, as every band: drawn again when its signature changes. */
#define GL_TOP OH_HEAD                  /* the list: under the header, to the help band (op_graph.c OP_FOOT) */
#define GL_BAND (OP_FOOT_Y - OH_HEAD)
#define GL_TOP_Y 3                      /* the first line's top, in the band */
#define GL_END (GL_BAND - 2)            /* the lines' room ends here */
#define GL_BX 6                         /* the value bar: the screen's width less a margin each side */
#define GL_BW 228
#define GL_BH 4                         /* its thickness */
#define GL_CK 10                        /* the on / off mark's side */
static struct {
    uint32_t s, first, end, n, cur;
    uint16_t bar;
} gld;                                                  /* what gl_paint draws (gl_draw) */
/* the line pitch: 4 lines fill the room between the header and the help line; a page of fewer lines keeps it */
static int32_t gl_pitch(void)
{
    return (GL_END - GL_TOP_Y) / (int32_t)GL_PAGE;
}
/* a line's rows from its top y: the text, then the bar under it, centred in the pitch */
static int32_t gl_ty(int32_t y, int32_t bh) { return y + (gl_pitch() - 2 - (int32_t)FONT_S.h - 3 - bh) / 2; }
static int32_t gl_by(int32_t y) { return gl_ty(y, GL_BH) + (int32_t)FONT_S.h + 3; }
static int32_t gl_y0(void) { return GL_TOP_Y; }   /* the first line's top */
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
/* the value in the large face (the user, 2026-10-17: "4 lines and yet font is too small"): every value that fits beside
 * its name, the unit and the on / off mark (a value too long for it, "DRUM BUS L+R", keeps the small face) */
static int gl_big(const char *name, const char *v, int32_t uw, uint32_t tog)
{
    return 6 + text_w(&FONT_S, name) + 12 + text_w(font_big(), v) + uw + (tog ? GL_CK + 6 : 0) <= 236;
}
/* line c's place from its top y: the name and the unit's text row (gl_lrow.ty), the value's (.fy, in the face .f), the bar's
 * (.by); the name, the value and the unit's width as drawn */
typedef struct {
    const felucca_font_t *f;
    int32_t ty, fy, by, uw;
    char nm[16], v[16];
} gl_lrow_t;
static void gl_lrow(const cell_t *c, int32_t y, gl_lrow_t *r)
{
    char lv[14];
    cut(r->nm, nm_card(c->label ? c->label : "", 0, r->nm, sizeof r->nm), 12);
    cut(r->v, gl_text(c, lv), 13);
    r->uw = c->kind != CK_ACT && c->unit[0] ? text_w(&FONT_S, c->unit) : 0;
    r->f = &FONT_S;
    r->ty = r->fy = gl_ty(y, GL_BH);                    /* (the text, then the bar, centred in the line) */
    r->by = gl_by(y);
    if (gl_big(r->nm, r->v, r->uw, gl_tog(c))) {        /* (the large value at the line's top, the name and the unit on its
                                                         * baseline, the bar under it) */
        r->f = font_big();
        r->fy = y + 1;
        r->ty = r->fy + (int32_t)font_big()->h / 2 - 4;
        r->by = r->fy + (int32_t)font_big()->h - 2;
    }
}
static void gl_line_draw(uint32_t k, int32_t y, const cell_t *c, uint32_t on)
{
    gl_lrow_t r;
    uint16_t vc = c->kind == CK_RO || c->kind == CK_ACT ? (c->col ? c->col : c->kind == CK_RO ? C_GRAY : C_AMB) : knob_col(k, c->col ? c->col : C_HI);
    int32_t uw, x, ty, by;
    uint32_t tog = gl_tog(c);                           /* on / off: the mark, no bar */
    uint32_t bar = c->kind != CK_ACT && c->gk != GK_NONE && !tog;
    uint16_t lc = knob_col(k, gld.bar), ink = on ? lc : vc;     /* (the cursor line: its text in the line's colour, no fill) */
    const char *b;
    gl_lrow(c, y, &r);
    uw = r.uw, ty = r.ty, by = r.by, b = r.v;
    cv_text(6, ty, &FONT_S, r.nm, on ? lc : C_WHITE);
    x = (tog ? 236 - GL_CK - 6 : 236) - uw - text_w(r.f, b);   /* (a unit right after the number: "90BPM", style guide 5) */
    cv_text(x, r.fy, r.f, b, ink);
    if (uw)
        cv_text(236 - uw, ty, &FONT_S, c->unit, on ? lc : C_DIM);
    if (tog)
        gl_check(236 - GL_CK, ty + ((int32_t)FONT_S.h - GL_CK) / 2, (uint32_t)(c->gv > c->gmin), ink, C_BLACK);
    else if (bar) {
        cell_t g = *c;
        if (g.gk == GK_PILL)
            g.gk = GK_DOTS;                             /* (a two-way choice: two segments, the chosen one lit) */
        draw_gauge(GL_BX, by, GL_BW, GL_BH, &g, ink, C_LINE);
    }
}
/* SYSTEM's first page, INFO: not 4 lines but every read-out at once, compact: the name left, the value right, text
 * only, no bars (the clock, the load of each core, the battery). No knob
 * does anything here and no line is the cursor */
#define GL_INFO_PH 26                                   /* a read-out page's line pitch at most */
static uint32_t gl_sys_item(void)                       /* SYSTEM's page shown is the one whose first line is this item (SI_NONE: not SYSTEM) */
{
    uint32_t e = gld.s && gld.first < gl_n(1) ? gl_ent(1, gld.first) : 0u;
    return gld.s ? sys_item(e >> 2, e & 3u) : SI_NONE;
}
static uint32_t gl_info(void)                           /* 1 + the read-out page shown (op_project.c info_cell: INFO, INTERNALS, USB STATS), 0: none */
{
    uint32_t it = gl_sys_item();
    return it == SI_INFO ? 1u + INFO_PG_INFO : it == SI_DIAG ? 1u + INFO_PG_DIAG : it == SI_UAINFO ? 1u + INFO_PG_USB : 0u;
}
/* SYSTEM > CREDITS, after ABOUT: who made what Optimist is built from, as text lines in INFO's style (the name left in
 * grey, the who right, no bars, no knob does anything); the rest, with licences and commits: LICENSING.md
 * (docs/CREDITS-AUDIT.md). Names only, nominative: JUNO models the Roland Juno-106, Optimist is not affiliated */
static const char *const CRED[][2] = {
    {"VERSION", FELUCCA_VERSION},                       /* ("OPTIMIST 0.1"; a release: its own string) */
    {"FELUCCA", "Leo Kuroshita"},                       /* (the platform: Hugelton Instruments) */
    {"SLOOP", "isod89"},
    {"MELODEE", "Kerem Kilic"},
    {"X0X", "Charles Vestal"},
    {"FLOWSTATE", "Z. Chowdhury"},
    {"JANGADA", "zednaked"},
    {"JUNO", "fm-106, KR-106"},                         /* (PatttF; Kayrock Screenprinting) */
    {"ACID", "Open303, TB-3PO"},                        /* (Robin Schmidt, Logarhythm: MIT) */
    {"SAMPLES", "VSCO, VCSL, Sonic Pi"},
    {"FONT", "Terminus"},
    {"THE REST", "LICENSING.md"},
};
#define CRED_N (sizeof CRED / sizeof CRED[0])
static uint32_t gl_cred(void) { return gl_sys_item() == SI_CREDITS; }
static uint32_t gl_textpage(void) { return gl_info() || gl_cred(); }   /* (a page drawn whole: no lines, no cursor) */
static void gl_cred_draw(int32_t y0)
{
    uint32_t i;
    int32_t ph = (GL_END - y0) / (int32_t)CRED_N, ty;
    char b[16];
    for (i = 0; i < CRED_N; i++) {
        ty = y0 + (int32_t)i * ph + (ph - (int32_t)FONT_S.h) / 2;
        cv_text(6, ty, &FONT_S, nm_card(CRED[i][0], 0, b, sizeof b), C_GRAY);
        cv_text(236 - text_w(&FONT_S, CRED[i][1]), ty, &FONT_S, CRED[i][1], i ? C_HI : C_WHITE);
    }
}
#define GL_INFO_BIG 40                                  /* a read-out page's pitch from which its values take the large face */
static void gl_info_draw(int32_t y0)
{
    uint32_t i, pg = gl_info() - 1u, n = info_lines(pg);
    int32_t ph = (GL_END - y0) / (int32_t)n, cap = ph >= GL_INFO_BIG ? gl_pitch() : GL_INFO_PH, ty, fy, uw, x;
    char b[16], lv[14];
    cell_t c;
    if (ph > cap)
        ph = cap;                                       /* (a short page: a list's pitch, from the top) */
    for (i = 0; i < n; i++) {
        int32_t y = y0 + (int32_t)i * ph;
        uint16_t vc;
        const felucca_font_t *f = &FONT_S;
        info_cell(pg, i, &c);
        vc = c.col ? c.col : C_GRAY;
        ty = fy = y + (ph - (int32_t)FONT_S.h) / 2;
        uw = c.unit[0] ? text_w(&FONT_S, c.unit) : 0;
        nm_card(c.label, 0, b, sizeof b);
        cut(lv, c.val, 13);
        if (ph >= GL_INFO_BIG && gl_big(b, lv, uw, 0)) {   /* (few read-outs: the values large, as the settings' lines) */
            f = font_big();
            fy = y + (ph - (int32_t)font_big()->h) / 2;
            ty = fy + (int32_t)font_big()->h / 2 - 4;
        }
        cv_text(6, ty, &FONT_S, b, f == &FONT_S ? C_GRAY : C_WHITE);
        x = 236 - uw - text_w(f, lv);
        cv_text(x, fy, f, lv, vc);
        if (uw)
            cv_text(236 - uw, ty, &FONT_S, c.unit, C_DIM);
    }
}
static void gl_paint(void)
{
    uint32_t i;
    int32_t y = gl_y0();
    cell_t c;
    if (gl_info()) {
        gl_info_draw(y);
        return;
    }
    if (gl_cred()) {
        gl_cred_draw(y);
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
        for (i = 0; i < info_lines(gl_info() - 1u); i++) {   /* (the info pages: their read-outs) */
            info_cell(gl_info() - 1u, i, &c);
            sig = hc(sig, &c);
        }
    if (sig == ui.sig[2])
        return;
    ui.sig[2] = sig;
    cv_tall(GL_TOP, GL_BAND, C_BLACK, gl_paint);
}
