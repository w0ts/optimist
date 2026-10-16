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
static uint8_t ln_before[sizeof(((track_t *)0)->dstep)];   /* (the pattern before the pick: an earlier test may leave steps) */
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
    check(lane_selected() == 4u && aud_lanes == 0u, "stopped, the mixer's ALGORITHM walk: silent (the user, 2026-10-10)");
    op_enter(SCR_STEP);                                 /* STEP, the clip editor: a lane change previews, stopped */
    ui.row[SCR_STEP] = (uint8_t)STP_PAT;              /* (the LANE page: the selected lane's; ALGORITHM picks the lane) */
    frame();
    aud_lanes = 0;
    turn(EN_ALGO, 1);
    check(lane_selected() == 5u && aud_lanes == 1u << 5, "stopped, STEP: a lane picked (ALGORITHM) previews its sound once");
    frame();
    check(aud_lanes == 0u, "... played by the next block, once");
    go_home();
    frame();
    ui.row[SCR_HOME] = MXR_LANE0 + 4u;
    lane_select(4);
    frame();
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
    /* the pick's key: a lane's black key on STEP (2026-10-16: no SHIFT) */
    song.sel = TRK_DRUM;
    frame();
    tap(B_SEQ);
    lane_select(0);
    k = BK(6);
    frame();
    aud_lanes = 0;
    kdown(k);
    check(lane_selected() == 6u && ly_lock == LY_STEP && kb_kind[k] == KS_UI && aud_lanes == 1u << 6,
          "stopped, a lane's black key on STEP: the lane picked, heard (the preview)");
    kup(k);
    lane_transport(1);
    lane_select(0);
    frame();
    memcpy(ln_before, TSEL->dstep, sizeof ln_before);
    kdown(k);
    check(lane_selected() == 6u && ly_lock == LY_STEP && kb_kind[k] == KS_UI && aud_lanes == 0u,
          "playing, a lane's black key on STEP: the lane picked silently (the key reaches the UI only)");
    {
        kup(k);
        check(!memcmp(ln_before, TSEL->dstep, sizeof ln_before), "... and no step set by it");
    }
    frame();
    check(ly_lock == LY_STEP && ui.scr == SCR_STEP, "... the white keys are steps still");
    tap(B_HOME);                                        /* the mixer: SHIFT held + a drum key, playing */
    lane_select(0);
    fm1_in.buttons |= BT(B_OCTDN);
    frame();
    kdown(BK(2));
    check(lane_selected() == 0u && ly_lock == LY_STEP && kb_kind[BK(2)] == KS_UI && smute_lane_on(2),
          "playing, SHIFT held + a lane's key on the mixer: the lane's sequencer muted (the key silent), no pick (2026-10-13)");
    smute_lane = 0;
    kup(BK(2));
    fm1_in.buttons &= ~BT(B_OCTDN);
    frame();
    lane_transport(0);
    song.sel = 0;
    lane_select(0);
    reset_ui();
}

/* STEP's pages: PATTERN first on every track (the user: "the first SEQ menu should have the length of the pattern"),
 * then the drum track's 16 lanes (SELECT walks them: the lane encoder on STEP) or a synth track's ARP rows; SEQ tapped
 * again pages on, past the last the first; ALGORITHM still switches tracks there */
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
    check(ui.row[SCR_STEP] == n - 1u, "SEQ tapped again: STEP's next page (the tools), the last with none");
    tap(B_SEQ);
    check(ui.row[SCR_STEP] == 0u, "... past the last page: PATTERN again (a button again wraps)");
    ui.row[SCR_STEP] = 0;
    turn(EN_ALGO, 1);
    check(song.sel == 1 && ui.scr == SCR_STEP, "ALGORITHM on STEP: the next track, as ever");
    turn(EN_ALGO, 1);
    turn(EN_ALGO, 1);
    check(song.sel == TRK_DRUM && ui.row[SCR_STEP] == 0, "... the drum track: STEP on PATTERN");
    ui.row[SCR_STEP] = 5;
    tap(B_HOME);
    tap(B_SEQ);
    check(ui.row[SCR_STEP] == SCR->rows() - 1u && SCR->rows() == STP_PAT + 1u + STP_TL,
          "the drum track's STEP keeps the page it was left on (2026-10-13), a row it lacks: the nearest, the last");
    ui.row[SCR_STEP] = 0;
    step_cell(0, 0, &c);
    check(c.d == &TP[P_SLEN], "the drum track's STEP, PATTERN (LEN DIV SWING GATE), no LANE page after it (sound values are not the sequencer's, 2026-10-14)");
    ui.force = 1;
    frame();
    ppm("opt-step-first-page");
    lane_select(0);
    aud_lanes = 0;
    turn(EN_SELECT, 1);
    step_cell(ui.row[SCR_STEP], 0, &c);
    check(lane_selected() == 0u && aud_lanes == 0u && !(c.label && !strcmp(c.label, "LEVEL")),
          "SELECT from PATTERN: no LANE page (LEVEL TUNE DECAY REV is not on STEP any more), the lane unchanged");
    {
        uint8_t l0 = ui.lane;
        turn(EN_SELECT, 5);
        turn(EN_SELECT, -9);
        turn(EN_SELECT, 9);
        turn(EN_SELECT, -(int32_t)STP_TL);             /* (past LANE: TOOLS, EUCLID, op_tools.c) */
        check(lane_selected() == 0u && ui.lane == l0 && ui.row[SCR_STEP] == STP_PAT,
              "SELECT on the drum STEP: only its pages (PATTERN), never the lane (the user, 2026-10-12)");
    }
    turn(EN_ALGO, 3);
    step_cell(ui.row[SCR_STEP], 1, &c);
    check(lane_selected() == 2u && ui.lane && aud_lanes == 1u << 2 && ui.row[SCR_STEP] == STP_PAT,
          "ALGORITHM on the drum STEP: the lane (DR, then the lanes), previewed (stopped)");
    ui.force = 1;
    frame();
    ppm("opt-step-lane");
    lane_transport(1);
    aud_lanes = 0;
    turn(EN_ALGO, 2);
    check(lane_selected() == 4u && aud_lanes == 0u, "... playing: silent");
    lane_transport(0);
    kdown(WK(2));                                       /* a step held: SELECT is NUDGE, the one exception */
    turn(EN_SELECT, 1);
    kup(WK(2));
    check(lane_selected() == 4u && ui.row[SCR_STEP] == STP_PAT, "a step held: SELECT is its nudge, not the lane");
    track_defaults_steps(TSEL);
    song.sel = 0;
    lane_select(0);
    reset_ui();
}
