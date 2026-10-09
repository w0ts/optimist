/* SPDX-License-Identifier: GPL-3.0-only */
/* DRUM STEP (FELUCCA_DRUM_STEP, ui_drumstep.c; behaviour of SLOOP 2.4 "Drums with the keys", isod89, GPL-3.0),
 * included by tests/ui_pages_test.c: the DRUMS grid page's keys (white = the 16 steps of KNOB 1's sound, first
 * four black = the page, heard), SELECT, the knobs heard, the SEQ layer's sound heard, FOLLOW, and the screens
 * (DIR/ds-*.ppm). */
#if FELUCCA_DRUM_STEP

static void ds_clear_drums(uint32_t len)
{
    uint32_t i;
    for (i = 0; i < NSTEP; i++)
        memset(&TDRUM->dstep[i], 0, sizeof(dstep_t));
    TDRUM->p[P_SLEN] = (int16_t)len;
    TDRUM->seq_idx = 0;
}
static uint32_t ds_count(uint32_t lane, uint32_t from, uint32_t to)
{
    uint32_t i, n = 0;
    for (i = from; i < to; i++)
        n += dstep_has(&TDRUM->dstep[i], lane) != 0;
    return n;
}
static void ds_open_grid(void)
{
    song.sel = TRK_DRUM; song.playing = 0; transport_req = 0; go_home(); frames(2);
    tap(B_SEQ); frames(2);
    drum_page = 0; ui.force = 1; frames(2);
}


/* a step key HELD on the grid (the store's switches on): the SEQ layer's held-step edits on the picked lane's step,
 * reusing steps_held_* (ui_layers.c); a tap still toggles; several keys held edit together */
#if FELUCCA_DRUM_STEP && FELUCCA_PLOCK && FELUCCA_MICRO && FELUCCA_FILLS && FELUCCA_CHANCE
static void dh_oct(uint32_t b) { edges_btn |= BT(b); fm1_in.buttons |= BT(b); frame(); fm1_in.buttons &= ~BT(b); frame(); }
static void dh_fresh(void)
{
    ds_clear_drums(64);
    AL(TDRUM)->n = 0; auto_touch(TRK_DRUM);
    ds_open_grid();
    drum_lane = 2; pen_lane = 2; drum_cursor = 0; ui.force = 1; frames(2);
}
/* the page of step idx, then its key down past HOLD_MS (a held step) */
static void dh_hold(uint32_t idx)
{
    static const uint8_t PAGE_KEY[4] = {1, 3, 5, 8};
    key(PAGE_KEY[(idx / 16u) & 3u]);
    fm1_in.notes = 1u << key_of_white(idx % 16u); frame(); frames(HOLD_FRAMES);
}
static void dh_let_go(void) { fm1_in.notes = 0; frames(2); }

static void drum_hold_one(uint32_t idx)
{
    char what[112];
    int32_t v = 0;
    uint32_t w = idx % 16u, first_par;
    dh_fresh();
    dh_hold(idx);
    snprintf(what, sizeof what, "grid step %u held: set at the press, a held step on its page", idx + 1u);
    check(dstep_has(&TDRUM->dstep[idx], 2) && ((gh_held >> w) & 1u) && ((ui.step_held >> w) & 1u) && ui.step_page == idx / 16u, what);
    first_par = lock_par;
    encs[panel.enc[EN_ALGO]] = 1; frame();
    snprintf(what, sizeof what, "grid step %u held + ALGORITHM: the lock parameter moves (the lane is kept)", idx + 1u);
    check(lock_par != first_par && lock_ok(TDRUM, lock_par) && drum_lane == 2 && song.sel == TRK_DRUM, what);
    encs[panel.enc[EN_PRESET]] = 3; frame();
    snprintf(what, sizeof what, "grid step %u held + PRESETS +3: a lock event on the step (the kit stays)", idx + 1u);
    check(lock_get(TDRUM, idx, lock_par, &v) && v == TDRUM->p[lock_par] + 3 && auto_find(AL(TDRUM), idx | AUTO_ONLY, mot_sid(lock_par)) >= 0, what);
    encs[panel.enc[EN_K4]] = 4; frame();
    snprintf(what, sizeof what, "grid step %u held + KNOB 4 +4: its nudge (an AUTO_NUDGE event)", idx + 1u);
    check(step_micro(TDRUM, idx) == 4 && auto_pseudo(AL(TDRUM), idx, AUTO_NUDGE, 0) == 4, what);
    encs[panel.enc[EN_K2]] = -3; frame();
    snprintf(what, sizeof what, "grid step %u held + KNOB 2 -3: its chance 85 %% (an event, every lane)", idx + 1u);
    check(step_chance_ev(TDRUM, idx) == 85u && !strcmp(ui.msg, "Chance 85%"), what);
    encs[panel.enc[EN_K3]] = 2; frame();
    snprintf(what, sizeof what, "grid step %u held + KNOB 3 +2: the lane's ratchet x3", idx + 1u);
    check(dstep_rat(&TDRUM->dstep[idx], 2) == 2u, what);
    dh_oct(B_OCTUP);
    snprintf(what, sizeof what, "grid step %u held + OCT+: fill only; its marks: an event and the F", idx + 1u);
    check(step_fill(TDRUM, idx) == FC_FILL && auto_step_marks(TDRUM, idx) == (AUTO_MK_ONLY | AUTO_MK_FILL), what);
    ui.force = 1; frames(2);
    if (idx == 0u)
        ppm("ds-7-grid-held-step");
    dh_oct(B_OCTDN);
    snprintf(what, sizeof what, "grid step %u held + OCT-: lock, nudge, chance, fill gone, the hit kept", idx + 1u);
    check(!auto_step_marks(TDRUM, idx) && step_chance_ev(TDRUM, idx) == 100u && dstep_has(&TDRUM->dstep[idx], 2) &&
          !strcmp(ui.msg, "Step automation cleared"), what);
    dh_let_go();
    snprintf(what, sizeof what, "grid step %u: let go after edits: the step stays, nothing held", idx + 1u);
    check(dstep_has(&TDRUM->dstep[idx], 2) && !gh_held && !gh_down && !ui.step_held, what);
}

static void drum_hold_tests(void)
{
    int32_t v = 0;
    uint32_t i;
    drum_hold_one(0);
    drum_hold_one(63);

    /* a tap against a hold */
    dh_fresh();
    key(key_of_white(5));
    check(dstep_has(&TDRUM->dstep[5], 2) && !gh_held, "tap on an empty step: set");
    fm1_in.notes = 1u << key_of_white(5); frames(10); fm1_in.notes = 0; frames(2);
    check(!dstep_has(&TDRUM->dstep[5], 2) && !gh_held, "a short press (160 ms) on a set step: a tap, cleared when let go");
    key(key_of_white(5));
    fm1_in.notes = 1u << key_of_white(5); frames(HOLD_FRAMES); 
    check(dstep_has(&TDRUM->dstep[5], 2) && ((gh_held >> 5) & 1u), "a set step held past HOLD_MS: a held step, still set");
    fm1_in.notes = 0; frames(2);
    check(dstep_has(&TDRUM->dstep[5], 2) && !gh_held, "let go with no edit: the step stays (a hold is not a tap)");
    key(key_of_white(5));
    check(!dstep_has(&TDRUM->dstep[5], 2), "and a tap clears it as before");

    /* several keys held edit together */
    dh_fresh();
    key(key_of_white(1)); key(key_of_white(2));
    fm1_in.notes = 1u << key_of_white(1); frame();
    fm1_in.notes |= 1u << key_of_white(2); frames(HOLD_FRAMES);
    check(gh_held == 6u, "two keys held: two held steps");
    encs[panel.enc[EN_K4]] = 2; frame();
    check(step_micro(TDRUM, 1) == 2 && step_micro(TDRUM, 2) == 2, "KNOB 4: both steps nudged together");
    encs[panel.enc[EN_K2]] = -2; frame();
    check(step_chance_ev(TDRUM, 1) == 90u && step_chance_ev(TDRUM, 2) == 90u, "KNOB 2: both take the chance");
    dh_let_go();

    /* the other lane's step keeps no edit of the picked lane's; a held step edits the picked lane's hit only */
    dh_fresh();
    dstep_set(&TDRUM->dstep[3], 5, LV_NORM, 0);
    dh_hold(3);
    encs[panel.enc[EN_K3]] = 2; frame();
    check(dstep_has(&TDRUM->dstep[3], 2) && dstep_rat(&TDRUM->dstep[3], 2) == 2u && dstep_rat(&TDRUM->dstep[3], 5) == 0,
          "KNOB 3: the picked lane's ratchet only");
    dh_let_go();

    /* OCT- clears recorded motion too: motion is a plock, one storage */
    dh_fresh();
    dstep_set(&TDRUM->dstep[7], 2, LV_NORM, 0);
    (void)auto_put(AL(TDRUM), 7, P_PAN, 5);                      /* a hold event (recorded motion) */
    check(motion_count(TDRUM) == 1u && auto_step_marks(TDRUM, 7) == AUTO_MK_ONLY, "motion on step 8: its mark is the one dot");
    dh_hold(7);
    encs[panel.enc[EN_K4]] = 1; frame();
    dh_oct(B_OCTDN);
    check(motion_count(TDRUM) == 0u && !auto_step_marks(TDRUM, 7) && step_micro(TDRUM, 7) == 0, "held + OCT-: the motion goes with the rest");
    dh_let_go();

    /* undo: one hold's step, lock and nudge are one level (EDIT + OCT-), redo brings them back */
    dh_fresh();
    undo_close();
    dh_hold(40);
    encs[panel.enc[EN_PRESET]] = 2; frame();
    encs[panel.enc[EN_K4]] = -3; frame();
    dh_let_go();
    check(dstep_has(&TDRUM->dstep[40], 2) && lock_get(TDRUM, 40, lock_par, &v) && step_micro(TDRUM, 40) == -3, "undo: a hold wrote a step, a lock and a nudge on step 41");
    press(B_EDIT); frames(10);
    dh_oct(B_OCTDN);
    check(!dstep_has(&TDRUM->dstep[40], 2) && !lock_get(TDRUM, 40, lock_par, &v) && step_micro(TDRUM, 40) == 0, "undo: EDIT + OCT-: all three gone, one level");
    dh_oct(B_OCTUP);
    check(dstep_has(&TDRUM->dstep[40], 2) && lock_get(TDRUM, 40, lock_par, &v) && step_micro(TDRUM, 40) == -3, "undo: EDIT + OCT+: redo, back");
    release(B_EDIT); frames(2);

    /* the grid shows them: marks under the cells, the held column */
    dh_fresh();
    dstep_set(&TDRUM->dstep[4], 2, LV_NORM, 0);
    check(step_micro_set(TDRUM, 4, 3), "a nudge on step 5");
    ui.force = 1; frames(2);
    ppm("ds-8-grid-marks");
    ds_clear_drums(16);
    AL(TDRUM)->n = 0; auto_touch(TRK_DRUM);
    (void)v; (void)i;
    go_home(); frames(2);
}
#elif FELUCCA_DRUM_STEP && !FELUCCA_AUTO
/* the default build: DRUM_STEP on (user-default since this batch), the store's switches off: a tap toggles, a long press is
 * no held step (nothing to edit it with), the picked lane's step is as the 2.4 grid had it */
static void drum_hold_tests(void)
{
    ds_clear_drums(64);
    ds_open_grid();
    drum_lane = 2; pen_lane = 2; drum_cursor = 0; ui.force = 1; frames(2);
    key(key_of_white(3));
    check(dstep_has(&TDRUM->dstep[3], 2), "default build (DRUM_STEP on, store off): a tap sets the step");
    fm1_in.notes = 1u << key_of_white(3); frames(HOLD_FRAMES);
    check(!gh_held && !ui.step_held && dstep_has(&TDRUM->dstep[3], 2), "a long press: no held step (the store is off), the step stays while down");
    fm1_in.notes = 0; frames(2);
    check(!dstep_has(&TDRUM->dstep[3], 2) && !gh_held, "let go: the set step is cleared, as a tap");
    encs[panel.enc[EN_K4]] = 2; frame();
    check(drum_cursor == 3 && !gh_held, "knobs as before: KNOB 4 is the level of the cursor's step");
    ds_clear_drums(16);
    go_home(); frames(2);
}
#else
static void drum_hold_tests(void) {}
#endif

static void drum_step_tests(void)
{
    uint32_t w, ok;
    /* the pure mapping */
    check(ds_pages(1) == 1 && ds_pages(16) == 1 && ds_pages(17) == 2 && ds_pages(64) == 4, "DRUM STEP: pages of 16 cover the pattern (1, 16, 17, 64)");
    check(ds_follow_page(0, 64) == 0 && ds_follow_page(15, 64) == 0 && ds_follow_page(16, 64) == 1 && ds_follow_page(63, 64) == 3 &&
          ds_follow_page(40, 20) == 1, "DRUM STEP: the page of the playhead");
    check(ds_page_key(1, 64) == 0 && ds_page_key(3, 64) == 1 && ds_page_key(5, 64) == 2 && ds_page_key(8, 64) == 3 &&
          ds_page_key(8, 49) == 3 && ds_page_key(8, 48) == -1 && ds_page_key(0, 64) == -1 && ds_page_key(10, 64) == -1,
          "DRUM STEP: the first four black keys open pages 1..4, not past the length, no other key");
    check(ds_step_of(0, 15, 64) == 15 && ds_step_of(3, 15, 64) == 63 && ds_step_of(1, 5, 20) == -1 && ds_step_of(1, 3, 20) == 19,
          "DRUM STEP: white key w of page p = step 16 p + w, none beyond the length");

    /* the DRUMS grid page: the keys are steps */
    ds_clear_drums(64);
    ds_open_grid();
    check(on_drum_page() && !drum_page && grid_keys_on(), "DRUMS grid page: its keys are steps");
    drum_lane = 2; drum_cursor = 0; ui.force = 1; frames(2);
    drums.hits = 0;
    for (w = 0, ok = 1; w < 16u; w += 3u) {
        key(key_of_white(w));
        ok &= dstep_has(&TDRUM->dstep[w], 2) != 0 && drum_cursor == w;
    }
    check(ok && ds_count(2, 0, 16) == 6 && ds_count(2, 16, 64) == 0, "white keys 1, 4, 7 ..: steps of the snare (KNOB 1's sound), page 1, the cursor follows");
    check(kb_kind[key_of_white(0)] == KS_NONE && ds_count(0, 0, 64) == 0, "a step key plays no pad and sets no other sound");
    key(key_of_white(3));
    check(!dstep_has(&TDRUM->dstep[3], 2) && dstep_has(&TDRUM->dstep[0], 2), "a set step pressed again: cleared");
    {   /* the sound heard when a key sets a step */
        aud_lanes = 0;
        fm1_in.notes = 1u << key_of_white(10); frame();
        check(dstep_has(&TDRUM->dstep[10], 2) && aud_lanes == (1u << 2), "a key sets a step: the sound is heard (audition)");
        fm1_in.notes = 0; frames(2);
        aud_lanes = 0;
        fm1_in.notes = 1u << key_of_white(10); frame();
        check(dstep_has(&TDRUM->dstep[10], 2) && aud_lanes == 0, "a key on a set step: it stays while the key is down");
        fm1_in.notes = 0; frames(2);
        check(!dstep_has(&TDRUM->dstep[10], 2) && aud_lanes == 0, "a tap on a set step: cleared when let go, silent");
    }
    key(3);                                                     /* black key 2: page 2 */
    check(drum_cursor / 16u == 1, "black key 2: page 2");
    key(key_of_white(0)); key(key_of_white(15));
    check(dstep_has(&TDRUM->dstep[16], 2) && dstep_has(&TDRUM->dstep[31], 2), "page 2: white keys 1 / 16 are steps 17 / 32");
    key(8); key(key_of_white(15));
    check(drum_cursor / 16u == 3 && dstep_has(&TDRUM->dstep[63], 2), "black key 4, white key 16: step 64");
    TDRUM->p[P_SLEN] = 20; drum_cursor = 0; frames(2);
    key(8); key(5);
    check(drum_cursor / 16u == 0, "length 20: the page 3 / 4 keys open nothing");
    key(3); key(key_of_white(4)); key(key_of_white(5));
    check(!dstep_has(&TDRUM->dstep[20], 2) && !dstep_has(&TDRUM->dstep[21], 2), "length 20, page 2: white keys 5.. set nothing");
    TDRUM->p[P_SLEN] = 64; drum_cursor = 0; ui.force = 1; frames(2);

    /* the knobs, heard */
    aud_lanes = 0;
    encs[panel.enc[EN_K1]] = 2; frame();
    check(drum_lane == 4 && aud_lanes == (1u << 4), "KNOB 1: the sound, heard");
    encs[panel.enc[EN_K1]] = 0; aud_lanes = 0;
    dstep_set(&TDRUM->dstep[1], 0, LV_HARD, 0); dstep_set(&TDRUM->dstep[1], 5, LV_NORM, 0);
    drum_cursor = 0; encs[panel.enc[EN_K2]] = 1; frame();
    check(drum_cursor == 1 && aud_lanes == ((1u << 0) | (1u << 5)), "KNOB 2: the step, the sounds it holds heard");
    aud_lanes = 0;

    /* SELECT: grid <-> kit */
    encs[panel.enc[EN_SELECT]] = 1; frames(2);
    check(drum_page == 1 && !grid_keys_on(), "SELECT right: the kit page (its keys play the pads)");
    key(key_of_white(0));
    check(kb_kind[key_of_white(0)] == KS_NONE && ds_count(4, 0, 64) == 0, "kit page: a key sets no step");
    encs[panel.enc[EN_SELECT]] = -1; frames(2);
    check(drum_page == 0, "SELECT left: the grid again");

    /* FOLLOW on the grid page */
    ds_clear_drums(64);
    drum_cursor = 5; song.playing = 1; TDRUM->seq_idx = 37; ui.step_follow = 1; frames(3);
    check(drum_cursor / 16u == ds_follow_page(TDRUM->seq_idx, 64) && drum_cursor % 16u == 5, "playing: the page follows the playhead, in the same column");
    key(1);
    check(drum_cursor / 16u == 0 && !ui.step_follow, "a page key while playing: that page, follow off");
    frames(3);
    check(drum_cursor / 16u == 0, "follow off: the page stays");
    key(DS_FOLLOW_KEY);
    check(ui.step_follow, "black key 5: follow on");
    key(DS_FOLLOW_KEY);
    check(!ui.step_follow, "black key 5 again: off");
    song.playing = 0; frames(3);
    check(ui.step_follow, "stopped: follow armed for the next start");

    /* the SEQ layer (held on the grid page): KNOB 1 and the white keys pick the lane, heard when stopped */
    ds_clear_drums(64);
    song.sel = TRK_DRUM; go_home(); frames(2);
    pen_lane = 0; drum_lane = 0;
    press(B_SEQ); frames(10);
    aud_lanes = 0;
    encs[panel.enc[EN_K1]] = 3; frame();
    check(pen_lane == 3 && drum_lane == 3 && aud_lanes == (1u << 3), "SEQ layer + KNOB 1: the sound (the grid's too), heard");
    frames(3); ui.force = 1; frames(2); ppm("ds-6-seq-lanes");
    aud_lanes = 0; drums.hits = 0;
    fm1_in.notes = 1u << key_of_white(6); frame();
    check(pen_lane == 6 && drum_lane == 6 && aud_lanes == (1u << 6), "SEQ + white key 7: lane 7 picked, heard (stopped)");
    check(ds_count(6, 0, 64) == 0 && drums.hits == 0 && kb_kind[key_of_white(6)] == KS_UI, "SEQ + key: no step set, no pad played");
    fm1_in.notes = 0; frames(2);
    aud_lanes = 0;
    fm1_in.notes = 1u << key_of_white(6); frame();
    check(pen_lane == 6 && aud_lanes == (1u << 6), "SEQ + the same key again: heard again");
    fm1_in.notes = 0; frames(2);
    key(3);
    check(ui.step_page == 1, "SEQ + black key 2: page 2");
    song.playing = 1; TDRUM->seq_idx = 50; ui.step_follow = 1; frames(3);
    check(ui.step_page == 3, "SEQ layer, playing: the page follows the playhead");
    key(1); frames(3);
    check(ui.step_page == 0 && !ui.step_follow, "SEQ layer: a page key turns follow off");
    aud_lanes = 0; drums.hits = 0;
    fm1_in.notes = 1u << key_of_white(9); frame();
    check(pen_lane == 9 && drum_lane == 9 && aud_lanes == 0 && drums.hits == 0, "playing: SEQ + key picks lane 10 and stays silent");
    fm1_in.notes = 0; frames(2);
    encs[panel.enc[EN_K1]] = -2; frame();
    check(pen_lane == 7 && aud_lanes == 0, "playing: SEQ + KNOB 1 picks silently");
    release(B_SEQ);
    song.playing = 0; frames(2);

    /* the grid: the preview rule and ALGORITHM */
    ds_clear_drums(64);
    ds_open_grid();
    drum_lane = 2; pen_lane = 2; drum_cursor = 0; ui.force = 1; frames(2);
    song.playing = 1; aud_lanes = 0; ui.step_follow = 0;
    encs[panel.enc[EN_K1]] = 2; frame();
    check(drum_lane == 4 && pen_lane == 4 && aud_lanes == 0, "playing: grid KNOB 1 picks silently");
    key(key_of_white(3));
    check(dstep_has(&TDRUM->dstep[3], 4) && aud_lanes == 0, "playing: a step key sets the step, silent");
    encs[panel.enc[EN_K2]] = 0;
    dstep_set(&TDRUM->dstep[1], 0, LV_NORM, 0);
    drum_cursor = 0; encs[panel.enc[EN_K2]] = 1; frame();
    check(drum_cursor == 1 && aud_lanes == 0, "playing: grid KNOB 2 (step) silent");
    song.playing = 0; frames(2);
    drum_lane = 2; pen_lane = 2; drum_cursor = 0; aud_lanes = 0;
    encs[panel.enc[EN_ALGO]] = 2; frame();
    check(drum_lane == 4 && pen_lane == 4 && aud_lanes == (1u << 4) && song.sel == TRK_DRUM, "grid ALGORITHM: walks the lanes, heard (stopped)");
    frames(30);
    encs[panel.enc[EN_ALGO]] = -1; frame();
    encs[panel.enc[EN_ALGO]] = -1; frame();
    encs[panel.enc[EN_ALGO]] = -1; frame();
    encs[panel.enc[EN_ALGO]] = -1; frame();
    encs[panel.enc[EN_ALGO]] = -1; frame();
    check(drum_lane == 0 && song.sel == TRK_DRUM && on_drum_page(), "ALGORITHM up: stops at the kick");
    encs[panel.enc[EN_ALGO]] = -1; frame();
    check(drum_lane == 0 && song.sel == TRK_DRUM, "the turn going on: still at the kick");
    frames(30);
    encs[panel.enc[EN_ALGO]] = -1; frame();
    check(song.sel == TRK_DRUM - 1 && !on_drum_page(), "a fresh turn after the stop: leaves for T3");
    ds_open_grid();
    drum_lane = 15; frames(30);
    encs[panel.enc[EN_ALGO]] = 3; frame();
    check(drum_lane == 15 && song.sel == TRK_DRUM, "ALGORITHM down at the last lane: stays");

    /* KNOB 3: set, ratchet, clear */
    ds_clear_drums(64);
    drum_lane = 1; drum_cursor = 0; ui.force = 1; frames(2);
    encs[panel.enc[EN_K3]] = 1; frame();
    check(dstep_has(&TDRUM->dstep[0], 1) && dstep_rat(&TDRUM->dstep[0], 1) == 0, "KNOB 3 right: sets the step");
    encs[panel.enc[EN_K3]] = 1; frame(); encs[panel.enc[EN_K3]] = 1; frame();
    check(dstep_rat(&TDRUM->dstep[0], 1) == 2, "KNOB 3 right again: ratchet x3");
    encs[panel.enc[EN_K3]] = 5; frame();
    check(dstep_rat(&TDRUM->dstep[0], 1) == 3, "ratchet stops at x4");
    encs[panel.enc[EN_K3]] = -1; frame();
    check(dstep_rat(&TDRUM->dstep[0], 1) == 2 && dstep_has(&TDRUM->dstep[0], 1), "KNOB 3 left: ratchet down");
    encs[panel.enc[EN_K3]] = -1; frame(); encs[panel.enc[EN_K3]] = -1; frame(); encs[panel.enc[EN_K3]] = -1; frame();
    check(!dstep_has(&TDRUM->dstep[0], 1), "KNOB 3 left to the end: the step is cleared");

    /* a synth track keeps everything */
    song.sel = 0; go_home(); frames(2);
    steps_clear(&trk[0]); trk[0].p[P_SLEN] = 16;
    press(B_SEQ); frames(6);
    key(key_of_white(1));
    check(step_on(&trk[0].step[1]), "a synth track: SEQ + key sets its step as before");
    release(B_SEQ);
    steps_clear(&trk[0]);

    /* the screens, on a groove of 64 steps (DIR/ds-*.ppm) */
    {
        uint32_t i;
        ds_clear_drums(64);
        for (i = 0; i < 64u; i++) {
            dstep_t *d = &TDRUM->dstep[i];
            if (i % 4u == 0u) dstep_set(d, 0, i % 16u ? LV_NORM : LV_HARD, 0);
            if (i % 8u == 4u) dstep_set(d, 2, LV_NORM, i % 16u == 12u ? 2u : 0u);
            if (i % 2u == 0u) dstep_set(d, 4, i % 4u ? LV_SOFT : LV_NORM, 0);
            if (i % 16u == 14u) dstep_set(d, 5, LV_NORM, 0);
            if (i % 16u == 10u) dstep_set(d, 3, LV_GHOST, 0);
        }
        ds_open_grid();
        drum_lane = 2; drum_cursor = 4; ui.force = 1; frames(2); ppm("ds-1-grid-page1");
        key(3); ui.force = 1; frames(2); ppm("ds-2-grid-page2");
        song.playing = 1; TDRUM->seq_idx = 37; ui.step_follow = 1; frames(4); ui.force = 1; frames(2); ppm("ds-3-grid-follow-playing");
        song.playing = 0; frames(2);
        TDRUM->p[P_SLEN] = 24; drum_cursor = 18; ui.force = 1; frames(2); ppm("ds-4-grid-length-24-page2");
        TDRUM->p[P_SLEN] = 64;
        go_home(); frames(2);
        pen_lane = 2; ui.step_page = 0;
        press(B_SEQ); frames(10); ui.force = 1; frames(2); ppm("ds-5-seq-layer");
        release(B_SEQ);
        ds_clear_drums(16);
        drum_cursor = 0;
    }
    go_home(); frames(2);
    drum_hold_tests();
    go_home(); frames(2);
}
#else
static void drum_step_tests(void) {}
#endif
