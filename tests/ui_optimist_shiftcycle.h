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
/* the photo's case (the user, 2026-11-01, IMG_4585): B and A struck at step 1 and carried through step 2, an E struck at step 2 */
static void shc_photo(track_t *t, uint32_t with_carry)
{
    fm1_irq_off();
    memset(&t->step[0], 0, sizeof t->step[0]);
    memset(&t->step[1], 0, sizeof t->step[1]);
    t->step[0].time = ST_NOTE, t->step[0].n = 2, t->step[0].note[0] = 71, t->step[0].note[1] = 69, t->step[0].vel = 100;
    t->step[1].time = ST_NOTE, t->step[1].vel = 100;
    if (with_carry)
        t->step[1].n = 3, t->step[1].flags = SF_CARRY, t->step[1].note[0] = 71, t->step[1].note[1] = 69, t->step[1].note[2] = 64;
    else
        t->step[1].n = 1, t->step[1].note[0] = 64;
    fm1_irq_on();
}
static void shc_carry_tests(void)
{
    track_t *t;
    step_t s0, s1;
    uint32_t grey, g1, g2, hard, c;
    sv_open();
    t = TSEL;
    c = trk_col(0);
    grey = col_shade(c, 2u);
    shc_photo(t, 1);
    st.pk = PK_NONE;
    frames(2);
    s0 = t->step[0], s1 = t->step[1];
    shc_key(1);
    check(!memcmp(&t->step[0], &s0, sizeof s0), "shift cycle, carried: B and A's own step byte-identical");
    check(shc_slv(t, 1, 0) == (uint32_t)((s1.lvl >> 0) & 3u) && shc_slv(t, 1, 1) == (uint32_t)((s1.lvl >> 2) & 3u) && shc_slv(t, 1, 2) == LV_HARD
          && t->step[1].n == 3 && t->step[1].flags == s1.flags && !step_muted(t, 1, 0),
          "shift cycle, carried: only the note that starts there (E) takes HARD, the carried B and A keep their level");
    check(pk_ok() && st.pk == 1u && st.nsel == 2u, "shift cycle, carried: ... E is the picked note");
    frames(4);
    hard = lvl_col(c, LV_HARD);
    check(sm_count(hard) > 0 && sm_count(C_NOTE) > 0, "shift cycle, carried: the selected E shows its level shade and a C_NOTE outline");
    shc_key(1);
    shc_key(1);
    check(step_muted(t, 1, 0) && shc_slv(t, 1, 0) == (uint32_t)(s1.lvl & 3u), "shift cycle, carried: GHOST then MUTED on the step, the carried slots still untouched");
    frames(4);
    g1 = sm_count(grey);
    shc_photo(t, 0);
    frames(4);
    g2 = sm_count(grey);
    check(g1 == g2 && g1 > 0, "shift cycle, carried: muted, only E is drawn grey (B and A carried through it keep their shade, picked E grey too)");
    check(sm_count(C_NOTE) > 0, "shift cycle, carried: ... the muted selected E keeps its outline");
    shc_photo(t, 1);
    st.pk = PK_NONE;
    shc_key(1);
    check(!step_muted(t, 1, 0) && shc_slv(t, 1, 2) == LV_NORM, "shift cycle, carried: again: unmuted, E back to NORM");
    /* no note starts there: a carry step of B and A only */
    fm1_irq_off();
    t->step[1].n = 2, t->step[1].lvl = 0, memset(&t->step[2], 0, sizeof t->step[2]);
    fm1_irq_on();
    s1 = t->step[1];
    st.pk = PK_NONE;
    shc_key(1);
    check(!memcmp(&t->step[1], &s1, sizeof s1) && !memcmp(&t->step[0], &s0, sizeof s0) && !step_muted(t, 1, 0) && strstr(ui.msg, "NO NOTE STARTS HERE"),
          "shift cycle, carried: no note starts at the step: nothing touched, 'NO NOTE STARTS HERE'");
}
static void shiftcycle_tests(void)
{
    shc_synth_tests();
    shc_carry_tests();
    shc_drum_tests();
    se_open(16);
    tap(B_HOME);
    reset_ui();
}
#endif
