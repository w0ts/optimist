/* SPDX-License-Identifier: GPL-3.0-only */
/* SLOOP's mixer (TRACKS, ui_studio.c), included by ui_pages_test.c: the two dial pages a HOME tap flips (1: VOL INSERT
 * SEND -, 2: SEND 2 INSERT 2 PAN COMP), the "1/2" / "2/2" in the header, "--" where the row has no such effect, and the
 * HOME double tap still opening SYSTEM. */
static void mx_turn(uint32_t k, int32_t n) { encs[panel.enc[EN_K1 + k]] = n; frame(); }
static void mixer_page_tests(void)
{
    int16_t pan = trk[0].p[P_PAN], lvl = trk[0].p[P_LEVEL];
    uint8_t keep[FX_NSLOT];
    uint32_t i;
    memcpy(keep, fxs_slot, sizeof keep);
    song.sel = 0; mix_page = 0; go_home(); frames(30);
    check(cur_page()->scope == SC_TRK && mix_page == 0u, "mixer: TRACKS opens on dial page 1");
    check(mix_kind(0) == MD_VOL && mix_kind(1) == MD_INS1 && mix_kind(2) == MD_SEND1 && mix_kind(3) == MD_NONE,
          "mixer page 1: VOL INSERT SEND, KNOB 4 empty");
    trk[0].p[P_LEVEL] = 60; trk[0].p[P_MUTE] = 0;
    mx_turn(0, 3);
    check(trk[0].p[P_LEVEL] > 60, "mixer page 1: KNOB 1 the level");
    trk[0].p[P_PAN] = 0;
    mx_turn(3, 5);
    check(trk[0].p[P_PAN] == 0, "mixer page 1: KNOB 4 does nothing (no PAN on page 1)");
    ui.force = 1; frame(); ppm("mixer-page1");
    tap(B_HOME); frames(3);
    check(mix_page == 1u && cur_page()->scope == SC_TRK && !ui.menu, "mixer: HOME tapped on TRACKS: dial page 2");
    check(mix_kind(0) == MD_SEND2 && mix_kind(1) == MD_INS2 && mix_kind(2) == MD_PAN && mix_kind(3) == MD_COMP,
          "mixer page 2: SEND 2, INSERT 2, PAN, COMP");
    mx_turn(2, 5);
    check(trk[0].p[P_PAN] > 0, "mixer page 2: KNOB 3 the pan");
    ui.force = 1; frame(); ppm("mixer-page2");
    /* the slots: DIST CHO DLY REV (the default): one insert, three sends; SEND is REV, SEND 2 the first other (CHO) */
    fxs_load(0, FXT_DIST); fxs_load(1, FXT_CHO); fxs_load(2, FXT_DLY); fxs_load(3, FXT_REV);
    if (FXS_ON(FXT_REV) && FXS_ON(FXT_CHO))
        check(mix_fx_slot(0, 1, 0) == 3 && mix_fx_slot(0, 1, 1) == 1, "mixer: SEND is REV, SEND 2 the first other send (CHO)");
    {
        int16_t x;
        check(mix_desc(0, MD_INS2, &x) == 0, "mixer: one insert in the slots: INSERT 2 is empty (\"--\")");
        check(!FXS_ON(FXT_COMP) ? mix_desc(0, MD_COMP, &x) == 0 : 1, "mixer: no COMP in a slot: COMP is empty");
    }
    if (FELUCCA_MASTER_COMP && FELUCCA_FX_DIST) {      /* COMP in slot 2: INSERT 2 and COMP are both it */
        int16_t x;
        fxs_load(1, FXT_COMP);
        check(mix_fx_slot(0, 0, 1) == 1 && mix_comp_slot(0) == 1 && mix_desc(0, MD_COMP, &x) != 0,
              "mixer: DIST + COMP in the slots: INSERT 2 and COMP are the COMP");
    }
    for (i = 0; i < FX_NSLOT; i++)
        fxs_load(i, keep[i]);
    /* the double tap: the first tap flips the page, the second opens SYSTEM */
    frames(30);
    tap(B_HOME);
    check(mix_page == 0u, "mixer: HOME tapped again: page 1");
    press(B_HOME);
    check(ui.menu == 1, "mixer: ... a quick second press: the SYSTEM menu");
    release(B_HOME); frames(2);
    menu_close(); frames(2);
    /* from another page HOME goes home, the page stays */
    mix_page = 1; open_family(FAM_ENV); frames(30);
    tap(B_HOME); frames(3);
    check(cur_page()->scope == SC_TRK && mix_page == 1u, "mixer: HOME from another page: TRACKS, the dial page kept");
    trk[0].p[P_PAN] = pan; trk[0].p[P_LEVEL] = lvl; mix_page = 0;
    go_home(); frames(30);
}
/* ALGORITHM on the mixer: up from T1 a MASTER row (a plain step), down DR then a stop, a fresh turn into the lanes */
static void mx_algo(int32_t n) { encs[panel.enc[EN_ALGO]] = n; frame(); }
static void mixer_master_tests(void)
{
    int16_t x, thr = song.g[G_CTHR];
    uint32_t q = song.master_q12;
    song.sel = 0; mix_row = 0; mix_page = 0; go_home(); frames(30);
    mx_algo(-1);
    check(mix_cur() == MIX_MASTER && song.sel == 0u, "mixer: ALGORITHM up from T1: the MASTER row (no track picked)");
    ui.force = 1; frame(); ppm("mixer-master");
    frames(2); mx_algo(-1);
    check(mix_cur() == MIX_MASTER, "mixer: MASTER is the top: up again stays");
    check(mix_disp(MIX_MASTER) == 0u && list_top(3, mix_disp(MIX_MASTER), MIX_LIST, 4) == 0u,
          "mixer: MASTER is the list's first row, scrolled into view");
    song.master_q12 = 2048;
    check(mix_desc(MIX_MASTER, MD_VOL, &x) == &MIX_MASTER_VOL && x == 2048, "MASTER page 1 KNOB 1: VOL, the VOLUME knob's level");
    mx_turn(0, 4);
    check(song.master_q12 == 2048u, "MASTER VOL: a read-out (the VOLUME knob sets it), the dial moves nothing");
    check(!mix_desc(MIX_MASTER, MD_INS1, &x) && !mix_desc(MIX_MASTER, MD_SEND1, &x) && !mix_desc(MIX_MASTER, MD_SEND2, &x) &&
          !mix_desc(MIX_MASTER, MD_INS2, &x) && !mix_desc(MIX_MASTER, MD_PAN, &x),
          "MASTER: no insert / send in the slots, no pan: those dials are empty");
    if (FELUCCA_MASTER_COMP) {
        mix_page = 1; ui.force = 1; frames(3);
        check(mix_page == 1u && mix_desc(MIX_MASTER, MD_COMP, &x) == &GP[G_CTHR], "MASTER page 2 KNOB 4: COMP, the master compressor's THRS");
        song.g[G_CTHR] = 0;
        mx_turn(3, -3);
        check(song.g[G_CTHR] < 0, "MASTER page 2 KNOB 4: turns the master compressor's THRS");
        ui.force = 1; frame(); ppm("mixer-master-p2");
        song.g[G_CTHR] = thr;
        mix_page = 0;
    } else {
        check(!mix_desc(MIX_MASTER, MD_COMP, &x), "MASTER without the master COMP: COMP is empty");
    }
    /* down: T1, then DR in one fast turn stops there; a fresh turn: lane 1 */
    frames(30); mx_algo(1);
    check(mix_cur() == 0u && song.sel == 0u, "mixer: down from MASTER: T1");
    frames(30);
    {
        uint32_t i;
        for (i = 0; i < 8u; i++)
            mx_algo(1);                                 /* (one turn: a frame apart) */
    }
    check(mix_cur() == TRK_DRUM && song.sel == TRK_DRUM, "mixer: a fast turn down from T1 stops on DR");
    frames(30); mx_algo(1);
    check(mix_cur() == NTRK && mix_lane() == 0, "mixer: a fresh turn: into the lanes (lane 1)");
    frames(30); mx_algo(-1);
    check(mix_cur() == TRK_DRUM, "mixer: a fresh turn up from lane 1: DR");
    {   /* a fast turn up from DR crosses T3 T2 T1 into MASTER with no stop */
        uint32_t i;
        frames(30);
        for (i = 0; i < 6u; i++)
            mx_algo(-1);
    }
    check(mix_cur() == MIX_MASTER, "mixer: a fast turn up from DR runs on into MASTER (no stop at T1)");
    track_select(1); frames(2);
    check(mix_cur() == 1u, "mixer: another track picked elsewhere: off MASTER, that track's row");
    song.master_q12 = q;
    song.sel = 0; mix_row = 0; go_home(); frames(30);
}
static void sloop_mixer_tests(void)
{
    mixer_master_tests();
    mixer_page_tests();                                 /* (HOME on TRACKS flips the dial page in every build) */
}
