/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, builder item SYS_DEBUG off (the user, 2026-10-17: "remove all the debugging stuff and replace by
 * what people want: CPU load, underruns and battery"): SYSTEM has no INTERNALS or USB STATS page; INFO is three read-outs,
 * CPU LOAD, UNDERRUNS (the drop-outs counted in: red just after one) and BATTERY, in the large face; KNOB 2 still walks
 * the CPU line's NOW / MAX 5S / MAX. Screen opt-info-lean.ppm. Built by tests/run_tests.sh with -DFELUCCA_SYS_DEBUG=0. */
static void sysdbg_off_tests(void)
{
#if !FELUCCA_SYS_DEBUG
    static const char *const L[3] = {"CPU LOAD", "UNDERRUNS", "BATTERY"};
    uint32_t i, ok = 1, half = OVL_HALF_US;
    cell_t c;
    reset_ui();
    memset((void *)&ovl, 0, sizeof ovl);
    info_mode = OVL_NOW;
    op_enter(SCR_SYSTEM);
    for (i = 0; i < SCREENS[SCR_SYSTEM].rows(); i++) {
        SCREENS[SCR_SYSTEM].cell(i, gl_kn(SCR_SYSTEM, i), &c);
        ok &= !c.label || (strcmp(c.label, "INTERNALS") && strcmp(c.label, "USB STATS"));
    }
    check(ok, "SYS_DEBUG off: SYSTEM has no INTERNALS and no USB STATS page");
    for (i = 0, ok = INFO_N == 3u && info_lines(INFO_PG_DIAG) == 3u; i < INFO_N; i++) {
        sys_info_cell(i, &c);
        ok &= c.kind == CK_RO && !strcmp(c.label, L[i]);
    }
    check(ok, "SYS_DEBUG off: INFO is CPU LOAD, UNDERRUNS, BATTERY");
    song.cpu_q8 = 50u * 256u / 100u + 1u;
    ui.row[SCR_SYSTEM] = 0;                             /* (INFO shown: KNOB 2 is its measure) */
    frame();
    sys_info_cell(0, &c);
    check(!strcmp(c.val, "50") && c.col == C_OK, "SYS_DEBUG off: CPU LOAD 50 %, green");
    turn(EN_K2, 1);
    sys_info_cell(0, &c);
    check(info_mode == OVL_MAX5 && !strcmp(c.label, "CPU LOAD MAX 5S"), "SYS_DEBUG off: KNOB 2 on INFO: CPU LOAD MAX 5S");
    turn(EN_K2, -1);
    sys_info_cell(1, &c);
    check(!strcmp(c.val, "0") && c.col == C_OK, "SYS_DEBUG off: UNDERRUNS 0, green");
    ovl_half(half * 101u / 100u, 1, fm1_ms);            /* (a late half: a drop-out) */
    sys_info_cell(1, &c);
    check(!strcmp(c.val, "1") && c.col == C_ERR, "SYS_DEBUG off: a drop-out counts in UNDERRUNS, red just after it");
    ui.row[SCR_SYSTEM] = 0;
    ui.force = 1;
    frame();
    ppm("opt-info-lean");
    {   /* the values in the large face: its digits taller than the small face's on the CPU LOAD line */
        int32_t y, top = 999, bot = -1;
        for (y = OH_HEAD; y < OH_HEAD + gl_pitch(); y++)
            if (px_in(150, (uint32_t)y, 86, 1, C_OK))
                top = y < top ? y : top, bot = y;
        check(bot - top > 14, "SYS_DEBUG off: INFO's values in the large face (a digit ~20 rows, the small face's ~10)");
    }
    memset((void *)&ovl, 0, sizeof ovl);
    reset_ui();
#endif
}
