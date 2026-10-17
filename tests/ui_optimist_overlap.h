/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, a synth note past another on STEP (the user, 2026-10-17: "in seq for drum we can see the selected
 * note super well, for synth tracks not at all. When we move a note make sure its plock follow. We should be able to extend /
 * move a note past another one in synth seq, some snapping that helps feel when the note touches"):
 *   the picker's note (KNOB 1, nothing held) drawn in C_NOTE over its whole length, its step framed; none picked: none
 *   KNOB 2 (move) into a note: the snap, then past it whole (ties and all), each note's locks with it
 *   KNOB 4 (LENGTH) into a note: the snap, then over it (a carry step: both sound), on past its end, back; note_span says so
 *   ALGORITHM / KNOB 3 on a note over another: it stays whole (its carry steps renamed)
 * Included by ui_optimist_test.c after ui_optimist_picker.h. */
static uint32_t ov_span(const track_t *t, uint32_t i)    /* the steps note 0 of step i sounds */
{
    return (note_span(t, i, 0) + 127u) / 128u;
}
static void overlap_tests(void)
{
    track_t *t;
    uint32_t n0;
    reset_ui();
    song.sel = 0;
    t = TSEL;
    t->p[P_SLEN] = 16;
    track_defaults_steps(t);
    fm1_irq_off();
    t->step[2].time = ST_NOTE, t->step[2].n = 1, t->step[2].note[0] = 60, t->step[2].vel = 100;
    t->step[3].time = ST_TIE;
    t->step[5].time = ST_NOTE, t->step[5].n = 1, t->step[5].note[0] = 67, t->step[5].vel = 100;
    t->step[6].time = ST_TIE;
#if FELUCCA_AUTO
    AL(t)->n = 0;
    (void)auto_put(AL(t), 2 | AUTO_ONLY, 0, 11);        /* a lock on each note's first step and on a TIE: they go with their notes */
    (void)auto_put(AL(t), 3 | AUTO_ONLY, 0, 12);
    (void)auto_put(AL(t), 5 | AUTO_ONLY, 0, 21);
#endif
    fm1_irq_on();
    tap(B_SEQ);
    pk_cards_down();
    n0 = sm_count(C_NOTE);
    check(n0 == 0u, "overlap: nothing picked: no note in the selection colour");
    turn(EN_K1, 1);
    ui.force = 1;
    frames(2);
    ppm("opt-step-picked");
    check(st.pk == 2u && sm_count(C_NOTE) > 0u, "overlap: a synth note picked (KNOB 1): drawn in the selection colour, its step framed");
    /* KNOB 2: move right: one step (4, 5 touch 67), the snap, past it */
    turn(EN_K2, 1);
    check(st.pk == 3u && step_on(&t->step[3]) && t->step[4].time == ST_TIE, "overlap: KNOB 2 right: the note a step on");
    turn(EN_K2, 1);
    check(st.pk == 3u && t->step[3].note[0] == 60 && t->step[5].note[0] == 67, "overlap: touching the next note: the snap, nothing moves");
    turn(EN_K2, 1);
    check(st.pk == 5u && t->step[3].note[0] == 67 && t->step[4].time == ST_TIE && t->step[5].note[0] == 60 && t->step[6].time == ST_TIE,
          "overlap: the next detent: past it, ties and all");
#if FELUCCA_AUTO
    {
        int32_t v;
        check(auto_get(AL(t), 5 | AUTO_ONLY, 0, &v) && v == 11 && auto_get(AL(t), 6 | AUTO_ONLY, 0, &v) && v == 12 &&
              auto_get(AL(t), 3 | AUTO_ONLY, 0, &v) && v == 21, "overlap: ... each note's locks went with it (its TIE's too)");
    }
#endif
    turn(EN_K2, -1);
    turn(EN_K2, -1);
    check(st.pk == 3u && t->step[3].note[0] == 60 && t->step[5].note[0] == 67, "overlap: KNOB 2 left: the snap, then back past it");
    /* KNOB 4: LENGTH from 2 steps (3, 4): it touches 67 at 5: the snap, then over it */
    turn(EN_K4, 1);
    check(ov_span(t, 3) == 2u && t->step[5].note[0] == 67 && t->step[5].n == 1u, "overlap: LENGTH into the next note: the snap, nothing changes");
    turn(EN_K4, 1);
    check(ov_span(t, 3) == 3u && t->step[5].n == 2u && (t->step[5].flags & SF_CARRY) && ov_span(t, 5) == 2u,
          "overlap: the next detent: over it (a carry step: both sound), the other note's length kept");
    turn(EN_K4, 2);
    check(ov_span(t, 3) == 5u && ov_span(t, 5) == 2u && t->step[7].time == ST_NOTE && (t->step[7].flags & SF_CARRY) &&
              t->step[7].n == 1u && t->step[7].note[0] == 60,
          "overlap: on past the other's end (a carry step of it alone), the other still two steps");
    turn(EN_K3, 1);
    check(t->step[3].note[0] == 61 && ov_span(t, 3) == 5u && t->step[7].note[0] == 61, "overlap: KNOB 3 on it: its pitch, still whole over the other");
    turn(EN_K3, -1);
    turn(EN_K4, -4);
    check(ov_span(t, 3) == 1u && t->step[5].n == 1u && t->step[5].note[0] == 67 && !(t->step[5].flags & SF_CARRY) && ov_span(t, 5) == 2u &&
              t->step[4].time == ST_REST && t->step[7].time == ST_REST,
          "overlap: LENGTH back to one step: the steps as they were around the other note");
    turn(EN_K4, 1);
    check(ov_span(t, 3) == 2u && t->step[4].time == ST_TIE, "overlap: and up again to touch it: a plain TIE");
    tap(B_HOME);
    ui.lane = 0;
    song.sel = 0;
    reset_ui();
}
