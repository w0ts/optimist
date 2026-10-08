/* SPDX-License-Identifier: GPL-3.0-only */
/* The renderer (docs/UI-OPTIMIST-DESIGN.md section 3), Felucca 1.0's structure in Terminus:
 *   y   0..24   the header: the track (its engine's colour) and the screen and row, the loop position, the tempo;
 *               a message (2.5 s) or the armed question ("CLEAR T2? YES") takes it over
 *   y  28..72   four cards: the cursor row's cells, KNOB 1..4 (label, value, unit; the hot cell white)
 *   y  76..198  the panel: the list of rows (the cursor row a bar in the track's colour), or the mixer's columns
 *   y 202..240  the footer: what YES and NO do here, what the keys play, the selected track's 16 steps
 * Lazy: each band remembers a signature of what it drew and is drawn again only when that changes; the meters
 * and the playheads are small canvases of their own, so a playing mixer never redraws a whole band. */
#define OY_HEAD 0
#define OH_HEAD 25
#define OY_CARD 28
#define OH_CARD 45
#define OY_PANEL 76
#define OH_PANEL 123
#define OY_FOOT 202
#define OH_FOOT 38
#define CARD_X(k) (3 + 59 * (int32_t)(k))
#define CARD_W 57
#define ROW_H 20
#define ROWS_SHOWN 6u                   /* (OH_PANEL / ROW_H) */
#define METER_H 60
#define OP_SURF RGB(26, 26, 30)         /* the cards' and columns' surface (Felucca's SURF; its tokens: phase 5) */

static uint32_t hs(uint32_t h, const char *s)          /* a signature: FNV-1a over a string */
{
    while (*s)
        h = (h ^ (uint8_t)*s++) * 16777619u;
    return h * 16777619u;
}
static uint32_t hu(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }

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
static void draw_head(void)
{
    char t[32], r[16], bpm[8];
    uint32_t sig, rec = song.rec || rec_wait || ft_on;
    uint16_t tc = trk_col(song.sel), mc;
    const char *line = 0;
    if (op_armed()) {
        str_cpy(t, ui.arm_q, sizeof t);
        str_cpy(t + str_len(t), " YES", sizeof t - str_len(t));
        line = t;
        mc = C_WARN;
    } else if (ui.msg_t) {
        line = ui.msg;
        mc = ui.msg_st ? C_STATUS[ui.msg_st & 3u] : C_HI;
    } else {
        str_cpy(t, SCR_NAME[ui.scr % SCR_N], sizeof t);
        str_cpy(t + str_len(t), " ", sizeof t - str_len(t));
        SCR->name(ui.row[ui.scr], t + str_len(t));
        mc = C_HI;
    }
    loop_pos(TSEL, r);
    fmt_int(bpm, song.g[G_BPM]);
    sig = hs(hs(hs(hu(hu(hu(1u, tc), mc), rec * 2u + song.playing), line ? line : t), r), bpm);
    sig = hu(sig, (uint32_t)(line != 0) + settings.palette * 4u);
    if (sig == ui.sig[0])
        return;
    ui.sig[0] = sig;
    cv_begin(240, OH_HEAD, C_BLACK);
    if (line) {
        cv_text(4, 4, &FONT_S, line, mc);
    } else {
        cv_rect(2, 3, 26, 19, tc);                      /* the track, in its engine's colour */
        cv_text(7, 4, &FONT_S, trk_tag(song.sel), C_BLACK);
        cv_text(34, 4, &FONT_S, cut(t, t, 16), mc);
        cv_text(236 - text_w(&FONT_S, bpm), 4, &FONT_S, bpm, C_HI);
        cv_text(236 - text_w(&FONT_S, bpm) - 8 - text_w(&FONT_S, r), 4, &FONT_S, r,
                song.playing ? C_OK : C_DIM);
        if (rec)
            cv_rect(236 - text_w(&FONT_S, bpm) - 16 - text_w(&FONT_S, r), 9, 6, 6, C_ERR);   /* recording / armed */
    }
    cv_line(0, OH_HEAD - 1, 239, OH_HEAD - 1, C_LINE);
    cv_blit(0, OY_HEAD);
}

/* ---- the cards */
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
    cv_text(x + (text_w(&FONT_S, c->label) > CARD_W - 4 ? 0 : 3), 4, &FONT_S, cut(b, c->label, 7), C_GRAY);
    if (c->kind == CK_ACT) {
        cv_text(x + 3, 24, &FONT_S, "YES", hot ? C_WHITE : C_AMB);   /* an action: YES does it */
        return;
    }
    vx = cv_text(x + 3, 24, &FONT_S, cut(b, c->val, 6), vc);
    if (c->unit[0] && vx + text_w(&FONT_S, c->unit) <= x + CARD_W - 1)
        cv_text(vx, 24, &FONT_S, c->unit, C_DIM);
}
static void draw_cards(void)
{
    cell_t c[4];
    uint32_t k, sig = hu(ui.hot * 2u + ui.hot_lit, settings.palette);
    for (k = 0; k < 4u; k++) {
        SCR->cell(ui.row[ui.scr], k, &c[k]);
        sig = hs(hs(hu(hu(sig, c[k].kind), c[k].col), c[k].label ? c[k].label : ""), c[k].val);
        sig = hs(sig, c[k].unit);
    }
    if (sig == ui.sig[1])
        return;
    ui.sig[1] = sig;
    cv_begin(240, OH_CARD, C_BLACK);
    for (k = 0; k < 4u; k++)
        draw_card(CARD_X(k), &c[k], k == ui.hot && ui.hot_lit);
    cv_blit(0, OY_CARD);
}

/* ---- the panel: the rows */
static void draw_list(void)
{
    uint32_t n = SCR->rows(), cur = ui.row[ui.scr], first = 0, i, k, sig;
    uint16_t bar = trk_col(song.sel);
    char nm[12], b[16];
    cell_t c;
    if (cur > 2u)
        first = cur - 2u;
    if (n > ROWS_SHOWN && first > n - ROWS_SHOWN)
        first = n - ROWS_SHOWN;
    sig = hu(hu(hu(hu(7u, n), cur), first), bar + settings.palette * 65536u);
    for (i = first; i < n && i < first + ROWS_SHOWN; i++) {
        SCR->name(i, nm);
        sig = hs(sig, nm);
        for (k = 0; k < 4u; k++) {
            SCR->cell(i, k, &c);
            sig = hs(hu(sig, c.kind), c.kind == CK_ACT && c.label ? c.label : c.val);
        }
    }
    if (sig == ui.sig[2])
        return;
    ui.sig[2] = sig;
    cv_begin(240, OH_PANEL, C_BLACK);
    for (i = first; i < n && i < first + ROWS_SHOWN; i++) {
        int32_t y = (int32_t)(i - first) * ROW_H;
        uint32_t on = i == cur;
        if (on)
            cv_rect(0, y, n > ROWS_SHOWN ? 236 : 240, ROW_H - 1, bar);   /* the cursor row: a bar, ink on it */
        SCR->name(i, nm);
        cv_text(4, y + 2, &FONT_S, cut(nm, nm, 9), on ? C_BLACK : C_GRAY);
        for (k = 0; k < 4u; k++) {
            uint32_t w = 4u, j;                         /* 4 characters, more over the empty cells after it */
            cell_t e;
            SCR->cell(i, k, &c);
            for (j = k + 1u; j < 4u; j++, w += 5u) {
                SCR->cell(i, j, &e);
                if (e.label || e.val[0])
                    break;
            }
            if (c.kind == CK_ACT && c.label)            /* an action: its name, dim */
                cv_text(80 + 40 * (int32_t)k, y + 2, &FONT_S, cut(b, c.label, w > 7u ? 7u : w), on ? C_BLACK : C_DIM);
            else if (c.val[0])
                cv_text(80 + 40 * (int32_t)k, y + 2, &FONT_S, cut(b, c.val, w > 13u ? 13u : w), on ? C_BLACK : C_HI);
        }
    }
    if (n > ROWS_SHOWN) {                               /* where the window is in the list */
        int32_t h = OH_PANEL * (int32_t)ROWS_SHOWN / (int32_t)n;
        cv_rect(237, 0, 3, OH_PANEL, C_LINE);
        cv_rect(237, OH_PANEL * (int32_t)first / (int32_t)n, 3, h, C_GRAY);
    }
    cv_blit(0, OY_PANEL);
}

/* ---- the panel: the mixer's columns, one under each card (Felucca's MIXER) */
static uint32_t trk_on_step(uint32_t c, uint32_t i)    /* step i of track c has something to play */
{
    const track_t *t = &trk[c];
    return is_drum(t) ? dstep_mask(&t->dstep[i % NSTEP]) != 0u : t->step[i % NSTEP].time == ST_NOTE && t->step[i % NSTEP].n;
}
static uint32_t meter_h(int32_t pk)                     /* a peak (32767 = 0 dBFS) as a height: 6 dB a step of 6 px */
{
    uint32_t lg = 0, a = pk > 0 ? (uint32_t)pk : 0u;
    while (a >>= 1)
        lg++;
    return lg < 5u ? 0u : (uint32_t)clamp((int32_t)(lg - 5u) * 6 + 6, 0, METER_H);
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
static void draw_mixer(void)
{
    uint32_t c, sig = hu(11u, settings.palette);
    char nm[16], b[10];
    for (c = 0; c < NTRK; c++) {
        snd_name(c, nm);
        sig = hs(hu(hu(hu(hu(sig, trk_col(c)), (uint32_t)(c == TRK_DRUM ? song.g[G_DRLVL] : trk[c].p[P_LEVEL])),
                       (uint32_t)trk[c].p[P_MUTE] * 2u + ((song.solo >> c) & 1u)), (song.sel == c) * 2u +
                    ((song.rec || rec_wait) && song.sel == c)), nm);
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
            cv_text(x + 3, 22, &FONT_S, cut(b, nm, 6), C_AMB);
            cv_rect(x + 8, 42, 14, METER_H, C_LINE);    /* LEVEL: the fader's position */
            cv_rect(x + 8, 42 + METER_H - lv * METER_H / 127, 14, lv * METER_H / 127, tc);
        }
        cv_blit(0, OY_PANEL);
        for (c = 0; c < NTRK; c++)
            ui.meter[c] = 0xFF, ui.step_drawn[c] = 0xFF;   /* (their canvases over the band: again) */
    }
    for (c = 0; c < NTRK; c++) {                        /* the meters: the output since the last frame */
        uint32_t h = meter_h(meter_ui_take(c)), shown = ui.meter[c] == 0xFF ? 0u : ui.meter[c];
        h = h > shown ? h : shown > 2u ? shown - 2u : 0u;   /* (falls 2 px a frame) */
        if (h != ui.meter[c]) {
            ui.meter[c] = (uint8_t)h;
            cv_begin(8, METER_H, C_LINE);
            cv_rect(0, METER_H - (int32_t)h, 8, (int32_t)h, h > METER_H - 6u ? C_ERR : C_OK);
            cv_blit((uint32_t)CARD_X(c) + 28u, OY_PANEL + 42u);
        }
    }
    for (c = 0; c < NTRK; c++)
        draw_steps(c, (uint32_t)CARD_X(c) + 4u, OY_PANEL + 110u, OP_SURF, &ui.step_drawn[c]);
}

/* ---- the footer */
static void draw_foot(void)
{
    cell_t c;
    char h[32], k[32];
    uint32_t sig, kind = MIX[ui.row[SCR_HOME] % NMIX].kind;
    SCR->cell(ui.row[ui.scr], ui.hot, &c);
    if (op_armed())
        str_cpy(h, "SAVE yes   HOME no", sizeof h);
    else if (ui.scr == SCR_HOME && (kind == MK_SOUND || kind == MK_ENTER))
        str_cpy(h, "SAVE open", sizeof h);
    else if (c.kind == CK_ACT && c.label) {
        str_cpy(h, "SAVE ", sizeof h);
        str_cpy(h + 5, c.label, sizeof h - 5);
    } else if (c.d && is_toggle(c.d) && c.kind == CK_VAL)
        str_cpy(h, "SAVE toggle", sizeof h);
    else
        h[0] = 0;
    if (ui.scr != SCR_HOME && !op_armed())
        str_cpy(h + str_len(h), h[0] ? "  HOME back" : "HOME back", sizeof h - str_len(h));
    str_cpy(k, "KEYS play ", sizeof k);
    str_cpy(k + 10, is_drum(TSEL) ? LANE_NAME[lane_selected()] : trk_tag(song.sel), sizeof k - 10);
    sig = hs(hs(hu(5u, settings.palette), h), k);
    if (sig != ui.sig[3]) {
        ui.sig[3] = sig;
        cv_begin(240, OH_FOOT, C_BLACK);
        cv_line(0, 0, 239, 0, C_LINE);
        cv_text(4, 3, &FONT_S, h, op_armed() ? C_WARN : C_GRAY);
        cv_text(4, 21, &FONT_S, k, C_DIM);
        cv_blit(0, OY_FOOT);
        ui.foot_step = 0xFF;
    }
    draw_steps(song.sel, 188u, OY_FOOT + 26u, C_BLACK, &ui.foot_step);   /* the selected track's steps */
}

static void op_frame_draw(void)
{
    if (ui.force) {
        lcd_fill(0, 0, 240, 240, C_BLACK);
        ui.sig[0] = ui.sig[1] = ui.sig[2] = ui.sig[3] = 0;
        memset(ui.meter, 0xFF, sizeof ui.meter);
        memset(ui.step_drawn, 0xFF, sizeof ui.step_drawn);
        ui.foot_step = 0xFF;
    }
    draw_head();
    draw_cards();
    if (ui.scr == SCR_HOME)
        draw_mixer();
    else
        draw_list();
    draw_foot();
    ui.force = 0;
}
