/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, STEP's cards as an overlay (the user, 2026-10-10: "we should show the 4 cards in step seq
 * only on page change and when modifying the values, like layover"): the grid takes the panel from y 25; the four
 * cards are drawn over its top on a page change or an edit, for OP_CARDS_FRAMES, and the grid is drawn whole again
 * when they go. Included by ui_optimist_test.c after ui_optimist_cards.h. */

/* two probes that tell the grid from the cards: lane row 2's white strip at x 2 (the cards' band is black there), and
 * the first card's outline (ui_optimist_cards.h card0_up; the grid has no such edge there) */
static int sc_grid_up(void) { return px_in(2, SP_Y + SG_TOP + 2 * SG_LH, 1, SG_LH - 1, C_WHITE) && !card0_up(); }
static int sc_cards_up(void) { return !px_in(2, SP_Y + SG_TOP + 2 * SG_LH, 1, SG_LH - 1, C_WHITE) && card0_up(); }
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
    for (m = 0; m < 1u; m++) {                         /* (once: the cards are 2x2, there is no other mode) */
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
        check(ui.cards_t == 0u && !card0_up(), "... and they go");
        /* another drum lane, whichever way: no overlay (the user, 2026-10-10: "and NOT when switching drum lane"; the
         * header names the lane) */
        {
            uint32_t l = lane_selected(), ok;
            turn(EN_PRESET, 1);                         /* (PRESETS pages the window: one page here, nothing) */
            frames(2);
            ok = lane_selected() == l && ui.cards_t == 0u && !card0_up();
            check(ok, "STEP overlay: PRESETS leaves the drum lane, no overlay");
            press(B_SEQ);
            kdown(WK(6));
            kup(WK(6));
            release(B_SEQ);
            frames(2);
            ok = lane_selected() == 6u && l != 6u && ui.cards_t == 0u && !card0_up();
            check(ok, "STEP overlay: SEQ + a key picks the drum lane, no overlay");
            ui.row[SCR_STEP] = (uint8_t)(STP_LANE0 + lane_selected());
            frames(2);
            turn(EN_SELECT, 1);
            frames(2);
            ok = lane_selected() == 7u && ui.cards_t == 0u && !card0_up();
            check(ok, "STEP overlay: SELECT on the lane pages picks the next lane, no overlay");
            ui.row[SCR_STEP] = 0;
            lane_select(2);
            frames(2);
        }
        /* a quick tap: the step toggled, no overlay at all (the user, 2026-10-10: "in seq, we should show the plock
         * overlay only after some time") */
        {
            uint32_t ok = 1, i;
            kdown(WK(5));
            for (i = 0; i < 3u; i++, frame())
                ok &= (uint32_t)(ui.cards_t == 0u && !card0_up());
            kup(WK(5));
            frames(OP_CARDS_FRAMES + 5u);
            ok &= (uint32_t)(ui.cards_t == 0u && !card0_up());
            check(ok, "STEP overlay: a step tapped, no overlay, during or after");
            kdown(WK(5));                               /* (the step back as it was) */
            kup(WK(5));
            frames(OP_CARDS_FRAMES + 5u);
        }
        /* a held step: the cards once the press is a hold; SELECT pages them */
        kdown(WK(3));
        frames(2);
        {
            char h[40];
            head_title(SCR_STEP, 0, h, sizeof h);
            check(ui.cards_t == 0u && !card0_up() && !strncmp(h, "STEPS ", 6),
                  "STEP overlay: a step pressed, before the hold time: no overlay yet, the header still the window's");
        }
        frames(HOLD_FRAMES);
        check(ui.cards_t > 0u && sc_cards_up(), "STEP overlay: a step held past the hold time shows the cards");
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
        check(ui.cards_t == 0u && ui.scr == SCR_STEP && !card0_up(), "... gone about 2 s after the release, the grid whole");
        /* the screen left: the overlay's timer is dropped, the other screens keep their cards */
        turn(EN_K1, 1);
        op_enter(SCR_SOUND);
        frames(3);
        check(ui.cards_t == 0u && cards_band_is(BAND_2X2), "other screens: their cards stay as they were");
        turn(EN_K1, 1);
        check(ui.cards_t == 0u, "other screens: a knob turn starts no overlay timer");
        /* a synth track's roll: same */
        reset_ui();
        song.sel = 0;
        tap(B_SEQ);
        frames(3);
        check(ui.cards_t == 0u && !card0_up(), "STEP overlay: a synth track, idle: the roll alone");
        turn(EN_K1, 1);
        check(ui.cards_t > 0u && card0_up(), "STEP overlay: a synth track, a knob turned: the cards");
        frames(OP_CARDS_FRAMES + 5u);
        check(ui.cards_t == 0u && !card0_up(), "STEP overlay: a synth track, gone: the roll whole");
    }
    reset_ui();
}
/* the user, 2026-10-11: "on many pages, the 4 top cards dont show the values of the current page, it seems to be a
 * plock card from step seq". A step let go while its cards were up made them show the page that was held (a snapshot)
 * until the next edit on STEP; leaving STEP in that time left the snapshot on: every other screen's cards were the
 * step's. The cards of a page left to must be that page's, at once and after the timer, whichever way STEP is left */
static uint32_t leak_cards(uint32_t via, uint32_t wait)  /* the cards band after: a step held, paged, let go, STEP left */
{
    reset_ui();
    song.sel = 0;
    tap(B_SEQ);
    frames(3);
    kdown(WK(4));
    frames(HOLD_FRAMES + 2u);
    turn(EN_SELECT, 1);                                 /* (its next card page, a p-lock page when built) */
    frames(2);
    kup(WK(4));
    frames(wait);
    tap(via);
    frames(OP_CARDS_FRAMES + 5u);
    return sc_hash(OY_CARD, 45);
}
static uint32_t clean_cards(uint32_t via)               /* the same screen's cards, STEP never opened */
{
    reset_ui();
    song.sel = 0;
    tap(via);
    frames(OP_CARDS_FRAMES + 5u);
    return sc_hash(OY_CARD, 45);
}
static void step_cards_leak_tests(void)
{
    static const uint8_t VIA[3] = {B_ENV, B_LFO, B_FX};
    uint32_t i, ok = 1, ok2 = 1;
    for (i = 0; i < 3u; i++) {
        uint32_t c = clean_cards(VIA[i]);
        ok &= (uint32_t)(leak_cards(VIA[i], 2u) == c && ui.scr == SCR_SOUND);
        ok2 &= (uint32_t)(leak_cards(VIA[i], OP_CARDS_FRAMES + 5u) == c);
    }
    check(ok, "STEP left by ENV / LFO / FX just after a held step was let go: the page's own cards, not the step's");
    check(ok2, "... and left after the step's cards timed out: the page's own cards");
    reset_ui();
}
