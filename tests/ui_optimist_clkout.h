/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, GLOBAL > CLOCK OUT (SYSTEM's until 2026-10-12; io/midi/clock_out.c, settings word bit 26): OFF by default; KNOB 1 on the
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
        check(w < SCR->rows() && !strcmp(nm, "CLOCK OUT"), "GLOBAL: a CLK OUT line in its Clock out section (GLO tapped)");
    }
    ui.force = 1;
    frame();
    turn(LINE_KNOB(w), 1);
    check(clko_on == 1u && ((bp23_word() >> 26) & 1u) == 1u, "GLOBAL CLK OUT: its knob right: ON (bit 26)");
    ui.force = 1;
    frame();
    frame();
    ppm("opt-clkout");
    turn(LINE_KNOB(w), 1);
    check(clko_on == 1u, "GLOBAL CLK OUT: its knob stops at ON");
    turn(LINE_KNOB(w), -1);
    check(clko_on == 0u && ((bp23_word() >> 26) & 1u) == 0u, "GLOBAL CLK OUT: its knob left: OFF");
    clko_on = 0;
    reset_ui();
#endif
}
