/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, 2026-10-24 (the user: "speeding up navigation", the iPad's touch columns): the mirror's
 * navigation (ui/optimist/op_nav.c, 109 NAV) lands where the panel's own gestures land. A page button's family through
 * nav_open is the button's first tap (the same rows, the same cursor), opened again it stays; nav_row is SELECT landing
 * on the row; nav_walk is ALGORITHM landing on the row (a lane, MASTER); a question or a held layer refuses all three.
 * Built with FELUCCA_ED_MIRROR (the MIRROR_CHECK run). */
#if FELUCCA_ED_MIRROR
static void nav_tests(void)
{
    uint32_t r, n, ok;
    char a[12], b[12];
    reset_ui();
    song.sel = 0;
    frames(2);
    tap(B_LFO);                                         /* (the panel's way: LFO's first tap) */
    frames(2);
    n = nav_rows(), r = ui.row[SCR_SOUND];
    SCR->name(r, a);
    reset_ui();
    song.sel = 0;
    frames(2);
    ok = nav_open(SCR_SOUND, B_LFO);
    frames(2);
    SCR->name(ui.row[SCR_SOUND], b);
    check(ok && ui.scr == SCR_SOUND && nav_mode() == B_LFO && nav_rows() == n && ui.row[SCR_SOUND] == r && str_eq(a, b),
          "nav: LFO's family opened as LFO's first tap (the rows, the cursor row)");
    ok = nav_open(SCR_SOUND, B_LFO);
    frames(1);
    check(ok && ui.row[SCR_SOUND] == r, "nav: the family opened again stays (no next row, as a second tap would)");
    {   /* the rows by the names the FM-1 shows them by (its list and header: the names table's title): LFO DEST is "LFO to" */
        uint32_t k, raw = 0, titled = 0;
        char c[12];
        for (k = 0; k < nav_rows(); k++) {
            SCR->name(k, a);
            nav_row_name(k, c, sizeof c);
            raw |= str_eq(a, "LFO DEST");
            titled |= str_eq(c, "LFO to");
        }
        check(raw && titled, "nav: a row named as the FM-1 titles it (LFO DEST: \"LFO to\", the names table)");
    }
    ok = n > 1u && nav_row(n - 1u);
    frames(1);
    check(ok && ui.row[SCR_SOUND] == n - 1u, "nav: a row picked is the cursor row");
    check(!nav_row(n), "nav: a row past the rows is refused");
    ok = nav_walk(MXR_T1 + 2u);
    frames(1);
    check(ok && song.sel == 2u && ui.scr == SCR_SOUND && nav_track() == MXR_T1 + 2u, "nav: T3 walked to, the screen kept");
    ok = nav_walk(MXR_LANE0 + 1u);
    frames(1);
    check(ok && is_drum(TSEL) && lane_picked() && lane_selected() == 1u && nav_track() == MXR_LANE0 + 1u,
          "nav: a drum lane walked to (DR, the lane picked)");
    ok = nav_walk(MXR_MASTER);
    frames(1);
    check(ok && ui.scr == SCR_FX && nav_track() == MXR_MASTER, "nav: MASTER walked to: the master FX, as ALGORITHM");
    nav_open(SCR_HOME, NAV_NONE);
    nav_walk(MXR_MASTER);
    frames(1);
    n = nav_rows();
    nav_row_name(0, a, sizeof a);
    nav_row_name(n ? n - 1u : 0u, b, sizeof b);
    ok = n > 1u && nav_row(n - 1u);
    frames(1);
    check(ok && !str_eq(a, b) && nav_cur() == n - 1u && mx_set(mx_row()) == n - 1u,
          "nav: MIX lists the row's knob sets by name (MASTER: not all MIX); one picked is the set shown");
    ok = nav_open(SCR_PROJECT, NAV_NONE);
    frames(1);
    check(ok && ui.scr == SCR_PROJECT && nav_track() == NAV_NONE && !nav_walk(MXR_T1),
          "nav: PROJECT opened, no track there (ALGORITHM walks nothing)");
    ok = nav_open(SCR_SONG, NAV_NONE);
    frames(1);
    nav_row_name(1, a, sizeof a);
    check(ok && ui.scr == SCR_SONG && nav_rows() == 3u && str_eq(a, "Song") && nav_row(2) && nav_cur() == 2u &&
          song_kind(ui.row[SCR_SONG], &n) == SG_MODE && nav_row(1) && nav_cur() == 1u && song_kind(ui.row[SCR_SONG], &n) == SG_PART,
          "nav: SONG lists its three views (Scenes, Song, Settings), one picked as SEQ cycles to it");
#if FELUCCA_PATTERNS
    check(nav_row(3) && song_on_pat_row() && nav_cur() == 3u && nav_row(0) && nav_cur() == 0u,
          "nav: SONG's row 3 is the PATTERNS row (the client's PATTERN button), then back to Scenes");
    nav_row(3);
    nav_open(SCR_PROJECT, NAV_NONE);
    frames(1);
    check(nav_open(SCR_SONG, NAV_NONE) && ui.scr == SCR_SONG && !song_on_pat_row() && nav_cur() < 3u,
          "nav: SONG opened after the patterns: one of its views, not the PATTERNS row it was left on");
    nav_row(3);
    check(nav_open(SCR_SONG, NAV_NONE) && !song_on_pat_row(), "nav: SONG tapped while on the patterns: its views");
#endif
    ok = nav_open(SCR_HOME, NAV_NONE);
    frames(1);
    ok = ok && nav_walk(MXR_MASTER);
    frames(1);
    if (ui.scr != SCR_HOME)
        ok = ok && nav_open(SCR_HOME, NAV_NONE), frames(1);
    ok = ok && mx_on_master() && nav_open(SCR_SOUND, B_FX);
    frames(1);
    check(ok && ui.scr == SCR_FX, "nav: FX on the mixer's MASTER row opens the master FX, as the panel's FX");
    check(!nav_open(SCR_TEMPO, NAV_NONE) && !nav_open(SCR_SOUND, B_SAVE), "nav: TEMPO and a button with no family refused");
    nav_open(SCR_GLOBAL, NAV_NONE);
    frames(1);
    n = nav_rows();
    nav_row_name(n ? n - 1u : 0u, a, sizeof a);
    ok = n > 1u && nav_row(n - 1u);
    frames(1);
    check(ui.scr == SCR_GLOBAL && ok && a[0] && nav_cur() == n - 1u && ui.row[SCR_GLOBAL] == gl_first(0, ui.row[SCR_GLOBAL]),
          "nav: GLOBAL lists its pages by title; one picked: the cursor on its first line");
    check(nav_knobs() == 3u && gl_pitch() > 0, "nav: GLOBAL's knobs are its four lines");
    nav_open(SCR_SOUND, B_LFO);
    frames(2);
    check(nav_knobs() == (cards_2x2() ? 2u : 1u), "nav: SOUND's knobs are the cards (2x2 as drawn)");
    nav_open(SCR_STEP, NAV_NONE);
    frames(1);
    check(nav_knobs() == 0u, "nav: STEP has no knob places (its grid)");
    reset_ui();
    nav_open(SCR_SOUND, NAV_NONE);
    frames(1);
    press(B_FX);                                        /* (a held layer, FX's: the panel's gesture first) */
    frames(30);
    check(!nav_row(0) && !nav_walk(MXR_T1 + 1u) && !nav_open(SCR_HOME, NAV_NONE) && !nav_knobs(),
          "nav: refused while a layer is held, no knob places");
    release(B_FX);
    frames(2);
    reset_ui();
}
#endif
