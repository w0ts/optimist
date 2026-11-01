/* SPDX-License-Identifier: GPL-3.0-only */
/* A performance layer's map (op_layers.c), the whole screen while it is held or locked: the header names it, the
 * cards are its four knobs (their forms as everywhere), the panel its 16 tiles (the white keys, 4 x 4, in the
 * instruments' colours: a drum lane in its source's, a track in its engine's), under them the layer's
 * state (the scale, the chain, a modifier key held); no footer. Sentence case as drawn (op_case). The panel to the
 * help band (or the screen's foot) in two passes (cv_tall). */
typedef struct {
    char lab[8];
    uint16_t bg, fg, top;               /* fill, text, a 3-px band at the top (0: none) */
} tile_t;
static const char *const LAY_NAME[LY_COUNT] = {"", "FX", "ERASE", "REPEAT", "", "KEY", "MIX", "SCENES", "",
                                               FIF(FELUCCA_PATTERNS)("PATTERNS")};
#define TILE_H (cards_2x2() ? (OP_PH - 20) / 4 : 36)   /* a row of tiles; 4 rows in the panel, the layer's state under them (24; 19 over the help band) */
#define LAY_SUB_Y (4 * TILE_H + 2)
static tile_t lay_tl[16];                               /* the tiles as filled last (the panel and the state under them) */

static void fm6_lay_title(char *t, uint32_t n);           /* op_fm6draw.c: ENV held on FM6 */
static void fm6_lay_draw(void);
static void lay_title(char *t, uint32_t n)
{
    if (lay.shown == LY_OPS)
        fm6_lay_title(t, n);                            /* ("FM6 OP3") */
    else
        str_cpy(t, LAY_NAME[lay.shown % LY_COUNT], n);
    if (lay.lock != LY_PLAY)
        str_app(t, " LOCKED", n);
}
/* a layer that is not the track's: its name is the header's badge (the audit's #11: "SCENES", "REPEAT", "FX") */
static const char *lay_badge(void) { return LAY_NAME[lay.shown % LY_COUNT]; }

/* ---- the cards: the layer's four knobs */
static void lay_cell(uint32_t k, cell_t *c)
{
    static const char *const LAB[LY_COUNT][4] = {
        [LY_FX] = {"FILTER", "DUST", "DUCK", "TRK FLT"}, [LY_ROLL] = {"RATE"},
        [LY_SCALE] = {"CHORD", "SCALE", "KEYS", "TRANSP"}, [LY_MIX] = {"T1", "T2", "T3", TRK4_TAG}};
    uint32_t l = lay.shown % LY_COUNT;
    int16_t *vp;
    const param_desc_t *d = lay_desc(l, k, &vp);
    cell_clear(c);
    if (ui.master && lay_master_ok(l)) {                /* MASTER: the master FX's four values, in any layer (one the */
        static const char *const LABM[4] = {"FILTER", "DUST", "DUCK", "COMP"};   /* build lacks: the frame alone) */
        if (d) {
            cell_param(c, d, vp);
            c->label = LABM[k & 3u];
        }
        return;
    }
    if (l == LY_OPS) {
        fm6_lay_cell(k, c);                             /* the FM6 page's four values (op_fm6.c) */
        return;
    }
    if (d) {
        cell_param(c, d, vp);
        c->label = LAB[l][k & 3u] ? LAB[l][k & 3u] : c->label;
        if (l == LY_MIX)
            c->col = trk_col(k);
        if (l == LY_FX && k == 3u)
            c->unit = trk_tag(song.sel);                /* (the track's FILTER beside the master's: its track in the unit's place) */
        return;
    }
    if (l == LY_ERASE) {
        const track_t *t = TSEL;
        if (k == 0u || (k == 2u && !is_drum(t))) {      /* SHIFT, TRANSPOSE: a turn moves them; no value */
            c->label = k ? "TRANSP" : "SHIFT";
            c->kind = CK_RO;
            cell_val(c, k ? "- +" : "< >");
        } else if (k == 1u) {
            c->label = "LENGTH";
            c->kind = CK_VAL;
            fmt_int(c->val, (int32_t)trk_len(t));
            cell_gauge(c, 0, 0, NSTEP, (int32_t)trk_len(t));
        }
        return;
    }
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
            cell_val(c, arrangement_enabled ? "SONG" : "LOOP");
            cell_gauge(c, 1, 0, 1, arrangement_enabled);
        } else if (k == 3u) {
            cell_val(c, srec == 2u ? "REC" : srec ? "ARMED" : "OFF");
            c->col = srec_col();
            cell_gauge(c, 1, 0, 2, srec);
        }
    }
}
/* (noinline: a call from op_frame_draw, which is smaller without it inlined) */
static __attribute__((noinline)) void lay_draw_cards(void)
{
    cell_t c[4];
    uint32_t k, sig = hu(hu(hu(0x1A7u + lay.shown, lay.hot), settings.palette + lay.used * 8u), cards_2x2());
    for (k = 0; k < 4u; k++) {
        lay_cell(k, &c[k]);
        sig = hc(sig, &c[k]);
    }
    if (sig == ui.sig[1])
        return;
    ui.sig[1] = sig;
    draw_card_band(c, lay.used ? lay.hot : 4u);         /* (2x2: op_draw.c) */
}

/* ---- the tiles */
/* a filled tile's label is its ink (black or white, gfx.c ink_on), whatever the caller passed: the fills are track
 * colours and their shades, black text was unreadable on the darker ones (the user, 2026-10-18) */
static void tile_set(tile_t *t, const char *lab, uint16_t bg, uint16_t fg, uint16_t top)
{
    str_cpy(t->lab, lab, sizeof t->lab);
    t->bg = bg, t->fg = bg != OP_SURF ? ink_on(bg) : fg, t->top = top;
}
static void tiles_keys(tile_t *tl, uint32_t erase)      /* ERASE, REPEAT: the keys' sounds, lit while held */
{
    const track_t *t = TSEL;
    uint32_t i, j, q;
    uint16_t hc_ = erase ? C_ERR : trk_col(song.sel);
    for (i = 0; i < 16u; i++) {
        uint32_t k, down, present = 0;
        char b[8];
        uint16_t col;
        if (is_drum(t) && i >= DRUM_LANES)
            continue;                                   /* (the drum track: a tile per lane, its black key) */
        k = is_drum(t) ? key_of_lane(i) : white_key(i), down = (fm1_in.notes >> k) & 1u;
        col = is_drum(t) ? lane_col(i) : trk_col(song.sel);
        if (is_drum(t)) {
            str_cpy(b, LANE_SHORT_OF(i), sizeof b);
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
        str_cpy(sub, l == LY_ROLL ? "Hold + key repeats" : song.playing ? "Erase as it plays" : "Erase every step", n);
        break;
    case LY_SCALE: {
        uint32_t root = (uint32_t)trk[0].p[P_ROOT] % 12u, mask = SCALE_MASK[ui_clamp(trk[0].p[P_SCALE], 0, NSCALES - 1)];
        for (i = 0; i < 16u; i++) {
            uint32_t pc = (53u + white_key(i)) % 12u, in = (mask >> ((pc + 12u - root) % 12u)) & 1u;
            tile_set(&tl[i], N_NOTE[pc], pc == root ? trk_col(sel) : OP_SURF, pc == root ? C_BLACK : in ? C_WHITE : C_DIM, 0);
        }
        str_cpy(sub, "KEY ", n);
        str_cpy(sub + 4, N_NOTE[root], n - 4u);
        str_app(sub, " ", n);
        str_app(sub, N_SCALE[ui_clamp(trk[0].p[P_SCALE], 0, NSCALES - 1)], n);
        break;
    }
    case LY_MIX:
        for (i = 0; i < 4u; i++) {
            int m = (int)mx_muted(MXR_T1 + i);
            str_cpy(b, "MUTE ", sizeof b);
            str_cpy(b + 5, trk_tag(i), 3);
            tile_set(&tl[i], b, m ? trk_col(i) : OP_SURF, m ? C_BLACK : C_HI, m ? 0 : trk_col(i));   /* (filled = muted; heard = outlined) */
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
        str_cpy(sub, "Mute FX", n);
#endif
        /* (no tap tempo here: PLAY held is the TEMPO page, the user 2026-11-01) */
        break;
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
        cv_box(x, y, CARD_W, TILE_H - 2, t->bg);
        if (t->top)
            cv_rect(x, y, CARD_W, 3, t->top);
        op_case(b, t->lab, sizeof b);
        cv_texts(x + (CARD_W - text_ws(b)) / 2, y + (TILE_H - 16) / 2, b, t->fg);
    }
    cv_texts(4, LAY_SUB_Y, op_case(b, lay_sub, sizeof b), C_GRAY);   /* (the key and scale, the chain...) */
}
/* (noinline: a call from op_frame_draw, which is smaller without it inlined) */
static __attribute__((noinline)) void lay_draw_tiles(void)
{
    tile_t *tl = lay_tl;
    uint32_t i, sig = hu(0x7117u, settings.palette);
    if (lay.shown == LY_OPS) {
        fm6_lay_draw();                                 /* the algorithm and the black keys' map (op_fm6draw.c) */
        return;
    }
    tiles_fill(tl, lay_sub, sizeof lay_sub);
    if (!FELUCCA_PARAM_HELP && lay_hint_t)
        str_cpy(lay_sub, "OCT- LOCKS IT", sizeof lay_sub);   /* (no help line in this build: the state line says it) */
    for (i = 0; i < 16u; i++)
        sig = hs(hu(hu(hu(sig, tl[i].bg), tl[i].fg), tl[i].top), tl[i].lab);
    sig = hs(sig, lay_sub);
    if (sig == ui.sig[2])
        return;
    ui.sig[2] = sig;
    cv_tall(OP_PY, OH_BODY, C_BLACK, lay_paint);     /* (the tiles and the state to the screen's foot) */
}
/* "Home locks it", once a power-on for each layer, for OP_MSG_FRAMES when the layer opens, on the help line (the audit's
 * #24, style guide 9: hints never in the header, which keeps the layer's badge; op_help.c hl_pre); a build without the
 * help line shows it on the state line under the tiles meanwhile (lay_draw_tiles). Gone with the layer */
static void lay_hint(void)
{
    static uint32_t told;
    if (lay.shown == LY_PLAY || lay.lock != LY_PLAY) {
        lay_hint_t = 0;                                 /* (the layer let go or locked: its hint with it) */
        return;
    }
    if (lay_hint_t)
        lay_hint_t--;
    if ((told >> lay.shown) & 1u || lay.shown == LY_SONG)   /* (SAVE's layer does not lock: SAVE then HOME is undo) */
        return;
    told |= 1u << lay.shown;
    lay_hint_t = (uint8_t)OP_MSG_FRAMES;
}
/* ---- the TEMPO page's panel: the tempo big, the beat, the nudge, the clock followed */
static uint32_t tempo_sig(void)
{
    return hu(hu(hu(hu(0x7E3u, (uint32_t)song.g[G_BPM]), song.playing ? clk_beat % 4u + 1u : 0u), (uint32_t)(clk_nudge + 64)),
              ((uint32_t)song.g[G_SYNC] * 4u + sy.src) * 4u + (uint32_t)song.g[G_MIDI]) ^
           (sy.src ? (uint32_t)(sync_ofs[sync_ofs_ix()] + 512) << 20 | sync_ofs_ix() << 30 : 0u);   /* (following: the offset) */
}
static void tempo_draw(int32_t h)
{
    char b[16];
    const char *u;
    uint32_t i, beat = song.playing ? clk_beat % 4u : 9u;
    int32_t x, bx, short_ = h < 58, dy = h > 58 ? (h - 58) / 2 : 0, bs = h >= 100 ? 28 : 16;   /* (CARDS 2x2: 40 rows, no clock
                                                         * line; SYNC is a card; taller: the same, in the middle, the beat larger) */
    fmt_int(b, song.g[G_BPM]);
    x = cv_text(8, 6 + dy, font_big(), b, clk_nudge ? C_WARN : C_WHITE);
    cv_texts(x + 6, 20 + dy, "BPM", C_GRAY);
    bx = 236 - 4 * bs - 18 < 150 ? 236 - 4 * bs - 18 : 150;   /* (the larger beat: its four boxes inside the screen) */
    for (i = 0; i < 4u; i++)                            /* the beat */
        cv_box(bx + (int32_t)i * (bs + 6), 8 + dy, bs, bs, i == beat ? (i ? C_WHITE : C_OK) : C_BLACK);
    if (sy.src) {                                       /* following a clock: the offset OCT- / + move (op_tempo.c) */
        int32_t o = sync_ofs[sync_ofs_ix()];
        x = cv_texts(bx, (short_ ? 25 : 32) + dy + bs - 16, sync_ofs_ix() == 1u ? "BT ofs " : sync_ofs_ix() == 2u ? "Link ofs " : "Ofs ", o ? C_WARN : C_GRAY);
        b[0] = o > 0 ? '+' : 0;
        b[1] = 0;
        fmt_int(b + (o > 0), o);
        x = cv_texts(x, (short_ ? 25 : 32) + dy + bs - 16, b, o ? C_WARN : C_GRAY);
        cv_texts(x, (short_ ? 25 : 32) + dy + bs - 16, " ms", o ? C_WARN : C_GRAY);
    } else if (clk_nudge)
        cv_texts(bx, (short_ ? 25 : 32) + dy + bs - 16, clk_nudge < 0 ? "Nudge -" : "Nudge +", C_WARN);
    if (short_)
        return;
    if (!value_long(&GP[G_SYNC], song.g[G_SYNC], b))    /* the clock followed: "Auto: USB" */
        param_format(&GP[G_SYNC], song.g[G_SYNC], b, &u);
    cv_texts(8, 40 + dy + bs - 16, b, C_GRAY);
    cv_box(80, 42 + dy + bs - 16, 16, 8, sy.src ? C_OK : C_BLACK);     /* RX: an external clock heard */
    cv_texts(100, 40 + dy + bs - 16, "RX", sy.src ? C_OK : C_DIM);
}

/* ---- the PATTERNS row's panel (docs/PATTERNS-DESIGN.md section 4): no cards over it (the user, 2026-11-01), the whole panel
 * from the header down: the pattern numbers 1..16 on two lines (a 2-digit number is 16 px, a column 13.6), the 4 x 16 session grid,
 * and under it the PREVIEW of the pattern last touched, read from the store without loading it (pat_peek), drawn as the
 * mixer draws a row: 4 groups of 4 steps (MX_STEP_X), shaded by level, the track's colour; the drums' lanes as rows */
#if FELUCCA_PATTERNS
#define PG_X0 21                                        /* the first cell's x: the track's tag and its knob left of it */
#define PG_CW 11                                        /* a cell's width (12 before the group gaps: 3 x MXL_GG = 6 px had to come from the row) */
#define PG_PITCH 13                                     /* a column's pitch (cell 11 + 2) */
#define PG_GG 3                                         /* the extra gap after every 4th column, here and in the preview (the mixer's MXL_GG stays 2; the user, 2026-11-01: a bit bigger) */
#define PG_X(i) (PG_X0 + (int32_t)(i) * PG_PITCH + (int32_t)((i) / 4u) * PG_GG)   /* column i's x: 4 groups of 4, the last cell ends at 236 */
#define PG_SX(i) (PG_PVX + (int32_t)(i) * PG_PITCH + (int32_t)((i) / 4u) * PG_GG)   /* the preview's step i: the same groups */
#define PG_NUMX(i, w) (PG_X(i) + (PG_CW - (w)) / 2 > 240 - (int32_t)(w) ? 240 - (int32_t)(w) : PG_X(i) + (PG_CW - (w)) / 2)   /* a number of width w over column i: centred, the last one kept inside 240 px ("16" was 225..241) */
#define PG_NUMH 11                                      /* a line of numbers */
#define PG_LANE 5                                       /* a drum lane's row in the preview */
#define PG_PVX 12                                       /* the preview's first step (215 px wide: centred) */
#define PG_PVD (PG_LANE * (int32_t)DRUM_LANES)          /* the preview's steps, drums */
#define PG_PVT 3                                        /* the preview's top margin under its rule (no label: the track and slot are known, the user, 2026-11-01) */
#define PG_PVGAP 5                                      /* the space between the last track row (DR) and the preview (the user, 2026-11-01) */
#define PG_PVH (PG_PVT + PG_PVD)                        /* the preview: the steps (the drums' lanes are the tallest) */
#define PG_SYH 24                                       /* a synth step's height in the preview */
static const uint8_t PAT_ROW_KNOB[4] = {0, 1, 2, 3};    /* the knob that cues row k: K1 T1, K2 T2, K3 T3, K4 DR (PAT_KNOB_CELL's inverse) */
/* the pattern previewed: the selected track's, the slot last touched (pv_touch), else its playing one, its queued one first */
static uint32_t pv_slot(void)
{
    uint32_t t = song.sel % NTRK;
    if (pv_t == t && pv_s < PAT_N)
        return pv_s;
    return pat_req[t] < PAT_N ? pat_req[t] : pat_cur[t];
}
static struct {
    uint32_t gen;
    uint8_t t, s, ok, has;
    pat_peek_t d;
} pvc = {.t = 0xFF};                                    /* the slot as read last: read again only when the slot or the store changed */
static const pat_peek_t *pv_peek(uint32_t t, uint32_t s, int *has)
{
    if (s >= PAT_N) {
        *has = 0;
        return 0;
    }
    if (pvc.t != t || pvc.s != s || pvc.gen != sec_gen) {
        pvc.t = (uint8_t)t, pvc.s = (uint8_t)s, pvc.gen = sec_gen;
        pvc.has = (uint8_t)pat_has(t, s);
        pvc.ok = (uint8_t)(pvc.has && pat_peek(t, s, &pvc.d));
    }
    *has = pvc.has;
    return pvc.ok ? &pvc.d : 0;
}
static uint32_t song_grid_sig(void)
{
    uint32_t k, h = 0x51u + lay.pchg * 7u + song.sel + knob_colors * 977u + settings.palette * 31u, s = pv_slot();
    for (k = 0; k < NTRK; k++)
        h = h * 31u + pat_cur[k] * 3u + pat_req[k] * 5u + (uint32_t)(lay.pmod == 5u);
    for (k = 0; k < NTRK * PAT_N; k++)
        h = h * 3u + (uint32_t)pat_has(k / PAT_N, k % PAT_N);
    return (h * 31u + s) * 31u + sec_gen;
}
static void pv_steps(uint32_t t, int32_t y, const pat_peek_t *d, uint16_t tc)
{
    uint32_t i, l;
    int32_t x;
    if (!is_drum(&trk[t])) {                            /* a synth track: one row of 16 steps, as the mixer's channels */
        for (i = 0; i < 16u; i++) {
            x = PG_SX(i);
            cv_box(x, y, 11, PG_SYH, d && ((d->hit[0] >> i) & 1u) ? ((d->mut[0] >> i) & 1u ? mute_col() : lvl_col(tc, (d->lvl[0] >> (2u * i)) & 3u)) : C_BLACK);
        }
        return;
    }
    for (l = 0; l < DRUM_LANES; l++)                    /* the drums: a row a lane, its hits (as the mixer's lanes: a dot where empty) */
        for (i = 0; i < 16u; i++) {
            x = PG_SX(i);
            if (d && ((d->hit[l] >> i) & 1u))
                cv_rect(x, y + (int32_t)l * PG_LANE, 11, PG_LANE - 1,
                        (d->mut[l] >> i) & 1u ? mute_col() : lvl_col(tc, (d->lvl[l] >> (2u * i)) & 3u));
            else
                step_dot(x, y + (int32_t)l * PG_LANE, 11, PG_LANE - 1, !(i & 3u));
        }
}
static void pv_draw(int32_t y)                          /* the preview, from y */
{
    uint32_t t = song.sel % NTRK, s = pv_slot();
    int has = 0;
    const pat_peek_t *d = pv_peek(t, s, &has);
    uint16_t tc = trk_col(t);
    cv_hline(y, C_DIM);
    if (s >= PAT_N)
        cv_texts(237 - text_ws("NONE"), y + 3, "NONE", C_DIM);
    else if (!has || !d)
        cv_texts(237 - text_ws("EMPTY"), y + 3, has ? "?" : "EMPTY", C_DIM);
    if (s < PAT_N && has && d)
        pv_steps(t, y + PG_PVT, d, tc);
}
static void song_grid_draw(int32_t h)
{
    uint32_t k, i, sel = song.sel % NTRK;
    int32_t gp = (h - 2 * PG_NUMH - PG_PVH - 8 - PG_PVGAP) / (int32_t)NTRK, ch = gp - 3;
    pat_refresh();
    for (i = 0; i < PAT_N; i++) {                       /* the pattern numbers: two lines, the odd ones over the even ones, text on the dark panel (no text on
                                                         * a fill); the selected track's playing one green, its queued amber, the one previewed white */
        char nb[4];
        uint16_t nc = pat_cur[sel] == i ? C_OK : pat_req[sel] == i ? C_WARN : pv_slot() == i ? C_WHITE : C_GRAY;
        fmt_int(nb, (int32_t)i + 1);
        cv_texts(PG_NUMX(i, text_ws(nb)), (int32_t)(i & 1u) * PG_NUMH, nb, nc);
    }
    for (k = 0; k < NTRK; k++) {
        int32_t y = 2 * PG_NUMH + 2 + (int32_t)k * gp;
        uint16_t tc = trk_col(k);
        int on = k == sel || lay.pmod == 5u;            /* (the selected track, its keys launch: lined; black 5: every track) */
        char kn[3] = {'K', (char)('1' + PAT_ROW_KNOB[k]), 0};
        if (on)
            cv_hline(y - 2, C_DIM), cv_hline(y + gp - 2, C_DIM);
        cv_texts(3, y + 1, trk_tag(k), on ? C_WHITE : tc);
        cv_texts(3, y + 1 + 11, kn, knob_col(PAT_ROW_KNOB[k], C_DIM));   /* (the knob that cues this row: its colour with KNOB COLORS) */
        for (i = 0; i < PAT_N; i++) {
            int32_t x = PG_X(i);
            uint16_t c = pat_cur[k] == i ? C_OK : pat_req[k] == i ? C_WARN : pat_has(k, i) ? col_shade(tc, 4u) : OP_SURF;
            cv_box(x, y, PG_CW, ch, c);
            if ((pat_cur[k] == i && ((lay.pchg >> k) & 1u)) || pat_live_at(k, i))
                cv_rect(x + 4, y + ch / 2 - 1, 3, 3, C_WHITE);   /* (edits not saved: the one playing changed since its source, or
                                                         * a slot keeping its live pattern, issue 142: SAVE or RELOAD) */
        }
    }
    pv_draw(2 * PG_NUMH + 2 + (int32_t)NTRK * gp + 2 + PG_PVGAP);
}
#endif
