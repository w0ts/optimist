/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c: what a load uses and this build lacks is a popup (the user, 2026-10-26: "this should have
 * been a modal popup when loading the project"): "Missing" over the items, amber, no time-out; HOME or YES closes it
 * and does nothing else (op_state.c miss_show, OA_MISSING) */

static void missline_tests(void)
{
#if FELUCCA_MISSING_WARN
    uint32_t scr;
    reset_ui();
    frame();
    miss_show("PHYS T2 +1");                            /* (what miss_tick hands over after a load) */
    frame();
    check(op_armed() && ui.arm_scr == ARM_OP && ui.arm_row == OA_MISSING && !strcmp(ui.arm_verb, "MISSING") &&
              !strcmp(ui.arm_arg, "PHYS T2 +1") && !ui.arm_danger,
          "MISSING after a load: a popup, Missing over the items, amber (said, not asked: no question mark)");
    frames(400);                                        /* (~6.4 s: past the 3 s a question waits) */
    check(op_armed() && ui.arm_row == OA_MISSING, "... still up after 6 s: it waits for the user");
    check(miss_busy(), "... a next load's popup waits for this one (miss_busy)");
    scr = ui.scr;
    tap(B_HOME);
    frame();
    check(!op_armed() && ui.scr == scr, "... HOME closes it and does nothing else (the same screen)");
    miss_show("KIT 909");
    frame();
    tap(B_SAVE);
    frame();
    check(!op_armed() && ui.scr == scr, "... SAVE (YES) closes it too, nothing run");
    reset_ui();
#endif
}
