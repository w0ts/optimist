/* SPDX-License-Identifier: GPL-3.0-only */
/* SLOOP's step automation through the automation store (seq/auto.h), included by ui_pages_test.c; run_tests.sh builds
 * it with every automation switch (MICRO FILLS PLOCK CHANCE MOTION). SLOOP's gestures, on 64-step patterns:
 *   SEQ + a step held + PRESETS     a lock (lock_par) on it          step 0, 40, 63
 *   SEQ + a step held + KNOB 4      its nudge
 *   SEQ + a step held + OCT+        its fill condition
 *   SEQ + a step held + OCT-        its step-only events cleared
 *   SEQ + a drum step held + SELECT its chance, 5 % a detent (an event)
 *   the tiles' marks                auto_step_marks
 *   a full list (128 events)        "Pattern full: 128 events", nothing written
 *   EDIT + OCT- / OCT+              undo / redo of a hold's store edits
 *   FOLLOW                          the SEQ layer's page is the playhead's (synth too); a page key stops it */
#if FELUCCA_PLOCK && FELUCCA_MICRO && FELUCCA_FILLS && FELUCCA_CHANCE
static void sa_oct(uint32_t b) { edges_btn |= BT(b); fm1_in.buttons |= BT(b); frame(); fm1_in.buttons &= ~BT(b); frame(); }
/* the SEQ layer on track t at the page of step idx, that step held (its key down) */
static void sa_hold(uint32_t idx)
{
    static const uint8_t PAGE_KEY[4] = {1, 3, 5, 8};   /* (C#, D#, F#, G#: pages 1..4) */
    press(B_SEQ); frames(HOLD_FRAMES);
    key(PAGE_KEY[(idx / 16u) & 3u]);
    fm1_in.notes = 1u << key_of_white(idx % 16u); frame();
}
static void sa_let_go(void) { fm1_in.notes = 0; frame(); release(B_SEQ); frames(2); }
static int sa_full_msg(void) { return !strcmp(ui.msg, "Pattern full: 128 events"); }

static void sloop_auto_synth_step(uint32_t idx)
{
    track_t *t = &trk[0];
    char what[96];
    int32_t v = 0;
    lock_par = P_ED_FLT;
    sa_hold(idx);
    snprintf(what, sizeof what, "synth step %u: SEQ + its page key + the step held: the page", idx + 1u);
    check(ui.step_page == idx / 16u && step_on(&t->step[idx]), what);
    encs[panel.enc[EN_PRESET]] = 3; frame();
    snprintf(what, sizeof what, "synth step %u held + PRESETS +3: a lock event at the track's value + 3", idx + 1u);
    check(lock_get(t, idx, P_ED_FLT, &v) && v == t->p[P_ED_FLT] + 3 && auto_find(AL(t), idx | AUTO_ONLY, P_ED_FLT) >= 0,
          what);
    encs[panel.enc[EN_K4]] = 4; frame();
    snprintf(what, sizeof what, "synth step %u held + KNOB 4 +4: its nudge +4 (an AUTO_NUDGE event)", idx + 1u);
    check(step_micro(t, idx) == 4 && auto_pseudo(AL(t), idx, AUTO_NUDGE, 0) == 4, what);
    sa_oct(B_OCTUP);
    snprintf(what, sizeof what, "synth step %u held + OCT+: fill only (an AUTO_FILL event), the page kept", idx + 1u);
    check(step_fill(t, idx) == FC_FILL && ui.step_page == idx / 16u, what);
    snprintf(what, sizeof what, "synth step %u: its tile's marks from the store (dot, F)", idx + 1u);
    check(auto_step_marks(t, idx) == (AUTO_MK_ONLY | AUTO_MK_FILL), what);
    sa_oct(B_OCTDN);
    snprintf(what, sizeof what, "synth step %u held + OCT-: its lock, nudge, fill gone, the step kept", idx + 1u);
    check(!auto_step_marks(t, idx) && step_on(&t->step[idx]) && !strcmp(ui.msg, "Step automation cleared"), what);
    sa_let_go();
}

static void sloop_auto_ui_tests(void)
{
    track_t *t = &trk[0];
    uint32_t i;
    int32_t v = 0;
    song.sel = 0; song.playing = 0; go_home(); frame();
    steps_clear(t); AL(t)->n = 0; auto_touch(0);
    t->p[P_SLEN] = 64;
    sloop_auto_synth_step(0);
    sloop_auto_synth_step(40);
    sloop_auto_synth_step(63);

    /* undo: one SEQ hold's lock and nudge on step 40, undone and redone with EDIT + OCT- / OCT+ */
    steps_clear(t); AL(t)->n = 0; auto_touch(0);
    undo_close();
    sa_hold(40);
    encs[panel.enc[EN_PRESET]] = 2; frame();
    encs[panel.enc[EN_K4]] = -3; frame();
    sa_let_go();
    check(lock_get(t, 40, P_ED_FLT, &v) && step_micro(t, 40) == -3, "undo: a hold wrote a lock and a nudge on step 41");
    press(B_EDIT); frames(10);
    sa_oct(B_OCTDN);
    check(!lock_get(t, 40, P_ED_FLT, &v) && step_micro(t, 40) == 0 && !step_on(&t->step[40]),
          "undo: EDIT + OCT-: the step, its lock and its nudge gone (one level)");
    sa_oct(B_OCTUP);
    check(lock_get(t, 40, P_ED_FLT, &v) && step_micro(t, 40) == -3 && step_on(&t->step[40]),
          "undo: EDIT + OCT+: redo, the lock and the nudge back");
    release(B_EDIT); frames(2);

    /* the 128-event limit: a full list takes no lock, nudge, fill or chance, and says so */
    steps_clear(t); AL(t)->n = 0;
    for (i = 0; i < AUTO_MAX; i++)
        (void)auto_put(AL(t), i % 64u, i < 64u ? P_PAN : P_DETUNE, 1);
    auto_touch(0);
    check(AL(t)->n == AUTO_MAX, "full: 128 hold events on track 1");
    sa_hold(63);
    ui.msg[0] = 0;
    encs[panel.enc[EN_PRESET]] = 1; frame();
    check(sa_full_msg() && !lock_get(t, 63, P_ED_FLT, &v), "full: step 64 held + PRESETS: no lock, \"Pattern full: 128 events\"");
    ui.msg[0] = 0;
    encs[panel.enc[EN_K4]] = 2; frame();
    check(sa_full_msg() && step_micro(t, 63) == 0, "full: KNOB 4: no nudge, the message");
    ui.msg[0] = 0;
    sa_oct(B_OCTUP);
    check(sa_full_msg() && step_fill(t, 63) == FC_NORM, "full: OCT+: no fill, the message");
    sa_let_go();
    check(AL(t)->n == AUTO_MAX, "full: the list unchanged");
    AL(t)->n = 0; auto_touch(0);
    steps_clear(t);

    /* drums: a step held + SELECT, its chance (steps 1 and 64) */
    song.sel = TRK_DRUM; go_home(); frame();
    steps_clear(TDRUM); AL(TDRUM)->n = 0;
    TDRUM->p[P_SLEN] = 64;
    sa_hold(0);
    encs[panel.enc[EN_SELECT]] = -5; frame();
    check(step_chance_ev(TDRUM, 0) == 75u && !strcmp(ui.msg, "Chance 75%"), "drums: step 1 held + SELECT -5: chance 75 %, an event");
    encs[panel.enc[EN_SELECT]] = 10; frame();
    check(step_chance_ev(TDRUM, 0) == 100u && auto_find(AL(TDRUM), AUTO_ONLY, AUTO_CHANCE) < 0,
          "drums: SELECT +10: 100 %, the event gone");
    sa_let_go();
    sa_hold(63);
    encs[panel.enc[EN_SELECT]] = -20; frame();
    check(step_chance_ev(TDRUM, 63) == 0u && (auto_step_marks(TDRUM, 63) & AUTO_MK_ONLY),
          "drums: step 64 held + SELECT -20: never (0 %), its tile's dot");
    sa_oct(B_OCTDN);
    check(step_chance_ev(TDRUM, 63) == 100u, "drums: OCT-: its chance cleared");
    sa_let_go();
    for (i = 0; i < AUTO_MAX; i++)
        (void)auto_put(AL(TDRUM), i % 64u, i < 64u ? P_PAN : P_DETUNE, 1);
    sa_hold(5);
    ui.msg[0] = 0;
    encs[panel.enc[EN_SELECT]] = -1; frame();
    check(sa_full_msg() && step_chance_ev(TDRUM, 5) == 100u, "drums: a full list: no chance, the message");
    sa_let_go();
    AL(TDRUM)->n = 0;
    steps_clear(TDRUM); TDRUM->p[P_SLEN] = 16;

    /* motion: REC + a knob writes a hold event of the store; a full list says so */
    song.sel = 0; go_home(); frame();
    AL(t)->n = 0; auto_touch(0);
    t->p[P_SLEN] = 64;
    transport_req = 1; frames(2); song.rec = 1u;
    open_family(FAM_ENV); frames(20);
    encs[panel.enc[EN_K1]] = 2; frame();
    check(motion_count(t) == 1u && auto_count(AL(t), AUTO_ONLYS) == 0u && (auto_w.on & 1u),
          "motion: recording, ENV KNOB 1: one hold event in the store, PLAY on");
    for (i = AL(t)->n; i < AUTO_MAX; i++)
        (void)auto_put(AL(t), i % 64u, i < 64u ? P_PAN : P_DETUNE, 1);
    ui.msg[0] = 0;
    encs[panel.enc[EN_K2]] = 2; frame();
    check(sa_full_msg() && AL(t)->n == AUTO_MAX, "motion: a full list: the knob records nothing, \"Pattern full: 128 events\"");
    song.rec = 0; transport_req = 2; frames(2);
    motion_clear(t);
    AL(t)->n = 0; auto_touch(0);
    go_home(); frame();

    /* FOLLOW on a synth track of 64 steps: the SEQ layer's page is the playhead's */
    song.sel = 0; go_home(); frame();
    t->p[P_SLEN] = 64;
    song.playing = 1;
    ui.step_follow = 1;
    press(B_SEQ); frames(HOLD_FRAMES);
    for (i = 0; i < 2000u && t->seq_idx / 16u != 2u; i++)
        frame();
    check(t->seq_idx / 16u == 2u && ui.step_page == 2u, "follow: playing, SEQ held on a synth track: the playhead's page (3)");
    key(1);                                           /* (C#: page 1) */
    check(ui.step_page == 0 && !ui.step_follow, "follow: a page key: that page, FOLLOW off");
    for (i = 0; i < 2000u && t->seq_idx / 16u == 0u; i++)
        frame();
    check(ui.step_page == 0, "follow: off, the playhead elsewhere: the page stays");
    key(STEP_FOLLOW_KEY);
    check(ui.step_follow && !strcmp(ui.msg, "FOLLOW ON"), "follow: the fifth black key: FOLLOW on again");
    frame();
    check(ui.step_page == t->seq_idx / 16u, "follow: on: the playhead's page");
    sa_oct(B_OCTUP);
    check(!ui.step_follow, "follow: SEQ + OCT+ (a page): FOLLOW off");
    release(B_SEQ); frames(2);
    song.playing = 0; frame();
    t->p[P_SLEN] = 16;
    steps_clear(t);
    go_home(); frame();
}
#else
static void sloop_auto_ui_tests(void) {}
#endif
