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
    check(INFO_N == INFO_BASE, "the INFO page has the same lines in every single-core build (clock, CPU0, 2ND CORE, PEAK, DROPS, [GUARD,] battery)");
#endif
    reset_ui();
}

/* the overload monitor (system/overload.h; the user, 2026-10-15: "a MAX of the last few seconds and a DROP-OUT DETECTOR"): INFO's
 * PEAK (the longest half of the last ~5 s, % of a half, the load's colours), DROPS (late halves, amber / red), GUARD (the CPU
 * guard's level, the voices shed), SHIFT + YES on INFO clearing DROPS' display, and the header's red rule for ~1 s after a drop,
 * on every page. The ISR's part is fed here as audio.c would (ovl_half) */
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
    uint32_t pk, dr, t, i, half = OVL_HALF_US;
    reset_ui();
    memset((void *)&ovl, 0, sizeof ovl);
    pk = info_line("PEAK 5S");
    dr = info_line("DROPS");
    check(pk < INFO_N && dr < INFO_N, "INFO: a PEAK 5S line and a DROPS line");
    t = fm1_ms;
    ovl_half(half * 40u / 100u, 0, t);
    ovl_half(half * 70u / 100u, 0, t + 6u);
    sys_info_cell(pk, &c);
    check(!strcmp(c.val, "70") && !strcmp(c.unit, "%") && c.col == C_WARN && c.kind == CK_RO, "PEAK: the longest half, 70 %, amber (the load's colours)");
    ovl_half(half * 133u / 100u, 1, t + 1500u);         /* (the user's log: 7,693 us, 133 % of a half) */
    fm1_ms = t + 1600u;
    sys_info_cell(pk, &c);
    check(!strcmp(c.val, "133") && c.col == C_ERR, "PEAK over 100 %: 133, red (a late half)");
    for (i = 1; i <= 6u; i++)
        ovl_half(half / 2u, 0, t + 1500u + i * 1024u);  /* (6 s of light halves: the 133 % ages out of the window) */
    fm1_ms = t + 1500u + 6u * 1024u;
    sys_info_cell(pk, &c);
    check(!strcmp(c.val, "50") && c.col == C_OK, "PEAK: ~5 s later the window forgets it (50 %, green)");
    sys_info_cell(dr, &c);
    check(!strcmp(c.val, "1") && c.col == C_WARN, "DROPS: 1 since boot, amber (none in the last 5 s)");
    ovl_half(half * 101u / 100u, 1, fm1_ms);
    sys_info_cell(dr, &c);
    check(!strcmp(c.val, "2") && c.col == C_ERR, "DROPS: 2, red (one just now)");
    fm1_ms += 20u;
    op_enter(SCR_SYSTEM);
    ui.row[SCR_SYSTEM] = 0;
    frame();
    check(px_in(0, OH_HEAD - 2u, 240, 2, C_ERR), "a drop in the last second: the header's rule red");
    op_enter(SCR_HOME);
    frame();
    check(px_in(0, OH_HEAD - 2u, 240, 2, C_ERR), "... on every page (the mixer)");
    frames(OVL_MARK_MS / 16u + 2u);
    check(!px_in(0, OH_HEAD - 2u, 240, 2, C_ERR), "~1 s later: the rule as before");
    op_enter(SCR_SYSTEM);
    ui.row[SCR_SYSTEM] = 0;
    frame();
    press(B_OCTDN);
    tap(B_SAVE);
    release(B_OCTDN);
    sys_info_cell(dr, &c);
    check(!strcmp(c.val, "0") && c.col == C_OK && ovl.drops == 2u, "SHIFT + YES on INFO: DROPS from 0 (the counter itself goes on)");
    check(ui.scr == SCR_SYSTEM && !lay.chord, "... and no redo, no PROJECT");
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
    memset((void *)&ovl, 0, sizeof ovl);
    reset_ui();
}
