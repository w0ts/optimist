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
    ok = dl.src[2] == DL_KIT && presets_step_inplace(1) && drum_kit_pos() != kit;   /* (SRC KIT: the kit, 2026-10-12) */
    ok &= (uint32_t)presets_step_inplace(-1) && drum_kit_pos() == kit && dl.src[2] == DL_KIT;
    check(ok && song.sel == TRK_DRUM && mx_row() == MXR_LANE0 + 2u,
          "PRESETS on a drum lane's row (SRC KIT): the kit changes in place both ways, the cursor stays on the lane");
    tap(B_ENV);
    song.sel = 0;
    frames(2);
    ok = ui.scr == SCR_SOUND && presets_step_inplace(1) && presets_step_inplace(-1);
    check(ok, "PRESETS on SOUND: the preset (another engine's rows maybe) in place");
    reset_ui();
}
/* the mixer shows the cursor's group only: MASTER alone, the four channels, the 16 drum sounds; the step from one view
 * to the next is drawn in place */
static void mixer_views_tests(void)
{
    uint32_t ok;
    reset_ui();
    song.sel = 0;
    frames(2);
    ok = algo_step_inplace(-1) && mx_row() == MXR_MASTER && mxd.first == MXR_MASTER && mxd.last == MXR_T1;
    check(ok, "ALGORITHM past T1: MASTER alone, drawn in place");
    ok = algo_step_inplace(1) && mx_row() == MXR_T1 && mxd.first == MXR_T1 && mxd.last == MXR_LANE0;
    check(ok, "... back: the four channels, in place");
    while (mx_row() < MXR_DR)
        turn(EN_ALGO, 1);
    ok = algo_step_inplace(1) && mx_row() == MXR_LANE0 && mxd.first == MXR_LANE0 && mxd.last == MXR_LN &&
         px_in(0, OP_PY, 240, 1, lane_col(0));
    check(ok, "past DR: the drum lanes, in place, the kick's row framed");
    ok = algo_step_inplace(-1) && mx_row() == MXR_DR && mxd.first == MXR_T1;
    check(ok, "... back up: the channels, DR selected");
    reset_ui();
}
/* a track change shows its name in the header for ~1.5 s (TRACK 2, its engine, its preset), on every screen, synth
 * tracks only; drawn in place */
static void title_flash_tests(void)
{
    uint32_t ok, n;
    reset_ui();
    song.sel = 0;
    frames(OP_TFLASH_FRAMES + 2u);
    n = full_fills;
    turn(EN_ALGO, 1);
    frame();
    ok = song.sel == 1u && tf.t > 0u && px_in(2, 3, 4, 19, trk_col(1)) && px_in(120, 4, 116, 16, C_WHITE) && full_fills == n;
    check(ok, "ALGORITHM to T2 on the mixer: TRACK 2, its engine and preset in the header, in place");
    ppm("opt-title-flash-mix");
    frames(OP_TFLASH_FRAMES);
    check(tf.t == 0u && !px_in(120, 4, 116, 16, C_WHITE) && full_fills == n, "... ~1.5 s later the normal header again");
    tap(B_ENV);
    frames(2);
    turn(EN_ALGO, 1);
    frame();
    check(ui.scr == SCR_SOUND && song.sel == 2u && tf.t > 0u && px_in(120, 4, 116, 16, C_WHITE), "on SOUND (ENV) too: TRACK 3");
    ppm("opt-title-flash-env");
    turn(EN_ALGO, 1);
    frame();
    check(song.sel == TRK_DRUM && tf.t == 0u, "the drum track: no name flash (the header at once)");
    song.sel = 0;
    frames(2);
    check(tf.t > 0u, "a track selected another way (the editor, the mixer's follow): the flash too");
    reset_ui();
    frames(OP_TFLASH_FRAMES + 2u);
}
static void mix35_tests(void)
{
    preset_inplace_tests();
    mixer_views_tests();
    title_flash_tests();
}
