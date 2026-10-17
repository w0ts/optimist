/* SPDX-License-Identifier: GPL-3.0-only */
/* STEP's note picker (op_step.c pk_*, the user, 2026-10-17): the cards down, nothing held, KNOB 1 picks a note (the drum
 * track: a hit of the selected lane), KNOB 2 moves it, KNOB 3 its pitch / lane, KNOB 4 its length / level; SAVE its
 * cards, HOME lets them go; SAVE held mutes it (seq/auto.h AUTO_MUTE: the sequencer plays it as a rest), HOME held
 * deletes it, HOME tapped goes HOME. Included by ui_optimist_test.c */
static uint32_t pk_count_syn(const track_t *t)          /* the synth notes of steps 0..15 */
{
    uint32_t i, n = 0;
    for (i = 0; i < 16u; i++)
        n += (uint32_t)step_on(&t->step[i]);
    return n;
}
static void pk_cards_down(void)
{
    frames(OP_CARDS_FRAMES + 5u);                       /* (STEP opened: whatever showed the cards, they go) */
}
static void pk_hold_btn(uint32_t b)                     /* b held past the hold time alone, then let go */
{
    press(b);
    frames(HOLD_FRAMES);
    release(b);
    frames(2);
}
static void picker_synth_tests(void)
{
    track_t *t;
    uint32_t n0, len0;
    reset_ui();
    song.sel = 0;
    t = TSEL;
    t->p[P_SLEN] = 16;
    track_defaults_steps(t);
    fm1_irq_off();
    t->step[2].time = ST_NOTE, t->step[2].n = 1, t->step[2].note[0] = 60, t->step[2].vel = 100;
    t->step[5].time = ST_NOTE, t->step[5].n = 1, t->step[5].note[0] = 64, t->step[5].vel = 100;
    fm1_irq_on();
    tap(B_SEQ);
    pk_cards_down();
    len0 = (uint32_t)t->p[P_SLEN];
    turn(EN_K1, 1);
    check(ui.scr == SCR_STEP && st.pk == 2u && ui.cards_t == 0u && (uint32_t)t->p[P_SLEN] == len0,
          "picker: KNOB 1 with the cards down picks the first note (step 3), no cards, LEN as it was");
    turn(EN_K1, 1);
    check(st.pk == 5u, "picker: KNOB 1 again: the next note (step 6)");
    turn(EN_K1, 1);
    check(st.pk == 5u, "picker: ... past the last: stays on it");
    turn(EN_K1, -1);
    check(st.pk == 2u, "picker: KNOB 1 back: the note before");
    turn(EN_K2, 1);
    check(step_on(&t->step[3]) && !step_on(&t->step[2]) && st.pk == 3u && ui.cards_t == 0u,
          "picker: KNOB 2 moves the note a step right, the pick with it, no cards");
    n0 = t->step[3].note[0];
    turn(EN_K3, 1);
    check(t->step[3].note[0] != n0 && step_on(&t->step[3]), "picker: KNOB 3: its pitch");
    turn(EN_K4, 1);
    check(step_note_len(t, 3) == 2u, "picker: KNOB 4: its length (two steps)");
    tap(B_SAVE);
    check(st.latch && st.held == 1u << 3 && ui.cards_t > 0u, "picker: SAVE: the picked note's cards (as if its step were held)");
    tap(B_HOME);
    check(!st.latch && !st.held && ui.scr == SCR_STEP && step_on(&t->step[3]), "picker: HOME tapped: its cards go, the note and STEP kept");
#if FELUCCA_AUTO
    pk_hold_btn(B_SAVE);
    check(step_muted(t, 3, 0) && step_on(&t->step[3]) && ui.scr == SCR_STEP, "picker: SAVE held alone: the note muted, kept");
    fm1_irq_off();
    (void)auto_step(t, 3);
    n0 = (uint32_t)STEP_MUTED_SYN(t);
    (void)auto_step(t, 5);
    n0 |= (uint32_t)STEP_MUTED_SYN(t) << 1;
    fm1_irq_on();
    check(n0 == 1u, "picker: the sequencer enters the muted step muted (a rest), the next note not");
    fm1_irq_off();
    (void)step_unlock(t, 3);
    fm1_irq_on();
    check(step_muted(t, 3, 0), "picker: the step's p-locks cleared: the mute stays (not a lock)");
    pk_hold_btn(B_SAVE);
    check(!step_muted(t, 3, 0), "picker: SAVE held again: the note on");
#endif
    n0 = pk_count_syn(t);
    press(B_SAVE);
    press(B_HOME);
    release(B_HOME);
    release(B_SAVE);
    frames(2);
    check(pk_count_syn(t) == n0 && ui.scr == SCR_STEP, "picker: SAVE then HOME (undo): neither deletes nor leaves STEP");
    turn(EN_K1, 1);
    turn(EN_K1, -1);                                    /* (the pick on a note again, whatever the undo did) */
    n0 = pk_count_syn(t);
    pk_hold_btn(B_HOME);
    check(pk_count_syn(t) + 1u == n0 && ui.scr == SCR_STEP, "picker: HOME held alone: the picked note deleted, STEP kept");
    tap(B_HOME);
    check(ui.scr != SCR_STEP, "picker: HOME tapped: back HOME, as ever");
}
static void picker_drum_tests(void)
{
    track_t *t;
    reset_ui();
    song.sel = TRK_DRUM;
    t = TDRUM;
    t->p[P_SLEN] = 16;
    track_defaults_steps(t);
    fm1_irq_off();
    memset(t->dstep, 0, 16u * sizeof(dstep_t));
    dstep_set(&t->dstep[1], 2, LV_NORM, 0);
    dstep_set(&t->dstep[6], 2, LV_NORM, 0);
    dstep_set(&t->dstep[4], 0, LV_NORM, 0);             /* (another lane's hit: not walked) */
    fm1_irq_on();
    tap(B_SEQ);
    lane_select(2);
    pk_cards_down();
    turn(EN_K1, 1);
    check(st.pk == 1u, "picker, drums: KNOB 1 picks the selected lane's first hit");
    turn(EN_K1, 1);
    check(st.pk == 6u, "picker, drums: ... the next one (another lane's hit passed)");
    turn(EN_K1, -1);
    turn(EN_K2, 1);
    check(dstep_has(&t->dstep[2], 2) && !dstep_has(&t->dstep[1], 2) && st.pk == 2u, "picker, drums: KNOB 2 moves the hit a step");
    turn(EN_K4, 1);
    check(dstep_lvl(&t->dstep[2], 2) == LV_HARD, "picker, drums: KNOB 4: its level (hard)");
    turn(EN_K3, 1);
    check(dstep_has(&t->dstep[2], 3) && !dstep_has(&t->dstep[2], 2) && lane_selected() == 3u && st.pk == 2u,
          "picker, drums: KNOB 3: the hit to the lane under it, that lane selected, still picked");
#if FELUCCA_AUTO
    pk_hold_btn(B_SAVE);
    check(step_muted(t, 2, 3), "picker, drums: SAVE held: the hit muted");
    fm1_irq_off();
    (void)auto_step(t, 2);
    fm1_irq_on();
    check(STEP_MUTED_LANES(t) == 1u << 3, "picker, drums: the sequencer enters the step with that lane muted only");
    turn(EN_K2, 1);
    check(dstep_has(&t->dstep[3], 3) && step_muted(t, 3, 3) && !step_muted(t, 2, 3), "picker, drums: a muted hit moved: its mute with it");
    pk_hold_btn(B_HOME);
    check(!dstep_has(&t->dstep[3], 3) && !step_muted(t, 3, 3) && dstep_has(&t->dstep[4], 0), "picker, drums: HOME held: the hit and its mute gone, the other lane's hit kept");
#endif
}
static void picker_tests(void)
{
    picker_synth_tests();
    picker_drum_tests();
    reset_ui();
}
