/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, the note played on the keys at the screen's bottom left (the user, 2026-10-24, after Melodee:
 * "display the notes (a#4) played by the user on the keyboard on the bottom left of the screen"): its name while the key
 * is down and about 1 s after it is let go, then the corner as it was. Included after kdown / kup */

static uint32_t nn_corner_hash(void)                    /* the corner the name takes (op_draw.c NN_X .. NN_W, NN_Y ..) */
{
    uint32_t x, y, h = 2166136261u;
    for (y = NN_Y; y < 240u; y++)
        for (x = 0; x < NN_W; x++)
            h = (h ^ screen[y * 240u + x]) * 16777619u;
    return h;
}

static void notename_tests(void)
{
    uint32_t before, k = WK(5), white = 0, x, y;
    reset_ui();
    song.sel = 0;
    op_enter(SCR_HOME);
    ui.force = 1;
    frames(3);
    before = nn_corner_hash();
    ppm("opt-notename-before");
    kdown(k);
    frames(2);
    for (y = NN_Y; y < 240u; y++)
        for (x = 0; x < NN_W; x++)
            white += screen[y * 240u + x] == swap16(C_WHITE);
    check(kb_shown == kb_nt[k][0] && white > 10u && nn_corner_hash() != before,
          "a key played: its note's name at the bottom left, in white");
    ppm("opt-notename");
    kup(k);
    frames(NN_FRAMES / 2u);
    check(nn_corner_hash() != before, "... still shown half a second after the key is let go");
    frames(NN_FRAMES);
    ppm("opt-notename-after");
    check(nn_corner_hash() == before, "... then gone: the corner as it was before the key");
}

/* #121 (2026-10-31): without the help line the name's end asked ui.force, which cleared the WHOLE screen to black and drew
 * it again (a blink ~1 s after a key). Now the bands are drawn again over it: no fill of the whole screen, the corner
 * as it was. Counted by full_fills (lcd_fill of 240 x 240) */
#if !FELUCCA_PARAM_HELP
static void notename_blink_tests(void)
{
    static const uint8_t scrs[] = { SCR_HOME, SCR_SOUND, SCR_FX, SCR_SCOPE, SCR_TEMPO, SCR_SYSTEM };
    uint32_t i, k = WK(5), before, n, ok = 1u, same = 1u;
    for (i = 0; i < sizeof scrs; i++) {
        reset_ui();
        song.sel = 0;
        op_enter(scrs[i]);
        ui.force = 1;
        frames(3);
        before = nn_corner_hash();
        kdown(k);
        frames(2);
        kup(k);
        frames(NN_FRAMES - 2u);
        n = full_fills;
        frames(6);
        ok &= full_fills == n;
        same &= nn_corner_hash() == before;
    }
    check(ok, "#121 no help line: the note name going (about 1 s after a key) does not clear the whole screen to black (HOME, SOUND, FX, SCOPE, TEMPO, SYSTEM)");
    check(same, "... the corner as it was before the key on each");
}
#else
static void notename_blink_tests(void) {}
#endif
