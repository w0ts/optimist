/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, GLOBAL > MIDI > CLK OUT (SYSTEM's until 2026-10-12; io/midi/clock_out.c, settings word bit 26): OFF by default; KNOB 1 on the
 * CLOCK OUT row (or YES on it) sets it; the clock it sends: tests/clock_out_test.c. Screen opt-clkout.ppm. */
static void clkout_tests(void)
{
#if FELUCCA_MIDI_CLKOUT
    uint32_t w;
    reset_ui();
    clko_on = 0;
    tap(B_GLO);                                         /* (GLOBAL: the global settings, 2026-10-12) */
    w = line_named(SCR_GLOBAL, "CLK OUT");
    {
        char nm[12];
        SCR->name(w, nm);
        check(w < SCR->rows() && !strcmp(nm, "MIDI"), "GLOBAL: a CLK OUT line on the MIDI page (GLO tapped)");
    }
    ui.force = 1;
    frame();
#if FELUCCA_BLE
    static const uint8_t ORD[] = {CLKO_OFF, CLKO_USB, CLKO_BLE, CLKO_BOTH};   /* the list as shown: OFF USB BLE BOTH */
#else
    static const uint8_t ORD[] = {CLKO_OFF, CLKO_USB};
#endif
    {
        uint32_t i, ok = 1, bit = 1;
        for (i = 0; i < sizeof ORD; i++) {
            ok &= clko_on == ORD[i];
            bit &= ((bp23_word() >> 26) & 1u) == (ORD[i] != CLKO_OFF);
            if (i == 1u) {
                ui.force = 1;
                frame();
                frame();
                ppm("opt-clkout");
            }
            turn(LINE_KNOB(w), 1);
        }
        check(ok && bit, "GLOBAL CLK OUT: its knob right steps OFF, USB, BLE, BOTH as built (without BLE: OFF, USB); bit 26 = on");
        check(clko_on == ORD[sizeof ORD - 1u], "GLOBAL CLK OUT: its knob stops at the last");
        for (i = sizeof ORD; i-- > 0;) {
            ok &= clko_on == ORD[i];
            turn(LINE_KNOB(w), -1);
        }
        check(ok && clko_on == CLKO_OFF && ((bp23_word() >> 26) & 1u) == 0u, "GLOBAL CLK OUT: its knob left walks back to OFF (bit 26 clear)");
    }
    clko_on = 0;
    reset_ui();
#endif
}
