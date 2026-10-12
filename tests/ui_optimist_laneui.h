/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, the drum lanes' gestures on the lane-auto core (the user, 2026-10-12; lanedes/DESIGN.md
 * section 6, seq/auto_lane.c, drums/lane_play.c):
 *   REC + a lane knob playing   SOUND, SOUND 2, ENV and the mixer's lane LEVEL CUT DRIVE record the lane's motion
 *                               (lane_knob: a hold event, heard through the overlay; the patch kept); not recording,
 *                               the patch as ever; the list full: MOTION FULL
 *   a held drum step            LEVEL RATCHET TUNE DECAY (D3): TUNE and DECAY the selected lane's locks there; EDIT
 *                               and ENV its SOUND / ENV pages' values as locks; a step without the lane's hit: NO HIT
 *                               ON THIS STEP (D4); YES a hold <-> a lock; the padlock and the arrow; the lane's hit
 *                               removed takes its locks with it
 *   STEP's marks                a lane's event in its lane's colour (D9)
 *   the lane's LFO              LFO with a lane selected: LFO and LFO DEST of the lane (lane_play.c dlfo); half lit
 *                               there, dark on DR itself
 * Included after the helpers (WK, kdown, kup). */
#if DLA_ON && FELUCCA_PLOCK && FELUCCA_MOTION && FELUCCA_DRUM_EDIT && FELUCCA_DRUM_SYNTH
static uint32_t lane_ev_n(uint32_t l, uint32_t v)       /* the drum track's events of lane l's value v */
{
    const auto_list_t *a = AL(TDRUM);
    uint32_t i, n = 0;
    for (i = 0; i < a->n; i++)
        n += a->ev[i].param == AUTO_LANE(l, v);
    return n;
}
static void lane_rec_tests(void)
{
    track_t *t = TDRUM;
    int32_t keep, h1;
    reset_ui();
    memset(&dl, 0, sizeof dl);
    AL(t)->n = 0;
    auto_w.on = 0;
    dla_holds_off();
    dla_locks_off();
    song.sel = TRK_DRUM;
    TDRUM->p[P_E0] = DRUM_SAMPLED;                      /* the 808: synthesised */
    lane_select(4);
    ui.lane = 1;
    frame();
    tap(B_ENV);                                         /* the lane's ENV: CLICK DECAY BEND LEVEL */
    turn(EN_K2, -3);
    check(dl.ofs[4][DE_DECAY] < 0 && !AL(t)->n, "a lane knob, not recording: the patch (dl.ofs), no event");
    keep = dl.ofs[4][DE_DECAY];
    song.playing = 1;
    song.rec = 1u << TRK_DRUM;
    turn(EN_K2, -6);
    h1 = dla.hv[4][DE_DECAY];
    check(dl.ofs[4][DE_DECAY] == keep && lane_ev_n(4, DE_DECAY) == 1u && ((dla.hm[4] >> DE_DECAY) & 1u) && h1 < keep &&
              lane_motion_drives(4, DE_DECAY), "REC + ENV DECAY playing: the lane's motion (a hold event, heard), the patch kept");
    {
        cell_t c;
        page_cell(snd_page(ui.row[SCR_SOUND]), 1, &c);
        check(c.gv == h1, "... recording: its card shows the value recorded, the next detent goes on from it");
    }
    turn(EN_K2, -2);
    check(dla.hv[4][DE_DECAY] < h1 && dl.ofs[4][DE_DECAY] == keep, "... and does: further down, the patch still kept");
    mx_lane_set(6, DE_DRIVE, -5);                       /* the mixer's lane row (D12: each row its own lane) */
    check(lane_ev_n(6, DE_DRIVE) == 1u && !dl.ofs[6][DE_DRIVE] && dla.hv[6][DE_DRIVE] == -5,
          "the mixer's lane DRIVE, recording: that lane's motion, its patch kept");
    {   /* the list full: MOTION FULL, nothing written */
        auto_list_t *a = AL(t);
        uint32_t n0 = a->n;
        while (a->n < AUTO_MAX)
            (void)auto_put(a, (a->n % 16u) | AUTO_ONLY, AUTO_LANE(15, DE_SNAP) - (a->n / 16u) * 8u, 1);
        ui.msg_t = 0;
        mx_lane_set(7, DE_LEVEL, -3);
        check(a->n == AUTO_MAX && !lane_ev_n(7, DE_LEVEL) && str_eq(ui.msg, "MOTION FULL") && !motion_full,
              "the drum track's list full: MOTION FULL, nothing recorded");
        a->n = (uint8_t)n0;
    }
    song.rec = 0;
    mx_lane_set(6, DE_DRIVE, 3);
    check(dl.ofs[6][DE_DRIVE] == 3 && !((dla.hm[6] >> DE_DRIVE) & 1u), "not recording: the patch, the hold in force let go (heard)");
    song.playing = 0;
    dla_holds_off();
    dla_locks_off();
    AL(t)->n = 0;
    auto_w.on = 0;
    memset(&dl, 0, sizeof dl);
    reset_ui();
}
static void lane_held_tests(void)
{
    track_t *t = TDRUM;
    int32_t x;
    cell_t c;
    reset_ui();
    memset(&dl, 0, sizeof dl);
    song.sel = TRK_DRUM;
    TDRUM->p[P_E0] = DRUM_SAMPLED;
    track_defaults_steps(t);
    AL(t)->n = 0;
    lane_select(4);
    ui.lane = 1;
    op_enter(SCR_STEP);
    frames(2);
    key(WK(2));                                         /* the closed hat on step 3 */
    kdown(WK(2));
    frames(HOLD_FRAMES);
    step_cell(0, 2, &c);
    {
        cell_t d;
        step_cell(0, 3, &d);
        check(c.label && !strcmp(c.label, "TUNE") && d.label && !strcmp(d.label, "DECAY") && !c.mark && !d.mark,
              "a held drum step: LEVEL RATCHET TUNE DECAY (D3), no lock yet");
    }
    turn(EN_K4, -5);
    turn(EN_K3, 2);
    step_cell(0, 3, &c);
    check(lane_lock_get(t, 2, 4, DE_DECAY, &x) && x < 0 && c.mark == 1u && c.gv == x && dl.ofs[4][DE_DECAY] == 0,
          "KNOB 4: the lane's DECAY locked on the step (a padlock), the patch kept");
    check(lane_lock_get(t, 2, 4, DE_TUNE, &x) && x > 0, "KNOB 3: the lane's TUNE locked on the step");
    ui.force = 1;
    frame();
    ppm("opt-lane-held");
    ui.hot = 3;
    tap(B_SAVE);                                        /* YES: DECAY a hold event */
    step_cell(0, 3, &c);
    check(lane_hold_get(t, 2, 4, DE_DECAY, &x) && !lane_lock_get(t, 2, 4, DE_DECAY, &x) && c.mark == 2u &&
              str_eq(ui.msg, "HOLD: UNTIL THE NEXT"), "YES on DECAY: a hold event (the arrow)");
    tap(B_SAVE);
    step_cell(0, 3, &c);
    check(lane_lock_get(t, 2, 4, DE_DECAY, &x) && c.mark == 1u, "YES again: this step only (the padlock)");
    press(B_ENV);                                       /* the lane's ENV values as locks */
    check(st.lock_pg != LOCK_NONE && PAGES[st.lock_pg].scope == SC_DSND && PAGES[st.lock_pg].fam == FAM_ENV,
          "a held drum step + ENV: the lane's ENV page, as locks");
    turn(EN_K1, 3);
    check(lane_lock_get(t, 2, 4, DE_CLICK, &x) && x > 0 && !dl.ofs[4][DE_CLICK], "... KNOB 1: the lane's CLICK locked");
    ui.force = 1;
    frame();
    ppm("opt-lane-held-env");
    release(B_ENV);
    press(B_EDIT);
    check(st.lock_pg != LOCK_NONE && PAGES[st.lock_pg].scope == SC_DSND && PAGES[st.lock_pg].fam == FAM_EDIT &&
              PAGES[st.lock_pg].id[0] == DE_TUNE, "a held drum step + EDIT: the lane's SOUND page, as locks");
    turn(EN_K3, 4);
    check(lane_lock_get(t, 2, 4, DE_SNAP, &x) && x > 0, "... KNOB 3: the lane's SNAP locked");
    release(B_EDIT);
    kup(WK(2));
    check(dstep_has(&t->dstep[2], 4), "let go: the step kept (edited: no tap)");
    kdown(WK(6));                                       /* step 7: no hit of the hat */
    ui.msg_t = 0;
    turn(EN_K4, -3);
    check(!lane_lock_get(t, 6, 4, DE_DECAY, &x) && str_eq(ui.msg, "NO HIT ON THIS STEP"),
          "a step without the lane's hit: NO HIT ON THIS STEP, no lock (D4)");
    kup(WK(6));
    check(!dstep_has(&t->dstep[6], 4), "... and the step stays empty");
    dstep_set(&t->dstep[2], 0, LV_NORM, 0);             /* the kick there too */
    key(WK(2));                                         /* tapped: the hat's hit goes, the kick stays */
    check(!dstep_has(&t->dstep[2], 4) && dstep_has(&t->dstep[2], 0) && !lane_ev_n(4, DE_DECAY) &&
              !lane_ev_n(4, DE_TUNE) && !lane_ev_n(4, DE_CLICK), "the lane's hit removed: its locks there with it (D4)");
    {   /* STEP's marks: a lane's event in its lane's colour (D9), whichever lane is selected */
        uint32_t s = 5, xm = SG_X + s * SG_CW + (SG_CW - 2u) / 2u - 1u, ym = SP_Y + SG_MK_Y;
        fm1_irq_off();
        dl.src[9] = DL_USR;                             /* (a colour of its own: a user sample's, sloop/ui_colors.c lane_col) */
        dstep_set(&t->dstep[s], 9, LV_NORM, 0);
        (void)lane_lock_set(t, s, 9, DE_LEVEL, -4);
        fm1_irq_on();
        lane_select(0);
        ui.cards_t = 0;
        ui.force = 1;
        frames(2);
        check(px_in(xm, ym, 3, 3, lane_col(9)) && lane_col(9) != lane_col(0), "STEP: lane 10's lock marked in its colour, lane 1 selected");
        ppm("opt-lane-marks");
    }
    track_defaults_steps(t);
    AL(t)->n = 0;
    memset(&dl, 0, sizeof dl);
    lane_select(0);
    reset_ui();
}
#if LANE_LFO_UI
static void lane_lfo_tests(void)
{
    const dlfo_t *c = dlfo_get(3);
    uint32_t l;
    reset_ui();
    for (l = 0; l < DRUM_LANES; l++)
        dlfo_set(l, &DLFO_DEF);
    song.sel = TRK_DRUM;
    TDRUM->p[P_E0] = DRUM_SAMPLED;
    op_enter(SCR_SOUND);
    ui.lane = 0;                                        /* DR itself */
    frame();
    check(!((op_btn_avail() >> B_LFO) & 1u), "DR itself: the LFO button dark");
    tap(B_LFO);
    check(snd_fam != FAM_LFO, "... tapped: nothing");
    lane_select(3);
    ui.lane = 1;
    frame();
    check(((op_btn_avail() >> B_LFO) & 1u), "a lane selected: the LFO button half lit");
    tap(B_LFO);
    check(ui.scr == SCR_SOUND && snd_fam == FAM_LFO && SCR->rows() == 2u && !strcmp(snd_page(0)->title, "LFO") &&
              !strcmp(snd_page(1)->title, "LFO DEST") && op_screen_btn() == B_LFO,
          "LFO on a lane: the lane's LFO and LFO DEST, no other row");
    turn(EN_K1, -3);                                    /* RATE down: from 60 */
    turn(EN_K2, 1);                                     /* WAVE: TRI */
    check(c->rate < DLFO_RATE_DEF && c->wave == 1u && dlfo_is_def(dlfo_get(2)), "KNOB 1, 2: the lane's RATE, WAVE (that lane only)");
    ui.force = 1;
    frame();
    ppm("opt-lane-lfo");
    tap(B_LFO);                                         /* again: LFO DEST */
    turn(EN_K1, 2);                                     /* DEST: DECAY */
    turn(EN_K2, 30);                                    /* AMT */
    check(c->s[0].dest == 2u && c->s[0].amt > 0 && ((dlfo_on >> 3) & 1u), "LFO DEST: the lane's DEST DECAY and AMT: its LFO on");
    ui.force = 1;
    frame();
    ppm("opt-lane-lfo-dest");
    ui.lane = 0;                                        /* back to DR */
    op_rows_fix();
    frame();
    check(snd_fam != FAM_LFO && !((op_btn_avail() >> B_LFO) & 1u), "back on DR: no LFO rows, the LFO button dark");
    dlfo_set(3, &DLFO_DEF);
    lane_select(0);
    reset_ui();
}
#else
static void lane_lfo_tests(void) {}
#endif
static void lane_ui_tests(void)
{
    lane_rec_tests();
    lane_held_tests();
    lane_lfo_tests();
}
#else
static void lane_ui_tests(void) {}
#endif
