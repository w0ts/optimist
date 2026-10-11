/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, SYSTEM > CLOCK OUT (io/midi/clock_out.c, settings word bit 26): OFF by default; KNOB 1 on the
 * CLOCK OUT row (or YES on it) sets it; the clock it sends: tests/clock_out_test.c. Screen opt-clkout.ppm. */
static void clkout_tests(void)
{
#if FELUCCA_MIDI_CLKOUT
    uint32_t w;
    reset_ui();
    clko_on = 0;
    op_enter(SCR_SYSTEM);
    for (w = 0; w < SCR->rows(); w++) {                 /* the CLOCK OUT row */
        char nm[12];
        SCR->name(w, nm);
        if (!strcmp(nm, "CLOCK OUT"))
            break;
    }
    check(w < SCR->rows(), "SYSTEM: a CLOCK OUT row");
    ui.row[SCR_SYSTEM] = (uint8_t)w;
    ui.force = 1;
    frame();
    turn(EN_K1, 1);
    check(clko_on == 1u && ((bp23_word() >> 26) & 1u) == 1u, "SYSTEM CLOCK OUT: KNOB 1 right: ON (bit 26)");
    ui.force = 1;
    frame();
    frame();
    ppm("opt-clkout");
    turn(EN_K1, 1);
    check(clko_on == 1u, "SYSTEM CLOCK OUT: KNOB 1 stops at ON");
    turn(EN_K1, -1);
    check(clko_on == 0u && ((bp23_word() >> 26) & 1u) == 0u, "SYSTEM CLOCK OUT: KNOB 1 left: OFF");
    clko_on = 0;
    reset_ui();
#endif
}
