/* SPDX-License-Identifier: GPL-3.0-only */
/* Muted = one grey, everywhere (the user, 2026-11-01: "i said muted notes should be GRAY. how hard can it be? (also for synths,
 * logically)"), included by ui_optimist_test.c: a muted note or hit (the store's AUTO_MUTE / AUTO_SMUTE) is drawn in mute_col(),
 * never a shade of the track's colour or its level, in STEP's roll and drum grid (picked or not), the mixer's step rows and
 * the patterns screen's preview; an unmuted one keeps its level colour. The keys' LEDs: a muted step's key dim (shiftcycle). */
#if FELUCCA_AUTO
static uint32_t mg_cell(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t col)   /* every pixel of the box is col */
{
    int32_t r, c;
    for (r = 0; r < h; r++)
        for (c = 0; c < w; c++)
            if (screen[(y + r) * 240 + x + c] != swap16(col))
                return 0;
    return 1;
}
static void mg_roll_tests(void)
{
    track_t *t;
    uint32_t before, after;
    uint16_t c;
    sv_open();
    t = TSEL;
    c = trk_col(0);
    fm1_irq_off();
    se_set(t, 0, 60, 1);
    se_set(t, 2, 64, 1);
    fm1_irq_on();
    ui.scr = SCR_STEP;
    st.pk = PK_NONE;
    frames(4);
    before = sm_count(mute_col());
    check(sm_count(lvl_col(c, LV_NORM)) > 0, "mute grey, roll: an unmuted synth note is in its level colour");
    check(step_mute_set(t, 0, 0, 1), "mute grey, roll: step 1 muted");
    ui.force = 1;
    frames(4);
    after = sm_count(mute_col());
    check(after > before, "mute grey, roll: the muted note is drawn in mute_col()");
    check(sm_count(lvl_col(c, LV_NORM)) > 0 && sm_count(col_shade(c, 2u)) == 0u, "mute grey, roll: ... the other note keeps its level colour, no dim shade of the track's colour");
    ppm("opt-mute-roll");
    st.pk = 0, st.pk_trk = (uint8_t)song.sel;                                          /* (picked: grey stays, the outline in C_NOTE over it) */
    ui.force = 1;
    frames(4);
    check(pk_ok() && sm_count(mute_col()) > before && sm_count(C_NOTE) > 0, "mute grey, roll: the picked muted note stays grey, its C_NOTE outline over it");
}
static void mg_drum_tests(void)
{
    track_t *t;
    uint32_t before;
    uint16_t c;
    int32_t x = SG_X + 2 * SG_CW;
    se_open(16);
    song.sel = TRK_DRUM;
    t = TSEL;
    track_defaults_steps(t);
    t->p[P_SLEN] = 16;
    fm1_irq_off();
    memset(t->dstep, 0, sizeof t->dstep);
    dstep_set(&t->dstep[2], 2u, LV_HARD, 0);
    dstep_set(&t->dstep[3], 2u, LV_HARD, 0);
    fm1_irq_on();
    lane_select(2);
    ui.scr = SCR_STEP;
    st.pk = PK_NONE;
    frames(4);
    c = op_lane_col(2);
    check(mg_cell(x, SP_Y + SG_LY0(2), SG_CW - 2, SG_LH - 1, lvl_col(c, LV_HARD)), "mute grey, drums: an unmuted hit is in its lane's level colour");
    before = sm_count(mute_col());
    check(step_mute_set(t, 2, 2, 1), "mute grey, drums: lane 3's hit on step 3 muted");
    ui.force = 1;
    frames(4);
    check(mg_cell(x, SP_Y + SG_LY0(2), SG_CW - 2, SG_LH - 1, mute_col()), "mute grey, drums: the muted hit is drawn exactly in mute_col()");
    check(mg_cell(x + SG_CW, SP_Y + SG_LY0(2), SG_CW - 2, SG_LH - 1, lvl_col(c, LV_HARD)) && sm_count(mute_col()) > before, "mute grey, drums: ... the unmuted hit beside it keeps its level colour");
    ppm("opt-mute-drums");
    st.pk = 2;                                          /* (picked) */
    ui.force = 1;
    frames(4);
    check(mg_cell(x, SP_Y + SG_LY0(2), SG_CW - 2, SG_LH - 1, mute_col()), "mute grey, drums: picked, still mute_col()");
}
static void mg_mixer_tests(void)
{
    track_t *t;
    uint32_t before;
    se_open(16);
    t = TSEL;
    fm1_irq_off();
    se_set(t, 1, 60, 1);
    fm1_irq_on();
    ui.scr = SCR_HOME;
    song.playing = 0;
    ui.force = 1;
    frames(4);
    before = sm_count(mute_col());
    check(step_mute_set(t, 1, 0, 1), "mute grey, mixer: a synth step muted");
    ui.force = 1;
    frames(4);
    check(sm_count(mute_col()) > before, "mute grey, mixer: the muted synth step's cell is drawn in mute_col()");
    se_open(16);
    song.sel = TRK_DRUM;
    t = TSEL;
    track_defaults_steps(t);
    t->p[P_SLEN] = 16;
    fm1_irq_off();
    memset(t->dstep, 0, sizeof t->dstep);
    dstep_set(&t->dstep[1], 2u, LV_NORM, 0);
    fm1_irq_on();
    ui.scr = SCR_HOME;
    ui.force = 1;
    frames(4);
    before = sm_count(mute_col());
    check(step_mute_set(t, 1, 2, 1) && mx_nmuted(MXR_LANE0 + 2u, t, 1), "mute grey, mixer: a drum hit muted");
    ui.force = 1;
    frames(4);
    check(sm_count(mute_col()) > before, "mute grey, mixer: the muted hit's cell is drawn in mute_col()");
}
#if FELUCCA_PATTERNS
static void mg_preview_tests(void)
{
    track_t *t;
    uint32_t y;
    reset_ui();
    song.sel = 0;
    song.playing = 0;
    pw_edit(0, 48u);
    t = &trk[0];
    t->step[4].time = ST_NOTE, t->step[4].n = 1, t->step[4].note[0] = 52;
    t->step[8].time = ST_NOTE, t->step[8].n = 1, t->step[8].note[0] = 55;
    check(step_mute_set(t, 4, 0, 1), "mute grey, preview: a synth step muted");
    pat_store_slot(0, 4);
    press(B_LFO);
    frames(HOLD_FRAMES);
    key(WK(4));
    ui.force = 1;
    frame();
    y = pvt_pv_y();
    check(px_in(pvt_step_x(4) + 1u, y + PG_PVT, 9, 12, mute_col()) && !px_in(pvt_step_x(4) + 1u, y + PG_PVT, 9, 12, lvl_col(trk_col(0), LV_NORM)),
          "mute grey, preview: the muted synth step is drawn in mute_col(), not in the track's colour");
    check(px_in(pvt_step_x(8) + 1u, y + PG_PVT, 9, 12, lvl_col(trk_col(0), LV_NORM)), "mute grey, preview: ... the unmuted step keeps its level colour");
    release(B_LFO);
    if (is_drum(&trk[3])) {
        dstep_t *d;
        reset_ui();
        song.sel = 3;
        press(B_LFO);
        frames(HOLD_FRAMES);
        d = trk[3].dstep;
        memset(d, 0, sizeof trk[3].dstep);
        dstep_set(&d[0], 2, LV_HARD, 0);
        dstep_set(&d[5], 0, LV_NORM, 0);
        check(step_mute_set(&trk[3], 0, 2, 1), "mute grey, preview: a drum hit muted");
        pat_store_slot(3, 6);
        key(WK(6));
        ui.force = 1;
        frame();
        y = pvt_pv_y();
        check(px_in(pvt_step_x(0), y + PG_PVT + 2u * PG_LANE, 11, PG_LANE - 1, mute_col()) && !px_in(pvt_step_x(0), y + PG_PVT + 2u * PG_LANE, 11, PG_LANE - 1, lvl_col(trk_col(3), LV_HARD)),
              "mute grey, preview: the muted drum hit is drawn in mute_col()");
        check(px_in(pvt_step_x(5), y + PG_PVT, 11, PG_LANE - 1, lvl_col(trk_col(3), LV_NORM)), "mute grey, preview: ... the unmuted hit keeps its level colour");
        release(B_LFO);
    }
    reset_ui();
}
#endif
static void mutegrey_tests(void)
{
    mg_roll_tests();
    mg_drum_tests();
    mg_mixer_tests();
#if FELUCCA_PATTERNS
    mg_preview_tests();
#endif
}
#endif
