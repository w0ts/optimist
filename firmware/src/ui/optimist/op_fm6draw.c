/* SPDX-License-Identifier: GPL-3.0-only */
/* FM6's pictures (op_fm6.c): the algorithm, drawn with ui/sloop/ui_fm6.c's maths (copied: a tree per carrier, its
 * modulators above it, a modulator of several operators over the first; the feedback loop right of its box), in this
 * UI's colours: the operator edited white, carriers in the engine's colour, an operator off or at level 0 dim. Over
 * SOUND's FM6 rows (GRAPH_H rows, op_draw.c draw_list) and, larger, as ENV held's map above the black keys' map. */
#if OP_FM6
static void op_row_pick(uint32_t r);                    /* op_input.c */
static int fm6_on_page(void) { return ui.scr == SCR_SOUND && snd_fam == SND_FM6 && fm6_sel(); }   /* SOUND's FM6 rows are up */
/* the row the pictures are about: SOUND's cursor row when its FM6 rows are up (SELECT moves it, f6.row only follows a
 * turn), else the layer's page */ 
static uint32_t f6_cur_row(void) { return fm6_on_page() && lay.shown != LY_OPS ? ui.row[SCR_SOUND] % F6_ROWS : f6.row; }
static int f6_eg_row(uint32_t r) { return r == F6_OP0 + 1u || r == F6_OP0 + 2u || r == F6_PIT0 || r == F6_PIT0 + 1u; }
/* SHIFT2 / SHIFT held + a black key on SOUND's FM6 rows (op_input.c shift_keys): the shifts' language (SHIFT2 selects what
 * the key names, SHIFT mutes it) on the layer's keys. M-VAVE's FM editor (app/screen.c 2316-2352, GPL-3.0-only) picks the
 * operator with a black key and toggles it with a long press, on the page itself; here the shifts take their place so that
 * the keys keep playing. Returns 1 when the key was taken (every black key of the layer: a key not named under SHIFT does
 * nothing) */
static int fm6_shift_key(uint32_t k, int mute)
{
    int32_t t = f6_key_target(k);
    if (t < 0 || !fm6_on_page())
        return 0;
    if (mute) {
        if (t < 6) {
            char b[8] = "OP1 ";
            int16_t *ed = f6_ed();
            fm6_set(ed, FV_ON + (uint32_t)t, !ed[FV_ON + (uint32_t)t]);
            b[2] = (char)('1' + t);
            str_cpy(b + 4, ed[FV_ON + (uint32_t)t] ? "ON" : "OFF", 4);
            ui_message(b);
        }
    } else {
        fm6_lay_key(k);
        f6.picked = 0;                                  /* (the rows stay: this is no layer) */
        ui.row[SCR_SOUND] = 0xFF;                       /* (so that the pick is a change) */
        op_row_pick(f6.row);
    }
    ui.force = 1;
    return 1;
}
static uint32_t fm6_keys_off(void)                      /* SHIFT held: the keys of the operators switched off */
{
    static const uint8_t KEY[6] = {1, 3, 5, 8, 10, 13};
    uint32_t i, m = 0;
    for (i = 0; i < 6u; i++)
        if (!f6_ed()[FV_ON + i])
            m |= 1u << KEY[i];
    return m;
}
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
static uint32_t f6_graph_sig(void)                      /* what the algorithm's (or the envelope's) picture shows */
{
    const int16_t *v = f6_ed();
    uint32_t op, row = f6_cur_row(), sig = (uint32_t)v[FV_ALG] * 977u + f6.target * 131u + (uint32_t)(v[FV_FB] != 0) * 3u +
                                           f6_kind(row) * 7919u + trk_col(song.sel) * 13u + row * 104729u;
    for (op = 0; op < 6u; op++)
        sig = sig * 31u + (v[FM6_OPB(op + 1u) + FO_OL] != 0 && v[FV_ON + op]);
    if (f6_eg_row(row))                                 /* (the envelope: its eight values) */
        for (op = 0; op < 8u; op++)
            sig = sig * 31u + (uint32_t)v[f6_kind(row) == 0u ? FM6_OPB(f6.target + 1u) + FO_R1 + op : FV_PR + op];
    return sig;
}
/* The envelope pages' picture, after M-VAVE's FM editor (app/screen.c 751-768, 721-735: GPL-3.0-only): the six operators as
 * chips (the one edited white, one switched off struck through) above a static shape of the EG, a dot a stage. Schematic,
 * not a trace in milliseconds: a stage's width follows its rate (high = fast; their formula, 8 + (99 - R) * 32 / 99 px),
 * its height its level, L3 holds until release (a flat stretch before stage 4), the envelope starts at L4. The pitch EG has
 * its zero line (level 50) */
static void f6_eg_draw(int32_t y0, int32_t h)
{
    const int16_t *v = f6_ed();
    uint32_t row = f6_cur_row(), pit = f6_kind(row) == 1u, i, sel = f6.target % 6u;
    const int16_t *r = pit ? v + FV_PR : v + FM6_OPB(sel + 1u) + FO_R1, *l = r + 4;
    int32_t top = y0 + 21, bot = y0 + h - 18, ph = bot - top, x = 20, ly, nx, ny;
    uint16_t ec = trk_col(song.sel), c = pit || v[FV_ON + sel] ? ec : C_DIM;
    char b[8];
    if (pit)
        cv_texts(4, y0 + 1, "Pitch EG", C_GRAY);
    else
        for (i = 0; i < 6u; i++) {                      /* the chips: OP1 .. OP6, "x1" for one switched off */
            int on = v[FV_ON + i] != 0, edit = i == sel;
            int32_t cx = 8 + (int32_t)i * 38;
            b[0] = on ? 'O' : 'x';
            b[1] = on ? 'P' : (char)('1' + i);
            b[2] = on ? (char)('1' + i) : 0;
            b[3] = 0;
            if (edit)
                cv_rect(cx, y0, 34, 16, C_WHITE);
            else
                cv_frame(cx, y0, 34, 16, on ? C_GRAY : C_LINE);
            cv_texts(cx + 17 - text_w(&FONT_S, b) / 2, y0 + 1, b, edit ? C_BLACK : on ? C_HI : C_DIM);
        }
    cv_rect(20, bot, 201, 1, C_LINE);                   /* the baseline, the pitch EG's zero line over it */
    if (pit)
        cv_rect(20, bot - 50 * ph / 99, 201, 1, C_LINE);
    ly = bot - l[3] * ph / 99;
    for (i = 0; i < 4u; i++) {
        if (i == 3u) {                                  /* L3 holds until the key is released */
            cv_line(x, ly, x + 30, ly, c);
            x += 30;
        }
        ny = bot - l[i] * ph / 99;
        nx = x + 8 + (99 - r[i]) * 32 / 99;
        cv_line(x, ly, nx, ny, c);
        cv_rect(nx - 2, ny - 2, 5, 5, C_WHITE);
        x = nx;
        ly = ny;
    }
    cv_texts(4, y0 + h - 16, "R high = fast  L3 holds", C_DIM);
}
/* the algorithm in rows y0 .. y0 + h of the canvas begun (big: ENV held's map, the key to the colours). The boxes as tall
 * as the rows let them be, never less than their number (the audit's #25: 12 rows, the digit's 10 of ink and a row of
 * air each side); rows too short for that: the numbers alone, in the boxes' colours. Each number in its box's middle */
#define F6_INK 10                       /* a digit's rows of ink (Terminus: rows 2..11 of 16) */
static void f6_alg_draw(int32_t y0, int32_t h, int big)
{
    const int16_t *v = f6_ed();
    uint8_t mods[6], car, fb;
    f6_node_t nd[6];
    int16_t cols = 0, rows = 0, cw, rh, px[6], py[6];
    int32_t tall = !big && h >= 90;                     /* (SOUND's FM6 rows, the rows few: a taller picture, larger boxes) */
    int32_t bw = big ? 14 : tall ? 13 : 10, bh = big ? 7 : tall ? 10 : 7, foot = big ? 6 : 12, rmax = big ? 24 : tall ? 38 : 24,
            cmax = big ? 56 : tall ? 52 : 40, boxed;
    uint32_t op, m, sel = f6_kind(f6_cur_row()) == 0u ? f6.target : 6u;
    uint16_t ec = trk_col(song.sel);
    char b[8];
    if (f6_eg_row(f6_cur_row())) {                      /* the envelope pages: their shape, not the algorithm */
        f6_eg_draw(y0, h);
        return;
    }
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
    boxed = 2 * bh >= F6_INK + 2;                       /* (room for the number inside its box) */
    if (!boxed)
        bh = F6_INK / 2;                                /* (the wires to the number's ink) */
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
            cv_line(px[op], py[op] + bh, px[op], y0 + h - foot / 2, C_DIM);
        if ((fb >> op) & 1u)                            /* lit while its feedback is above 0 */
            f6_loop(px[op], py[op], bw, bh, v[FV_FB] ? C_GRAY : C_LINE);
    }
    if (cols)                                           /* the output */
        cv_rect((240 - cols * cw) / 2 + cw / 4, y0 + h - foot / 2, cols * cw - cw / 2, 1, C_DIM);
    for (op = 0; op < 6u; op++) {
        char n[2] = {(char)('1' + op), 0};
        int on = op == sel, mute = !v[FM6_OPB(op + 1u) + FO_OL] || !v[FV_ON + op];
        uint16_t box = on ? C_WHITE : (car >> op) & 1u ? (mute ? col_shade(ec, 2u) : ec) : C_BLACK;
        if (nd[op].row < 0)
            continue;
        if (!boxed)                                     /* (no room for a box: the number alone, in its box's colour) */
            cv_texts(px[op] - 4, py[op] - 7, n, box == C_BLACK ? (mute ? C_DIM : C_GRAY) : box);
        else if (box == C_BLACK)                        /* a modulator: outlined on black, brighter when it sounds */
            cv_frame(px[op] - bw, py[op] - bh, 2 * bw, 2 * bh, mute ? C_LINE : C_GRAY);
        else
            cv_rect(px[op] - bw, py[op] - bh, 2 * bw, 2 * bh, box);
        if (boxed)                                      /* (its ink, rows 2..11, in the box's middle) */
            cv_texts(px[op] - 4, py[op] - 7, n, on || ((car >> op) & 1u && !mute) ? C_BLACK : mute ? C_DIM : C_HI);
    }
    str_cpy(b, "Alg ", sizeof b);                       /* "Alg 5", the feedback */
    fmt_int(b + 4, v[FV_ALG] + 1);
    cv_texts(4, y0 + 1, b, C_GRAY);
    if (big) {                                          /* (one line: "Car" in the carriers' colour, the feedback) */
        str_cpy(b, "Fb ", sizeof b);
        fmt_int(b + 3, v[FV_FB]);
        cv_texts(236 - text_w(&FONT_S, b) - 8 - text_w(&FONT_S, "Car"), y0 + 1, "Car", ec);
        cv_texts(236 - text_w(&FONT_S, b), y0 + 1, b, v[FV_FB] ? C_GRAY : C_DIM);
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
/* the panel, top down: the voice and the page (a line), the algorithm, the black keys' map at the foot (to the help band) */
#define F6_KEY_H 20                                     /* a key of the map */
#define F6_KEY_Y ((int32_t)OH_BODY - F6_KEY_H - 1)      /* the map's top */
#define F6_ALG_Y 15                                     /* the algorithm's top, under the voice's line (4 rows of 14 over the help band) */
static void fm6_lay_paint(void)                         /* (cv_tall: the panel to the help band, no footer) */
{
    static const char *const KEYS[10] = {"1", "2", "3", "4", "5", "6", "PI", "GL", "MO", "PO"};
    static const uint8_t KT[10] = {0, 1, 2, 3, 4, 5, FMT_PIT, FMT_GLO, FMT_MONO, FMT_POLY};
    static const uint8_t GROUP[10] = {0, 0, 0, 1, 1, 2, 2, 2, 3, 3};
    uint32_t i, kd = f6_kind(f6.row), voice = (uint32_t)TSEL->p[P_VOICE];
    char nm[16], pg[24], c[16];
    uint16_t ec = trk_col(song.sel);
    fm6_name(nm, f6_ed());
    fm6_row_name(f6.row, pg);
    cv_texts(4, 0, nm, C_WHITE);                /* the voice (its name as stored), the page (the names table's) */
    nm_title(pg, c, sizeof c);
    cv_texts(236 - text_w(&FONT_S, c), 0, c, C_GRAY);
    f6_alg_draw(F6_ALG_Y, F6_KEY_Y - F6_ALG_Y, 1);
    for (i = 0; i < 10u; i++) {
        uint32_t on = (KT[i] < 6u && kd == 0u && KT[i] == f6.target) || (KT[i] == FMT_PIT && kd == 1u) ||
                      (KT[i] == FMT_GLO && kd == 2u) || (KT[i] == FMT_MONO && (voice == V_MONO || voice == V_LEGATO)) ||
                      (KT[i] == FMT_POLY && voice == V_POLY);
        int32_t x = 6 + (int32_t)i * 21 + (int32_t)GROUP[i] * 6;
        cv_box(x, F6_KEY_Y, 19, F6_KEY_H, on ? C_WHITE : C_BLACK);
        cv_texts(x + 10 - text_w(&FONT_S, KEYS[i]) / 2, F6_KEY_Y + (F6_KEY_H - F6_INK) / 2 - 2, KEYS[i], on ? C_BLACK : ec);
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
static int fm6_on_page(void) { return 0; }
static uint32_t f6_cur_row(void) { return 0; }
static int fm6_shift_key(uint32_t k, int mute) { (void)k; (void)mute; return 0; }
static uint32_t fm6_keys_off(void) { return 0; }
static void f6_alg_draw(int32_t y0, int32_t h, int big) { (void)y0; (void)h; (void)big; }
#endif
