/* SPDX-License-Identifier: GPL-3.0-only */
/* A performance layer's map (op_layers.c), the whole screen while it is held or locked: the header names it, the
 * cards are its four knobs (their forms as everywhere), the panel its 16 tiles (the white keys, 4 x 4, in the
 * instruments' colours: a drum lane in its source's, a track in its engine's), under them the layer's
 * state (the scale, the chain, a modifier key held); no footer. Sentence case as drawn (op_case). The panel to the
 * screen's foot in two passes (cv_tall). */
typedef struct {
    char lab[8];
    uint16_t bg, fg, top;               /* fill, text, a 3-px band at the top (0: none) */
} tile_t;
static const char *const LAY_NAME[LY_COUNT] = {"", "FX", "ERASE", "REPEAT", "", "KEY", "MIX", "SCENES", "",
                                               FIF(FELUCCA_PATTERNS)("PATTERNS")};
#define TILE_H 36                       /* a row of tiles; 4 rows in the panel, the layer's state under them */
#define LAY_SUB_Y 146
static tile_t lay_tl[16];                               /* the tiles as filled last (the panel and the state under them) */

static void lay_title(char *t, uint32_t n)
{
    str_cpy(t, LAY_NAME[lay.shown % LY_COUNT], n);
    if (lay.lock != LY_PLAY)
        str_cpy(t + str_len(t), " LOCKED", n - str_len(t));
}

/* ---- the cards: the layer's four knobs */
static void lay_cell(uint32_t k, cell_t *c)
{
    static const char *const LAB[LY_COUNT][4] = {
        [LY_FX] = {"FILTER", "DUST", "DUCK", "TRK FLT"}, [LY_ROLL] = {"RATE"},
        [LY_SCALE] = {"CHORD", "SCALE", "KEYS", "TRANSP"}, [LY_MIX] = {"T1", "T2", "T3", "DR"}};
    uint32_t l = lay.shown % LY_COUNT;
    int16_t *vp;
    const param_desc_t *d = lay_desc(l, k, &vp);
    cell_clear(c);
    if (d) {
        cell_param(c, d, vp);
        c->label = LAB[l][k & 3u] ? LAB[l][k & 3u] : c->label;
        if (l == LY_MIX)
            c->col = trk_col(k);
        return;
    }
    if (l == LY_ERASE) {
        const track_t *t = TSEL;
        if (k == 0u || (k == 2u && !is_drum(t))) {      /* SHIFT, TRANSPOSE: a turn moves them; no value */
            c->label = k ? "TRANSP" : "SHIFT";
            c->kind = CK_RO;
            str_cpy(c->val, k ? "- +" : "< >", sizeof c->val);
        } else if (k == 1u) {
            c->label = "LENGTH";
            c->kind = CK_VAL;
            fmt_int(c->val, (int32_t)trk_len(t));
            cell_gauge(c, 0, 0, NSTEP, (int32_t)trk_len(t));
        }
        return;
    }
#if FELUCCA_PATTERNS
    if (l == LY_PAT) {
        song_pat_cell(k, c);                            /* (as SONG's PATTERNS row: "3", "3>5") */
        return;
    }
#endif
    if (l == LY_SONG) {
#if SEC_LOGGED
        if (k < 2u) {
            prj_mem(c, k);                              /* the MEM gauge: % used, scenes that still fit */
            return;
        }
#endif
        c->kind = CK_RO;
        c->label = k == 3u ? "REC" : k == 2u ? "MODE" : "";
        if (k == 2u) {
            str_cpy(c->val, arrangement_enabled ? "SONG" : "LOOP", sizeof c->val);
            cell_gauge(c, 1, 0, 1, arrangement_enabled);
        } else if (k == 3u) {
            str_cpy(c->val, srec == 2u ? "REC" : srec ? "ARMED" : "OFF", sizeof c->val);
            c->col = srec ? C_ERR : 0;
            cell_gauge(c, 1, 0, 2, srec);
        }
    }
}
static void lay_draw_cards(void)
{
    cell_t c[4];
    uint32_t k, sig = hu(hu(0x1A7u + lay.shown, lay.hot), settings.palette + lay.used * 8u);
    for (k = 0; k < 4u; k++) {
        lay_cell(k, &c[k]);
        sig = hc(sig, &c[k]);
    }
    if (sig == ui.sig[1])
        return;
    ui.sig[1] = sig;
    cv_begin(240, OH_CARD, C_BLACK);
    for (k = 0; k < 4u; k++)
        draw_card(CARD_X(k), &c[k], k == lay.hot && lay.used);
    cv_blit(0, OY_CARD);
}

/* ---- the tiles */
static void tile_set(tile_t *t, const char *lab, uint16_t bg, uint16_t fg, uint16_t top)
{
    str_cpy(t->lab, lab, sizeof t->lab);
    t->bg = bg, t->fg = fg, t->top = top;
}
static void tiles_keys(tile_t *tl, uint32_t erase)      /* ERASE, REPEAT: the keys' sounds, lit while held */
{
    const track_t *t = TSEL;
    uint32_t i, j, q;
    uint16_t hc_ = erase ? C_ERR : trk_col(song.sel);
    for (i = 0; i < 16u; i++) {
        uint32_t k = key_of_lane(i), down = (fm1_in.notes >> k) & 1u, present = 0;
        char b[8];
        uint16_t col = is_drum(t) ? lane_col(i) : trk_col(song.sel);
        if (is_drum(t)) {
            str_cpy(b, LANE_SHORT[i], sizeof b);
            for (j = 0; j < trk_len(t); j++)
                present |= dstep_has(&t->dstep[j], i);
        } else {
            uint32_t n = kb_map(t, k);
            if (n == KB_SILENT) {
                str_cpy(b, "-", sizeof b);
            } else {
                note_name(b, n);
                for (j = 0; j < trk_len(t); j++)
                    for (q = 0; q < t->step[j].n && t->step[j].time == ST_NOTE; q++)
                        present |= t->step[j].note[q] == n;
            }
        }
        tile_set(&tl[i], b, down ? hc_ : OP_SURF, down ? C_BLACK : present ? C_WHITE : C_DIM,
                 present && !down ? col : 0);
    }
}
static void tiles_fill(tile_t *tl, char *sub, uint32_t n)
{
    static const char *const PSHORT[16] = {"LOOP 4", "LOOP 8", "LOOP16", "LOOP32", "STUTT", "REV", "STOP", "HALF",
                                           "LOW", "HIGH", "PHONE", "CRUSH", "ALIAS", "GATE", "ECHO", "WOBBLE"};
    uint32_t i, l = lay.shown, sel = song.sel % NTRK;
    char b[8];
    sub[0] = 0;
    for (i = 0; i < 16u; i++)
        tile_set(&tl[i], "", OP_SURF, C_DIM, 0);
    switch (l) {
    case LY_FX:
        for (i = 0; i < 16u; i++) {
            int on = punch.req == (int8_t)i;
            tile_set(&tl[i], PSHORT[i], on ? C_WHITE : OP_SURF, on ? C_BLACK : C_HI, on ? 0 : C_LINE);
        }
        str_cpy(sub, FELUCCA_PUNCH_LATCH ? "Key latches  OCT- off" : "Hold + key", n);
        break;
    case LY_ERASE:
    case LY_ROLL:
        tiles_keys(tl, l == LY_ERASE);
        str_cpy(sub, l == LY_ROLL ? "Hold + key repeats" : song.playing ? "As it plays  OCT undo" : "Every step  OCT undo", n);
        break;
    case LY_SCALE: {
        uint32_t root = (uint32_t)trk[0].p[P_ROOT] % 12u, mask = SCALE_MASK[clamp(trk[0].p[P_SCALE], 0, NSCALES - 1)];
        for (i = 0; i < 16u; i++) {
            uint32_t pc = (53u + key_of_lane(i)) % 12u, in = (mask >> ((pc + 12u - root) % 12u)) & 1u;
            tile_set(&tl[i], N_NOTE[pc], pc == root ? trk_col(sel) : OP_SURF, pc == root ? C_BLACK : in ? C_WHITE : C_DIM, 0);
        }
        str_cpy(sub, "KEY ", n);
        str_cpy(sub + 4, N_NOTE[root], n - 4u);
        str_cpy(sub + str_len(sub), " ", n - str_len(sub));
        str_cpy(sub + str_len(sub), N_SCALE[clamp(trk[0].p[P_SCALE], 0, NSCALES - 1)], n - str_len(sub));
        break;
    }
    case LY_MIX:
        for (i = 0; i < 4u; i++) {
            int m = trk[i].p[P_MUTE] != 0, so = (song.solo >> i) & 1u;
            str_cpy(b, "MUTE ", sizeof b);
            str_cpy(b + 5, trk_tag(i), 3);
            tile_set(&tl[i], b, m ? OP_SURF : trk_col(i), m ? C_DIM : C_BLACK, 0);
            str_cpy(b, "SOLO ", sizeof b);
            str_cpy(b + 5, trk_tag(i), 3);
            tile_set(&tl[4 + i], b, so ? C_WHITE : OP_SURF, so ? C_BLACK : C_HI, trk_col(i));
#if !FELUCCA_FILLS
            str_cpy(b, "FX ", sizeof b);
            str_cpy(b + 3, trk_tag(i), 3);
            tile_set(&tl[8 + i], b, fx_on(&trk[i]) ? col_shade(trk_col(i), 5u) : OP_SURF, fx_on(&trk[i]) ? C_BLACK : C_DIM,
                     trk_col(i));
#endif
        }
#if FELUCCA_FILLS
        tile_set(&tl[8], "FILL", fill_now ? C_WHITE : OP_SURF, fill_now ? C_BLACK : C_HI, 0);
        tile_set(&tl[9], "BAR", fill_bar_on ? C_WHITE : OP_SURF, fill_bar_on ? C_BLACK : C_HI, fill_arm ? C_WHITE : 0);
        str_cpy(sub, "Black 1-4 FX on/off", n);
#else
        str_cpy(sub, "Mute solo FX tap", n);
#endif
        fmt_int(b, song.g[G_BPM]);
        tile_set(&tl[15], b, song.playing && clk_pos < BEAT_U / 4u ? C_WHITE : C_LINE, song.playing && clk_pos < BEAT_U / 4u ? C_BLACK : C_WHITE, 0);
        tile_set(&tl[14], "TAP >", C_BLACK, C_DIM, 0);
        break;
    case LY_SONG: {
        uint32_t ready = arrangement_ready();
        for (i = 0; i < LAY_NSCN; i++) {
            int used = (ready >> i) & 1u, playing = live_sec == (int8_t)i && !arrangement_clock.running;
            b[0] = (char)('A' + i), b[1] = 0;
#if FELUCCA_PATTERNS
            if (playing && pat_scene_dirty(i))
                b[1] = '*', b[2] = 0;                   /* "B*": its patterns changed since */
#endif
            tile_set(&tl[i], b, used ? (playing ? C_OK : C_GRAY) : OP_SURF, used ? C_BLACK : C_DIM,
                     live_req == (int8_t)i ? C_WARN : 0);
        }
        str_cpy(sub, srec == 2u ? "Song rec: recording" : srec ? "Song rec: armed" : "Rec + key store", n);
#if FELUCCA_QCHAIN
        if (lay.chain_n >= 2u || chain_n) {
            uint32_t m = lay.chain_n >= 2u ? lay.chain_n : chain_n, k = 5;
            str_cpy(sub, "CHAIN", n);
            for (i = 0; i < m && i < CHAIN_MAX && k + 2u < n; i++) {
                sub[k++] = ' ';
                sub[k++] = (char)('A' + ((lay.chain_n >= 2u ? lay.chain[i] : chain_sec[i]) % 16u));
            }
            sub[k] = 0;
        }
#endif
        break;
    }
#if FELUCCA_PATTERNS
    case LY_PAT:
        pat_refresh();
        for (i = 0; i < PAT_N; i++) {
            int used = pat_has(sel, i), cur = pat_cur[sel] == i, q = pat_req[sel] == i;
            fmt_int(b, (int32_t)i + 1);
            if (cur && ((lay.pchg >> sel) & 1u))
                str_cpy(b + str_len(b), "*", 2);
            tile_set(&tl[i], b, cur ? C_OK : q ? C_WARN : used ? col_shade(trk_col(sel), 4u) : OP_SURF,
                     cur || q || used ? C_BLACK : C_DIM, used ? trk_col(sel) : 0);
        }
        str_cpy(sub, lay.pmod == 6u ? "Store: a slot" : lay.pmod == 7u ? (lay.pca == 0xFF ? "Copy: from" : "Copy: to") :
                     lay.pmod == 8u ? "Clear: a slot" : lay.pmod == 10u ? "Scene: a key" : "OCT- bar  OCT+ now", n);
        break;
#endif
    default:
        break;
    }
}
static char lay_sub[32];                                /* what lay_paint draws under the tiles: the layer's state */
static void lay_paint(void)
{
    uint32_t i;
    char b[32];
    for (i = 0; i < 16u; i++) {
        int32_t x = CARD_X(i % 4u), y = 2 + (int32_t)(i / 4u) * TILE_H;
        const tile_t *t = &lay_tl[i];
        cv_rect(x, y, CARD_W, TILE_H - 2, t->bg);
        if (t->top)
            cv_rect(x, y, CARD_W, 3, t->top);
        op_case(b, t->lab, sizeof b);
        cv_text(x + (CARD_W - text_w(&FONT_S, b)) / 2, y + (TILE_H - 16) / 2, &FONT_S, b, t->fg);
    }
    cv_text(4, LAY_SUB_Y, &FONT_S, op_case(b, lay_sub, sizeof b), C_GRAY);   /* (the key and scale, the chain...) */
}
static void lay_draw_tiles(void)
{
    tile_t *tl = lay_tl;
    uint32_t i, sig = hu(0x7117u, settings.palette);
    tiles_fill(tl, lay_sub, sizeof lay_sub);
    for (i = 0; i < 16u; i++)
        sig = hs(hu(hu(hu(sig, tl[i].bg), tl[i].fg), tl[i].top), tl[i].lab);
    sig = hs(sig, lay_sub);
    if (sig == ui.sig[2])
        return;
    ui.sig[2] = sig;
    cv_tall(OY_PANEL, OH_BODY, C_BLACK, lay_paint);     /* (the tiles and the state to the screen's foot) */
}
/* "Home locks it", once a power-on for each layer, in the header's message slot when the layer opens (the user,
 * 2026-10-08: no footer; the hint flashes once); gone with the layer */
static void lay_hint(void)
{
    static uint32_t told;
    static uint8_t saying;
    if (lay.shown == LY_PLAY || lay.lock != LY_PLAY) {
        if (saying && ui.msg_t)
            ui.msg_t = 0;                               /* (the layer let go: its hint with it) */
        saying = 0;
        return;
    }
    if ((told >> lay.shown) & 1u || lay.shown == LY_SONG)   /* (SAVE's layer does not lock: SAVE then HOME is undo) */
        return;
    told |= 1u << lay.shown;
    saying = 1;
    ui_message("HOME LOCKS IT");
}
/* ---- the TEMPO page's panel: the tempo big, the beat, the nudge, the clock followed */
static uint32_t tempo_sig(void)
{
    return hu(hu(hu(hu(0x7E3u, (uint32_t)song.g[G_BPM]), song.playing ? clk_beat % 4u + 1u : 0u), (uint32_t)(clk_nudge + 64)),
              (uint32_t)song.g[G_SYNC] * 4u + sy.src);
}
static void tempo_draw(int32_t h)
{
    char b[16];
    const char *u;
    uint32_t i, beat = song.playing ? clk_beat % 4u : 9u;
    int32_t x;
    (void)h;
    fmt_int(b, song.g[G_BPM]);
    x = cv_text(8, 6, font_big(), b, clk_nudge ? C_WARN : C_WHITE);
    cv_text(x + 6, 20, &FONT_S, "BPM", C_GRAY);
    for (i = 0; i < 4u; i++)                            /* the beat */
        cv_rect(150 + (int32_t)i * 22, 8, 16, 16, i == beat ? (i ? C_WHITE : C_OK) : C_LINE);
    if (clk_nudge)
        cv_text(150, 32, &FONT_S, clk_nudge < 0 ? "Nudge -" : "Nudge +", C_WARN);
    param_format(&GP[G_SYNC], song.g[G_SYNC], b, &u);   /* the clock followed: "A:USB" */
    cv_text(8, 40, &FONT_S, b, C_GRAY);
    cv_rect(80, 42, 16, 8, sy.src ? C_OK : C_LINE);     /* RX: an external clock heard */
    cv_text(100, 40, &FONT_S, "RX", sy.src ? C_OK : C_DIM);
}

/* ---- the PATTERNS row's panel: the 4 x 16 session grid (PATTERNS-DESIGN 6.2), h rows from the panel's top */
#if FELUCCA_PATTERNS
static uint32_t song_grid_sig(void)
{
    uint32_t k, h = 0x51u + lay.pchg * 7u + song.sel;
    for (k = 0; k < NTRK; k++)
        h = h * 31u + pat_cur[k] * 3u + pat_req[k] * 5u;
    for (k = 0; k < NTRK * PAT_N; k++)
        h = h * 3u + (uint32_t)pat_has(k / PAT_N, k % PAT_N);
    return h;
}
static void song_grid_draw(int32_t h)
{
    uint32_t k, i;
    int32_t rh = (h - 4) / (int32_t)NTRK;
    pat_refresh();
    for (k = 0; k < NTRK; k++) {
        int32_t y = 2 + (int32_t)k * rh;
        uint16_t tc = trk_col(k);
        if (k == song.sel)
            cv_rect(0, y - 1, 240, rh, C_LINE);         /* (the selected track: its keys launch) */
        cv_text(3, y + (rh - 12) / 2, &FONT_S, trk_tag(k), k == song.sel ? C_WHITE : tc);
        for (i = 0; i < PAT_N; i++) {
            int32_t x = 26 + (int32_t)i * 13;
            uint16_t c = pat_cur[k] == i ? C_OK : pat_req[k] == i ? C_WARN : pat_has(k, i) ? col_shade(tc, 4u) : OP_SURF;
            cv_rect(x, y, 11, rh - 3, c);
            if (pat_cur[k] == i && ((lay.pchg >> k) & 1u))
                cv_rect(x + 4, y + (rh - 3) / 2 - 1, 3, 3, C_WHITE);   /* (changed since its source) */
        }
    }
}
#endif
