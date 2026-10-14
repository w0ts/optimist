/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, the uniform-UI changes (the user's rulings on the audit, 2026-10-13; docs/UI-OPTIMIST-DESIGN.md
 * sections 2, 2.1). Each test says what is gone and what is there. Included after ui_optimist_algotrack.h. */
#if FELUCCA_ARRANGER
static void un_song_rec_tests(void)                     /* #16a: SONG's MODE row, KNOB 2 = the SONG REC card: picked by a turn, armed by YES */
{
    cell_t c;
    uint32_t mode;
    reset_ui();
    song.sel = 0;
    song.playing = 0;
    transport_req = 0;
    op_enter(SCR_SONG);
    mode = song_base_mode();
    ui.row[SCR_SONG] = (uint8_t)mode;
    arrangement.count = 2;                              /* (a song of parts: arming asks "Record over song?") */
    frame();
    song_cell(mode, 1, &c);
    check(c.kind == CK_ACT, "SONG MODE row: the SONG REC card is an action card");
    turn(EN_K2, 1);
    turn(EN_K2, -1);
    check(srec == 0 && !op_armed() && ui.hot == 1u, "KNOB 2 turned: the card picked, SONG REC not armed, nothing asked");
    tap(B_SAVE);
    check(srec == 0 && op_armed() && !strcmp(ui.arm_q, "RECORD OVER SONG?"), "YES on it: asks \"Record over song?\"");
    tap(B_SAVE);
    check(srec == 1 && !op_armed(), "... YES: SONG REC armed");
    turn(EN_K2, 1);
    check(srec == 1, "KNOB 2 turned with SONG REC on: it stays on (a turn never stops it either)");
    tap(B_SAVE);
    check(srec == 0, "YES on the card again: SONG REC off");
    ui.row[SCR_SONG] = (uint8_t)mode;
    press(B_SAVE);
    tap(B_REC);
    release(B_SAVE);
    check(op_armed() && !strcmp(ui.arm_q, "RECORD OVER SONG?"), "SAVE + REC unchanged: asks too");
    tap(B_HOME);
    arrangement.count = 1;
    reset_ui();
}
#endif
static void un_scope_tests(void)                        /* #12: SCOPE's source = ALGORITHM's track, MASTER = the master; SELECT has no source to pick */
{
    reset_ui();
    song.sel = 2;
    go_home();
    frame();
    tap(B_HOME);
    check(ui.scr == SCR_SCOPE && scope_src == 3u && SCR->rows() == 1u, "SCOPE: opened on the mixer's track row, the source is that track (T3); no source row");
    turn(EN_SELECT, 3);
    turn(EN_SELECT, -3);
    frames(2);
    check(ui.scr == SCR_SCOPE && scope_src == 3u && ui.row[SCR_SCOPE] == 0u, "SELECT on SCOPE: no source picked (the old source row is gone)");
    turn(EN_ALGO, -1);
    frames(2);
    check(song.sel == 1u && scope_src == 2u, "ALGORITHM: the source follows the selected track (T2)");
    turn(EN_ALGO, -5);
    frames(2);
    check(ui.scr == SCR_SCOPE && ui.master && scope_src == 0u, "ALGORITHM to MASTER: the master output, SCOPE stays (not the master FX screen)");
    press(B_EDIT);
    frames(HOLD_FRAMES);
    release(B_EDIT);
    frames(2);
    check(ui.scr == SCR_SCOPE && ui.master && scope_src == 0u, "... a layer held and let go on it: still the master");
    turn(EN_ALGO, 1);
    frames(2);
    check(ui.scr == SCR_SCOPE && !ui.master && scope_src == 1u, "ALGORITHM right of MASTER: T1");
    turn(EN_ALGO, -1);
    tap(B_HOME);
    check(ui.scr == SCR_HOME && !ui.master && (frame(), scope_src == 0u), "HOME: the mixer, the master flag gone");
    go_home();
    ui.row[SCR_HOME] = MXR_MASTER;                      /* the mixer's MASTER row, HOME: SCOPE on the master */
    frame();
    tap(B_HOME);
    check(ui.scr == SCR_SCOPE && ui.master && scope_src == 0u, "SCOPE opened from the mixer's MASTER row: the master");
    tap(B_HOME);
    reset_ui();
}
static void un_drum_keys_tests(void)                    /* #13: the drum track's upper (black) keys play and pick nothing; the layer meanings stay */
{
    static const uint8_t BLACK[5] = {1, 3, 5, 8, 10};
    uint32_t i, ok = 1, ok2 = 1;
    reset_ui();
    lane_transport(0);
    song.sel = TRK_DRUM;
    go_home();
    lane_select(0);
    frame();
    for (i = 0; i < 5u; i++) {
        uint32_t k = BLACK[i];
        aud_lanes = 0;
        lane_select(0);
        kdown(k);
        ok &= kb_kind[k] == KS_NONE && aud_lanes == 0u && lane_selected() == 0u;
        kup(k);
    }
    lane_select(0);
    check(ok, "the drum track: an upper key (key 1 too) plays nothing and picks no lane (it used to play the lane on its left)");
    if (ACC_UI) {                                       /* the last upper key is the ACCENT lane's: it plays (silently) and picks it */
        aud_lanes = 0;
        kdown(DACC_KEY);
        ok = kb_kind[DACC_KEY] == KS_DRUM && aud_lanes == 0u && lane_selected() == DRUM_ACC;
        kup(DACC_KEY);
        lane_select(0);
        check(ok, "... the last upper key, the ACCENT lane's, is the exception");
    }
    aud_lanes = 0;
    kdown(WK(3));
    check(kb_kind[WK(3)] == KS_DRUM, "... a white key still plays its lane");
    kup(WK(3));
    lane_select(0);
    op_enter(SCR_SOUND);
    frame();
    press(B_OCTDN);
    for (i = 0; i < 5u; i++) {
        lane_select(0);
        kdown(BLACK[i]);
        kup(BLACK[i]);
        ok2 &= lane_selected() == 0u;
    }
    release(B_OCTDN);
    lane_select(0);
    check(ok2, "HOME held + an upper key off the mixer: no lane picked either (the last upper key: the ACCENT lane)");
    press(B_OCTDN);
    kdown(WK(5));
    kup(WK(5));
    release(B_OCTDN);
    check(lane_selected() == 5u, "... HOME + a white key still picks its lane");
    go_home();
    frame();
    lane_select(0);
    smute_lane = 0;
    press(B_OCTDN);
    kdown(BLACK[0]);
    kup(BLACK[0]);
    release(B_OCTDN);
    check(smute_trk_on(0), "the mixer: HOME held + upper key 1 is still T1's sequencer mute");
    smute_trk = 0;
    press(B_GLO);
    frames(HOLD_FRAMES);
    kdown(BLACK[0]);
    check(kb_kind[BLACK[0]] == KS_UI, "GLO held + an upper key on the drum track: still the MIX layer's (FX bypass / fills)");
    kup(BLACK[0]);
    release(B_GLO);
    frames(2);
    reset_ui();
    song.sel = 0;
}
#if FELUCCA_PATTERNS
static void un_pat_tests(void)                          /* #15: the patterns layer, SAVE + n stores, HOME + n clears; copy = launch, then SAVE + target */
{
    track_t *t = &trk[0];
    uint32_t k;
    reset_ui();
    song.sel = 0;
    song.playing = 0;
    transport_req = 0;
    for (k = 0; k < PAT_N; k++)
        pat_write(0, k, 0);
    for (k = 0; k < 8u; k++)
        t->step[k].time = ST_NOTE, t->step[k].n = 1, t->step[k].note[0] = (uint8_t)(50 + k);
    press(B_LFO);
    frames(HOLD_FRAMES);
    for (k = 7; k <= 8u; k++) {                         /* black 7 (copy) and 8 (clear) held + white 1: modifiers no more */
        fm1_in.notes |= 1u << (k == 7u ? 15 : 17);
        frame();
        fm1_in.notes &= ~(1u << (k == 7u ? 15 : 17));
        frame();
    }
    check(!pat_has(0, 0) && !op_armed(), "LFO held + black 7 / 8: no copy, no clear modifier (nothing)");
    press(B_SAVE);
    key(WK(0));
    release(B_SAVE);
    check(pat_has(0, 0), "LFO held, SAVE + key 1: stored into slot 1");
    key(WK(0));                                         /* launch slot 1 (stopped: loaded) */
    check(pat_cur[0] == 0, "LFO held + key 1: slot 1 launched");
    press(B_SAVE);
    key(WK(3));
    release(B_SAVE);
    check(pat_has(0, 3) && !op_armed(), "COPY = launch the source, then SAVE + the target key: slot 4 holds the copy");
    press(B_SAVE);
    key(WK(3));                                         /* over a used slot: asked */
    release(B_SAVE);
    check(op_armed(), "SAVE + a used slot's key: asks before it overwrites");
    op_disarm();
    release(B_LFO);
    frames(2);
    for (k = 0; k < PAT_N; k++)
        pat_write(0, k, 0);
    pat_cur[0] = PAT_NONE;
    pat_req[0] = PAT_NONE;
    track_defaults_steps(t);
    reset_ui();
}
#endif
static void un_step_presets_tests(void)                 /* #1 (kept) and the accident: PRESETS on STEP scrolls the window; HOME + PRESETS does nothing */
{
    uint32_t e0, p0, pg0;
    reset_ui();
    song.sel = 0;
    song.playing = 0;
    transport_req = 0;
    TSEL->p[P_SLEN] = 48;                               /* (three windows) */
    tap(B_SEQ);
    frames(2);
    e0 = TSEL->eng_req, p0 = TSEL->preset;
    st.page = 0;
    turn(EN_PRESET, 1);
    check(st.page == 1u && TSEL->eng_req == e0 && TSEL->preset == p0, "STEP: PRESETS scrolls the 16-step window (kept)");
    pg0 = st.page;
    press(B_OCTDN);
    turn(EN_PRESET, 1);
    turn(EN_PRESET, -1);
    release(B_OCTDN);
    frames(2);
    check(TSEL->eng_req == e0 && TSEL->preset == p0 && st.page == pg0 && ui.scr == SCR_STEP && !op_armed(),
          "STEP: HOME + PRESETS does nothing (no engine change, no scroll, no NO)");
    TSEL->p[P_SLEN] = 16;
    reset_ui();
}
static void uniform_tests(void)
{
    un_step_presets_tests();
    un_drum_keys_tests();
    un_scope_tests();
#if FELUCCA_ARRANGER
    un_song_rec_tests();
#endif
#if FELUCCA_PATTERNS
    un_pat_tests();
#endif
}
