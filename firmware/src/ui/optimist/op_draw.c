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
#define ROWS_SHOWN ((uint32_t)OH_BODY / ROW_H)   /* 5 rows under the 2x2 cards (draw_list) */
#define METER_H 90                      /* meter_h's scale (op_mixdraw.c scales it to a row's meter) */
static void draw_step_panel(void);                     /* op_stepdraw.c: STEP's grid / roll */
static void step_frame(void);
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
static void gl_draw(void);                              /* op_gldraw.c: GLOBAL and SYSTEM's list */
#if FELUCCA_PARAM_HELP
static int32_t hl_room(void);                           /* op_help.c: the help line */
static void hl_pre(uint32_t ov);
static void hl_post(void);
#else
#define hl_room() 0
#define hl_pre(ov) ((void)(ov))
#define hl_post() ((void)0)
#endif
#if FELUCCA_PARAM_HELP
#define HL_NOTE(k) hl_note(k)
#define HL_TICK() hl_tick()
static void hl_note(uint32_t k);
static void hl_tick(void);
#else
#define HL_NOTE(k) ((void)(k))
#define HL_TICK() ((void)0)
#endif
#define LIST_H ((int32_t)OH_BODY - hl_room())  /* the rows' panel: over the help line on PROJECT (op_help.c) */

static uint32_t hs(uint32_t h, const char *s)          /* a signature: FNV-1a over a string */
{
    while (*s)
        h = (h ^ (uint8_t)*s++) * 16777619u;
    return h * 16777619u;
}
static uint32_t hu(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }
static uint32_t hc(uint32_t h, const cell_t *c)        /* a cell's signature: all it draws */
{
    h = hs(hs(hu(hu(hu(h, c->kind + c->mark * 16u + c->dim * 256u + c->dr * 512u), c->col), (uint32_t)c->gk << 16 ^ (uint16_t)c->gv), c->label ? c->label : ""), c->val);
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
/* which pages show the track's badge (the user, 2026-10-13: "while in pages that are not relevant for a track, we should
 * not display the track in the header"). The rule: the badge only where the page's values belong to the selected
 * track / lane. One flag per screen and per layer, so a change is one digit. The mixer's title carries the row's name
 * ("SNARE LEVELS"), SONG's PATTERNS row is every track's, SCOPE picks its own source, TEMPO's rows are the project's REC
 * settings; the MASTER pages show no track either (their title says MASTER) */
static const uint8_t HEAD_TRK_SCR[SCR_N] = {
    [SCR_HOME] = 0, [SCR_SOUND] = 1, [SCR_FX] = 0, [SCR_PROJECT] = 0, [SCR_SYSTEM] = 0,
    [SCR_STEP] = 1, [SCR_SONG] = 0,  [SCR_TEMPO] = 0, [SCR_SCOPE] = 0, [SCR_GLOBAL] = 0};
static void head_cont(char *t);
/* the page's title into t (n bytes), as drawn (sentence case, the names table's: op_names.c): the cursor row's name; on a
 * page that is not a track's (no badge) after the screen's word ("Master ins tone", "Project tools"), the row alone where
 * it starts with that word ("Project"). cont: the n/N is shown, a continuation page's number goes ("Edit 1" -> "Edit") */
static void head_title_c(uint32_t scr, uint32_t row, char *t, uint32_t n, int cont)
{
    const char *s = SCR_NAME[scr % SCR_N];
    char r[32], d[32];
    uint32_t k;
    if (scr == SCR_STEP && st.held && ui.cards_t) {     /* a step held, its cards up: their page, "LFO 5/21" (a tap shows
                                                         * no cards: the header stays the window's) */
        hp_title(r, sizeof r);
    } else if (scr == SCR_STEP && ui.cards_t) {         /* a page changed, the cards up: the page ("Pattern 1/3") */
        SCREENS[SCR_STEP].name(row, r);
    } else if (scr == SCR_STEP) {                       /* the window: "Steps 17-32" */
        uint32_t a = st.page * 16u + 1u, b = a + 15u < trk_len(TSEL) ? a + 15u : trk_len(TSEL);
        str_cpy(t, "Steps ", n);
        fmt_int(t + 6, (int32_t)a);
        str_cpy(t + str_len(t), "-", n - str_len(t));
        fmt_int(t + str_len(t), (int32_t)b);
        return;
    } else {
        SCREENS[scr % SCR_N].name(row, r);
    }
    if (cont)
        head_cont(r);
    nm_title(r, d, sizeof d);
    if (scr == SCR_HOME && row % MXR_N != MXR_MASTER) { /* the mixer: the row's name as stored and the set, "snare sends" */
        str_cpy(t, row % MXR_N >= MXR_LANE0 ? lane_short(row % MXR_N - MXR_LANE0) : trk_tag(mx_trk_of(row % MXR_N)), n);
        str_cpy(t + str_len(t), " ", n - str_len(t));
    } else if (HEAD_TRK_SCR[scr % SCR_N] || gl_scr(scr)) {   /* a track's page: the badge names the track, the title the
                                                         * page; GLOBAL, SYSTEM: the badge names the screen */
        str_cpy(t, d, n);
        return;
    } else {
        for (k = 0; s[k] && s[k] == r[k]; k++)
            ;
        if (!s[k] && (!r[k] || r[k] == ' ')) {          /* (the row starts with the screen's word: "Project") */
            str_cpy(t, d, n);
            return;
        }
        op_case(t, s, n);                               /* ("Master", "Song") */
        str_cpy(t + str_len(t), " ", n - str_len(t));
    }
    if (d[0] >= 'A' && d[0] <= 'Z' && d[1] >= 'a' && d[1] <= 'z')
        d[0] = (char)(d[0] + 32);                       /* (a word goes on the sentence: "ins tone"; "T1", "ENV" stay) */
    str_cpy(t + str_len(t), d, n - str_len(t));
}
static void head_title(uint32_t scr, uint32_t row, char *t, uint32_t n) { head_title_c(scr, row, t, n, 0); }
/* SOUND's whole list (snd_fam SND_ALL): row's place among the rows of its family (the SOUND row: EDIT's first) and their
 * count, the n/N its page button's list shows */
static void snd_fam_pos(uint32_t row, uint32_t *at, uint32_t *n)
{
    uint32_t r, rows = snd_rows(), f = row && snd_page(row) ? snd_page(row)->fam : FAM_EDIT;
    *at = *n = 0;
    for (r = 0; r < rows; r++)
        if ((r && snd_page(r) ? snd_page(r)->fam : FAM_EDIT) == f) {
            *at += r < row;
            (*n)++;
        }
}
/* where SELECT is among the pages its turns walk, " 2/4" after the title (the user, 2026-10-11: "when i page thru
 * with select (for plock or normal pages) we should display the name of the page and the 1/4 (current/total pages).
 * ideally everywhere"): a held step's card pages, STEP's pages while their cards show, the mixer's sets of the row, a
 * screen's rows (SOUND, its families, FX, PROJECT, SYSTEM, SONG). The same button's repeat walks the same pages.
 * Empty when there is one page, or the title is not a page's (STEP's window) */
static void head_pos(uint32_t scr, uint32_t row, char *p)
{
    uint32_t at, n;
    p[0] = 0;
    if (scr == SCR_STEP && st.held && ui.cards_t)
        at = st.hp, n = hp_count();
    else if (scr == SCR_STEP && !ui.cards_t)
        return;
    else if (scr == SCR_HOME)
        at = mx_set(row % MXR_N), n = mx_sets(row % MXR_N);
    else if (gl_scr(scr))                               /* GLOBAL, SYSTEM: the page (whole sections, op_global.c), within the screen */
        at = gl_pg(scr == SCR_SYSTEM, row), n = gl_npg(scr == SCR_SYSTEM);
    else if (scr == SCR_SOUND && snd_fam == SND_ALL)    /* SOUND's whole list: the row within its family, as its page */
        snd_fam_pos(row, &at, &n);                      /* button shows it (one counter a page; the titles fit: op_names.c) */
    else
        at = row, n = SCREENS[scr % SCR_N].rows();
    if (n < 2u)
        return;
    p[0] = ' ';
    fmt_int(p + 1, (int32_t)(at < n ? at : n - 1u) + 1);
    str_cpy(p + str_len(p), "/", 2);
    fmt_int(p + str_len(p), (int32_t)n);
}
/* the title t (32 bytes) and the page's n/N in w characters: the n/N always whole, the name cut before it; a name too
 * long loses the screen's word first ("Sound ENV dest 3/12" -> "ENV dest 3/12"). 1: the name was cut (never on a track
 * page: the names table, op_names.c, gives titles that fit next to the widest badge; tests/ui_optimist_names.h) */
static int head_fit(char *t, const char *pos, int32_t w)
{
    char c[32];
    int32_t k = w - (int32_t)str_len(pos), i;
    const char *nm = t;
    if ((int32_t)str_len(t) <= k) {
        str_cpy(t + str_len(t), pos, 32u - str_len(t));
        return 0;
    }
    for (i = 0; t[i] && t[i] != ' '; i++)
        ;
    if (t[i])
        nm = t + i + 1;
    cut(c, nm, (uint32_t)(k < 1 ? 1 : k > 24 ? 24 : k));
    for (k = (int32_t)str_len(c); k > 1 && c[k - 1] == ' '; k--)
        c[k - 1] = 0;
    str_cpy(c + str_len(c), pos, sizeof c - str_len(c));
    str_cpy(t, c, 32);
    return 1;                                           /* (cut: a word dropped, or the name) */
}
/* the header's badge: the selected track's engine (the drum track: DRUMS; a lane selected: its name, "SNARE"), in its
 * colour (the user, 2026-10-08: the track tag "brings no value, put the algo there"; the track is the colour and the
 * strips) */
static const char *head_engine(void)
{
    static char ln[8];
    uint32_t i;
    if (lane_picked()) {
        const char *s = lane_acc() ? DACC_NAME : LANE_SHORT[lane_selected()];   /* (the ACCENT lane: "ACCENT") */
        for (i = 0; s[i] && i < sizeof ln - 1u; i++)
            ln[i] = (char)(s[i] >= 'a' && s[i] <= 'z' ? s[i] - 32 : s[i]);
        ln[i] = 0;
        return ln;
    }
    return is_drum(TSEL) ? "DRUMS" : ENGINES[TSEL->eng_req % NENGINES]->name;
}
/* the track page's badge (the user, 2026-10-14: "we should always have the track number on the top left with the engine/
 * preset"): the track code and the engine, "2 DIGITAL", "4 DRUMS", "4 SNARE" for a picked lane, "4 ACCENT" */
static const char *head_badge(char *b, uint32_t n)
{
    fmt_int(b, (int32_t)song.sel % NTRK + 1);           /* (the number only: 1 2 3, the drum track 4) */
    str_cpy(b + str_len(b), " ", n - str_len(b));
    str_cpy(b + str_len(b), head_engine(), n - str_len(b));
    return b;
}
/* the preset follows the page's name where it fits whole (the title keeps its room first; the preset is dropped before
 * the name shortens): the track's sound as stored ("Insert 1/4  WARM PAD"); a picked lane has none, MASTER and the
 * non-track pages no badge. pr (14 bytes) is empty when none */
static void head_preset(char *pr, uint32_t badge_trk)
{
    char nm[16];
    pr[0] = 0;
    if (!badge_trk || lane_picked())
        return;
    snd_name(song.sel, nm);
    str_cpy(pr, nm, 16);                                /* (as stored: never re-cased) */
}
/* the track's name on a track change (the user, 2026-10-11): ~1.5 s in the header, on every screen, the badge "TRACK 2"
 * in the track's colour, then its engine in that colour and its preset; a synth track only (the drum track and MASTER:
 * none). Then the normal header comes back. Drawn as the header always is, by its signature: in place, no clear */
#define OP_TFLASH_FRAMES 90u
static struct {
    uint8_t t;                          /* frames the name stays */
    uint8_t sel;                        /* the track seen last */
    uint8_t seen;                       /* (the first frame: no flash at power-on) */
} tf;
static void tflash_tick(void)
{
    if (!tf.seen || song.sel != tf.sel) {
        tf.t = (uint8_t)(tf.seen && song.sel < NPART && !ui.master ? OP_TFLASH_FRAMES : 0u);
        tf.sel = song.sel;
        tf.seen = 1;
    } else if (tf.t) {
        tf.t--;
    }
    if (ui.master)
        tf.t = 0;                                       /* (MASTER picked meanwhile: its own badge) */
}
static void draw_head_flash(uint16_t tc)                /* the header while tf.t runs: TRACK 2, ANALOG, the preset */
{
    char b[16], c[32];
    int32_t x, bw;
    uint32_t room;
    const char *eng = ENGINES[TSEL->eng_req % NENGINES]->name;
    str_cpy(b, "TRACK ", sizeof b);
    fmt_int(b + 6, (int32_t)song.sel + 1);
    bw = text_w(&FONT_S, b) + 8;
    cv_rect(2, 3, bw, 19, tc);
    cv_text(6, 4, &FONT_S, b, C_BLACK);
    x = cv_text(bw + 8, 4, &FONT_S, eng, tc);
    snd_name(song.sel, c);                              /* (the preset as stored: never re-cased) */
    room = x + 8 < 228 ? (uint32_t)(236 - x - 8) / 8u : 1u;
    cv_text(x + 8, 4, &FONT_S, cut(b, c, room < 14u ? room : 14u), C_WHITE);   /* (b holds 16) */
}
/* a continuation page says its number once (the user, 2026-10-12, D11: "digits never collide with n/N"): with the n/N
 * shown, "EDIT 1 1/8" is "EDIT 2/8". An Optimist-side table: PAGES keeps SLOOP's titles. OSC 2, FLT 2, ENV2 and a
 * PART n keep theirs: they name a second unit, or a part */
static const char *const HEAD_CONT[][2] = {{"EDIT 1", "EDIT"}, {"EDIT 2", "EDIT"}, {"REC 2", "REC"}, {"ARP 2", "ARP"},
                                           {"SCL 2", "SCL"}, {"VOICE 2", "VOICE"}, {"SOUND 2", "SOUND"},
                                           {"SOUND 3", "SOUND"}, {"STEP 2", "STEP"}, {"LFO 2", "LFO"}};
static void head_cont(char *t)
{
    uint32_t i, n = str_len(t), m;
    for (i = 0; i < sizeof HEAD_CONT / sizeof HEAD_CONT[0]; i++) {
#if FELUCCA_LFO2
        if (str_eq(HEAD_CONT[i][0], "LFO 2")
#if FELUCCA_ENG_FM6
            && !ENG_IS(ENGINES[TSEL->eng_req % NENGINES], FM6)
#endif
            && !is_drum(TSEL))
            continue;                                   /* (a synth's LFO 2 is a second unit, as OSC 2: it keeps its number) */
#endif
        m = str_len(HEAD_CONT[i][0]);
        if (n >= m && str_eq(t + n - m, HEAD_CONT[i][0]) && (n == m || t[n - m - 1u] == ' ')) {
            str_cpy(t + n - m, HEAD_CONT[i][1], 32u - (n - m));
            return;
        }
    }
}
/* the title's room in pixels: right of the title's x (lead: the badge and its gap, or the bare margin), left of the loop
 * position r and the tempo bpm */
#define HEAD_PAD 2                      /* the badge: its text's inset */
#define HEAD_GAP 5                      /* the badge to the title */
#define HEAD_END 1                      /* the title's end to the REC mark (the title's room: op_names.c's titles fit it) */
static int32_t head_room(int32_t lead, const char *bpm, const char *r)
{
    return 236 - text_w(&FONT_S, bpm) - 16 - HEAD_END - (r[0] ? text_w(&FONT_S, r) : 0) - lead;
}
/* ... and per layer (HEAD_TRK_SCR above) */
static const uint8_t HEAD_TRK_LY[LY_COUNT] = {
    [LY_PLAY] = 1,      /* (the screen's flag decides) */
    [LY_FX] = 0,        /* filter, dust, duck are the song's (TRK FLT is the only track knob) */
    [LY_ERASE] = 1, [LY_ROLL] = 0, [LY_STEP] = 1, [LY_SCALE] = 1, [LY_MIX] = 0, [LY_SONG] = 0, [LY_OPS] = 1,
#if FELUCCA_PATTERNS
    [LY_PAT] = 0,       /* every track's pattern */
#endif
};
static uint32_t head_has_track(void)
{
    if (name_on())
        return 0;
    if (lay.shown != LY_PLAY)
        return HEAD_TRK_LY[lay.shown % LY_COUNT] && !ui.master;
    return HEAD_TRK_SCR[ui.scr % SCR_N] && !ui.master;
}
/* the title t (32 bytes, as drawn) with the n/N pos, cut to the room only where the names table failed (losing the
 * screen's word first); 1: cut (tests/ui_optimist_names.h: never on a page) */
static int head_finish(char *t, const char *pos, int32_t room)
{
    if (pos[0])
        return head_fit(t, pos, room > 8 ? room / 8 : 1);
    return (int32_t)str_len(t) * 8 > room;
}
/* the header as drawn (draw_head; the tests read it): the badge, the title and its n/N fitted, the preset, the loop
 * position and the tempo, their places */
typedef struct {
    char t[32], r[16], bpm[8], pos[12], bt[24], pr[16];
    const char *eng;                    /* the badge's text, "" none */
    uint16_t tc, mc;                    /* the badge's colour, the title's */
    uint8_t badge, cut;                 /* a badge drawn; the title cut to its room (never, on a track page) */
    int32_t bw, tx, room;               /* the badge's width, the title's x and room (px) */
} head_t;
static void head_make(head_t *h)
{
    const char *eng0 = head_engine();
    uint32_t trkb;
    h->tc = lane_picked() ? op_lane_col(lane_selected()) : trk_col(song.sel);
    h->mc = C_HI;
    h->eng = eng0;
    if (ui.scr == SCR_FX || ui.master                   /* MASTER: its badge in C_HI, as the mixer's M row; the mixer's */
        || (lay.shown == LY_PLAY && mx_on_master())) {  /* MASTER row too (no engine) */
        h->eng = "MASTER";
        h->tc = C_HI;
    } else if ((ui.scr == SCR_GLOBAL || ui.scr == SCR_SYSTEM) && lay.shown == LY_PLAY) {   /* GLOBAL, SYSTEM: the badge names the screen (one GLO cycle) */
        h->eng = SCR_NAME[ui.scr];
        h->tc = C_HI;
    }
    h->badge = (uint8_t)(head_has_track() || ui.msg_t);
    trkb = h->badge && !ui.msg_t && h->eng == eng0;
    if (!h->badge)
        h->eng = "";                                    /* (no badge: the title starts at the left margin) */
    else if (trkb)
        h->eng = head_badge(h->bt, sizeof h->bt);       /* (a track page: "2 DIGITAL") */
    head_preset(h->pr, trkb);
    h->bw = h->badge ? text_w(&FONT_S, h->eng) + HEAD_PAD * 2 : 0;
    h->pos[0] = 0;
    h->t[0] = 0;
    if (ui.msg_t) {
        h->mc = ui.msg_st ? C_STATUS[ui.msg_st & 3u] : C_HI;
    } else if (name_on()) {
        name_title(h->t, sizeof h->t);                  /* NAME: "Name U03", "Name project 3" */
    } else if (lay.shown != LY_PLAY) {
        lay_title(h->t, sizeof h->t);                   /* a layer: its name ("Scenes", "FX locked") */
    } else {
        head_pos(ui.scr, ui.row[ui.scr], h->pos);
        head_title_c(ui.scr, ui.row[ui.scr], h->t, sizeof h->t, h->pos[0] != 0);
        if (gl_scr(ui.scr))                             /* GLOBAL, SYSTEM: the screen's name alone (the page's name and n/N are */
            op_case(h->t, SCR_NAME[ui.scr], sizeof h->t), h->pos[0] = 0;   /* the list's own title, op_gldraw.c) */
    }
    loop_pos(TSEL, h->r);
    if (ui.scr == SCR_STEP)
        h->r[0] = 0;                                    /* (STEP: the grid shows the playhead; the room is the pick's) */
    fmt_int(h->bpm, song.g[G_BPM]);
    h->tx = h->badge ? 2 + h->bw + HEAD_GAP : 6;        /* the title's room, left of the position and the tempo */
    h->room = head_room(h->tx, h->bpm, h->r);
    h->cut = (uint8_t)head_finish(h->t, h->pos, h->room);
    if (!ui.msg_t && lay.shown == LY_PLAY && ui.scr == SCR_STEP && !ui.cards_t && !lane_picked()) {   /* STEP: the window,
                                                         * then the pick: the lane's short name as the kit names it
                                                         * ("Steps 1-16 o.hat"), a synth's notes ("Steps 1-16 C4 E4+");
                                                         * a picked lane: the badge names it ("4 SNARE") */
        char c[16];
        if (is_drum(TSEL))
            str_cpy(c, lane_short(lane_selected()), sizeof c);
        else
            step_pick_name(c, sizeof c);
        str_cpy(h->t + str_len(h->t), " ", sizeof h->t - str_len(h->t));
        str_cpy(h->t + str_len(h->t), c, sizeof h->t - str_len(h->t));
    }
}
static void draw_head(void)
{
    head_t h;
    char c[32];
    uint32_t sig, rec = song.rec || rec_wait || ft_on;
    int32_t tw, bx;
    head_make(&h);
    bx = 236 - text_w(&FONT_S, h.bpm);
    sig = hs(hs(hs(hs(hu(hu(hu(1u, h.tc), h.mc), rec * 2u + song.playing), ui.msg_t ? ui.msg : h.t), h.r), h.bpm), h.eng);
    sig = hs(sig, h.pr);
    sig = hu(sig, (uint32_t)(ui.msg_t != 0) + settings.palette * 4u);
    tflash_tick();
    if (tf.t && !ui.msg_t && !name_on()) {
        snd_name(song.sel, c);
        sig = hs(hu(hu(sig, 0x7F1u), TSEL->eng_req), c);
    }
    if (sig == ui.sig[0])
        return;
    ui.sig[0] = sig;
    cv_begin(240, OH_HEAD, C_BLACK);
    if (tf.t && !ui.msg_t && !name_on()) {
        draw_head_flash(h.tc);
    } else if (ui.msg_t) {
        cv_text(4, 4, &FONT_S, op_case(c, ui.msg, sizeof c), h.mc);   /* (sentence case: "T1 cleared") */
    } else {
        if (h.badge) {
            cv_rect(2, 3, h.bw, 19, h.tc);              /* the track's engine, in its colour */
            cv_text(2 + HEAD_PAD, 4, &FONT_S, h.eng, C_BLACK);
        }
        tw = cv_text(h.tx, 4, &FONT_S, cut(c, h.t, h.room > 8 ? (uint32_t)h.room / 8u : 1u), h.mc);   /* (fitted above) */
        if (h.pr[0] && tw + 8 + text_w(&FONT_S, h.pr) <= h.tx + h.room)   /* (the preset, if it fits whole) */
            cv_text(tw + 8, 4, &FONT_S, h.pr, h.tc);
        cv_text(bx, 4, &FONT_S, h.bpm, C_HI);
        if (h.r[0])
            cv_text(bx - 8 - text_w(&FONT_S, h.r), 4, &FONT_S, h.r, song.playing ? C_OK : C_DIM);
        if (rec)
            cv_rect(bx - 16 - text_w(&FONT_S, h.r), 9, 6, 6, C_ERR);   /* recording / armed */
    }
    cv_line(0, OH_HEAD - 1, 239, OH_HEAD - 1, C_LINE);
    cv_blit(0, OY_HEAD);
}

/* ---- the cards: label, value and unit, the value's form under them */
/* the actual value's bar (op_effval.c: the user, 2026-10-12, "one number, two bars"): 2 px, white (amber on the hot
 * card, whose entered bar is white), no trough; in a 1x4 card under the entered bar (both 2 px), in a big card 3 px
 * over it (below the digits' foot) */
#define EFFV_Y1 41                      /* a 1x4 card's: y (the entered bar at 37) */
#define EFFV_Y2 (BIG_H - 6)             /* a big card's, from its top */
static void effv_bar(int32_t x, int32_t y, int32_t w, const cell_t *c, uint32_t hot)
{
    cell_t a = *c;
    a.gk = c->ak, a.gmin = c->amin, a.gmax = c->amax, a.gv = c->av;
    draw_gauge(x, y, w, 2, &a, hot ? C_AMB : C_WHITE, OP_SURF);
}
/* an action's card (the user, 2026-10-13: the value is the verb, never "YES"): the verb (val, else the label), its object
 * small above (obj, else the label over a val), destructive ones in the alarm colour; YES does it, and asks first */
static const char *act_verb(const cell_t *c) { return c->val[0] ? c->val : c->label; }
static const char *act_small(const cell_t *c) { return c->obj ? c->obj : c->val[0] ? c->label : ""; }
static uint16_t act_col(const cell_t *c, uint32_t hot) { return c->danger ? C_ERR : hot ? C_WHITE : C_AMB; }
/* a card's label as drawn: the action's object, else the label; the names table's card form (capitals, 6 at most) */
static const char *card_label(const cell_t *c, char *b, uint32_t n)
{
    const char *eng = (ui.scr == SCR_SOUND || ui.scr == SCR_STEP) && !is_drum(TSEL) ? ENGINES[TSEL->eng_req % NENGINES]->name : 0;
    return nm_card(c->kind == CK_ACT ? act_small(c) : c->label, eng, b, n);
}
static void draw_card(uint32_t k, int32_t x, const cell_t *c, uint32_t hot)
{
    char b[16];
    uint16_t vc = c->dim ? C_DIM : hot ? C_WHITE : c->kind == CK_RO ? (c->col ? c->col : C_AMB) : knob_col(k, c->col ? c->col : C_HI);   /* (KNOB COLORS: knob k's) */
    int32_t vx;
    cv_box(x, 0, CARD_W, OH_CARD - 1, OP_SURF);
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
    if (c->dr)                                          /* the drum track's own value, a lane picked: tagged DR */
        cv_text(x + CARD_W - 3 - text_w(&FONT_S, "DR"), 3, &FONT_S, "DR", C_AMB);
    {
        const char *lab = card_label(c, b, sizeof b);   /* (the names table's: in capitals, 6 at most) */
        cv_text(x + (text_w(&FONT_S, lab) > CARD_W - 4 ? 0 : 3), 3, &FONT_S, lab, knob_col(k, C_GRAY));
    }
    if (c->kind == CK_ACT) {
        cv_text(x + 3, 20, &FONT_S, cut(b, act_verb(c), 6), act_col(c, hot));
        return;
    }
    vx = cv_text(x + 3, 20, &FONT_S, cut(b, c->val, 6), vc);
    if (c->unit[0] && vx + text_w(&FONT_S, c->unit) <= x + CARD_W - 1)
        cv_text(vx, 20, &FONT_S, c->unit, C_DIM);
    if (c->ak) {                                        /* two bars: entered, then the actual value (op_effval.c) */
        draw_gauge(x + 4, EFFV_Y1 - 4, CARD_W - 8, 2, c, vc, C_LINE);
        effv_bar(x + 4, EFFV_Y1, CARD_W - 8, c, hot);
    } else {
        draw_gauge(x + 4, 38, CARD_W - 8, 4, c, vc, C_LINE);
    }
}
/* the cards 2x2 (the user, 2026-10-08; SLOOP 2.4's big values, ui/sloop/ui_draw.c graph_big's layout, copied): value k
 * in large type in a 2 x 2 block placed as the knobs sit (KNOB 1 top left, 2 top right, 3 bottom left, 4 bottom
 * right), its label and unit small above it, its form under it; the hot one white. A value too wide for the large
 * face (a name) is drawn in the small one */
#define BIG_W 116
#define BIG_H 46
static void draw_big(uint32_t k, const cell_t *c, uint32_t hot)
{
    char b[16];
    int32_t x = k & 1u ? 121 : 3, y = k & 2u ? BIG_H + 1 : 0, uw = c->unit[0] ? text_w(&FONT_S, c->unit) + 4 : 0;
    int32_t dw = c->dr ? text_w(&FONT_S, "DR") + 4 : 0;   /* (the DR tag's room, top right) */
    uint16_t vc = c->dim ? C_DIM : hot ? C_WHITE : c->kind == CK_RO ? (c->col ? c->col : C_AMB) : knob_col(k, c->col ? c->col : C_HI);   /* (KNOB COLORS) */
    cv_box(x, y, BIG_W, BIG_H, OP_SURF);
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
    uw += dw;
    if (c->dr)                                          /* the drum track's own value, a lane picked: tagged DR */
        cv_text(x + BIG_W - 3 - text_w(&FONT_S, "DR"), y + 2, &FONT_S, "DR", C_AMB);
    {
        char l[16];
        cv_text(x + 5, y + 2, &FONT_S, cut(b, card_label(c, l, sizeof l), (uint32_t)(BIG_W - 10 - uw) / 8u), knob_col(k, C_GRAY));
    }
    if (c->unit[0])
        cv_text(x + BIG_W - 3 - text_w(&FONT_S, c->unit) - (c->mark ? 10 : 0) - dw, y + 2, &FONT_S, c->unit, C_DIM);
    if (c->kind == CK_ACT) {
        const char *v = act_verb(c);
        if (text_w(font_big(), v) <= BIG_W - 10)
            cv_text(x + 5, y + 13, font_big(), v, act_col(c, hot));
        else if (text_w(font_big(), v) <= BIG_W - 4)   /* (a 7-letter verb, REVERSE: large too, from the card's edge) */
            cv_text(x + 2, y + 13, font_big(), v, act_col(c, hot));
        else
            cv_text(x + 5, y + 20, &FONT_S, cut(b, v, 13), act_col(c, hot));   /* (CALIBRATE: the small face) */
        return;
    }
    if (text_w(font_big(), c->val) <= BIG_W - 10)
        cv_text(x + 5, y + 13, font_big(), c->val, vc);
    else
        cv_text(x + 5, y + 20, &FONT_S, cut(b, c->val, 13), vc);
    draw_gauge(x + 5, y + BIG_H - 3, BIG_W - 10, 2, c, vc, C_LINE);
    if (c->ak)                                          /* the actual value's bar, over the entered one (op_effval.c) */
        effv_bar(x + 5, y + EFFV_Y2, BIG_W - 10, c, hot);
}
/* the cards' band: the four cells c, hot the one white (4: none), 2x2 but on the mixer (op_layers.c cards_2x2) */
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
/* the actual values' bars (op_effval.c): computed every frame for the four cards shown; the band is drawn again only when
 * what it shows besides them changes (a bar appearing or going is such a change), a moving bar alone is its own 2 px
 * strip, at most every EFFV_MS (~30 Hz). A bar whose value comes back to the entered one stays EFFV_LINGER_MS, so an LFO
 * crossing it does not make the band blink */
#define EFFV_MS 33u
#define EFFV_LINGER_MS 400u
static struct {
    int16_t *vp[4];                                     /* the value whose bar shows (the cell's) */
    uint32_t t_on[4];                                   /* when it last differed (fm1_ms) */
    int16_t av[4];                                      /* the bars as drawn */
    uint32_t t;                                         /* the last strips drawn */
} ev;
static void effv_take(uint32_t k, cell_t *c)
{
    uint32_t r = effv_cell(c);
    if (r == 1u)
        ev.t_on[k] = fm1_ms, ev.vp[k] = c->vp;
    else if (r == 2u && !(ev.vp[k] == c->vp && fm1_ms - ev.t_on[k] < EFFV_LINGER_MS))
        c->ak = GK_NONE;
    if (!r)
        ev.vp[k] = 0;
}
static void effv_strips(const cell_t *c, uint32_t hot)
{
    uint32_t k;
    if (fm1_ms - ev.t < EFFV_MS)
        return;
    for (k = 0; k < 4u; k++) {
        int32_t x = CARD_X(k) + 4, y = EFFV_Y1, w = CARD_W - 8;
        if (!c[k].ak || c[k].av == ev.av[k])
            continue;
        if (cards_2x2())
            x = (k & 1u ? 121 : 3) + 5, y = (k & 2u ? BIG_H + 1 : 0) + EFFV_Y2, w = BIG_W - 10;
        cv_begin((uint32_t)w, 2, OP_SURF);
        effv_bar(0, 0, w, &c[k], k == hot);
        cv_blit((uint32_t)x, (uint32_t)(OY_CARD + y));
        ev.av[k] = c[k].av;
    }
    ev.t = fm1_ms;
}
static void draw_cards(void)
{
    static cell_t snap[4];                              /* the cells last drawn (STEP: what a step let go still shows) */
    cell_t c[4];
    uint32_t k, sig = hu(hu(ui.hot * 2u + ui.hot_lit, settings.palette), cards_2x2() + knob_colors * 2u);
    for (k = 0; k < 4u; k++) {
        if (cards_snap_use && ui.scr == SCR_STEP) {     /* (the snapshot is STEP's only: elsewhere a page's own cells) */
            c[k] = snap[k];
        } else {
            SCR->cell(ui.row[ui.scr], k, &c[k]);
            effv_take(k, &c[k]);                        /* (right after the cell: a lane value's dsv is the cell's) */
        }
        snap[k] = c[k];
        sig = hu(hc(sig, &c[k]), (c[k].ak | (uint32_t)(uint16_t)c[k].amin << 4) ^ (uint32_t)(uint16_t)c[k].amax << 16);
    }
    if (sig == ui.sig[1]) {
        effv_strips(c, ui.hot_lit ? ui.hot : 4u);
        return;
    }
    ui.sig[1] = sig;
    draw_card_band(c, ui.hot_lit ? ui.hot : 4u);
    for (k = 0; k < 4u; k++)
        ev.av[k] = c[k].av;
    ev.t = fm1_ms;
}

/* ---- the panel: the rows, each value a number over its form; SOUND: the cursor row's graph on top */
/* a row rh px high (ROW_H, or taller when a page's few rows fill the panel: its text and gauge stay together, in the
 * middle of the row, the gauge thicker) */
/* cell k of row i (c) as the list shows it, into b (n bytes): 4 characters, more over the empty cells after it (the list
 * cell form of the names table, op_names.c: "ANLG", "INS", "DEL"); 0: nothing, 2: cut to its room */
static int row_cell_text(uint32_t i, uint32_t k, const cell_t *c, char *b, uint32_t n)
{
    cell_t e;
    uint32_t j, w;
    for (j = k + 1u, w = 4u; j < 4u; j++, w += 5u) {
        SCR->cell(i, j, &e);
        if (e.label || e.val[0])
            break;
    }
    if (c->kind == CK_ACT && c->label)
        str_cpy(b, nm_cell(c->val[0] ? c->val : c->label), n);
    else if (c->val[0])
        str_cpy(b, c->gk != GK_NONE || str_len(c->val) > w ? nm_cell(c->val) : c->val, n);
    else
        return 0;
    w = c->gk != GK_NONE ? 4u : w > 13u ? 13u : w;
    if (str_len(b) <= w)
        return 1;
    b[w] = 0;                                           /* (a value the table lacks: cut; tests/ui_optimist_names.h) */
    return 2;
}
static void draw_row(uint32_t i, int32_t y, uint32_t on, uint16_t bar, int32_t wide, int32_t rh)
{
    char nm[12], b[16];
    uint32_t k;
    cell_t c;
    int32_t gh = 2 + (rh - ROW_H) / 10;
    if (on)
        cv_rect(0, y, wide, rh - 1, cur_bg(bar));       /* the cursor row: the track's colour, text white */
    y += (rh - ROW_H) / 2;
    SCR->name(i, nm);
    cv_text(4, y + 1, &FONT_S, nm_title(nm, b, sizeof b), on ? C_WHITE : C_GRAY);   /* "ENV to": the header's title */
    if (ui.scr == SCR_SONG && song_row_col(i))         /* SONG: the scene playing, queued; the part playing */
        cv_rect(70, y + 5, 7, 7, song_row_col(i));
    for (k = 0; k < 4u; k++) {
        int32_t x = 80 + 40 * (int32_t)k;
        SCR->cell(i, k, &c);
        if (row_cell_text(i, k, &c, b, sizeof b))
            cv_text(x, y + 1, &FONT_S, b, on ? C_WHITE : c.kind == CK_ACT ? C_DIM : C_HI);   /* (an action: its verb, dim) */
        draw_gauge(x, y + 16, 36, gh, &c, on ? C_WHITE : c.col ? c.col : C_AMB, on ? cur_trough(bar) : C_LINE);
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
    if (lst.pic && lst.shown)
        cv_line(0, GRAPH_H + 1, 239, GRAPH_H + 1, C_LINE);   /* (the rule over the rows; the picture alone: none) */
    for (i = lst.first; i < lst.n && i < lst.first + lst.shown; i++)
        draw_row(i, lst.top + (int32_t)(i - lst.first) * lst.rh, i == lst.cur, lst.bar, lst.n > lst.shown ? 236 : 240, lst.rh);
    if (lst.shown && lst.n > lst.shown) {               /* where the window is in the list */
        int32_t h = (LIST_H - lst.top) * (int32_t)lst.shown / (int32_t)lst.n;
        cv_rect(238, lst.top, 1, LIST_H - lst.top, C_LINE);
        cv_rect(237, lst.top + (LIST_H - lst.top) * (int32_t)lst.first / (int32_t)lst.n, 3, h, C_GRAY);
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
    if (pic && ui.scr != SCR_SONG) {                    /* a picture: alone, the whole panel (the user, 2026-10-12: "Keep the
                                                         * graph"; ARP "is the same as lfo/env. no list"); the header names
                                                         * the row and its n/N, SELECT walks them. SONG keeps its rows under
                                                         * the session grid: its list is the content */
        shown = 0;
        top = LIST_H;
        gr_h = LIST_H;
    } else if (pic) {                                   /* the picture takes the height the rows leave: they sit at the foot */
        uint32_t fit = (uint32_t)(LIST_H - GRAPH_MIN - 3) / ROW_H;
        shown = n < fit ? n : fit;
        top = LIST_H - (int32_t)shown * ROW_H;
        gr_h = top - 3;
    } else if (n && n <= shown) {                       /* no picture (EDIT, VOICE, PROJECT, SYSTEM, GLOBAL, the master FX: the
                                                         * user, 2026-10-12, "we can keep the list below when there is nothing
                                                         * to display besides the 4 cards"), rows to spare: taller rows */
        shown = n;
        rh = LIST_H / (int32_t)n;
        rh = rh > 40 ? 40 : rh;                         /* (a few rows, no picture: at most 40 px a row, at the top) */
    }
    if (cur >= shown / 2u)
        first = cur - shown / 2u;
    if (first + shown > n)
        first = n > shown ? n - shown : 0u;
    sig = hu(hu(hu(hu(hu(7u, n), cur), first), bar + settings.palette * 65536u), (uint32_t)(uintptr_t)gp);
    for (i = first; i < n && i < first + shown; i++) {
        SCR->name(i, nm);
        sig = hs(sig, nm);
        for (k = 0; k < 4u; k++) {
            SCR->cell(i, k, &c);
            sig = hc(sig, &c);
        }
    }
    if (!shown && n) {                                  /* the picture alone: the values it reads, the cursor row's and its graph page's */
        for (k = 0; k < 4u; k++) {
            SCR->cell(cur, k, &c);
            sig = hc(sig, &c);
            if (gp) {
                page_cell(gp, k, &c);
                sig = hc(sig, &c);
            }
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
    cv_tall(OP_PY, LIST_H, C_BLACK, list_paint);    /* (the panel to the screen's foot: two passes) */
}

/* ---- pieces of the mixer's rows (op_mixdraw.c) */
static uint32_t meter_h(int32_t pk)                     /* a peak (32767 = 0 dBFS) as a height: 6 dB a step */
{
    uint32_t lg = 0, a = pk > 0 ? (uint32_t)pk : 0u;
    while (a >>= 1)
        lg++;
    return lg < 5u ? 0u : (uint32_t)clamp(((int32_t)(lg - 5u) * 4 + 4) * METER_H / 44, 0, METER_H);
}
/* ---- no footer (the user, 2026-10-08: "no footer anywhere"): every screen draws to the screen's foot. Under a
 * question the modal covers the panel's first MODAL_H rows; the rows below it are an empty band */
static void draw_under(void)
{
    if ((op_overlay() != 1u && op_overlay() != 3u) || ui.sig[3] == 1u)
        return;
    ui.sig[3] = 1u;
    if (OP_PH > MODAL_H) {                              /* (2x2: the modal takes the whole panel) */
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
            if (a <= end || rr == 33)                   /* (the part to come: a thin line on black) */
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
    {                                                   /* STEP's grid takes the panel from y 25 (no cards): the band again whole when
                                                         * that changes (a layer on STEP, the NAME screen) */
        static uint8_t was_full;
        uint32_t full = (ui.scr == SCR_STEP || glo_cycle()) && lay.shown == LY_PLAY && !name_on();   /* (GLOBAL and
                                                         * SYSTEM's list too: no cards, op_gldraw.c) */
        if (full != was_full && !ui.force) {
            lcd_fill(0, OH_HEAD, 240, 240 - OH_HEAD, C_BLACK);
            memset(ui.sig + 1, 0, sizeof ui.sig - sizeof ui.sig[0]);   /* (the help line's too) */
            mx_redraw();
        }
        was_full = (uint8_t)full;
    }
    hl_pre(ov);                                         /* (the help line: its rows kept from the bands, op_help.c) */
    draw_head();
    if (name_on()) {                                    /* NAME: the field and the keyboard (op_name.c), no footer */
        name_draw();
        ui.force = 0;
        return;
    }
    if (lay.shown != LY_PLAY)
        lay_draw_cards();                               /* a layer: its knobs' cards, its tiles (op_laydraw.c) */
    else if (ui.scr != SCR_STEP && !glo_cycle())
        draw_cards();                                   /* (the mixer too: its knobs are the selected row's; STEP: step_frame) */
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
            step_frame();                               /* the grid / the roll, the cards over it: op_stepdraw.c */
        else if (glo_cycle())
            gl_draw();                                  /* GLOBAL, SYSTEM: the settings list (op_gldraw.c) */
#if FELUCCA_BLE
        else if (dev_listing())
            dev_draw(OP_PY, OH_BODY);                   /* SYSTEM > BLUETOOTH > the BLE devices (op_project.c) */
#endif
        else
            draw_list();
        if (ov == 2u) {                                 /* the toast over the live panel: again when it redrew */
            if (ui.sig[2] != under)
                ui.sig[4] = 0;
            draw_overlay(ov);
        }
    }
    draw_under();
    hl_post();
    ui.force = 0;
}
