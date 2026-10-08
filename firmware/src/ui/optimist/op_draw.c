/* SPDX-License-Identifier: GPL-3.0-only */
/* The renderer (docs/UI-OPTIMIST-DESIGN.md section 3), Felucca 1.0's structure in Terminus:
 *   y   0..24   the header: the selected track's engine (in its colour), the screen and row, the loop position, the
 *               tempo; a passive message (2.5 s: MISSING, RECORDING) takes it over
 *   y  28..72   four cards: the cursor row's cells, KNOB 1..4 (label, value, unit, its form; the hot cell white)
 *   y  76..239  the panel (to the screen's foot: no footer, the user, 2026-10-08): the list of rows (each value a number over its form; the cursor row a bar in the track's
 *               colour; on SOUND the cursor row's graph above it), or STEP's grid / roll (op_stepdraw.c), or the
 *               scope (op_scope.c); a question covers it with the modal, the result of a confirmed action shows as a
 *               toast in its middle
 *   y  27..239  the mixer and the drum mixer, instead of the cards and the panel: four strips (the drum mixer: 4, 8
 *               or 16), the control SELECT is on lit on all of them (draw_mixer, op_dmixdraw.c)
 * What the footer said went elsewhere: STEP's window and pick to the header, a held step's values under its grid,
 * a layer's "Home locks it" to the header once.
 * Lazy: each band remembers a signature of what it drew and is drawn again only when that changes; the meters
 * and the playheads are small canvases of their own, so a playing mixer never redraws a whole band. Each band is
 * one canvas of at most 240 x 124 pixels (gfx.c CV_MAX; a band: 124 rows at most). */
#define ROW_H 20
#define OH_BODY (240 - OY_PANEL)        /* the panel to the screen's foot: there is no footer (the user, 2026-10-08) */
#define ROWS_SHOWN 8u                   /* (OH_BODY / ROW_H) */
#define METER_H 90                      /* the mixer's faders and meters (the strips to the screen's foot) */
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
static void dm_draw(void);                              /* op_dmixdraw.c: the drum mixer */
static void dm_redraw(void);
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
    if (scr == SCR_STEP) {                              /* the window: "STEPS 17-32" */
        uint32_t a = st.page * 16u + 1u, b = a + 15u < trk_len(TSEL) ? a + 15u : trk_len(TSEL);
        str_cpy(t, "STEPS ", n);
        fmt_int(t + 6, (int32_t)a);
        str_cpy(t + str_len(t), "-", n - str_len(t));
        fmt_int(t + str_len(t), (int32_t)b);
        return;
    }
    SCREENS[scr % SCR_N].name(row, r);
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
    if (!ui.msg_t && lay.shown == LY_PLAY && ui.scr == SCR_STEP) {   /* STEP: the window, then the pick (no footer): */
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
static void draw_card(int32_t x, const cell_t *c, uint32_t hot)
{
    char b[16];
    uint16_t vc = hot ? C_WHITE : c->col ? c->col : c->kind == CK_RO ? C_AMB : C_HI;
    int32_t vx;
    cv_rect(x, 0, CARD_W, OH_CARD - 1, OP_SURF);
    if (c->col)
        cv_rect(x, 0, 2, OH_CARD - 1, c->col);          /* the track's / the engine's colour */
    if (hot)
        cv_rect(x, 0, CARD_W, 2, C_WHITE);              /* the hot cell: PRESETS and YES act on it */
    if (c->mark) {                                      /* a lock on the step held: a padlock, top right */
        cv_rect(x + CARD_W - 9, 7, 7, 5, C_WARN);
        cv_rect(x + CARD_W - 8, 4, 1, 3, C_WARN);
        cv_rect(x + CARD_W - 4, 4, 1, 3, C_WARN);
        cv_rect(x + CARD_W - 8, 3, 5, 1, C_WARN);
    }
    if (!c->label)
        return;
    cv_text(x + (text_w(&FONT_S, c->label) > CARD_W - 4 ? 0 : 3), 3, &FONT_S, cut(b, op_label(b, c->label, sizeof b), 7),
            C_GRAY);                                    /* ("ATK" as printed, "Engine" in sentence case) */
    if (c->kind == CK_ACT) {
        cv_text(x + 3, 20, &FONT_S, "YES", hot ? C_WHITE : C_AMB);   /* an action: YES does it */
        return;
    }
    vx = cv_text(x + 3, 20, &FONT_S, cut(b, c->val, 6), vc);
    if (c->unit[0] && vx + text_w(&FONT_S, c->unit) <= x + CARD_W - 1)
        cv_text(vx, 20, &FONT_S, c->unit, C_DIM);
    draw_gauge(x + 4, 38, CARD_W - 8, 4, c, vc, C_LINE);
}
static void draw_cards(void)
{
    cell_t c[4];
    uint32_t k, sig = hu(ui.hot * 2u + ui.hot_lit, settings.palette);
    for (k = 0; k < 4u; k++) {
        SCR->cell(ui.row[ui.scr], k, &c[k]);
        sig = hc(sig, &c[k]);
    }
    if (sig == ui.sig[1])
        return;
    ui.sig[1] = sig;
    cv_begin(240, OH_CARD, C_BLACK);
    for (k = 0; k < 4u; k++)
        draw_card(CARD_X(k), &c[k], k == ui.hot && ui.hot_lit);
    cv_blit(0, OY_CARD);
}

/* ---- the panel: the rows, each value a number over its form; SOUND: the cursor row's graph on top */
static void draw_row(uint32_t i, int32_t y, uint32_t on, uint16_t bar, int32_t wide)
{
    char nm[12], b[16];
    uint32_t k, j, w;
    cell_t c, e;
    if (on)
        cv_rect(0, y, wide, ROW_H - 1, bar);            /* the cursor row: a bar, ink on it */
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
        draw_gauge(x, y + 16, 36, 2, &c, on ? C_BLACK : c.col ? c.col : C_AMB, on ? col_shade(bar, 5u) : C_LINE);
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
    int32_t top;
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
        draw_row(i, lst.top + (int32_t)(i - lst.first) * ROW_H, i == lst.cur, lst.bar, lst.n > lst.shown ? 236 : 240);
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
    int32_t top = 0;
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
    if (pic) {
        top = GRAPH_H + 3;                              /* the picture, then five rows */
        shown = (uint32_t)(OH_BODY - top) / ROW_H;
    }
    if (cur >= shown / 2u)
        first = cur - shown / 2u;
    if (n > shown && first > n - shown)
        first = n - shown;
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
    if (ui.scr == SCR_SONG)
        for (i = first; i < n && i < first + shown; i++)
            sig = hu(sig, song_row_col(i));
    if (sig == ui.sig[2])
        return;
    ui.sig[2] = sig;
    lst.gp = gp, lst.pic = pic, lst.first = first, lst.shown = shown, lst.n = n, lst.cur = cur, lst.top = top;
    lst.bar = bar;
    cv_tall(OY_PANEL, OH_BODY, C_BLACK, list_paint);    /* (the panel to the screen's foot: two passes) */
}

/* ---- the mixer: four strips, one a track (Felucca's MIXER), the screen's height */
static uint32_t meter_h(int32_t pk)                     /* a peak (32767 = 0 dBFS) as a height: 6 dB a step */
{
    uint32_t lg = 0, a = pk > 0 ? (uint32_t)pk : 0u;
    while (a >>= 1)
        lg++;
    return lg < 5u ? 0u : (uint32_t)clamp(((int32_t)(lg - 5u) * 4 + 4) * METER_H / 44, 0, METER_H);
}
/* track c's 16 steps of the page playing (the step lit in its colour, the playhead white) at x, y; *drawn: what was
 * drawn there (the step playing, or the page while stopped): again only when that changes. lane: a drum lane's steps
 * (the drum mixer), else DRUM_LANES (the track's) */
static void draw_steps_of(uint32_t c, uint32_t lane, uint32_t x, uint32_t y, uint16_t bg, uint8_t *drawn)
{
    const track_t *t = &trk[c % NTRK];
    uint32_t len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP), at = t->seq_idx % len, page = at & ~15u, i;
    uint32_t key = song.playing ? at : 64u + (page >> 4);
    uint16_t col = lane < DRUM_LANES ? lane_col(lane) : trk_col(c % NTRK);
    if (key == *drawn)
        return;
    *drawn = (uint8_t)key;
    cv_begin(48, 6, bg);
    for (i = 0; i < 16u; i++) {
        uint32_t on = lane < DRUM_LANES ? dstep_has(&t->dstep[(page + i) % NSTEP], lane) : trk_on_step(c % NTRK, page + i);
        cv_rect((int32_t)i * 3, 0, 2, 6, song.playing && page + i == at ? C_WHITE : page + i >= len ? bg : on ? col : C_LINE);
    }
    cv_blit(x, y);
}
static void draw_steps(uint32_t c, uint32_t x, uint32_t y, uint16_t bg, uint8_t *drawn)
{
    draw_steps_of(c, DRUM_LANES, x, y, bg, drawn);
}
/* The strips take the screen's height (no cards on the mixer, none at their foot, no footer either: the user's
 * rulings, op_screens.c MIX): from the top the track's numeral and its M S R badges, the sound's name, the fader with
 * the meter beside it, then a row a control (the sends, DRIVE, FILTER, FX on / dry) in MIX's order, the 16 steps
 * playing, and PAN at the very foot, as a console's ("put the pan all at the bottom"; SELECT still walks VOLUME, PAN,
 * the sends...). The control SELECT is on is lit on the four strips at once (its form in the track's colour,
 * framed; the hot strip's frame white: PRESETS acts there), the others dim; "-" where the drum track has no such
 * value. The selected track's strip (ALGORITHM) is framed in its colour with its head tinted. Each strip one canvas,
 * 55 x 213 (gfx.c's canvas holds 240 x 124 pixels); on the right the master column (op_scope.c). The drum mixer
 * (op_dmixdraw.c) draws its strips with the same pieces */
#define MIX_Y 27                        /* the strips: screen rows 27..239, under the header (no footer) */
#define MIX_H 213
#define MX_W 55                         /* a strip's width, MX_X(k) its x: the master column on the right */
#define MX_X(k) (1 + 57 * (int32_t)(k))
#define MX_COL_X 229                    /* the master column: its meter and the compressor's reduction */
#define MX_COL_W 10
#define MX_FADER_Y 34                   /* in the strip: the fader and the meter (METER_H) */
#define MX_ROW_Y 128                    /* the controls' rows (PAN aside), MX_ROW_H each */
#define MX_ROW_H 10
#define MX_STEPS_Y 191                  /* the 16 steps playing */
#define MX_PAN_Y 201                    /* PAN, at the strip's foot */
static void cv_frame(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)   /* a 1 px outline */
{
    cv_rect(x, y, w, 1, c);
    cv_rect(x, y + h - 1, w, 1, c);
    cv_rect(x, y, 1, h, c);
    cv_rect(x + w - 1, y, 1, h, c);
}
/* a strip's fader (x, fw wide): the value from the bottom, lit (the control SELECT is on: framed in fr) or dim */
static void strip_fader(const cell_t *e, int32_t x, int32_t fw, uint32_t lit, uint16_t tc, uint16_t fr)
{
    uint16_t on = lit ? tc : col_shade(tc, 3u), off = lit ? col_shade(tc, 2u) : C_LINE;
    int32_t h = e->d && e->gmax > e->gmin ? (e->gv - e->gmin) * METER_H / (e->gmax - e->gmin) : 0;
    cv_rect(x, MX_FADER_Y, fw, METER_H, off);
    cv_rect(x, MX_FADER_Y + METER_H - h, fw, h, on);
    if (lit)
        cv_frame(x - 2, MX_FADER_Y - 2, fw + 4, METER_H + 4, fr);
}
/* a strip's control row at y in a strip w wide: its form, lit or dim, a dash when there is no such value; inv: the
 * pill lit while the value is 0 (FX ON: P_FXOFF is the bypass) */
static void strip_ctl(const cell_t *e, int32_t y, int32_t w, uint32_t lit, uint16_t tc, uint16_t fr, uint32_t inv)
{
    uint16_t on = lit ? tc : col_shade(tc, 3u), off = lit ? col_shade(tc, 2u) : C_LINE;
    cell_t c = *e;
    if (lit)
        cv_frame(2, y, w - 4, MX_ROW_H, fr);
    if (!c.d) {                                         /* no such value: "-" */
        cv_rect(w / 2 - (w > 20 ? 3 : 2), y + 4, w > 20 ? 6 : 4, 2, lit ? tc : C_DIM);
        return;
    }
    if (inv && c.gk == GK_PILL)
        c.gv = (int16_t)!c.gv;
    draw_gauge(w > 20 ? 4 : 3, y + 3, w - (w > 20 ? 8 : 6), 4, &c, on, off);
}
static uint32_t mix_row_now(void) { return ui.row[SCR_HOME] % NMIX; }
static void draw_strip_ctl(uint32_t c, uint32_t i, int32_t y, uint16_t tc)   /* MIX row i at y (MX_FADER_Y: the fader) */
{
    cell_t e;
    uint32_t lit = i == mix_row_now();
    uint16_t fr = ui.hot == c && ui.hot_lit ? C_WHITE : tc;   /* (the hot strip: PRESETS there) */
    mix_cell(i, c, &e);
    if (y == MX_FADER_Y)
        strip_fader(&e, 8, 14, lit, tc, fr);
    else
        strip_ctl(&e, y, MX_W, lit, tc, fr, MIX[i].id == P_FXOFF);
}
static void draw_strip(uint32_t c)
{
    uint32_t i, j = 0, cur = mix_row_now();
    uint16_t tc = trk_col(c);
    char nm[16], b[10];
    cv_begin(MX_W, MIX_H, OP_SURF);
    if (song.sel == c) {                                /* the selected track: framed, its head tinted */
        cv_rect(0, 0, MX_W, 17, col_shade(tc, 3u));
        cv_frame(0, 0, MX_W, MIX_H, tc);
    }
    cv_text(3, 3, &FONT_S, trk_tag(c), song.sel == c ? C_WHITE : tc);
    if (trk[c].p[P_MUTE])
        cv_text(26, 3, &FONT_S, "M", C_WARN);
    if ((song.solo >> c) & 1u)
        cv_text(35, 3, &FONT_S, "S", C_OK);
    if ((song.rec || rec_wait) && song.sel == c)
        cv_text(44, 3, &FONT_S, "R", C_ERR);
    snd_name(c, nm);
    cv_text(3, 19, &FONT_S, cut(b, nm, 6), MIX[cur].kind == MK_SOUND ? C_WHITE : tc);   /* (its engine's colour) */
    if (MIX[cur].kind == MK_SOUND)                      /* the SOUND row: the names lit */
        cv_frame(1, 18, MX_W - 2, 14, tc);
    for (i = 0; i < NMIX; i++) {                        /* the fader first, PAN at the foot, the rest between */
        int32_t y;
        if (MIX[i].kind != MK_TRK)
            continue;
        y = MIX[i].id == P_PAN ? MX_PAN_Y : !j ? MX_FADER_Y : MX_ROW_Y + (int32_t)(j - 1u) * MX_ROW_H;
        j += MIX[i].id != P_PAN;
        draw_strip_ctl(c, i, y, tc);
    }
    cv_blit((uint32_t)MX_X(c), MIX_Y);
}
static void master_col(uint32_t force);                /* op_scope.c: the master column */
static void draw_mixer(void)
{
    uint32_t c, i, sig = hu(hu(hu(11u, settings.palette), ui.row[SCR_HOME]), ui.hot * 2u + ui.hot_lit), redrawn = 0;
    char nm[16];
    cell_t e;
    for (c = 0; c < NTRK; c++) {
        snd_name(c, nm);
        sig = hs(hu(hu(hu(sig, trk_col(c)), (uint32_t)trk[c].p[P_MUTE] * 2u + ((song.solo >> c) & 1u)),
                    (song.sel == c) * 2u + ((song.rec || rec_wait) && song.sel == c)), nm);
        for (i = 0; i < NMIX; i++)
            if (MIX[i].kind == MK_TRK) {
                mix_cell(i, c, &e);
                sig = hs(hu(sig, (uint32_t)e.gv), e.val);
            }
    }
    if (sig != ui.sig[2]) {
        ui.sig[2] = sig;
        redrawn = 1;
        for (c = 0; c < NTRK; c++)
            draw_strip(c);
        for (c = 0; c < NTRK; c++)
            ui.meter[c] = 0xFF, ui.step_drawn[c] = 0xFF;   /* (their canvases over the strips: again) */
    }
    for (c = 0; c < NTRK; c++) {                       /* the meters */
        uint32_t h = meter_h(meter_ui_take(c)), shown = ui.meter[c] == 0xFF ? 0u : ui.meter[c];
        h = h > shown ? h : shown > 2u ? shown - 2u : 0u;   /* (falls 2 px a frame) */
        if (h != ui.meter[c]) {
            ui.meter[c] = (uint8_t)h;
            cv_begin(8, METER_H, C_LINE);
            cv_rect(0, METER_H - (int32_t)h, 8, (int32_t)h, h > METER_H - 4u ? C_ERR : C_OK);
            cv_blit((uint32_t)MX_X(c) + 28u, MIX_Y + MX_FADER_Y);
        }
    }
    for (c = 0; c < NTRK; c++)
        draw_steps(c, (uint32_t)MX_X(c) + 4u, MIX_Y + MX_STEPS_Y, OP_SURF, &ui.step_drawn[c]);
    master_col(redrawn);
}

/* ---- no footer (the user, 2026-10-08: "no footer anywhere"): every screen draws to the screen's foot. Under a
 * question the modal covers the panel's first OH_PANEL rows; the rows below it are an empty band */
static void draw_under(void)
{
    if (op_overlay() != 1u || ui.sig[3] == 1u)
        return;
    ui.sig[3] = 1u;
    cv_begin(240, 240 - (OY_PANEL + OH_PANEL), C_BLACK);
    cv_blit(0, OY_PANEL + OH_PANEL);
}

/* ---- the overlay: the modal (a question) or the toast (a result) over the panel */
static uint32_t op_overlay(void) { return op_armed() ? 1u : ui.toast_t ? 2u : 0u; }
static void draw_overlay(uint32_t ov)
{
    uint32_t sig = ov == 1u ? hs(hs(hu(hu(3u, ui.arm_danger), settings.palette), ui.arm_verb), ui.arm_arg)
                           : hs(hu(4u, ui.msg_st), ui.msg);
    if (sig == ui.sig[4])
        return;
    ui.sig[4] = sig;
    if (ov == 1u)
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
        memset(ui.meter, 0xFF, sizeof ui.meter);
        memset(ui.step_drawn, 0xFF, sizeof ui.step_drawn);
        ui.sig[3] = 0;                                  /* (the mixers' foot: the strips, or the band under a question) */
        dm_redraw();
    }
    draw_head();
    if (name_on()) {                                    /* NAME: the field and the keyboard (op_name.c), no footer */
        name_draw();
        ui.force = 0;
        return;
    }
    if (lay.shown != LY_PLAY)
        lay_draw_cards();                               /* a layer: its knobs' cards, its tiles (op_laydraw.c) */
    else if (!mix_screen(ui.scr))
        draw_cards();                                   /* (the mixers have none: their strips take the height) */
    if (ov == 1u) {
        draw_overlay(ov);                               /* the modal: the whole panel */
    } else {
        uint32_t under = ui.sig[2];
        if (lay.shown != LY_PLAY)
            lay_draw_tiles();
        else if (ui.scr == SCR_HOME)
            draw_mixer();
        else if (ui.scr == SCR_DMIX)
            dm_draw();                                  /* the drum mixer: op_dmixdraw.c */
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
