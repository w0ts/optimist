/* SPDX-License-Identifier: GPL-3.0-only */
/* GLOBAL and SYSTEM drawn as a list (op_global.c; docs/UI-OPTIMIST-DESIGN.md 4.7 GLOBAL and SYSTEM): no cards, the page's 4 lines
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
    return 6 + text_ws(name) + 12 + text_w(font_big(), v) + uw + (tog ? GL_CK + 6 : 0) <= 236;
}
/* line c's place from its top y: the name and the unit's text row (gl_lrow.ty), the value's (.fy, in the face .f), the bar's
 * (.by); the name, the value and the unit's width as drawn */
typedef struct {
    const felucca_font_t *f, *nf;                       /* (the value's face, the name's) */
    int32_t ty, fy, by, uw;
    char nm[16], v[16];
} gl_lrow_t;
/* the name in the large face too, when the name and the value both fit large (Felucca's look: "large fonts are much
 * more enjoyable on screen", a player, 2026-10-25) */
#define GL_NM_GAP 4                                     /* (the room kept between a large name and its value) */
static int gl_big_nm(const char *name, const char *v, int32_t uw, uint32_t tog)
{
    return 6 + text_w(font_big(), name) + GL_NM_GAP + text_w(font_big(), v) + uw + (tog ? GL_CK + 6 : 0) <= 236;
}
/* a name's shorter form for a line whose large name would not fit beside its value (the large face everywhere beats the
 * long name: SYSTEM's KNOB COLORS), or 0 */
static const char *gl_short_nm(const char *name)
{
    return str_eq(name, "KNOB COLORS") ? "COLORS" : 0;
}
static void gl_lrow(const cell_t *c, int32_t y, gl_lrow_t *r)
{
    char lv[14];
    cut(r->nm, nm_card(c->label ? c->label : "", 0, r->nm, sizeof r->nm), 12);
#if WIFI_UI
    cut(r->v, gl_text(c, lv), c->wide ? 15u : 13u);
#else
    cut(r->v, gl_text(c, lv), 13);
#endif
    r->uw = c->kind != CK_ACT && c->unit[0] ? text_ws(c->unit) : 0;
    r->f = r->nf = &FONT_S;
    r->ty = r->fy = gl_ty(y, GL_BH);                    /* (the text, then the bar, centred in the line) */
    r->by = gl_by(y);
    if (gl_big(r->nm, r->v, r->uw, gl_tog(c))) {        /* (the large value at the line's top, the name and the unit on its
                                                         * baseline, the bar under it) */
        r->f = font_big();
        r->fy = y + 1;
        r->ty = r->fy + (int32_t)font_big()->h / 2 - 4;
        r->by = r->fy + (int32_t)font_big()->h - 2;
        if (!gl_big_nm(r->nm, r->v, r->uw, gl_tog(c)) && gl_short_nm(r->nm) &&
            gl_big_nm(gl_short_nm(r->nm), r->v, r->uw, gl_tog(c)))
            str_cpy(r->nm, gl_short_nm(r->nm), sizeof r->nm);
        if (gl_big_nm(r->nm, r->v, r->uw, gl_tog(c)))
            r->nf = font_big();
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
    x = text_st(6, r.nf == &FONT_S ? ty : r.fy, r.nf, r.nm, on ? lc : knob_col_dim(k, C_WHITE), c->strike);   /* (a muted track's volume: struck, PAGE_LINES) */
    if (c->dr)
        cv_texts(x + 8, ty, "DR", C_GRAY);              /* (the drum track's own value, a lane picked: as the cards tag it) */
    x = (tog ? 236 - GL_CK - 6 : 236) - uw - text_w(r.f, b);   /* (a unit right after the number: "90BPM", style guide 5) */
    cv_text(x, r.fy, r.f, b, ink);
    if (uw)
        cv_texts(236 - uw, ty, c->unit, on ? lc : C_DIM);
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
 * only but the CPU lines' bars (gl_info_bar: the load now and its peaks; the clock, the load of each core, RAM, the battery). No line is the cursor,
 * no knob does anything */
#define GL_INFO_PH 26                                   /* a read-out page's line pitch at most */
#define GL_INFO_PH_MAIN 34                              /* INFO's: five or six lines (the battery is the header's, 2026-10-29) spread over the band */
static uint32_t gl_sys_item(void)                       /* SYSTEM's page shown is the one whose first line is this item (SI_NONE: not SYSTEM) */
{
    uint32_t e = gld.s && gld.first < gl_n(1) ? gl_ent(1, gld.first) : 0u;
    return gld.s ? sys_item(e >> 2, e & 3u) : SI_NONE;
}
static uint32_t gl_info(void)                           /* 1 + the read-out page shown (op_project.c info_cell: INFO, INTERNALS, USB STATS, CPU TIMES, BT STATS), 0: none */
{
    uint32_t it = gl_sys_item();
    return it == SI_INFO ? 1u + INFO_PG_INFO : it == SI_DIAG ? 1u + INFO_PG_DIAG : it == SI_UAINFO ? 1u + INFO_PG_USB :
           it == SI_DUALT ? 1u + INFO_PG_DUAL : it == SI_BTSTAT ? 1u + INFO_PG_BT : 0u;
}
/* SYSTEM > CREDITS, the last page: film end titles (op_credits.c: the credits, the crawl, KNOB 1; no cursor, no line) */
static uint32_t gl_cred(void) { return gl_sys_item() == SI_CREDITS; }
static uint32_t gl_textpage(void) { return gl_info() || gl_cred(); }   /* (a page drawn whole: no lines, no cursor) */
#define GL_BAND_W (240 - OP_MTR - OH_HEAD)                       /* INFO's first page has no help band (the user, 2026-10-29): its room runs to the screen's foot */
#define GL_END_W (GL_BAND_W - 2)
static uint32_t gl_info_wide(void)                      /* the page with no foot line (op_help.c hl_pre): from the cursor, not from the last draw (hl_pre runs before gl_draw) */
{
    uint32_t e;
    if (ui.scr != SCR_SYSTEM)
        return 0u;
    e = gl_ent(1, gl_first(1, ui.row[SCR_SYSTEM]));
    return sys_item(e >> 2, e & 3u) == SI_INFO;
}
#define GL_INFO_BIG 40                                  /* a read-out page's pitch from which its values take the large face */
#define GL_INFO_BGAP 4                                  /* a CPU line: its text, this gap, its bar (GL_BH) */
#define GL_INFO_TK 2                                    /* ... the peak ticks: this wide, standing this far over and under the bar */
/* a CPU line's bar under its text (op_project.c info_bar; the user, 2026-10-28): the screen's width as the settings' bars,
 * filled to the load now in the load's colours, the dim tick at the peak since boot, the bright one at the 10 s peak over it; the
 * text never on it */
static void gl_info_bar(int32_t by, const cell_t *c)
{
    int32_t span = c->gmax - c->gmin, k;
    cell_t g = *c;
    draw_gauge(GL_BX, by, GL_BW, GL_BH, &g, load_col(c->gv), C_LINE);
    if (c->av > c->gv && span > 0)                      /* (the RAM bar: the undo history's borrowed share after the fill,
                                                         * dimmer: op_project.c info_ram) */
        cv_rect(GL_BX + (c->gv - c->gmin) * GL_BW / span, by, (c->av - c->gv) * GL_BW / span, GL_BH, col_shade(C_GRAY, 2u));
    for (k = 1; k >= 0; k--) {
        int32_t v = c->hold[k], x;
        if (v <= 0 || span <= 0)
            continue;
        x = GL_BX + (v - c->gmin) * (GL_BW - GL_INFO_TK) / span;
        cv_rect(x, by - GL_INFO_TK, GL_INFO_TK, GL_BH + 2 * GL_INFO_TK, k ? col_shade(load_col(v), 4u) : load_col(v));
    }
}
static int32_t gl_info_pitch(uint32_t pg, uint32_t n, int32_t y0)   /* a read-out page's line pitch: the band shared by its n lines */
{
    int32_t ph = ((pg == INFO_PG_INFO ? GL_END_W : GL_END) - y0) / (int32_t)n, cap = ph >= GL_INFO_BIG ? gl_pitch() : pg == INFO_PG_INFO ? GL_INFO_PH_MAIN : GL_INFO_PH;
    return ph > cap ? cap : ph;                         /* (a short page: a list's pitch, from the top) */
}
static void gl_info_draw(int32_t y0)
{
    uint32_t i, pg = gl_info() - 1u, n = info_lines(pg);
    int32_t ph = gl_info_pitch(pg, n, y0), ty, fy, uw, x;
    char b[20], lv[14];
    const char *nm;
    cell_t c;
    for (i = 0; i < n; i++) {
        int32_t y = y0 + (int32_t)i * ph, tp;
        uint16_t vc;
        const felucca_font_t *f = &FONT_S;
        info_cell(pg, i, &c);
        tp = c.pkbar ? ph - (GL_INFO_BGAP + GL_BH + GL_INFO_TK) : ph;   /* (the text's room: a CPU line's bar under it) */
        vc = c.col ? c.col : C_GRAY;
        ty = fy = y + (tp - (int32_t)FONT_S.h) / 2;
        uw = c.unit[0] ? text_ws(c.unit) : 0;
        nm = nm_card(c.label, 0, b, sizeof b);
        cut(lv, c.val, 13);
        if (ph >= GL_INFO_BIG && gl_big(nm, lv, uw, 0)) {   /* (few read-outs: the values large, as the settings' lines) */
            f = font_big();
            fy = y + (tp - (int32_t)font_big()->h) / 2;
            ty = fy + (int32_t)font_big()->h / 2 - 4;
        }
        if (f != &FONT_S && gl_big_nm(nm, lv, uw, 0)) {     /* (the name large too where both fit: one size on a row) */
            cv_text(6, fy, f, nm, c.danger ? C_WARN : C_WHITE);
        } else if (f != &FONT_S && pg != INFO_PG_INFO) {   /* (else the whole row small, never a large value by a small name; INFO's first page keeps the large value: a small name beside it) */
            f = &FONT_S;
            fy = ty = y + (tp - (int32_t)FONT_S.h) / 2;
            cv_texts(6, ty, nm, c.danger ? C_WARN : C_GRAY);
        } else
            cv_texts(6, ty, nm, c.danger ? C_WARN : C_GRAY);
        x = 236 - uw - text_w(f, lv);
        cv_text(x, fy, f, lv, vc);
        if (uw)
            cv_texts(236 - uw, ty, c.unit, C_DIM);
        if (c.pkbar)
            gl_info_bar(fy + (int32_t)f->h + GL_INFO_BGAP, &c);
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
        gl_cred_draw();
        return;
    }
#if FELUCCA_BT_UI
    if (gld.s && bt_pg_shown()) {                       /* a Bluetooth page with no link: MODE, then its devices over the two
                                                         * middle lines' room, then STATE (op_bt.c bt_pg_paint) */
        for (i = gld.first; i < gld.end; i++, y += gl_pitch())
            if (i == gld.first || i == gld.first + 3u) {
                gl_cell(gld.s, i, &c);
                gl_line_draw(i - gld.first, y, &c, i == gld.cur);
            }
        bt_pg_paint(bt_pg_shown() - 1u, gl_y0() + gl_pitch(), gl_y0() + 3 * gl_pitch(), gld.bar);
        return;
    }
#endif
#if WIFI_UI
    if (gld.s && wf_pg_shown()) {                       /* the WI-FI page with nothing joined: the same (op_wifi.c wf_pg_paint) */
        for (i = gld.first; i < gld.end; i++, y += gl_pitch())
            if (i == gld.first || i == gld.first + 3u) {
                gl_cell(gld.s, i, &c);
                gl_line_draw(i - gld.first, y, &c, i == gld.cur);
            }
        wf_pg_paint(gl_y0() + gl_pitch(), gl_y0() + 3 * gl_pitch(), gld.bar);
        return;
    }
#endif
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
    sig = hu(hu(hu(hu(hu(0x6C15u, gld.s), gld.cur), gld.bar + settings.palette * 65536u), knob_colors), gl_info_wide());
    for (i = gld.first; i < gld.end; i++) {
        gl_sect(gld.s, i, nm);
        gl_cell(gld.s, i, &c);
        sig = hc(hs(sig, nm), &c);
    }
#if FELUCCA_BT_UI
    if (gld.s && bt_pg_shown())
        sig = hu(sig, bt_pg_sig(bt_pg_shown() - 1u));   /* (a Bluetooth page's devices) */
#endif
#if WIFI_UI
    if (gld.s && wf_pg_shown())
        sig = hu(sig, wf_pg_sig());                     /* (the WI-FI page's networks) */
#endif
    if (gl_info())
        for (i = 0; i < info_lines(gl_info() - 1u); i++) {   /* (the info pages: their read-outs) */
            info_cell(gl_info() - 1u, i, &c);
            sig = hu(hc(sig, &c), (uint32_t)(uint16_t)c.hold[0] << 16 ^ (uint16_t)c.hold[1] ^ (uint32_t)c.pkbar << 15);
        }
    if (gl_cred()) {                                    /* (the credits: the crawl's offset; any other page: it starts at the top next time) */
        cred_tick();
        cr.px = (int16_t)cred_px();
        sig = hu(sig, (uint32_t)(uint16_t)cr.px);
        if (sig != ui.sig[2])
            cr.draws++;                                 /* (a paint: the tests count them) */
    } else
        cred_off();
    if (sig == ui.sig[2])
        return;
    ui.sig[2] = sig;
    cv_tall(GL_TOP, gl_info_wide() ? GL_BAND_W : GL_BAND, C_BLACK, gl_paint);
}

/* PAGE_LINES (op_draw.c pl_on): a screen's cursor row as GLOBAL's lines, value k on line k (its knob), the hot one in the
 * line's colour as GLOBAL's cursor line; a cell with no label leaves its line empty. The header names the row and its n/N */
#if FELUCCA_PAGE_LINES
static cell_t pl_c[4];
static void pl_paint(void)
{
    uint32_t k;
    for (k = 0; k < 4u; k++)
        if (pl_c[k].label)
            gl_line_draw(k, gl_y0() + (int32_t)k * gl_pitch(), &pl_c[k], ui.hot_lit && ui.hot == k);
}
static void pl_draw(void)
{
    uint32_t k, row = ui.row[ui.scr];
    uint32_t sig = hu(hu(hu(hu(0x9E15u + ui.scr, row), ui.hot * 2u + ui.hot_lit), trk_col(song.sel) + settings.palette * 65536u),
                      knob_colors);
    for (k = 0; k < 4u; k++) {
        SCR->cell(row, k, &pl_c[k]);
        sig = hc(sig, &pl_c[k]);
    }
    if (sig == ui.sig[2])
        return;
    ui.sig[2] = sig;
    gld.bar = trk_col(song.sel);
    cv_tall(GL_TOP, GL_BAND, C_BLACK, pl_paint);
}
#else
static void pl_draw(void) {}
#endif
