/* SPDX-License-Identifier: GPL-3.0-only */
/* The user, 2026-10-26: "holding mute and press and holding long a key mute the track and unmute on release" (op_input.c
 * hm_*), and "while in drum mode, hold an black key to select a drum lane, then a 4 line screen shows up with midi fx
 * like fill, rolls etc for that lane" (op_lanefx.c, FELUCCA_LANE_FX). Included by ui_optimist_test.c after kdown / kup */
static int muted_key(uint32_t k) { return mx_muted(mx_row_of_smute(smute_key(k))); }
static void hold_mute_tests(void)
{
    uint32_t k = WK(0);                                 /* (T1's key) */
    reset_ui();
    ui.scr = SCR_HOME;
    press(B_OCTDN);
    kdown(k);
    check(muted_key(k), "SHIFT + T1's key down: T1 muted at once");
    kup(k);
    check(muted_key(k), "... a tap let go: stays muted");
    key(k);
    check(!muted_key(k), "... tapped again: heard");
    kdown(k);
    frames(HOLD_FRAMES);
    check(muted_key(k), "SHIFT + the key held long: muted while held");
    kup(k);
    check(!muted_key(k), "... let go: heard again");
    key(k);                                             /* (muted) */
    kdown(k);
    frames(HOLD_FRAMES);
    check(!muted_key(k), "a muted track, the key held long: heard while held");
    kup(k);
    check(muted_key(k), "... let go: muted again");
    key(k);                                             /* (heard) */
    kdown(k);
    frames(HOLD_FRAMES);
    release(B_OCTDN);
    check(muted_key(k), "SHIFT let go first, the key still held: still muted");
    kup(k);
    check(!muted_key(k), "... the key let go: heard again");
    reset_ui();
}
/* a step held on a drum page: STEP's held-step cards over it, the page back when let go */
static void step_peek_tests(void)
{
    track_t *t;
    reset_ui();
    song.sel = TRK_DRUM;
    t = TSEL;
    steps_clear(t);
    ui.scr = SCR_HOME;
    kdown(WK(2));
    check(ui.scr == SCR_HOME, "drums, the mixer: a step key down: no cards yet");
    frames(HOLD_FRAMES);
    check(ui.scr == SCR_STEP && st_holding() && speek.prev == SCR_HOME, "... held past the hold time: STEP's held-step cards");
    kup(WK(2));
    check(ui.scr == SCR_HOME && !st.held, "... let go: the mixer again");
    key(WK(3));
    check(ui.scr == SCR_HOME && dstep_mask(&t->dstep[3]), "a tap: the step set, the mixer stays");
    song.sel = 0;
    ui.scr = SCR_HOME;
    kdown(WK(2));
    frames(HOLD_FRAMES);
    check(ui.scr == SCR_HOME, "a synth track: the keys play, no cards");
    kup(WK(2));
    steps_clear(t);
    reset_ui();
}
#if FELUCCA_LANE_FX
/* PAGE_LINES: LANE FX keeps the 2x2 cards (the user, 2026-10-26: "use the 2*2 layout ... uniform with other pages") */
static void lane_fx_lines_tests(void)
{
    reset_ui();
    song.sel = TRK_DRUM;
    ui.scr = SCR_SOUND;
    kdown(BK(4));
    frames(HOLD_FRAMES);
    ui.force = 1;
    frame();
    check(ui.scr == SCR_LFX && !pl_on(), "PAGE_LINES: LANE FX as the 2x2 cards, not four lines");
    ppm("opt-lane-fx-lines");
    kup(BK(4));
    reset_ui();
}
static void lane_fx_ui_tests(void)
{
    track_t *t;
    reset_ui();
    song.sel = TRK_DRUM;
    t = TSEL;
    AL(t)->n = 0;
    ui.scr = SCR_SOUND;
    kdown(BK(4));
    check(ui.scr == SCR_SOUND && lane_selected() == 4u, "a lane's black key down: its lane selected, no page yet");
    frames(HOLD_FRAMES);
    check(ui.scr == SCR_LFX && lx.lane == 4u, "... held past the hold time: LANE FX for lane 4");
    turn(EN_K1, 3);
    turn(EN_K2, -2);
    turn(EN_K3, 1);
    turn(EN_K4, -4);
    check(lfx_get(t, 4, LFX_I_ROLL) == 3 && lfx_get(t, 4, LFX_I_CHANCE) == 90 && lfx_get(t, 4, LFX_I_FILL) == FC_FILL &&
          lfx_get(t, 4, LFX_I_NUDGE) == -4, "KNOB 1-4: ROLL x4, CHANCE 90 % (5 % a detent), FILL ONLY, NUDGE -4");
    {
        cell_t c;
        SCREENS[SCR_LFX].cell(0, LFX_I_ROLL, &c);
        check(!strcmp(c.label, "ROLL") && !strcmp(c.val, "x4"), "its card: ROLL x4");
    }
    ui.force = 1;
    frame();
    ppm("opt-lane-fx");
    press(B_OCTDN);
    turn(EN_K1, 1);
    release(B_OCTDN);
    check(lfx_get(t, 4, LFX_I_ROLL) == 0 && ui.scr == SCR_LFX, "SHIFT + KNOB 1: ROLL back to OFF, the page stays");
    turn(EN_SELECT, 1);
    turn(EN_K1, 5);
    turn(EN_K2, 2);
    turn(EN_K3, -4);
    check(ui.row[SCR_LFX] == 1u && lfx_get(t, 4, LFX_I_VRND) == 10 && lfx_get(t, 4, LFX_I_FLAM) == 2 &&
          lfx_get(t, 4, LFX_I_RAMP) == -20, "SELECT: LANE FX 2, KNOB 1-3: VRAND 10, FLAM 10 ms, RAMP -20 %");
    {
        cell_t c;
        SCREENS[SCR_LFX].cell(1, 1, &c);
        check(!strcmp(c.label, "FLAM") && !strcmp(c.val, "10") && !strcmp(c.unit, "ms"), "its card: FLAM 10 ms");
    }
    ui.force = 1;
    frame();
    ppm("opt-lane-fx2");
    turn(EN_SELECT, -1);
    turn(EN_K2, 3);                                     /* (90 % + 3 detents of 5: 100 %, no further) */
    check(lfx_get(t, 4, LFX_I_CHANCE) == 100 && !lfx_get(t, 4, LFX_I_EVERY), "CHANCE up to 100 %, not past it in one turn");
    turn(EN_K2, 2);
    {
        cell_t c;
        SCREENS[SCR_LFX].cell(0, 1, &c);
        check(lfx_get(t, 4, LFX_I_EVERY) == (FELUCCA_COND ? 2 : 0) && lfx_get(t, 4, LFX_I_CHANCE) == 100 &&
              (!FELUCCA_COND || !strcmp(c.val, "2:2")), "... past 100: EVERY 2:2 on the CHANCE card (with COND)");
    }
    turn(EN_K2, -3);
    check(!lfx_get(t, 4, LFX_I_EVERY) && lfx_get(t, 4, LFX_I_CHANCE) == 100, "... back down: 100 %, EVERY off");
    turn(EN_K2, -1);
    check(lfx_get(t, 4, LFX_I_CHANCE) == 95, "... then in percent again: 95 %");
    kup(BK(4));
    check(ui.scr == SCR_SOUND, "the key let go: the screen before");
#if FELUCCA_UNDO_HISTORY
    {                                                   /* one undo level a gesture: the knobs' turns until they rest */
        int32_t a, b;
        AL(t)->n = 0;
        kdown(BK(4));
        frames(HOLD_FRAMES);
        turn(EN_K1, 1);
        turn(EN_K1, 1);
        turn(EN_K3, 1);                                 /* (no pause: one gesture, ROLL x3 and FILL ONLY) */
        frames(LX_UNDO_MS / 16u + 2u);
        turn(EN_K2, -4);                                /* (after the rest: a second gesture, CHANCE 80) */
        kup(BK(4));
        a = lfx_get(t, 4, LFX_I_CHANCE);
        undo_apply(0);
        b = lfx_get(t, 4, LFX_I_ROLL);
        check(a == 80 && lfx_get(t, 4, LFX_I_CHANCE) == 100 && b == 2 && lfx_get(t, 4, LFX_I_FILL) == FC_FILL,
              "undo: the second gesture (CHANCE) undone alone, the first (ROLL, FILL) kept");
        undo_apply(0);
        check(!lfx_get(t, 4, LFX_I_ROLL) && !lfx_get(t, 4, LFX_I_FILL) && !AL(t)->n, "undo again: the first gesture, both its values");
        undo_apply(1);
        check(lfx_get(t, 4, LFX_I_ROLL) == 2 && lfx_get(t, 4, LFX_I_FILL) == FC_FILL, "redo: the first gesture back whole");
    }
#endif
    key(BK(5));
    check(ui.scr == SCR_SOUND && lane_selected() == 5u, "a tap: the lane selected, no page");
    ui.scr = SCR_STEP;
    kdown(BK(5));
    frames(HOLD_FRAMES);
    check(ui.scr == SCR_LFX && lx.prev == SCR_STEP && ly_lock == LY_STEP, "on STEP too: LANE FX, the keys still steps");
    steps_clear(t);
    key(WK(2));
    check(ui.scr == SCR_STEP && dstep_has(&t->dstep[2], 5) && dstep_lvl(&t->dstep[2], 5) == LV_NORM,
          "... a step pressed: the page goes, the lane's hit there (a lane key held + a step, as before)");
    kup(BK(5));
    ui.scr = SCR_HOME;
    kdown(BK(5));
    frames(HOLD_FRAMES);
    check(ui.scr == SCR_LFX && lx.prev == SCR_HOME, "the mixer: LANE FX");
    key(WK(2));
    check(ui.scr == SCR_HOME && dstep_lvl(&t->dstep[2], 5) == LV_HARD, "... a step pressed: the page goes, the level cycles (HARD)");
    kup(BK(5));
    steps_clear(t);
    ui.scr = SCR_SOUND;
    song.sel = 0;
    ui.scr = SCR_SOUND;
    kdown(BK(5));
    frames(HOLD_FRAMES);
    check(ui.scr == SCR_SOUND, "a synth track: no page");
    kup(BK(5));
    AL(TDRUM)->n = 0;
    reset_ui();
}
#else
static void lane_fx_lines_tests(void) {}
static void lane_fx_ui_tests(void) {}
#endif
