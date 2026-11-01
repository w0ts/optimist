/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, SYSTEM > USB AUDIO (io/usb/ua_tap.h): a line a source (MASTER T1 T2 T3 DRUMS, DRUM BUS, DR1..DR10,
 * RET A B C: their own row, last),
 * its channels as text (no bar): NONE 1 2 1+2 3 4 3+4; the defaults T1 T2 T3 DRUMS on 1 2 3 4, the rest NONE; a line's knob
 * walks the list (stops at the ends), YES steps it round, OCT- held + the knob its default; the choice reaches the routing
 * (ua_ident, the lane taps, the returns'); the lines over USB AUDIO's pages, one section. Screen opt-usbaudio.ppm. */
static void uasel_tests(void)
{
#if FELUCCA_USB_AUDIO
    static const char *const SRC[6] = {"MASTER", "T1", "T2", "T3", "DRUMS", "DRUM BUS"};
    static const char *const DEF[6] = {"NONE", "1", "2", "3", "4", "NONE"};
    uint32_t w[6], k, ok = 1;
    cell_t c;
    char nm[12];
    reset_ui();
    ua_route_default();
    for (k = 0; k < 6u; k++) {
        w[k] = line_named(SCR_SYSTEM, SRC[k]);
        check(w[k] < SCR->rows(), "SYSTEM: a USB AUDIO line for each source");
        SCR->name(w[k], nm);
        check(!strcmp(nm, "USB AUDIO"), "SYSTEM: the source lines on the USB AUDIO pages");
        SCR->cell(w[k], gl_kn(SCR_SYSTEM, w[k]), &c);
        check(!strcmp(c.val, DEF[k]) && c.gk == GK_NONE, "SYSTEM USB AUDIO: today's routing by default, text only");
        ok &= !k || w[k] == w[k - 1u] + 1u;
    }
    check(ok, "SYSTEM USB AUDIO: MASTER T1 T2 T3 DRUMS DRUM BUS, one line each, in order");
    for (k = 0; k < DRUM_LANES; k++) {                  /* (the lanes after them: DR1..DR10) */
        char want[6];
        snprintf(want, sizeof want, "DR%u", k + 1u);
        ok &= line_named(SCR_SYSTEM, want) == w[5] + 1u + k;
        SCR->cell(w[5] + 1u + k, gl_kn(SCR_SYSTEM, w[5] + 1u + k), &c);
        ok &= !strcmp(c.val, "NONE");
    }
    check(ok, "SYSTEM USB AUDIO: DR1..DR10 after the drum bus, NONE by default");
    {   /* one section: the pages after the first go on with it (the header's title is USB audio on each) */
        uint32_t p0 = gl_pg(1, w[0]), pl = gl_pg(1, w[5] + DRUM_LANES);
        check(pl == p0 + 3u && gl_first(1, w[0]) == w[0], "SYSTEM USB AUDIO: four pages of four lines, MASTER first");
    }
    {   /* RET A B C: after DR10, a page of their own (the fifth, the section's still), NONE by default */
        static const char *const RET[3] = {"RET A", "RET B", "RET C"};
        uint32_t r0 = w[5] + DRUM_LANES + 1u;
        for (k = 0, ok = 1; k < 3u; k++) {
            ok &= line_named(SCR_SYSTEM, RET[k]) == r0 + k && gl_pg(1, r0 + k) == gl_pg(1, w[0]) + 4u;
            SCR->name(r0 + k, nm);
            ok &= !strcmp(nm, "USB AUDIO");
            SCR->cell(r0 + k, gl_kn(SCR_SYSTEM, r0 + k), &c);
            ok &= !strcmp(c.val, "NONE") && c.gk == GK_NONE;
        }
        check(gl_npg(1) <= GL_MAXP, "SYSTEM: its pages fit the page table (op_global.c GL_MAXP), the returns' page with them");
        check(ok && gl_first(1, r0 + 2u) == r0, "SYSTEM USB AUDIO: RET A B C after DR10, their own page, NONE by default");
        line_named(SCR_SYSTEM, "RET C");
        for (k = 0; k < 40u; k++)
            turn(LINE_KNOB(r0 + 2u), 1);
        SCR->cell(r0 + 2u, gl_kn(SCR_SYSTEM, r0 + 2u), &c);
        check(ua_dst[UAS_RA + 2] == UAD_34 && !strcmp(c.val, "3+4") && ua_ret_on == 4u && !ua_ident,
              "SYSTEM USB AUDIO: RET C stops at 3+4, the return routed");
        tap(B_SAVE);
        check(ua_dst[UAS_RA + 2] == UAD_NONE && !ua_ret_on && ua_ident, "SYSTEM USB AUDIO: RET C: YES steps round to NONE");
    }
    line_named(SCR_SYSTEM, "MASTER");
    ui.force = 1;
    frame();
    ppm("opt-usbaudio");
    for (k = 0; k < 3u; k++)
        turn(LINE_KNOB(w[0]), 1);
    SCR->cell(w[0], gl_kn(SCR_SYSTEM, w[0]), &c);
    check(ua_dst[UAS_MAIN] == UAD_12 && !strcmp(c.val, "1+2") && !ua_ident,
          "SYSTEM USB AUDIO: MASTER's knob right three detents: 1+2 (its L and R), the routing on");
    line_named(SCR_SYSTEM, "DR10");
    for (k = 0; k < 40u; k++)
        turn(LINE_KNOB(w[5] + DRUM_LANES), 1);
    SCR->cell(w[5] + DRUM_LANES, gl_kn(SCR_SYSTEM, w[5] + DRUM_LANES), &c);
    check(ua_dst[UAS_RA - 1u] == UAD_34 && !strcmp(c.val, "3+4") && (dlt_mask >> 9 & 1u), "SYSTEM USB AUDIO: DR10 stops at 3+4, lane 10 tapped");
    tap(B_SAVE);
    check(ua_dst[UAS_RA - 1u] == UAD_NONE && !dlt_mask, "SYSTEM USB AUDIO: YES steps round to NONE");
    line_named(SCR_SYSTEM, "T1");
    turn(LINE_KNOB(w[1]), -1);
    check(ua_dst[UAS_T1] == UAD_NONE, "SYSTEM USB AUDIO: T1 to NONE");
    turn(LINE_KNOB(w[1]), OP_RESET);                    /* (OCT- held + the knob) */
    line_named(SCR_SYSTEM, "MASTER");
    turn(LINE_KNOB(w[0]), OP_RESET);
    check(ua_dst[UAS_T1] == UAD_1 && ua_dst[UAS_MAIN] == UAD_NONE && ua_ident, "SYSTEM USB AUDIO: the factory value: the default routing");
    ua_route_default();
    reset_ui();
#endif
}
