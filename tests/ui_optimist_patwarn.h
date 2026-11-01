/* SPDX-License-Identifier: GPL-3.0-only */
/* Pattern mode (LFO held, SONG's PATTERNS row), the user 2026-10-31, included by ui_optimist_test.c:
 *   warning    stopped (issue 142, 2026-11-01): no question any more, the outgoing pattern's edits are kept as its live
 *              pattern and come back with it (playing: ui_optimist_patlive.h)
 *   black 5    ALL TRACKS: the white keys launch on T1..DR (one ask naming the tracks with edits); again: the selected track
 *   black 6    STOP (was black 5), the edits kept too
 *   black held a black key 1-4 held + white n stores that track's working pattern in slot n, black 5 held: every track's
 *   numbers    the pattern numbers 1..16 over the grid's columns, on the dark panel (no text on a fill); the screen itself: ui_optimist_patview.h */
#if FELUCCA_PATTERNS
static void pw_edit(uint32_t k, uint32_t n)             /* track k's first step: a note (an edit of the working pattern) */
{
    trk[k].step[0].time = ST_NOTE;
    trk[k].step[0].n = 1;
    trk[k].step[0].note[0] = (uint8_t)n;
}
static int pw_all(uint32_t slot)                        /* every track plays slot, nothing queued */
{
    uint32_t k;
    for (k = 0; k < NTRK; k++)
        if (pat_cur[k] != slot || pat_req[k] != PAT_NONE)
            return 0;
    return 1;
}
static void patwarn_tests(void)
{
    uint32_t k;
    reset_ui();
    song.sel = 0;
    song.playing = 0;
    for (k = 0; k < NTRK; k++) {
        pw_edit(k, 48u + k);
        pat_store_slot(k, 3);
    }
    press(B_LFO);
    frames(HOLD_FRAMES);
    check(lpk.on && song_on_pat_row() && lay.pmod == 0u && !pat_changed(), "patwarn: LFO held, T1..DR stored in slot 4: nothing unsaved");
    key(WK(5));
    check(!op_armed() && pat_cur[0] == 5u, "... white 6 with no edits: launched at once, no question");
    key(WK(3));
    check(!op_armed() && pat_cur[0] == 3u && !(pat_changed() & 1u), "... white 4 again: back, still clean");
    pw_edit(0, 60);
    key(WK(5));
    check(!op_armed() && pat_cur[0] == 5u && !trk[0].step[0].n && pat_live_at(0, 3), "T1 edited, white 6 (stopped): launched at once, no question, the edit kept as slot 4's live pattern");
    key(WK(3));
    check(!op_armed() && pat_cur[0] == 3u && trk[0].step[0].note[0] == 60u && (pat_changed() & 1u), "... slot 4 again: it plays its edit (not saved: \"*\")");
    pw_edit(0, 48u);
    key(WK(5));
    key(WK(3));
    check(!pat_live_at(0, 3) && !(pat_changed() & 1u), "... edited back to the saved pattern and left: the live one dropped, clean");
    pw_edit(1, 70);
    key(WK(7));
    check(!op_armed() && pat_cur[0] == 7u, "T2 edited, T1 selected: T1 launches at once (T2's edit is not on the line)");
    key(WK(3));
    pw_edit(1, 48u + 1u);
    /* black 5: all tracks */
    key(BK(4));
    check(lay.pmod == 5u && !strcmp(ui.msg, "ALL TRACKS"), "black 5: ALL TRACKS");
    pw_edit(0, 61);
    pw_edit(2, 62);
    key(WK(6));
    check(!op_armed() && pw_all(6u) && pat_live_at(0, 3) && pat_live_at(2, 3) && !pat_live_at(1, 3), "... T1 and T3 edited, white 7: slot 7 on all four at once, T1's and T3's edits kept");
    key(WK(3));
    check(!op_armed() && pw_all(3u) && lay.pmod == 5u && trk[0].step[0].note[0] == 61u && trk[2].step[0].note[0] == 62u, "... slot 4 on all four: the edits back");
    pw_edit(0, 48u);
    pw_edit(2, 50u);
    /* black 6: stop */
    key(BK(5));
    check(!op_armed() && pw_all(PAT_NONE) && !strcmp(ui.msg, "STOPPED"), "black 6 (all tracks picked): every track stopped");
    key(WK(3));
    pw_edit(3, 80);
    key(BK(5));
    check(!op_armed() && pat_cur[3] == PAT_NONE && pat_live_at(3, 3), "... DR edited, black 6: stopped at once, the edit kept");
    key(WK(3));
    pw_edit(3, 48u + 3u);
    key(BK(4));
    check(lay.pmod == 0u && !strcmp(ui.msg, "PATTERNS") && pw_all(3u), "black 5 again: the selected track only, black 5 does not stop any more");
    for (k = 0; k < NTRK; k++)
        pat_store_slot(k, 3);
    key(BK(5));
    check(!op_armed() && pat_cur[0] == PAT_NONE && pat_cur[1] == 3u, "black 6, one track: only the selected track stops");
    key(WK(3));
    /* a black key held + white n: store */
    kdown(BK(1));
    pw_edit(1, 90);
    key(WK(9));
    kup(BK(1));
    check(song.sel == 1u && pat_has(1, 9) && !pat_has(0, 9) && !pat_has(2, 9) && pat_cur[1] == 9u, "black 2 held + white 10: T2's pattern stored in slot 10, no other track");
    kdown(BK(4));
    key(WK(10));
    kup(BK(4));
    check(pat_has(0, 10) && pat_has(1, 10) && pat_has(2, 10) && pat_has(3, 10), "black 5 held + white 11: every track's pattern stored in slot 11");
    kdown(BK(0));
    key(WK(10));
    kup(BK(0));
    check(op_armed() && !strcmp(ui.arm_q, "STORE T1 11?"), "black 1 held + white 11 over a used slot: asked, as SAVE + n");
    tap(B_HOME);
    key(WK(2));
    check(!op_armed() && !pat_has(0, 2) && pat_cur[0] == 2u, "a white key alone still launches");
    /* the numbers over the grid */
    ui.force = 1;
    frame();
    ppm("opt-patterns-numbers");
    check(px_in((uint32_t)PG_X(2), OP_PY, PG_CW, PG_NUMH, C_OK), "the numbers: the playing pattern's number lit over its column (the odd ones on the upper line)");
    check(px_in((uint32_t)PG_X(0), OP_PY, PG_CW, PG_NUMH, C_GRAY), "... the others grey, text on the dark panel above the blocks");
    release(B_LFO);
    reset_ui();
}
#endif
