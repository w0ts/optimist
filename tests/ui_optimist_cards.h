/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, the cards (the user, 2026-10-12: "Cards are ALWAYS 2x2 on every screen with cards except the
 * mixer"; docs/UI-OPTIMIST-DESIGN.md sections 3 and 11.6): the cursor row's values as SLOOP 2.4's big values 2 x 2 on
 * every screen with cards, the mixer's four in a line (1x4). There is no CARDS setting any more: SYSTEM > SCREEN has no
 * CARDS cell and the settings word's bit 23 is neither read nor written. Included by ui_optimist_test.c after its
 * helpers and ui_optimist_drummix.h (the settings word). */
enum { BAND_1X4, BAND_2X2 };                            /* the two bands the screens draw (the mixer's, every other) */

/* the cards are black, outlined in C_LINE (no grey surface, the user 2026-10-10): a card is told by its outline's
 * right edge, at two heights (a roll's C line, horizontal, cannot be both) */
static int card_edge(uint32_t x, uint32_t y) { return px_at(x, y + 10u, C_LINE) && px_at(x, y + 30u, C_LINE); }
static int card0_up(void)                               /* the first card drawn, as the screen has them */
{
    return card_edge(cards_2x2() ? 3u + BIG_W - 1u : (uint32_t)CARD_X(0) + CARD_W - 1u, OY_CARD);
}
/* the cards' band as drawn: 1x4 one row of cards (the gap under it black: no big value's outline at band row 45), 2x2
 * its two rows of big values (the second row at band rows 47..92) */
static int cards_band_is(uint32_t band)
{
    uint32_t k, ok = 1;
    if (band == BAND_1X4)
        return card_edge((uint32_t)CARD_X(0) + CARD_W - 1u, OY_CARD) && !px_at(30u, OY_CARD + BIG_H - 1u, C_LINE);
    for (k = 0; k < 4u; k++) {                          /* each quadrant's outline */
        uint32_t x = k & 1u ? 121u : 3u, y = OY_CARD + (k & 2u ? BIG_H + 1u : 0u);
        ok &= (uint32_t)card_edge(x + BIG_W - 1u, y);
    }
    return (int)ok;
}
/* screen scr drawn whole (forced): the 2x2 band (STEP: the cards are an overlay, ui_optimist_stepcards.h) */
static int cards_screen_ok(uint32_t scr, const char *shot)
{
    char nm[48];
    int ok;
    ui.force = 1;
    frame();
    frame();
    ok = (scr == SCR_STEP || cards_band_is(BAND_2X2)) && ui.scr == scr && cards_2x2();
    if (!ok)
        printf("  cards: screen %u not 2x2\n", scr);
    if (shot) {
        snprintf(nm, sizeof nm, "opt-cards-2x2-%s", shot);
        ppm(nm);
    }
    return ok;
}
static void cards_tests(void)
{
    uint32_t r, n, row_nograph = 0xFF;
    reset_ui();
    song.sel = 0;
    frame();
    check(ui.scr == SCR_HOME && !cards_2x2() && OP_PY == OY_PANEL && cards_band_is(BAND_1X4),
          "the mixer: its four cards in a line (1x4), the panel at 76");
    op_enter(SCR_SOUND);
    check(cards_2x2() && ROWS_SHOWN == 5u && GRAPH_MIN == 40 && OP_PY == OY_PANEL_2X2 && OP_CH + OY_CARD <= OP_PY && SC_H > 80 &&
          4 * TILE_H + 2 + 16 <= OP_PH && SG_INFO_Y + SG_INFO_DY + 13 <= SP_H && MODAL_H == OP_PH,
          "the cards 2x2 off the mixer: the band 93, the panel 117: 5 rows, graph 40, STEP, tiles, scope and modal inside");
    tap(B_ENV);                                         /* SOUND with a graph (the envelope) */
    check(cards_screen_ok(SCR_SOUND, "sound-env"), "cards 2x2: SOUND with its graph (ENV)");
    sound_all();                                     /* the mixer, YES on the track: every row */
    n = SCR->rows();
    for (r = 1; r < n && row_nograph == 0xFF; r++)
        if (snd_page(r) && !snd_graph_page(r))
            row_nograph = r;
    ui.row[SCR_SOUND] = (uint8_t)(row_nograph == 0xFF ? 0 : row_nograph);
    frame();
    check(row_nograph != 0xFF && cards_screen_ok(SCR_SOUND, "sound-plain"), "cards 2x2: SOUND on a row with no graph");
    ui.row[SCR_SOUND] = 0;
    check(cards_screen_ok(SCR_SOUND, 0), "cards 2x2: SOUND's SOUND row (the presets' picture)");
    reset_ui();
    tap(B_SEQ);
    check(cards_screen_ok(SCR_STEP, "step"), "cards 2x2: STEP, a synth track's roll");
    song.sel = TRK_DRUM;
    frame();
    check(cards_screen_ok(SCR_STEP, "step-drums"), "cards 2x2: STEP, the drum grid");
    song.sel = 0;
    reset_ui();
    op_enter(SCR_TEMPO);
    frame();
    check(cards_screen_ok(SCR_TEMPO, "tempo"), "cards 2x2: TEMPO");
    {
        static const uint8_t S[] = {SCR_SONG, SCR_FX, SCR_PROJECT, SCR_SYSTEM, SCR_SCOPE, SCR_GLOBAL};
        uint32_t i, ok = 1;
        for (i = 0; i < sizeof S; i++) {
            reset_ui();
            op_enter(S[i]);
            frame();
            ok &= (uint32_t)cards_screen_ok(S[i], S[i] == SCR_SCOPE ? "scope" : S[i] == SCR_GLOBAL ? "global" : 0);
        }
        check(ok, "cards 2x2: SONG, FX, PROJECT, SYSTEM, SCOPE, GLOBAL");
    }
    reset_ui();
    op_enter(SCR_SOUND);
    press(B_FX);                                        /* a layer held (over SOUND): its cards and tiles */
    frames(HOLD_FRAMES);
    ui.force = 1;
    frame();
    ppm("opt-cards-2x2-layer-fx");
    check(lay.shown == LY_FX && cards_band_is(BAND_2X2), "cards 2x2: a performance layer (FX held), its cards and its tiles");
    release(B_FX);
    reset_ui();
    {   /* the hot value in white in its quadrant (KNOB 4: bottom right) */
        tap(B_ENV);
        turn(EN_K4, 1);
        ui.force = 1;
        frame();
        check(ui.hot == 3u && px_in(126, OY_CARD + BIG_H + 13u, 110, 30, C_WHITE) &&
              !px_in(8, OY_CARD + 13u, 110, 28, C_WHITE),
              "cards 2x2: KNOB 4 hot, its value white at the bottom right, KNOB 1's not");
    }
    reset_ui();
    {   /* no CARDS setting: SYSTEM > SCREEN's cells are COLOR, BRIGHT, -, KNOB COLORS; bit 23 ignored */
        uint32_t k, none = 1, w;
        cell_t c;
        op_enter(SCR_SYSTEM);
        ui.row[SCR_SYSTEM] = 0;
        frame();
        for (k = 0; k < 4u; k++) {
            SCR->cell(0, k, &c);
            none &= (uint32_t)!(c.label && !strcmp(c.label, "CARDS"));
        }
        SCR->cell(0, 2, &c);
        check(none && c.kind == CK_NONE, "SYSTEM SCREEN: no CARDS cell (KNOB 3 empty)");
        w = bp23_word();
        bp23_from_word(w | 1u << 23);                   /* an old record that chose 1x4 */
        op_enter(SCR_SOUND);
        tap(B_ENV);
        ui.force = 1;
        frame();
        check(cards_2x2() && cards_band_is(BAND_2X2) && ((bp23_word() >> 23) & 1u) == 1u,
              "settings word bit 23 set (an old 1x4): not read, the cards 2x2; the bit kept as read");
        bp23_from_word(w & ~(1u << 23));
        check(((bp23_word() >> 23) & 1u) == 0u, "... a word without it writes it as 0: never written by the UI");
        bp23_from_word(w);
    }
    fuzz(3000, 777);                                    /* random use: every draw on the screen */
    check(1, "3000 frames of random use with the 2x2 cards: every draw on the screen");
    reset_ui();
}
