/* SPDX-License-Identifier: GPL-3.0-only */
/* Pattern mode (LFO held, SONG's PATTERNS row), the user 2026-10-31, included by ui_optimist_test.c:
 *   warning    a white key that would replace a track's pattern with edits not stored asks "LOSE EDITS? T1 T3" first
 *              (YES = SAVE launches, NO = HOME keeps the edits); a clean track launches at once
 *   black 5    ALL TRACKS: the white keys launch on T1..DR (one ask naming the tracks with edits); again: the selected track
 *   black 6    STOP (was black 5), asked over edits too
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
    check(op_armed() && !strcmp(ui.arm_q, "LOSE EDITS T1?") && pat_cur[0] == 3u, "T1 edited, white 6: \"LOSE EDITS T1?\", nothing launched yet");
    tap(B_HOME);
    check(!op_armed() && pat_cur[0] == 3u && trk[0].step[0].note[0] == 60u, "... NO (HOME): cancelled, the edit is still there");
    key(WK(5));
    tap(B_SAVE);
    check(!op_armed() && pat_cur[0] == 5u && !trk[0].step[0].n, "... YES (SAVE): slot 6 launched, the edit gone with the old pattern");
    key(WK(3));
    check(!op_armed() && pat_cur[0] == 3u, "... the stored slot 4 back, clean");
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
    check(op_armed() && !strcmp(ui.arm_q, "LOSE EDITS T1 T3?") && pat_cur[0] == 3u && pat_cur[2] == 3u, "... T1 and T3 edited, white 7: one question naming T1 T3");
    tap(B_HOME);
    check(!op_armed() && pw_all(3u), "... NO: nothing launched");
    key(WK(6));
    tap(B_SAVE);
    check(!op_armed() && pw_all(6u), "... YES: slot 7 on all four tracks");
    key(WK(3));
    check(!op_armed() && pw_all(3u) && lay.pmod == 5u, "... clean: slot 4 on all four at once, no question");
    /* black 6: stop */
    key(BK(5));
    check(!op_armed() && pw_all(PAT_NONE) && !strcmp(ui.msg, "STOPPED"), "black 6 (all tracks picked): every track stopped");
    key(WK(3));
    pw_edit(3, 80);
    key(BK(5));
    check(op_armed() && !strcmp(ui.arm_q, "LOSE EDITS DR?") && pat_cur[3] == 3u, "... DR edited, black 6: asked before the stop");
    tap(B_HOME);
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
