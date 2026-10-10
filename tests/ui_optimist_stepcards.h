/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, STEP's cards as an overlay (the user, 2026-10-10: "we should show the 4 cards in step seq
 * only on page change and when modifying the values, like layover"): the grid takes the panel from y 25; the four
 * cards are drawn over its top on a page change or an edit, for OP_CARDS_FRAMES, and the grid is drawn whole again
 * when they go. Included by ui_optimist_test.c after ui_optimist_cards.h. */

/* two probes that tell the grid from the cards: lane row 2's white strip at x 2 (the cards' band is black there), and
 * x 4, y 30 (a card's surface; the grid has nothing but a lane's strip or black there) */
static int sc_grid_up(void) { return px_in(2, SP_Y + SG_TOP + 2 * SG_LH, 1, SG_LH - 1, C_WHITE) && !px_in(4, 30, 1, 1, OP_SURF); }
static int sc_cards_up(void) { return !px_in(2, SP_Y + SG_TOP + 2 * SG_LH, 1, SG_LH - 1, C_WHITE) && px_in(4, 30, 1, 1, OP_SURF) &&
                                       px_in(4, OY_CARD + 2, 50, 20, OP_SURF); }
static void sc_open_drums(void)
{
    reset_ui();
    song.sel = TRK_DRUM;
    tap(B_SEQ);
    lane_select(2);
    frames(3);
}
static void step_cards_tests(void)
{
    uint32_t m;
    for (m = 0; m < CARDS_N; m++) {
        op_cards = (uint8_t)m;
        sc_open_drums();
        check(ui.scr == SCR_STEP && ui.cards_t == 0u && sc_grid_up(), "STEP overlay: idle, the grid alone");
        check(SP_Y == 25 && SG_TOP + SG_H <= SP_H && px_in(2, SP_Y + SG_TOP, 6, SG_LH - 1, lane_col(0)), "STEP overlay: the panel starts under the header and runs to the foot");
        turn(EN_K1, 1);
        check(ui.cards_t > 0u && sc_cards_up(),
              "STEP overlay: a knob turned, the cards over the grid");
        frames(OP_CARDS_FRAMES - 10u);
        check(ui.cards_t > 0u && !px_in(2, SP_Y + SG_TOP + 2 * SG_LH, 1, SG_LH - 1, C_WHITE), "... still there before the timeout");
        turn(EN_K2, 1);                                 /* an edit restarts the timer */
        frames(OP_CARDS_FRAMES - 10u);
        check(ui.cards_t > 0u, "... another edit restarts the timer");
        frames(15);
        check(ui.cards_t == 0u && sc_grid_up(), "... after the timeout the grid is drawn whole again");
        /* a page change: SEQ tapped to the next STEP page */
        tap(B_SEQ);
        check(ui.cards_t > 0u, "STEP overlay: SEQ tapped (the next page) shows the cards");
        frames(OP_CARDS_FRAMES + 5u);
        check(ui.cards_t == 0u && !px_in(4, 30, 1, 1, OP_SURF), "... and they go");
        /* PRESETS on the drum track: the lane's cards */
        turn(EN_PRESET, 1);
        check(ui.cards_t > 0u, "STEP overlay: PRESETS picks the drum lane, the cards show");
        frames(OP_CARDS_FRAMES + 5u);
        /* a held step: SELECT pages its cards */
        kdown(WK(3));
        frames(2);
        check(ui.cards_t > 0u && sc_cards_up(), "STEP overlay: a step held shows the cards");
        frames(2u * OP_CARDS_FRAMES);
        check(ui.cards_t > 0u && sc_cards_up(), "... they stay up the whole time it is held");
        turn(EN_SELECT, 1);
        check(ui.cards_t > 0u, "STEP overlay: SELECT pages the held step's cards, shown");
        kup(WK(3));
        frames(OP_CARDS_FRAMES / 2u);
        check(ui.cards_t > 0u, "... after the release they wait the idle time");
        frames(OP_CARDS_FRAMES);
        check(ui.cards_t == 0u && ui.scr == SCR_STEP && !px_in(4, 30, 1, 1, OP_SURF), "... gone about 2 s after the release, the grid whole");
        /* the screen left: the overlay's timer is dropped, the other screens keep their cards */
        turn(EN_K1, 1);
        op_enter(SCR_SOUND);
        frames(3);
        check(ui.cards_t == 0u && cards_band_is(m), "other screens: their cards stay as they were");
        turn(EN_K1, 1);
        check(ui.cards_t == 0u, "other screens: a knob turn starts no overlay timer");
        /* a synth track's roll: same */
        reset_ui();
        song.sel = 0;
        tap(B_SEQ);
        frames(3);
        check(ui.cards_t == 0u && !px_in(4, 30, 1, 1, OP_SURF), "STEP overlay: a synth track, idle: the roll alone");
        turn(EN_K1, 1);
        check(ui.cards_t > 0u && px_in(4, 30, 1, 1, OP_SURF), "STEP overlay: a synth track, a knob turned: the cards");
        frames(OP_CARDS_FRAMES + 5u);
        check(ui.cards_t == 0u && !px_in(4, 30, 1, 1, OP_SURF), "STEP overlay: a synth track, gone: the roll whole");
    }
    op_cards = CARDS_LINE;
    reset_ui();
}
