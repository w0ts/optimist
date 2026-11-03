/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, STEP's two modes on a synth track (the user, 2026-11-03): ENTRY MODE (STEP ENTRY) and MUTE MODE (the step
 * sequencer's keys: a tapped step with a note is muted / unmuted, not taken out). Each track keeps its mode; the roll's right edge says
 * which (C_NOTE ENTRY, grey MUTE); a triangle at the roll's top / foot marks a step with a note the rows cannot show. Included by
 * ui_optimist_test.c after ui_optimist_stepentry.h (sen_open, sen_rec_off). */
static void smd_dbl(void)                               /* two SEQ taps, the cards put back */
{
    tap(B_SEQ);
    tap(B_SEQ);
    frames(30);
}
static int smd_edge(uint16_t col)                       /* the roll's right-edge bar is on the screen in col */
{
    return px_in(RL_X + 16u * RL_CW + 2u, SP_Y + SG_TOP + 4u, 3, 6, col);
}
static void stepmode_mute_tests(void)
{
    track_t *t;
    uint32_t n0;
    sen_open(16);
    sen_rec_off();
    cards_up();
    frames(30);
    t = TSEL;
    check(!se_on() && se_mute_mode(), "step modes: REC off, stopped: MUTE MODE");
    n0 = kb_map(t, WK(0));
    key(WK(0));
    check(step_on(&t->step[0]), "step modes, mute: an empty step tapped: a note set as before");
#if FELUCCA_AUTO
    key(WK(0));
    check(step_on(&t->step[0]) && step_muted(t, 0, 0) && !strcmp(ui.msg, "STEP MUTED"),
          "step modes, mute: the step tapped again: its note muted and kept, said STEP MUTED");
    n0 = t->step[0].note[0];
    frames(2);
    check(!step_shown(t, 0), "step modes, mute: ... a muted step's key is dim, not lit");
    key(WK(0));
    check(step_on(&t->step[0]) && !step_muted(t, 0, 0) && t->step[0].note[0] == n0 && !strcmp(ui.msg, "STEP ON"),
          "step modes, mute: ... tapped again: on again at its pitch, said STEP ON");
    key(WK(0));
    check(step_muted(t, 0, 0) && st.pk == 0u, "step modes, mute: ... muted again, its note the picked one");
#endif
    reset_ui();
}
static void stepmode_memory_tests(void)
{
    sen_open(16);
    sen_rec_off();
    cards_up();
    frames(30);
    check(!se_on() && st.se_pref[0] == 0u, "step modes, memory: T1 starts as REC and the transport say (stopped, REC off: MUTE)");
    smd_dbl();
    check(se_on() && st.se_pref[0] == 1u && st.se_pref[1] == 0u, "step modes, memory: two SEQ taps: T1's mode is ENTRY, T2's untouched");
    op_enter(SCR_HOME);
    frames(2);
    op_enter(SCR_STEP);
    frames(2);
    check(se_on() && st.se_pref[0] == 1u, "step modes, memory: STEP left and entered: T1 is still ENTRY");
    song.sel = 1;
    frames(2);
    check(!se_on() && se_mute_mode(), "step modes, memory: T2 on STEP: its own mode, MUTE");
    song.sel = 0;
    frames(2);
    check(se_on(), "step modes, memory: back on T1: ENTRY");
    smd_dbl();
    check(!se_on() && se_mute_mode() && st.se_pref[0] == 2u, "step modes, memory: two SEQ taps again: T1 is MUTE");
    song.sel = 1;
    song.sel = 0;
    frames(2);
    check(!se_on() && se_mute_mode(), "step modes, memory: T1 stays MUTE after another track was selected");
    reset_ui();
}
static void stepmode_cue_tests(void)
{
    track_t *t;
    sen_open(16);
    sen_rec_off();
    pk_cards_down();
    frames(2);
    t = TSEL;
    frames(2);
    check(smd_edge(mute_col()) && !smd_edge(C_NOTE), "step modes, cue: MUTE MODE: the roll's right edge grey");
    smd_dbl();
    check(se_on() && smd_edge(C_NOTE) && !smd_edge(mute_col()), "step modes, cue: ENTRY MODE: the roll's right edge C_NOTE");
    reset_ui();
    /* notes the rows cannot show: a triangle at the roll's top (above) and foot (below), in the step's column */
    sen_open(16);
    sen_rec_off();
    pk_cards_down();
    frames(2);
    t = TSEL;
    t->step[1].time = ST_NOTE, t->step[1].n = 1, t->step[1].note[0] = 20, t->step[1].vel = 100;
    t->step[3].time = ST_NOTE, t->step[3].n = 1, t->step[3].note[0] = 110, t->step[3].vel = 100;
    t->step[5].time = ST_NOTE, t->step[5].n = 1, t->step[5].note[0] = 64, t->step[5].vel = 100;
    t->step[6].time = ST_NOTE, t->step[6].n = 1, t->step[6].note[0] = 66, t->step[6].vel = 100;
    song.octave = 2;                                    /* (the view two octaves up: 20 is under it, 110 over it, 64 and 66 in) */
    sync_reload = 1;
    ui.sig[2] = 0;
    frames(4);
    roll_range(t);
    check(rg.lo > 20 && rg.hi < 110, "step modes, more: the rows hold 64 and 66, not 20 nor 110");
    check(px_in(RL_X + 3u * RL_CW, SP_Y + SG_TOP, RL_CW - 1u, 3, C_AMB) && !px_in(RL_X + 3u * RL_CW, SP_Y + SG_TOP + (uint32_t)(rg.n * rg.rh) - 3u, RL_CW - 1u, 3, C_AMB),
          "step modes, more: a note above the top row: a triangle at the roll's top, in its column only");
    check(px_in(RL_X + 1u * RL_CW, SP_Y + SG_TOP + (uint32_t)(rg.n * rg.rh) - 3u, RL_CW - 1u, 3, C_AMB) && !px_in(RL_X + 1u * RL_CW, SP_Y + SG_TOP, RL_CW - 1u, 3, C_AMB),
          "step modes, more: a note below the lowest row: a triangle at its foot");
    check(!px_in(RL_X + 5u * RL_CW, SP_Y + SG_TOP, RL_CW - 1u, 3, C_AMB) && !px_in(RL_X + 6u * RL_CW, SP_Y + SG_TOP, RL_CW - 1u, 3, C_AMB),
          "step modes, more: notes inside the rows: no triangle");
    reset_ui();
}
static void stepmode_tests(void)
{
    stepmode_mute_tests();
    stepmode_memory_tests();
    stepmode_cue_tests();
}
