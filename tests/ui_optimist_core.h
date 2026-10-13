/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, SYSTEM > CPU > 2ND CORE (FELUCCA_DUAL, system/dual_mode.h; settings word bits 27..28) and the
 * CPU1 cell's reason. A DUAL build (DUAL_LOAD here) has a 2ND CORE AUTO / OFF line as the fourth line of SYSTEM's first page,
 * after CLOCK, CPU0 and CPU1: a knob or PRESETS sets it, YES the other, HOME held + PRESETS back to AUTO; it is saved as the
 * settings word is (SYSTEM left). When the core is down CPU1's cell reads why (DUAL_WHY) instead of OFF. A single-core
 * build has neither. */
static void core_row_tests(void)
{
#if DUAL_LOAD
    uint32_t w, w1, q = dual_c1_q8, why = dual.why, bm = dual.boot_mode;
    cell_t c;
    char nm[12];
    reset_ui();
    dual_mode = DUALM_AUTO;
    dual.boot_mode = DUALM_AUTO;
    dual.why = DUAL_UP;
    sys_dirty = 0;
    w = line_named(SCR_SYSTEM, "2ND CORE");
    w1 = line_named(SCR_SYSTEM, "CPU1");
    SCREENS[SCR_SYSTEM].name(w, nm);
    SCREENS[SCR_SYSTEM].cell(w, w % 4u, &c);
    check(w < SCREENS[SCR_SYSTEM].rows() && w == w1 + 1u && !strcmp(nm, "CPU") && c.kind == CK_VAL && !strcmp(c.val, "AUTO"),
          "2ND CORE (DUAL): a line of SYSTEM's CPU section, right after CPU1, AUTO by default");
    check(w1 / 4u == 0u && w / 4u == 0u && w % 4u == 3u && line_named(SCR_SYSTEM, "CPU1") == w1,
          "2ND CORE (DUAL): the fourth line of the first page (CLOCK, CPU0, CPU1, 2ND CORE); KNOB 4 sets it");
    {   /* the page layout: the next section starts the second page */
        char nx[12];
        SCREENS[SCR_SYSTEM].name(w + 1u, nx);
        check(strcmp(nx, "CPU") && (w + 1u) % 4u == 0u, "2ND CORE (DUAL): the next section starts a new page");
    }
    line_named(SCR_SYSTEM, "2ND CORE");
    turn(LINE_KNOB(w), 1);
    SCREENS[SCR_SYSTEM].cell(w, w % 4u, &c);
    check(dual_mode == DUALM_OFF && !strcmp(c.val, "OFF") && sys_dirty && ui.row[SCR_SYSTEM] == w,
          "2ND CORE: its knob right: OFF (dual_mode 1), a setting to save, the cursor on it");
    check(((dual_mode_out(0u, dual_mode) >> DUAL_MODE_BITS) & 3u) == 1u && dual_mode_in(dual_mode_out(0u, dual_mode)) == DUALM_OFF,
          "2ND CORE: OFF is the settings word's bits 27..28 = 1 (read back as OFF)");
    check(c.col == C_AMB, "2ND CORE: amber while the next start applies it (this boot started with AUTO)");
    turn(LINE_KNOB(w), 1);
    check(dual_mode == DUALM_OFF, "2ND CORE: its knob stops at OFF");
    turn(LINE_KNOB(w), -1);
    SCREENS[SCR_SYSTEM].cell(w, w % 4u, &c);
    check(dual_mode == DUALM_AUTO && !strcmp(c.val, "AUTO") && c.col == 0, "2ND CORE: its knob left: AUTO, not amber (the boot's setting)");
    turn(EN_PRESET, 1);
    check(dual_mode == DUALM_AUTO && ui.scr == SCR_SYSTEM, "2ND CORE: PRESETS does nothing here (2026-10-13)");
    turn(LINE_KNOB(w), 1);
    press(B_HOME);
    turn(LINE_KNOB(w), 1);
    release(B_HOME);
    check(dual_mode == DUALM_AUTO && ui.scr == SCR_SYSTEM, "2ND CORE: HOME held + its knob: back to AUTO (HOME no NO)");
    tap(B_SAVE);
    check(dual_mode == DUALM_OFF, "2ND CORE: YES toggles it");
    tap(B_SAVE);
    check(dual_mode == DUALM_AUTO, "2ND CORE: YES again: AUTO");
    {   /* a self-disable (dual_mode_after, DUAL_MODE_MENU 1 in this UI) is saved: it shows as OFF, amber, and AUTO resets it */
        dual.boot_mode = DUALM_AUTO;
        dual_mode = (uint8_t)dual_mode_after(DUALM_AUTO, DUALM_AUTO, DUAL_MISREAD);
        SCREENS[SCR_SYSTEM].cell(w, w % 4u, &c);
        check(DUAL_MODE_MENU == 1 && dual_mode == DUALM_OFF && !strcmp(c.val, "OFF"),
              "2ND CORE: a misread's self-disable is kept (DUAL_MODE_MENU 1) and reads OFF here");
        press(B_HOME);
        turn(LINE_KNOB(w), 1);
        release(B_HOME);
        check(dual_mode == DUALM_AUTO, "2ND CORE: ... and HOME + its knob sets it back to AUTO");
    }
    {   /* CPU1's cell: the load while it runs, else why it is down (dual.why), OFF with no reason */
        dual_c1_q8 = 128u;
        dual.why = DUAL_UP;
        SCREENS[SCR_SYSTEM].cell(w1, w1 % 4u, &c);
        check(!strcmp(c.label, "CPU1") && !strcmp(c.val, "50") && c.col == 0, "CPU1 up: its load, 50 %");
        dual_c1_q8 = DUAL_C1_OFF;
        dual.why = DUAL_MISREAD;
        SCREENS[SCR_SYSTEM].cell(w1, w1 % 4u, &c);
        check(!strcmp(c.val, "MISREAD") && !strcmp(c.unit, "") && c.col == C_AMB, "CPU1 down on a misread: its cell reads MISREAD");
        dual.why = DUAL_CANARY;
        SCREENS[SCR_SYSTEM].cell(w1, w1 % 4u, &c);
        check(!strcmp(c.val, "CANARY"), "CPU1 down on the canary: CANARY");
        dual.why = DUAL_EARLY;
        SCREENS[SCR_SYSTEM].cell(w1, w1 % 4u, &c);
        check(!strcmp(c.val, "AFTER CRASH") && strlen(c.val) < sizeof c.val, "CPU1 held after a crash: AFTER CRASH (it fits the cell)");
        dual.why = DUAL_SETOFF;
        SCREENS[SCR_SYSTEM].cell(w1, w1 % 4u, &c);
        check(!strcmp(c.val, "SETTING"), "CPU1 down by 2ND CORE OFF: SETTING");
        dual.why = DUAL_UP;
        SCREENS[SCR_SYSTEM].cell(w1, w1 % 4u, &c);
        check(!strcmp(c.val, "OFF"), "CPU1 down with no reason: OFF");
        {
            uint32_t i, ok = 1;
            for (i = 1; i < DUAL_WHY_N; i++) {
                dual.why = (uint8_t)i;
                SCREENS[SCR_SYSTEM].cell(w1, w1 % 4u, &c);
                ok &= !strcmp(c.val, dual_why_name(i));
            }
            check(ok, "CPU1 down: every reason's name is its cell's value");
        }
    }
    dual_c1_q8 = q;
    dual.why = (uint8_t)why;
    dual.boot_mode = (uint8_t)bm;
    dual_mode = DUALM_AUTO;
    sys_dirty = 0;
    reset_ui();
#else
    reset_ui();
    check(line_named(SCR_SYSTEM, "2ND CORE") == SCREENS[SCR_SYSTEM].rows(), "2ND CORE: no such line in a single-core build");
    reset_ui();
#endif
}
