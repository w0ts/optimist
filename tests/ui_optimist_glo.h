/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, GLOBAL (the user, 2026-10-12: "what is supposed to be the GLO page ? i thought it would be the
 * page where we have bpm, nudge, midi track settings etc"; docs/UI-OPTIMIST-DESIGN.md section 4.9): GLO tapped opens it
 * from every screen, again its next row; its rows are TEMPO's (TEMPO, REC, REC 2) and SYSTEM's global ones (MIDI, CLOCK
 * OUT, CHANNELS), which SYSTEM no longer shows, GLO tapped again its next row, on through SYSTEM's and round (D1, 2026-10-12); 2x2 cards and the rows under them; OCT
 * nudges on its TEMPO row; GLO held is still the layer; the mixer's knob sets are SELECT's (ui_optimist_mixer.h).
 * Included after ui_optimist_cardspic.h (cp_draw, cp_list). */
static uint32_t glo_find(uint32_t scr, const char *name)  /* the row of screen scr named name, or its row count */
{
    uint32_t r, n = SCREENS[scr].rows();
    char nm[12];
    for (r = 0; r < n; r++) {
        SCREENS[scr].name(r, nm);
        if (!strcmp(nm, name))
            break;
    }
    return r;
}
static void glo_tests(void)
{
    static const char *const ROWS[] = {"TEMPO", "REC", "REC 2", "MIDI",
#if FELUCCA_MIDI_CLKOUT
                                       "CLOCK OUT",
#endif
                                       "CHANNELS"};
    uint32_t i, n, ok = 1, r;
    char nm[12], p[12];
    cell_t a, b;
    reset_ui();
    song.sel = 0;
    ui.row[SCR_GLOBAL] = 0;                             /* (it keeps its row, as every screen: an earlier test left one) */
    tap(B_GLO);
    n = SCR->rows();
    check(ui.scr == SCR_GLOBAL && ui.row[SCR_GLOBAL] == 0u && n == sizeof ROWS / sizeof ROWS[0],
          "GLO tapped on the mixer: GLOBAL");
    for (i = 0; i < n; i++) {
        SCR->name(i, nm);
        ok &= (uint32_t)!strcmp(nm, ROWS[i]);
    }
    check(ok, "GLOBAL's rows: TEMPO REC REC 2 MIDI (CLOCK OUT) CHANNELS (no SYSTEM row: GLO again, D1)");
    ok = 1;
    for (i = 0; i < SCREENS[SCR_SYSTEM].rows(); i++) {  /* the global rows are GLOBAL's only */
        SCREENS[SCR_SYSTEM].name(i, nm);
        ok &= (uint32_t)(strcmp(nm, "MIDI") && strcmp(nm, "CHANNELS") && strcmp(nm, "CLOCK OUT"));
    }
    check(ok && glo_find(SCR_SYSTEM, "SCREEN") < SCREENS[SCR_SYSTEM].rows() &&
          glo_find(SCR_SYSTEM, "CALIBRATE") < SCREENS[SCR_SYSTEM].rows(),
          "SYSTEM keeps the setup rows (SCREEN, CALIBRATE ...), no MIDI, CLOCK OUT or CHANNELS");
    ok = 1;                                             /* one place each value lives: the same cells as TEMPO's, SYSTEM's */
    for (r = 0; r < n; r++)
        for (i = 0; i < 4u; i++) {
            SCR->cell(r, i, &a);
            if (r < TPR_N)
                tp_cell(r, i, &b);
            else
                sys_cell(NSYS_SETUP + r - TPR_N, i, &b);
            ok &= (uint32_t)(a.kind == b.kind && a.label == b.label && !strcmp(a.val, b.val));
        }
    check(ok, "GLOBAL's cells are TEMPO's and SYSTEM's own (tp_cell, sys_cell)");
    {   /* the TEMPO row's KNOB 1 is the BPM, the TEMPO page shows it */
        int16_t bpm = song.g[G_BPM];
        turn(EN_K1, 3);
        check(song.g[G_BPM] > bpm, "GLOBAL TEMPO: KNOB 1 the BPM");
        tp_cell(TPR_TEMPO, 0, &a);
        SCR->cell(0, 0, &b);
        check(!strcmp(a.val, b.val), "... the same value the TEMPO page shows");
        song.g[G_BPM] = bpm;
    }
    ok = 1;                                             /* every row: 2x2, its rows under the cards, the n/N */
    for (r = 0; r < n; r++) {
        ui.row[SCR_GLOBAL] = (uint8_t)r;
        cp_draw();
        ok &= (uint32_t)(cards_2x2() && cp_list() && lst.cur == r && cp_pos(p));
        if (!ok) {
            printf("  glo: row %u pic %u shown %u\n", r, lst.pic, lst.shown);
            break;
        }
    }
    ui.row[SCR_GLOBAL] = 0;
    cp_draw();
    ppm("opt-glo-global");
    check(ok, "GLOBAL: every row the 2x2 cards and the rows under them (no picture), the header's n/N");
    tap(B_GLO);
    check(ui.scr == SCR_GLOBAL && ui.row[SCR_GLOBAL] == 1u, "GLO tapped again: the next row");
    for (i = 1; i < n; i++)
        tap(B_GLO);
    check(ui.scr == SCR_SYSTEM && ui.row[SCR_SYSTEM] == 0u, "... past GLOBAL's last: SYSTEM's first (CPU), one cycle (D1)");
    for (i = 0; i < SCREENS[SCR_SYSTEM].rows(); i++)
        tap(B_GLO);
    check(ui.scr == SCR_GLOBAL && ui.row[SCR_GLOBAL] == 0u, "... past SYSTEM's last: GLOBAL's first again");
    {   /* OCT on the TEMPO row: the nudge, the keyboard octave untouched; elsewhere the octave */
        int8_t o = song.octave;
        press(B_OCTUP);
        frames(2);
        check(clk_nudge > 0 && song.octave == o, "GLOBAL TEMPO: OCT+ held nudges the clock, the octave untouched");
        release(B_OCTUP);
        frames(2);
        check(clk_nudge == 0, "... let go: no nudge");
        turn(EN_SELECT, 1);                             /* the REC row */
        tap(B_OCTUP);
        check(clk_nudge == 0 && song.octave == o + 1, "GLOBAL REC: OCT+ is the keyboard octave again");
        song.octave = o;
    }
    {   /* a global setting changed: saved when GLOBAL is left */
        r = glo_find(SCR_GLOBAL, "CHANNELS");
        ui.row[SCR_GLOBAL] = (uint8_t)r;
        frame();
        turn(EN_K1, 1);
        check(sys_dirty == 1u, "GLOBAL CHANNELS: KNOB 1 a MIDI channel (a setting of the FM-1)");
        turn(EN_K1, -1);
        tap(B_HOME);
        check(ui.scr == SCR_HOME && sys_dirty == 0u, "HOME: back to the mixer, the settings saved (as SYSTEM left)");
    }
    {   /* from another screen, and GLO held */
        tap(B_ENV);
        tap(B_GLO);
        check(ui.scr == SCR_GLOBAL, "GLO tapped on SOUND: GLOBAL");
        tap(B_HOME);
        press(B_GLO);
        frames(HOLD_FRAMES);
        check(lay.shown == LY_MIX && ui.scr == SCR_HOME, "GLO held: its performance layer, as before");
        frames(HOLD_FRAMES);
        release(B_GLO);
        frames(2);
        check(ui.scr == SCR_HOME && lay.shown == LY_PLAY, "... let go after the hold: no GLOBAL (a hold is no tap)");
    }
    {   /* the mixer's MASTER row: GLO opens GLOBAL there too */
        turn(EN_ALGO, -1);
        tap(B_GLO);
        check(ui.scr == SCR_GLOBAL, "GLO tapped on the mixer's MASTER: GLOBAL");
        tap(B_HOME);
    }
    fuzz(2000, 9090);
    check(1, "2000 frames of random use with GLOBAL: every draw on the screen");
    reset_ui();
}
