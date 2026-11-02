/* SPDX-License-Identifier: GPL-3.0-only */
/* A new pattern (issue w0ts/optimist#142 points 4 and 6, the user 2026-11-02), included by ui_optimist_test.c after
 * ui_optimist_patlive.h:
 *   the pattern page   SAVE + n on a slot with no pattern asks "CLONE T1 5?" (YES = SAVE: the working copy stored there); HOME asks
 *                      "EMPTY T1 5?" (YES: an empty pattern, the track plays it); HOME again, or 3 s: nothing created. Its own
 *                      slot and a used slot are as before (saved, "STORE T1 5?")
 *   SONG scenes        KNOB 1..4 on an empty scene creates it (the loop stored, "SCENE N CREATED") instead of "EMPTY SCENE";
 *                      the knob's list ends with the first pattern not filled yet: resting on it asks the same question and
 *                      sets the scene's reference to it on YES */
#if FELUCCA_PATTERNS
static void patnew_ui_tests(void)
{
    uint32_t k, c, sl;
    uint8_t refs[NTRK];
    reset_ui();
    song.sel = 0;
    song.playing = 0;
    for (k = 0; k < 8u; k++)
        trk[0].step[k].time = ST_NOTE, trk[0].step[k].n = 1, trk[0].step[k].note[0] = (uint8_t)(40 + k);
    for (sl = 0; sl < PAT_N && (pat_has(0, sl) || pat_users(0, sl, PAT_ALL)); sl++)
        ;
    check(sl < PAT_N - 1u, "patnew: a free slot for T1");
    /* the pattern page: SAVE + n on an empty slot */
    hold2(B_OCTUP, B_LFO);                              /* (the pattern page locked open: SAVE + n there) */
    frames(2);
    press(B_SAVE);
    key(WK(sl));
    release(B_SAVE);
    check(clone_asked() && !pat_has(0, sl), "patnew: SAVE + an empty slot: the CLONE question, nothing created yet");
    check(!strncmp(ui.arm_q, "CLONE T1 ", 9), "... it reads CLONE T1 n?");
    tap(B_HOME);
    check(op_armed() && ui.arm_row == OA_PAT_EMPTY && !strncmp(ui.arm_q, "EMPTY T1 ", 9), "... HOME: the EMPTY question instead");
    tap(B_HOME);
    check(!op_armed() && !pat_has(0, sl) && pat_cur[0] != sl, "... HOME again: cancelled, nothing created");
    press(B_SAVE);
    key(WK(sl));
    release(B_SAVE);
    clone_yes();
    check(pat_has(0, sl) && pat_cur[0] == sl && !op_armed() && !strncmp(ui.msg, "CLONED T1 ", 10), "... SAVE (YES): the working copy cloned there, \"CLONED T1 n\"");
    check(trk[0].step[3].note[0] == 43u, "... and it still plays it");
    press(B_SAVE);
    key(WK(sl));
    release(B_SAVE);
    check(op_armed() && ui.arm_row == OA_PAT_STORE, "... a used slot: the STORE question as before, no CLONE");
    tap(B_HOME);
    /* EMPTY: the next free slot, answered HOME then SAVE */
    sl++;
    while (sl < PAT_N && (pat_has(0, sl) || pat_users(0, sl, PAT_ALL)))
        sl++;
    press(B_SAVE);
    key(WK(sl));
    release(B_SAVE);
    tap(B_HOME);
    tap(B_SAVE);
    check(!op_armed() && pat_cur[0] == sl && !pat_has(0, sl) && trk[0].step[3].time != ST_NOTE && !strncmp(ui.msg, "NEW T1 ", 7),
          "... HOME, then SAVE on \"EMPTY T1 n?\": T1 plays a new empty pattern, \"NEW T1 n\"");
    tap(B_HOME);                                        /* (the page let go) */
    /* SONG, an empty scene: the knob creates it */
    for (k = 0; k < 8u; k++)
        trk[0].step[k].time = ST_NOTE, trk[0].step[k].n = 1, trk[0].step[k].note[0] = (uint8_t)(60 + k);
    pat_launch(0, 0, PW_END);
    reset_ui();
    song.sel = 0;
    for (c = LAY_NSCN - 1u; c > 0u && project_used(c); c--)
        ;
    op_enter(SCR_SONG);
    ui.row[SCR_SONG] = (uint8_t)song_row_of(SG_SCENE, c);
    frames(3);
    song_refresh(1);
    check(!project_used(c) && !((sg.scn >> c) & 1u), "patnew: SONG, scene N empty");
    turn(EN_K1, 1);
    check(project_used(c) && pat_scene_refs(c, refs) && strcmp(ui.msg, "EMPTY SCENE") != 0, "... KNOB 1 on it: the scene is created (the loop stored), no \"EMPTY SCENE\"");
    frames(60);
    check(pat_scene_refs(c, refs) && !op_armed(), "... its reference moved on to a pattern that exists: nothing asked");
    /* a pattern not filled yet: the end of the knob's list */
    for (k = 0; k < 24u && !op_armed(); k++) {
        turn(EN_K1, 1);
        frames(60);
    }
    check(clone_asked() && !strncmp(ui.arm_q, "CLONE T1 ", 9), "... KNOB 1 on to the first free pattern, resting there: the CLONE question");
    sl = (uint32_t)(ui.arm_k & 15u);
    check(!pat_has(0, sl), "... asked, nothing created yet");
    tap(B_SAVE);
    check(pat_has(0, sl) && pat_scene_refs(c, refs) && refs[0] == sl && !op_armed(), "... YES: the pattern cloned and the scene's T1 names it");
    /* EMPTY from the scene: the reference only */
    for (k = 0; k < 24u && !op_armed(); k++) {
        turn(EN_K1, 1);
        frames(60);
    }
    if (clone_asked()) {
        sl = (uint32_t)(ui.arm_k & 15u);
        tap(B_HOME);
        tap(B_SAVE);
        check(!pat_has(0, sl) && pat_scene_refs(c, refs) && refs[0] == sl && pat_cur[0] != sl, "... EMPTY: the scene names the new pattern, the track's own pattern unchanged");
    } else {
        check(1, "... (no further free pattern)");
    }
    /* a cancelled question: the reference stays */
    for (k = 0; k < 24u && !op_armed(); k++) {
        turn(EN_K1, 1);
        frames(60);
    }
    if (clone_asked()) {
        uint8_t was[NTRK];
        memcpy(was, refs, sizeof was);
        sl = (uint32_t)(ui.arm_k & 15u);
        tap(B_HOME);
        tap(B_HOME);
        frames(10);
        check(pat_scene_refs(c, refs) && refs[0] != sl && !pat_has(0, sl), "... HOME twice: nothing created, the reference not set");
    }
    /* playing: the scene is created in the arena, as a store while playing */
    reset_ui();
    song.sel = 0;
    for (c = LAY_NSCN - 1u; c > 0u && project_used(c); c--)
        ;
    op_enter(SCR_SONG);
    ui.row[SCR_SONG] = (uint8_t)song_row_of(SG_SCENE, c);
    song.playing = 1;
    frames(3);
    song_refresh(1);
    turn(EN_K1, 1);
    check(project_used(c) && strcmp(ui.msg, "EMPTY SCENE") != 0, "patnew: SONG, playing: KNOB 1 on an empty scene creates it too");
    song.playing = 0;
    reset_ui();
}
#endif
