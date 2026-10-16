/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, STEP with a step held (the user, 2026-10-16): PRESETS moves the held step left / right in time (its
 * events go with it, the neighbour swaps, the ends refuse, the window pages, undoable), ALGORITHM moves it up / down (a synth
 * note a semitone, the SELECTed one of several; a drum hit one lane); the encoders keep their meaning with nothing held and when
 * the step is let go; the edited note stays in view under the cards, the selected note is drawn in C_NOTE. And REC held on a
 * picked drum lane clears only that lane. Included by ui_optimist_test.c. */
static void sm_open(uint32_t drum, uint32_t len)
{
    reset_ui();
    if (song.playing)
        tap(B_PLAY), frames(3);
    song.sel = drum ? TRK_DRUM : 0;
    track_defaults_steps(TSEL);
    TSEL->p[P_SLEN] = (int16_t)len;
    if (drum)
        lane_select(2);
    tap(B_SEQ);
    frames(2);
}
static void sm_note(track_t *t, uint32_t i, uint32_t n)
{
    memset(&t->step[i], 0, sizeof t->step[i]);
    t->step[i].note[0] = (uint8_t)n;
    t->step[i].n = 1;
    t->step[i].time = ST_NOTE;
    t->step[i].vel = 100;
}
static void sm_undo(void)
{
    press(B_SAVE);
    press(B_HOME);
    release(B_HOME);
    release(B_SAVE);
}
static uint32_t sm_count(uint16_t col)                  /* the pixels of colour col on the screen */
{
    uint32_t n = 0, i;
    for (i = 0; i < 240u * 240u; i++)
        n += screen[i] == swap16(col);
    return n;
}
static void step_move_synth_tests(void)
{
    track_t *t;
    sm_open(0, 16);
    t = TSEL;
    sm_note(t, 3, 60);
    sm_note(t, 5, 70);
#if FELUCCA_AUTO
    AL(t)->n = 0;
    (void)auto_put(AL(t), 3 | AUTO_ONLY, AUTO_CHANCE, 50);   /* its chance, a lock and a motion event: they go with it */
    (void)auto_put(AL(t), 3 | AUTO_ONLY, 0, 10);
    (void)auto_put(AL(t), 3, 0, 20);
    (void)auto_put(AL(t), 4 | AUTO_ONLY, AUTO_NUDGE, 3);     /* (the neighbour's) */
#endif
    kdown(WK(3));
    turn(EN_PRESET, 1);
    check(t->step[4].time == ST_NOTE && t->step[4].note[0] == 60 && t->step[3].time != ST_NOTE, "step move: PRESETS right: the held step one step on");
    check(st.held == 1u << 4 && st.page == 0, "step move: ... the hold follows it");
#if FELUCCA_AUTO
    {
        int32_t v;
        check(auto_get(AL(t), 4 | AUTO_ONLY, AUTO_CHANCE, &v) && v == 50 && auto_get(AL(t), 4 | AUTO_ONLY, 0, &v) && v == 10 &&
              auto_get(AL(t), 4, 0, &v) && v == 20 && !auto_get(AL(t), 3 | AUTO_ONLY, AUTO_CHANCE, &v),
              "step move: ... its chance, lock and motion event went with it");
        check(auto_get(AL(t), 3 | AUTO_ONLY, AUTO_NUDGE, &v) && v == 3, "step move: ... the step it passed took its place with its nudge");
    }
#endif
    turn(EN_PRESET, 1);
    check(t->step[5].note[0] == 60 && t->step[4].note[0] == 70, "step move: ... on over a note: they swap, nothing lost");
    turn(EN_PRESET, -2);
    check(t->step[3].note[0] == 60 && t->step[5].note[0] == 70 && st.held == 1u << 3, "step move: PRESETS left twice: back");
    kup(WK(3));
    check(!st.held && t->step[3].time == ST_NOTE && t->step[3].note[0] == 60, "step move: let go: not toggled, nothing held");
    kdown(WK(5));
    turn(EN_PRESET, -1);
    kup(WK(5));
    check(t->step[4].note[0] == 70, "step move: a finger that stayed where it was held a step that moved: released clean");
    sm_undo();
    check(t->step[5].note[0] == 70 && t->step[4].time != ST_NOTE, "step move: undo: the last move back");
    /* the ends */
    sm_open(0, 16);
    t = TSEL;
    sm_note(t, 0, 50);
    sm_note(t, 15, 52);
    kdown(WK(0));
    turn(EN_PRESET, -1);
    check(t->step[0].note[0] == 50 && !strcmp(ui.msg, "FIRST STEP"), "step move: the first step left: stays, said");
    kup(WK(0));
    kdown(WK(15));
    turn(EN_PRESET, 1);
    check(t->step[15].note[0] == 52 && !strcmp(ui.msg, "LAST STEP"), "step move: the last step right: stays, said, no wrap");
    kup(WK(15));
    /* across the window's pages */
    sm_open(0, 32);
    t = TSEL;
    sm_note(t, 15, 61);
    kdown(WK(15));
    turn(EN_PRESET, 1);
    check(st.page == 1 && st.held == 1u && t->step[16].note[0] == 61 && t->step[15].time != ST_NOTE, "step move: past the window's edge: the next page, the step held there");
    turn(EN_PRESET, -1);
    check(st.page == 0 && st.held == 1u << 15 && t->step[15].note[0] == 61, "step move: ... and back");
    turn(EN_PRESET, 1);
    kup(WK(15));
    check(!st.held && t->step[16].time == ST_NOTE, "step move: let go on the other page: nothing stuck");
    /* ties: a note moves with its ties; a tied neighbour refuses */
    sm_open(0, 16);
    t = TSEL;
    sm_note(t, 2, 60);
    t->step[3].time = ST_TIE;
    kdown(WK(2));
    turn(EN_PRESET, 1);
    check(t->step[3].time == ST_NOTE && t->step[4].time == ST_TIE && t->step[2].time != ST_NOTE, "step move: a note moves with its tie");
    kup(WK(2));
    sm_note(t, 6, 55);
    t->step[7].time = ST_TIE;
    kdown(WK(3));
    turn(EN_PRESET, 3);
    check(t->step[4].note[0] == 60 && t->step[5].time == ST_TIE && t->step[6].note[0] == 55 && t->step[7].time == ST_TIE, "step move: a note stops short of a tied note (its ties safe)");
    kup(WK(3));
    /* ALGORITHM: the pitch */
    sm_open(0, 16);
    t = TSEL;
    sm_note(t, 3, 60);
    kdown(WK(3));
    turn(EN_ALGO, 1);
    check(t->step[3].note[0] == 61 && song.sel == 0, "step move: ALGORITHM up: a semitone, the track stays");
    turn(EN_ALGO, -3);
    check(t->step[3].note[0] == 58, "step move: ALGORITHM down 3: three semitones");
    turn(EN_ALGO, -12 * 6);
    check(t->step[3].note[0] == 46, "step move: a fast turn: at most an octave a call");
    sm_note(t, 3, 126);
    turn(EN_ALGO, 5);
    check(t->step[3].note[0] == 127, "step move: ... clamped at 127");
    kup(WK(3));
    /* the encoders as they were with nothing held, and after the step is let go */
    sm_open(0, 32);
    turn(EN_PRESET, 1);
    check(st.page == 1, "step move: nothing held: PRESETS pages the window");
    turn(EN_PRESET, -1);
    sm_note(TSEL, 3, 60);
    kdown(WK(3));
    kup(WK(3));
    turn(EN_ALGO, 1);
    check(song.sel == 1, "step move: nothing held (a step let go): ALGORITHM walks the track again");
    song.sel = 0;
}
static void step_move_notes_tests(void)
{
    track_t *t;
    int32_t y, top;
    sm_open(0, 16);
    t = TSEL;
    sm_note(t, 3, 60);
    t->step[3].n = 2;
    t->step[3].note[1] = 64;
    ui.force = 1;
    frames(3);
    check(sm_count(C_NOTE) == 0, "step notes: nothing held: no note in the selection colour");
    kdown(WK(3));
    frames(HOLD_FRAMES);
    ui.force = 1;
    frames(3);
    check(held_note_sel(3) == 0 && sm_count(C_NOTE) > 0, "step notes: a step of two notes held: the selected note (the first) in C_NOTE");
    turn(EN_SELECT, 1);
    check(st.nsel == 1 && held_note_sel(3) == 1, "step notes: SELECT picks the second note");
    turn(EN_ALGO, 1);
    check(t->step[3].note[0] == 60 && t->step[3].note[1] == 65, "step notes: ALGORITHM moves only the selected note");
    turn(EN_SELECT, 5);
    check(st.nsel == 1, "step notes: SELECT stops at the last note");
    turn(EN_SELECT, -1);
    turn(EN_ALGO, -2);
    check(t->step[3].note[0] == 58 && t->step[3].note[1] == 65, "step notes: ... and the first");
    turn(EN_PRESET, 1);
    check(t->step[4].n == 2 && t->step[4].note[1] == 65 && st.held == 1u << 4, "step notes: PRESETS still moves the whole step in time");
    kup(WK(3));
    sm_open(0, 16);
    t = TSEL;
    sm_note(t, 3, 60);
    kdown(WK(3));
    frames(HOLD_FRAMES);
    ui.force = 1;
    frames(3);
    check(sm_count(C_NOTE) == 0, "step notes: one note: no selection colour");
    turn(EN_SELECT, 1);
    check(st.nsel == 0 && (hp_count() < 2u || st.hp == 1u), "step notes: one note: SELECT keeps its meaning (the card pages)");
    kup(WK(3));
    /* the edited note stays in view under the cards */
    sm_open(0, 16);
    t = TSEL;
    sm_note(t, 1, 30);
    sm_note(t, 2, 90);
    sm_note(t, 3, 60);
    kdown(WK(3));
    frames(HOLD_FRAMES);
    top = (int32_t)OP_PY - SP_Y;
    {
        int32_t dir, k;
        for (dir = -1; dir <= 1; dir += 2) {
            for (k = 0; k < 8; k++) {
                turn(EN_ALGO, dir * 12);
                frames(2);
                roll_range(t);
                y = SG_TOP + sg_dy + (rg.hi - (int32_t)t->step[3].note[0]) * rg.rh;
                if (y < top + rg.rh || y + 2 * rg.rh > SG_TOP + SG_H)
                    break;
            }
            check(k == 8 && y >= top + rg.rh && y + rg.rh <= SG_TOP + SG_H - rg.rh + 1,
                  dir < 0 ? "step notes: the note moved to the bottom: its row below the cards, a row's margin" : "step notes: the note moved to the top: its row below the cards, a row's margin");
        }
    }
    kup(WK(3));
}
static void step_move_drum_tests(void)
{
    track_t *t;
    sm_open(1, 16);
    t = TSEL;
    dstep_set(&t->dstep[1], 2, LV_HARD, 1);
    dstep_set(&t->dstep[1], 3, LV_NORM, 0);
#if FELUCCA_AUTO
    AL(t)->n = 0;
    (void)auto_put(AL(t), 1 | AUTO_ONLY, AUTO_LANE(2, 0), 5);
#endif
    kdown(WK(1));
    turn(EN_ALGO, 1);
    check(dstep_has(&t->dstep[1], 1) && !dstep_has(&t->dstep[1], 2) && lane_selected() == 1, "step move, drums: ALGORITHM up: the hit one lane up (as drawn), the lane follows");
    check(dstep_lvl(&t->dstep[1], 1) == LV_HARD && dstep_rat(&t->dstep[1], 1) == 1 && dstep_has(&t->dstep[1], 3), "step move, drums: ... its level and ratchet kept, the other lanes alone");
#if FELUCCA_AUTO
    {
        int32_t v;
        check(auto_get(AL(t), 1 | AUTO_ONLY, AUTO_LANE(1, 0), &v) && v == 5 && !auto_get(AL(t), 1 | AUTO_ONLY, AUTO_LANE(2, 0), &v), "step move, drums: ... its lock followed it");
    }
#endif
    turn(EN_ALGO, -1);
    check(dstep_has(&t->dstep[1], 2) && lane_selected() == 2, "step move, drums: ALGORITHM down: back");
    turn(EN_ALGO, -1);
    check(dstep_has(&t->dstep[1], 2) && dstep_has(&t->dstep[1], 3) && !strcmp(ui.msg, "LANE TAKEN"), "step move, drums: onto a lane that hits there: refused, said");
    turn(EN_PRESET, 1);
    check(dstep_has(&t->dstep[2], 2) && !dstep_has(&t->dstep[1], 2) && dstep_has(&t->dstep[1], 3) && st.held == 1u << 2,
          "step move, drums: PRESETS right: the lane's hit one step on, the other lanes stay");
#if FELUCCA_AUTO
    {
        int32_t v;
        check(auto_get(AL(t), 2 | AUTO_ONLY, AUTO_LANE(2, 0), &v) && v == 5, "step move, drums: ... its lock too");
    }
#endif
    kup(WK(1));
    kdown(WK(2));
    lane_select(0);
    dstep_set(&t->dstep[2], 0, LV_NORM, 0);
    turn(EN_ALGO, 1);
    check(dstep_has(&t->dstep[2], 0) && !strcmp(ui.msg, "TOP LANE"), "step move, drums: the top lane up: refused");
    kup(WK(2));
    lane_select(2);
}
static void lane_clear_tests(void)
{
    track_t *t;
    uint32_t i;
    sm_open(1, 16);
    t = TSEL;
    ui.lane = 1;
    for (i = 0; i < 4u; i++)
        dstep_set(&t->dstep[i], 2, LV_NORM, 0), dstep_set(&t->dstep[i], 3, LV_NORM, 0);
    dstep_set(&t->dstep[1], DRUM_ACC, LV_NORM, 0);
#if FELUCCA_AUTO
    AL(t)->n = 0;
    (void)auto_put(AL(t), 1 | AUTO_ONLY, AUTO_LANE(2, 0), 5);
    (void)auto_put(AL(t), 1 | AUTO_ONLY, AUTO_LANE(3, 0), 6);
    (void)auto_put(AL(t), 2, AUTO_LANE(2, 1), 7);
#endif
    rh_hold_ms(700 + 100);
    check(rh.ring && rh.lane == 2, "lane clear: REC held on a picked lane: the ring, for lane 2");
    {
        uint32_t t0 = fm1_ms;
        while (fm1_ms - t0 < 1400u)
            frame();
    }
    release(B_REC);
    frames(2);
    {
        uint32_t gone = 1, kept = 1;
        for (i = 0; i < 4u; i++)
            gone &= !dstep_has(&t->dstep[i], 2), kept &= dstep_has(&t->dstep[i], 3);
        check(gone && kept && dstep_has(&t->dstep[1], DRUM_ACC), "lane clear: only that lane's steps went; the other lanes and the accents stay");
    }
    check(!strcmp(ui.msg, "SNARE CLEARED"), "lane clear: the toast names the lane");
#if FELUCCA_AUTO
    {
        int32_t v;
        check(!auto_get(AL(t), 1 | AUTO_ONLY, AUTO_LANE(2, 0), &v) && !auto_get(AL(t), 2, AUTO_LANE(2, 1), &v) &&
              auto_get(AL(t), 1 | AUTO_ONLY, AUTO_LANE(3, 0), &v) && v == 6, "lane clear: its locks and motion went, the other lane's stay");
    }
#endif
    sm_undo();
    {
        uint32_t back = 1;
        for (i = 0; i < 4u; i++)
            back &= dstep_has(&t->dstep[i], 2);
        check(back, "lane clear: undo brings the lane back");
    }
    /* DR itself (no lane picked): the whole track, as before */
    ui.lane = 0;
    rh_hold_ms(700 + 1300 + 100);
    release(B_REC);
    frames(2);
    check(!dstep_has(&t->dstep[0], 2) && !dstep_has(&t->dstep[0], 3), "lane clear: no lane picked: the whole drum track cleared, as before");
    /* a synth track: unchanged */
    reset_ui();
    song.sel = 1;
    frame();
    trk[1].step[0].time = ST_NOTE, trk[1].step[0].n = 1, trk[1].step[0].note[0] = 60;
    ui.lane = 1;
#if FELUCCA_MOTION
    AL(&trk[1])->n = 0;
    (void)auto_put(AL(&trk[1]), 4, mot_sid(0), 9);       /* a recorded motion event */
#endif
    rh_hold_ms(700 + 1300 + 100);
    release(B_REC);
    frames(2);
    check(trk[1].step[0].n == 0 && !strcmp(ui.msg, "T2 CLEARED"), "lane clear: a synth track: cleared whole, as before");
#if FELUCCA_MOTION
    {
        int32_t v;
        check(!auto_get(AL(&trk[1]), 4, mot_sid(0), &v), "REC held: the track's recorded motion goes too");
        sm_undo();
        check(auto_get(AL(&trk[1]), 4, mot_sid(0), &v) && v == 9 && trk[1].step[0].n == 1, "REC held: undo brings the steps and the motion back");
    }
#endif
    ui.lane = 0;
    song.sel = 0;
    reset_ui();
}
static void step_move_tests(void)
{
    step_move_synth_tests();
    step_move_notes_tests();
    step_move_drum_tests();
    lane_clear_tests();
}
