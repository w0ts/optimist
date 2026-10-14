/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, the drum track's ACCENT lane on the Optimist UI (core.h DRUM_ACC, lane 16: the core of
 * feat/drum-accent; the user's ruling: "it should work like any other drum track, it is a drum track. just no audio
 * output, no synth engine. but everything else is the same"):
 *   the last upper key  (key 25) picks the lane (HOME + the key, on STEP too; the key played) and plays it silently;
 *                     SEQ + it on STEP: nothing (the keys there are steps); upper key 1 is silent, as the other upper keys
 *   STEP              a 17th row of the grid, the steps edited as any lane's; the held step: LEVEL RATCHET ACCENT
 *   the ACCENT amount the SOUND row's fourth card on every lane, the lane's STEP page, the mixer's ACCENT row; a lock
 *   ALGORITHM         the walk goes on past the 16th lane to the ACCENT lane
 *   the lane's pages  no SOUND, SOURCE, KIT, ENV or LFO pages (it has no sound); never past the 16 sounds' arrays
 * Included after the helpers (WK, kdown, kup). */
#if ACC_UI
static void acc_pick(void)                              /* the accent lane selected on the drum track, SOUND up */
{
    reset_ui();
    lane_transport(0);
    song.sel = TRK_DRUM;
    lane_select(0);
    ui.lane = 0;
    op_enter(SCR_SOUND);
    snd_family(SND_ALL);
    ui.row[SCR_SOUND] = 0;
    frames(2);
}
static void acc_key_tests(void)
{
    uint32_t i, ok = 1;
    acc_pick();
    drums.hits = 0;
    aud_lanes = 0;
    press(B_HOME);
    kdown(DACC_KEY);
    kup(DACC_KEY);
    release(B_HOME);
    check(lane_selected() == DRUM_ACC && ui.lane == 1u && lane_picked() && drums.hits == 0u && aud_lanes == 0u,
          "SOUND, HOME held + the last upper key: the ACCENT lane picked, silent");
    check(!strcmp(head_engine(), "ACCENT"), "... the header's badge: ACCENT");
    lane_select(3);
    for (i = 0; i < 4u; i++)
        ui.enc_t[EN_K1 + i] = 0;                        /* (no knob turned lately: drum_hit_pick waits 300 ms after one) */
    drums.hits = 0;
    kdown(DACC_KEY);
    check(lane_selected() == DRUM_ACC && kb_kind[DACC_KEY] == KS_DRUM && drums.hits == 0u,
          "SOUND, the last upper key played: the lane picked (the keys play), no sound");
    kup(DACC_KEY);
    check(kb_kind[DACC_KEY] == KS_NONE, "... and let go");
    lane_select(3);
    op_enter(SCR_STEP);
    frames(2);
    press(B_HOME);
    kdown(DACC_KEY);
    kup(DACC_KEY);
    release(B_HOME);
    check(lane_selected() == DRUM_ACC && drums.hits == 0u && aud_lanes == 0u && ui.scr == SCR_STEP,
          "STEP, HOME held + the last upper key: the ACCENT lane picked, silent");
    for (i = 0; i < 16u; i++) {                         /* a white key still picks its lane, the last upper key only the accent */
        lane_select(DRUM_ACC);
        press(B_HOME);
        kdown(WK(i));
        kup(WK(i));
        release(B_HOME);
        ok &= lane_selected() == i;
    }
    check(ok, "... HOME + a white key picks its sound");
    lane_select(3);
    memset(TDRUM->dstep, 0, sizeof TDRUM->dstep);
    dstep_set(&TDRUM->dstep[0], DRUM_ACC, LV_NORM, 0);
    press(B_SEQ);
    kdown(DACC_KEY);
    kup(DACC_KEY);
    release(B_SEQ);
    check(lane_selected() == 3u && dstep_has(&TDRUM->dstep[0], DRUM_ACC),
          "STEP, SEQ held + the last upper key: nothing (no lane picked, no step cleared)");
    memset(TDRUM->dstep, 0, sizeof TDRUM->dstep);
    frames(2);
    /* the keys while recording: a hit of the accent lane into the step playing */
    reset_ui();
    song.sel = TRK_DRUM;
    memset(TDRUM->dstep, 0, sizeof TDRUM->dstep);
    op_enter(SCR_SOUND);
    lane_transport(1);
    song.rec = 1u << TRK_DRUM;
    kdown(DACC_KEY);
    frames(2);
    kup(DACC_KEY);
    for (i = 0, ok = 0; i < NSTEP; i++)
        ok |= (uint32_t)dstep_has(&TDRUM->dstep[i], DRUM_ACC);
    check(ok, "REC playing, the last upper key: an accent recorded");
    song.rec = 0;
    lane_transport(0);
    memset(TDRUM->dstep, 0, sizeof TDRUM->dstep);
    reset_ui();
}
static void acc_layers_tests(void)                      /* an upper key's meaning in a held layer is that layer's */
{
    static const uint8_t BTN[] = {B_GLO,
#if FELUCCA_PATTERNS
                                  B_LFO,
#endif
                                  B_EDIT};
    uint32_t i, ok = 1;
    for (i = 0; i < sizeof BTN; i++) {
        acc_pick();
        lane_select(2);
        press(BTN[i]);
        frames(HOLD_FRAMES);
        kdown(DACC_KEY);
        ok &= lane_selected() == 2u && kb_kind[DACC_KEY] != KS_DRUM;
        kup(DACC_KEY);
        release(BTN[i]);
        frames(2);
    }
    check(ok, "a layer held (GLO, LFO with the patterns, EDIT), the last upper key: the layer's key, no accent picked or played");
    acc_pick();
    go_home();
    frame();
    lane_select(2);
    smute_trk = 0;
    press(B_HOME);
    kdown(1);
    kup(1);
    release(B_HOME);
    check(smute_trk_on(0) && lane_selected() == 2u && !smute_lane_on(DRUM_ACC), "the mixer, HOME held + upper key 1: T1's sequencer mute, as ever");
    smute_trk = 0;
    smute_lane = 0;
    press(B_HOME);
    frames(HOLD_FRAMES);
    kdown(DACC_KEY);
    kup(DACC_KEY);
    check(smute_lane_on(DRUM_ACC) && smute_trk == 0u && !smute_lane_on(2) && lane_selected() == 2u && drums.hits == 0u,
          "the mixer, HOME held + the last upper key: the ACCENT lane's sequencer mute on (nothing else, no lane picked)");
    {
        uint32_t f = 0, d = 0;
        keys_lit(&f, &d);
        check((f >> DACC_KEY & 1u) && (d >> DACC_KEY & 1u), "... its LED full (muted), the key in the dim set too");
    }
    kdown(DACC_KEY);
    kup(DACC_KEY);
    {
        uint32_t f = 0, d = 0;
        keys_lit(&f, &d);
        check(!smute_lane_on(DRUM_ACC) && !(f >> DACC_KEY & 1u) && (d >> DACC_KEY & 1u), "... the key again: off, its LED dim");
    }
    release(B_HOME);
    smute_lane = 0;
    reset_ui();
}
static void acc_sound_tests(void)
{
    uint32_t r, n, bad = 0, ok = 1, f;
    cell_t c;
    acc_pick();
    check(snd_rows() > 1u, "(sound row: several rows on the drum track)");
    n = snd_rows();
    lane_select(DRUM_ACC);
    ui.lane = 1;
    snd_family(SND_ALL);
    ui.row[SCR_SOUND] = 0;
    frame();
    for (r = 0; r < snd_rows(); r++)
        bad += snd_page(r) && snd_page(r)->scope == SC_DSND;
    check(!bad, "the ACCENT lane selected: no lane page (SOUND, SOURCE, KIT, ENV, LFO) among SOUND's rows");
    check(snd_rows() < n && snd_rows() >= 1u, "... but the SOUND row is there");
    SCR->cell(0, 0, &c);
    ok = c.label && !strcmp(c.label, "KIT");
    SCR->cell(0, 1, &c);
    ok &= !c.label && c.kind == CK_NONE;               /* (no LANE card any more) */
    SCR->cell(0, 2, &c);
    ok &= !c.label || !c.label[0];
    SCR->cell(0, 3, &c);
    ok &= c.label && !strcmp(c.label, "ACCENT") && c.vp == &TDRUM->p[P_DACC] && c.d == &DRUM_ACC_DESC;
    check(ok, "the SOUND row: KIT, no second card, no SRC, the ACCENT amount");
    TDRUM->p[P_DACC] = 27;
    turn(EN_K4, 5);
    check(TDRUM->p[P_DACC] > 27, "KNOB 4: the ACCENT amount up");
    turn(EN_K3, 5);
    check(TDRUM->p[P_DACC] > 27, "KNOB 3 (no SRC): nothing");
    press(B_HOME);
    turn(EN_K4, 1);
    release(B_HOME);
    check(TDRUM->p[P_DACC] == DACC_DEF, "HOME + KNOB 4: back to the default");
    TDRUM->p[P_DACC] = 120;
    turn(EN_K4, 40);
    check(TDRUM->p[P_DACC] == DACC_MAX, "... stops at 127");
    TDRUM->p[P_DACC] = 5;
    turn(EN_K4, -40);
    check(TDRUM->p[P_DACC] == 0, "... and at 0");
    TDRUM->p[P_DACC] = DACC_DEF;
    lane_select(4);
    ui.lane = 1;
    frame();
    SCR->cell(0, 3, &c);
    check(c.label && !strcmp(c.label, "ACCENT") && c.vp == &TDRUM->p[P_DACC], "a sound lane: the ACCENT card there too (the track's own)");
    SCR->cell(0, 2, &c);
    check(c.label && !strcmp(c.label, "SRC"), "... next to its SRC");
    lane_select(DRUM_ACC);
    ui.lane = 1;
    for (f = 0; f < 3u; f++) {                          /* ENV, LFO, EDIT: the families with no rows here */
        static const uint8_t BTN[3] = {B_ENV, B_LFO, B_EDIT};
        tap(BTN[f]);
        frames(2);
        ok &= ui.scr == SCR_SOUND && snd_rows() >= 1u && lane_selected() == DRUM_ACC;
        for (r = 0; r < snd_rows(); r++) {
            uint32_t k;
            for (k = 0; k < 4u; k++)
                SCR->cell(r, k, &c);                    /* (every cell of every row: none past the sounds) */
        }
    }
    check(ok, "ENV, LFO, EDIT on the ACCENT lane: SOUND stays, no empty page, every cell readable");
    turn(EN_PRESET, 1);
    frames(2);
    check(lane_selected() == DRUM_ACC, "PRESETS on the ACCENT lane: the kit as on DR, the lane stays");
    reset_ui();
}
static void acc_step_tests(void)
{
    track_t *t = TDRUM;
    uint32_t y, ok;
    cell_t c;
    reset_ui();
    lane_transport(0);
    song.sel = TRK_DRUM;
    track_defaults_steps(t);
    memset(t->dstep, 0, sizeof t->dstep);
    AL(t)->n = 0;
    lane_select(DRUM_ACC);
    ui.lane = 1;
    op_enter(SCR_STEP);
    frames(2);
    key(WK(3));
    check(dstep_has(&t->dstep[3], DRUM_ACC) && dstep_lanes(&t->dstep[3]) == 1u << DRUM_ACC && !dstep_mask(&t->dstep[3]),
          "STEP, the ACCENT lane: an empty step tapped sets the accent (no sound lane touched)");
    ui.force = 1;
    frames(2);
    y = SP_Y + (uint32_t)SG_LY(DRUM_ACC) + 2u;
    check(px_in((uint32_t)(SG_X + 3 * SG_CW), y, SG_CW - 2, 6, lvl_col(C_HI, LV_NORM)), "the grid's 17th row: the accent drawn in its colour");
    check(!px_in((uint32_t)(SG_X + 4 * SG_CW), y, SG_CW - 2, 6, lvl_col(C_HI, LV_NORM)), "... and only there");
    ppm("opt-step-accent");
    key(WK(3));
    check(!dstep_has(&t->dstep[3], DRUM_ACC), "... tapped again: cleared");
    lane_select(2);
    key(WK(5));
    lane_select(DRUM_ACC);
    key(WK(5));
    check(dstep_has(&t->dstep[5], 2) && dstep_has(&t->dstep[5], DRUM_ACC), "a step with a hit and an accent: both");
    press(B_SEQ);
    kdown(WK(5));
    kup(WK(5));
    release(B_SEQ);
    check(!dstep_lanes(&t->dstep[5]), "SEQ + the step: the whole step cleared, the accent too");
    key(WK(6));
    kdown(WK(6));
    frames(HOLD_FRAMES);
    step_cell(0, 0, &c);
    ok = c.label && !strcmp(c.label, "LEVEL");
    step_cell(0, 1, &c);
    ok &= c.label && !strcmp(c.label, "RATCHET");
#if FELUCCA_PLOCK
    step_cell(0, 2, &c);
    ok &= c.label && !strcmp(c.label, "ACCENT");
#endif
    step_cell(0, 3, &c);
    ok &= !c.label || !c.label[0];
    check(ok, "a held accent step: LEVEL RATCHET ACCENT (no TUNE, no DECAY: no sound)");
    turn(EN_K1, 1);
    check(dstep_lvl(&t->dstep[6], DRUM_ACC) == LV_HARD, "KNOB 1: the accent step's level up");
    turn(EN_K2, 1);
    check(dstep_rat(&t->dstep[6], DRUM_ACC) == 1u, "KNOB 2: its ratchet");
#if FELUCCA_PLOCK
    {
        int32_t x;
        TDRUM->p[P_DACC] = DACC_DEF;
        turn(EN_K3, 6);
        check(lock_get(t, 6, P_DACC, &x) && x > DACC_DEF && TDRUM->p[P_DACC] == DACC_DEF,
              "KNOB 3: the ACCENT amount locked on the step, the track's value kept");
        step_cell(0, 2, &c);
        check(c.mark == 1u && c.gv == x, "... its padlock and value on the card");
        press(B_HOME);
        turn(EN_K3, 1);
        release(B_HOME);
        check(!lock_get(t, 6, P_DACC, &x), "HOME + KNOB 3: the lock dropped");
        turn(EN_K3, 6);
        ui.hot = 2;
        tap(B_SAVE);
#if FELUCCA_MOTION
        check(hold_get(t, 6, P_DACC, &x) && !lock_get(t, 6, P_DACC, &x),
              "YES: the amount is a motion value like any lane's: the lock becomes a hold (until the next)");
        tap(B_SAVE);
        check(lock_get(t, 6, P_DACC, &x) && !hold_get(t, 6, P_DACC, &x), "YES again: this step only");
#else
        check(lock_get(t, 6, P_DACC, &x) && !hold_get(t, 6, P_DACC, &x) && str_eq(ui.msg, "NO HOLD HERE"),
              "YES (no motion built): no hold, the lock stays, as any value's");
#endif
    }
    kup(WK(6));
    kdown(WK(9));                                       /* step 10: no accent there */
    frames(HOLD_FRAMES);
    step_cell(0, 2, &c);
    turn(EN_K3, 4);
    {
        int32_t x;
        check(!lock_get(t, 9, P_DACC, &x) && !strcmp(c.val, "-"), "a step with no accent hit: no lock (NO HIT ON THIS STEP), its card says -");
    }
    kup(WK(9));
    key(WK(6));
    check(!dstep_has(&t->dstep[6], DRUM_ACC), "(the accent step tapped off ...");
    {
        int32_t x;
        check(!lock_get(t, 6, P_DACC, &x), "... takes its lock with it)");
    }
#else
    kup(WK(6));
#endif
    /* the lane's own STEP page is gone (batch D, the audit's #21: its amount alone was a page of one card; it is the SOUND
     * row's ACCENT card and the mixer's ACCENT row): Pattern, then the tools */
    memset(t->dstep, 0, sizeof t->dstep);
    ui.row[SCR_STEP] = 0;
    frames(2);
    {
        char nm[12];
        uint32_t r;
        ok = SCREENS[SCR_STEP].rows() == STP_PAT + 1u + STP_TL;
        for (r = 0; r < SCREENS[SCR_STEP].rows(); r++) {
            step_name(r, nm);
            ok &= !str_eq(nm, "ACCENT");
        }
        check(ok, "STEP on the ACCENT lane: no LANE page (its amount alone), Pattern and the tools");
    }
    op_enter(SCR_SOUND);
    ui.row[SCR_SOUND] = 0;
    frames(2);
    SCR->cell(0, 3, &c);
    check(c.label && !strcmp(c.label, "ACCENT") && c.vp == &TDRUM->p[P_DACC], "... the amount: the SOUND row's ACCENT card");
    reset_ui();
    memset(t->dstep, 0, sizeof t->dstep);
}
static void acc_walk_tests(void)
{
    uint32_t ok = 1, scr;
    cell_t c;
    for (scr = 0; scr < 3u; scr++) {
        static const uint8_t S[3] = {SCR_SOUND, SCR_STEP, SCR_HOME};
        reset_ui();
        lane_transport(0);
        song.sel = TRK_DRUM;
        op_enter(S[scr]);
        frames(2);
        turn(EN_ALGO, 40);
        frames(2);
        ok &= lane_selected() == DRUM_ACC && mx_sel_row() == MXR_ACC && lane_picked() && ui.scr == S[scr];
        turn(EN_ALGO, -1);
        frames(2);
        ok &= lane_selected() == 15u;
        turn(EN_ALGO, 1);
        frames(2);
        ok &= lane_selected() == DRUM_ACC;
    }
    check(ok, "ALGORITHM walks on to the ACCENT lane after the 16th, on SOUND, STEP and the mixer, and back");
    reset_ui();
    song.sel = TRK_DRUM;
    go_home();
    frame();
    turn(EN_ALGO, 40);
    frames(2);
    ok = mx_row() == MXR_ACC && mxd.first == MXR_LANE0;
    {
        cell_t l;
        mix_cell(MXR_LANE0 + 4u, 0, &l);
        ok &= l.label && !strcmp(l.label, "MUTE");
        mix_cell(MXR_LANE0 + 4u, 1, &l);
        ok &= l.label && !strcmp(l.label, "LEVEL");
    }
    mix_cell(MXR_ACC, 0, &c);
    ok &= c.label && !strcmp(c.label, "MUTE");           /* (the lane's sequencer mute: smute_toggle(SMUTE_ACC)) */
    mix_cell(MXR_ACC, 1, &c);
    ok &= c.label && !strcmp(c.label, "ACCENT") && c.vp == &TDRUM->p[P_DACC] && c.d == &DRUM_ACC_DESC;
    mix_cell(MXR_ACC, 2, &c);
    ok &= !c.label || !c.label[0];
    mix_cell(MXR_ACC, 3, &c);
    ok &= !c.label || !c.label[0];
    check(ok, "the mixer's ACCENT row: a lane's layout (MUTE, ACCENT = the amount), no CUT, sends or sound");
    check(mx_sets(MXR_ACC) == 1u, "... one set of cards");
    TDRUM->p[P_DACC] = DACC_DEF;
    turn(EN_K2, 4);
    check(TDRUM->p[P_DACC] > DACC_DEF, "... KNOB 2 (LEVEL) turns the amount");
    press(B_HOME);
    turn(EN_K2, 1);
    release(B_HOME);
    check(TDRUM->p[P_DACC] == DACC_DEF, "... HOME + KNOB 2: back to 27");
    turn(EN_K3, 3);
    turn(EN_K4, 3);
    check(TDRUM->p[P_DACC] == DACC_DEF, "... the empty cards: inert");
    smute_lane = 0;
    turn(EN_K1, 1);
    check(smute_lane_on(DRUM_ACC) && !smute_lane_on(0) && !dlm_muted(0), "KNOB 1 (MUTE): the ACCENT lane's sequencer mute on");
    frames(130);                                        /* (the help line, over the foot a while after a turn, gone) */
    ui.force = 1;
    frames(2);
    check(px_in(6, mx_row_y(MXR_ACC) + 2u, 30, 12, C_GRAY), "... the row drawn muted (its code grey)");
    turn(EN_K1, -1);
    check(!smute_lane_on(DRUM_ACC), "... and off");
    turn(EN_K1, 1);
    press(B_HOME);
    turn(EN_K1, 1);
    release(B_HOME);
    check(!smute_lane_on(DRUM_ACC), "HOME + KNOB 1: MUTE off (the default)");
    smute_lane = 0;
    TDRUM->p[P_DACC] = DACC_DEF;
    ui.force = 1;
    frames(2);
    ppm("opt-mixer-accent");
    check(px_in(0, mx_row_y(MXR_ACC), 120, 10, col_shade(C_HI, 3u)), "... the row across the foot of the sounds, tinted");
    reset_ui();
}
static void acc_guard_tests(void)                       /* every screen, the accent lane selected: no read past the 16 sounds (ASAN builds see it) */
{
    uint32_t scr, r, k, row;
    cell_t c;
    reset_ui();
    lane_transport(0);
    song.sel = TRK_DRUM;
    for (scr = 0; scr < SCR_N; scr++) {
        lane_select(DRUM_ACC);
        ui.lane = 1;
        if (scr == SCR_FX || scr == SCR_GLOBAL || scr == SCR_SYSTEM || scr == SCR_PROJECT || scr == SCR_SONG || scr == SCR_TEMPO)
            continue;
        op_enter(scr);
        ui.force = 1;
        frames(3);
        for (row = 0; row < SCREENS[scr].rows() && row < 40u; row++) {
            ui.row[scr] = (uint8_t)row;
            ui.force = 1;
            frames(2);
            for (k = 0; k < 4u; k++)
                SCREENS[scr].cell(row, k, &c);
        }
    }
    for (r = 0; r < 3u; r++) {                          /* the buttons' pages on SOUND: ENV LFO FX EDIT ARP SCL */
        static const uint8_t BTN[6] = {B_ENV, B_LFO, B_FX, B_EDIT, B_ARP, B_SCL};
        uint32_t b;
        lane_select(DRUM_ACC);
        op_enter(SCR_SOUND);
        for (b = 0; b < 6u; b++) {
            tap(BTN[b]);
            frames(2);
            ui.force = 1;
            frames(2);
        }
    }
    check(1, "the ACCENT lane selected on every screen, row and page: drawn, no crash");
    reset_ui();
}
#if FELUCCA_MOTION
static void acc_motion_tests(void)                      /* REC playing, the ACCENT amount turned: recorded as motion (a hold event) */
{
    const auto_list_t *a;
    uint32_t i, n = 0;
    acc_pick();
    lane_select(DRUM_ACC);
    ui.lane = 1;
    AL(TDRUM)->n = 0;
    auto_w.on = 0;
    TDRUM->p[P_DACC] = DACC_DEF;
    lane_transport(1);
    song.rec = 1u << TRK_DRUM;
    turn(EN_K4, 5);
    a = AL(TDRUM);
    for (i = 0; i < a->n; i++)
        n += a->ev[i].param == mot_sid(P_DACC) && !(a->ev[i].place & AUTO_ONLY);
    check(n >= 1u, "REC playing, the ACCENT amount turned: a motion event recorded");
    song.rec = 0;
    lane_transport(0);
    AL(TDRUM)->n = 0;
    auto_w.on = 0;
    TDRUM->p[P_DACC] = DACC_DEF;
    reset_ui();
}
#endif
static void acc_other_keys_tests(void)                  /* the other upper keys stay silent on the drum track */
{
    static const uint8_t BLACK[10] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22};   /* (the ten upper keys but the last: 25) */
    uint32_t i, ok = 1;
    acc_pick();
    lane_select(2);
    ui.lane = 1;
    for (i = 0; i < 10u; i++) {
        drums.hits = 0;
        aud_lanes = 0;
        kdown(BLACK[i]);
        ok &= kb_kind[BLACK[i]] == KS_NONE && aud_lanes == 0u && drums.hits == 0u && lane_selected() == 2u;
        kup(BLACK[i]);
    }
    check(ok, "the other ten upper keys, key 1 too: silent, no lane picked (the last upper key is the ACCENT lane's alone)");
    check(DACC_KEY == 25u && kb_drum_lane(1) != DRUM_ACC && key_of_lane(DRUM_ACC) == DACC_KEY, "(the ACCENT lane's key is the lone last upper key, 25)");
    reset_ui();
}
#if FELUCCA_PARAM_HELP
static void acc_help_tests(void)
{
    cell_t c;
    acc_pick();
    SCR->cell(0, 3, &c);
    check(hl_find(SCR_SOUND, 0, &c) != 0u, "the help line: the ACCENT card of the SOUND row has its text");
    lane_select(DRUM_ACC);
    ui.lane = 1;
    SCR->cell(0, 1, &c);
    check(hl_find(SCR_SOUND, 0, &c) != 0u, "... and the LANE card of the ACCENT lane");
    go_home();
    ui.row[SCR_HOME] = MXR_ACC;
    mix_cell(MXR_ACC, 1, &c);
    check(hl_find(SCR_HOME, MXR_ACC, &c) != 0u, "... and the mixer's ACCENT LEVEL card");
    /* (STEP's ACCENT page is gone, batch D: the amount is the SOUND row's card, checked above) */
    reset_ui();
}
#endif
static void accent_ui_tests(void)
{
    acc_key_tests();
    acc_layers_tests();
    acc_other_keys_tests();
#if FELUCCA_MOTION
    acc_motion_tests();
#endif
    acc_sound_tests();
    acc_step_tests();
    acc_walk_tests();
    acc_guard_tests();
#if FELUCCA_PARAM_HELP
    acc_help_tests();
#endif
}
#else
static void accent_ui_tests(void)                       /* no ACCENT lane in the UI: the walk, the mixer and the keys are the 16 sounds' */
{
    check(DRUM_UILANES == DRUM_LANES, "(no ACCENT lane in this build's UI)");
    check(MXR_N == MXR_LANE0 + DRUM_LANES, "... the mixer has the 16 lanes' rows only");
}
#endif
