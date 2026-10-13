/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, ALGORITHM is the track and nothing else (docs/UI-OPTIMIST-DESIGN.md section 26; the user,
 * 2026-10-13: "ALGORITHM moves track - NO. only the track!"). On every screen and in every performance layer a turn moves
 * the walk (MASTER T1 T2 T3 DR, the 16 lanes) and changes no setting, no cursor row, no page; and the controls that had
 * been ALGORITHM's elsewhere work without it: on GLOBAL and SYSTEM SELECT pages and a knob picks its line, on the BLE
 * devices list SELECT moves the cursor. Included after ui_optimist_pagemem.h. */
static uint32_t at_sum(void)                            /* everything ALGORITHM must leave alone, folded: the globals, every track's
                                                         * sound and pattern (not its running state) */
{
    uint32_t h = 2166136261u, i, t;
    const uint8_t *b = (const uint8_t *)song.g;
    for (i = 0; i < sizeof song.g; i++)
        h = (h ^ b[i]) * 16777619u;
    for (t = 0; t < NTRK; t++) {
        b = (const uint8_t *)trk[t].p;
        for (i = 0; i < sizeof trk[t].p; i++)
            h = (h ^ b[i]) * 16777619u;
        b = (const uint8_t *)trk[t].step;
        for (i = 0; i < sizeof trk[t].step; i++)
            h = (h ^ b[i]) * 16777619u;
    }
    return h;
}
static void at_screen_tests(void)
{
    uint32_t scr, ok = 1;
    for (scr = 0; scr < SCR_N; scr++) {
        uint32_t sum, row, p, bad = 0;
        if (scr == SCR_FX || scr == SCR_TEMPO)
            continue;                                   /* (MASTER itself; TEMPO is a page of the screen under it: below) */
        reset_ui();
        lane_transport(0);
        song.sel = 0;
        op_enter(scr);
        ui.row[scr] = (uint8_t)(SCREENS[scr].rows() > 1u ? 1u : 0u);
        op_rows_fix();
        frames(2);
        sum = at_sum();
        row = ui.row[scr];
        p = walk_pos(0);
        turn(EN_ALGO, 1);                               /* T1 -> T2 */
        bad |= !(walk_pos(0) == p + 1u && song.sel == 1u);
        bad |= !(ui.scr == scr && at_sum() == sum);
        if (scr != SCR_SOUND && scr != SCR_STEP && scr != SCR_HOME)
            bad |= ui.row[scr] != row;                  /* (a list screen's cursor; SOUND's rows follow the track's pages, the mixer's row is the walk) */
        turn(EN_ALGO, 20);                              /* the last lane, stopping there */
        bad |= !(walk_pos(0) == MXR_N - 1u && ui.scr == scr && at_sum() == sum && lane_selected() == 15u);
        turn(EN_ALGO, -40);                             /* up to MASTER: the master FX screen (the mixer's row), no setting */
        bad |= !((scr == SCR_HOME ? mx_row() == MXR_MASTER : ui.scr == SCR_FX) && at_sum() == sum);
        turn(EN_ALGO, 1);                               /* back to T1: the screen left from */
        bad |= !(song.sel == 0u && ui.scr == scr && at_sum() == sum);
        if (bad)
            printf("  algotrack: %s wrong (scr %u sel %u bad %u pos %u sum %d row %u/%u)\n", SCR_NAME[scr], ui.scr, song.sel, bad, walk_pos(0), at_sum() == sum, ui.row[scr], row);
        ok &= !bad;
    }
    check(ok, "ALGORITHM on every screen: only the walk moves (T1 .. DR .. the 16th lane .. MASTER and back); no setting, row or screen changes");
}
static void at_layer_tests(void)
{
    static const uint8_t BTN[] = {B_FX, B_EDIT, B_ARP, B_SCL, B_GLO, B_LFO, B_SAVE};
    uint32_t i, ok = 1;
    for (i = 0; i < sizeof BTN; i++) {
        uint32_t sum, scr, bad = 0;
        reset_ui();
        lane_transport(0);
        song.sel = 0;
        sum = at_sum();
        press(BTN[i]);
        frames(HOLD_FRAMES);
        scr = ui.scr;
        turn(EN_ALGO, 1);
        bad |= !(song.sel == 1u && ui.scr == scr && at_sum() == sum);
        turn(EN_ALGO, -1);
        bad |= !(song.sel == 0u && ui.scr == scr && at_sum() == sum);
        release(BTN[i]);
        frames(HOLD_FRAMES);
        bad |= !(at_sum() == sum && ui.scr == scr);
        if (bad)
            printf("  algotrack: layer button %u wrong (sel %u)\n", BTN[i], song.sel);
        ok &= !bad;
    }
    check(ok, "ALGORITHM in every performance layer (FX EDIT ARP SCL GLO LFO SAVE held): the track only, no setting");
}
static void at_tempo_tests(void)
{
    uint32_t sum, ok;
    reset_ui();
    song.sel = 0;
    sum = at_sum();
    press(B_PLAY);
    frames(HOLD_FRAMES);
    ok = ui.scr == SCR_TEMPO;
    turn(EN_ALGO, 1);
    ok &= song.sel == 1u && at_sum() == sum;
    release(B_PLAY);
    frames(2);
    check(ok, "ALGORITHM on the TEMPO page (PLAY held): the track only, no setting");
}
static void at_replacement_tests(void)
{
    uint32_t s, ok = 1;
    for (s = 0; s < 2u; s++) {                          /* GLOBAL, SYSTEM: SELECT pages; knob k picks line k, ALGORITHM leaves it */
        uint32_t scr = s ? SCR_SYSTEM : SCR_GLOBAL, k, i, np;
        reset_ui();
        song.sel = 0;
        op_enter(scr);
        ui.row[scr] = 0;
        np = gl_npg(s);
        for (i = 1; i < np && ok; i++) {
            turn(EN_SELECT, 1);
            ok &= ui.row[scr] == gl_pfirst(s, i) && gl_pg(s, ui.row[scr]) == i;
        }
        turn(EN_SELECT, -100);
        for (k = 0; k < 4u; k++) {
            cell_t c;
            uint32_t line = gl_first(s, ui.row[scr]) + k;
            if (line >= gl_end(s, ui.row[scr]))
                break;
            gl_cell(s, line, &c);
            if (c.kind != CK_VAL && c.kind != CK_RO && c.kind != CK_ACT)
                continue;
            turn(EN_K1 + k, c.kind == CK_VAL ? 1 : 0);   /* (a read-out or an action: only the pick, a zero turn is no turn) */
            if (c.kind == CK_VAL) {
                ok &= ui.row[scr] == line;
                turn(EN_K1 + k, -1);
            }
        }
        {
            uint32_t was = ui.row[scr], sum = at_sum();
            turn(EN_ALGO, 2);
            turn(EN_ALGO, -2);
            ok &= ui.row[scr] == was && ui.scr == scr && at_sum() == sum && song.sel == 0u;
        }
    }
    check(ok, "GLOBAL, SYSTEM: SELECT pages, knob k picks line k, ALGORITHM never moves the cursor");
    reset_ui();
}
static void algotrack_tests(void)
{
    at_screen_tests();
    at_layer_tests();
    at_tempo_tests();
    at_replacement_tests();
}
