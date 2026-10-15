/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, the user's requests of 2026-10-12 (docs/UI-OPTIMIST-DESIGN.md section 2.4):
 *   "the algo button should work the same on all pages, master (if it makes sense), t1, t2,t3,t4 and then individual
 *   drum lanes (if it makes sense)", then "i want uniformity": one walk, the mixer's order MASTER T1 T2 T3 DR and the
 *   16 lanes, the same on every page and layer
 *   "the preset on a drum lane should change the drum lane preset, not the kit": PRESETS on DR the kit; on a lane with
 *   SRC KIT the kit too, on a lane on an instrument that lane's sound only; HOME + PRESETS the same split
 *   ARP "is the same as lfo/env. no list": its picture is never blank, the arp off or notes held
 * Included after ui_optimist_glo.h. */
static uint32_t al_walk(int32_t d)                      /* ALGORITHM d detents, then where the walk is (a mixer row) */
{
    turn(EN_ALGO, d);
    frames(2);
    return ui.scr == SCR_HOME ? mx_row() : ui.scr == SCR_FX && lay.shown == LY_PLAY ? (uint32_t)MXR_MASTER
           : ui.master ? (uint32_t)MXR_MASTER : mx_sel_row();
}
static void algo_mixer_tests(void)
{
    uint32_t ok = 1, r;
    reset_ui();
    song.sel = 0;
    frames(2);
    ok &= al_walk(-1) == MXR_MASTER && al_walk(-1) == MXR_MASTER;
    for (r = MXR_T1; r < MXR_LN; r++)
        ok &= al_walk(1) == r;
#if FELUCCA_USB_AUDIO
    ok &= al_walk(1) == MXR_USB && al_walk(1) == MXR_USB;   /* (the USB input's row, the mixer's last) */
    ok &= al_walk(-1) == MXR_LN - 1u;
#endif
#if FELUCCA_USB_AUDIO
    ok &= al_walk(1) == MXR_N - 1u && al_walk(-1) == MXR_LN - 1u;
#else
    ok &= al_walk(1) == MXR_LN - 1u;
#endif
    ok &= lane_picked() && lane_selected() == DRUM_UILANES - 1u;
    check(ok, "walk, the mixer: MASTER, T1 .. DR, the 16 lanes and the ACCENT lane (built), stopping at both ends");
    ok = al_walk(-(int32_t)DRUM_UILANES) == MXR_DR && !lane_picked() && song.sel == TRK_DRUM;
    check(ok, "... back to DR: DR itself selected (no lane)");
    reset_ui();
}
static void algo_sound_tests(void)
{
    uint32_t ok;
    reset_ui();
    song.sel = 0;
    set_engine_of(&trk[0], trk_def_engine(0));          /* (the tests before leave T1 on whatever engine the presets' walk ended on; an FM6 has its own ENV page) */
    frames(2);
    tap(B_ENV);
    ok = al_walk(-1) == MXR_MASTER && ui.scr == SCR_FX;
    ok &= al_walk(1) == MXR_T1 && ui.scr == SCR_SOUND && snd_fam == FAM_ENV;
    check(ok, "walk, SOUND (a synth's ENV): MASTER is the master FX screen, right T1's ENV again");
    ok = al_walk(1) == MXR_T1 + 1u && al_walk(1) == MXR_T1 + 2u && al_walk(1) == MXR_DR && !lane_picked();
    ok &= al_walk(1) == MXR_LANE0 && lane_picked() && lane_selected() == 0u;
    ok &= al_walk(1) == MXR_LANE0 + 1u && lane_picked() && lane_selected() == 1u && ui.scr == SCR_SOUND;
    ppm("opt-algo-sound-lane2");
    check(ok, "walk, SOUND: T2 T3 DR, then lane 1 and lane 2 (the drum SOUND pages are per lane)");
    ok = px_in(2, 3, 4, 19, lane_col(1));
    check(ok, "... the header's badge: the lane, in its sound's colour");
    ok = al_walk(-2) == MXR_DR && !lane_picked() && al_walk(-200) == MXR_MASTER && ui.scr == SCR_FX;
    check(ok, "... and back: DR itself, then down to MASTER");
    /* the drum track's own SOUND rows (EDIT on DR): into its lanes */
    reset_ui();
    song.sel = TRK_DRUM;
    frame();
    tap(B_EDIT);
    ok = ui.scr == SCR_SOUND && al_walk(1) == MXR_LANE0 && lane_picked() && al_walk(-1) == MXR_DR && al_walk(-1) == MXR_T1 + 2u;
    check(ok, "walk, the drum track's EDIT rows: DR -> lane 1 -> DR -> T3");
    song.sel = 0;
    reset_ui();
}
/* the same walk on every screen and layer (the user, 2026-10-12: "i want uniformity"): from T1, MASTER (the page's
 * master view: the master FX screen, a layer's master FX cards; the badge MASTER, the track buttons dark), back to the
 * same page on T1, then T2 T3 DR and the 16 lanes, stopping at both ends */
static int al_full_walk(uint32_t scr0, int in_lay, const char *nm)
{
    uint32_t r, ok = 1, n;
    ok &= al_walk(-1) == MXR_MASTER && al_walk(-1) == MXR_MASTER && mx_master_dead(B_ENV) && mx_master_dead(B_ARP);
    ok &= al_walk(1) == MXR_T1 && ui.scr == scr0 && !mx_master_dead(B_ENV) && (in_lay ? lay.shown != LY_PLAY : 1);
    for (r = MXR_T1 + 1u; r < MXR_LN; r++) {
        n = al_walk(1);
        ok &= n == r && (r < MXR_LANE0 ? !lane_picked() : lane_picked() && lane_selected() == r - MXR_LANE0);
    }
    ok &= al_walk(3) == (scr0 == SCR_HOME ? MXR_N : MXR_LN) - 1u && ui.scr == scr0;   /* (the mixer: on to the USB input's
                                                         * row, in a build with USB audio) */
    ok &= al_walk(-(int32_t)MXR_N) == MXR_MASTER;
    if (!ok)
        printf("  walk on %s: broke (scr %u, at %u)\n", nm, ui.scr, mx_sel_row());
    return (int)ok;
}
static void algo_every_tests(void)
{
    static const uint8_t PB[] = {B_ENV, B_LFO, B_EDIT, B_ARP, B_SCL, B_FX, B_SEQ};   /* (GLO: GLOBAL, whose ALGORITHM moves
                                                         * its cursor: ui_optimist_glo.h) */
    static const uint8_t SC[] = {SCR_SCOPE};                /* (PROJECT, SONG: no track, ALGORITHM does nothing: ui_optimist_algonone.h) */
    static const uint8_t LB[] = {B_SCL, B_EDIT};            /* (the layers per track: FX ARP GLO SAVE LFO are not: ui_optimist_algonone.h) */
    uint32_t i, ok = 1;
    for (i = 0; i < sizeof PB; i++) {
        reset_ui();
        song.sel = 0;
        frame();
        tap(PB[i]);
        ok &= (uint32_t)al_full_walk(ui.scr, 0, "a page button's page");
    }
    check(ok, "walk, SOUND's families and STEP: MASTER T1 .. DR, the 16 lanes, both ends, the page kept");
    ok = 1;
    for (i = 0; i < sizeof SC; i++) {
        reset_ui();
        song.sel = 0;
        op_enter(SC[i]);
        frame();
        ok &= (uint32_t)al_full_walk(SC[i], 0, SCR_NAME[SC[i]]);
    }
    check(ok, "walk, SCOPE: the same walk (its source follows ALGORITHM)");
    reset_ui();
    song.sel = 0;
    frame();
    ok = (uint32_t)al_full_walk(SCR_HOME, 0, "the mixer");
    check(ok, "walk, the mixer: the same walk (its rows)");
    ok = 1;
    for (i = 0; i < sizeof LB; i++) {
        reset_ui();
        song.sel = 0;
        tap(B_ENV);
        press(LB[i]);
        frames(HOLD_FRAMES);
        ok &= (uint32_t)al_full_walk(SCR_SOUND, 1, "a layer");
        ok &= al_walk(-(int32_t)MXR_N) == MXR_MASTER && ui.master;
        frames(HOLD_FRAMES);
        release(LB[i]);
        frames(2);
    }
    check(ok, "walk, the SCL and EDIT layers: the same walk, MASTER their ui.master");
    /* MASTER on a page with no master values: the master FX screen, the badge MASTER; right: the page again */
    reset_ui();
    song.sel = 0;
    tap(B_SEQ);
    ok = al_walk(-1) == MXR_MASTER && ui.scr == SCR_FX && px_in(2, 3, 4, 19, C_HI) && al_walk(1) == MXR_T1 && ui.scr == SCR_STEP;
    check(ok, "STEP, ALGORITHM left from T1: MASTER (the master FX), right: STEP again");
    /* STEP's lane pages follow the lane */
    reset_ui();
    song.sel = TRK_DRUM;
    frame();
    tap(B_SEQ);
    ok = al_walk(1) == MXR_LANE0 && lane_picked() && al_walk(2) == MXR_LANE0 + 2u;
    ui.row[SCR_STEP] = (uint8_t)STP_PAT;              /* (the LANE page: the selected lane's) */
    frame();
    ok &= al_walk(1) == MXR_LANE0 + 3u && ui.row[SCR_STEP] == STP_PAT && lane_selected() == 3u;
    ok &= al_walk(-4) == MXR_DR && ui.row[SCR_STEP] == STP_PAT && !lane_picked();
    check(ok, "STEP, the drum view: the walk into the lanes and back to DR; the page stays (SELECT's), the lane is previewed");
    /* HOME + a key: a lane straight away, on any page */
    reset_ui();
    song.sel = 0;
    op_enter(SCR_SOUND);
    song.sel = TRK_DRUM;
    frame();
    fm1_in.buttons |= BT(B_OCTDN);
    frame();
    kdown(WK(5));
    kup(WK(5));
    fm1_in.buttons &= ~BT(B_OCTDN);
    frame();
    ok = lane_picked() && lane_selected() == 5u && al_walk(1) == MXR_LANE0 + 6u && al_walk(-7) == MXR_DR;
    check(ok, "HOME + a drum key on SOUND: that lane selected, the walk on from it");
    song.sel = 0;
    reset_ui();
}
/* PRESETS: DR the kit; a lane by its SOURCE (the user, 2026-10-12: "if Slot=kit. preset knob changes the preset kit, if
 * Slot=something else preset changes the preset for the drum lane"); HOME + PRESETS the same split */
static uint32_t al_first_own(uint32_t l)                /* the first SOURCE entry of lane l that is its own sound */
{
    uint32_t i, n = ls_count(l);
    for (i = 0; i < n && !ls_ok(i); i++)
        ;
    return i < n ? i : 0u;
}
static void algo_presets_tests(void)
{
    uint32_t ok, kit, i, n, k0, k1;
    uint8_t src[DRUM_LANES];
    reset_ui();
    memset(&dl, 0, sizeof dl);
    song.sel = TRK_DRUM;
    frame();
    tap(B_EDIT);
    kit = drum_kit_pos();
    turn(EN_PRESET, 1);
    frames(2);
    ok = !lane_picked() && drum_kit_pos() != kit && !memcmp(dl.src, (uint8_t[DRUM_LANES]){0}, DRUM_LANES);
    check(ok, "PRESETS on DR: the kit, the lanes as the kit has them");
    turn(EN_PRESET, -1);
    frames(2);
    kit = drum_kit_pos();
    n = ls_count(2);
    dl.src[2] = (uint8_t)dsnd_idx_src(al_first_own(2));   /* lane 3 (snare) on an instrument, lane 4 (clap) on KIT */
    snd_mark(TRK_DRUM);                                 /* (the lanes as loaded: no edits to ask about) */
    turn(EN_ALGO, 4);                                   /* lane 4: the clap, SRC KIT */
    frames(2);
    memcpy(src, dl.src, sizeof src);
    turn(EN_PRESET, 1);
    frames(2);
    ok = lane_picked() && lane_selected() == 3u && drum_kit_pos() != kit && !memcmp(src, dl.src, sizeof src);
    check(ok, "PRESETS on a lane with the source KIT: the kit (as on DR), every lane's source stays");
    check(ui.toast_t && ui.toast_col == kit_col() && !strncmp(ui.msg, "KIT ", 4), "... the toast names the kit");
    fm1_in.buttons |= BT(B_OCTDN);
    frame();
    turn(EN_PRESET, 1);
    fm1_in.buttons &= ~BT(B_OCTDN);
    frames(2);
    check(drum_kit_pos() != kit && !memcmp(src, dl.src, sizeof src), "HOME + PRESETS on a lane with SRC KIT: the kits, as on DR");
    TDRUM->p[P_E0] = (int16_t)kit;                      /* (the kit back: kit was a factory kit) */
    frame();
    turn(EN_ALGO, -1);                                  /* lane 3: the snare, on an instrument */
    frames(2);
    ppm("opt-algo-lane-before");
    turn(EN_PRESET, 1);
    frames(2);
    ppm("opt-algo-lane-after");
    ok = lane_picked() && lane_selected() == 2u && drum_kit_pos() == kit && n >= 2u && dl.src[2] != src[2] && dl.src[2] != DL_KIT;
    for (i = 0; i < DRUM_LANES; i++)
        ok &= i == 2u || dl.src[i] == src[i];
    check(ok, "PRESETS on a lane on an instrument: that lane's sound only, the kit and the other lanes stay");
    check(ui.toast_t && ui.toast_col == lane_col(2) && !strncmp(ui.msg, "SNARE: ", 7), "... the toast names the lane and its sound, in its colour");
    turn(EN_PRESET, -1);
    frames(2);
    check(dl.src[2] == src[2], "... and back");
    turn(EN_PRESET, -1);
    frames(2);
    check(dl.src[2] != DL_KIT, "... never back to KIT by PRESETS (the source row sets KIT)");
    dl.src[2] = src[2];
    snd_mark(TRK_DRUM);                                 /* (the lanes as loaded: no edits to ask about) */
    k0 = ls_kind(ls_cur(2, n));
    fm1_in.buttons |= BT(B_OCTDN);
    frame();
    turn(EN_PRESET, 1);
    fm1_in.buttons &= ~BT(B_OCTDN);
    frames(2);
    ppm("opt-algo-lane-kind");
    k1 = ls_kind(ls_cur(2, n));
    ok = k1 != k0 && dl.src[2] != DL_KIT && drum_kit_pos() == kit;
    for (i = 0; i < ls_cur(2, n); i++)
        ok &= !ls_ok(i) || ls_kind(i) != k1;           /* (the kind's first sound) */
    check(ok, "HOME + PRESETS on a lane on an instrument: the first sound of the next kind (the kit stays)");
    check(ui.scr == SCR_SOUND, "... HOME let go: no NO (the screen stays)");
    /* the mixer's lane rows: the same */
    memset(&dl, 0, sizeof dl);
    dl.src[4] = (uint8_t)dsnd_idx_src(al_first_own(4));
    snd_mark(TRK_DRUM);                                 /* (the lanes as loaded: no edits to ask about) */
    go_home();
    frame();
    ui.row[SCR_HOME] = MXR_LANE0 + 4u;
    lane_select(4);
    frame();
    src[4] = dl.src[4];
    turn(EN_PRESET, 1);
    frames(2);
    ok = lane_picked() && dl.src[4] != src[4] && drum_kit_pos() == kit;
    check(ok, "the mixer's lane row on an instrument: PRESETS (source LANE) the lane's sound");
    memset(&dl, 0, sizeof dl);
    lane_select(0);
    song.sel = 0;
    reset_ui();
}
/* ARP: its picture is never blank (the user, 2026-10-12) */
static uint32_t arp_pic_px(void)                        /* lit pixels of the panel under the cards */
{
    uint32_t x, y, n = 0;
    for (y = (uint32_t)OP_PY; y < 240u; y++)
        for (x = 0; x < 240u; x++)
            n += screen[y * 240u + x] != swap16(C_BLACK);
    return n;
}
static void algo_arp_tests(void)
{
    uint32_t off, held;
    int16_t mode;
    reset_ui();
    song.sel = 0;
    tap(B_ARP);
    mode = TSEL->p[P_AMODE];
    TSEL->p[P_AMODE] = 0;
    ui.force = 1;
    frames(3);
    off = arp_pic_px();
    ppm("opt-algo-arp-off");
    check(off > 1500u && px_in(2, OP_PY + 20u, 236, GRAPH_H - 30u, col_shade(trk_col(0), 3u)),
          "ARP off, nothing held: a picture (the pattern UP would play, shaded), not two lines");
    ui.row[SCR_SOUND] = 1;                              /* ARP 2 */
    ui.force = 1;
    frames(3);
    check(arp_pic_px() > 1500u, "ARP 2, the arp off: the same picture");
    ui.row[SCR_SOUND] = 0;
    TSEL->p[P_AMODE] = 1;
    TSEL->nheld = 0;                                    /* (nothing left held or latched by an earlier test) */
    TSEL->arp_phys = 0;
    kdown(WK(0));
    kdown(WK(4));
    ui.force = 1;
    frames(3);
    held = arp_pic_px();
    ppm("opt-algo-arp-held");
    check(TSEL->nheld >= 2u && held > 1500u && px_in(2, OP_PY + 20u, 236, GRAPH_H - 30u, trk_col(0)),
          "ARP on, two keys held (their chord notes): the pattern of those notes in the track's colour");
    kup(WK(0));
    kup(WK(4));
    TSEL->p[P_AMODE] = mode;
    reset_ui();
}
static void algo_tests(void)
{
    algo_mixer_tests();
    algo_sound_tests();
    algo_every_tests();
    algo_presets_tests();
    algo_arp_tests();
}
