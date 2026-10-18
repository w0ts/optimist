/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, the second core on SYSTEM > INFO (FELUCCA_DUAL, system/dual_mode.h). 2026-10-14: there is no
 * 2ND CORE line any more, the core is always on; dual.c's guard still parks it and INFO's 2ND CORE line reads OFF and why
 * (DUAL_WHY) instead of ON and the load. The self-disable lasts for the session (DUAL_MODE_MENU 0: nothing could set it back), and the
 * Optimist UI's settings load drops a stored OFF. A single-core build says NOT IN BUILD. */
static void core_row_tests(void)
{
    cell_t c;
    reset_ui();
    check(line_named(SCR_SYSTEM, "2ND CORE") == SCREENS[SCR_SYSTEM].rows(), "2ND CORE: no such line (the core is always on)");
#if DUAL_LOAD
    {
        uint32_t q = dual_c1_q8, why = dual.why;
        check(DUAL_MODE_MENU == 0, "2ND CORE: no menu sets it back, so a self-disable is not saved (DUAL_MODE_MENU 0)");
        dual_mode = DUALM_AUTO;
        dual.boot_mode = DUALM_AUTO;
        dual_mode = (uint8_t)dual_mode_after(DUALM_AUTO, DUALM_AUTO, DUAL_MISREAD);
        check(dual_mode == DUALM_AUTO, "2ND CORE: a misread's self-disable is not kept in the setting (the next boot starts CPU1 again)");
        dual_c1_q8 = 128u;
        dual.why = DUAL_UP;
        info_mode = 0;                                  /* (the NOW measure: an earlier test may have turned INFO's KNOB 1) */
        sys_info_cell(2, &c);
        check(!strcmp(c.label, "2ND CORE ON") && !strcmp(c.val, "50") && !strcmp(c.unit, "%") && c.col == C_OK, "2ND CORE ON: the CPU1 load, 50 %, green (under 60)");
        dual_c1_q8 = DUAL_C1_OFF;
        dual.why = DUAL_MISREAD;
        sys_info_cell(2, &c);
        check(!strcmp(c.label, "2ND CORE OFF") && !strcmp(c.val, "MISREAD") && !strcmp(c.unit, "") && c.col == C_WARN, "2ND CORE OFF on a misread: MISREAD, amber (C_WARN)");
        dual.why = DUAL_CANARY;
        sys_info_cell(2, &c);
        check(!strcmp(c.val, "CANARY"), "CPU1 down on the canary: CANARY");
        dual.why = DUAL_EARLY;
        sys_info_cell(2, &c);
        check(!strcmp(c.val, "AFTER CRASH") && strlen(c.val) < sizeof c.val, "CPU1 held after a crash: AFTER CRASH (it fits the cell)");
        dual.why = DUAL_SETOFF;
        sys_info_cell(2, &c);
        check(!strcmp(c.val, "SETTING"), "CPU1 down by a stored OFF: SETTING");
        dual.why = DUAL_UP;
        sys_info_cell(2, &c);
        check(!strcmp(c.label, "2ND CORE OFF") && !strcmp(c.val, "NO REASON"), "2ND CORE OFF with no reason: NO REASON");
        {
            uint32_t i, ok = 1;
            for (i = 1; i < DUAL_WHY_N; i++) {
                dual.why = (uint8_t)i;
                sys_info_cell(2, &c);
                ok &= !strcmp(c.label, "2ND CORE OFF") && !strcmp(c.val, dual_why_name(i)) && strlen(c.val) < sizeof c.val;
            }
            check(ok, "CPU1 down: every reason's name is its cell's value");
        }
        dual_c1_q8 = q;
        dual.why = (uint8_t)why;
        dual_mode = DUALM_AUTO;
    }
#else
    sys_info_cell(2, &c);
    check(!strcmp(c.label, "2ND CORE") && !strcmp(c.val, "NOT IN BUILD") && !strcmp(c.unit, "") && c.gk != GK_NONE, "a single-core build: 2ND CORE, NOT IN BUILD");
    check(INFO_N == 6u + FELUCCA_CPU_GUARD, "the INFO page: clock, CPU, 2ND CORE, DROPS, UNDERRUNS, [GUARD,] battery in every build (the internals: DIAGNOSTICS)");
#endif
    reset_ui();
}

/* the overload monitor (system/overload.h; the user, 2026-10-15: "a MAX of the last few seconds and a DROP-OUT DETECTOR", "the
 * second encoder should switch between CURRENT, MAX OVER A FEW SECONDS, MAX FOR EVER. And we should also monitor UNDERRUNS"):
 * INFO's CPU line in the measure KNOB 1 picks (NOW, MAX 5S, MAX: % of a half, the load's colours, over 100 late), DROPS (late
 * halves, amber / red), UNDERRUNS (MIDI in lost, USB audio's), GUARD (the CPU guard's level, the voices shed), SHIFT + YES on
 * INFO clearing the displays, and the header's rule for ~1 s after a drop (red) or an underrun (amber), on every page. The ISR's
 * part is fed here as audio.c would (ovl_half) */
static uint32_t info_line(const char *label)            /* INFO's line with this label, INFO_N none */
{
    uint32_t i;
    cell_t c;
    for (i = 0; i < INFO_N; i++) {
        sys_info_cell(i, &c);
        if (!strcmp(c.label, label))
            return i;
    }
    return INFO_N;
}
static void overload_tests(void)
{
    cell_t c;
    uint32_t dr, xr, t, i, half = OVL_HALF_US;
    const char *cpu = DUAL_LOAD ? "CPU0" : "LOAD";
    char lb[24];
    reset_ui();
    memset((void *)&ovl, 0, sizeof ovl);
    info_mode = OVL_NOW;
    dr = info_line("DROPS");
    xr = info_line("UNDERRUNS");
    check(dr < INFO_N && xr < INFO_N && info_line("PEAK 5S") == INFO_N, "INFO: DROPS and UNDERRUNS lines (the maxima: the CPU line's KNOB 1)");
    t = fm1_ms;
    ovl_half(half * 40u / 100u, 0, t);
    ovl_half(half * 70u / 100u, 0, t + 6u);
    song.cpu_q8 = 50u * 256u / 100u + 1u;
    op_enter(SCR_SYSTEM);
    ui.row[SCR_SYSTEM] = 0;
    frame();
    sys_info_cell(1, &c);
    check(!strcmp(c.label, cpu) && !strcmp(c.val, "50") && c.col == C_OK, "the CPU line: NOW, the load (50 %, green)");
    turn(EN_K1, 1);
    sys_info_cell(1, &c);
    snprintf(lb, sizeof lb, "%s MAX 5S", cpu);
    check(info_mode == OVL_MAX5 && !strcmp(c.label, lb) && !strcmp(c.val, "70") && !strcmp(c.unit, "%") && c.col == C_WARN &&
          ui.scr == SCR_SYSTEM, "KNOB 1 on INFO: MAX 5S, the longest half (70 %, amber)");
    ovl_half(half * 133u / 100u, 1, t + 1500u);         /* (the user's log: 7,693 us, 133 % of a half) */
    fm1_ms = t + 1600u;
    sys_info_cell(1, &c);
    check(!strcmp(c.val, "133") && c.col == C_ERR, "MAX 5S over 100 %: 133, red (a late half)");
    for (i = 1; i <= 6u; i++)
        ovl_half(half / 2u, 0, t + 1500u + i * 1024u);  /* (6 s of light halves: the 133 % ages out of the window) */
    fm1_ms = t + 1500u + 6u * 1024u;
    sys_info_cell(1, &c);
    check(!strcmp(c.val, "50") && c.col == C_OK, "MAX 5S: ~5 s later the window forgets it (50 %, green)");
    turn(EN_K1, 1);
    sys_info_cell(1, &c);
    snprintf(lb, sizeof lb, "%s MAX", cpu);
    check(info_mode == OVL_MAXALL && !strcmp(c.label, lb) && !strcmp(c.val, "133") && c.col == C_ERR, "KNOB 1 again: MAX since boot, 133 % kept");
    turn(EN_K1, 1);
    check(info_mode == OVL_NOW, "KNOB 1 again: NOW (round)");
    turn(EN_K1, -1);
    check(info_mode == OVL_MAXALL, "KNOB 1 back: MAX");
    sys_info_cell(dr, &c);
    check(!strcmp(c.val, "1") && c.col == C_WARN, "DROPS: 1 since boot, amber (none in the last 5 s)");
    ovl_half(half * 101u / 100u, 1, fm1_ms);
    sys_info_cell(dr, &c);
    check(!strcmp(c.val, "2") && c.col == C_ERR, "DROPS: 2, red (one just now)");
    fm1_ms += 20u;
    frame();
    ppm("opt-info-overload");                           /* (INFO: CPU MAX, DROPS red, the header's rule red) */
    check(px_in(0, OH_HEAD - 2u, 240, 2, C_ERR), "a drop in the last second: the header's rule red");
    op_enter(SCR_HOME);
    frame();
    ppm("opt-mixer-drop");
    check(px_in(0, OH_HEAD - 2u, 240, 2, C_ERR), "... on every page (the mixer)");
    frames(OVL_MARK_MS / 16u + 2u);
    check(!px_in(0, OH_HEAD - 2u, 240, 2, C_ERR) && !px_in(0, OH_HEAD - 2u, 240, 2, C_WARN), "~1 s later: the rule as before");
    sys_info_cell(xr, &c);
    check(!strcmp(c.val, "0") && c.col == C_OK, "UNDERRUNS: 0, green");
    ovl.midi_lost[OVL_MIDI_TRS] += 2u;                  /* (TRS MIDI in: its ring full twice) */
    frame();
    check(px_in(0, OH_HEAD - 2u, 240, 2, C_WARN) && !px_in(0, OH_HEAD - 2u, 240, 2, C_ERR), "an underrun (MIDI in lost): the header's rule amber");
    sys_info_cell(xr, &c);
    check(!strcmp(c.val, "2") && strstr(c.unit, "MIDI2") && c.col == C_ERR, "UNDERRUNS: 2, MIDI2, red (just now)");
    op_enter(SCR_SYSTEM);
    ui.row[SCR_SYSTEM] = 0;
    frame();
    press(B_OCTDN);
    tap(B_SAVE);
    release(B_OCTDN);
    sys_info_cell(dr, &c);
    check(!strcmp(c.val, "0") && c.col == C_OK && ovl.drops == 2u, "SHIFT + YES on INFO: DROPS from 0 (the counter itself goes on)");
    sys_info_cell(xr, &c);
    check(!strcmp(c.val, "0") && ovl_midi_lost() == 2u, "... UNDERRUNS from 0");
    sys_info_cell(1, &c);
    check(!strcmp(c.val, "0"), "... the CPU line's MAX from 0");
    check(ui.scr == SCR_SYSTEM && !lay.chord, "... and no redo, no PROJECT");
    {   /* INFO split in two (the user, 2026-10-15): DIAGNOSTICS next, a page of its own, then USB DIAGNOSTICS (USB audio builds) */
        uint32_t e = gl_ent(1, 1), k, ok = 1;
        char t[32];
        ui.row[SCR_SYSTEM] = 1;
        ui.msg_t = 0;                                   /* (the header: the page's title, not "Counters cleared") */
        frame();
        ppm("opt-internals");
        gl_ptitle(1, 1, t, sizeof t);
        check(sys_item(e >> 2, e & 3u) == SI_DIAG && gl_end(1, 1) == 2u && !strcmp(t, "Internals"), "INTERNALS: the second page, its own");
        for (k = 0; k < INFO_DIAG; k++) {
            info_cell(INFO_PG_DIAG, k, &c);
            ok &= c.kind == CK_RO && c.label && c.label[0] && !strchr(c.label, '/') && !strchr(c.val, '/');
        }
        check(ok, "INTERNALS: read-outs in words (no a/b/c values)");
#if FELUCCA_USB_AUDIO
        ui.row[SCR_SYSTEM] = 2;
        frame();
        ppm("opt-usb-stats");
        e = gl_ent(1, 2);
        ok = sys_item(e >> 2, e & 3u) == SI_UAINFO && gl_end(1, 2) == 3u;
        for (k = 0; k < INFO_UA; k++) {
            info_cell(INFO_PG_USB, k, &c);
            ok &= c.kind == CK_RO && !strchr(c.label, '/') && !strchr(c.val, '/') && !strchr(c.unit, '/');
        }
        check(ok, "USB INTERNALS: the third page, the USB audio counters in words");
#endif
        ui.row[SCR_SYSTEM] = 0;
        frame();
    }
#if FELUCCA_CPU_GUARD
    {
        uint32_t g = info_line("GUARD");
        check(g < INFO_N, "INFO: a GUARD line (CPU_GUARD builds)");
        cg.level = 0, cg.vcap = 0;
        sys_info_cell(g, &c);
        check(!strcmp(c.val, "OFF") && c.col == C_OK, "GUARD OFF, green");
        cg.level = CG_L_REVERB;
        cg.sheds += 3u;
        sys_info_cell(g, &c);
        check(!strcmp(c.val, "REVERB -3") && !strcmp(c.unit, "V") && c.col == C_ERR, "GUARD REVERB, 3 voices shed, red");
        cg.level = CG_TOP;
        cg.vcap = 5;
        sys_info_cell(g, &c);
        check(!strncmp(c.val, "NOTES MAX5", 10), "GUARD at the top, reacting: the voices capped (MAX5)");
        cg.level = 0, cg.vcap = 0;
    }
#endif
    info_mode = OVL_NOW;
    song.cpu_q8 = 0;
    memset((void *)&ovl, 0, sizeof ovl);
    reset_ui();
}
