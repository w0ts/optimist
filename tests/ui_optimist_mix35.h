/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, the user's requests of 2026-10-11 (batch 35, docs/UI-OPTIMIST-DESIGN.md section 13):
 *   PRESETS repaints in place: a kit or a preset browsed on the mixer (a drum lane's row too) never clears the whole
 *   screen to black ("the screen flashes in mixer when i select drum lane presets")
 * Included by ui_optimist_test.c after ui_optimist_ble.h. */
static int presets_step_inplace(int32_t d)
{
    uint32_t n;
    frames(2);
    n = full_fills;
    turn(EN_PRESET, d);
    frames(2);
    return full_fills == n;
}
static void preset_inplace_tests(void)
{
    uint32_t ok, kit;
    reset_ui();
    song.sel = 0;
    frames(2);
    ok = presets_step_inplace(1) && presets_step_inplace(-1);
    check(ok, "PRESETS on the mixer's T1 row: the preset changes in place (no full clear)");
    while (mx_row() < MXR_LANE0 + 2u)
        turn(EN_ALGO, 1);
    kit = drum_kit_pos();
    ok = presets_step_inplace(1) && drum_kit_pos() != kit;
    ok &= (uint32_t)presets_step_inplace(-1) && drum_kit_pos() == kit;
    check(ok && song.sel == TRK_DRUM && mx_row() == MXR_LANE0 + 2u,
          "PRESETS on a drum lane's row: the kit changes in place both ways, the cursor stays on the lane");
    tap(B_ENV);
    song.sel = 0;
    frames(2);
    ok = ui.scr == SCR_SOUND && presets_step_inplace(1) && presets_step_inplace(-1);
    check(ok, "PRESETS on SOUND: the preset (another engine's rows maybe) in place");
    reset_ui();
}
static void mix35_tests(void)
{
    preset_inplace_tests();
}
