/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c: the MISSING line after a load stays in the header until HOME (the user, 2026-10-25: "it
 * should remain, as long as the user does not dismiss it with home"); another message takes the bar for its time, then
 * the line is back; HOME lets it go and does nothing else (storage/miss.c miss_txt, miss_dismiss) */

static void missline_tests(void)
{
#if FELUCCA_MISSING_WARN
    uint32_t scr;
    reset_ui();
    frame();
    str_cpy(miss_txt, "MISSING: PHYS T2", sizeof miss_txt);   /* (what miss_tick keeps after a load) */
    frames(400);                                        /* (~6.4 s: longer than the 2.5 s the line used to stay) */
    check(!strcmp(ui.msg, "MISSING: PHYS T2") && ui.msg_t, "MISSING after a load: still in the header after 6 s");
    ui_message("SAVED");
    frame();
    check(!strcmp(ui.msg, "SAVED"), "... another message takes the bar for its time");
    frames(200);
    check(!strcmp(ui.msg, "MISSING: PHYS T2") && ui.msg_t, "... then the MISSING line is back");
    scr = ui.scr;
    tap(B_HOME);
    frame();
    check(!miss_txt[0] && strcmp(ui.msg, "MISSING: PHYS T2") && ui.scr == scr,
          "... HOME lets it go and does nothing else (the same screen)");
    frames(200);
    check(strncmp(ui.msg, "MISSING", 7) || !ui.msg_t, "... and it does not come back");
    reset_ui();
#endif
}
