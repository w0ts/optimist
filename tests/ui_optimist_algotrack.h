/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, ALGORITHM is the track and nothing else (docs/UI-OPTIMIST-DESIGN.md section 26; the user,
 * 2026-10-13: "ALGORITHM moves track - NO. only the track!"). On every screen and in every performance layer a turn moves
 * the walk (MASTER T1 T2 T3 DR, the 16 lanes) and changes no setting, no cursor row, no page; and the controls that had
 * been ALGORITHM's elsewhere work without it: on GLOBAL and SYSTEM SELECT pages and a knob picks its line, on the BLE
 * devices list SELECT moves the cursor. The one exception (the user, 2026-10-14: "ALGORITHM should scroll one item at a time"):
 * on GLOBAL and SYSTEM's lists ALGORITHM scrolls the lines, one a detent across the pages (op_global.c glo_walk). Included after ui_optimist_pagemem.h. */
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
        if (scr != SCR_HOME && scr != SCR_SOUND && scr != SCR_STEP && scr != SCR_SCOPE)
            continue;                                   /* (the pages with no track: at_inert_tests; MASTER FX, TEMPO there too) */
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
        bad |= !(walk_pos(0) == MXR_N - 1u && ui.scr == scr && at_sum() == sum && lane_selected() == DRUM_UILANES - 1u);
        turn(EN_ALGO, -40);                             /* up to MASTER: the master FX screen (the mixer's row; SCOPE: its master source), no setting */
        bad |= !((scr == SCR_HOME ? mx_row() == MXR_MASTER : scr == SCR_SCOPE ? (ui.scr == SCR_SCOPE && ui.master) : ui.scr == SCR_FX) &&
                 at_sum() == sum);          /* (SCOPE: MASTER is its source, the screen stays) */
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
    static const uint8_t BTN[] = {B_EDIT, B_SCL};       /* (the layers per track; the others: at_inert_tests) */
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
    check(ok, "ALGORITHM in the per-track layers (EDIT SCL held): the track only, no setting");
}
/* ALGORITHM does nothing where there is no track (the user, 2026-10-14: "it makes no sense to change track when i am on a
 * page that has no track"): no track, no screen, no cursor, no setting. The pages, the layers the header gives no badge,
 * TEMPO, NAME; the master FX screen opened by the FX button (reached by the walk it is the walk's: ui_optimist_algo.h) */
static uint32_t at_inert_state(void)
{
    return ui.scr | (uint32_t)song.sel << 8 | (uint32_t)ui.row[ui.scr] << 16 | (uint32_t)ui.master << 24 | (uint32_t)lane_picked() << 25;
}
static void at_inert_tests(void)
{
    static const uint8_t PG[] = {SCR_PROJECT, SCR_SONG, SCR_FX};
    static const uint8_t BTN[] = {B_FX, B_ARP, B_GLO, B_SAVE
#if FELUCCA_PATTERNS
                                  , B_LFO               /* (the patterns layer; without them LFO is only SHIFT) */
#endif
    };
    uint32_t i, ok = 1, sum, st;
    for (i = 0; i < sizeof PG; i++) {
        uint32_t bad;
        reset_ui();
        song.sel = 1;
        op_enter(PG[i]);
        frames(2);
        sum = at_sum();
        st = at_inert_state();
        turn(EN_ALGO, 1);
        turn(EN_ALGO, -3);
        turn(EN_ALGO, 40);
        bad = !(at_inert_state() == st && at_sum() == sum);
        if (bad)
            printf("  algotrack: %s moved on ALGORITHM (state %x -> %x)\n", SCR_NAME[PG[i]], st, at_inert_state());
        ok &= !bad;
    }
    check(ok, "ALGORITHM does nothing on PROJECT, SONG and the master FX screen (no track, screen, row or setting; GLOBAL and SYSTEM scroll their lines)");
    ok = 1;
    for (i = 0; i < sizeof BTN; i++) {                  /* layers over the SOUND page of T2: FX ARP GLO LFO SAVE are global */
        uint32_t bad;
        reset_ui();
        song.sel = 1;
        tap(B_ENV);
        sum = at_sum();
        st = at_inert_state();
        press(BTN[i]);
        frames(HOLD_FRAMES);
        turn(EN_ALGO, 2);
        turn(EN_ALGO, -5);
        bad = !(song.sel == 1u && ui.scr == SCR_SOUND && at_sum() == sum);
        release(BTN[i]);
        frames(HOLD_FRAMES);
        bad |= !(at_inert_state() == st);
        if (bad)
            printf("  algotrack: layer button %u moved on ALGORITHM\n", BTN[i]);
        ok &= !bad;
    }
    check(ok, "ALGORITHM does nothing in the global layers (FX ARP GLO LFO SAVE held): no track, no setting");
    reset_ui();
    song.sel = 1;
    sum = at_sum();
    press(B_PLAY);
    frames(HOLD_FRAMES);
    ok = ui.scr == SCR_TEMPO;
    turn(EN_ALGO, 1);
    turn(EN_ALGO, -4);
    ok &= ui.scr == SCR_TEMPO && song.sel == 1u && at_sum() == sum;
    release(B_PLAY);
    frames(2);
    check(ok, "ALGORITHM does nothing on the TEMPO page (PLAY held)");
    reset_ui();
    song.sel = 1;
    tap(B_ENV);
    ok = al_walk(1) == MXR_T1 + 2u && song.sel == 2u;   /* (the track pages still walk) */
    check(ok, "ALGORITHM still walks on SOUND");
    reset_ui();
}
static void at_replacement_tests(void)
{
    uint32_t s, ok = 1;
    for (s = 0; s < 2u; s++) {                          /* GLOBAL, SYSTEM: SELECT pages; knob k picks line k, ALGORITHM scrolls the lines */
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
        {   /* ALGORITHM (the user, 2026-10-14): the cursor a line a detent, across the pages, stopping at the ends; no track, no setting */
            uint32_t i, sum = at_sum(), st;
            ui.row[scr] = 0;
            frame();
            st = at_inert_state() & ~(0xFFu << 16);
            for (i = 1; i < gl_n(s); i++) {
                turn(EN_ALGO, 1);
                ok &= ui.row[scr] == i && gl_pg(s, i) == gl_pg(s, ui.row[scr]) && ui.hot == i - gl_first(s, i);
            }
            turn(EN_ALGO, 5);
            ok &= ui.row[scr] == gl_n(s) - 1u;           /* (stops at the last line) */
            for (i = gl_n(s) - 1u; i > 0; i--) {
                turn(EN_ALGO, -1);
                ok &= ui.row[scr] == i - 1u;
            }
            turn(EN_ALGO, -5);
            ok &= ui.row[scr] == 0u && (at_inert_state() & ~(0xFFu << 16)) == st && at_sum() == sum && song.sel == 0u && ui.scr == scr;
            turn(EN_ALGO, 3);
            ok &= ui.row[scr] == (gl_n(s) > 3u ? 3u : gl_n(s) - 1u);   /* (several detents at once) */
            if (!ok)
                printf("  algotrack: %s ALGORITHM scroll wrong (row %u)\n", scr == SCR_SYSTEM ? "SYSTEM" : "GLOBAL", ui.row[scr]);
        }
    }
    check(ok, "GLOBAL, SYSTEM: SELECT pages, knob k picks line k, ALGORITHM scrolls one line a detent across the pages (no track, no setting)");
    reset_ui();
}
static void algotrack_tests(void)
{
    at_screen_tests();
    at_layer_tests();
    at_inert_tests();
    at_replacement_tests();
}
