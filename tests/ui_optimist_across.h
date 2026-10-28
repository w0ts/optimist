/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, 2026-10-28 (issue #97): the mixer ACROSS, SHIFT2 (OCT+ held) + HOME: the four knobs the cursor's group's rows
 * (T1 T2 T3 DR, the lanes by four) on one value n, SELECT n by one, ALGORITHM the groups, the shared label on the first card alone, SHIFT +
 * a knob mutes that card's row; again: by row. Included after ui_optimist_retmute.h. */
static void across_flip(void)
{
    press(B_OCTUP);
    tap(B_HOME);
    release(B_OCTUP);
}
static void across_tests(void)
{
    cell_t c[4];
    uint32_t k, scr;
    int16_t lv[4];
    reset_ui();
    mx.across = 0, mx.set = 0, mx.an = 0;
    op_enter(SCR_HOME);
    ui.row[SCR_HOME] = MXR_T1;
    frames(2);
    across_flip();
    scr = ui.scr;
    check(mx.across && scr == SCR_HOME, "across: SHIFT2 + HOME on the mixer turns ACROSS on, the mixer stays");
    for (k = 0; k < 4u; k++)
        mix_cell(MXR_T1, k, &c[k]);
    check(c[0].label && str_eq(c[0].label, "VOLUME"), "across: card 1 on value 1 is VOLUME, named");
    check(c[1].label && !c[1].label[0] && c[2].label && !c[2].label[0] && c[3].label && !c[3].label[0],
          "across: cards 2..4 (T2 T3 DR, the same VOLUME): no label");
    check(c[0].col == mx_col(MXR_T1) && c[3].col == mx_col(MXR_DR), "across: each card in its row's colour");
    for (k = 0; k < 3u; k++)
        lv[k] = trk[k].p[P_LEVEL];
    lv[3] = song.g[G_DRLVL];
    turn(EN_K2, -3);
    turn(EN_K4, -3);
    check(trk[1].p[P_LEVEL] < lv[1] && song.g[G_DRLVL] < lv[3] && trk[0].p[P_LEVEL] == lv[0] && trk[2].p[P_LEVEL] == lv[2],
          "across: KNOB 2 turns T2's volume, KNOB 4 DR's, the others stay");
    trk[1].p[P_LEVEL] = lv[1], song.g[G_DRLVL] = lv[3];
    press(B_OCTDN);
    turn(EN_K1, 1);
    turn(EN_K3, 1);
    release(B_OCTDN);
    check(mx_muted(MXR_T1) && mx_muted(MXR_T1 + 2u) && !mx_muted(MXR_T1 + 1u), "across: SHIFT + KNOB 1 and 3 in one hold: T1 and T3 muted");
    mx_mute_set(MXR_T1, 0), mx_mute_set(MXR_T1 + 2u, 0);
    turn(EN_SELECT, 1);
    mix_cell(MXR_T1, 0, &c[0]);
    mix_cell(MXR_T1, 1, &c[1]);
    check(mx.an == 1u && c[0].label && str_eq(c[0].label, "PAN") && !c[1].label[0], "across: SELECT one detent: value 2, PAN (T1 named, T2 not)");
    turn(EN_SELECT, -1);
    turn(EN_ALGO, 1);
    check(mx_row() == MXR_LANE0, "across: ALGORITHM from the channels: the first group of lanes");
    for (k = 0; k < 4u; k++)
        mix_cell(mx_row(), k, &c[k]);
    check(c[0].col == mx_col(MXR_LANE0) && c[3].col == mx_col(MXR_LANE0 + 3u), "across: lanes 1-4 on the four cards");
    turn(EN_ALGO, 1);
    check(mx_row() == MXR_LANE0 + 4u, "across: ALGORITHM again: lanes 5-8");
    turn(EN_ALGO, -2);
    check(mx_row() == MXR_T1, "across: ALGORITHM back two: the channels, T1");
    turn(EN_ALGO, -1);
    check(mx_row() == MXR_MASTER && !mx_across(mx_row()), "across: MASTER: as ever, its own cards");
    turn(EN_ALGO, 1);
    across_flip();
    mix_cell(MXR_T1, 1, &c[1]);
    check(!mx.across && c[1].label && str_eq(c[1].label, "PAN"), "across: SHIFT2 + HOME again: by row (T1's PAN on card 2)");
    reset_ui();
}
