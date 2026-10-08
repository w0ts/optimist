/* SPDX-License-Identifier: GPL-3.0-only */
/* The renderer (docs/UI-OPTIMIST-DESIGN.md section 3), Felucca 1.0's structure in Terminus:
 *   y   0..24   the header: the track (its engine's colour) and the screen and row, the loop position, the tempo;
 *               a passive message (2.5 s: MISSING, RECORDING) takes it over
 *   y  28..72   four cards: the cursor row's cells, KNOB 1..4 (label, value, unit, its form; the hot cell white)
 *   y  76..198  the panel: the list of rows (each value a number over its form; the cursor row a bar in the track's
 *               colour; on SOUND the cursor row's graph above it), or the mixer's columns; a question covers it
 *               with the modal, the result of a confirmed action shows as a toast in its middle
 *   y 202..240  the footer: what YES and NO do here, what the keys play, the selected track's 16 steps
 * Lazy: each band remembers a signature of what it drew and is drawn again only when that changes; the meters
 * and the playheads are small canvases of their own, so a playing mixer never redraws a whole band. Each band is
 * one canvas of at most 240 x 123 (gfx.c: 124 rows at most). */
#define ROW_H 20
#define ROWS_SHOWN 6u                   /* (OH_PANEL / ROW_H) */
#define METER_H 44

static uint32_t hs(uint32_t h, const char *s)          /* a signature: FNV-1a over a string */
{
    while (*s)
        h = (h ^ (uint8_t)*s++) * 16777619u;
    return h * 16777619u;
}
static uint32_t hu(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }
static uint32_t hc(uint32_t h, const cell_t *c)        /* a cell's signature: all it draws */
{
    h = hs(hs(hu(hu(hu(h, c->kind), c->col), (uint32_t)c->gk << 16 ^ (uint16_t)c->gv), c->label ? c->label : ""), c->val);
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
static void draw_head(void)
{
    char t[32], c[32], r[16], bpm[8];
    uint32_t sig, rec = song.rec || rec_wait || ft_on;
    uint16_t tc = trk_col(song.sel), mc = C_HI;
    if (ui.msg_t) {
        mc = ui.msg_st ? C_STATUS[ui.msg_st & 3u] : C_HI;
    } else {
        head_title(ui.scr, ui.row[ui.scr], t, sizeof t);
    }
    loop_pos(TSEL, r);
    fmt_int(bpm, song.g[G_BPM]);
    sig = hs(hs(hs(hu(hu(hu(1u, tc), mc), rec * 2u + song.playing), ui.msg_t ? ui.msg : t), r), bpm);
    sig = hu(sig, (uint32_t)(ui.msg_t != 0) + settings.palette * 4u);
    if (sig == ui.sig[0])
        return;
    ui.sig[0] = sig;
    cv_begin(240, OH_HEAD, C_BLACK);
    if (ui.msg_t) {
        cv_text(4, 4, &FONT_S, op_case(c, ui.msg, sizeof c), mc);   /* (sentence case: "T1 cleared") */
    } else {
        cv_rect(2, 3, 26, 19, tc);                      /* the track, in its engine's colour */
        cv_text(7, 4, &FONT_S, trk_tag(song.sel), C_BLACK);
        cv_text(34, 4, &FONT_S, op_case(c, cut(t, t, 16), sizeof c), mc);   /* "Sound ENV", "Mix master" */
        cv_text(236 - text_w(&FONT_S, bpm), 4, &FONT_S, bpm, C_HI);
        cv_text(236 - text_w(&FONT_S, bpm) - 8 - text_w(&FONT_S, r), 4, &FONT_S, r,
                song.playing ? C_OK : C_DIM);
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
static void draw_list(void)
{
    uint32_t n = SCR->rows(), cur = ui.row[ui.scr], first = 0, i, k, sig, shown = ROWS_SHOWN;
    uint16_t bar = trk_col(song.sel);
    const page_t *gp = ui.scr == SCR_SOUND && cur ? snd_page(cur) : 0;
    int32_t top = 0;
    char nm[12];
    cell_t c;
    if (gp && !has_graph(gp))
        gp = 0;
    if (gp) {
        top = GRAPH_H + 3;                              /* the graph, then three rows */
        shown = 3u;
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
    if (gp)
        sig = hu(sig, graph_sig());
    if (sig == ui.sig[2])
        return;
    ui.sig[2] = sig;
    cv_begin(240, OH_PANEL, C_BLACK);
    if (gp) {
        draw_sound_graph(gp);
        cv_line(0, GRAPH_H + 1, 239, GRAPH_H + 1, C_LINE);
    }
    for (i = first; i < n && i < first + shown; i++)
        draw_row(i, top + (int32_t)(i - first) * ROW_H, i == cur, bar, n > shown ? 236 : 240);
    if (n > shown) {                                    /* where the window is in the list */
        int32_t h = (OH_PANEL - top) * (int32_t)shown / (int32_t)n;
        cv_rect(237, top, 3, OH_PANEL - top, C_LINE);
        cv_rect(237, top + (OH_PANEL - top) * (int32_t)first / (int32_t)n, 3, h, C_GRAY);
    }
    cv_blit(0, OY_PANEL);
}

/* ---- the panel: the mixer's columns, one under each card (Felucca's MIXER) */
static uint32_t meter_h(int32_t pk)                     /* a peak (32767 = 0 dBFS) as a height: 6 dB a step of 4 px */
{
    uint32_t lg = 0, a = pk > 0 ? (uint32_t)pk : 0u;
    while (a >>= 1)
        lg++;
    return lg < 5u ? 0u : (uint32_t)clamp((int32_t)(lg - 5u) * 4 + 4, 0, METER_H);
}
/* track c's 16 steps of the page playing (the step lit in its colour, the playhead white) at x, y; *drawn: what was
 * drawn there (the step playing, or the page while stopped): again only when that changes */
static void draw_steps(uint32_t c, uint32_t x, uint32_t y, uint16_t bg, uint8_t *drawn)
{
    const track_t *t = &trk[c % NTRK];
    uint32_t len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP), at = t->seq_idx % len, page = at & ~15u, i;
    uint32_t key = song.playing ? at : 64u + (page >> 4);
    if (key == *drawn)
        return;
    *drawn = (uint8_t)key;
    cv_begin(48, 6, bg);
    for (i = 0; i < 16u; i++)
        cv_rect((int32_t)i * 3, 0, 2, 6, song.playing && page + i == at ? C_WHITE :
                page + i >= len ? bg : trk_on_step(c % NTRK, page + i) ? trk_col(c % NTRK) : C_LINE);
    cv_blit(x, y);
}
/* a strip's small forms: PAN from the centre, the three sends, FX on / dry, the FILTER from the centre */
static const uint8_t STRIP_ID[6] = {P_PAN, P_REV, P_DLY, P_CHOR, P_FXOFF,
#if FELUCCA_TRK_FILT
                                    P_TFLT
#else
                                    0xFF
#endif
};
static void strip_cell(uint32_t c, uint32_t i, cell_t *e)
{
    int16_t *vp;
    cell_clear(e);
    if (STRIP_ID[i] != 0xFFu)
        cell_param(e, mix_desc(STRIP_ID[i], c, &vp), vp);
}
static void draw_strip_forms(uint32_t c, int32_t x, uint16_t tc)
{
    cell_t e;
    uint32_t i;
    for (i = 0; i < 6u; i++) {
        static const int8_t X[6] = {4, 4, 21, 38, 4, 22}, Y[6] = {88, 95, 95, 95, 102, 103}, W[6] = {49, 15, 15, 15, 14, 31};
        strip_cell(c, i, &e);
        if (i == 4u && e.gk == GK_PILL)
            e.gv = (int16_t)!e.gv;                      /* (FX: lit while on, P_FXOFF is the bypass) */
        draw_gauge(x + X[i], Y[i], W[i], i == 4u ? 6 : 3, &e, tc, C_LINE);
    }
}
static void draw_mixer(void)
{
    uint32_t c, i, sig = hu(11u, settings.palette);
    char nm[16], b[10];
    cell_t e;
    for (c = 0; c < NTRK; c++) {
        snd_name(c, nm);
        sig = hs(hu(hu(hu(hu(sig, trk_col(c)), (uint32_t)(c == TRK_DRUM ? song.g[G_DRLVL] : trk[c].p[P_LEVEL])),
                       (uint32_t)trk[c].p[P_MUTE] * 2u + ((song.solo >> c) & 1u)), (song.sel == c) * 2u +
                    ((song.rec || rec_wait) && song.sel == c)), nm);
        for (i = 0; i < 6u; i++) {
            strip_cell(c, i, &e);
            sig = hu(sig, (uint32_t)e.gv);
        }
    }
    if (sig != ui.sig[2]) {
        ui.sig[2] = sig;
        cv_begin(240, OH_PANEL, C_BLACK);
        for (c = 0; c < NTRK; c++) {
            int32_t x = CARD_X(c), lv = c == TRK_DRUM ? song.g[G_DRLVL] : trk[c].p[P_LEVEL];
            uint16_t tc = trk_col(c);
            cv_rect(x, 0, CARD_W, OH_PANEL, OP_SURF);
            if (song.sel == c)
                cv_rect(x, 0, CARD_W, 2, C_WHITE);      /* the selected track */
            cv_text(x + 3, 4, &FONT_S, trk_tag(c), tc);
            if (trk[c].p[P_MUTE])
                cv_text(x + 27, 4, &FONT_S, "M", C_WARN);
            if ((song.solo >> c) & 1u)
                cv_text(x + 36, 4, &FONT_S, "S", C_OK);
            if ((song.rec || rec_wait) && song.sel == c)
                cv_text(x + 45, 4, &FONT_S, "R", C_ERR);
            snd_name(c, nm);
            cv_text(x + 3, 21, &FONT_S, cut(b, nm, 6), C_AMB);
            cv_rect(x + 8, 40, 14, METER_H, C_LINE);    /* LEVEL: the fader's position */
            cv_rect(x + 8, 40 + METER_H - lv * METER_H / 127, 14, lv * METER_H / 127, tc);
            draw_strip_forms(c, x, tc);
        }
        cv_blit(0, OY_PANEL);
        for (c = 0; c < NTRK; c++)
            ui.meter[c] = 0xFF, ui.step_drawn[c] = 0xFF;   /* (their canvases over the band: again) */
    }
    for (c = 0; c < NTRK && ui.overlay != 2u; c++) {   /* the meters (held under a toast, which covers two) */
        uint32_t h = meter_h(meter_ui_take(c)), shown = ui.meter[c] == 0xFF ? 0u : ui.meter[c];
        h = h > shown ? h : shown > 2u ? shown - 2u : 0u;   /* (falls 2 px a frame) */
        if (h != ui.meter[c]) {
            ui.meter[c] = (uint8_t)h;
            cv_begin(8, METER_H, C_LINE);
            cv_rect(0, METER_H - (int32_t)h, 8, (int32_t)h, h > METER_H - 4u ? C_ERR : C_OK);
            cv_blit((uint32_t)CARD_X(c) + 28u, OY_PANEL + 40u);
        }
    }
    for (c = 0; c < NTRK; c++)
        draw_steps(c, (uint32_t)CARD_X(c) + 4u, OY_PANEL + 112u, OP_SURF, &ui.step_drawn[c]);
}

/* ---- the footer */
static void draw_foot(void)
{
    cell_t c;
    char h[32], k[32], b[32];
    uint32_t sig, kind = MIX[ui.row[SCR_HOME] % NMIX].kind;
    SCR->cell(ui.row[ui.scr], ui.hot, &c);
    if (op_armed())
        h[0] = 0;                                       /* (the modal says yes / no: not twice) */
    else if (ui.scr == SCR_HOME && (kind == MK_SOUND || kind == MK_ENTER))
        str_cpy(h, "SAVE open", sizeof h);
    else if (c.kind == CK_ACT && c.label) {
        str_cpy(h, "SAVE ", sizeof h);
        str_cpy(h + 5, c.label, sizeof h - 5);
    } else if (c.d && is_toggle(c.d) && c.kind == CK_VAL)
        str_cpy(h, "SAVE toggle", sizeof h);
    else
        h[0] = 0;
    op_case(b, h, sizeof b);                            /* (each hint its own sentence: "Save load  Home back") */
    if (ui.scr != SCR_HOME && !op_armed())
        str_cpy(b + str_len(b), b[0] ? "  Home back" : "Home back", sizeof b - str_len(b));
    str_cpy(h, b, sizeof h);
    str_cpy(k, "Keys play ", sizeof k);
    str_cpy(k + 10, is_drum(TSEL) ? LANE_NAME[lane_selected()] : trk_tag(song.sel), sizeof k - 10);
    sig = hs(hs(hu(5u, settings.palette), h), k);
    if (sig != ui.sig[3]) {
        ui.sig[3] = sig;
        cv_begin(240, OH_FOOT, C_BLACK);
        cv_line(0, 0, 239, 0, C_LINE);
        cv_text(4, 3, &FONT_S, h, C_GRAY);
        cv_text(4, 21, &FONT_S, k, C_DIM);              /* "Keys play" and the lane as the kit names it */
        cv_blit(0, OY_FOOT);
        ui.foot_step = 0xFF;
    }
    draw_steps(song.sel, 188u, OY_FOOT + 26u, C_BLACK, &ui.foot_step);   /* the selected track's steps */
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
        ui.foot_step = 0xFF;
    }
    draw_head();
    draw_cards();
    if (ov == 1u) {
        draw_overlay(ov);                               /* the modal: the whole panel */
    } else {
        uint32_t under = ui.sig[2];
        if (ui.scr == SCR_HOME)
            draw_mixer();
        else
            draw_list();
        if (ov == 2u) {                                 /* the toast over the live panel: again when it redrew */
            if (ui.sig[2] != under)
                ui.sig[4] = 0;
            draw_overlay(ov);
        }
    }
    draw_foot();
    ui.force = 0;
}
