/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, the user's ruling of 2026-10-13: on the MIXER, HOME held + a key MUTES THE SEQUENCER
 * (seq_mute.c: the 16 drum keys the 16 lanes, the 4 black keys T1 T2 T3 DR) instead of picking the lane; every other
 * screen keeps the pick, STEP the clear. The mixer's MUTE card (P_MUTE, dlm_mute) is another thing and stays. The row
 * shows it (grey steps, an X), the keys light it with HOME held. Included after ui_optimist_mute.h. */
static uint32_t smute_black(uint32_t b)                 /* black key b (0..3): F# G# A# C# */
{
    static const uint8_t K[4] = {1, 3, 5, 8};
    return K[b];
}
static void smute_clear(void)
{
    uint32_t i;
    smute_trk = 0;
    smute_lane = 0;
    for (i = 0; i < NTRK; i++)
        trk[i].p[P_MUTE] = 0;
    for (i = 0; i < DRUM_LANES; i++)
        dlm_set_mute(i, 0);
}
static void smute_tests(void)
{
    uint32_t f, d, y, i;
    smute_clear();
    reset_ui();
    song.sel = TRK_DRUM;
    lane_select(0);
    go_home();
    frames(2);
    press(B_OCTDN);
    key(key_of_lane(2));                                /* (the third drum key: lane 2) */
    check(smute_lane_on(2) && smute_lane == 4u && smute_trk == 0u && lane_selected() == 0u && ui.scr == SCR_HOME,
          "the mixer, HOME held + the third drum key: lane 2's sequencer muted, no pick, still the mixer");
    check(!dlm_muted(2) && !TDRUM->p[P_MUTE], "... the audio mute (the MUTE card) untouched");
    key(key_of_lane(2));
    check(!smute_lane_on(2), "... the key again: unmuted");
    key(key_of_lane(15));
    key(smute_black(1));
    check(smute_lane == 0x8000u && smute_trk == 2u, "the 16th drum key: lane 15; the second black key (G#): T2");
    key(smute_black(3));
    check(smute_trk == 10u, "the fourth black key (C#): DR");
    key(10);                                            /* (a black key past the four: nothing) */
    key(13);                                            /* (the second octave's black keys too) */
    check(smute_trk == 10u && smute_lane == 0x8000u, "another black key: nothing");
    release(B_OCTDN);
    check(ui.scr == SCR_HOME, "HOME let go after keys: no NO (the mixer stays)");
    smute_clear();
    song.sel = 0;
    lane_select(0);
    reset_ui();
    frames(2);
    press(B_OCTDN);
    key(key_of_lane(3));
    check(smute_lane_on(3) && lane_selected() == 0u, "a synth track selected: the same, whatever the track");
    release(B_OCTDN);
    smute_clear();

    /* the rows: the steps grey, the X */
    song.sel = 0;
    reset_ui();
    go_home();
    for (i = 0; i < 4u; i++)
        dstep_set(&TDRUM->dstep[i], 0, LV_NORM, 0);
    frames(2);
    y = mx_row_y(MXR_T1 + 1u);
    check(!px_in(15, y + 18u, 8, 14, C_GRAY), "T2 not muted: no X");
    smute_set_trk(1, 1);
    frames(2);
    ppm("opt-smute-t2");
    check(px_in(15, y + 18u, 8, 14, C_GRAY) && !px_in(6, y + 18u, 8, 14, C_WARN), "T2's sequencer muted: the X, not the audio M badge");
    smute_set_trk(1, 0);
    frames(2);
    check(!px_in(15, y + 18u, 8, 14, C_GRAY), "... unmuted: the X gone");

    /* the keys' lights, HOME held */
    smute_clear();
    smute_set_lane(2, 1);
    smute_set_trk(0, 1);
    reset_ui();
    go_home();
    frames(2);
    keys_lit(&f, &d);
    check(!f && !d, "the mixer, no HOME: the keys dark");
    press(B_OCTDN);
    frames(HOLD_FRAMES);
    keys_lit(&f, &d);
    check(f == ((1u << key_of_lane(2)) | (1u << smute_black(0))) && (d | f) == (keys_white(16) | (1u << 1) | (1u << 3) | (1u << 5) | (1u << 8) | (ACC_UI ? 1u << DACC_KEY : 0u)),
          "HOME held: the 16 drum keys, the 4 black keys and the last upper key lit, the muted lane and T1 full, the rest dim");
    release(B_OCTDN);
    frames(2);
    keys_lit(&f, &d);
    check(!f && !d, "HOME let go: the keys dark again");
    smute_clear();

    /* the other screens keep the pick; STEP picks too (HOME + a key), SEQ + a key there clears */
    reset_ui();
    song.sel = TRK_DRUM;
    lane_select(0);
    tap(B_EDIT);
    check(ui.scr == SCR_SOUND, "(the drum track's EDIT: the SOUND rows)");
    press(B_OCTDN);
    key(key_of_lane(4));
    release(B_OCTDN);
    check(lane_selected() == 4u && smute_lane == 0u && smute_trk == 0u, "SOUND: HOME held + a drum key still picks the lane, mutes nothing");
    reset_ui();
    song.sel = TRK_DRUM;
    op_enter(SCR_STEP);
    frames(2);
    dstep_set(&TDRUM->dstep[4], lane_selected(), LV_NORM, 0);
    press(B_SEQ);
    key(key_of_lane(4));
    release(B_SEQ);
    check(smute_lane == 0u && smute_trk == 0u && !dstep_has(&TDRUM->dstep[4], lane_selected()), "STEP: SEQ held + a step clears it, mutes nothing");
    dstep_set(&TDRUM->dstep[4], 4, LV_NORM, 0);
    press(B_OCTDN);
    key(key_of_lane(4));
    release(B_OCTDN);
    check(smute_lane == 0u && smute_trk == 0u && lane_selected() == 4u && dstep_has(&TDRUM->dstep[4], 4), "STEP: HOME held + a key picks the lane, clears and mutes nothing");
    smute_clear();
    song.sel = 0;
    reset_ui();
}
