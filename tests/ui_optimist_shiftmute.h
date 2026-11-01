/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, STEP's right shift (the user, 2026-11-01: "right shift should also go to muted state for now (also for
 * synths) and then the note is grey"): SHIFT2 (OCT+ held) + a step key on a set step toggles the mute of its notes (a synth step) or
 * of the selected lane's hit (the drum track), the store event of SAVE held on a picked note (AUTO_SMUTE / AUTO_MUTE): toast MUTED /
 * UNMUTED, one undo level, the muted note the picked one, drawn grey, its key dim, played as a rest. An empty drum step is still the
 * hard hit, an empty synth step a plain tap. Included by ui_optimist_test.c after ui_optimist_stepsave.h. */
#if FELUCCA_AUTO
static void shm_key(uint32_t w)                         /* SHIFT2 held + step key w, then let go (the octave must stay) */
{
    press(B_OCTUP);
    kdown(WK(w));
    kup(WK(w));
    release(B_OCTUP);
}
static uint32_t shm_peak(track_t *t, uint32_t n)        /* the most notes the track sounded at once over n frames of playing */
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
static void shm_synth_tests(void)
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
    grey = col_shade(trk_col(0), 2u);
    before = sm_count(grey);
    shm_key(0);
    check(step_muted(t, 0, 0) && step_on(&t->step[0]) && t->step[0].n == 2 && !strcmp(ui.msg, "MUTED"),
          "shift mute: SHIFT2 + a synth step: muted (both notes kept), toast MUTED");
    check(song.octave == oct, "shift mute: ... the octave unchanged (SHIFT2 was used)");
    check(pk_ok() && st.pk == 0u, "shift mute: ... the muted note is the picked one");
    frames(4);
    keys_lit(&f, &d, &h);
    check(!(f & sv_kw(0)) && (d & sv_kw(0)), "shift mute: ... its key dim, not lit");
    check(sm_count(grey) > before, "shift mute: ... drawn grey");
    shm_key(0);
    check(!step_muted(t, 0, 0) && step_on(&t->step[0]) && !strcmp(ui.msg, "UNMUTED"), "shift mute: again: unmuted, toast UNMUTED");
    shm_key(0);
    check(shm_peak(t, 90) == 0u, "shift mute: a muted step plays as a rest");
    st.pk = PK_NONE;
    sm_undo();
    check(!step_muted(t, 0, 0) && step_on(&t->step[0]), "shift mute: undo: one level, unmuted again");
    check(shm_peak(t, 90) > 0u, "shift mute: ... and played again");
    shm_key(5);
    check(step_on(&t->step[5]) && !step_muted(t, 5, 0), "shift mute: SHIFT2 + an empty synth step: the plain tap, a note set");
}
static void shm_drum_tests(void)
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
    shm_key(1);
    check(step_muted(t, 1, 2) && !step_muted(t, 1, 3) && dstep_has(&t->dstep[1], 2) && !strcmp(ui.msg, "MUTED"),
          "shift mute: SHIFT2 + a drum hit: that lane's hit muted, the other lane's not");
    check(pk_ok() && st.pk == 1u, "shift mute: ... picked");
    frames(4);
    keys_lit(&f, &d, &h);
    check(!(f & sv_kw(1)) && (d & sv_kw(1)), "shift mute: ... its key dim");
    shm_key(1);
    check(!step_muted(t, 1, 2) && !strcmp(ui.msg, "UNMUTED"), "shift mute: drums again: unmuted");
    shm_key(1);
    st.pk = PK_NONE;
    sm_undo();
    check(!step_muted(t, 1, 2) && dstep_has(&t->dstep[1], 2), "shift mute: drums undo: back");
    shm_key(6);
    check(dstep_has(&t->dstep[6], 2) && dstep_lvl(&t->dstep[6], 2) == LV_HARD && !step_muted(t, 6, 2),
          "shift mute: SHIFT2 + an empty drum step: still the hard hit");
}
static void shiftmute_tests(void)
{
    shm_synth_tests();
    shm_drum_tests();
    se_open(16);
    tap(B_HOME);
    reset_ui();
}
#endif
