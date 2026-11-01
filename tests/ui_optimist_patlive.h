/* SPDX-License-Identifier: GPL-3.0-only */
/* Live patterns (issue w0ts/optimist#142, the user 2026-11-01; docs/PATTERNS-DESIGN.md 9), included by ui_optimist_test.c after
 * ui_optimist_patwarn.h and ui_optimist_prjflow.h:
 *   RELOAD     HOME held + SEQ (acts when let go, no key used): "RELOAD T1?", YES drops the track's edits and plays its saved
 *              pattern again, HOME changes nothing; no edits: "NO EDITS"; on the pattern page with ALL TRACKS: "RELOAD ALL?";
 *              HOME + SEQ + a key on the pattern page stays the scene's clear (no RELOAD)
 *   project    a LOAD of another project with live patterns (the work otherwise as stored) asks "SAVE PROJECT n?" first; YES
 *              saves everything (each live pattern the saved one) then loads */
#if FELUCCA_PATTERNS
static void hs_chord(void)                              /* HOME held, SEQ tapped, HOME let go */
{
    press(B_HOME);
    press(B_SEQ);
    release(B_SEQ);
    release(B_HOME);
}
static void pl_edit(uint32_t k, uint32_t n)
{
    trk[k].step[1].time = ST_NOTE;
    trk[k].step[1].n = 1;
    trk[k].step[1].note[0] = (uint8_t)n;
}
static void patlive_ui_tests(void)
{
    uint32_t k, scr;
    reset_ui();
    song.sel = 0;
    song.playing = 0;
    for (k = 0; k < NTRK; k++) {
        pl_edit(k, 50u + k);
        pat_store_slot(k, 9);
    }
    op_enter(SCR_HOME);
    scr = ui.scr;
    hs_chord();
    check(!op_armed() && !strcmp(ui.msg, "NO EDITS") && ui.scr == scr, "patlive: HOME + SEQ with nothing edited: \"NO EDITS\", no question, the screen stays (no STEP)");
    pl_edit(0, 70);
    hs_chord();
    check(op_armed() && !strcmp(ui.arm_q, "RELOAD T1?") && trk[0].step[1].note[0] == 70u, "T1 edited, HOME + SEQ: \"RELOAD T1?\" asked, nothing changed yet");
    tap(B_HOME);
    check(!op_armed() && trk[0].step[1].note[0] == 70u && ui.scr == scr, "... HOME: cancelled, the edit still there");
    hs_chord();
    tap(B_SAVE);
    check(!op_armed() && trk[0].step[1].note[0] == 50u && pat_cur[0] == 9u && !(pat_changed() & 1u), "... YES: the saved pattern back, clean");
    pl_edit(0, 71);
    pat_launch(0, 10, PW_END);                          /* (left: a live record) */
    pat_launch(0, 9, PW_END);
    check(pat_live_at(0, 9) && trk[0].step[1].note[0] == 71u, "T1 left and back: it plays its edit (a live pattern)");
    hs_chord();
    tap(B_SAVE);
    check(!pat_live_at(0, 9) && trk[0].step[1].note[0] == 50u, "... RELOAD: the live pattern dropped, the saved one plays");
    /* the pattern page: ALL TRACKS, and SEQ + HOME + a key (a scene's clear) */
    pl_edit(0, 72);
    pl_edit(2, 73);
    press(B_LFO);
    frames(HOLD_FRAMES);
    key(BK(4));
    check(lay.pmod == 5u, "the pattern page, black 5: ALL TRACKS");
    hs_chord();
    check(op_armed() && !strcmp(ui.arm_q, "RELOAD ALL?"), "... HOME + SEQ: \"RELOAD ALL?\"");
    tap(B_SAVE);
    check(trk[0].step[1].note[0] == 50u && trk[2].step[1].note[0] == 52u && !pat_changed(), "... YES: every track's saved pattern back");
    key(BK(4));
    pl_edit(0, 74);
    press(B_HOME);
    press(B_SEQ);
    key(WK(0));
    release(B_SEQ);
    release(B_HOME);
    check(!op_armed() || strcmp(ui.arm_q, "RELOAD T1?"), "... HOME + SEQ + a white key: the scene's gesture, no RELOAD asked");
    if (op_armed())
        tap(B_HOME);
    release(B_LFO);
    reset_ui();
    /* a project LOAD with only live patterns changed: asked to save first, YES saves them as the saved patterns */
    if (!prj_slot_used(PR_PROJECT, 1))
        (void)prj_save_go(1);                           /* (the target project stored; the first one open again below) */
    prj_load_go(0);
    pat_launch(0, 9, PW_END);
    pl_edit(0, 76);
    pat_launch(0, 10, PW_END);                          /* (slot 10's edit kept as its live pattern) */
    pat_launch(0, 9, PW_END);
    work_mark();                                        /* (the work as stored: only the live pattern is unsaved) */
    check(pat_live_count() > 0u && !work_changed() && prj_unsaved(), "the open project holds a live pattern, the work otherwise as stored: unsaved");
    pf_row(PR_PROJECT);
    pf_key(1);
    check(op_armed() && !strncmp(ui.arm_q, "SAVE PROJECT", 12), "... LOAD of another project: \"SAVE PROJECT n?\" first");
    tap(B_SAVE);
    check(!op_armed() && project_cur() == 1u, "... YES: saved, then the other project loaded");
    prj_load_go(0);
    check(project_cur() == 0u && !pat_live_count(), "... the first project again: no live pattern, its edit saved");
    pat_launch(0, 10, PW_END);
    pat_launch(0, 9, PW_END);
    check(pat_has(0, 9) && trk[0].step[1].note[0] == 76u, "... the saved pattern is the edit (PROJECT SAVE saved everything)");
    reset_ui();
}
#endif
