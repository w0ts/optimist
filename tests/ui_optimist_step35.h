/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, STEP batch 35 (the user, 2026-10-11): step entry follows the keyboard octave; a held step
 * and OCT- / OCT+ transpose it by an octave. Included after the helpers. */

static void s35_open(void)                              /* a synth track's STEP, stopped, the octave 0, steps empty */
{
    uint32_t i;
    reset_ui();
    song.sel = 0;
    song.octave = 0;
    TSEL->p[P_SLEN] = 16;                               /* (a LEN left by an earlier test or the fuzz: one bar) */
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
    press(B_SEQ);                                       /* (OCT alone on the roll moves its view, 2026-10-12: the */
    frames(HOLD_FRAMES);                                /* keyboard octave is OCT while the keys play, SEQ held) */
    tap(B_OCTUP);
    release(B_SEQ);
    frames(2);
    check(song.octave == 1 && st.roll_oct == 0, "STEP, SEQ held + OCT+: the keyboard octave up, the roll's view as it was");
    kdown(WK(1));
    kup(WK(1));
    check(TSEL->step[1].note[0] == n + 12u, "STEP entry after OCT+: the note an octave up (it follows the keyboard octave)");
    step_pick_name(b, sizeof b);
    {
        char e[8];
        note_name(e, n + 12u);
        check(!strncmp(b, e, strlen(e)) && (b[strlen(e)] == 0 || b[strlen(e)] == ' '), "... the pick's name says the octave too");
    }
    song.octave = -1;                                   /* (as SEQ held + OCT- OCT- would) */
    kdown(WK(2));
    kup(WK(2));
    check(TSEL->step[2].note[0] == n - 12u, "STEP entry at the keyboard octave -1: the note an octave down");
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
/* the drum grid's lanes named (the user, 2026-10-11: "put the name of the instrument (two letters) on the drum piano
 * roll"): each lane's colour chip and code (LANE_CODE, the mixer's) at the left, the selected lane's white */
static void s35_drum_codes_tests(void)
{
    uint32_t l, ok = 1;
    reset_ui();
    song.sel = TRK_DRUM;
    tap(B_SEQ);
    lane_select(3);
    ui.sig[2] = 0;
    frames(3);
    for (l = 0; l < DRUM_LANES; l++) {
        uint32_t y = (uint32_t)(SP_Y + SG_LY(l)), h = (uint32_t)(SG_LY(l + 1u) - SG_LY(l)) - 1u;
        uint16_t c = l == 3u ? C_WHITE : lane_col(l);
        ok &= (uint32_t)(px_in(6, y, 18, h, c) && px_in(2, y, 3, h, c) && !px_in(6, y, 18, h, l == 3u ? lane_col(l) : C_WHITE));
    }
    check(ok && SG_X >= 6 + 16 + 2 && SG_X + 16 * SG_CW <= 240, "the drum grid: each lane's two-letter code and chip at its left, in its colour, the selected lane's white");
    check(!strcmp(LANE_CODE[0], "BD") && !strcmp(LANE_CODE[15], "CB") && mx_code(MXR_LANE0 + 2u) == LANE_CODE[2],
          "... the codes are the mixer's (LANE_CODE: BD .. CB)");
    reset_ui();
}
/* the page and its n/N in the header (the user, 2026-10-11: "when i page thru with select (for plock or normal pages)
 * we should display the name of the page and the 1/4 (current/total pages). ideally everywhere") */
static int s35_pos_is(uint32_t at, uint32_t n)          /* the header's n/N now: " at/n" */
{
    char p[12], e[12];
    head_pos(ui.scr, ui.row[ui.scr], p);
    snprintf(e, sizeof e, " %u/%u", at, n);
    if (strcmp(p, e))
        printf("  pos [%s] want [%s]\n", p, e);
    return !strcmp(p, e);
}
static void s35_pages_tests(void)
{
    char t[32], nm[16];
    uint32_t n;
    reset_ui();
    song.sel = 0;
    tap(B_ENV);                                         /* SOUND, the ENV family */
    n = SCR->rows();
    check(ui.scr == SCR_SOUND && n >= 2u && s35_pos_is(ui.row[SCR_SOUND] + 1u, n), "SOUND ENV: the header's n/N (the family's rows)");
    turn(EN_SELECT, 1);
    head_title(ui.scr, ui.row[ui.scr], t, sizeof t);
    SCR->name(ui.row[ui.scr], nm);
    check(s35_pos_is(ui.row[SCR_SOUND] + 1u, n) && strstr(t, nm) != 0, "... SELECT: the next page's name and its n/N");
    tap(B_ENV);                                         /* (the same button: the family's next, round) */
    check(s35_pos_is(ui.row[SCR_SOUND] + 1u, n), "... ENV again (the repeat): the n/N follows");
    strcpy(t, "SOUND ENV2 DEST");
    head_fit(t, " 3/12", 12);
    check(!strcmp(t, "ENV2 DE 3/12"), "a title too long: the screen's word goes first, the name cut, the n/N whole");
    strcpy(t, "SOUND LFO");
    head_fit(t, " 2/4", 14);
    check(!strcmp(t, "SOUND LFO 2/4"), "a title that fits: whole, then the n/N");
    /* the mixer: the row's sets */
    go_home();
    frames(2);
    n = mx_sets(mx_row());
    turn(EN_SELECT, -8);                                /* (the first set) */
    turn(EN_SELECT, 1);
    check(n < 2u || s35_pos_is(2u, n), "the mixer: SELECT, the set's n/N (T1 INSERT 2/4)");
    /* STEP: its pages while their cards show; the window's header otherwise */
    reset_ui();
    song.sel = 0;
    tap(B_SEQ);
    frames(2);
    {
        char p[12];
        head_pos(SCR_STEP, ui.row[SCR_STEP], p);
        check(!p[0], "STEP, nothing shown over the grid: no n/N (the header names the window)");
    }
    n = SCR->rows();
    tap(B_SEQ);                                         /* SEQ again: STEP's next page, its cards up */
    head_title(SCR_STEP, ui.row[SCR_STEP], t, sizeof t);
    SCR->name(ui.row[SCR_STEP], nm);
    check(ui.cards_t > 0u && !strcmp(t, nm) && s35_pos_is(2u, n), "STEP, SEQ again: the page's name and n/N (ARP 2/3)");
    /* a held step: its card pages */
    s35_open();
    s35_note(3, 60, LV_NORM);
    kdown(WK(3));
    frames(HOLD_FRAMES);
    head_title(SCR_STEP, 0, t, sizeof t);
    check(!strcmp(t, "STEP") && (hp_count() < 2u || s35_pos_is(1u, hp_count())), "a held step: STEP 1/N");
    if (hp_count() >= 2u) {
        turn(EN_SELECT, 1);
        head_title(SCR_STEP, 0, t, sizeof t);
        check((!strcmp(t, "EXTRA") || !strcmp(t, PAGES[st.lock_pg % NPAGES].title)) && s35_pos_is(2u, hp_count()),
              "... SELECT: the next card page's name and 2/N");
    }
    kup(WK(3));
    frames(2);
    s35_open();
}
