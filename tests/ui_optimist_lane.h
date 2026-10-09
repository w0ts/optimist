/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, the drum lane: a change previews its sound only when stopped, the pick's key is silent
 * while playing (docs/UI-OPTIMIST-DESIGN.md section 11.6). Included after the helpers. */

static void lane_transport(int play)                    /* start / stop, settled */
{
    if (!!song.playing != !!play) {
        transport_req = play ? 1 : 2;
        frames(4);
    }
}
static void lane_preview_tests(void)
{
    uint32_t k;
    reset_ui();
    lane_transport(0);
    song.sel = TRK_DRUM;
    frame();
    go_home();
    frame();
    aud_lanes = 0;
    turn(EN_ALGO, 1);                                   /* DR -> its first lane (kick) */
    check(mx_row() == MXR_LANE0 && lane_selected() == 0u, "the mixer, ALGORITHM past DR: the kick lane");
    lane_select(3);                                     /* (from another lane, so that the walk is a change) */
    ui.row[SCR_HOME] = MXR_LANE0 + 3u;
    frame();
    aud_lanes = 0;
    turn(EN_ALGO, 1);
    check(lane_selected() == 4u && aud_lanes == 1u << 4, "stopped: a lane selected previews its sound once");
    frame();
    check(aud_lanes == 0u, "... played by the next block, once");
    lane_transport(1);
    aud_lanes = 0;
    turn(EN_ALGO, 1);
    check(lane_selected() == 5u && aud_lanes == 0u, "playing: a lane selected, silent");
    lane_transport(0);
    song.sel = 0;
    go_home();
    frame();
    aud_lanes = 0;
    turn(EN_ALGO, 1);
    check(song.sel == 1u && aud_lanes == 0u, "a synth track selected: no preview");
    /* the pick's key: SEQ held + a drum key on STEP */
    song.sel = TRK_DRUM;
    frame();
    tap(B_SEQ);
    lane_select(0);
    k = WK(6);
    fm1_in.buttons |= BT(B_SEQ);
    frame();
    aud_lanes = 0;
    kdown(k);
    check(lane_selected() == 6u && ly_lock == LY_PLAY && kb_kind[k] != KS_UI && aud_lanes == 0u,
          "stopped, SEQ held + a drum key: the lane picked, its key plays (the preview), no second one");
    kup(k);
    lane_transport(1);
    lane_select(0);
    frame();
    kdown(k);
    check(lane_selected() == 6u && ly_lock == LY_STEP && kb_kind[k] == KS_UI && aud_lanes == 0u,
          "playing, SEQ held + a drum key: the lane picked silently (the key reaches the UI only)");
    {
        uint32_t i, any = 0;
        for (i = 0; i < trk_len(TSEL); i++)
            any |= dstep_has(&TSEL->dstep[i], 6u);
        kup(k);
        check(!any, "... and no step set by it");
    }
    fm1_in.buttons &= ~BT(B_SEQ);
    frame();
    check(ly_lock == LY_STEP && ui.scr == SCR_STEP, "SEQ let go: the keys are steps again");
    tap(B_HOME);                                        /* the mixer: HOME held + a drum key, playing */
    lane_select(0);
    fm1_in.buttons |= BT(B_HOME);
    frame();
    kdown(WK(2));
    check(lane_selected() == 2u && ly_lock == LY_STEP && kb_kind[WK(2)] == KS_UI && ui.row[SCR_HOME] == MXR_LANE0 + 2u,
          "playing, HOME held + a drum key on the mixer: the lane picked silently, the cursor on its row");
    kup(WK(2));
    fm1_in.buttons &= ~BT(B_HOME);
    frame();
    lane_transport(0);
    song.sel = 0;
    lane_select(0);
    reset_ui();
}

/* STEP's pages: PATTERN first on every track (the user: "the first SEQ menu should have the length of the pattern"),
 * then the drum track's 16 lanes (SELECT walks them: the lane encoder on STEP) or a synth track's ARP rows; SEQ tapped
 * again pages round; ALGORITHM still switches tracks there */
static void step_pages_tests(void)
{
    cell_t c;
    uint32_t n, r;
    reset_ui();
    lane_transport(0);
    song.sel = 0;
    frame();
    tap(B_SEQ);
    step_cell(0, 0, &c);
    check(ui.scr == SCR_STEP && ui.row[SCR_STEP] == 0 && c.d == &TP[P_SLEN], "a synth track's STEP opens on PATTERN: LEN first");
    n = SCR->rows();
    for (r = 1; r < n; r++)
        tap(B_SEQ);
    check(n >= 2u && ui.row[SCR_STEP] == n - 1u, "SEQ tapped again: STEP's next page (ARP, ARP 2)");
    tap(B_SEQ);
    check(ui.row[SCR_STEP] == 0, "... round, back to PATTERN");
    turn(EN_ALGO, 1);
    check(song.sel == 1 && ui.scr == SCR_STEP, "ALGORITHM on STEP: the next track, as ever");
    turn(EN_ALGO, 1);
    turn(EN_ALGO, 1);
    check(song.sel == TRK_DRUM && ui.row[SCR_STEP] == 0, "... the drum track: STEP on PATTERN");
    ui.row[SCR_STEP] = 5;
    tap(B_HOME);
    tap(B_SEQ);
    step_cell(0, 0, &c);
    check(ui.row[SCR_STEP] == 0 && c.d == &TP[P_SLEN] && SCR->rows() == STP_LANE0 + DRUM_LANES,
          "the drum track's STEP opens on PATTERN (LEN DIV SWING GATE), then its 16 lanes");
    ui.force = 1;
    frame();
    ppm("opt-step-first-page");
    lane_select(0);
    aud_lanes = 0;
    turn(EN_SELECT, 1);
    step_cell(ui.row[SCR_STEP], 0, &c);
    check(ui.row[SCR_STEP] == STP_LANE0 && lane_selected() == 0u && c.label && !strcmp(c.label, "LEVEL"),
          "SELECT from PATTERN: the first lane's page (its LEVEL TUNE DECAY REV), the lane selected");
    turn(EN_SELECT, 3);
    check(lane_selected() == 3u && aud_lanes == 1u << 3, "SELECT over the lanes: lane_sel follows, previewed (stopped)");
    ui.force = 1;
    frame();
    ppm("opt-step-lane");
    lane_transport(1);
    aud_lanes = 0;
    turn(EN_SELECT, 1);
    check(lane_selected() == 4u && aud_lanes == 0u, "... playing: silent");
    lane_transport(0);
    kdown(WK(2));                                       /* a step held: SELECT is NUDGE, the one exception */
    turn(EN_SELECT, 1);
    kup(WK(2));
    check(lane_selected() == 4u && ui.row[SCR_STEP] == STP_LANE0 + 4u, "a step held: SELECT is its nudge, not the lane");
    track_defaults_steps(TSEL);
    song.sel = 0;
    lane_select(0);
    reset_ui();
}
