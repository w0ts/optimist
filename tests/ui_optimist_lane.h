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
