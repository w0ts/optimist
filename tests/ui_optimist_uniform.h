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
static void uniform_tests(void)
{
#if FELUCCA_ARRANGER
    un_song_rec_tests();
#endif
}
