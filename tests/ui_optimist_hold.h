/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, SYSTEM > CALIBRATE > HOLD: a click is a tap, a hold shows the layer (docs/UI-OPTIMIST-DESIGN.md
 * section 11.6; the FM-1 showed the map one click in two at SLOOP's 140 ms). Included after the helpers. */

/* FX held ms milliseconds, then let go: whether its map showed, whether it counted as a tap (SOUND's FX rows) */
static void hold_fx(uint32_t ms, int *map, int *tapped)
{
    uint32_t t0;
    reset_ui();
    press(B_FX);
    t0 = fm1_ms;
    *map = 0;
    while (fm1_ms - t0 < ms) {
        frame();
        *map |= lay.shown == LY_FX;
    }
    release(B_FX);
    *tapped = ui.scr == SCR_SOUND && snd_fam == FAM_FX;
}
static void hold_tests(void)
{
    int map, tapped;
    uint32_t w;
    check(op_hold_ms() == 350u, "HOLD: 350 ms by default");
    hold_fx(200, &map, &tapped);
    check(!map && tapped, "a 200 ms click on FX: a tap (its rows), no map");
    hold_fx(300, &map, &tapped);
    check(!map && tapped, "a 300 ms click: a tap, no map");
    hold_fx(420, &map, &tapped);
    check(map && tapped, "a 420 ms press: the map shows; let go within HOLD + 150 ms it is still a tap");
    hold_fx(600, &map, &tapped);
    check(map && !tapped, "a 600 ms hold: the map, no tap");
    reset_ui();
    op_enter(SCR_SYSTEM);
    for (w = 0; w < SCR->rows(); w++) {                 /* the CALIBRATE row, KNOB 2 */
        char nm[12];
        SCR->name(w, nm);
        if (!strcmp(nm, "CALIBRATE"))
            break;
    }
    ui.row[SCR_SYSTEM] = (uint8_t)w;
    frame();
    turn(EN_K2, 1);
    check(op_hold_ms() == 500u, "SYSTEM CALIBRATE: KNOB 2 HOLD to 500 ms");
    hold_fx(420, &map, &tapped);
    check(!map && tapped, "... a 420 ms press is then a tap, no map");
    w = bp23_word();
    bp23_from_word(w & ~(3u << 21));
    check(op_hold_ms() == 350u, "HOLD in the settings word: a word without the bits reads 350 ms");
    bp23_from_word(w);
    check(op_hold_ms() == 500u, "... read back: 500 ms");
    hold_sel = 1u;
    hold_fx(300, &map, &tapped);
    check(map && tapped, "HOLD 250: a 300 ms press shows the map, still a tap");
    hold_sel = 0u;
    {   /* PLAY: the TEMPO page at the same threshold */
        uint32_t t0;
        reset_ui();
        press(B_PLAY);
        t0 = fm1_ms;
        while (fm1_ms - t0 < 300u)
            frame();
        map = ui.scr == SCR_TEMPO;
        while (fm1_ms - t0 < 420u)
            frame();
        check(!map && ui.scr == SCR_TEMPO, "PLAY held: TEMPO after HOLD (not at 300 ms, at 420 ms)");
        release(B_PLAY);
        frames(2);
        if (song.playing)
            tap(B_PLAY);
    }
    reset_ui();
}
