/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, the user's rulings of 2026-10-14 (the mixer's MUTE card is gone) and 2026-10-16 (one mute, the
 * sequencer's; the shifts' language: SHIFT is off): SHIFT (OCT- held) on the volume card (KNOB 1) toggles the row's mute,
 * T1..T3 and DR the track's sequencer mute (seq_mute.c: the row's M badge), a lane its own; once per hold, the value
 * untouched. A sound mute an older project left (P_MUTE, drum_mix.c dlm_mute) shows as muted, and the toggle clears it.
 * MASTER has none. Included after ui_optimist_algo.h. */
static void mute_card(uint32_t r, cell_t *c)            /* row r's KNOB 1 on its first set */
{
    mx.set = 0;
    mix_cell(r, 0, c);
}
static void mute_tests(void)
{
    cell_t c;
    uint32_t ok, y, i;
    int16_t lv;
    for (i = 0; i < NTRK; i++)
        trk[i].p[P_MUTE] = 0;                           /* (everything heard: an earlier test may have left a mute) */
    for (i = 0; i < DRUM_LANES; i++)
        dlm_set_mute(i, 0);
    reset_ui();
    song.octave = 0;                                    /* (reset_ui keeps the octave: an earlier test may have left one) */
    song.sel = 1;
    frames(2);
    mx.set = 0;
    lv = trk[1].p[P_LEVEL];
    press(B_OCTDN);
    turn(EN_K1, 1);
    turn(EN_K1, 1);
    release(B_OCTDN);
    frames(2);
    mute_card(MXR_T1 + 1u, &c);
    y = mx_row_y(MXR_T1 + 1u);
    ok = smute_trk_on(1) && !trk[1].p[P_MUTE] && trk[1].p[P_LEVEL] == lv && c.label && !strcmp(c.label, "VOLUME") &&
         px_in(6, y + 18u, 8, 14, C_WARN);
    check(ok, "the mixer, T2's VOLUME card with SHIFT: muted once (two detents, the sequencer mute), the volume kept, the row's M badge");
    check(song.octave == 0, "... and SHIFT used: the octave unchanged");
    press(B_OCTDN);
    turn(EN_K1, 1);
    release(B_OCTDN);
    check(!smute_trk_on(1), "... SHIFT again: unmuted");
    press(B_OCTUP);
    turn(EN_K1, 1);
    release(B_OCTUP);
    check(!smute_trk_on(1) && !trk[1].p[P_MUTE], "... SHIFT2 + the knob: no mute any more (SHIFT2 is pick)");
    trk[1].p[P_LEVEL] = lv;
    turn(EN_K1, 1);
    check(!smute_trk_on(1) && trk[1].p[P_LEVEL] != lv, "... a plain turn: the volume, no mute");
    trk[1].p[P_LEVEL] = lv;
    song.sel = TRK_DRUM;
    frames(2);
    ui.row[SCR_HOME] = MXR_DR;
    press(B_OCTDN);
    turn(EN_K1, 1);
    release(B_OCTDN);
    check(smute_trk_on(TRK_DRUM) && !TDRUM->p[P_MUTE], "DR: SHIFT on its VOLUME card mutes the drum track");
    smute_trk = 0;
    ui.row[SCR_HOME] = MXR_LANE0 + 2u;
    lane_select(2);
    frames(2);
    press(B_OCTDN);
    turn(EN_K1, 1);
    release(B_OCTDN);
    check(smute_lane_on(2) && !smute_lane_on(1) && !smute_lane_on(3) && !smute_trk_on(TRK_DRUM) && !dlm_muted(2),
          "a lane's LEVEL card with SHIFT: that lane muted, the track heard");
    smute_lane = 0;
    dlm_set_mute(2, 1);                                 /* (an older project's sound mute: shown, and the toggle clears it) */
    check(mx_muted(MXR_LANE0 + 2u), "a lane's sound mute from an older project: shown as muted");
    press(B_OCTDN);
    turn(EN_K1, 1);
    release(B_OCTDN);
    check(!dlm_muted(2) && !smute_lane_on(2) && !mx_muted(MXR_LANE0 + 2u), "... SHIFT + the knob: heard, the sound mute gone with it");
    mute_card(MXR_MASTER, &c);
    check(!c.label || strcmp(c.label, "MUTE"), "MASTER: no MUTE card");
    reset_ui();
    ui.row[SCR_HOME] = MXR_MASTER;
    frames(2);
    key(WK(0));
    check(ly_lock == LY_STEP, "the mixer on MASTER: the keys reach the UI only (silent)");
    reset_ui();
    op_enter(SCR_FX);
    frames(2);
    tap(B_SAVE);
    check(ui.scr == SCR_PROJECT, "SAVE tapped on the master FX page: PROJECT (SAVE works on every page but SEQ)");
    /* a muted row's VOLUME / LEVEL turned: unmuted (the user, 2026-10-15); a cursor move or the SHIFT toggle itself: not */
    reset_ui();
    song.sel = 1;
    frames(2);
    ui.row[SCR_HOME] = MXR_T1 + 1u;
    smute_set_trk(1, 1);
    lv = trk[1].p[P_LEVEL];
    turn(EN_SELECT, 1);
    turn(EN_SELECT, -1);
    ui.row[SCR_HOME] = MXR_T1 + 1u;
    frames(2);
    check(smute_trk_on(1), "a muted track: a cursor move does not unmute it");
    turn(EN_K1, 1);
    check(!smute_trk_on(1) && trk[1].p[P_LEVEL] != lv, "a muted track: its VOLUME turned unmutes it");
    smute_set_trk(1, 1);
    press(B_OCTDN);
    turn(EN_K1, 1);
    release(B_OCTDN);
    check(!smute_trk_on(1), "... SHIFT + the knob toggles (muted -> unmuted), no reset of the volume");
    trk[1].p[P_LEVEL] = (int16_t)(trk[1].p[P_LEVEL] == 0 ? 100 : trk[1].p[P_LEVEL]);
    song.sel = TRK_DRUM;
    frames(2);
    ui.row[SCR_HOME] = MXR_LANE0 + 2u;
    lane_select(2);
    frames(2);
    mx.set = 0;
    dl.ofs[2][DE_LEVEL] = 0;
    smute_set_lane(2, 1);
    turn(EN_K1, -1);
    check(!smute_lane_on(2), "a muted lane: its LEVEL turned unmutes it");
    smute_lane = 0;
    reset_ui();
    song.sel = 0;
    reset_ui();
}
