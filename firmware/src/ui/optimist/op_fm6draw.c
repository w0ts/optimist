/* SPDX-License-Identifier: GPL-3.0-only */
/* FM6's pictures (op_fm6.c): the algorithm, drawn with ui/sloop/ui_fm6.c's maths (copied: a tree per carrier, its
 * modulators above it, a modulator of several operators over the first; the feedback loop right of its box), in this
 * UI's colours: the operator edited white, carriers in the engine's colour, an operator off or at level 0 dim. Over
 * SOUND's FM6 rows (GRAPH_H rows, op_draw.c draw_list) and, larger, as ENV held's map above the black keys' map. */
#if OP_FM6
/* the modulators of each operator (bit m: OP m+1 modulates it), the carriers, the feedback loop; index 0..5 =
 * OP1..OP6. As eng_fm6.c renders them (FM6_ALG): OP6 .. OP1, through two buses */
static void f6_graph(const int16_t *v, uint8_t *mods, uint8_t *car, uint8_t *fb)
{
    const uint8_t *fl = FM6_ALG[v[FV_ALG] & 31];
    uint32_t i, b1 = 0, b2 = 0;
    *car = *fb = 0;
    for (i = 0; i < 6u; i++)
        mods[i] = 0;
    for (i = 0; i < 6u; i++) {
        uint32_t f = fl[i], op = 5u - i, inb = (f >> 4) & 3u, outb = f & 3u, bit = 1u << op;
        mods[op] |= (uint8_t)(inb == 1u ? b1 : inb == 2u ? b2 : 0u);
        if (outb == 0u)
            *car |= (uint8_t)bit;
        else if (outb == 1u)
            b1 = (f & 4u) ? b1 | bit : bit;
        else
            b2 = (f & 4u) ? b2 | bit : bit;
        if ((f & 0xC0u) == 0xC0u)                       /* an operator's own loop */
            *fb |= (uint8_t)bit;
    }
}
typedef struct { int16_t x2, row, w; } f6_node_t;      /* x2: centre in half columns */
static uint32_t f6_parent(const uint8_t *mods, uint32_t m)   /* the first operator m modulates, 6 = none */
{
    uint32_t op;
    for (op = 0; op < 6u; op++)
        if (op != m && ((mods[op] >> m) & 1u))
            return op;
    return 6;
}
static int16_t f6_width(const uint8_t *mods, f6_node_t *nd, uint32_t op, uint32_t depth)
{
    uint32_t m;
    int16_t w = 0;
    for (m = 0; m < 6u && depth < 6u; m++)
        if (m != op && f6_parent(mods, m) == op)
            w = (int16_t)(w + f6_width(mods, nd, m, depth + 1u));
    nd[op].w = w ? w : 1;
    return nd[op].w;
}
static int16_t f6_place(const uint8_t *mods, f6_node_t *nd, uint32_t op, int16_t x0, int16_t row, uint32_t depth)
{
    uint32_t m;
    int16_t x = x0, top = row;
    nd[op].x2 = (int16_t)(2 * x0 + nd[op].w);
    nd[op].row = row;
    for (m = 0; m < 6u && depth < 6u; m++)
        if (m != op && f6_parent(mods, m) == op) {
            int16_t t = f6_place(mods, nd, m, x, (int16_t)(row + 1), depth + 1u);
            top = t > top ? t : top;
            x = (int16_t)(x + nd[m].w);
        }
    return top;
}
static void f6_loop(int32_t x, int32_t y, int32_t bw, int32_t bh, uint16_t c)   /* right of the box, into its top */
{
    cv_line(x + bw, y, x + bw + 4, y, c);
    cv_line(x + bw + 4, y, x + bw + 4, y - bh - 3, c);
    cv_line(x + bw + 4, y - bh - 3, x, y - bh - 3, c);
    cv_line(x, y - bh - 3, x, y - bh, c);
}
static uint32_t f6_graph_sig(void)                      /* what the algorithm's picture shows */
{
    const int16_t *v = f6_ed();
    uint32_t op, sig = (uint32_t)v[FV_ALG] * 977u + f6.target * 131u + (uint32_t)(v[FV_FB] != 0) * 3u +
                       f6_kind(f6.row) * 7919u + trk_col(song.sel) * 13u;
    for (op = 0; op < 6u; op++)
        sig = sig * 31u + (v[FM6_OPB(op + 1u) + FO_OL] != 0 && v[FV_ON + op]);
    return sig;
}
/* the algorithm in rows y0 .. y0 + h of the canvas begun (big: larger boxes, the key to the colours) */
static void f6_alg_draw(int32_t y0, int32_t h, int big)
{
    const int16_t *v = f6_ed();
    uint8_t mods[6], car, fb;
    f6_node_t nd[6];
    int16_t cols = 0, rows = 0, cw, rh, px[6], py[6];
    int32_t bw = big ? 14 : 10, bh = big ? 9 : 7, foot = big ? 18 : 12, rmax = big ? 30 : 24, cmax = big ? 56 : 40;
    uint32_t op, m, sel = f6_kind(f6.row) == 0u ? f6.target : 6u;
    uint16_t ec = trk_col(song.sel);
    char b[8];
    f6_graph(v, mods, &car, &fb);
    for (op = 0; op < 6u; op++)
        nd[op].w = nd[op].x2 = nd[op].row = -1;
    for (op = 0; op < 6u; op++)                         /* the carriers, OP1 first, then their trees */
        if ((car >> op) & 1u) {
            int16_t top;
            f6_width(mods, nd, op, 0);
            top = f6_place(mods, nd, op, cols, 0, 0);
            cols = (int16_t)(cols + nd[op].w);
            rows = top + 1 > rows ? (int16_t)(top + 1) : rows;
        }
    cw = (int16_t)(cols ? (236 / cols > cmax ? cmax : 236 / cols) : cmax);
    rh = (int16_t)(rows ? ((h - foot) / rows > rmax ? rmax : (h - foot) / rows) : rmax);
    bh = bh * 2 > rh - 2 ? (rh - 2) / 2 : bh;
    for (op = 0; op < 6u; op++) {
        px[op] = (int16_t)((240 - cols * cw) / 2 + nd[op].x2 * cw / 2);
        py[op] = (int16_t)(y0 + h - foot - nd[op].row * rh - rh / 2);
    }
    for (op = 0; op < 6u; op++) {                       /* the wires, under the boxes */
        if (nd[op].row < 0)
            continue;
        for (m = 0; m < 6u; m++)
            if ((mods[op] >> m) & 1u && nd[m].row >= 0)
                cv_line(px[m], py[m] + bh, px[op], py[op] - bh, C_DIM);
        if ((car >> op) & 1u)
            cv_line(px[op], py[op] + bh, px[op], y0 + h - foot + 6, C_DIM);
        if ((fb >> op) & 1u)                            /* lit while its feedback is above 0 */
            f6_loop(px[op], py[op], bw, bh, v[FV_FB] ? C_GRAY : C_LINE);
    }
    if (cols)                                           /* the output */
        cv_rect((240 - cols * cw) / 2 + cw / 4, y0 + h - foot + 6, cols * cw - cw / 2, 1, C_DIM);
    for (op = 0; op < 6u; op++) {
        char n[2] = {(char)('1' + op), 0};
        int on = op == sel, mute = !v[FM6_OPB(op + 1u) + FO_OL] || !v[FV_ON + op];
        uint16_t box = on ? C_WHITE : (car >> op) & 1u ? (mute ? col_shade(ec, 2u) : ec) : mute ? OP_SURF : C_LINE;
        if (nd[op].row < 0)
            continue;
        cv_rect(px[op] - bw, py[op] - bh, 2 * bw, 2 * bh, box);
        cv_text(px[op] - 4, py[op] - 8, &FONT_S, n, on || ((car >> op) & 1u && !mute) ? C_BLACK : mute ? C_DIM : C_HI);
    }
    str_cpy(b, "Alg ", sizeof b);                       /* "Alg 5", the feedback */
    fmt_int(b + 4, v[FV_ALG] + 1);
    cv_text(4, y0 + 1, &FONT_S, b, C_GRAY);
    if (big) {
        str_cpy(b, "Fb ", sizeof b);
        fmt_int(b + 3, v[FV_FB]);
        cv_text(236 - text_w(&FONT_S, "Car"), y0 + 1, &FONT_S, "Car", ec);
        cv_text(236 - text_w(&FONT_S, b), y0 + 17, &FONT_S, b, v[FV_FB] ? C_GRAY : C_DIM);
    }
}

/* ---- ENV held: the header's title, the panel (the algorithm, then the black keys' map as on the keyboard:
 * OP1 OP2 OP3 | OP4 OP5 | OP6 PIT GLO | MONO POLY) */
static void fm6_lay_title(char *t, uint32_t n)          /* "FM6 OP3", "FM6 PIT", "FM6 GLO" (+ " LOCKED": lay_title) */
{
    uint32_t kd = f6_kind(f6.row);
    str_cpy(t, "FM6 ", n);
    if (kd == 0u) {
        str_cpy(t + 4, "OP1", n - 4u);
        t[6] = (char)('1' + f6.target);
    } else {
        str_cpy(t + 4, kd == 1u ? "PIT" : "GLO", n - 4u);
    }
}
#define F6_MAP_Y (cards_2x2() ? 80 : 96)                                    /* the black keys' map in the panel */
static void fm6_lay_paint(void)                         /* (cv_tall: the panel to the screen's foot, no footer) */
{
    static const char *const KEYS[10] = {"1", "2", "3", "4", "5", "6", "PI", "GL", "MO", "PO"};
    static const uint8_t KT[10] = {0, 1, 2, 3, 4, 5, FMT_PIT, FMT_GLO, FMT_MONO, FMT_POLY};
    static const uint8_t GROUP[10] = {0, 0, 0, 1, 1, 2, 2, 2, 3, 3};
    uint32_t i, kd = f6_kind(f6.row), voice = (uint32_t)TSEL->p[P_VOICE];
    char nm[16], pg[24], c[16];
    uint16_t ec = trk_col(song.sel);
    fm6_name(nm, f6_ed());
    fm6_row_name(f6.row, pg);
    f6_alg_draw(0, F6_MAP_Y - 18, 1);
    cv_text(4, F6_MAP_Y - 17, &FONT_S, op_case(c, nm, sizeof c), C_WHITE);   /* the voice, the page */
    op_case(c, pg, sizeof c);
    cv_text(236 - text_w(&FONT_S, c), F6_MAP_Y - 17, &FONT_S, c, C_GRAY);
    for (i = 0; i < 10u; i++) {
        uint32_t on = (KT[i] < 6u && kd == 0u && KT[i] == f6.target) || (KT[i] == FMT_PIT && kd == 1u) ||
                      (KT[i] == FMT_GLO && kd == 2u) || (KT[i] == FMT_MONO && (voice == V_MONO || voice == V_LEGATO)) ||
                      (KT[i] == FMT_POLY && voice == V_POLY);
        int32_t x = 6 + (int32_t)i * 21 + (int32_t)GROUP[i] * 6;
        cv_rect(x, F6_MAP_Y + 2, 19, 22, on ? C_WHITE : C_LINE);
        cv_text(x + 10 - text_w(&FONT_S, KEYS[i]) / 2, F6_MAP_Y + 5, &FONT_S, KEYS[i], on ? C_BLACK : ec);
    }
}
static void fm6_lay_draw(void)
{
    uint32_t sig;
    char nm[16], pg[24];
    if (!fm6_sel())
        return;
    fm6_name(nm, f6_ed());
    fm6_row_name(f6.row, pg);
    sig = hs(hs(hu(hu(f6_graph_sig(), (uint32_t)TSEL->p[P_VOICE]), f6.row * 8u + settings.palette), nm), pg);
    if (sig == ui.sig[2])
        return;
    ui.sig[2] = sig;
    cv_tall(OP_PY, OH_BODY, C_BLACK, fm6_lay_paint);
}
/* ENV let go after the layer was used: SOUND's FM6 rows on the page it showed (as SLOOP's page stays up) */
static void fm6_lay_end(void)
{
    if (!f6.picked)
        return;
    f6.picked = 0;
    if (!fm6_sel())
        return;
    if (ui.scr != SCR_SOUND)
        op_enter(SCR_SOUND);
    snd_fam = SND_FM6;
    ui.row[SCR_SOUND] = 0xFF;
    op_row_pick(f6.row);
    ui.force = 1;
}
#else
static void fm6_lay_title(char *t, uint32_t n) { str_cpy(t, "", n); }
static void fm6_lay_draw(void) {}
static void fm6_lay_end(void) {}
static uint32_t f6_graph_sig(void) { return 0; }
static void f6_alg_draw(int32_t y0, int32_t h, int big) { (void)y0; (void)h; (void)big; }
#endif
