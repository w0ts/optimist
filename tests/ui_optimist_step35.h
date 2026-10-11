/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, STEP batch 35 (the user, 2026-10-11): step entry follows the keyboard octave; a held step
 * and OCT- / OCT+ transpose it by an octave. Included after the helpers. */

static void s35_open(void)                              /* a synth track's STEP, stopped, the octave 0, steps empty */
{
    uint32_t i;
    reset_ui();
    song.sel = 0;
    song.octave = 0;
    for (i = 0; i < NSTEP; i++)
        step_clear(&TSEL->step[i]);
    tap(B_SEQ);
    frames(2);
}
static uint32_t s35_pick(uint32_t k)                    /* SEQ held + key k: the pen; its note */
{
    uint32_t n;
    press(B_SEQ);
    kdown(k);
    n = pen_note[0];
    kup(k);
    release(B_SEQ);
    frames(2);
    return n;
}
static void s35_entry_tests(void)
{
    uint32_t n, ok;
    char b[16];
    s35_open();
    n = s35_pick(WK(7));
    kdown(WK(0));
    kup(WK(0));
    check(step_on(&TSEL->step[0]) && TSEL->step[0].note[0] == n, "STEP entry: a tapped step takes the note played");
    tap(B_OCTUP);
    check(song.octave == 1, "STEP, OCT+ with no step held: the keyboard octave up");
    kdown(WK(1));
    kup(WK(1));
    check(TSEL->step[1].note[0] == n + 12u, "STEP entry after OCT+: the note an octave up (it follows the keyboard octave)");
    step_pick_name(b, sizeof b);
    {
        char e[8];
        note_name(e, n + 12u);
        check(!strncmp(b, e, strlen(e)) && (b[strlen(e)] == 0 || b[strlen(e)] == ' '), "... the pick's name says the octave too");
    }
    tap(B_OCTDN);
    tap(B_OCTDN);
    kdown(WK(2));
    kup(WK(2));
    check(TSEL->step[2].note[0] == n - 12u, "STEP entry after OCT- OCT-: the note an octave down");
    n = s35_pick(WK(7));                                /* played at -1: the pen as heard, no second shift */
    kdown(WK(3));
    kup(WK(3));
    ok = TSEL->step[3].note[0] == n;
    check(ok, "STEP entry: a note played at another octave is entered as played (not shifted twice)");
    song.octave = 0;
    s35_open();
}
static void s35_transpose_tests(void)
{
    uint32_t n, pg;
    s35_open();
    n = s35_pick(WK(7));
    kdown(WK(4));
    kup(WK(4));
    TSEL->step[4].note[1] = (uint8_t)(n + 4u);          /* (a chord: both notes move) */
    TSEL->step[4].n = 2;
    kdown(WK(4));
    frames(HOLD_FRAMES);
    tap(B_OCTUP);
    check(TSEL->step[4].note[0] == n + 12u && TSEL->step[4].note[1] == n + 16u && song.octave == 0,
          "a step held + OCT+: its notes an octave up, the keyboard octave untouched");
    tap(B_OCTDN);
    tap(B_OCTDN);
    check(TSEL->step[4].note[0] == n - 12u && TSEL->step[4].note[1] == n - 8u && song.octave == 0,
          "a step held + OCT- OCT-: two octaves down from there");
    check(ui.cards_t > 0u && ui.hot_lit && ui.hot == 0u, "... the held step's cards up, NOTE lit");
    kup(WK(4));
    frames(2);
    check(step_on(&TSEL->step[4]), "... let go: the step kept (edited, not a tap)");
    TSEL->step[4].note[0] = 120;
    TSEL->step[4].n = 1;
    kdown(WK(4));
    frames(HOLD_FRAMES);
    tap(B_OCTUP);
    check(TSEL->step[4].note[0] == 127u, "a step held + OCT+ near the top: clamped to the valid range");
    kup(WK(4));
    frames(2);
    pg = 0;
#if FELUCCA_PLOCK
    pg = 1;
#endif
    if (pg) {                                           /* on a p-lock page: back to the step's page, its NOTE lit */
        kdown(WK(4));
        frames(HOLD_FRAMES);
        tap(B_ENV);
        tap(B_OCTDN);
        check(st.hp == 0u && st.lock_pg == LOCK_NONE && TSEL->step[4].note[0] == 115u, "a step held on a lock page + OCT-: transposed, the cards back to the step's NOTE");
        kup(WK(4));
        frames(2);
    }
    s35_open();
}
/* the roll (the user, 2026-10-11: "could we have the note names on the left, and more details ?") */
static void s35_note(uint32_t i, uint32_t n, uint32_t lv)
{
    step_t *s = &TSEL->step[i];
    step_clear(s);
    s->time = ST_NOTE;
    s->n = 1;
    s->note[0] = (uint8_t)n;
    s->lvl = (uint8_t)lv;
    s->vel = 100;
}
static uint32_t s35_y(int32_t n) { return (uint32_t)(SP_Y + SG_TOP + (rg.hi - n) * rg.rh + rg.rh / 2); }   /* row n's middle */
static uint32_t s35_x(uint32_t w) { return RL_X + w * RL_CW + 3u; }                                    /* column w, inside */
static void s35_roll_tests(void)
{
    int16_t len0;
    uint16_t c;
    uint32_t i, ok;
    s35_open();
    len0 = TSEL->p[P_SLEN];
    TSEL->p[P_SLEN] = 32;
    s35_note(0, 60, LV_NORM);                           /* C4, tied over steps 2 and 3 */
    TSEL->step[1].time = ST_TIE;
    TSEL->step[2].time = ST_TIE;
    s35_note(4, 64, LV_GHOST);                          /* E4, ghost */
    s35_note(8, 62, LV_HARD);
    s35_note(17, 67, LV_NORM);                          /* (page 2: in the range of every page) */
    sync_reload = 1;
    ui.sig[2] = 0;
    frames(2);
    roll_range(TSEL);
    c = trk_col(0);
    check(rg.lo <= 60 && rg.hi >= 67 && rg.rh >= 11 && SP_Y + SG_TOP + (rg.hi - rg.lo + 1) * rg.rh <= SP_Y + SG_TOP + SG_H,
          "the roll: the pattern's notes on every page (60..67 here), rows tall enough to name each one");
    check(px_in(0, s35_y(60) - 6u, 24, 12, C_HI) && px_in(0, s35_y(61) - 6u, 24, 12, C_DIM) && px_in(0, s35_y(62) - 6u, 24, 12, C_AMB) &&
          !px_in(0, s35_y(60) - 6u, 24, 12, c),
          "the roll's gutter: C4 named bright, C# dim, a row with a note (D) amber");
    check(px_at(s35_x(5), s35_y(61), col_shade(C_LINE, 2u)) && px_at(s35_x(5), s35_y(60), C_BLACK) && px_at(4, s35_y(61) + 5u, col_shade(C_LINE, 2u)),
          "the roll: a black key's row tinted (the gutter too), a white key's black");
    ok = 1;
    for (i = 0; i < 3u; i++)
        ok &= (uint32_t)px_at(s35_x(i), s35_y(60), lvl_col(c, LV_NORM));
    ok &= (uint32_t)px_at(RL_X + RL_CW - 1u, s35_y(60), lvl_col(c, LV_NORM)) && !px_at(s35_x(3), s35_y(60), lvl_col(c, LV_NORM));
    check(ok, "the roll: a note's bar as long as its ties, joined over the gaps, ending with them");
    check(px_at(s35_x(4), s35_y(64), lvl_col(c, LV_GHOST)) && px_at(s35_x(8), s35_y(62), lvl_col(c, LV_HARD)) &&
          lvl_col(c, LV_GHOST) != lvl_col(c, LV_HARD), "the roll: the level as the shade (ghost dim, hard full)");
    check(px_at(RL_X - 1u, SP_Y + SG_TOP + 3u, C_DIM) && px_at(RL_X + 4u * RL_CW - 1u, SP_Y + SG_TOP + 3u, C_LINE) &&
          px_at(RL_X + 16u * RL_CW - 1u, SP_Y + SG_TOP + 3u, C_LINE), "the roll: the bar's start C_DIM, the beats C_LINE");
    check(px_in(1, SP_Y + SG_PH_Y + 1u, 10, 2, C_HI) && px_in(12, SP_Y + SG_PH_Y + 1u, 10, 2, C_LINE),
          "the roll: the page indicator in the gutter, 1-16 lit of two");
    turn(EN_PRESET, 1);
    frames(2);
    check(st.page == 1u && px_in(12, SP_Y + SG_PH_Y + 1u, 10, 2, C_HI) && px_at(s35_x(1), s35_y(67), lvl_col(c, LV_NORM)) &&
          px_at(RL_X + 16u * RL_CW - 1u, SP_Y + SG_TOP + 3u, C_DIM), "the roll, 17-32 (PRESETS): its note, the indicator, LEN's end C_DIM");
    turn(EN_PRESET, -1);
    st.follow = 1;
    lane_transport(1);
    ok = 0;
    for (i = 0; i < 600u && !ok; i++) {                 /* the playhead over the tied C4: its column white */
        frame();
        ok = sg_ph >= 0 && sg_ph <= 2 && st.page == 0u;
    }
    check(ok && px_at(s35_x((uint32_t)sg_ph), s35_y(60), C_WHITE) && px_at(s35_x(4), s35_y(64), lvl_col(c, LV_GHOST)),
          "the roll playing: the playhead's column white, the others as they are (a column painted, not the band)");
    lane_transport(0);
    TSEL->p[P_SLEN] = len0;
    s35_open();
}
