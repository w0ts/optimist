/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, GLOBAL and SYSTEM as one settings list (docs/UI-OPTIMIST-DESIGN.md sections 4.11, 20; the
 * user, 2026-10-12: "the system menu would be better and more readable as vertical list of options", "select scroll by
 * page", "algo one by one", "when we scroll with algo, we scroll page by page (the selection on the page moves line by
 * line)"). A line a setting: the cells of TEMPO's and SYSTEM's rows, in order, under section names; pages of 4 lines
 * marked 1..4, KNOB k line k of the page; SELECT and GLO page (ui_optimist_pages.h D1), ALGORITHM moves the cursor line
 * by line, PRESETS the cursor line's value; neither changes the track or its sound here. Included after
 * ui_optimist_cardspic.h. Screens opt-glo-global-top, -mid, opt-glo-system-cpu. */
static uint32_t glo_find(uint32_t scr, const char *name)  /* the first line of screen scr in section name, or the count */
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
/* the lines screen s should have: every cell of its rows that is something, in order (one place each value lives) */
static uint32_t glo_expect(uint32_t s, cell_t *out, char (*sec)[12], uint32_t max)
{
    uint32_t u, k, n = 0, rows = s ? NSYS_SETUP : glo_rows();
    cell_t c;
    for (u = 0; u < rows; u++)
        for (k = 0; k < 4u; k++) {
            if (s)
                sys_cell(u, k, &c);
            else
                glo_cell(u, k, &c);
            if (c.kind == CK_NONE || n >= max || (!s && u == TPR_TEMPO && k == 3u))   /* (SYNC: PLAY's TEMPO page only; GLOBAL has CLK IN) */
                continue;
            out[n] = c;
            if (s || u >= GL_SYS0)
                str_cpy(sec[n], SYS[(s ? u : glo_sys(u)) % NSYS].name, 12);
            else
                tp_name(u == TPR_REC2 ? TPR_REC : u, sec[n]);
            n++;
        }
    return n;
}
static int glo_sections(uint32_t scr, const char *const *want, uint32_t nw)   /* the sections in order, each once */
{
    uint32_t i, j = 0, n = SCREENS[scr].rows();
    char nm[12], prev[12] = "";
    for (i = 0; i < n; i++) {
        SCREENS[scr].name(i, nm);
        if (!strcmp(nm, prev))
            continue;
        while (j < nw && strcmp(want[j], nm))           /* (a section this build lacks: skipped) */
            j++;
        if (j == nw) {
            printf("  glo: section %s out of order\n", nm);
            return 0;
        }
        str_cpy(prev, nm, sizeof prev);
    }
    return 1;
}
static void glo_list_tests(void)
{
    static const char *const GS[] = {"TEMPO", "REC", "MIDI", "CHANNELS"};
    static const char *const SS[] = {"INFO", "SCREEN", "AUDIO", "KEYS", "USB", "BLUETOOTH", "CALIBRATE", "ABOUT"};
    cell_t want[GL_MAXL], c;
    char sec[GL_MAXL][12], nm[12];
    uint32_t s, i, n, ok = 1;
    for (s = 0; s < 2u; s++) {
        uint32_t scr = s ? SCR_SYSTEM : SCR_GLOBAL;
        n = glo_expect(s, want, sec, GL_MAXL);
        ok &= SCREENS[scr].rows() == n;
        for (i = 0; i < n && ok; i++) {
            SCREENS[scr].cell(i, gl_kn(scr, i), &c);
            SCREENS[scr].name(i, nm);
            ok &= (uint32_t)(c.kind == want[i].kind && c.label == want[i].label && !strcmp(c.val, want[i].val) &&
                             !strcmp(nm, sec[i]));
            if (!ok)
                printf("  glo: screen %u line %u: %s %s\n", scr, i, c.label ? c.label : "-", nm);
        }
    }
    check(ok, "the list: a line a cell of TEMPO's / SYSTEM's rows, in order, empty cells left out (one place each value)");
    SCREENS[SCR_GLOBAL].cell(0, 0, &c);
    ok = !strcmp(c.label, "BPM");
    SCREENS[SCR_GLOBAL].cell(1, 1, &c);
    ok &= !strcmp(c.label, "NUDGE") && c.kind == CK_RO;
    SCREENS[SCR_GLOBAL].cell(2, 2, &c);
    ok &= !strcmp(c.label, "SWING");
    i = SCREENS[SCR_GLOBAL].rows() - 1u;
    SCREENS[SCR_GLOBAL].cell(i, gl_kn(SCR_GLOBAL, i), &c);
    check(ok && !strcmp(c.label, "DRUMS"), "GLOBAL's lines: BPM NUDGE SWING first (Tempo), DRUMS last (Channels)");
    check(glo_sections(SCR_GLOBAL, GS, 4) && glo_sections(SCR_SYSTEM, SS, 8) && glo_find(SCR_GLOBAL, "REC 2") == SCREENS[SCR_GLOBAL].rows(),
          "the sections in order: Tempo Rec (REC 2's QUANT is Rec's) MIDI (CLK OUT on it) Channels; Info Screen Audio Keys USB Bluetooth Calibrate About");
    {   /* SYSTEM's first page is INFO: one line (the page is drawn whole), its read-outs the page's own (sys_info_cell) */
        cell_t c1, c2;
        char nm1[12];
        SCREENS[SCR_SYSTEM].cell(0, 0, &c);
        SCREENS[SCR_SYSTEM].name(0, nm);
        SCREENS[SCR_SYSTEM].name(1, nm1);
        sys_info_cell(0, &c1);
        sys_info_cell(1, &c2);
        check(!strcmp(nm, "INFO") && c.kind == CK_RO && !strcmp(c.label, "INFO") && strcmp(nm1, "INFO") && gl_end(1, 0) == 1u &&
                  c1.kind == CK_RO && !strcmp(c1.label, "CLOCK") && c2.kind == CK_RO && !strcmp(c2.label, DUAL_LOAD ? "CPU0" : "LOAD"),
              "SYSTEM's first page is INFO, one line: the clock and the load (read-outs) are its own, the next section starts page 2");
    }
    for (s = 0, ok = 1; s < 2u; s++)                    /* nothing in GLOBAL or SYSTEM is an empty line */
        for (i = 0; i < SCREENS[s ? SCR_SYSTEM : SCR_GLOBAL].rows(); i++) {
            SCREENS[s ? SCR_SYSTEM : SCR_GLOBAL].cell(i, gl_kn(s ? SCR_SYSTEM : SCR_GLOBAL, i), &c);
            ok &= c.kind != CK_NONE && c.label != 0;
        }
    check(ok, "every line has a name and a value (no empty line)");
}
static void glo_draw_tests(void)
{
    uint32_t k, ok = 1, ng = SCREENS[SCR_GLOBAL].rows();
    char p[12];
    reset_ui();
    song.sel = 0;
    ui.row[SCR_GLOBAL] = 0;
    tap(B_GLO);
    frames(OP_TFLASH_FRAMES + 2u);                      /* (the track name's flash over: the header's own) */
    knob_colors = 1;
    ui.force = 1;
    frames(2);
    for (k = 2; k < 3u; k++)                            /* (line 1 the cursor: white ink; 2 NUDGE a read-out: dim; Tempo has 3 lines) */
        ok &= (uint32_t)px_in(4, OH_HEAD, 12, 240 - OH_HEAD, KNOB_COL[k]);
    check(ok && px_in(0, OH_HEAD, 240, 60, KNOB_COL[0]) && !cards_band_is(BAND_2X2),
          "GLOBAL drawn: no cards, the lines marked 1..4 at the left (knob colours), the cursor line its knob's colour at full strength");
    ppm("opt-glo-global-top");
    knob_colors = 0;
    turn(EN_SELECT, 1);
    frames(2);
    cp_pos(p);
    check(ui.row[SCR_GLOBAL] == gl_end(0, 0) && !strncmp(p, " 2/", 3), "SELECT 1: page 2 shown (2/N), the cursor on its first line");
    ui.force = 1;
    frames(2);
    ppm("opt-glo-global-mid");
    turn(EN_SELECT, (int32_t)ng);
    frames(2);
    check(ui.scr == SCR_GLOBAL && ui.row[SCR_GLOBAL] == gl_first(0, ng - 1u), "SELECT right: stops on GLOBAL's last page (never SYSTEM)");
    turn(EN_SELECT, -100);
    check(ui.scr == SCR_GLOBAL && ui.row[SCR_GLOBAL] == 0u, "SELECT left: stops on GLOBAL's first page (no wrap)");
    op_enter(SCR_SYSTEM);
    ui.row[SCR_SYSTEM] = 0;
    turn(EN_SELECT, -1);
    check(ui.scr == SCR_SYSTEM && ui.row[SCR_SYSTEM] == 0u, "SELECT left on SYSTEM's first page: stays (never GLOBAL)");
    turn(EN_SELECT, 1000);
    check(ui.scr == SCR_SYSTEM && ui.row[SCR_SYSTEM] == gl_first(1, SCREENS[SCR_SYSTEM].rows() - 1u),
          "SELECT right: stops on SYSTEM's last page (no wrap)");
    ui.row[SCR_SYSTEM] = 0;
    knob_colors = 1;
    ui.force = 1;
    frames(2);
    ppm("opt-glo-system-cpu");
    knob_colors = 0;
    {   /* a knob picks its line; ALGORITHM never moves the cursor line (the user, 2026-10-13: "only the track") */
        uint32_t i = gl_end(1, 0) - 1u, was = song.sel;   /* (the first page's last line) */
        turn(EN_K1 + gl_kn(SCR_SYSTEM, i), 1);
        cp_pos(p);
        ok = ui.row[SCR_SYSTEM] == i && !strncmp(p, " 1/", 3);
        turn(EN_K1 + gl_kn(SCR_SYSTEM, i), -1);
        turn(EN_ALGO, 1);
        turn(EN_ALGO, -1);
        cp_pos(p);
        check(ok && ui.row[SCR_SYSTEM] == i && song.sel == was && !strncmp(p, " 1/", 3),
              "knob k picks line k; ALGORITHM turned both ways leaves the cursor line and the page alone");
    }
    reset_ui();
}
static void glo_tests(void)
{
    uint32_t i, ok = 1, w, tot;
    int16_t v;
    cell_t a;
    glo_list_tests();
    glo_draw_tests();
    reset_ui();
    song.sel = 0;
    ui.row[SCR_GLOBAL] = 0;
    tap(B_GLO);
    check(ui.scr == SCR_GLOBAL && ui.row[SCR_GLOBAL] == 0u, "GLO tapped on the mixer: GLOBAL, its first line");
    {   /* the knobs: KNOB k the page's line k; the turn makes it the cursor */
        int16_t bpm = song.g[G_BPM], sw = song.g[G_SWING];
        turn(EN_K1, 3);
        ok = song.g[G_BPM] > bpm && ui.row[SCR_GLOBAL] == 0u;
        turn(EN_K3, 2);
        check(ok && song.g[G_SWING] != sw && ui.row[SCR_GLOBAL] == 2u, "Tempo page: KNOB 1 the BPM, KNOB 3 the SWING (its line the cursor)");
        tp_cell(TPR_TEMPO, 0, &a);
        {
            cell_t b;
            SCR->cell(0, 0, &b);
            check(!strcmp(a.val, b.val), "... the same value the TEMPO page shows");
        }
        song.g[G_BPM] = bpm;
        song.g[G_SWING] = sw;
        w = line_named(SCR_GLOBAL, "TRK 2");
        turn(EN_SELECT, (int32_t)gl_pg(0, w) - (int32_t)gl_pg(0, ui.row[SCR_GLOBAL]));   /* (SELECT: to its page) */
        {
            cell_t c1, c2, d1, d2;
            SCR->cell(w - 1u, gl_kn(SCR_GLOBAL, w - 1u), &c1);
            SCR->cell(w, gl_kn(ui.scr, w), &c2);
            turn(LINE_KNOB(w), 1);
            SCR->cell(w - 1u, gl_kn(SCR_GLOBAL, w - 1u), &d1);
            SCR->cell(w, gl_kn(ui.scr, w), &d2);
            check(strcmp(c2.val, d2.val) && !strcmp(c1.val, d1.val) && ui.row[SCR_GLOBAL] == w,
                  "Channels: knob k changes the page's line k (T2), not its neighbour; the cursor on it");
            turn(LINE_KNOB(w), -1);
            SCR->cell(w, gl_kn(ui.scr, w), &d2);
            check(!strcmp(c2.val, d2.val), "... and back");
        }
    }
    {   /* PRESETS does nothing here (the user, 2026-10-13); never the track or its sound */
        static track_t keep[NTRK];
        uint32_t sel = song.sel, cur = preset_pos(&tot);
        memcpy(keep, trk, sizeof keep);
        w = line_named(SCR_GLOBAL, "SWING");
        v = song.g[G_SWING];
        turn(EN_PRESET, 2);
        check(song.g[G_SWING] == v && ui.row[SCR_GLOBAL] == w, "PRESETS on GLOBAL: the cursor line's value is not touched (SWING)");
        song.g[G_SWING] = (int16_t)(GP[G_SWING].def + 3);
        press(B_HOME);
        turn(EN_PRESET, 1);
        release(B_HOME);
        check(song.g[G_SWING] == GP[G_SWING].def + 3 && ui.scr == SCR_GLOBAL, "HOME held + PRESETS: nothing either (HOME no NO)");
        press(B_HOME);
        turn(LINE_KNOB(w), 1);
        release(B_HOME);
        check(song.g[G_SWING] == GP[G_SWING].def && ui.scr == SCR_GLOBAL, "HOME held + the line's knob: its default");
        song.g[G_SWING] = v;
        turn(EN_PRESET, -1);
        line_named(SCR_SYSTEM, "INFO");
        turn(EN_PRESET, 1);                             /* (a read-out: nothing) */
        check(song.sel == sel && preset_pos(&tot) == cur && !lane_picked() && !ui.master,
              "PRESETS on GLOBAL and SYSTEM: the track and its preset never change");
        line_named(SCR_GLOBAL, "SWING");
        memcpy(trk, keep, sizeof keep);
    }
    {   /* the read-outs: CPU's CLOCK and LOAD, NUDGE; a knob does nothing, YES nothing */
        w = line_named(SCR_SYSTEM, "INFO");
        frame();
        turn(LINE_KNOB(w), 3);
        SCR->cell(w, gl_kn(ui.scr, w), &a);
        tap(B_SAVE);
        check(a.kind == CK_RO && ui.scr == SCR_SYSTEM && !op_armed(), "SYSTEM INFO: a read-out page: its knobs and YES do nothing");
    }
    {   /* the actions: About's VERSION says it; Calibrate's PANEL asks first (ui_optimist_pages.h P28) */
        line_named(SCR_SYSTEM, "VERSION");
        frame();
        tap(B_SAVE);
        check(strstr(ui.msg, "OPTIMIST UI") != 0, "About: YES on VERSION says the version");
        line_named(SCR_SYSTEM, "PANEL");
        frame();
        tap(B_SAVE);
        check(op_armed() && ui.arm_scr == SCR_SYSTEM && ui.arm_row == ui.row[SCR_SYSTEM] && !strcmp(ui.arm_arg, "PANEL"),
              "Calibrate: YES on PANEL asks first (the question on its line)");
        tap(B_HOME);
        check(!op_armed() && ui.scr == SCR_SYSTEM, "... HOME: cancelled");
#if FELUCCA_BLE
        w = line_named(SCR_SYSTEM, "BLE");
        frame();
        tap(B_SAVE);
        ok = dev_listing();
        i = song.sel;
        turn(EN_SELECT, 1);
        ok &= song.sel == i && dev_listing() && ui.row[SCR_SYSTEM] <= 1u;
        tap(B_HOME);
        check(ok && !dev_listing() && ui.scr == SCR_SYSTEM && ui.row[SCR_SYSTEM] == w,
              "Bluetooth: YES on BLE opens the devices, SELECT moves their cursor (no track), HOME back to the BLE line");
#endif
    }
    reset_ui();
    song.sel = 0;
    ui.row[SCR_GLOBAL] = 0;
    tap(B_GLO);
    {   /* OCT on a page with Tempo's lines: the nudge, the keyboard octave untouched; another page: the octave */
        int8_t o = song.octave;
        press(B_OCTUP);
        frames(2);
        check(clk_nudge > 0 && song.octave == o, "GLOBAL's Tempo page: OCT+ held nudges the clock, the octave untouched");
        release(B_OCTUP);
        frames(2);
        check(clk_nudge == 0, "... let go: no nudge");
        turn(EN_SELECT, 1);                             /* (page 2: Rec's lines) */
        tap(B_OCTUP);
        check(clk_nudge == 0 && song.octave == o + 1, "GLOBAL page 2: OCT+ is the keyboard octave again");
        song.octave = o;
    }
    {   /* GLO tapped: GLOBAL and SYSTEM in turn, each on its own line */
        line_named(SCR_SYSTEM, "HOLD");
        w = ui.row[SCR_SYSTEM];
        line_named(SCR_GLOBAL, "QUANT");
        i = ui.row[SCR_GLOBAL];
        tap(B_GLO);
        ok = ui.scr == SCR_SYSTEM && ui.row[SCR_SYSTEM] == w;
        tap(B_GLO);
        check(ok && ui.scr == SCR_GLOBAL && ui.row[SCR_GLOBAL] == i, "GLO tapped: SYSTEM, again GLOBAL, each on the line it was left on");
    }
    {   /* a global setting changed: saved when GLOBAL is left */
        w = line_named(SCR_GLOBAL, "TRK 1");
        frame();
        turn(LINE_KNOB(w), 1);
        check(sys_dirty == 1u, "GLOBAL Channels: T1's knob a MIDI channel (a setting of the FM-1)");
        turn(LINE_KNOB(w), -1);
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
