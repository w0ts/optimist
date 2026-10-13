/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, the user's request of 2026-10-13: "screen should remember its page, including plocks etc.".
 * Every screen shows the page it was left on when it is entered again: SOUND per track and family (a row the track no
 * longer has: the nearest), STEP its row, the held step's card page per track (a page the track lacks: STEP), the
 * mixer's knob set, the lists their cursor line. A page button, a shortcut or a knob that jumps still jumps. Included
 * by ui_optimist_test.c after WK / kdown / kup. */
static void pm_to(uint32_t scr)                         /* a screen entered and left again by HOME: the way a user comes back */
{
    op_enter(SCR_HOME);
    frames(2);
    op_enter(scr);
    frames(2);
}
static uint32_t pm_rows(uint32_t scr) { return SCREENS[scr].rows(); }
/* family button b of SOUND on track tr: the family shown and the row, after HOME and the button again */
static void pm_family_tests(void)
{
    uint32_t r1, r2, n;
    reset_ui();
    song.sel = 0;
    track_select(0);
    tap(B_EDIT);
    frames(2);
    n = snd_rows();
    check(ui.scr == SCR_SOUND && snd_fam == FAM_EDIT && n >= 3u, "page memory, SOUND: EDIT opens its rows");
    turn(EN_SELECT, 2);
    r1 = ui.row[SCR_SOUND];
    check(r1 == 2u, "... SELECT twice: the third row");
    tap(B_HOME);
    check(ui.scr == SCR_HOME, "... HOME: the mixer");
    tap(B_ENV);
    frames(2);
    tap(B_HOME);
    tap(B_EDIT);
    frames(2);
    check(ui.scr == SCR_SOUND && snd_fam == FAM_EDIT && ui.row[SCR_SOUND] == r1,
          "... away through ENV and back by EDIT: the third row again (the family's page last shown)");
    /* ENV's own page is its own */
    tap(B_HOME);
    tap(B_ENV);
    frames(2);
    check(snd_fam == FAM_ENV || snd_fam == SND_FM6, "... ENV: its own family");
    /* per track: T2 has its own page of the same family */
    tap(B_HOME);
    track_select(1);
    frames(2);
    tap(B_EDIT);
    frames(2);
    check(snd_fam == FAM_EDIT && ui.row[SCR_SOUND] == 0u, "page memory, SOUND per track: T2's EDIT, never looked at: its first row");
    turn(EN_SELECT, 1);
    r2 = ui.row[SCR_SOUND];
    tap(B_HOME);
    track_select(0);
    tap(B_EDIT);
    frames(2);
    check(ui.row[SCR_SOUND] == r1, "... T1's EDIT again: T1's page");
    tap(B_HOME);
    track_select(1);
    tap(B_EDIT);
    frames(2);
    check(ui.row[SCR_SOUND] == r2, "... T2's EDIT again: T2's page");
    /* walking to another track on SOUND keeps the page's name (as before) */
    turn(EN_SELECT, 1);
    track_select(2);
    frames(2);
    check(ui.scr == SCR_SOUND && snd_fam == FAM_EDIT, "... ALGORITHM on SOUND: the family stays across tracks");
    tap(B_HOME);
    track_select(0);
    reset_ui();
}
/* an engine change: the row the new engine lacks is the nearest, the last */
static void pm_engine_tests(void)
{
    uint32_t e, emax = 0, emin = 0, nmax = 0, nmin = 1000u, n, keep, ok;
    reset_ui();
    song.sel = 0;
    track_select(0);
    keep = TSEL->eng_req;
    for (e = 0; e < NENGINES; e++) {
        if (eng_free(e))
            continue;
        set_engine(e);
        snd_fam = FAM_EDIT;
        n = snd_rows();
        if (n > nmax)
            nmax = n, emax = e;
        if (n < nmin)
            nmin = n, emin = e;
    }
    set_engine(emax);
    frames(2);
    tap(B_EDIT);
    frames(2);
    turn(EN_SELECT, (int32_t)nmax);
    check(ui.row[SCR_SOUND] == nmax - 1u, "page memory, an engine change: the last EDIT row of the engine with the most");
    tap(B_HOME);
    frames(2);
    set_engine(emin);
    frames(2);
    tap(B_EDIT);
    frames(2);
    n = snd_rows();
    ok = ui.scr == SCR_SOUND && snd_fam == FAM_EDIT && ui.row[SCR_SOUND] == (nmax - 1u < n ? nmax - 1u : n - 1u);
    check(ok, "... another engine with fewer: the row it has nearest (the last), still EDIT");
    set_engine(keep);
    tap(B_HOME);
    reset_ui();
}
static void pm_step_tests(void)
{
    uint32_t d, r;
    for (d = 0; d < 2u; d++) {
        reset_ui();
        song.sel = d ? TRK_DRUM : 0;
        track_select(song.sel);
        tap(B_SEQ);
        frames(2);
        check(ui.scr == SCR_STEP && ui.row[SCR_STEP] == 0u, d ? "page memory, STEP on DR: opens on PATTERN" : "page memory, STEP on T1: opens on PATTERN");
        turn(EN_SELECT, 1);
        r = ui.row[SCR_STEP];
        check(r == 1u, d ? "... SELECT: the next page (the lane's)" : "... SELECT: the next page");
        tap(B_HOME);
        tap(B_SEQ);
        frames(2);
        check(ui.scr == SCR_STEP && ui.row[SCR_STEP] == r, d ? "... HOME, SEQ: the same page again (DR)" : "... HOME, SEQ: the same page again");
        if (d) {                                            /* the drum track's page is the track's, not the lane's */
            tap(B_HOME);
            lane_select(7);
            tap(B_SEQ);
            frames(2);
            check(ui.row[SCR_STEP] == r, "... DR: another lane selected meanwhile, the same page (per track, not per lane)");
        }
    }
    track_select(0);
    reset_ui();
}
static void pm_lists_tests(void)
{
    static const uint8_t SCRS[] = {SCR_FX, SCR_PROJECT, SCR_SONG, SCR_SCOPE};
    uint32_t i, r, g, s;
    for (i = 0; i < sizeof SCRS; i++) {
        reset_ui();
        op_enter(SCRS[i]);
        frames(2);
        if (pm_rows(SCRS[i]) < 2u)
            continue;
        turn(EN_SELECT, 1);
        r = ui.row[SCRS[i]];
        pm_to(SCRS[i]);
        check(ui.scr == SCRS[i] && ui.row[SCRS[i]] == r && r != 0u, "page memory, a list: left and entered again, its cursor row kept");
    }
    reset_ui();
    tap(B_GLO);
    frames(2);
    check(ui.scr == SCR_GLOBAL, "page memory, GLOBAL / SYSTEM: GLO opens GLOBAL");
    turn(EN_ALGO, 5);
    g = ui.row[SCR_GLOBAL];
    tap(B_GLO);
    turn(EN_ALGO, 3);
    s = ui.row[SCR_SYSTEM];
    tap(B_HOME);
    tap(B_GLO);
    check(ui.scr == SCR_GLOBAL && ui.row[SCR_GLOBAL] == g && g != 0u, "... left by HOME, GLO: GLOBAL on the line it was left on");
    tap(B_GLO);
    check(ui.scr == SCR_SYSTEM && ui.row[SCR_SYSTEM] == s && s != 0u, "... GLO again: SYSTEM on the line it was left on");
    /* TEMPO, held open by PLAY */
    reset_ui();
    press(B_PLAY);
    frames(HOLD_FRAMES);
    turn(EN_SELECT, 1);
    r = ui.row[SCR_TEMPO];
    release(B_PLAY);
    frames(2);
    press(B_PLAY);
    frames(HOLD_FRAMES);
    check(ui.scr == SCR_TEMPO && ui.row[SCR_TEMPO] == r && r != 0u, "... TEMPO (PLAY held): the row it was left on");
    release(B_PLAY);
    frames(2);
    /* the mixer's knob set */
    reset_ui();
    turn(EN_SELECT, 1);
    r = mx.set;
    check(r == 1u, "page memory, the mixer: SELECT, the second knob set");
    op_enter(SCR_PROJECT);
    frames(2);
    tap(B_HOME);
    check(ui.scr == SCR_HOME && mx.set == r, "... PROJECT and HOME: the same knob set");
    reset_ui();
}
static void pm_held_tests(void)
{
#if FELUCCA_PLOCK
    uint32_t pg, hp, i;
    uint8_t ix[OP_MAXROWS];
    reset_ui();
    song.sel = 0;
    track_select(0);
    memset(hp_mem, 0, sizeof hp_mem);
    tap(B_SEQ);
    frames(2);
    kdown(WK(0));
    check(st.held && st.hp == 0u, "page memory, a held step: never used, the step's page");
    if (hp_count() <= HP_BASE) {
        kup(WK(0));
        reset_ui();
        return;
    }
    turn(EN_SELECT, (int32_t)hp_count());
    hp = st.hp;
    pg = st.lock_pg;
    check(hp == hp_count() - 1u && pg != LOCK_NONE, "... SELECT to the last page: a p-lock page");
    kup(WK(0));
    check(!st.held && st.hp == 0u, "... the step let go: nothing held");
    kdown(WK(5));
    check(st.held && st.hp == hp && st.lock_pg == pg, "... the next step held: the same p-lock page, reopened");
    kup(WK(5));
    /* per track */
    track_select(1);
    frames(2);
    kdown(WK(0));
    check(st.hp == 0u && st.lock_pg == LOCK_NONE, "... on T2, never used: the step's page (per track)");
    kup(WK(0));
    track_select(0);
    frames(2);
    kdown(WK(0));
    check(st.hp == hp && st.lock_pg == pg, "... back on T1: its page");
    /* a page button with a step held jumps, and that becomes the page */
    press(B_ENV);
    release(B_ENV);
    frames(2);
    pg = st.lock_pg;
    hp = st.hp;
    kup(WK(0));
    kdown(WK(2));
    check(st.hp == hp && st.lock_pg == pg, "... a page button with a step held, then the next step: that page");
    kup(WK(2));
    /* the extras page */
    if (EX_N) {
        kdown(WK(0));
        turn(EN_SELECT, -(int32_t)hp_count());
        turn(EN_SELECT, 1);
        check(st.hp == 1u, "... SELECT to EXTRA");
        kup(WK(0));
        kdown(WK(0));
        check(st.hp == 1u, "... the next step held: EXTRA");
        kup(WK(0));
    }
    /* a p-lock page the track lacks: the step's page */
    track_select(TRK_DRUM);
    frames(2);
    for (i = 0; i < NPAGES && lock_pg_ok(&PAGES[i]); i++)
        ;
    hp_locks(ix);
    if (i < NPAGES) {
        hp_mem[TRK_DRUM] = (uint8_t)(2u + i);
        kdown(WK(0));
        check(st.held && st.hp == 0u && st.lock_pg == LOCK_NONE, "... a remembered p-lock page the track lacks: the step's page");
        kup(WK(0));
    }
    hp_mem[TRK_DRUM] = 0;
    track_select(0);
    reset_ui();
#endif
}
static void pagemem_tests(void)
{
    pm_family_tests();
    pm_engine_tests();
    pm_step_tests();
    pm_lists_tests();
    pm_held_tests();
}
