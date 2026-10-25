/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c: a user preset whose engine this build lacks is not in the PRESETS list (the FM-1, 2026-10-25:
 * a synth preset saved on a synth build, then drum-machines flashed: PRESETS stopped at the last factory sound, the step
 * onto the preset said MISSING and the sound stayed, every next turn the same) */

static void premiss_tests(void)
{
    uint32_t all, miss, i, k, e, u0 = 0, u1 = 0;
    reset_ui();
    song.sel = 0;
    pl_whole = 1;                                       /* (the PRESETS knob's list: every engine's sounds) */
    up_missing = 0;
    (void)preset_pos(&all);
    up_missing = 2u;                                    /* (user preset 1's engine left out of the build) */
    (void)preset_pos(&miss);
    for (i = 0; i < miss; i++) {
        e = preset_at(i, &k);
        u0 += (uint32_t)(e == NENGINES && k == 0u);
        u1 += (uint32_t)(e == NENGINES && k == 1u);
    }
    check(miss == all - 1u && u0 == 1u && !u1,
          "PRESETS: a user preset of an engine not built is not listed; the other user preset still is");
    up_missing = 0;
    reset_ui();
}
