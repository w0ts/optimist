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
static uint32_t sc_hash(uint32_t y, uint32_t h)         /* the screen rows y .. y + h, hashed */
{
    uint32_t i, a = 2166136261u;
    for (i = y * 240u; i < (y + h) * 240u; i++)
        a = (a ^ screen[i]) * 16777619u;
    return a;
}
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
    uint32_t m, held_h = 0, held_i = 0;
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
        check(px_in(2, SP_Y + SG_LY(15), 6, SG_LY(16) - SG_LY(15) - 1, lane_col(15)) && px_in(0, SP_Y + SG_LY(15), 240, 1, C_BLACK) &&
              SP_Y + SG_PH_Y + 4 == 240 && SG_LY(16) + 2 <= SG_MK_Y, "STEP overlay: the lanes run to the foot (the last lane's row, the marks, the playhead's strip at y 236)");
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
        check(px_in(4, SP_Y + SG_INFO_Y, 232, SG_INFO_H, C_HI) || px_in(4, SP_Y + SG_INFO_Y, 232, SG_INFO_H, C_GRAY),
              "... with the held step's two lines over the grid's foot");
        held_h = sc_hash(OY_CARD, 45);
        turn(EN_SELECT, 1);
        frames(2);
        check(ui.cards_t > 0u && (hp_count() < 2u || sc_hash(OY_CARD, 45) != held_h), "STEP overlay: SELECT pages the held step's cards, shown");
        held_h = sc_hash(OY_CARD, 45);
        held_i = sc_hash(SP_Y + SG_INFO_Y, SG_INFO_H);
        kup(WK(3));
        frames(OP_CARDS_FRAMES / 2u);
        check(ui.cards_t > 0u, "... after the release they wait the idle time");
        check(sc_hash(OY_CARD, 45) == held_h && sc_hash(SP_Y + SG_INFO_Y, SG_INFO_H) == held_i,
              "... still the page and the lines that were held, not the PATTERN row's");
        turn(EN_K1, 1);
        frames(2);
        check(sc_hash(OY_CARD, 45) != held_h, "... an edit after the release shows the live cards");
        frames(OP_CARDS_FRAMES + 5u);
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
