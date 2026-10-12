/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, the user's request of 2026-10-12: "in the mixer page add a card for muting the track, first
 * card". KNOB 1 of the first knob set is MUTE on T1..T3 and DR (the track's P_MUTE: GLO held + keys 1-4, the row's M
 * badge) and on the lanes (the lane's own mute, drum_mix.c dlm_mute); MASTER has none. One storage each: the card, the
 * badge and the other ways of muting agree. Included after ui_optimist_algo.h. */
static void mute_card(uint32_t r, cell_t *c)            /* row r's KNOB 1 on its first set */
{
    mx.set = 0;
    mix_cell(r, 0, c);
}
static void mute_tests(void)
{
    cell_t c;
    uint32_t ok, y, i;
    for (i = 0; i < NTRK; i++)
        trk[i].p[P_MUTE] = 0;                           /* (everything heard: an earlier test may have left a mute) */
    for (i = 0; i < DRUM_LANES; i++)
        dlm_set_mute(i, 0);
    reset_ui();
    song.sel = 1;
    frames(2);
    mx.set = 0;
    turn(EN_K1, 1);
    frames(2);
    mute_card(MXR_T1 + 1u, &c);
    y = mx_row_y(MXR_T1 + 1u);
    ok = trk[1].p[P_MUTE] == 1 && c.label && !strcmp(c.label, "MUTE") && !strcmp(c.val, "ON") && px_in(6, y + 18u, 8, 14, C_WARN);
    ppm("opt-mute-t2");
    check(ok, "the mixer, T2's KNOB 1: MUTE on, the card ON, the row's M badge");
    trk[1].p[P_MUTE] = 0;                               /* (as GLO held + key 2 does) */
    frames(2);
    mute_card(MXR_T1 + 1u, &c);
    check(!strcmp(c.val, "OFF") && !px_in(6, y + 18u, 8, 14, C_WARN), "... unmuted elsewhere: the card OFF, no badge");
    ui.hot = 0;
    ui.hot_lit = 1;
    tap(B_SAVE);
    check(trk[1].p[P_MUTE] == 1 && ui.scr == SCR_HOME, "... YES on the MUTE card toggles it (no SOUND screen)");
    trk[1].p[P_MUTE] = 0;
    song.sel = TRK_DRUM;
    frames(2);
    mute_card(MXR_DR, &c);
    check(c.label && !strcmp(c.label, "MUTE") && c.vp == &TDRUM->p[P_MUTE], "DR: KNOB 1 the drum track's MUTE");
    ui.row[SCR_HOME] = MXR_LANE0 + 2u;
    lane_select(2);
    frames(2);
    turn(EN_K1, 1);
    frames(2);
    mute_card(MXR_LANE0 + 2u, &c);
ok = dlm_muted(2) && !dlm_muted(1) && !dlm_muted(3) && !strcmp(c.val, "ON") && !TDRUM->p[P_MUTE];
    ppm("opt-mute-lane");
    check(ok, "a lane's KNOB 1: that lane muted (its own mute), the card ON, the track heard");
    dlm_set_mute(2, 0);                                 /* (as SLOOP's kit pads do) */
    frames(2);
    mute_card(MXR_LANE0 + 2u, &c);
    check(!strcmp(c.val, "OFF"), "... unmuted elsewhere: the card OFF");
    mute_card(MXR_MASTER, &c);
    check(!c.label || strcmp(c.label, "MUTE"), "MASTER: no MUTE card");
    song.sel = 0;
    reset_ui();
}
