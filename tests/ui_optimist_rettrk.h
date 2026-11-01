/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, 2026-10-30: the returns as tracks (docs/UI-OPTIMIST-DESIGN.md 4.1.1; the user: "the idea of having the
 * tracks is that the return track stuff move out of the master track", "clicking home ... should just change the screen to
 * mixer. not change the track, page", "make the return tracks normal track citizens"):
 *   1  RET A's EDIT pages: FX (its effect's TYPE, as a synth's ENGINE; no LEVEL), the effect's own pages; FX: its PRE POST inserts,
 *      its sends; the header's badge RET A
 *   2  MASTER's pages: the master's only (ui_optimist_pagekeep.h, ui_optimist_pages.h D3)
 *   3  the walk RET A B C, MASTER, T1 .. the lanes on every page (ui_optimist_algo.h; 2026-10-31: "keep it AT THE TOP")
 *   4  the mixer: a return or MASTER selected, the master screen RET A B C over MASTER (ui_optimist_mix35.h)
 *   5  HOME: the mixer on the selection's row, nothing else changed; HOME again: SCOPE (2026-10-31, any row)
 *   6  the keys on a return or MASTER: the track selected before (the last played)
 *   7  both OCT on a return's page: the master screen, MASTER selected; again: that page back
 * Included after ui_optimist_pagekeep.h (pgk_rt_row, pgk_rt_is) and ui_optimist_master.h (both_oct). */
static void rtk_rows(char *b, uint32_t n)               /* the FX screen's row names, comma joined */
{
    uint32_t r, rows = SCR->rows(), at = 0;
    char t[16];
    b[0] = 0;
    for (r = 0; r < rows && at < n; r++) {
        SCR->name(r, t);
        at += (uint32_t)snprintf(b + at, n - at, "%s%s", r ? "," : "", t);
    }
}
static void rtk_pages_tests(void)
{
    char rows[160], bd[24];
    cell_t c;
    fxs_set(FXS_DEF);
    reset_ui();
    song.sel = 1;
    track_select(1);
    frames(2);
    mx_apply(MXR_RET0);
    frames(2);
    tap(B_EDIT);
    frames(2);
    rtk_rows(rows, sizeof rows);
    check(ui.scr == SCR_FX && ui.fxo == 1u && rt_fam == RF_EDIT && !strncmp(rows, "FX,RET REV", 10),
          "1: EDIT on RET A's row: its EDIT pages, FX (its TYPE, as a synth's ENGINE) then its effect's (CHO: RET REV)");
    SCR->cell(0, 0, &c);
    check(c.label && !strcmp(c.label, "FX") && !strcmp(c.val, "CHO"), "... FX's first card: the effect's TYPE, CHO, labelled FX (2026-10-31)");
    check(pgk_rt_row("MIX") == SCR->rows(), "... no LEVEL on EDIT (the user: \"only the fx param\"; the mixer row keeps it)");
    {
        head_t h;
        head_make(&h);
        str_cpy(bd, h.eng ? h.eng : "", sizeof bd);
    }
    check(!strcmp(bd, "RET A"), "... the header's badge: RET A");
    check(song.sel == 1u, "... the track selected stays T2");
    tap(B_EDIT);
    check(ui.row[SCR_FX] == 1u && pgk_rt_is("RET REV"), "... EDIT again: its next page");
    tap(B_FX);
    frames(2);
    rtk_rows(rows, sizeof rows);
    check(ui.fxo == 1u && rt_fam == RF_FX && !strncmp(rows, "INSERT", 6) && strstr(rows, "SENDS") != 0 && ui.row[SCR_FX] == 0u,
          "1: FX on RET A: its FX pages, INSERT (PRE, POST) then SENDS");
    SCR->cell(0, 0, &c);
    check(c.kind != CK_NONE, "... INSERT's first card: PRE's type");
    turn(EN_ALGO, 2);
    frames(2);
    check(ui.scr == SCR_FX && ui.fxo == 3u && rt_fam == RF_FX && song.sel == 1u, "3: ALGORITHM on: RET C's FX pages, T2 still selected");
    fxs_set(FXS_DEF);
    reset_ui();
}
static void rtk_home_tests(void)
{
    reset_ui();
    song.sel = 1;
    track_select(1);
    tap(B_ENV);
    tap(B_ENV);
    frames(2);
    {
        uint32_t row = ui.row[SCR_SOUND];
        tap(B_HOME);
        frames(2);
        check(ui.scr == SCR_HOME && mx_row() == MXR_T1 + 1u && song.sel == 1u,
              "5: HOME on T2's ENV page: the mixer, on T2's row, T2 still selected");
        check(mxd.first == MXR_T1 && mxd.last == MXR_LANE0, "... the tracks' view");
        tap(B_HOME);
        frames(2);
        check(ui.scr == SCR_SCOPE && song.sel == 1u, "... HOME again: SCOPE (2026-10-31: never back to the page)");
        tap(B_ENV);
        frames(2);
        check(ui.scr == SCR_SOUND && snd_fam == FAM_ENV && ui.row[SCR_SOUND] == row, "... ENV: T2's ENV page as left (the page memory)");
    }
    reset_ui();
    song.sel = 2;
    track_select(2);
    mx_apply(MXR_RET0 + 1u);
    tap(B_EDIT);
    tap(B_EDIT);
    frames(2);
    check(ui.scr == SCR_FX && ui.fxo == 2u && ui.row[SCR_FX] == 1u, "5: RET B's second EDIT page");
    tap(B_HOME);
    frames(2);
    check(ui.scr == SCR_HOME && mx_row() == MXR_RET0 + 1u && song.sel == 2u && mxd.first == MXR_RET0 && mxd.last == MXR_T1,
          "... HOME: the mixer's master screen, RET B's row, T3 still selected");
    ppm("opt-rettrk-home-retb");
    tap(B_HOME);
    frames(2);
    check(ui.scr == SCR_SCOPE, "... HOME again: SCOPE, as for a track (2026-10-31)");
    tap(B_HOME);
    frames(2);
    check(ui.scr == SCR_HOME, "... HOME on SCOPE: the mixer");
    mx_apply(MXR_RET0 + 1u);
    tap(B_EDIT);
    frames(2);
    check(ui.scr == SCR_FX && ui.fxo == 2u && rt_fam == RF_EDIT && ui.row[SCR_FX] == 1u, "... EDIT: RET B's EDIT page as left (the page memory)");
    reset_ui();
}
static void rtk_keys_tests(void)
{
    reset_ui();
    song.sel = 1;
    track_select(1);
    tap(B_ENV);
    frames(2);
    turn(EN_ALGO, -2);                                  /* T2 to MASTER in one turn (past T1: T2 stays selected) */
    turn(EN_ALGO, -1);                                  /* RET C */
    frames(2);
    check(ui.scr == SCR_FX && ui.fxo == 3u && rt_fam == RF_EDIT, "6: the walk left past T1 and MASTER: RET C's EDIT pages (from ENV)");
    kdown(WK(0));
    check(kb_kind[WK(0)] == KS_NOTE && kb_trk[WK(0)] == 1u, "6: a key on RET C's page plays the track selected before it (T2)");
    kup(WK(0));
    reset_ui();
    song.sel = 1;
    track_select(1);
    mx_apply(MXR_RET0 + 2u);
    frames(2);
    kdown(WK(0));
    check(kb_kind[WK(0)] == KS_NOTE && kb_trk[WK(0)] == 1u, "6: a key on the mixer's RET C row plays T2 (selected before)");
    kup(WK(0));
    mx_apply(MXR_MASTER);
    frames(2);
    kdown(WK(1));
    check(kb_kind[WK(1)] == KS_NOTE && kb_trk[WK(1)] == 1u, "6: ... on MASTER's row: T2 too");
    kup(WK(1));
    reset_ui();
}
static void rtk_oct_tests(void)
{
    reset_ui();
    song.sel = 0;
    track_select(0);
    mx_apply(MXR_RET0 + 1u);
    tap(B_FX);
    frames(2);
    check(ui.scr == SCR_FX && ui.fxo == 2u && rt_fam == RF_FX, "7: RET B's FX pages");
    both_oct();
    check(ui.scr == SCR_HOME && mx_row() == MXR_MASTER && mxd.first == MXR_RET0 && mxd.last == MXR_T1,
          "7: both OCT: the master screen, MASTER selected");
    both_oct();
    check(ui.scr == SCR_FX && ui.fxo == 2u && rt_fam == RF_FX, "7: ... again: RET B's FX pages back");
    reset_ui();
}
static void rettrk_tests(void)
{
    rtk_pages_tests();
    rtk_home_tests();
    rtk_keys_tests();
    rtk_oct_tests();
}
