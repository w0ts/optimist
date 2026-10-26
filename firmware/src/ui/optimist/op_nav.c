/* SPDX-License-Identifier: GPL-3.0-only */
/* The mirror's touch navigation (builder item ED_MIRROR; io/editor/ed_mirror.c 109 NAV; the client ios/OptimistMirror
 * Nav/NavViews.swift). What a touch screen beside the panel lists, and the jumps it makes, each by the panel's own path:
 *   the rows    the screen's rows as SELECT walks them (on SOUND: the family a page button narrowed it to), the cursor's
 *   a row       op_row_pick, as SELECT landing there
 *   a track     walk_to, as ALGORITHM landing there: MASTER, T1..T3, DR, the lanes, ACCENT; only where ALGORITHM walks
 *   a screen    op_enter; a SOUND family as its page button's first tap (op_jump), never its next-row second tap
 *   the knobs   where KNOB 1..4's values are on the panel (the 1x4 cards, the 2x2 cards, the four lines): a drag there turns
 *               that knob (the client's 108 PANEL)
 * None while a question, NAME, a held or locked layer, the TEMPO page or a held step is up (nav_free): the panel's own
 * gestures first. GLOBAL and SYSTEM list their pages of 4 lines (the user, 2026-10-24: "global and system sub pages are
 * missing"), by their titles, as SELECT pages them (glo_step), the cursor on the page's first line. SONG lists its three
 * views, Scenes, Song (its parts), Settings, as SEQ cycles them (nav_song_view), and row 3 is its PATTERNS row (a PATTERNS build:
 * the client's PATTERN button; the user, 2026-10-24: "SEQ hold: we have just three pages ... and pattern gets its own button"). */
#if FELUCCA_ED_MIRROR
_Static_assert(SCR_N == 11u, "109 NAV's screen numbers are SCR_*: web/EDITOR_PROTOCOL.md lists them");
#define NAV_NONE 127u                                   /* (no family, no track: a 7-bit byte) */
static int nav_free(void) { return !op_armed() && !name_on() && lay_now() == LY_PLAY && !tp.on && !st_holding(); }
/* MIX lists the selected row's knob sets (MIX INSERT SENDS SOUND; MASTER's RETURNS, A SENDS ...), as SELECT pages them there
 * (op_mixer.c mx_page): its rows are the tracks, the client's track buttons (the user, 2026-10-24: "a pletora of buttons
 * just called mix") */
#define NAV_SONG_VIEWS 3u
/* SONG's view v reached by the panel's own way, SEQ's tap (op_song.c song_view_cycle, untouched): at most two taps; an empty
 * chain has no PARTS view (the cycle goes SCENES, SETTINGS), as on the panel */
static void nav_song_view(uint32_t v)
{
    uint32_t a, i;
    for (i = 0; i < NAV_SONG_VIEWS && (song_on_pat_row() || song_view_of(song_kind(ui.row[SCR_SONG], &a)) != v); i++)
        song_view_cycle();
}
static uint32_t nav_rows(void)
{
    if (ui.scr == SCR_SONG)
        return NAV_SONG_VIEWS;
    return glo_cycle() ? gl_npg(ui.scr == SCR_SYSTEM) : ui.scr == SCR_HOME ? mx_sets(mx_row()) : SCR->rows();
}
static uint32_t nav_cur(void)
{
    uint32_t a;
    if (ui.scr == SCR_SONG)                             /* (the PATTERNS row: 3, past the views) */
        return song_on_pat_row() ? NAV_SONG_VIEWS : song_view_of(song_kind(ui.row[SCR_SONG], &a));
    return glo_cycle() ? gl_pg(ui.scr == SCR_SYSTEM, ui.row[ui.scr]) : ui.scr == SCR_HOME ? mx_set(mx_row()) : ui.row[ui.scr];
}
/* GLOBAL and SYSTEM: line i of screen s's page p (the first line of the page past p - 1's) */
static uint32_t nav_pg_line(uint32_t s, uint32_t p)
{
    uint32_t i = 0;
    while (p-- && i < gl_n(s))
        i = gl_end(s, i);
    return i;
}
/* row r's name as the FM-1's list and header title it (op_draw.c draw_row: the drum track's SOUND pages by what they hold,
 * then the names table's title, "ENV2 to", "Filter 2"; the user, 2026-10-24: "page names should match the page name on the
 * fm1"); GLOBAL and SYSTEM: their pages' titles, the mixer: the set's name */
static void nav_row_name(uint32_t r, char *b, uint32_t n)   /* (b holds n, 12 at least) */
{
    uint32_t s = ui.scr == SCR_SYSTEM;
    if (ui.scr == SCR_SONG) {
        static const char *const V[NAV_SONG_VIEWS] = {"Scenes", "Song", "Settings"};
        str_cpy(b, V[r % NAV_SONG_VIEWS], n);
    } else if (glo_cycle()) {
        gl_ptitle(s, nav_pg_line(s, r), b, n);
    } else if (ui.scr == SCR_HOME) {
        uint8_t keep = mx.set;                          /* (mix_name names the set shown: set r, for the name only) */
        mx.set = (uint8_t)r;
        mix_name(mx_row(), b);
        mx.set = keep;
    } else {
        char nm[12];
        if (ui.scr == SCR_SOUND)
            snd_title_row(r, nm);
        else
            SCR->name(r, nm);
        nm_title(nm, b, n);
    }
}
/* the page button (panel.c B_*) of the family SOUND shows, NAV_NONE: every row, another screen */
static uint32_t nav_mode(void)
{
    uint32_t b, f = snd_fam == SND_FM6 ? FAM_ENV : snd_fam;
    if (ui.scr != SCR_SOUND || f == SND_ALL)
        return NAV_NONE;
    for (b = 0; b < NB; b++)
        if (JUMP_FAM[b] == f)
            return b;
    return NAV_NONE;
}
/* the walk's place (a mixer row, MXR_MASTER .. MXR_LN - 1), NAV_NONE where ALGORITHM walks nothing (the USB row too) */
static uint32_t nav_track(void)
{
    uint32_t p = walk_pos(0);
    return algo_has_track() && p < MXR_LN ? p : NAV_NONE;
}
static void nav_track_name(uint32_t p, char *b)         /* (b holds 12) */
{
    str_cpy(b, p == MXR_MASTER ? "MASTER" : p >= MXR_LANE0 ? lane_name(p - MXR_LANE0) : trk_tag(mx_trk_of(p)), 12);
}
static int nav_row(uint32_t r)
{
    if (nav_free() && ui.scr == SCR_SONG) {
#if FELUCCA_PATTERNS
        if (r == NAV_SONG_VIEWS) {
            op_row_pick(song_pat_row_idx());
            return 1;
        }
#endif
        if (r < NAV_SONG_VIEWS)
            nav_song_view(r);
        return r < NAV_SONG_VIEWS;
    }
    if (!nav_free() || r >= nav_rows())
        return 0;
    if (glo_cycle())
        gl_at(nav_pg_line(ui.scr == SCR_SYSTEM, r));   /* (a page: its first line the cursor, as SELECT) */
    else if (ui.scr == SCR_HOME)
        mx_page((int32_t)r - (int32_t)mx_set(mx_row()));   /* (the set, as SELECT's detents) */
    else if (r != ui.row[ui.scr])
        op_row_pick(r);
    return 1;
}
static int nav_walk(uint32_t p)
{
    if (!nav_free() || p >= MXR_LN || nav_track() == NAV_NONE || ft_on)
        return 0;
    if (p != nav_track())
        walk_to(p, 0);
    return 1;
}
/* screen scr (SCR_*; TEMPO is PLAY held, never opened), on SOUND page button b's family (NAV_NONE: every row) */
static int nav_open(uint32_t scr, uint32_t b)
{
    uint32_t f;
    if (!nav_free() || scr >= SCR_N || scr == SCR_TEMPO || scr == SCR_LFX)
        return 0;
    if (scr == SCR_SOUND && b == B_FX && mx_on_master())
        scr = SCR_FX;                                   /* (FX on the mixer's MASTER: the master FX, as the panel's, op_input.c) */
    else if (scr == SCR_SOUND && b != NAV_NONE) {
        if (b >= NB || JUMP_FAM[b] == 0xFF || JUMP_FAM[b] == FAM_GLO)
            return 0;
        f = JUMP_FAM[b] == FAM_ENV && fm6_sel() ? SND_FM6 : JUMP_FAM[b];
        if (ui.scr == SCR_SOUND && snd_fam == f)
            return 1;                                   /* (there: a second tap would be the next row) */
        if (f != SND_FM6 && !snd_has_fam(f))
            return 0;                                   /* (none on this track: its LED is dark) */
        op_jump(JUMP_FAM[b]);
        return 1;
    }
    if (ui.scr != scr || (scr == SCR_SOUND && snd_fam != SND_ALL))
        op_enter(scr);
    if (scr == SCR_SONG && song_on_pat_row())
        nav_song_view(0);                               /* (SONG: its views, never the PATTERNS row it was left on: PATTERN's
                                                         * own button is ROW 3; the user, 2026-10-24) */
    return 1;
}
/* where KNOB 1..4's values are on the panel, as op_draw.c draws them: 0 nowhere (a layer, STEP, the PRESETS list over it), 1 the
 * 1x4 cards (CARD_X, OY_CARD), 2 the 2x2 cards (BIG_W x BIG_H from OY_CARD), 3 the four lines (op_gldraw.c: the first's top
 * GL_TOP + gl_y0, gl_pitch apart). The client draws the places from these (Nav/NavState.swift knobRects) */
static uint32_t nav_knobs(void)
{
    if (!nav_free() || ui.overlay || ui.scr == SCR_STEP || ui.scr == SCR_SCOPE || song_tbl_on() || prj_listing()
#if FELUCCA_BT_UI
        || dev_listing()
#endif
    )
        return 0;
    return glo_cycle() || pl_on() ? 3u : cards_2x2() ? 2u : 1u;
}
#endif
