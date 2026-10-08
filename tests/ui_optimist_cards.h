/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, SYSTEM > SCREEN > CARDS: the cursor row's values as four cards in a line (1x4) or as
 * SLOOP 2.4's big values 2 x 2 (docs/UI-OPTIMIST-DESIGN.md sections 3 and 11.6), on every screen with cards.
 * Included by ui_optimist_test.c after its helpers and ui_optimist_drummix.h (the settings word). */

/* the cards' band as drawn for the mode: 1x4 has black under its cards (band row 44), 2x2 its two rows of big
 * values (the second row's surface at band rows 47..92) */
static int cards_band_is(uint32_t mode)
{
    uint32_t k, ok = 1;
    if (mode == CARDS_LINE)
        return px_in(4, OY_CARD + 2u, 50, 20, OP_SURF) && !px_in(0, OY_CARD + OH_CARD, 240, 2, OP_SURF);
    for (k = 0; k < 4u; k++) {                          /* each quadrant's surface, where the knob sits */
        uint32_t x = k & 1u ? 121u : 3u, y = OY_CARD + (k & 2u ? BIG_H + 1u : 0u);
        ok &= (uint32_t)px_in(x + BIG_W - 6u, y + BIG_H - 2u, 4, 1, OP_SURF);
    }
    return (int)ok;
}
/* screen scr drawn whole (forced) in both modes: the band as the mode, nothing of the panel above OP_PY */
static int cards_screen_ok(uint32_t scr, const char *shot)
{
    uint32_t m, ok = 1;
    char nm[48];
    for (m = 0; m < CARDS_N; m++) {
        op_cards = (uint8_t)m;
        ui.force = 1;
        frame();
        frame();
        ok &= (uint32_t)cards_band_is(m) && ui.scr == scr;
        if (!ok) {
            printf("  cards: screen %u mode %u\n", scr, m);
            break;
        }
        if (m == CARDS_2X2 && shot) {
            snprintf(nm, sizeof nm, "opt-cards-2x2-%s", shot);
            ppm(nm);
        }
    }
    op_cards = CARDS_LINE;
    ui.force = 1;
    frame();
    return (int)ok;
}
static void cards_tests(void)
{
    uint32_t r, n, row_nograph = 0xFF;
    reset_ui();
    song.sel = 0;
    frame();
    check(op_cards == CARDS_LINE && ROWS_SHOWN == 8u && GRAPH_H == 58 && OP_PY == OY_PANEL,
          "CARDS: four in a line by default, the panel at 76, 8 rows, the graph 58");
    op_cards = CARDS_2X2;
    check(ROWS_SHOWN == 5u && GRAPH_H == 40 && OP_PY == OY_PANEL_2X2 && OP_CH + OY_CARD <= OP_PY && SC_H > 80 &&
          4 * TILE_H + 2 + 16 <= OP_PH && SG_INFO_Y + SG_INFO_DY + 13 <= OP_PH && MODAL_H == OP_PH,
          "CARDS 2x2: the band 93, the panel 117: 5 rows, graph 40, STEP, tiles, scope and modal inside");
    op_cards = CARDS_LINE;
    tap(B_ENV);                                         /* SOUND with a graph (the envelope) */
    check(cards_screen_ok(SCR_SOUND, "sound-env"), "CARDS both modes: SOUND with its graph (ENV)");
    mix_to_sound();                                     /* the mixer, YES on the track: every row */
    n = SCR->rows();
    for (r = 1; r < n && row_nograph == 0xFF; r++)
        if (snd_page(r) && !snd_graph_page(r))
            row_nograph = r;
    ui.row[SCR_SOUND] = (uint8_t)(row_nograph == 0xFF ? 0 : row_nograph);
    frame();
    check(row_nograph != 0xFF && cards_screen_ok(SCR_SOUND, "sound-plain"), "CARDS both modes: SOUND on a row with no graph");
    ui.row[SCR_SOUND] = 0;
    check(cards_screen_ok(SCR_SOUND, 0), "CARDS both modes: SOUND's SOUND row (the presets' picture)");
    reset_ui();
    tap(B_SEQ);
    check(cards_screen_ok(SCR_STEP, "step"), "CARDS both modes: STEP, a synth track's roll");
    song.sel = TRK_DRUM;
    frame();
    check(cards_screen_ok(SCR_STEP, "step-drums"), "CARDS both modes: STEP, the drum grid");
    song.sel = 0;
    reset_ui();
    op_enter(SCR_TEMPO);
    frame();
    check(cards_screen_ok(SCR_TEMPO, "tempo"), "CARDS both modes: TEMPO");
    {
        static const uint8_t S[] = {SCR_SONG, SCR_FX, SCR_PROJECT, SCR_SYSTEM, SCR_SCOPE};
        uint32_t i, ok = 1;
        for (i = 0; i < sizeof S; i++) {
            reset_ui();
            op_enter(S[i]);
            frame();
            ok &= (uint32_t)cards_screen_ok(S[i], S[i] == SCR_SCOPE ? "scope" : 0);
        }
        check(ok, "CARDS both modes: SONG, FX, PROJECT, SYSTEM, SCOPE");
    }
    reset_ui();
    press(B_FX);                                        /* a layer held: its cards and tiles */
    frames(HOLD_FRAMES);
    {
        uint32_t m, ok = lay.shown == LY_FX;
        for (m = 0; m < CARDS_N && ok; m++) {
            op_cards = (uint8_t)m;
            ui.force = 1;
            frame();
            ok &= (uint32_t)cards_band_is(m);
            if (m == CARDS_2X2)
                ppm("opt-cards-2x2-layer-fx");
        }
        op_cards = CARDS_LINE;
        check(ok, "CARDS both modes: a performance layer (FX held), its cards and its tiles");
    }
    release(B_FX);
    reset_ui();
    {   /* the hot value in white in its quadrant (KNOB 4: bottom right) */
        tap(B_ENV);
        op_cards = CARDS_2X2;
        turn(EN_K4, 1);
        ui.force = 1;
        frame();
        check(ui.hot == 3u && px_in(126, OY_CARD + BIG_H + 13u, 110, 30, C_WHITE) &&
              !px_in(8, OY_CARD + 13u, 110, 28, C_WHITE),
              "CARDS 2x2: KNOB 4 hot, its value white at the bottom right, KNOB 1's not");
        op_cards = CARDS_LINE;
    }
    reset_ui();
    {   /* the setting: SYSTEM > SCREEN's third cell, kept in the settings word (bit 23) */
        uint32_t w;
        op_enter(SCR_SYSTEM);
        ui.row[SCR_SYSTEM] = 0;
        frame();
        turn(EN_K3, 1);
        w = bp23_word();
        check(op_cards == CARDS_2X2 && ((w >> 23) & 1u), "SYSTEM SCREEN: KNOB 3 CARDS to 2x2, kept in the settings word (bit 23)");
        bp23_from_word(w & ~(1u << 23));
        check(op_cards == CARDS_LINE, "... a word without the bit: four in a line");
        bp23_from_word(w);
        check(op_cards == CARDS_2X2, "... read back: 2x2");
        op_cards = CARDS_LINE;
    }
    op_cards = CARDS_2X2;                               /* random use in 2x2: every draw on the screen */
    fuzz(3000, 777);
    op_cards = CARDS_LINE;
    check(1, "3000 frames of random use in CARDS 2x2: every draw on the screen");
    reset_ui();
}
