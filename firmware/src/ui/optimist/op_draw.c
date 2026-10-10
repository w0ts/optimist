/* SPDX-License-Identifier: GPL-3.0-only */
/* The renderer (docs/UI-OPTIMIST-DESIGN.md section 3), Felucca 1.0's structure in Terminus:
 *   y   0..24   the header: the selected track's engine (in its colour), the screen and row, the loop position, the
 *               tempo; a passive message (2.5 s: MISSING, RECORDING) takes it over
 *   y  28..72   four cards: the cursor row's cells, KNOB 1..4 (label, value, unit, its form; the hot cell white)
 *   y  76..239  the panel (to the screen's foot: no footer, the user, 2026-10-08): the list of rows (each value a number over its form; the cursor row a bar in the track's
 *               colour; on SOUND the cursor row's graph above it), or STEP's grid / roll (op_stepdraw.c), or the
 *               scope (op_scope.c); a question covers it with the modal, the result of a confirmed action shows as a
 *               toast in its middle
 *               the mixer's panel: a row a track, MASTER above T1, the drum lanes after DR (op_mixdraw.c)
 * What the footer said went elsewhere: STEP's window and pick to the header, a held step's values under its grid,
 * a layer's "Home locks it" to the header once.
 * Lazy: each band remembers a signature of what it drew and is drawn again only when that changes; the meters
 * and the playheads are small canvases of their own, so a playing mixer never redraws a whole band. Each band is
 * one canvas of at most 240 x 124 pixels (gfx.c CV_MAX; a band: 124 rows at most). */
#define ROW_H 20
#define OH_BODY OP_PH                 /* the panel to the screen's foot: there is no footer (the user, 2026-10-08) */
#define ROWS_SHOWN ((uint32_t)OH_BODY / ROW_H)   /* 8 rows; CARDS 2x2: 5 */
#define METER_H 90                      /* meter_h's scale (op_mixdraw.c scales it to a row's meter) */
static void draw_step_panel(void);                     /* op_stepdraw.c: STEP's grid / roll */
static void lay_title(char *t, uint32_t n);             /* op_laydraw.c: a performance layer's map */
static void lay_draw_cards(void);
static void lay_draw_tiles(void);
static uint32_t tempo_sig(void);
#if FELUCCA_PATTERNS
static uint32_t song_grid_sig(void);                    /* op_laydraw.c: SONG's session grid */
static void song_grid_draw(int32_t h);
#endif
static void tempo_draw(int32_t h);
static void mx_draw(void);                              /* op_mixdraw.c: the mixer's rows */
static void mx_redraw(void);
static void scope_draw(void);                           /* op_scope.c: the oscilloscope */
static uint32_t op_overlay(void);
static uint32_t f6_graph_sig(void);                     /* op_fm6draw.c: FM6's algorithm */
static void f6_alg_draw(int32_t y0, int32_t h, int big);

static uint32_t hs(uint32_t h, const char *s)          /* a signature: FNV-1a over a string */
{
    while (*s)
        h = (h ^ (uint8_t)*s++) * 16777619u;
    return h * 16777619u;
}
static uint32_t hu(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }
static uint32_t hc(uint32_t h, const cell_t *c)        /* a cell's signature: all it draws */
{
    h = hs(hs(hu(hu(hu(h, c->kind + c->mark * 16u), c->col), (uint32_t)c->gk << 16 ^ (uint16_t)c->gv), c->label ? c->label : ""), c->val);
    return hs(h, c->unit);
}

/* s cut to n characters into b (b holds n + 1) */
static const char *cut(char *b, const char *s, uint32_t n)
{
    str_cpy(b, s, n + 1u);
    return b;
}

/* the loop position of track t: "bar.beat" in its pattern */
static void loop_pos(const track_t *t, char *b)
{
    uint32_t len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP), s = t->seq_idx % len, k;
    fmt_int(b, (int32_t)(s / 16u + 1u));
    k = str_len(b);
    b[k] = '.';
    b[k + 1] = (char)('1' + (s % 16u) / 4u);
    b[k + 2] = 0;
}

/* ---- the header */
/* the screen and its cursor row into t (n bytes): "SOUND ENV"; a row that starts with the screen's name carries it
 * already, so it stands alone ("PROJECT", not "PROJECT PROJECT"; "FX SLOTS" on FX) */
static void head_title(uint32_t scr, uint32_t row, char *t, uint32_t n)
{
    const char *s = SCR_NAME[scr % SCR_N];
    char r[16];
    uint32_t k;
    if (scr == SCR_STEP && st.held) {                   /* a step held: its card page, "STEP 3/9 LFO" */
        hp_title(t, n);
        return;
    }
    if (scr == SCR_STEP) {                              /* the window: "STEPS 17-32" */
        uint32_t a = st.page * 16u + 1u, b = a + 15u < trk_len(TSEL) ? a + 15u : trk_len(TSEL);
        str_cpy(t, "STEPS ", n);
        fmt_int(t + 6, (int32_t)a);
        str_cpy(t + str_len(t), "-", n - str_len(t));
        fmt_int(t + str_len(t), (int32_t)b);
        return;
    }
    SCREENS[scr % SCR_N].name(row, r);
    if (scr == SCR_FX) {                                /* the global FX: "FX MASTER", then the page ("FX MASTER DELAY") */
        int ms = r[0] == 'M' && r[1] == 'A' && r[2] == 'S' && r[3] == 'T' && r[4] == 'E' && r[5] == 'R';   /* ("MASTER COMP") */
        str_cpy(t, ms ? "FX" : "FX MASTER", n);
        if (ms || r[0] != 'F' || r[1] != 'X') {
            str_cpy(t + str_len(t), " ", n - str_len(t));
            str_cpy(t + str_len(t), r, n - str_len(t));
        }
        return;
    }
    if (scr == SCR_HOME && row % MXR_N != MXR_MASTER) { /* the mixer: the row's full name and the set, "SNARE LEVELS" */
        if (row % MXR_N >= MXR_LANE0)
            str_cpy(t, LANE_SHORT[(row % MXR_N - MXR_LANE0) % DRUM_LANES], n);
        else
            str_cpy(t, trk_tag(mx_trk_of(row % MXR_N)), n);
        for (k = 0; t[k]; k++)
            t[k] = (char)(t[k] >= 'a' && t[k] <= 'z' ? t[k] - 32 : t[k]);
        str_cpy(t + str_len(t), " ", n - str_len(t));
        str_cpy(t + str_len(t), r, n - str_len(t));
        return;
    }
    for (k = 0; s[k] && s[k] == r[k]; k++)
        ;
    if (!s[k] && (!r[k] || r[k] == ' ')) {
        str_cpy(t, r, n);
        return;
    }
    str_cpy(t, s, n);
    str_cpy(t + str_len(t), " ", n - str_len(t));
    str_cpy(t + str_len(t), r, n - str_len(t));
}
/* the header's badge: the selected track's engine (the drum track: DRUMS), in its colour (the user, 2026-10-08: the
 * track tag "brings no value, put the algo there"; the track is the colour and the strips) */
static const char *head_engine(void)
{
    return is_drum(TSEL) ? "DRUMS" : ENGINES[TSEL->eng_req % NENGINES]->name;
}
static void draw_head(void)
{
    char t[32], c[32], r[16], bpm[8];
    uint32_t sig, rec = song.rec || rec_wait || ft_on;
    uint16_t tc = trk_col(song.sel), mc = C_HI;
    const char *eng = head_engine();
    if (ui.scr == SCR_FX || ui.master) {                /* MASTER: its badge in C_HI, as the mixer's M row */
        eng = "MASTER";
        tc = C_HI;
    }
    int32_t bw = text_w(&FONT_S, eng) + 8, tx, room;
    if (ui.msg_t) {
        mc = ui.msg_st ? C_STATUS[ui.msg_st & 3u] : C_HI;
    } else if (name_on()) {
        name_title(t, sizeof t);                        /* NAME: "Name U03", "Name project 3" */
    } else if (lay.shown != LY_PLAY) {
        lay_title(t, sizeof t);                         /* a layer: its name ("Scenes", "FX locked") */
    } else {
        head_title(ui.scr, ui.row[ui.scr], t, sizeof t);
    }
    op_case(c, t, sizeof c);                            /* ("Sound ENV", "Mix master") */
    str_cpy(t, c, sizeof t);
    if (!ui.msg_t && lay.shown == LY_PLAY && ui.scr == SCR_STEP && !st.held) {   /* STEP: the window, then the pick (no footer): */
        if (is_drum(TSEL))                              /* the lane's short name as the kit names it ("Steps 1-16 */
            str_cpy(c, LANE_SHORT[lane_selected()], sizeof c);   /* o.hat"), a synth's notes ("Steps 1-16 C4 E4+") */
        else
            step_pick_name(c, sizeof c);
        str_cpy(t + str_len(t), " ", sizeof t - str_len(t));
        str_cpy(t + str_len(t), c, sizeof t - str_len(t));
    }
    loop_pos(TSEL, r);
    if (ui.scr == SCR_STEP)
        r[0] = 0;                                       /* (STEP: the grid shows the playhead; the room is the pick's) */
    fmt_int(bpm, song.g[G_BPM]);
    sig = hs(hs(hs(hs(hu(hu(hu(1u, tc), mc), rec * 2u + song.playing), ui.msg_t ? ui.msg : t), r), bpm), eng);
    sig = hu(sig, (uint32_t)(ui.msg_t != 0) + settings.palette * 4u);
    if (sig == ui.sig[0])
        return;
    ui.sig[0] = sig;
    cv_begin(240, OH_HEAD, C_BLACK);
    if (ui.msg_t) {
        cv_text(4, 4, &FONT_S, op_case(c, ui.msg, sizeof c), mc);   /* (sentence case: "T1 cleared") */
    } else {
        cv_rect(2, 3, bw, 19, tc);                      /* the track's engine, in its colour */
        cv_text(6, 4, &FONT_S, eng, C_BLACK);
        tx = bw + 8;                                    /* the title, cut to the room left of the position */
        room = 236 - text_w(&FONT_S, bpm) - 16 - (r[0] ? text_w(&FONT_S, r) + 8 : 0) - tx;
        cv_text(tx, 4, &FONT_S, cut(c, t, room > 8 ? (uint32_t)room / 8u : 1u), mc);   /* (cased above) */
        cv_text(236 - text_w(&FONT_S, bpm), 4, &FONT_S, bpm, C_HI);
        if (r[0])
            cv_text(236 - text_w(&FONT_S, bpm) - 8 - text_w(&FONT_S, r), 4, &FONT_S, r, song.playing ? C_OK : C_DIM);
        if (rec)
            cv_rect(236 - text_w(&FONT_S, bpm) - 16 - text_w(&FONT_S, r), 9, 6, 6, C_ERR);   /* recording / armed */
    }
    cv_line(0, OH_HEAD - 1, 239, OH_HEAD - 1, C_LINE);
    cv_blit(0, OY_HEAD);
}

/* ---- the cards: label, value and unit, the value's form under them */
static void draw_card(uint32_t k, int32_t x, const cell_t *c, uint32_t hot)
{
    char b[16];
    uint16_t vc = hot ? C_WHITE : c->kind == CK_RO ? (c->col ? c->col : C_AMB) : knob_col(k, c->col ? c->col : C_HI);   /* (KNOB COLORS: knob k's) */
    int32_t vx;
    cv_rect(x, 0, CARD_W, OH_CARD - 1, OP_SURF);
    if (c->col)
        cv_rect(x, 0, 2, OH_CARD - 1, c->col);          /* the track's / the engine's colour */
    if (hot)
        cv_rect(x, 0, CARD_W, 2, C_WHITE);              /* the hot cell: PRESETS and YES act on it */
    if (c->mark == 2u) {                                /* a hold event on the step held: an arrow, top right */
        cv_rect(x + CARD_W - 10, 7, 6, 2, C_AMB);
        cv_rect(x + CARD_W - 5, 5, 1, 6, C_AMB);
        cv_rect(x + CARD_W - 4, 6, 1, 4, C_AMB);
        cv_rect(x + CARD_W - 3, 7, 1, 2, C_AMB);
    } else if (c->mark) {                               /* a lock on the step held: a padlock, top right */
        cv_rect(x + CARD_W - 9, 7, 7, 5, C_WARN);
        cv_rect(x + CARD_W - 8, 4, 1, 3, C_WARN);
        cv_rect(x + CARD_W - 4, 4, 1, 3, C_WARN);
        cv_rect(x + CARD_W - 8, 3, 5, 1, C_WARN);
    }
    if (!c->label)
        return;
    cv_text(x + (text_w(&FONT_S, c->label) > CARD_W - 4 ? 0 : 3), 3, &FONT_S, cut(b, op_label(b, c->label, sizeof b), 7),
            knob_col(k, C_GRAY));                       /* ("ATK" as printed, "Engine" in sentence case) */
    if (c->kind == CK_ACT) {
        cv_text(x + 3, 20, &FONT_S, "YES", hot ? C_WHITE : C_AMB);   /* an action: YES does it */
        return;
    }
    vx = cv_text(x + 3, 20, &FONT_S, cut(b, c->val, 6), vc);
    if (c->unit[0] && vx + text_w(&FONT_S, c->unit) <= x + CARD_W - 1)
        cv_text(vx, 20, &FONT_S, c->unit, C_DIM);
    draw_gauge(x + 4, 38, CARD_W - 8, 4, c, vc, C_LINE);
}
/* CARDS 2x2 (the user, 2026-10-08; SLOOP 2.4's big values, ui/sloop/ui_draw.c graph_big's layout, copied): value k
 * in large type in a 2 x 2 block placed as the knobs sit (KNOB 1 top left, 2 top right, 3 bottom left, 4 bottom
 * right), its label and unit small above it, its form under it; the hot one white. A value too wide for the large
 * face (a name) is drawn in the small one */
#define BIG_W 116
#define BIG_H 46
static void draw_big(uint32_t k, const cell_t *c, uint32_t hot)
{
    char b[16];
    int32_t x = k & 1u ? 121 : 3, y = k & 2u ? BIG_H + 1 : 0, uw = c->unit[0] ? text_w(&FONT_S, c->unit) + 4 : 0;
    uint16_t vc = hot ? C_WHITE : c->kind == CK_RO ? (c->col ? c->col : C_AMB) : knob_col(k, c->col ? c->col : C_HI);   /* (KNOB COLORS) */
    cv_rect(x, y, BIG_W, BIG_H, OP_SURF);
    if (c->col)
        cv_rect(x, y, 2, BIG_H, c->col);
    if (hot)
        cv_rect(x, y, BIG_W, 2, C_WHITE);
    if (c->mark == 2u) {                                /* a hold event on the step held: an arrow, top right */
        cv_rect(x + BIG_W - 10, y + 7, 6, 2, C_AMB);
        cv_rect(x + BIG_W - 5, y + 5, 1, 6, C_AMB);
        cv_rect(x + BIG_W - 4, y + 6, 1, 4, C_AMB);
        cv_rect(x + BIG_W - 3, y + 7, 1, 2, C_AMB);
        uw += 10;
    } else if (c->mark) {                               /* a lock on the step held: a padlock, top right */
        cv_rect(x + BIG_W - 9, y + 7, 7, 5, C_WARN);
        cv_rect(x + BIG_W - 8, y + 3, 5, 4, C_WARN);
        uw += 10;
    }
    if (!c->label)
        return;
    {
        char l[16];
        cv_text(x + 5, y + 2, &FONT_S, cut(b, op_label(l, c->label, sizeof l), (uint32_t)(BIG_W - 10 - uw) / 8u), knob_col(k, C_GRAY));
    }
    if (c->unit[0])
        cv_text(x + BIG_W - 3 - text_w(&FONT_S, c->unit) - (c->mark ? 10 : 0), y + 2, &FONT_S, c->unit, C_DIM);
    if (c->kind == CK_ACT) {
        cv_text(x + 5, y + 13, font_big(), "YES", hot ? C_WHITE : C_AMB);   /* an action: YES does it */
        return;
    }
    if (text_w(font_big(), c->val) <= BIG_W - 10)
        cv_text(x + 5, y + 13, font_big(), c->val, vc);
    else
        cv_text(x + 5, y + 20, &FONT_S, cut(b, c->val, 13), vc);
    draw_gauge(x + 5, y + BIG_H - 3, BIG_W - 10, 2, c, vc, C_LINE);
}
/* the cards' band: the four cells c, hot the one white (4: none), as CARDS sets them (op_state.c op_cards) */
static void draw_card_band(const cell_t *c, uint32_t hot)
{
    uint32_t k;
    cv_begin(240, OP_CH, C_BLACK);
    for (k = 0; k < 4u; k++) {
        if (cards_2x2())
            draw_big(k, &c[k], k == hot);
        else
            draw_card(k, CARD_X(k), &c[k], k == hot);
    }
    cv_blit(0, OY_CARD);
}
static void draw_cards(void)
{
    cell_t c[4];
    uint32_t k, sig = hu(hu(ui.hot * 2u + ui.hot_lit, settings.palette), op_cards + knob_colors * 2u);
    for (k = 0; k < 4u; k++) {
        SCR->cell(ui.row[ui.scr], k, &c[k]);
        sig = hc(sig, &c[k]);
    }
    if (sig == ui.sig[1])
        return;
    ui.sig[1] = sig;
    draw_card_band(c, ui.hot_lit ? ui.hot : 4u);
}

/* ---- the panel: the rows, each value a number over its form; SOUND: the cursor row's graph on top */
/* a row rh px high (ROW_H, or taller when a page's few rows fill the panel: its text and gauge stay together, in the
 * middle of the row, the gauge thicker) */
static void draw_row(uint32_t i, int32_t y, uint32_t on, uint16_t bar, int32_t wide, int32_t rh)
{
    char nm[12], b[16];
    uint32_t k, j, w;
    cell_t c, e;
    int32_t gh = 2 + (rh - ROW_H) / 10;
    if (on)
        cv_rect(0, y, wide, rh - 1, bar);               /* the cursor row: a bar, ink on it */
    y += (rh - ROW_H) / 2;
    SCR->name(i, nm);
    cv_text(4, y + 1, &FONT_S, op_case(b, cut(nm, nm, 9), sizeof b), on ? C_BLACK : C_GRAY);   /* "ENV dest" */
    if (ui.scr == SCR_SONG && song_row_col(i))         /* SONG: the scene playing, queued; the part playing */
        cv_rect(70, y + 5, 7, 7, song_row_col(i));
    for (k = 0; k < 4u; k++) {
        int32_t x = 80 + 40 * (int32_t)k;
        SCR->cell(i, k, &c);
        for (j = k + 1u, w = 4u; j < 4u; j++, w += 5u) {   /* 4 characters, more over the empty cells after it */
            SCR->cell(i, j, &e);
            if (e.label || e.val[0])
                break;
        }
        if (c.kind == CK_ACT && c.label)                /* an action: its name, dim */
            cv_text(x, y + 1, &FONT_S, cut(b, op_label(b, c.label, sizeof b), w > 7u ? 7u : w), on ? C_BLACK : C_DIM);
        else if (c.val[0])
            cv_text(x, y + 1, &FONT_S, cut(b, c.val, c.gk != GK_NONE ? 4u : w > 13u ? 13u : w), on ? C_BLACK : C_HI);
        draw_gauge(x, y + 16, 36, gh, &c, on ? C_BLACK : c.col ? c.col : C_AMB, on ? col_shade(bar, 5u) : C_LINE);
    }
}
/* a band taller than a canvas (gfx.c CV_MAX: 240 x 124): drawn in passes of 120 rows, fn painting the whole band in
 * its own coordinates each time (gfx.c cv_oy shifts and clips every primitive) */
static void cv_tall(uint32_t y, uint32_t h, uint16_t bg, void (*fn)(void))
{
    uint32_t r0, n;
    for (r0 = 0; r0 < h; r0 += n) {
        n = h - r0 < 120u ? h - r0 : 120u;
        cv_begin(240, n, bg);
        cv_oy = -(int32_t)r0;
        fn();
        cv_oy = 0;
        cv_blit(0, y + r0);
    }
}
static struct {
    const page_t *gp;
    uint32_t pic, first, shown, n, cur;
    int32_t top, rh;
    uint16_t bar;
} lst;                                                  /* what list_paint draws (draw_list) */
static void list_paint(void)
{
    uint32_t i;
    if (lst.pic == 1u)
        draw_sound_graph(lst.gp);
    else if (lst.pic == 3u)
        tempo_draw(GRAPH_H);
    else if (lst.pic == 4u)
        f6_alg_draw(0, GRAPH_H, 0);
    else if (lst.pic == 5u)
        pre_draw();
#if FELUCCA_PATTERNS
    else if (lst.pic == 2u)
        song_grid_draw(GRAPH_H);
#endif
    if (lst.pic)
        cv_line(0, GRAPH_H + 1, 239, GRAPH_H + 1, C_LINE);
    for (i = lst.first; i < lst.n && i < lst.first + lst.shown; i++)
        draw_row(i, lst.top + (int32_t)(i - lst.first) * lst.rh, i == lst.cur, lst.bar, lst.n > lst.shown ? 236 : 240, lst.rh);
    if (lst.n > lst.shown) {                            /* where the window is in the list */
        int32_t h = (OH_BODY - lst.top) * (int32_t)lst.shown / (int32_t)lst.n;
        cv_rect(237, lst.top, 3, OH_BODY - lst.top, C_LINE);
        cv_rect(237, lst.top + (OH_BODY - lst.top) * (int32_t)lst.first / (int32_t)lst.n, 3, h, C_GRAY);
    }
}
static void draw_list(void)
{
    uint32_t n = SCR->rows(), cur = ui.row[ui.scr], first = 0, i, k, sig, shown = ROWS_SHOWN;
    uint16_t bar = trk_col(song.sel);
    const page_t *gp = ui.scr == SCR_SOUND ? snd_graph_page(cur) : 0;
    int32_t top = 0, rh = ROW_H;
    uint32_t pic = 0;                                   /* the picture over the rows: 1 SOUND's graph, 2 the session
                                                         * grid (SONG's PATTERNS row), 3 the tempo */
    char nm[12];
    cell_t c;
    pic = gp ? 1u : ui.scr == SCR_TEMPO ? 3u : 0u;
    if (ui.scr == SCR_SOUND && snd_fam == SND_FM6)
        pic = 4u;                                       /* FM6's rows: the algorithm (op_fm6draw.c) */
    else if (ui.scr == SCR_SOUND && !snd_page(cur))
        pic = 5u;                                       /* the SOUND row: the presets with their engines */
#if FELUCCA_PATTERNS
    if (song_on_pat_row())
        pic = 2u;
#endif
    if (pic) {                                          /* the picture takes the height the rows leave: they sit at the foot */
        uint32_t fit = (uint32_t)(OH_BODY - GRAPH_MIN - 3) / ROW_H;
        shown = n < fit ? n : fit;
        top = OH_BODY - (int32_t)shown * ROW_H;
        gr_h = top - 3;
    } else if (n && n <= shown) {                       /* no picture, rows to spare: taller rows fill the panel */
        shown = n;
        rh = OH_BODY / (int32_t)n;
        rh = rh > 40 ? 40 : rh;                         /* (a few rows, no picture: at most 40 px a row, at the top) */
    }
    if (cur >= shown / 2u)
        first = cur - shown / 2u;
    if (first + shown > n)
        first = n > shown ? n - shown : 0u;
    sig = hu(hu(hu(hu(hu(7u, n), cur), first), bar + settings.palette * 65536u), (uint32_t)(gp - PAGES));
    for (i = first; i < n && i < first + shown; i++) {
        SCR->name(i, nm);
        sig = hs(sig, nm);
        for (k = 0; k < 4u; k++) {
            SCR->cell(i, k, &c);
            sig = hc(sig, &c);
        }
    }
    sig = hu(sig, pic == 1u ? graph_sig() : pic == 3u ? tempo_sig() : pic == 4u ? f6_graph_sig() : pic == 5u ? pre_sig() :
                  pic * 977u);
#if FELUCCA_PATTERNS
    if (pic == 2u)
        sig = hu(sig, song_grid_sig());
#endif
    sig = hu(sig, (uint32_t)(top * 256 + rh));
    if (ui.scr == SCR_SONG)
        for (i = first; i < n && i < first + shown; i++)
            sig = hu(sig, song_row_col(i));
    if (sig == ui.sig[2])
        return;
    ui.sig[2] = sig;
    lst.gp = gp, lst.pic = pic, lst.first = first, lst.shown = shown, lst.n = n, lst.cur = cur, lst.top = top, lst.rh = rh;
    lst.bar = bar;
    cv_tall(OP_PY, OH_BODY, C_BLACK, list_paint);    /* (the panel to the screen's foot: two passes) */
}

/* ---- pieces of the mixer's rows (op_mixdraw.c) */
static uint32_t meter_h(int32_t pk)                     /* a peak (32767 = 0 dBFS) as a height: 6 dB a step */
{
    uint32_t lg = 0, a = pk > 0 ? (uint32_t)pk : 0u;
    while (a >>= 1)
        lg++;
    return lg < 5u ? 0u : (uint32_t)clamp(((int32_t)(lg - 5u) * 4 + 4) * METER_H / 44, 0, METER_H);
}
static void cv_frame(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)   /* a 1 px outline */
{
    cv_rect(x, y, w, 1, c);
    cv_rect(x, y + h - 1, w, 1, c);
    cv_rect(x, y, 1, h, c);
    cv_rect(x + w - 1, y, 1, h, c);
}
/* ---- no footer (the user, 2026-10-08: "no footer anywhere"): every screen draws to the screen's foot. Under a
 * question the modal covers the panel's first MODAL_H rows; the rows below it are an empty band */
static void draw_under(void)
{
    if ((op_overlay() != 1u && op_overlay() != 3u) || ui.sig[3] == 1u)
        return;
    ui.sig[3] = 1u;
    if (OP_PH > MODAL_H) {                              /* (CARDS 2x2: the modal takes the whole panel) */
        cv_begin(240, (uint32_t)(OP_PH - MODAL_H), C_BLACK);
        cv_blit(0, (uint32_t)(OP_PY + MODAL_H));
    }
}

/* ---- the overlay: the modal (a question) or the toast (a result) over the panel */
static uint32_t op_overlay(void) { return rh.ring ? 3u : op_armed() ? 1u : ui.toast_t ? 2u : 0u; }
/* REC held: the ring over the panel (SLOOP's hold screen's ring, copied: 36 px, 7 thick, filling clockwise from the
 * top), in the track's instrument colour, its name, "Keep holding"; no question: the ring is the confirmation */
static void draw_ring(void)
{
    uint32_t el = fm1_ms - rh.t1;
    int32_t a, rr, end = el >= RH_CLEAR_MS ? 1024 : (int32_t)(el * 1024u / RH_CLEAR_MS), cy = MODAL_H / 2 - 14;
    uint16_t tc = trk_col(rh.trk);
    char b[16];
    cv_begin(240, MODAL_H, C_BLACK);
    for (a = 0; a < 1024; a += 2) {
        int32_t co = SINE[((uint32_t)a + 768u) & 1023u], si = SINE[(uint32_t)a & 1023u];   /* from 12 o'clock */
        uint16_t c = a <= end ? tc : C_LINE;
        for (rr = 30; rr <= 36; rr++)
            cv_pset(120 + ((si * rr) >> 15), cy + ((co * rr) >> 15), c);
    }
    str_cpy(b, "Clear ", sizeof b);
    str_cpy(b + 6, trk_tag(rh.trk), sizeof b - 6u);
    cv_text(120 - text_w(&FONT_S, b) / 2, cy + 40, &FONT_S, b, tc);
    cv_text(120 - text_w(&FONT_S, "Keep holding") / 2, cy + 56, &FONT_S, "Keep holding", C_GRAY);
    cv_blit(0, OP_PY);
}
static void draw_overlay(uint32_t ov)
{
    uint32_t sig = ov == 3u ? hu(hu(0x71u, (fm1_ms - rh.t1) * 64u / RH_CLEAR_MS), rh.trk * 16u + settings.palette)
                 : ov == 1u ? hs(hs(hu(hu(3u, ui.arm_danger), settings.palette), ui.arm_verb), ui.arm_arg)
                           : hs(hu(4u, ui.msg_st), ui.msg);
    if (sig == ui.sig[4])
        return;
    ui.sig[4] = sig;
    if (ov == 3u)
        draw_ring();
    else if (ov == 1u)
        draw_modal();
    else
        draw_toast();
}

static void op_frame_draw(void)
{
    uint32_t ov = op_overlay();
    if (ui.force) {
        lcd_fill(0, 0, 240, 240, C_BLACK);
        memset(ui.sig, 0, sizeof ui.sig);
    }
    if (ov != ui.overlay || ui.force) {                 /* the panel under the overlay: again when it goes */
        ui.overlay = (uint8_t)ov;
        ui.sig[2] = ui.sig[4] = 0;
        ui.sig[3] = 0;                                  /* (the band under a question) */
        mx_redraw();
    }
    draw_head();
    if (name_on()) {                                    /* NAME: the field and the keyboard (op_name.c), no footer */
        name_draw();
        ui.force = 0;
        return;
    }
    if (lay.shown != LY_PLAY)
        lay_draw_cards();                               /* a layer: its knobs' cards, its tiles (op_laydraw.c) */
    else
        draw_cards();                                   /* (the mixer too: its knobs are the selected row's) */
    if (ov == 1u || ov == 3u) {
        draw_overlay(ov);                               /* the modal, the REC ring: the whole panel */
    } else {
        uint32_t under = ui.sig[2];
        if (lay.shown != LY_PLAY)
            lay_draw_tiles();
        else if (ui.scr == SCR_HOME)
            mx_draw();                                  /* the mixer's rows: op_mixdraw.c */
        else if (ui.scr == SCR_SCOPE)
            scope_draw();                               /* the oscilloscope: op_scope.c */
        else if (ui.scr == SCR_STEP)
            draw_step_panel();                          /* the grid / the roll: op_stepdraw.c */
        else
            draw_list();
        if (ov == 2u) {                                 /* the toast over the live panel: again when it redrew */
            if (ui.sig[2] != under)
                ui.sig[4] = 0;
            draw_overlay(ov);
        }
    }
    draw_under();
    ui.force = 0;
}
