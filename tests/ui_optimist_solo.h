/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c: solo on every mixer row but the returns, the master and the ACCENT lane. SHIFT + SHIFT2 (OCT- and OCT+ held) on the
 * volume card (KNOB 1) toggles the row's solo, once per hold (SHIFT + the knob alone is the reset): a track (song.solo's bit), a lane (drum_mix.c
 * dlm_solo), the USB input (SOLO_USB). One solo set: core.h trk_silent. The soloed row is marked (S badge, a lane's code green). Included after
 * ui_optimist_strike.h. */
static void solo_clear(void)
{
    uint32_t i;
    for (i = 0; i < NTRK; i++)
        trk[i].p[P_MUTE] = 0;
    song.solo = 0;
    dlm_solo_clear();
    for (i = 0; i < DRUM_LANES; i++)
        dlm_set_mute(i, 0);
}
static void solo_gesture(void)                          /* both OCT held, KNOB 1 one detent, both let go */
{
    press(B_OCTDN);
    press(B_OCTUP);
    turn(EN_K1, 1);
    release(B_OCTUP);
    release(B_OCTDN);
    frames(2);
}
static void solo_tests(void)
{
    uint32_t y;
    int16_t lv;
    solo_clear();
    reset_ui();
    song.octave = 0;
    song.sel = 1;
    frames(2);
    mx.set = 0;
    ui.row[SCR_HOME] = MXR_T1 + 1u;
    lv = trk[1].p[P_LEVEL];
    solo_gesture();
    y = mx_row_y(MXR_T1 + 1u);
    check(song.solo == 2u && trk[1].p[P_LEVEL] == lv && !trk[1].p[P_MUTE] && song.octave == 0 && !ui.master && px_in(6, y + 18u, 8, 14, C_OK),
          "solo: T2's VOLUME card with SHIFT + SHIFT2: soloed once, the volume and mute kept, the row's S badge");
    check(trk_silent(&trk[0]) && !trk_silent(&trk[1]) && trk_silent(TDRUM), "solo: ... the others silent");
    solo_gesture();
    check(song.solo == 0 && !trk_silent(&trk[0]), "solo: ... again: unsoloed");
    press(B_OCTDN);
    turn(EN_K1, 1);
    release(B_OCTDN);
    check(song.solo == 0, "solo: SHIFT + the knob alone is still the reset: no solo");
    press(B_OCTUP);
    turn(EN_K1, 1);
    release(B_OCTUP);
    check(song.solo == 0 && trk[1].p[P_MUTE] == 1, "solo: SHIFT2 + the knob alone is still the mute");
    trk[1].p[P_MUTE] = 0;
    trk[1].p[P_LEVEL] = lv;
    song.sel = TRK_DRUM;
    frames(2);
    ui.row[SCR_HOME] = MXR_DR;
    solo_gesture();
    check(song.solo == (1u << TRK_DRUM) && !trk_silent(TDRUM) && trk_silent(&trk[0]), "solo: DR's VOLUME card: the drum track soloed");
    song.solo = 0;
    ui.row[SCR_HOME] = MXR_LANE0 + 2u;
    lane_select(2);
    frames(2);
    solo_gesture();
    check(dlm_soloed(2) && !dlm_soloed(1) && dlm_solo && !trk_silent(TDRUM) && trk_silent(&trk[0]) && !dlm_lane_heard(1) && dlm_lane_heard(2),
          "solo: a lane's LEVEL card: that lane soloed, the drum track heard, the other lanes and tracks silent");
    ui.row[SCR_HOME] = MXR_LANE0 + 3u;
    lane_select(3);
    frames(2);
    solo_gesture();
    check(dlm_soloed(2) && dlm_soloed(3), "solo: a second lane: both soloed");
    ui.row[SCR_HOME] = MXR_LANE0 + 2u;
    lane_select(2);
    frames(2);
    solo_gesture();
    ui.row[SCR_HOME] = MXR_LANE0 + 3u;
    lane_select(3);
    frames(2);
    solo_gesture();
    check(!song.solo && !dlm_solo && dlm_lane_heard(1), "solo: both lanes unsoloed: everything heard");
#if FELUCCA_USB_AUDIO
    ui.row[SCR_HOME] = MXR_USB;
    frames(2);
    mx.set = 0;
    solo_gesture();
    y = mx_row_y(MXR_USB);
    check(song.solo == SOLO_USB && !ut.v[UTV_MUTE] && trk_silent(&trk[0]) && trk_silent(TDRUM) && !ut_silent(),
          "solo: the USB row's VOLUME card: the USB input soloed, every track silent, the USB input heard");
    check(mx_soloed(MXR_USB) && px_in(6, y + 18u, 8, 14, C_OK), "solo: ... its row's S badge");
    solo_gesture();
    check(song.solo == 0, "solo: ... again: unsoloed");
#endif
    ui.row[SCR_HOME] = MXR_MASTER;
    frames(2);
    solo_gesture();
    check(song.solo == 0, "solo: MASTER: none");
    solo_clear();
    reset_ui();
    song.sel = 0;
    reset_ui();
}
