/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, SYSTEM > USB AUDIO (io/usb/ua_tap.h): four lines CH1..CH4, each one channel's source as text
 * (no bar); the defaults T1 T2 T3 DRUMS; a line's knob walks the list (stops at the ends), YES steps it round, OCT- held
 * + the knob its default; the choice reaches the routing (ua_ident, the lane taps). Screen opt-usbaudio.ppm. */
static void uasel_tests(void)
{
#if FELUCCA_USB_AUDIO
    static const char *const CH[4] = {"CH1", "CH2", "CH3", "CH4"};
    static const char *const DEF[4] = {"T1", "T2", "T3", "DRUMS"};
    uint32_t w[4], k;
    cell_t c;
    reset_ui();
    ua_route_default();
    for (k = 0; k < 4u; k++) {
        char nm[12];
        w[k] = line_named(SCR_SYSTEM, CH[k]);
        check(w[k] < SCR->rows(), "SYSTEM: a USB AUDIO line for each capture channel");
        SCR->name(w[k], nm);
        check(!strcmp(nm, "USB AUDIO"), "SYSTEM: the CH lines on the USB AUDIO page");
        SCR->cell(w[k], gl_kn(SCR_SYSTEM, w[k]), &c);
        check(!strcmp(c.val, DEF[k]) && c.gk == GK_NONE, "SYSTEM USB AUDIO: today's routing by default, text only");
    }
    check(w[1] == w[0] + 1u && w[2] == w[1] + 1u && w[3] == w[2] + 1u, "SYSTEM USB AUDIO: CH1..CH4, one line each, in order");
    line_named(SCR_SYSTEM, "CH1");
    ui.force = 1;
    frame();
    ppm("opt-usbaudio");
    turn(LINE_KNOB(w[0]), -1);
    SCR->cell(w[0], gl_kn(SCR_SYSTEM, w[0]), &c);
    check(ua_src[0] == UAS_MAIN_S && !strcmp(c.val, "MAIN L+R") && !ua_ident,
          "SYSTEM USB AUDIO: CH1's knob left: MAIN L+R (one sum, (L + R) / 2), the routing on");
    line_named(SCR_SYSTEM, "CH4");
    for (k = 0; k < 40u; k++)
        turn(LINE_KNOB(w[3]), 1);
    SCR->cell(w[3], gl_kn(SCR_SYSTEM, w[3]), &c);
    check(ua_src[3] == UAS_N - 1u && !strcmp(c.val, "DR10") && (dlt_mask >> 9 & 1u), "SYSTEM USB AUDIO: CH4 stops at DR10, lane 10 tapped");
    tap(B_SAVE);
    check(ua_src[3] == UAS_OFF && !dlt_mask, "SYSTEM USB AUDIO: YES steps round to OFF");
    turn(LINE_KNOB(w[3]), OP_RESET);                    /* (OCT- held + the knob) */
    line_named(SCR_SYSTEM, "CH1");
    turn(LINE_KNOB(w[0]), OP_RESET);
    check(ua_src[3] == UAS_DRUMS && ua_src[0] == UAS_T1 && ua_ident, "SYSTEM USB AUDIO: the factory value: the default routing");
    ua_route_default();
    reset_ui();
#endif
}
