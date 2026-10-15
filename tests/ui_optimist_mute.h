/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, the user's ruling of 2026-10-14: the mixer's MUTE card is gone; SHIFT2 (OCT+ held) on the volume
 * card (KNOB 1) toggles the row's mute: T1..T3 and DR the track's P_MUTE (the row's M badge), a lane its own mute (drum_mix.c
 * dlm_mute); once per hold, the value untouched. MASTER has none. Included after ui_optimist_algo.h. */
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
    press(B_OCTUP);
    turn(EN_K1, 1);
    turn(EN_K1, 1);
    release(B_OCTUP);
    frames(2);
    mute_card(MXR_T1 + 1u, &c);
    y = mx_row_y(MXR_T1 + 1u);
    ok = trk[1].p[P_MUTE] == 1 && trk[1].p[P_LEVEL] == lv && c.label && !strcmp(c.label, "VOLUME") && px_in(6, y + 18u, 8, 14, C_WARN);
    check(ok, "the mixer, T2's VOLUME card with SHIFT2: muted once (two detents), the volume kept, the row's M badge");
    check(song.octave == 0, "... and SHIFT2 used: the octave unchanged");
    press(B_OCTUP);
    turn(EN_K1, 1);
    release(B_OCTUP);
    check(trk[1].p[P_MUTE] == 0, "... SHIFT2 again: unmuted");
    turn(EN_K1, 1);
    check(trk[1].p[P_MUTE] == 0 && trk[1].p[P_LEVEL] != lv, "... a plain turn: the volume, no mute");
    trk[1].p[P_LEVEL] = lv;
    song.sel = TRK_DRUM;
    frames(2);
    ui.row[SCR_HOME] = MXR_DR;
    press(B_OCTUP);
    turn(EN_K1, 1);
    release(B_OCTUP);
    check(TDRUM->p[P_MUTE] == 1, "DR: SHIFT2 on its VOLUME card mutes the drum track");
    TDRUM->p[P_MUTE] = 0;
    ui.row[SCR_HOME] = MXR_LANE0 + 2u;
    lane_select(2);
    frames(2);
    press(B_OCTUP);
    turn(EN_K1, 1);
    release(B_OCTUP);
    check(dlm_muted(2) && !dlm_muted(1) && !dlm_muted(3) && !TDRUM->p[P_MUTE], "a lane's LEVEL card with SHIFT2: that lane muted (its own mute), the track heard");
    dlm_set_mute(2, 0);
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
    song.sel = 0;
    reset_ui();
}
