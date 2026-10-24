/* SPDX-License-Identifier: GPL-3.0-only */
/* SONG's two tables (the user, 2026-10-21: "a much more compact view of the scenes and parts ... the language we use in the
 * step sequencer"): SCENES, a table of 8 scenes a window (A-H, I-P) by 5 lanes (T1 T2 T3 DR: the pattern each track plays,
 * BARS: AUTO or the scene's length), and PARTS, a timeline of 8 parts a window by 2 lanes (the scene, and the bars: S the
 * scene's own, A the longest pattern, or a number). The rows, cells and turns underneath are op_song.c's, unchanged; this
 * file draws them as a table and gives the knobs:
 *   SCENES (the user, 2026-10-21): SELECT the cursor along the scenes, ALGORITHM the lane (T1 T2 T3 DR, BARS); KNOB 1 edits the
 *     scene under the cursor in that lane, KNOBS 2..4 the three scenes after it; PRESETS the cursor scene's BARS from any lane
 *   PARTS (the same, 2026-10-21): KNOB 1 the scene of the part under the cursor, KNOBS 2..4 the scenes of the three parts after
 *     it, PRESETS its BARS; SELECT the cursor along the parts (ALGORITHM does nothing here)
 * The PATTERNS grid is LFO's screen (op_layers.c lay_pat_peek), not SONG's. */
#define STB_COLS 8u                     /* the columns of a window */
#define STB_X0 38                       /* the first column's left, after the lanes' names */
#define STB_CW 25                       /* a column's width: 8 of them end at the screen's edge less a margin */
#define STB_PURPLE 0xB31Fu              /* the BARS lanes (a scene's, a part's) */
static uint8_t stb_lane;                /* the cursor's lane: SCENES 0..4 (T1 T2 T3 DR BARS), PARTS 0..1 (the scene, the bars) */
static struct {
    uint32_t kind, col, n, lane;        /* what stb_paint draws: the table, the cursor's column and lane, the columns there are */
    char t[5][STB_COLS][5];             /* the cells' texts, a lane a row */
    uint8_t live[STB_COLS];             /* the column's state: 1 playing, 2 queued, 3 empty scene */
} stb;

static int song_tbl_on(void)
{
    uint32_t a, k;
    if (ui.scr != SCR_SONG || name_on() || lay.shown != LY_PLAY || ui.row[SCR_SONG] >= song_rows())
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
    static const char *const SN[5] = {"T1", "T2", "T3", "DR", "BARS"}, *const PN[2] = {"SCN", "BARS"};
    for (lane = 0; lane < nl; lane++) {                 /* the lanes' names */
        const char *ln = stb.kind == SG_SCENE ? SN[lane] : PN[lane];
        const felucca_font_t *lf = stb_font(ln, STB_X0 - 5);
        y = 22 + (int32_t)lane * ph + (ph - (int32_t)lf->h) / 2;
        cv_text(3, y, lf, ln,
                lane == stb.lane ? C_WHITE : lane == (stb.kind == SG_SCENE ? 4u : 1u) ? STB_PURPLE : stb.kind == SG_SCENE ? trk_col(lane) : C_GRAY);
    }
    for (i = 0; i < STB_COLS && c0 + i < stb.n; i++) {
        int32_t x = STB_X0 + (int32_t)i * STB_CW;
        uint32_t rel = c0 + i - stb.col;                /* (PARTS: the cursor's part is KNOB 1's, the next three KNOBS 2..4's: their knob colours) */
        int kp = c0 + i >= stb.col && rel < 4u;           /* (the cursor's column and the three after it: KNOBS 1..4; SCENES: in the cursor's lane only) */
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
            uint16_t col = lane == nl - 1u ? STB_PURPLE : stb.kind == SG_SCENE ? (kp && lane == stb.lane && lane < 4u ? knob_col(rel, trk_col(lane)) : kp && lane == stb.lane ? knob_col(rel, STB_PURPLE) : trk_col(lane)) : kp ? knob_col(rel, C_HI) : C_HI;
            y = 22 + (int32_t)lane * ph;
            if ((v[0] == '-' || v[0] == 0 || (stb.live[i] == 3u && lane != nl - 1u)) && !(kp && lane == stb.lane))
                col = C_DIM;                            /* (nothing there: dim; the BARS lane stays purple) */
            if (lane == nl - 1u && c0 + i == stb.col) {   /* the current scene's / part's BARS: a purple frame (PRESETS' value) */
                cv_rect(x, y + 1, STB_CW - 2, 1, STB_PURPLE);
                cv_rect(x, y + ph - 2, STB_CW - 2, 1, STB_PURPLE);
                cv_rect(x, y + 1, 1, ph - 2, STB_PURPLE);
                cv_rect(x + STB_CW - 3, y + 1, 1, ph - 2, STB_PURPLE);
            }
            if (c0 + i == stb.col && lane == stb.lane) {
                cv_rect(x, y + 1, STB_CW - 2, 1, C_WHITE);              /* the cursor: an outline, no fill under text */
                cv_rect(x, y + ph - 2, STB_CW - 2, 1, C_WHITE);
                cv_rect(x, y + 1, 1, ph - 2, C_WHITE);
                cv_rect(x + STB_CW - 3, y + 1, 1, ph - 2, C_WHITE);
            }
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
static void song_tbl_draw(void)
{
    uint32_t a, kind = song_kind(ui.row[SCR_SONG], &a), nl = kind == SG_SCENE ? 5u : 2u, c0, i, lane;
    uint32_t sig = hu(hu(0x57B1u + kind, settings.palette + knob_colors * 256u), arrangement_clock.running * 256u + arrangement_clock.index);
    if (stb_lane >= nl || kind == SG_PART)
        stb_lane = kind == SG_PART ? 0u : (uint8_t)(nl - 1u);   /* (PARTS: the scene lane only) */
    stb.kind = kind, stb.col = a, stb.lane = stb_lane;
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
    sig = hu(hu(hu(sig, a), stb_lane), stb.n);
    if (sig == ui.sig[2])
        return;
    ui.sig[2] = sig;
    cv_tall(GL_TOP, GL_BAND, C_BLACK, stb_paint);
}
/* the knobs on a table (STEP's language): 1 when one turned */
static int song_tbl_knobs(void)
{
    uint32_t a, kind = song_kind(ui.row[SCR_SONG], &a), n = kind == SG_SCENE ? song_nscn() : arrangement.count, nl = kind == SG_SCENE ? 5u : 2u;
    int32_t s;
    int any = 0;
    if (stb_lane >= nl)
        stb_lane = (uint8_t)(nl - 1u);
    if (kind == SG_SCENE) {                             /* SCENES: SELECT the scene, ALGORITHM the lane (a track, or BARS); KNOB 1 edits the scene's value there, KNOBS 2..4 the next scenes' */
        uint32_t k;
        static const uint8_t KN[4] = {EN_K1, EN_K2, EN_K3, EN_K4};
        if ((s = panel_enc(EN_SELECT)) != 0) {
            op_row_pick((uint32_t)step_in(a, s, n));
            any = 1;
        }
        if ((s = panel_enc(EN_ALGO)) != 0) {
            stb_lane = (uint8_t)step_in(stb_lane, s, nl);
            any = 1;
        }
        for (k = 0; k < 4u; k++) {
            if ((s = panel_enc(KN[k])) == 0)
                continue;
            any = 1;
            if (a + k >= n)
                continue;
            if (stb_lane == 4u)
                song_scene_bars_turn(a + k, s);
            else if (!((sg.scn >> (a + k)) & 1u))
                ui_message("EMPTY SCENE");              /* (nothing stored: SAVE + its key stores the loop) */
            else
                song_turn(a + k, stb_lane, s, 0);                   /* (a scene's row is its index) */
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
