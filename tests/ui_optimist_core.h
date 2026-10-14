/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, the second core on SYSTEM > INFO (FELUCCA_DUAL, system/dual_mode.h). 2026-10-14: there is no
 * 2ND CORE line any more, the core is always on; dual.c's guard still parks it and INFO's CPU1 line reads why (DUAL_WHY)
 * instead of its load. The self-disable lasts for the session (DUAL_MODE_MENU 0: nothing could set it back), and the
 * Optimist UI's settings load drops a stored OFF. A single-core build has no CPU1 line. */
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
        check(!strcmp(c.label, "CPU1") && !strcmp(c.val, "50") && c.col == 0, "CPU1 up: its load, 50 %");
        dual_c1_q8 = DUAL_C1_OFF;
        dual.why = DUAL_MISREAD;
        sys_info_cell(2, &c);
        check(!strcmp(c.val, "MISREAD") && !strcmp(c.unit, "") && c.col == C_AMB, "CPU1 down on a misread: its cell reads MISREAD");
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
        check(!strcmp(c.val, "OFF"), "CPU1 down with no reason: OFF");
        {
            uint32_t i, ok = 1;
            for (i = 1; i < DUAL_WHY_N; i++) {
                dual.why = (uint8_t)i;
                sys_info_cell(2, &c);
                ok &= !strcmp(c.val, dual_why_name(i));
            }
            check(ok, "CPU1 down: every reason's name is its cell's value");
        }
        dual_c1_q8 = q;
        dual.why = (uint8_t)why;
        dual_mode = DUALM_AUTO;
    }
#else
    check(line_named(SCR_SYSTEM, "CPU1") == SCREENS[SCR_SYSTEM].rows(), "a single-core build: no CPU1 line");
#endif
    reset_ui();
}
