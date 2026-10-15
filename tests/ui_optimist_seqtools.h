/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, FELUCCA_SEQTOOLS and FELUCCA_COND (Felucca 1.5.6, ui/optimist/op_tools.c): STEP's TOOLS
 * page (REVERSE RANDOM COOK LENGTH: a turn arms, YES does it, one undo each) and EUCLID page (live, one undo a run), on
 * a synth track whose notes have exact lengths (a NOTE and its TIEs; a note ending in its own step, ngate; a note
 * going on into a carry step, SF_CARRY) and on the drum track (DR: the kit; a lane picked: that lane); the steps'
 * events move with them; the EDIT layer's SHIFT and LENGTH move and copy them; the held step's COND card. */
#if FELUCCA_SEQTOOLS
static void stt_undo(void)                              /* SAVE then HOME (op_combos.c) */
{
    press(B_SAVE);
    press(B_HOME);
    release(B_HOME);
    release(B_SAVE);
}
static void stt_open(uint32_t k, uint32_t page)         /* track k on STEP, page 0 TOOLS / 1 EUCLID */
{
    reset_ui();
    song.sel = (uint8_t)k;
    ui.lane = 0;
    tap(B_SEQ);
    ui.row[SCR_STEP] = (uint8_t)(step_rows0() + page);
    frame();
}
static void stt_yes(void)                               /* SAVE, and SAVE again at the question */
{
    tap(B_SAVE);
    if (op_armed())
        tap(B_SAVE);
}
static void stt_do(uint32_t card)                       /* arm card (KNOB card + 1) and YES */
{
    turn(EN_K1 + card, 1);
    tap(B_SAVE);
    if (op_armed())                                     /* (REVERSE, RANDOM, COOK ask first, the user 2026-10-13) */
        tap(B_SAVE);
}
static void stt_note(step_t *s, uint32_t note, uint32_t flags, uint32_t g)
{
    memset(s, 0, sizeof *s);
    s->time = ST_NOTE, s->n = 1, s->note[0] = (uint8_t)note, s->vel = 100, s->flags = (uint8_t)flags, s->ngate[0] = (uint8_t)g;
}
/* 8 steps: 0 N60 T T (3 steps), 4 N64 ending half way (ngate 64), 5 N67 going on into the carry step 6 (ends at 50) */
static void stt_chains(track_t *t)
{
    uint32_t i;
    for (i = 0; i < NSTEP; i++)
        memset(&t->step[i], 0, sizeof t->step[i]), t->step[i].time = ST_REST;
    t->p[P_SLEN] = 8;
    t->p[P_SCALE] = 0;
    t->p[P_ROOT] = 0;
    stt_note(&t->step[0], 60, 0, 0);
    t->step[1].time = ST_TIE, t->step[2].time = ST_TIE;
    stt_note(&t->step[4], 64, 0, 64);
    stt_note(&t->step[5], 67, 0, 0);
    stt_note(&t->step[6], 67, SF_CARRY, 50);
#if FELUCCA_AUTO
    AL(t)->n = 0;
#endif
}
/* the notes' spans in loop order (1/128 steps), and that a carry step still names its note's pitch */
static uint32_t stt_spans(const track_t *t, uint32_t *sp)
{
    uint32_t i, n = 0;
    uint8_t cont[NSTEP];
    tl_cont(t, trk_len(t), cont);
    for (i = 0; i < trk_len(t); i++)
        if (tl_gate(&t->step[i]) && !(cont[i] & 1u))
            sp[n++] = note_span(t, i, 0);
    return n;
}
/* the drum track's ACCENT lane (core.h DRUM_ACC) as a lane for the tools: that lane alone, or with the kit */
static uint32_t stt_acc(const track_t *t, uint32_t len)  /* its steps, a bit each */
{
    uint32_t i, m = 0;
    for (i = 0; i < len; i++)
        m |= (uint32_t)dstep_has(&t->dstep[i], DRUM_ACC) << i;
    return m;
}
static void accent_tools_tests(void)
{
    track_t *t = TDRUM;
    uint32_t i, kick = 0, n, m;
    reset_ui();
    song.sel = TRK_DRUM;
    t->p[P_SLEN] = 8;
    memset(t->dstep, 0, sizeof t->dstep);
    for (i = 0; i < 8u; i += 2u)
        dstep_set(&t->dstep[i], 0, LV_NORM, 0), kick |= 1u << i;
    dstep_set(&t->dstep[1], DRUM_ACC, LV_HARD, 1);
    dstep_set(&t->dstep[2], DRUM_ACC, LV_NORM, 0);
    tl_mark(t);
    tl_reverse(t, 8, (int32_t)DRUM_ACC);
    for (i = 0, n = 0; i < 8u; i++)
        n |= (uint32_t)dstep_has(&t->dstep[i], 0) << i;
    check(stt_acc(t, 8) == (1u << 6 | 1u << 5) && dstep_lvl(&t->dstep[6], DRUM_ACC) == LV_HARD &&
          dstep_rat(&t->dstep[6], DRUM_ACC) == 1u && n == kick, "the ACCENT lane: REVERSE that lane (its level and ratchet), the kick as it was");
    tl_mark(t);
    tl_reverse(t, 8, -1);
    for (i = 0, n = 0; i < 8u; i++)
        n |= (uint32_t)dstep_has(&t->dstep[i], 0) << i;
    check(stt_acc(t, 8) == (1u << 1 | 1u << 2) && n == (kick << 1 & 0xFFu), "... REVERSE of the kit: the accents with their steps");
    kick = n;
    for (i = 0; i < 8u; i++)
        dstep_clr(&t->dstep[i], DRUM_ACC);
    tl_euclid(t, 8, DRUM_ACC, 3, 0, 0);
    for (i = 0, n = 0, m = 0; i < 8u; i++)
        n += (uint32_t)dstep_has(&t->dstep[i], DRUM_ACC), m |= (uint32_t)dstep_has(&t->dstep[i], 0) << i;
    check(n == 3u && m == kick, "EUCLID into the ACCENT lane: 3 of 8, the kick as it was");
    tl_lane_rnd(t, 8, DRUM_ACC, 0);
    for (i = 0, n = 0, m = 0; i < 8u; i++)
        n += (uint32_t)dstep_has(&t->dstep[i], DRUM_ACC), m |= (uint32_t)dstep_has(&t->dstep[i], 0) << i;
    check(n >= 1u && m == kick, "RANDOM on the ACCENT lane: at least one accent, the kick as it was");
    m = stt_acc(t, 8);
    tl_cook_drum(t, 8, n, 1u << DRUM_ACC);
    for (i = 0, n = 0; i < 8u; i++)
        n |= (uint32_t)dstep_has(&t->dstep[i], 0) << i;
    check(n == kick, "COOK on the ACCENT lane: the kick as it was");
    tl_double_do(t, 8);
    check(trk_len(t) == 16u && stt_acc(t, 16) == (stt_acc(t, 8) | stt_acc(t, 8) << 8), "LENGTH x2: the accents copied with their steps");
    t->p[P_SLEN] = 8;
    m = stt_acc(t, 8);
    pattern_rotate(t, 1);
    check(stt_acc(t, 8) == ((m << 1 | m >> 7) & 0xFFu), "EDIT SHIFT: the accents one step later with the hits");
    memset(t->dstep, 0, sizeof t->dstep);
    t->p[P_SLEN] = 16;
}
static void seqtools_tests(void)
{
    track_t *t = &trk[0];
    uint32_t sp[NSTEP], sp2[NSTEP], n, n2, i, ok;
    /* REVERSE: the units mirrored, each note whole */
    stt_open(0, 0);
    stt_chains(t);
#if FELUCCA_COND
    (void)step_cond_set(t, 4, 3);
#endif
    n = stt_spans(t, sp);
    stt_do(TL_REV);
    n2 = stt_spans(t, sp2);
    check(n == 3u && n2 == 3u && sp[0] == 384u && sp[1] == 64u && sp[2] == 178u && sp2[0] == 178u && sp2[1] == 64u && sp2[2] == 384u,
          "REVERSE: the notes mirrored, each its exact length (3 steps, half a step, a step + a carry)");
    check(t->step[5].note[0] == 60 && t->step[3].note[0] == 64 && t->step[1].note[0] == 67 && (t->step[2].flags & SF_CARRY) &&
          t->step[2].note[0] == 67, "... N T T at 6-8, the short note at 4, the carried note at 2-3 (its carry after it)");
#if FELUCCA_COND
    check(step_cond(t, 3) == 3u && !step_cond(t, 4), "... the step's events moved with it (the condition 1:3 on step 4)");
#endif
    check(ui.toast_t && strstr(ui.msg, "REVERSED"), "... a toast says it");
    stt_undo();
    n2 = stt_spans(t, sp2);
    check(t->step[0].note[0] == 60 && t->step[1].time == ST_TIE && t->step[4].ngate[0] == 64 && n2 == 3u && sp2[0] == 384u,
          "... SAVE then HOME: undone, one level");
    /* RANDOM: the rhythm and the lengths stay, new pitches in the scale, the carry follows its note */
    stt_chains(t);
    n = stt_spans(t, sp);
    stt_do(TL_RND);
    n2 = stt_spans(t, sp2);
    ok = n2 == n;
    for (i = 0; i < n && ok; i++)
        ok = sp[i] == sp2[i];
    for (i = 0; i < 8u; i++)
        ok &= !tl_gate(&t->step[i]) || ((SCALE_MASK[6] >> (t->step[i].note[0] % 12u)) & 1u);
    check(ok && t->step[1].time == ST_TIE && t->step[6].note[0] == t->step[5].note[0] && (t->step[6].flags & SF_CARRY),
          "RANDOM: rhythm and exact lengths kept, the notes in the minor pentatonic (SCL CHR), the carry on the new pitch");
    /* COOK: changed, lengths kept, the carry follows */
    for (i = 0, ok = 1; i < 20u; i++) {
        uint32_t sig;
        stt_chains(t);
        sig = tl_sig(t);
        stt_do(TL_COOK);
        n2 = stt_spans(t, sp2);
        ok &= tl_sig(t) != sig && n2 == 3u && sp2[0] == 384u && sp2[1] == 64u && sp2[2] == 178u &&
              t->step[6].note[0] == t->step[5].note[0];
    }
    check(ok, "COOK (20 times): something changes each time, the lengths stay, the carried note follows its pitch");
    /* LENGTH: x2 copies the steps and their events, 1/2 keeps the steps past it; PATTERN FULL */
    stt_chains(t);
#if FELUCCA_PLOCK
    (void)lock_set(t, 4, P_PAN, 9);
#endif
    stt_do(TL_LEN);
    {
        int32_t v = 9;
#if FELUCCA_PLOCK
        v = lock_get(t, 12, P_PAN, &v) ? v : 0;
#endif
        check(t->p[P_SLEN] == 16 && t->step[12].ngate[0] == 64 && v == 9,
              "LENGTH x2 (DOUBLE): 16 steps, the copy whole (its own ends, its locks)");
    }
    turn(EN_K4, -1);
    stt_yes();
    check(t->p[P_SLEN] == 8 && t->step[12].note[0] == 64, "LENGTH 1/2 (HALF): 8 steps, the steps past it kept");
#if FELUCCA_PLOCK
    {   /* 64 events on steps 1..8 (their copies would not fit beside the 70 there are) */
        static const uint8_t LP[8] = {P_REV, P_DLY, P_CHOR, P_DIST, P_PAN, P_GLIDE, P_SGATE, P_DETUNE};
        uint32_t j;
        for (i = 0; i < 8u; i++)
            for (j = 0; j < 8u; j++)
                (void)lock_set(t, i, LP[j], 1);
        for (i = 16; AL(t)->n < 70u && i < NSTEP; i++)
            (void)lock_set(t, i, P_E0, 1);
    }
    n = AL(t)->n;
    turn(EN_K4, 1);
    stt_yes();
    check(t->p[P_SLEN] == 8 && AL(t)->n == n && strstr(ui.msg, "FULL"), "LENGTH x2 with too many events to copy: PATTERN FULL, nothing done");
    AL(t)->n = 0;
#endif
    /* EUCLID on a synth track: ON writes it live over the pattern, from the pattern as it was; OFF asks keep / restore */
    stt_open(0, 1);
    for (i = 0; i < NSTEP; i++)
        memset(&t->step[i], 0, sizeof t->step[i]), t->step[i].time = ST_REST;
    t->p[P_SLEN] = 16;
    stt_note(&t->step[3], 62, 0, 0);
    ng.on = 0;
    tl.hits = 4, tl.steps = 16, tl.rot = 0;
    turn(EN_K1, 1);                                     /* (OFF: a value only) */
    for (i = 0, n = 0; i < 16u; i++)
        n += tl_gate(&t->step[i]);
    check(n == 1u && !ng.on && tl.hits == 5u, "EUCLID OFF: HITS is a value, the pattern is as it was");
    turn(EN_K4, 1);                                     /* ON */
    for (i = 0, n = 0; i < 16u; i++)
        n += tl_gate(&t->step[i]);
    check(n == 5u && tl_gate(&t->step[0]) && ng.on && ng_here(t), "EUCLID ON: HITS 5 of 16 written at once (the root's note)");
    turn(EN_K1, -2);
    turn(EN_K3, 2);
    for (i = 0, n = 0, ok = 1; i < 16u; i++) {
        n += tl_gate(&t->step[i]);
        ok &= tl_gate(&t->step[i]) == eu_hit(i);
    }
    check(n == 3u && ok, "... HITS 3, ROT 2: from the pattern as it was, turned");
    turn(EN_K4, -1);                                    /* OFF asks */
    check(op_armed() && ng.on && !strcmp(ui.arm_verb, "KEEP?"), "... OFF: asks KEEP? (still ON until answered)");
    tap(B_HOME);                                        /* NO: restore */
    for (i = 0, n = 0; i < 16u; i++)
        n += tl_gate(&t->step[i]);
    check(!op_armed() && !ng.on && n == 1u && tl_gate(&t->step[3]) && t->step[3].note[0] == 62u,
          "... NO: the pattern as it was (one note on step 4)");
    turn(EN_K4, 1);
    tap(B_SAVE);                                        /* (the card picked: SAVE asks OFF) */
    check(op_armed() && ng.on, "ON, SAVE on the card: asks to turn it off");
    tap(B_SAVE);                                        /* YES: keep */
    for (i = 0, n = 0; i < 16u; i++)
        n += tl_gate(&t->step[i]);
    check(!op_armed() && !ng.on && n == 3u, "... YES: the generated steps stay (3 of 16), the generator is off");
    stt_undo();
    for (i = 0, n = 0; i < 16u; i++)
        n += tl_gate(&t->step[i]);
    check(n == 1u, "... SAVE then HOME: the generated steps undone (the pattern as it was)");
    turn(EN_K4, 1);
    t->step[5].time = ST_NOTE, t->step[5].n = 1, t->step[5].note[0] = 70;   /* (the pattern changed under it) */
    turn(EN_K1, 1);
    check(!ng.on && t->step[5].note[0] == 70u, "a pattern edited under a generator ends it: nothing rewritten");
    turn(EN_K4, 1);
    stt_open(1, 1);
    turn(EN_K4, 1);
    check(ng.on && ng.trk == 0u && !ng_here(&trk[1]) && !strncmp(ui.msg, "ANOTHER", 7), "ON on another track while one is ON: refused");
    ng.on = 0;
    /* TOOLS ask first: a turn picks the card, SAVE asks, SAVE again does it */
    stt_open(0, 0);
    t = &trk[0];
    stt_chains(t);
    turn(EN_K1, 1);
    tap(B_SAVE);
    check(op_armed() && !strcmp(ui.arm_verb, "REVERSE?") && tl_gate(&t->step[0]), "TOOLS: SAVE asks \"Reverse T1?\", nothing done yet");
    tap(B_HOME);
    check(!op_armed() && tl_gate(&t->step[0]), "... NO: nothing done");
    tap(B_SAVE);
    tap(B_SAVE);
    check(!op_armed() && !tl_gate(&t->step[0]), "... YES: reversed");
    /* the drum track: the kit, a lane */
    stt_open(TRK_DRUM, 0);
    t = TDRUM;
    t->p[P_SLEN] = 16;
    memset(t->dstep, 0, sizeof t->dstep);
    stt_do(TL_RND);
    check(dstep_has(&t->dstep[0], 0) && dstep_has(&t->dstep[4], 2) && dstep_has(&t->dstep[12], 2),
          "DR RANDOM: a beat (the kick on step 1, the snare on 2 and 4)");
    lane_select(4);
    ui.lane = 1;
    {
        uint16_t kick = 0, hat = 0, hat2 = 0;
        for (i = 0; i < 16u; i++)
            kick |= (uint16_t)(dstep_has(&t->dstep[i], 0) << i), hat |= (uint16_t)(dstep_has(&t->dstep[i], 4) << i);
        stt_do(TL_REV);
        for (i = 0, n = 0; i < 16u; i++)
            hat2 |= (uint16_t)(dstep_has(&t->dstep[i], 4) << i), n |= (uint32_t)dstep_has(&t->dstep[i], 0) << i;
        ok = 1;
        for (i = 0; i < 16u; i++)
            ok &= ((hat >> i) & 1u) == ((hat2 >> (15u - i)) & 1u);
        check(ok && n == kick && strstr(ui.msg, "CLOSED HAT"), "a lane picked (CLOSED HAT): REVERSE that lane only, the kick as it was");
    }
    stt_open(TRK_DRUM, 1);
    ui.lane = 1;
    lane_select(9);
    ng.on = 0;
    tl.hits = 2, tl.steps = 8, tl.rot = 0;
    turn(EN_K4, 1);
    turn(EN_K1, 1);
    for (i = 0, n = 0; i < 16u; i++)
        n += (uint32_t)dstep_has(&t->dstep[i], 9);
    check(n == 6u && dstep_has(&t->dstep[0], 0), "EUCLID on a lane: 3 of 8 twice in LOW TOM, the other lanes as they were");
    memset(t->dstep, 0, sizeof t->dstep);
    /* the EDIT layer's SHIFT: the steps' events with them */
    reset_ui();
    song.sel = 0;
    t = &trk[0];
    stt_chains(t);
#if FELUCCA_PLOCK
    (void)lock_set(t, 4, P_PAN, 7);
#endif
    press(B_EDIT);
    turn(EN_K1, 1);
    release(B_EDIT);
    {
        int32_t v = 7;
#if FELUCCA_PLOCK
        v = lock_get(t, 5, P_PAN, &v) && !lock_get(t, 4, P_PAN, (int32_t[]){0}) ? v : 0;
#endif
        check(t->step[5].ngate[0] == 64 && v == 7, "EDIT SHIFT: the step and its lock one later");
    }
#if FELUCCA_AUTO
    AL(t)->n = 0;
#endif
    track_defaults_steps(t);
    accent_tools_tests();
}
#endif
#if FELUCCA_COND
static void cond_card_tests(void)
{
    track_t *t = &trk[0];
    cell_t c;
    reset_ui();
    song.sel = 0;
    track_defaults_steps(t);
    tap(B_SEQ);
    key(WK(2));
    kdown(WK(2));
    frames(HOLD_FRAMES);
    turn(EN_SELECT, 1);                                 /* the EXTRA page */
    step_cell(0, EX_N - 1u, &c);
    check(c.label && !strcmp(c.label, "COND") && !strcmp(c.val, "-"), "COND: the held step's EXTRA page, KNOB 4, none: -");
    turn(EN_K1 + EX_N - 1u, 2);
    step_cell(0, EX_N - 1u, &c);
    check(step_cond(t, 2) == 2u && !strcmp(c.val, "2:2"), "... turned: 1:2, 2:2 (an event of the step)");
    turn(EN_K1 + EX_N - 1u, 20);
    check(step_cond(t, 2) == AUTO_NCOND, "... stops at 4:4");
    fm1_in.buttons |= BT(B_OCTDN);
    turn(EN_K1 + EX_N - 1u, 1);
    fm1_in.buttons &= ~BT(B_OCTDN);
    check(!step_cond(t, 2), "... HOME + KNOB 4: cleared");
    kup(WK(2));
    track_defaults_steps(t);
    AL(t)->n = 0;
}
#endif
