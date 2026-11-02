/* SPDX-License-Identifier: GPL-3.0-only */
/* SONG's two tables (the user, 2026-10-21: "a much more compact view of the scenes and parts ... the language we use in the
 * step sequencer"): SCENES, a table of 8 scenes a window (A-H, I-P) by 5 lanes (T1 T2 T3 DR: the pattern each track plays,
 * BARS: AUTO or the scene's length), and PARTS, a timeline of 8 parts a window by 2 lanes (the scene, and the bars: S the
 * scene's own, A the longest pattern, or a number). The rows, cells and turns underneath are op_song.c's, unchanged; this
 * file draws them as a table and gives the knobs:
 *   SCENES (the user, 2026-10-31: down one scene, not across): SELECT and ALGORITHM move the cursor along the scenes (the
 *     columns); KNOB 1 = T1, KNOB 2 = T2, KNOB 3 = T3, KNOB 4 = DR of the scene under the cursor; PRESETS its BARS
 *   PARTS (the same, 2026-10-21): KNOB 1 the scene of the part under the cursor, KNOBS 2..4 the scenes of the three parts after
 *     it, PRESETS its BARS; SELECT the cursor along the parts (ALGORITHM does nothing here)
 * The PATTERNS grid is LFO's screen (op_layers.c lay_pat_peek), not SONG's. */
#define STB_COLS 8u                     /* the columns of a window */
#define STB_X0 38                       /* the first column's left, after the lanes' names */
#define STB_CW 25                       /* a column's width: 8 of them end at the screen's edge less a margin */
#define STB_PURPLE 0xB31Fu              /* the BARS lanes (a scene's, a part's) */
static struct {
    uint32_t kind, col, n;              /* what stb_paint draws: the table, the cursor's column, the columns there are */
    char t[5][STB_COLS][5];             /* the cells' texts, a lane a row */
    uint8_t live[STB_COLS];             /* the column's state: 1 playing, 2 queued, 3 empty scene */
    uint8_t nx[4];                      /* per track lane: the columns (bit i) whose pattern does not exist yet (flashing red) */
    uint8_t bd_i, bd_on;                /* the queued scene's border: its column in the window, a scene is queued in it */
    uint8_t mk_i, mk_on;                /* the current scene's marker: its column in the window, drawn at all (always on the foot line, never in a lane) */
} stb;
#define STB_MK 15                       /* the marker's box (the foot line under the lanes is 16 px or more): a 13 px play triangle in it */
#define STB_TRI 13                      /* the triangle's side: 13 px tall, 13 wide */

static int song_tbl_on(void)
{
    uint32_t a, k;
    if (ui.scr != SCR_SONG || page_owned() || lay.shown != LY_PLAY || ui.row[SCR_SONG] >= song_rows())
        return 0;
    k = song_kind(ui.row[SCR_SONG], &a);
    return k == SG_SCENE || k == SG_PART;
}
/* cell (column col, lane) of the table as text: the pattern a track plays ("3", K keep, - none, S a plain section), a scene's
 * BARS (A or the number), a part's scene (its letter) and bars (S, A or the number) */
static void stb_text(uint32_t kind, uint32_t col, uint32_t lane, char *o)
{
    cell_t c;
    const char *v;
    song_cell(kind == SG_SCENE ? col : song_row_of_part(col), lane, &c);
    v = c.val;
    if (!c.label)
        v = "";
    else if (str_eq(v, "KEEP"))
        v = "K";
    else if (str_eq(v, "SEC") || str_eq(v, "SCENE"))
        v = "S";
    else if (str_eq(v, "STORED"))
        v = "ON";                                       /* (no PATTERNS: a stored scene; "STORED" is wider than a column) */
    else if (str_eq(v, "EMPTY"))
        v = "-";
    str_cpy(o, v, 5);                                   /* (AUTO: whole; stb_paint shortens it to A where the face does not fit) */
}
/* the large face (FONT_S at 2x) where the text fits its box, the small one where it does not (two-digit numbers in a column of 23 px) */
static const felucca_font_t *stb_font(const char *t, int32_t maxw)
{
    return text_w(&FONT_L, t) <= maxw ? &FONT_L : &FONT_S;
}
static void stb_paint(void)
{
    uint32_t nl = stb.kind == SG_SCENE ? 5u : 2u, lane, i, c0 = stb.col / STB_COLS * STB_COLS;
    int32_t ph = (GL_END - 22 - 16) / (int32_t)nl, y;
    char b[20];
    static const char *const SN[5] = {"T1", "T2", "T3", TRK4_TAG, "BARS"}, *const PN[2] = {"SCN", "BARS"};
    for (lane = 0; lane < nl; lane++) {                 /* the lanes' names */
        const char *ln = stb.kind == SG_SCENE ? SN[lane] : PN[lane];
        const felucca_font_t *lf = stb_font(ln, STB_X0 - 5);
        y = 22 + (int32_t)lane * ph + (ph - (int32_t)lf->h) / 2;
        cv_text(3, y, lf, ln, lane == (stb.kind == SG_SCENE ? 4u : 1u) ? STB_PURPLE : stb.kind == SG_SCENE ? trk_col(lane) : C_GRAY);
    }
    for (i = 0; i < STB_COLS && c0 + i < stb.n; i++) {
        int32_t x = STB_X0 + (int32_t)i * STB_CW;
        uint32_t rel = c0 + i - stb.col;                /* (PARTS: the cursor's part is KNOB 1's, the next three KNOBS 2..4's: their knob colours) */
        int kp = stb.kind == SG_SCENE ? c0 + i == stb.col : c0 + i >= stb.col && rel < 4u;   /* (SCENES: the cursor's column, its four lanes the KNOBS 1..4; PARTS: the cursor's part and the three after it) */
        uint16_t hc_ = stb.live[i] == 1u ? C_OK : stb.live[i] == 2u ? C_WARN : stb.live[i] == 3u ? C_DIM : kp ? knob_col(rel, C_GRAY) : C_GRAY;
        if (c0 + i == stb.col)
            hc_ = C_WHITE;
        if (stb.kind == SG_SCENE) {                     /* the column's head: the scene's letter */
            b[0] = (char)('A' + (c0 + i) % 16u), b[1] = 0;
        } else {
            fmt_int(b, (int32_t)(c0 + i) + 1);
        }
        {
            const felucca_font_t *hf = (int32_t)FONT_L.h <= 18 ? stb_font(b, STB_CW - 2) : &FONT_S;   /* (the head band is 18 rows) */
            cv_text(x + (STB_CW - text_w(hf, b)) / 2, 4, hf, b, hc_);
        }
        for (lane = 0; lane < nl; lane++) {
            const char *v = stb.t[lane][i];
            if (str_eq(v, "AUTO") && text_w(&FONT_S, v) > STB_CW - 4)
                v = "A";
            uint16_t col = lane == nl - 1u ? STB_PURPLE : stb.kind == SG_SCENE ? (kp ? knob_col(lane, trk_col(lane)) : trk_col(lane)) : kp ? knob_col(rel, C_HI) : C_HI;
            int knobcell = stb.kind == SG_SCENE && kp && lane < 4u;   /* (a cell a knob edits: outlined) */
            y = 22 + (int32_t)lane * ph;
            if ((v[0] == '-' || v[0] == 0 || (stb.live[i] == 3u && lane != nl - 1u)) && !knobcell)
                col = C_DIM;                            /* (nothing there: dim; the BARS lane stays purple) */
            if (lane == nl - 1u && c0 + i == stb.col) {   /* the current scene's / part's BARS: a purple frame (PRESETS' value) */
                cv_rect(x, y + 1, STB_CW - 2, 1, STB_PURPLE);
                cv_rect(x, y + ph - 2, STB_CW - 2, 1, STB_PURPLE);
                cv_rect(x, y + 1, 1, ph - 2, STB_PURPLE);
                cv_rect(x + STB_CW - 3, y + 1, 1, ph - 2, STB_PURPLE);
            }
            if (knobcell || (stb.kind == SG_PART && c0 + i == stb.col)) {
                uint16_t oc = knobcell ? knob_col(lane, C_WHITE) : C_WHITE;   /* the knob cells: an outline in the knob's colour, no fill under text */
                cv_rect(x, y + 1, STB_CW - 2, 1, oc);
                cv_rect(x, y + ph - 2, STB_CW - 2, 1, oc);
                cv_rect(x, y + 1, 1, ph - 2, oc);
                cv_rect(x + STB_CW - 3, y + 1, 1, ph - 2, oc);
            }
            if (!(lane < 4u && stb.kind == SG_SCENE && ((stb.nx[lane] >> i) & 1u)))   /* (a pattern not there yet: stb_nx_draw's, flashing) */
            {
                const felucca_font_t *vf = stb_font(v, STB_CW - 4);
                cv_text(x + (STB_CW - 2 - text_w(vf, v)) / 2, y + (ph - (int32_t)vf->h) / 2, vf, v, col);
            }
        }
    }
    if (stb.kind == SG_SCENE) {                         /* the foot: the window, and what the cursor is on */
        b[0] = (char)('A' + c0 % 16u), b[1] = '-', b[2] = (char)('A' + (c0 + STB_COLS - 1u) % 16u), b[3] = 0;
        cv_texts(3, GL_END - 14, b, C_GRAY);
    } else if (!stb.n) {                                /* (no part yet: how to make one) */
        cv_texts(3, GL_END - 14, "HOLD A BLACK KEY + A SCENE", C_GRAY);
    } else {
        fmt_int(b, (int32_t)c0 + 1);
        str_app(b, "-", sizeof b);
        fmt_app(b, (int32_t)(c0 + STB_COLS < stb.n ? c0 + STB_COLS : stb.n));
        str_app(b, " OF ", sizeof b);
        fmt_app(b, (int32_t)stb.n);
        cv_texts(3, GL_END - 14, b, C_GRAY);
    }
}
/* the current scene's marker (the user, 2026-11-02): a green play triangle on the foot line under the lanes (never in a lane's cell), in the column; it turns a quarter every
 * 160 ms while the transport runs and stands still when stopped. Only its own box is sent (cv_whole_w), not the table */
static uint32_t stb_mk_sig;
static uint32_t stb_mk_phase;
static void stb_mk_paint(void)
{
    int32_t dx, dy;
    uint32_t o = stb_mk_phase;
    for (dy = 0; dy < STB_TRI; dy++)
        for (dx = 0; dx < STB_TRI; dx++) {
            int32_t u = dx, v = dy, t, c = STB_TRI / 2;
            uint32_t r;
            for (r = 0; r < o; r++) {                   /* (a quarter turn about the centre, o times) */
                t = u;
                u = STB_TRI - 1 - v;
                v = t;
            }                                           /* (u, v: the point's place in the triangle pointing right) */
            if ((v > c ? v - c : c - v) * 2 <= STB_TRI - 1 - u)
                cv_rect((STB_MK - STB_TRI) / 2 + dx, (STB_MK - STB_TRI) / 2 + dy, 1, 1, C_OK);
        }
}
static void stb_marker_draw(void)
{
    int32_t x, y, ph;
    uint32_t sig;
    if (!stb.mk_on)
        return;
    stb_mk_phase = song.playing ? (fm1_ms / 160u) & 3u : 0u;
    sig = hu(stb_mk_phase + 1u, (uint32_t)stb.mk_i);
    if (sig == stb_mk_sig)
        return;
    stb_mk_sig = sig;
    ph = (GL_END - 22 - 16) / 5;
    x = STB_X0 + (int32_t)stb.mk_i * STB_CW + (STB_CW - 2 - STB_MK) / 2;
    y = 22 + 5 * ph + (GL_END - 22 - 5 * ph - STB_MK) / 2;   /* (the foot line under the five lanes, centred in its height) */
    cv_whole_w((uint32_t)x, (uint32_t)(GL_TOP + y), STB_MK, STB_MK, C_BLACK, stb_mk_paint);
}
/* a scene queued to launch (live_req): its column's border blinks, as the queued pattern's key does on the pattern page (op_keys.c:
 * (fm1_ms / 250) & 1, amber). The border runs in the gaps round the column's cells (one pixel out, 21 .. the last lane's foot); only its four
 * strips are sent (cv_whole_w), not the table */
static uint32_t stb_bd_sig;
static void stb_bd_nop(void) {}
static void stb_border_draw(void)
{
    int32_t x, y0, y1, ph;
    uint32_t on, sig;
    uint16_t c;
    if (!stb.bd_on)
        return;
    on = (fm1_ms / 250u) & 1u;
    sig = hu(on + 1u, (uint32_t)stb.bd_i);
    if (sig == stb_bd_sig)
        return;
    stb_bd_sig = sig;
    ph = (GL_END - 22 - 16) / 5;
    x = STB_X0 + (int32_t)stb.bd_i * STB_CW - 1;
    y0 = 21;
    y1 = 22 + 5 * ph - 1;                               /* (the last lane's blank bottom line) */
    c = on ? C_WARN : C_BLACK;
    cv_whole_w((uint32_t)x, (uint32_t)(GL_TOP + y0), STB_CW, 1, c, stb_bd_nop);
    cv_whole_w((uint32_t)x, (uint32_t)(GL_TOP + y1), STB_CW, 1, c, stb_bd_nop);
    cv_whole_w((uint32_t)x, (uint32_t)(GL_TOP + y0), 1, (uint32_t)(y1 - y0 + 1), c, stb_bd_nop);
    cv_whole_w((uint32_t)(x + STB_CW - 1), (uint32_t)(GL_TOP + y0), 1, (uint32_t)(y1 - y0 + 1), c, stb_bd_nop);
}
/* a pattern number that does not exist yet (a reference resting on the first free slot, or its CLONE / EMPTY question up): the number flashes
 * red (C_ERR, every 250 ms, the queued border's rate). Only the number's own box is sent (cv_whole_w) */
static uint32_t stb_nx_sig[4][STB_COLS];
static const char *stb_nx_text;
static uint16_t stb_nx_col;
static const felucca_font_t *stb_nx_font;
static void stb_nx_paint(void)
{
    if (stb_nx_col)
        cv_text(0, 0, stb_nx_font, stb_nx_text, stb_nx_col);
}
static void stb_nx_draw(void)
{
    uint32_t lane, i, on = (fm1_ms / 250u) & 1u;
    int32_t ph = (GL_END - 22 - 16) / 5;
    for (lane = 0; lane < 4u; lane++)
        for (i = 0; i < STB_COLS; i++) {
            const char *v = stb.t[lane][i];
            uint32_t sig;
            if (!((stb.nx[lane] >> i) & 1u))
                continue;
            sig = hu(on + 1u, hs(0, v));
            if (sig == stb_nx_sig[lane][i])
                continue;
            stb_nx_sig[lane][i] = sig;
            stb_nx_font = stb_font(v, STB_CW - 4);
            stb_nx_text = v;
            stb_nx_col = on ? C_ERR : 0;
            cv_whole_w((uint32_t)(STB_X0 + (int32_t)i * STB_CW + (STB_CW - 2 - text_w(stb_nx_font, v)) / 2),
                       (uint32_t)(GL_TOP + 22 + (int32_t)lane * ph + (ph - (int32_t)stb_nx_font->h) / 2),
                       (uint32_t)text_w(stb_nx_font, v), (uint32_t)stb_nx_font->h, C_BLACK, stb_nx_paint);
        }
}
static void song_tbl_draw(void)
{
    uint32_t a, kind = song_kind(ui.row[SCR_SONG], &a), nl = kind == SG_SCENE ? 5u : 2u, c0, i, lane;
    uint32_t sig = hu(hu(0x57B1u + kind, settings.palette + knob_colors * 256u), arrangement_clock.running * 256u + arrangement_clock.index);
    stb.kind = kind, stb.col = a;
    stb.n = kind == SG_SCENE ? song_nscn() : arrangement.count;
    c0 = a / STB_COLS * STB_COLS;
    memset(stb.t, 0, sizeof stb.t);
    for (i = 0; i < STB_COLS && c0 + i < stb.n; i++) {
        uint32_t col = c0 + i;
        stb.live[i] = 0;
        if (kind == SG_SCENE) {
            stb.live[i] = live_req == (int8_t)col ? 2u : live_sec == (int8_t)col && song.playing && !arrangement_clock.running ? 1u
                        : song_scene_empty(col) ? 3u : 0u;
        } else {
            stb.live[i] = arrangement_clock.running && arrangement_clock.index == col ? 1u : 0u;
        }
        for (lane = 0; lane < nl; lane++)
            stb_text(kind, col, lane, stb.t[lane][i]);
        sig = hu(hu(sig, stb.live[i]), hs(0, stb.t[0][i]) + hs(0, stb.t[1][i]) * 3u + hs(0, stb.t[2][i]) * 5u + hs(0, stb.t[3][i]) * 7u + hs(0, stb.t[4][i]) * 11u);
    }
    memset(stb.nx, 0, sizeof stb.nx);
#if FELUCCA_PATTERNS
    if (kind == SG_SCENE) {
        uint32_t k, v = 0xFFu, tk = 0xFFu, sc = 0xFFu;
        if (sg.ed_s != 0xFF && sg.ed_v < PAT_N && !pat_has(sg.ed_k, sg.ed_v))
            sc = sg.ed_s, tk = sg.ed_k, v = sg.ed_v;                    /* (the knob rests on it) */
        else if (ui.arm_scr == ARM_OP && (ui.arm_row == OA_PAT_CLONE || ui.arm_row == OA_PAT_EMPTY) && pn_scene != 0xFF)
            sc = pn_scene, tk = ui.arm_k >> 4, v = ui.arm_k & 15u;      /* (its question is up: the cell shows the number asked about) */
        if (sc >= c0 && sc < c0 + STB_COLS && tk < 4u) {
            k = sc - c0;
            fmt_int(stb.t[tk][k], (int32_t)v + 1);
            stb.nx[tk] = (uint8_t)(1u << k);
            sig = hu(sig, 0x4E00u + tk * 16u + v);
        }
    }
#endif
    sig = hu(hu(sig, a), stb.n);
    stb.bd_on = 0;
    if (kind == SG_SCENE && live_req >= 0 && (uint32_t)live_req >= c0 && (uint32_t)live_req < c0 + STB_COLS)
        stb.bd_on = 1, stb.bd_i = (uint8_t)((uint32_t)live_req - c0);
    stb.mk_on = 0;
    if (kind == SG_SCENE && live_sec >= 0 && (uint32_t)live_sec >= c0 && (uint32_t)live_sec < c0 + STB_COLS) {
        stb.mk_on = 1, stb.mk_i = (uint8_t)((uint32_t)live_sec - c0);
        sig = hu(sig, 0x4D00u + stb.mk_i);
    }
    if (sig != ui.sig[2]) {
        ui.sig[2] = sig;
        cv_tall(GL_TOP, GL_BAND, C_BLACK, stb_paint);
        stb_mk_sig = 0xFFFFFFFFu;                       /* (the band repainted without the marker: again) */
        stb_bd_sig = 0xFFFFFFFFu;
        memset(stb_nx_sig, 0, sizeof stb_nx_sig);
    }
    stb_nx_draw();
    stb_border_draw();
    stb_marker_draw();
}
/* the knobs on a table (STEP's language): 1 when one turned */
static int song_tbl_knobs(void)
{
    uint32_t a, kind = song_kind(ui.row[SCR_SONG], &a), n = kind == SG_SCENE ? song_nscn() : arrangement.count;
    int32_t s;
    int any = 0;
    if (kind == SG_SCENE) {                             /* SCENES: SELECT and ALGORITHM the scene; KNOB 1..4 = T1 T2 T3 DR of that scene, PRESETS its BARS */
        uint32_t k;
        static const uint8_t KN[4] = {EN_K1, EN_K2, EN_K3, EN_K4};
        if ((s = panel_enc(EN_SELECT)) != 0) {
            op_row_pick((uint32_t)step_in(a, s, n));
            any = 1;
        }
        if ((s = panel_enc(EN_ALGO)) != 0) {
            op_row_pick((uint32_t)step_in(a, s, n));
            any = 1;
        }
        for (k = 0; k < 4u; k++) {
            if ((s = panel_enc(KN[k])) == 0)
                continue;
            any = 1;
            if (!song_scene_make(a)) {
                if ((sg.used >> a) & 1u)
                    ui_message("EMPTY SCENE");          /* (a plain section; a failed store said why) */
            } else
                song_turn(a, k, s, 0);                              /* (a scene's row is its index, its lane the track) */
        }
        if ((s = panel_enc(EN_PRESET)) != 0) {         /* PRESETS: the current scene's BARS, from any lane */
            song_scene_bars_turn(a, s);
            any = 1;
        }
        return any;
    }
    {                                                   /* PARTS: KNOB 1 the part under the cursor's scene, KNOBS 2..4 the next parts', PRESETS its bars */
        uint32_t k;
        static const uint8_t KN[4] = {EN_K1, EN_K2, EN_K3, EN_K4};
        if ((s = panel_enc(EN_SELECT)) != 0) {
            op_row_pick(song_row_of_part((uint32_t)step_in(a, s, n)));
            any = 1;
        }
        for (k = 0; k < 4u; k++) {
            if ((s = panel_enc(KN[k])) == 0)
                continue;
            if (a + k < n)
                song_turn(song_row_of_part(a + k), 0, s, 0);
            any = 1;
        }
        if ((s = panel_enc(EN_PRESET)) != 0) {
            song_turn(ui.row[SCR_SONG], 1, s, 0);
            any = 1;
        }
    }
    return any;
}
