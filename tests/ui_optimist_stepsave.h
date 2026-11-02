/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, STEP's keys, SAVE and the picked note (the user, 2026-10-31):
 *   rec      "if REC is on the keyboard acts as midi keyboard, off it acts as step seq": REC off, a key toggles its step and
 *            the keys light the steps; REC on (armed), the keys play (a key starts the take, no step toggled) and light the
 *            keyboard; the drum track's white keys stay steps
 *   pick     "the last note entered/edited in the step seq must be selected": a step key's note, a held step's edit, a
 *            recorded note, a drum hit; KNOB 1 goes on from it; deleted: nothing picked; KNOB 1 left past the first: none
 *   save     no note picked: SAVE tapped stores the pattern into its own slot (unasked), SAVE + n into slot n (asked over a
 *            used one), SAVE held lights the used slots and the keys toggle no step, SAVE then HOME is undo (no store); a
 *            note picked: SAVE is the note's (its cards)
 * Included by ui_optimist_test.c after ui_optimist_stepedit.h (se_open, se_set, sgl_notes). */
static uint32_t sv_kw(uint32_t w) { return 1u << white_key(w); }
static uint32_t sv_whites(void)
{
    uint32_t w, m = 0;
    for (w = 0; w < 16u; w++)
        m |= sv_kw(w);
    return m;
}
static void sv_open(void)                               /* STEP on T1, LEN 16, one note in the pen, no chord mode */
{
    se_open(16);
    TSEL->p[P_CHORD] = 0;
    pen_n = 1, pen_note[0] = 60;
}
static uint32_t sv_hits(uint32_t lane)                  /* the drum track's steps with a hit of the lane */
{
    uint32_t i, n = 0;
    for (i = 0; i < NSTEP; i++)
        n += dstep_has(&TDRUM->dstep[i], lane) ? 1u : 0u;
    return n;
}
/* 2026-11-01 ("on SEQ, drums if REC is on black keys also record notes"): on STEP's drum track REC on, a black key plays its lane,
 * records the hit at the playhead and picks the lane (and the hit); REC off it only picks the lane (stopped: heard, not recorded) */
static void sv_drum_rec_tests(void)
{
    uint32_t f, d, h, n;
    memset(TDRUM->dstep, 0, sizeof TDRUM->dstep);
    lane_select(1);
    frames(2);
    check(rec_wait && step_drum_rec() && !song.playing, "drum rec: REC armed on STEP's drum track: the black keys are the pads");
    kdown(BK(5));
    frames(2);
    keys_lit(&f, &d, &h);
    check(lane_selected() == 5u && song.playing && ((song.rec >> TRK_DRUM) & 1u), "drum rec: a black key armed: the lane picked, the take started");
    check(sv_hits(5) == 1u && sv_hits(1) == 0u && !step_on(&TSEL->step[0]), "drum rec: ... its hit recorded at the playhead (one step, no step toggled)");
    check(f & (1u << BK(5)), "drum rec: ... its key lit");
    kup(BK(5));
    frames(4);
    check(pk_ok() && st.pk < NSTEP && dstep_has(&TDRUM->dstep[st.pk], 5), "drum rec: ... the hit entered is the picked note");
    n = sv_hits(5);
    kdown(BK(7));
    frames(2);
    kup(BK(7));
    frames(2);
    check(lane_selected() == 7u && sv_hits(7) == 1u && sv_hits(5) == n, "drum rec: recording: another black key picks its lane and records its hit");
    key(WK(3));
    frames(2);
    check(step_keys(fm1_in.buttons) && ly_lock == LY_STEP && lane_selected() == 7u, "drum rec: the white keys stay steps");
    tap(B_PLAY);
    frames(4);
    tap(B_REC);
    tap(B_REC);
    frames(2);
    check(!rec_wait && !song.rec && !step_drum_rec(), "drum rec: REC off: the black keys are lane picks");
    memset(TDRUM->dstep, 0, sizeof TDRUM->dstep);
    kdown(BK(2));
    frames(2);
    kup(BK(2));
    frames(2);
    check(lane_selected() == 2u && sv_hits(2) == 0u && !song.playing, "drum rec: REC off: a black key picks the lane, nothing recorded");
    tap(B_REC);
    frames(2);
}
static void sv_rec_tests(void)
{
    track_t *t;
    uint32_t f, d, h, n;
    sv_open();
    t = TSEL;
    fm1_irq_off();
    se_set(t, 2, 60, 1);                                /* (a project with notes: a key while armed starts the take) */
    fm1_irq_on();
    frames(2);
    key(WK(5));
    keys_lit(&f, &d, &h);
    check(step_on(&t->step[5]) && ly_lock == LY_STEP && (f & sv_kw(5)) && (d & sv_whites()),
          "keys: REC off: a key toggles its step, the keys light the steps");
    check(st.pk == 5u && pk_ok(), "pick: the note a step key set is the picked one");
    tap(B_REC);
    frames(2);
    check(rec_wait && se_on() && step_keys(fm1_in.buttons) && ly_lock == LY_STEP, "keys: REC on (armed), stopped: STEP ENTRY, the keys reach the UI");
    tap(B_PLAY);                                        /* (2026-11-01: PLAY in STEP ENTRY arms the take, the keys play) */
    frames(2);
    keys_lit(&f, &d, &h);
    check(rec_wait && !song.playing && !se_on() && !step_keys(fm1_in.buttons) && ly_lock == LY_PLAY && !(d & sv_whites()) && !(f & sv_kw(5)),
          "keys: REC on, PLAY armed: the keys play, no step lit or dim, the transport stopped");
    kdown(WK(9));
    n = pen_note[0];
    frames(2);
    keys_lit(&f, &d, &h);
    check(!step_on(&t->step[9]) && song.rec && song.playing && (f & (1u << WK(9))), "keys: ... a key: no step toggled, it plays and starts the take, its key lit");
    kup(WK(9));
    frames(4);
    check(pk_ok() && t->step[st.pk].note[st.nsel] == n && st.pk != 5u, "pick: the note recorded is the picked one");
    tap(B_PLAY);
    frames(4);
    check(!song.rec && !rec_wait && step_keys(fm1_in.buttons), "keys: stopped (REC off): the keys are steps again");
    /* the drum track: its white keys stay steps with REC on; a hit set is the picked one */
    reset_ui();
    song.sel = TRK_DRUM;
    memset(TDRUM->dstep, 0, sizeof TDRUM->dstep);
    tap(B_SEQ);
    lane_select(4);
    key(WK(6));
    check(dstep_has(&TDRUM->dstep[6], 4) && st.pk == 6u && pk_ok(), "pick: drums: the hit a step key set is the picked one");
    tap(B_REC);
    frames(2);
    check(rec_wait && step_keys(fm1_in.buttons), "keys: drums, REC on: the white keys stay steps (as on every page)");
    sv_drum_rec_tests();
    tap(B_REC);
    memset(TDRUM->dstep, 0, sizeof TDRUM->dstep);
    song.sel = 0;
}
static void sv_pick_tests(void)
{
    track_t *t;
    sv_open();
    t = TSEL;
    fm1_irq_off();
    se_set(t, 2, 60, 1), se_set(t, 9, 64, 1);
    fm1_irq_on();
    key(WK(5));
    check(st.pk == 5u, "pick: a step key's note picked");
    turn(EN_K1, 1);
    check(st.pk == 9u, "pick: KNOB 1 right goes on from it (the next note)");
    turn(EN_K1, -2);
    check(st.pk == 2u, "pick: ... left twice: the note before it");
    turn(EN_K1, -1);
    check(st.pk == PK_NONE && !pk_ok() && !strcmp(ui.msg, "NO NOTE"), "pick: KNOB 1 left past the first note: no note (NO NOTE)");
    turn(EN_K1, -1);
    check(st.pk == PK_NONE, "pick: ... further left: still none");
    turn(EN_K1, 1);
    check(st.pk == 2u, "pick: ... right: the first note again");
    kdown(WK(9));                                       /* a held step's edit: NOTE up */
    turn(EN_K1, 1);
    kup(WK(9));
    frames(OP_CARDS_FRAMES + 5u);
    check(st.pk == 9u && t->step[9].note[0] == 65u, "pick: a held step's note edited: it is the picked one");
    turn(EN_K2, -1);
    check(st.pk == 8u && !strcmp(sgl_notes(t), "2:60x1 5:60x1 8:65x1"), "pick: a moved note stays picked where it went");
    press(B_HOME);
    frames(HOLD_FRAMES);
    release(B_HOME);
    check(st.pk == PK_NONE && !pk_ok() && !strcmp(sgl_notes(t), "2:60x1 5:60x1"), "pick: the picked note deleted (HOME held): nothing picked");
    turn(EN_K1, 1);
    check(st.pk == 2u, "pick: ... KNOB 1 right: the first note");
}
#if FELUCCA_PATTERNS
static void sv_save_tests(void)
{
    track_t *t;
    uint32_t f, d, h, sl, used = 0, w;
    char nm[16], cl[16];
    sv_open();
    for (sl = 0; sl < 16u && (pat_has(0, sl) || sl == 2u || sl == 7u || sl == 8u || sl == 10u); sl++)
        ;                                               /* (a free slot whose step the test leaves alone) */
    snprintf(nm, sizeof nm, "STORED T1 %u", sl + 1u);
    snprintf(cl, sizeof cl, "CLONED T1 %u", sl + 1u);
    t = TSEL;
    fm1_irq_off();
    se_set(t, 2, 60, 1);
    fm1_irq_on();
    st.pk = PK_NONE;
    press(B_SAVE);
    kdown(WK(sl));
    kup(WK(sl));
    release(B_SAVE);
    check(clone_asked() && !pat_has(0, sl) && !step_on(&t->step[sl]), "save: SAVE + a white key (an empty slot): \"CLONE\" asked, no step toggled");
    clone_yes();
    check(sl < 16u && pat_has(0, sl) && pat_cur[0] == sl && !step_on(&t->step[sl]) && !op_armed() && !strcmp(ui.msg, cl),
          "save: ... YES: stored there, no step toggled");
    press(B_SAVE);
    frames(2);
    keys_lit(&f, &d, &h);
    for (w = 0; w < 16u; w++)
        used |= pat_has(0, w) ? sv_kw(w) : 0u;
    check((f & sv_whites()) == used && (used & sv_kw(sl)) && step_keys(fm1_in.buttons), "save: SAVE held: the used slot's key lit, nothing else");
    frames(HOLD_FRAMES);
    release(B_SAVE);
    check(!op_armed() && !step_on(&t->step[sl]), "save: ... let go after the hold: nothing stored, nothing asked");
    key(WK(7));
    check(step_on(&t->step[7]) && (pat_changed() & 1u), "save: a note added: the working pattern differs from its slot");
    st.pk = PK_NONE;
    ui.msg[0] = 0;
    tap(B_SAVE);
    check(!op_armed() && !(pat_changed() & 1u) && !strcmp(ui.msg, nm), "save: SAVE tapped, no note picked: stored into its own slot, unasked");
    st.pk = PK_NONE;
    press(B_SAVE);
    kdown(WK(sl));
    kup(WK(sl));
    release(B_SAVE);
    check(op_armed() && ui.arm_scr == ARM_OP, "save: SAVE + that key again, the slot used: asked");
    op_disarm();
    key(WK(8));
    key(WK(10));
    st.pk = PK_NONE;
    press(B_SAVE);
    press(B_HOME);
    release(B_HOME);
    release(B_SAVE);
    check(!step_on(&t->step[10]) && step_on(&t->step[8]) && !strncmp(ui.msg, "UNDO", 4) && (pat_changed() & 1u) && !op_armed(),
          "save: SAVE then HOME: undo, nothing stored");
    turn(EN_K1, 1);
    check(pk_ok(), "save: a note picked");
    ui.msg[0] = 0;
    tap(B_SAVE);
    check(st.latch && strcmp(ui.msg, nm) && (pat_changed() & 1u), "save: a note picked, SAVE tapped: its cards, no store");
    tap(B_HOME);
}
#endif
static void stepsave_tests(void)
{
    sv_rec_tests();
    sv_pick_tests();
#if FELUCCA_PATTERNS
    sv_save_tests();
#endif
    se_open(16);
    tap(B_HOME);
    reset_ui();
}
