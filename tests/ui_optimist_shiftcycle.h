/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, STEP's right shift (the user, 2026-11-01: "it should cycle velocities and mute. with visual feedback"):
 * SHIFT2 (OCT+ held) + a step key on a set step cycles NORM -> HARD -> GHOST -> MUTED -> NORM: a synth step's notes (the step's level),
 * the drum track's selected lane's hit. MUTED is the store event of SAVE held on a picked note (AUTO_SMUTE / AUTO_MUTE): the note kept,
 * grey, its key dim, played as a rest. A popup names each state, one undo level per press, the step's note the picked one. An empty
 * drum step is still the hard hit, an empty synth step a plain tap. Included by ui_optimist_test.c after ui_optimist_stepsave.h. */
#if FELUCCA_AUTO
static void shc_key(uint32_t w)                         /* SHIFT2 held + step key w, then let go (the octave must stay) */
{
    press(B_OCTUP);
    kdown(WK(w));
    kup(WK(w));
    release(B_OCTUP);
}
static uint32_t shc_peak(track_t *t, uint32_t n)        /* the most notes the track sounded at once over n frames of playing */
{
    uint32_t m = 0;
    tap(B_PLAY);
    while (n--) {
        frame();
        if (t->seq_n > m)
            m = t->seq_n;
    }
    tap(B_PLAY);
    frames(4);
    return m;
}
static uint32_t shc_slv(track_t *t, uint32_t i, uint32_t k)   /* a synth step's note k level */
{
    return (uint32_t)(t->step[i].lvl >> (2u * k)) & 3u;
}
static void shc_synth_tests(void)
{
    track_t *t;
    uint32_t f, d, h, grey, before;
    int32_t oct;
    sv_open();
    t = TSEL;
    fm1_irq_off();
    se_set(t, 0, 60, 1);
    t->step[0].n = 2, t->step[0].note[1] = 64;
    fm1_irq_on();
    st.pk = PK_NONE;
    oct = song.octave;
    check(shc_slv(t, 0, 0) == LV_NORM && shc_slv(t, 0, 1) == LV_NORM, "shift cycle: a set synth step starts NORM");
    shc_key(0);
    check(shc_slv(t, 0, 0) == LV_HARD && shc_slv(t, 0, 1) == LV_HARD && !step_muted(t, 0, 0) && t->step[0].n == 2
          && strstr(ui.msg, "STEP 1 HARD") && !strncmp(ui.msg, "T1", 2), "shift cycle: SHIFT2 + a synth step: HARD on both notes, popup 'T1 STEP 1 HARD'");
    check(song.octave == oct, "shift cycle: ... the octave unchanged (SHIFT2 was used)");
    check(pk_ok() && st.pk == 0u, "shift cycle: ... the step's note is the picked one");
    shc_key(0);
    check(shc_slv(t, 0, 0) == LV_GHOST && shc_slv(t, 0, 1) == LV_GHOST && !step_muted(t, 0, 0) && strstr(ui.msg, "GHOST"),
          "shift cycle: again: GHOST, popup says GHOST");
    grey = col_shade(trk_col(0), 2u);
    before = sm_count(grey);
    shc_key(0);
    check(step_muted(t, 0, 0) && step_on(&t->step[0]) && t->step[0].n == 2 && strstr(ui.msg, "STEP 1 MUTED"),
          "shift cycle: again: MUTED (both notes kept), popup says MUTED");
    frames(4);
    keys_lit(&f, &d, &h);
    check(!(f & sv_kw(0)) && (d & sv_kw(0)), "shift cycle: ... its key dim, not lit");
    check(sm_count(grey) > before, "shift cycle: ... drawn grey");
    shc_key(0);
    check(!step_muted(t, 0, 0) && shc_slv(t, 0, 0) == LV_NORM && shc_slv(t, 0, 1) == LV_NORM && strstr(ui.msg, "STEP 1 NORM"),
          "shift cycle: again: unmuted, back to NORM, popup says NORM");
    for (f = 0; f < 3u; f++)
        shc_key(0);
    check(step_muted(t, 0, 0), "shift cycle: three more: muted again");
    check(shc_peak(t, 90) == 0u, "shift cycle: a muted step plays as a rest");
    st.pk = PK_NONE;
    sm_undo();
    check(!step_muted(t, 0, 0) && shc_slv(t, 0, 0) == LV_GHOST && step_on(&t->step[0]), "shift cycle: undo: one level, GHOST and unmuted again");
    check(shc_peak(t, 90) > 0u, "shift cycle: ... and played again");
    shc_key(5);
    check(step_on(&t->step[5]) && !step_muted(t, 5, 0) && shc_slv(t, 5, 0) == LV_NORM, "shift cycle: SHIFT2 + an empty synth step: the plain tap, a NORM note");
}
static void shc_drum_tests(void)
{
    track_t *t;
    uint32_t f, d, h;
    se_open(16);
    song.sel = TRK_DRUM;
    t = TSEL;
    track_defaults_steps(t);
    t->p[P_SLEN] = 16;
    fm1_irq_off();
    dstep_set(&t->dstep[1], 2u, LV_NORM, 0);
    dstep_set(&t->dstep[1], 3u, LV_NORM, 0);
    fm1_irq_on();
    lane_select(2);
    frames(2);
    st.pk = PK_NONE;
    shc_key(1);
    check(dstep_lvl(&t->dstep[1], 2) == LV_HARD && !step_muted(t, 1, 2) && strstr(ui.msg, " HARD"), "shift cycle: SHIFT2 + a drum hit: HARD, popup says HARD");
    check(dstep_lvl(&t->dstep[1], 3) == LV_NORM && !step_muted(t, 1, 3), "shift cycle: ... the other lane's hit untouched");
    check(pk_ok() && st.pk == 1u, "shift cycle: ... picked");
    shc_key(1);
    check(dstep_lvl(&t->dstep[1], 2) == LV_GHOST && !step_muted(t, 1, 2) && strstr(ui.msg, " GHOST"), "shift cycle: drums again: GHOST");
    shc_key(1);
    check(step_muted(t, 1, 2) && !step_muted(t, 1, 3) && dstep_has(&t->dstep[1], 2) && strstr(ui.msg, " MUTED"),
          "shift cycle: drums again: MUTED, that lane's hit only, popup says MUTED");
    frames(4);
    keys_lit(&f, &d, &h);
    check(!(f & sv_kw(1)) && (d & sv_kw(1)), "shift cycle: ... its key dim");
    shc_key(1);
    check(!step_muted(t, 1, 2) && dstep_lvl(&t->dstep[1], 2) == LV_NORM && strstr(ui.msg, " NORM"), "shift cycle: drums again: unmuted, NORM");
    shc_key(1);
    st.pk = PK_NONE;
    sm_undo();
    check(dstep_lvl(&t->dstep[1], 2) == LV_NORM && dstep_has(&t->dstep[1], 2), "shift cycle: drums undo: one level, NORM again");
    shc_key(6);
    check(dstep_has(&t->dstep[6], 2) && dstep_lvl(&t->dstep[6], 2) == LV_HARD && !step_muted(t, 6, 2),
          "shift cycle: SHIFT2 + an empty drum step: still the hard hit");
}
static void shiftcycle_tests(void)
{
    shc_synth_tests();
    shc_drum_tests();
    se_open(16);
    tap(B_HOME);
    reset_ui();
}
#endif
