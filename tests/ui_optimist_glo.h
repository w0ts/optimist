/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, GLOBAL and SYSTEM as one settings list (docs/UI-OPTIMIST-DESIGN.md 4.7 GLOBAL and SYSTEM; the
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
            if (c.kind == CK_NONE || n >= max)          /* (GLOBAL's first page: TUNE QUANT FILT RESO) */
                continue;
            out[n] = c;
            str_cpy(sec[n], SYS[(s ? u : glo_sys(u)) % NSYS].name, 12);
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
    static const char *const GS[] = {"TUNE", "MIDI", "CHANNELS"};
    static const char *const SS[] = {"INFO", "INTERNALS",      /* (INFO split in two, 2026-10-15) */
#if FELUCCA_USB_AUDIO
                                     "USB STATS",         /* (the USB audio counters) */
#endif
#if OP_DTIMES
                                     "CPU TIMES",        /* (the dual render's times: DUAL_LOAD builds) */
#endif
                                     "BT MIDI", "BT AUDIO", "WI-FI", "DEVICES",   /* (the user, 2026-11-03: General, then Top, after Bluetooth and Wi-Fi) */
                                     "GENERAL", "METERS",   /* (METERS before the USB host page, 2026-11-03; USB SERIAL's row gone 2026-11-05) */
                                     "TOP",              /* (the CPU's jobs and the RAM's owners: SYS_TOP builds) */
                                     "USB AUDIO", "ABOUT", "CREDITS"};
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
    ok = !strcmp(c.label, "TUNE");
    SCREENS[SCR_GLOBAL].cell(1, 1, &c);
    ok &= !strcmp(c.label, "QUANT") && c.kind == CK_VAL;
    i = SCREENS[SCR_GLOBAL].rows() - 1u;
    SCREENS[SCR_GLOBAL].cell(i, gl_kn(SCR_GLOBAL, i), &c);
    check(ok && !strcmp(c.label, "DRUMS"), "GLOBAL's lines: TUNE QUANT first (Tune), DRUMS last (Channels)");
    check(glo_sections(SCR_GLOBAL, GS, 3) && glo_sections(SCR_SYSTEM, SS, sizeof SS / sizeof SS[0]) && glo_find(SCR_GLOBAL, "REC 2") == SCREENS[SCR_GLOBAL].rows(),
          "the sections in order: Tune (TUNE QUANT FILT RESO) MIDI (CLK OUT on it) Channels; Info Bluetooth Devices General USB Meters Top USB-audio About Credits");
    {   /* SYSTEM's first page is INFO: one line (the page is drawn whole), its read-outs the page's own (sys_info_cell) */
        cell_t c1, c2;
        char nm1[12];
        SCREENS[SCR_SYSTEM].cell(0, 0, &c);
        SCREENS[SCR_SYSTEM].name(0, nm);
        SCREENS[SCR_SYSTEM].name(1, nm1);
        sys_info_cell(0, &c1);
        sys_info_cell(1, &c2);
        check(!strcmp(nm, "INFO") && c.kind == CK_RO && !strcmp(c.label, "INFO") && strcmp(nm1, "INFO") && gl_end(1, 0) == 1u &&
                  c1.kind == CK_RO && c2.kind == CK_RO &&
                  (FELUCCA_SYS_DEBUG ? !strcmp(c1.label, DUAL_LOAD ? "CPU0" : "LOAD")
                                     : DUAL_LOAD ? !strcmp(c1.label, "CPU0") && !strcmp(c2.label, "CPU1") : !strcmp(c1.label, "CPU LOAD") && !strcmp(c2.label, "RAM")),
              "SYSTEM's first page is INFO, one line: the clock and the load (SYS_DEBUG off: the load, RAM; DUAL: CPU0, CPU1) are its own, the next section starts page 2");
    }
    for (s = 0, ok = 1; s < 2u; s++)                    /* nothing in GLOBAL or SYSTEM is an empty line */
        for (i = 0; i < SCREENS[s ? SCR_SYSTEM : SCR_GLOBAL].rows(); i++) {
            SCREENS[s ? SCR_SYSTEM : SCR_GLOBAL].cell(i, gl_kn(s ? SCR_SYSTEM : SCR_GLOBAL, i), &c);
            ok &= c.kind != CK_NONE && c.label != 0;
        }
    check(ok, "every line has a name and a value (no empty line)");
#if FELUCCA_TFLT_RESO && FELUCCA_TRK_FILT
    {   /* FILT RESO is the project's (2026-10-29): GLOBAL's FILTER section, not SYSTEM's General; General keeps its three and METERS joins */
        uint32_t f = glo_find(SCR_GLOBAL, "TUNE") + 2u, m = glo_find(SCR_GLOBAL, "MIDI"), g = glo_find(SCR_SYSTEM, "GENERAL");
        ok = f < SCREENS[SCR_GLOBAL].rows() && f < m;
        if (ok) {
            SCREENS[SCR_GLOBAL].cell(f, gl_kn(SCR_GLOBAL, f), &c);
            ok = c.label && !strcmp(c.label, "FILT RESO") && gl_end(0, f) == f + 1u && gl_first(0, f) == f - 2u;   /* (the third line of GLOBAL's first page) */
        }
        for (i = 0; i < SCREENS[SCR_SYSTEM].rows(); i++) {
            SCREENS[SCR_SYSTEM].cell(i, gl_kn(SCR_SYSTEM, i), &c);
            ok &= !(c.label && !strcmp(c.label, "FILT RESO"));
        }
        check(ok, "FILT RESO: a line of GLOBAL's first page (TUNE, QUANT, FILT RESO), none in SYSTEM");
#if !DBG_STATS
        ok = g + 2u < SCREENS[SCR_SYSTEM].rows() && gl_end(1, g) - gl_first(1, g) == 3u;
        SCREENS[SCR_SYSTEM].cell(g + 2u, gl_kn(SCR_SYSTEM, g + 2u), &c);
        check(ok && c.label && !strcmp(c.label, "METERS"), "SYSTEM > General: KNOB COLORS, HPHN BASS and METERS on one page (GL_JOIN: FILT RESO gone; METERS since 2026-11-03; USB SERIAL's row gone 2026-11-05)");
#endif
    }
#endif
#if DBG_STATS
    {   /* DEBUG STATS (the release, PLAT_STATS_APP) is General's last line (2026-10-29), no page of its own; METERS joins them (USB SERIAL did, until 2026-11-05) */
        static const char *const GL[4] = {"KNOB COLORS", "HPHN BASS", "DEBUG STATS", "METERS"};
        uint32_t g = glo_find(SCR_SYSTEM, "GENERAL");
        ok = g + 3u < SCREENS[SCR_SYSTEM].rows() && gl_end(1, g) - gl_first(1, g) == 4u;
        for (i = 0; ok && i < 4u; i++) {
            SCREENS[SCR_SYSTEM].cell(g + i, gl_kn(SCR_SYSTEM, g + i), &c);
            ok &= c.label && !strcmp(c.label, GL[i]);
        }
        check(ok, "SYSTEM > General (DEBUG STATS built): KNOB COLORS, HPHN BASS, DEBUG STATS and METERS on one page");
        for (i = 0, ok = 1; i < SCREENS[SCR_SYSTEM].rows(); i++) {
            char nm[24];
            SCREENS[SCR_SYSTEM].name(i, nm);
            ok &= strcmp(nm, "Debug") && strcmp(nm, "DEBUG");
        }
        check(ok, "SYSTEM has no DEBUG page (DEBUG STATS is on General's)");
    }
#endif
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
    check(ok && px_in(0, OH_HEAD, 240, 60, KNOB_COL[0]) && !cards_band_is(BAND_2X2),
          "GLOBAL drawn: no cards, no line numbers (removed 2026-10-15), the cursor line its knob's colour at full strength");
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
        int16_t tune = song.g[G_TUNE], rq = song.g[G_RECQ];
        turn(EN_K1, 3);
        ok = song.g[G_TUNE] > tune && ui.row[SCR_GLOBAL] == 0u;
        turn(EN_K2, 1);
        check(ok && ui.row[SCR_GLOBAL] == 1u, "GLOBAL's first page: KNOB 1 the TUNE, KNOB 2 the QUANT (its line the cursor)");
        song.g[G_TUNE] = tune;
        song.g[G_RECQ] = rq;
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
        w = line_named(SCR_GLOBAL, "TUNE");
        v = song.g[G_TUNE];
        turn(EN_PRESET, 2);
        check(song.g[G_TUNE] == v && ui.row[SCR_GLOBAL] == w, "PRESETS on GLOBAL: the cursor line's value is not touched (TUNE)");
        song.g[G_TUNE] = (int16_t)(GP[G_TUNE].def + 3);
        press(B_OCTDN);
        turn(EN_PRESET, 1);
        release(B_OCTDN);
        check(song.g[G_TUNE] == GP[G_TUNE].def + 3 && ui.scr == SCR_GLOBAL, "SHIFT held + PRESETS: nothing either (HOME no NO)");
        press(B_OCTDN);
        turn(LINE_KNOB(w), 1);
        release(B_OCTDN);
        check(song.g[G_TUNE] == GP[G_TUNE].def && ui.scr == SCR_GLOBAL, "SHIFT held + the line's knob: its default");
        song.g[G_TUNE] = v;
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
        check(a.kind == CK_RO && ui.scr == SCR_PROJECT && !op_armed(), "SYSTEM INFO: a read-out page: its knobs do nothing, SAVE (YES with nothing to do) opens PROJECT (2026-10-15)");
        op_enter(SCR_SYSTEM);
    }
    {   /* the actions: About's VERSION says it */
        line_named(SCR_SYSTEM, "VERSION");
        frame();
        tap(B_SAVE);
        check(strstr(ui.msg, "OPTIMIST UI") != 0, "About: YES on VERSION says the version");
#if BT_OVL
        bt.boot = 1;
        ble_store_reset(&ble_store);
        bt_enter(BT_MIDI, 1);                                    /* (the overlay list: since the page lists its devices itself, only dev_open opens it) */
        w = line_named(SCR_SYSTEM, "MODE");
        frame();
        dev_open(BT_MIDI);
        ok = dev_listing();
        i = song.sel;
        turn(EN_SELECT, 1);
        ok &= song.sel == i && dev_listing() && ui.row[SCR_SYSTEM] <= 1u;
        tap(B_HOME);
        check(ok && !dev_listing() && ui.scr == SCR_SYSTEM && ui.row[SCR_SYSTEM] == w,
              "Bluetooth: the overlay devices list: SELECT moves its cursor (no track), HOME back to the MODE line");
        bt_enter(BT_MIDI, 0);
#endif
    }
    reset_ui();
    song.sel = 0;
    ui.row[SCR_GLOBAL] = 0;
    tap(B_GLO);
    {   /* OCT on GLOBAL (no Tempo lines any more): never the nudge, the keyboard octave */
        int8_t o = song.octave;
        tap(B_OCTUP);
        check(clk_nudge == 0 && song.octave == o + 1, "GLOBAL's first page: OCT+ is the keyboard octave (the nudge is the PLAY-held page's)");
        song.octave = o;
        frames(120);                                    /* (the octave's toast away) */
    }
    {   /* GLO tapped: GLOBAL and SYSTEM in turn, each on its own line */
        line_named(SCR_SYSTEM, "KNOB COLORS");
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
        check(ui.scr == SCR_GLOBAL, "GLO tapped on SOUND: GLOBAL (the one open last: GLOBAL at power-on)");
        tap(B_GLO);
        check(ui.scr == SCR_SYSTEM, "... GLO on GLOBAL: SYSTEM");
        tap(B_HOME);
        tap(B_GLO);
        check(ui.scr == SCR_SYSTEM, "SYSTEM, then HOME, then GLO: SYSTEM again (the one open last, 2026-10-29)");
        tap(B_ENV);
        tap(B_GLO);
        check(ui.scr == SCR_SYSTEM, "... and from SOUND: SYSTEM still");
        tap(B_GLO);
        check(ui.scr == SCR_GLOBAL, "... GLO on SYSTEM: GLOBAL");
        tap(B_HOME);
        tap(B_GLO);
        check(ui.scr == SCR_GLOBAL, "GLOBAL, then HOME, then GLO: GLOBAL again");
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
        check(ui.scr == SCR_GLOBAL, "GLO tapped on the mixer's MASTER: GLOBAL (the one open last)");
        tap(B_HOME);
    }
    fuzz(2000, 9090);
    check(1, "2000 frames of random use with GLOBAL: every draw on the screen");
    reset_ui();
}
