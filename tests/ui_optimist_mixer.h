/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, the horizontal mixer (docs/UI-OPTIMIST-DESIGN.md sections 4.1, 11.6), SCOPE, the header's
 * engine and no footer (11.5). Included by ui_optimist_test.c after its helpers. */
#include "../firmware/src/storage/settings_word.c"     /* the settings word (project.c's, not in PROJ_HOST) */

static uint32_t mx_row_y(uint32_t r) { mrect_t q; mx_rect(r, &q); return (uint32_t)(OP_PY + q.y); }   /* row r on the screen */
static uint32_t mx_row_x(uint32_t r) { mrect_t q; mx_rect(r, &q); return (uint32_t)q.x; }
static int mx_labels(uint32_t r, uint32_t set, const char *a, const char *b, const char *c, const char *d)
{
    const char *want[4] = {a, b, c, d};
    uint32_t k, keep = mx.set;
    cell_t e;
    int ok = 1;
    mx.set = (uint8_t)set;
    for (k = 0; k < 4u; k++) {
        mix_cell(r, k, &e);
        if (want[k] && (!e.label || strcmp(e.label, want[k]))) {
            printf("  mixer row %u set %u cell %u: '%s', not '%s'\n", r, set, k, e.label ? e.label : "", want[k]);
            ok = 0;
        }
    }
    mx.set = (uint8_t)keep;
    return ok;
}
static void mixer_walk_tests(void)
{
    uint32_t i, ok = 1;
    reset_ui();
    ui.force = 1;
    frame();
    check(mx_row() == MXR_T1 && mxd.first == MXR_T1 && px_in(6, mx_row_y(MXR_T1) + 3u, 16, 14, C_WHITE) && MXL_H == (OP_FOOT ? 36 : 41) &&
              px_in(0, mx_row_y(MXR_DR) + 2u, 4, 30, trk_col(TRK_DRUM)) && !px_in(0, mx_row_y(MXR_DR) + (uint32_t)MXL_H + 2u, 4, 1, lane_col(0)),
          "the mixer opens on T1 T2 T3 DR, four rows of 41 px (36 over the help band; MASTER above, out of view)");
    for (i = 1; i < MXR_LN - MXR_T1; i++) {             /* ALGORITHM down: T2 T3 DR, then the 16 lanes */
        turn(EN_ALGO, 1);
        ok &= mx_row() == MXR_T1 + i;
        ok &= i < TRK_DRUM ? song.sel == i : song.sel == TRK_DRUM;
        if (MXR_T1 + i >= MXR_LANE0)
            ok &= lane_selected() == MXR_T1 + i - MXR_LANE0;
    }
#if FELUCCA_USB_AUDIO
    turn(EN_ALGO, 1);                                   /* (fx-rework stage 3: the USB input's row after the drum tracks) */
    ok &= mx_row() == MXR_USB && song.sel == TRK_DRUM;
    turn(EN_ALGO, 1);
    ok &= mx_row() == MXR_USB;
    ui.force = 1;
    frame();
    ok &= mxd.first == MXR_USB && mxd.last == MXR_N && px_in(2, 3, 50, 19, C_HI);   /* (alone; the badge "5 USB") */
    {
        uint32_t av = op_btn_avail();
        ok &= !(av & (1u << B_ENV | 1u << B_EDIT | 1u << B_FX));   /* (no instrument: the track buttons dark) */
    }
    ppm("opt-mixer-usb");
    check(ok, "ALGORITHM walks T1 T2 T3 DR and the 16 lanes (each selected), then the USB input's row, alone (the badge 5 USB, the "
          "track buttons dark), stopping there");
    turn(EN_ALGO, -1);
    ok = mx_row() == MXR_LN - 1u;
#else
    turn(EN_ALGO, 1);
#endif
    check(ok && mx_row() == MXR_LN - 1u, "ALGORITHM walks T1 T2 T3 DR and the 16 lanes (each selected), stopping at the last");
    ui.force = 1;
    frame();
    check(mxd.first == MXR_LANE0 && mxd.last == MXR_LN && mx_row_x(MXR_LN - 1u) == (ACC_UI ? 0u : 120u) && mx_row_y(MXR_LN - 1u) > mx_row_y(MXR_LN - 2u) &&
              px_in(ACC_UI ? 0 : 120, mx_row_y(MXR_LN - 1u), 120, 10, col_shade(ACC_UI ? C_HI : lane_col(15), 3u)),
          "past DR the view is the 16 drum sounds, two columns of eight; the selected lane tinted in its colour (the last: the 16th, right; with the ACCENT lane built, its row across the foot)");
    ppm("opt-mixer-h-lane16");
    ok = mx_row_x(MXR_LANE0 + 15u) == 120u && mx_row_y(MXR_LANE0 + 15u) > mx_row_y(MXR_LANE0 + 14u);
    check(ok, "the 16th lane is still the right column's last row");
    for (i = 0, ok = 1; i < DRUM_UILANES; i++) {        /* back up through the lanes to DR, T3 ... */
        turn(EN_ALGO, -1);
        ok &= mx_row() == MXR_LN - 2u - i;
    }
    check(ok && mx_row() == MXR_DR && song.sel == TRK_DRUM, "ALGORITHM back: the lanes, then DR");
    frame();
    check(mxd.first == MXR_T1 && mxd.last == MXR_LANE0, "on DR the view is the four channels again (T1 T2 T3 DR)");
    turn(EN_ALGO, -3);
    check(mx_row() == MXR_T1 && song.sel == 0, "... T3, T2, T1");
    turn(EN_ALGO, -1);
    ui.force = 1;
    frame();
    check(mx_row() == MXR_MASTER && song.sel == 0 && mxd.first == 0 && mxd.last == MXR_T1,
          "past T1 up: MASTER alone on the panel (the track stays)");
    turn(EN_ALGO, -1);
    check(mx_row() == MXR_MASTER, "... and it stops there");
    ui.force = 1;
    frame();
    check(px_in(2, 3, 20, 19, C_HI) && !px_in(2, 3, 20, 19, trk_col(0)),
          "the MASTER row: the header's badge is MASTER, not T1's engine (master has no instrument)");
    {
        uint32_t av = op_btn_avail(), dead = 1u << B_ENV | 1u << B_LFO | 1u << B_EDIT | 1u << B_ARP | 1u << B_SCL |
                                             1u << B_SEQ;
        check((av & dead) == 0u && (av & (1u << B_FX)) && (av & (1u << B_GLO)),
              "the MASTER row: ENV LFO EDIT ARP SCL SEQ dark, FX and GLO lit");
        tap(B_ENV);
        tap(B_LFO);
        tap(B_EDIT);
        tap(B_ARP);
        tap(B_SEQ);
        check(ui.scr == SCR_HOME && mx_row() == MXR_MASTER && !op_armed(), "... a track button tapped: nothing, still the MASTER row");
        press(B_OCTDN);
        tap(B_ENV);
        release(B_OCTDN);
        check(!op_armed(), "... SHIFT + ENV: no INIT of T1 asked");
        if (op_armed())
            op_no();
        {
            uint32_t tot, p0 = preset_pos(&tot);
            turn(EN_PRESET, 2);
            check(song.sel == 0 && preset_pos(&tot) == p0 && mx_row() == MXR_MASTER,
                  "... PRESETS turned: T1's sound unchanged (master has none)");
        }
        tap(B_FX);
        check(ui.scr == SCR_FX, "... FX: the master FX");
        tap(B_HOME);
        ui.row[SCR_HOME] = MXR_MASTER;
        frame();
    }
    ppm("opt-mixer-h-master");
    turn(EN_ALGO, 1);
    check(mx_row() == MXR_T1, "down again: T1");
    song.sel = 2;                                       /* the track selected elsewhere: the cursor follows */
    frame();
    check(mx_row() == MXR_T1 + 2u, "the cursor follows the track selected elsewhere");
    song.sel = 0;
    frame();
}
static void mixer_sets_tests(void)
{
    uint32_t n, last;
    reset_ui();
    n = mx_sets(MXR_T1);
    last = n - 1u;
    check(mx_labels(MXR_T1, 0, "VOLUME", "PAN", 0, 0) && n >= 3u,
          "a track: VOLUME PAN (the mix, no MUTE card: SHIFT2 on VOLUME mutes), then the inserts and the sends (no FX ON, no SOUND card)");
    {   /* values only: no knob set of any row holds a screen */
        uint32_t r, s, k, bad = 0, keep = mx.set;
        cell_t e;
        for (r = 0; r < MXR_N; r++)
            for (s = 0; s < mx_sets(r); s++) {
                mx.set = (uint8_t)s;
                for (k = 0; k < 4u; k++) {
                    mix_cell(r, k, &e);
                    bad += e.kind == CK_ACT || (e.label && (!strcmp(e.label, "FX") || !strcmp(e.label, "SONG") ||
                           !strcmp(e.label, "PROJECT") || !strcmp(e.label, "SYSTEM")));
                }
            }
        mx.set = (uint8_t)keep;
        check(!bad, "the mixer's knob sets are only values: no screens set on any row");
    }
    {   /* the INS set (the track insert slots, 2026-10-12): S1 TYPE, S1's amount, S2 TYPE, S2's amount */
        cell_t c;
        mx.set = 1;
        mix_cell(MXR_T1, 0, &c);
        check(c.d == &INS_DESC[0] && !strcmp(c.label, "S1") && !strcmp(c.val, FELUCCA_FX_DIST ? "DIST" : "EMPTY"),
              "the second set, the inserts: S1's TYPE first (DIST by default)");
        mix_cell(MXR_T1, 1, &c);
        check(!FELUCCA_FX_DIST || c.d == &TP[P_DIST], "... then S1's amount (DST)");
        mix_cell(MXR_T1, 3, &c);
        check(!TFLT_ON || c.d == &TP[FXT_AMT[FXT_FILT]], "... S2's amount the track's FILTER");
        mx.set = 2;
        mix_cell(MXR_T1, 0, &c);
        check(!FELUCCA_FX_REVERB || c.d == &TP[P_REV], "the third set, the sends: REV first (in a slot), then DLY CHO");
#if FELUCCA_MASTER_COMP
        ui.row[SCR_HOME] = MXR_T1;
        mx.set = 1;
        ui.hot = 2;
        {
            int16_t *v;
            int32_t i, cmp = 0;
            ins_desc(0, 1, &v);
            for (i = 0; i < (int32_t)INS_NLIST; i++)
                cmp = ins_list[i] == FXT_COMP ? i : cmp;
            turn(EN_K3, cmp - *v);                      /* S2's TYPE turned to COMP */
        }
        mix_cell(MXR_T1, 3, &c);
        check(ins_slot[0][1] == FXT_COMP && c.d == &TP[P_TCOMP] && ins_slot[1][1] == INS_DEF1,
              "KNOB 3 on S2 TYPE: COMP loaded in T1's S2 (T2's as it was); S2's amount COMP's (CMP)");
        {
            int16_t *v;
            int32_t i, cmp = 0;
            ins_desc(0, 0, &v);
            for (i = 0; i < (int32_t)INS_NLIST; i++)
                cmp = ins_list[i] == FXT_COMP ? i : cmp;
            turn(EN_K1, cmp - *v);                      /* S1 to COMP: S2's type, so the two swap (D3) */
        }
        check(ins_slot[0][0] == FXT_COMP && ins_slot[0][1] == INS_DEF0, "... KNOB 1 on S1 TYPE to COMP: the two slots swap");
        ins_defaults();
        mx.set = 0;
#endif
    }
    check(mx_labels(MXR_T1, 2, 0, 0, 0, "PAN") && mx_labels(MXR_DR, 2, 0, 0, 0, "PAN") &&
              mx_labels(MXR_LANE0 + 2u, 2, 0, 0, 0, "PAN"),
          "the sends set (a synth track, DR, a lane): card 4 is PAN, whatever sends are in a slot");
    {   /* PAN on the sends set is the same value as the track's: T1 and DR (the drum track's P_PAN), a lane's own (drum_mix.c) */
        cell_t c;
        int16_t p0 = trk[0].p[P_PAN];
        mx.set = 2;
        mix_cell(MXR_T1, 3, &c);
        check(c.vp == &trk[0].p[P_PAN] && c.d == &TP[P_PAN], "... T1's card 4 is its P_PAN");
        mix_cell(MXR_DR, 3, &c);
        check(c.vp == &trk[TRK_DRUM].p[P_PAN], "... DR's card 4 is the drum track's P_PAN");
        mix_cell(MXR_LANE0 + 2u, 3, &c);
        check(c.d && c.vp && *c.vp == dlm_pan[2], "... a lane's card 4 reads its own pan");
        ui.row[SCR_HOME] = MXR_LANE0 + 2u;
        ui.hot = 3;
        turn(EN_K4, 5);
        check(dlm_pan[2] == 5, "... KNOB 4 on a lane's PAN sets the lane's pan (drum_mix.c)");
        dlm_pan[2] = 0;
        trk[0].p[P_PAN] = p0;
        mx.set = 0;
    }
    check(mx_labels(MXR_DR, 0, "VOLUME", 0, 0, 0) && mx_labels(MXR_DR, 1, "S1", 0, "S2", 0) &&
              (!FELUCCA_FX_REVERB || mx_labels(MXR_DR, 2, "REV", 0, 0, 0)),
          "DR: VOLUME, its insert slots, its send levels (REV DLY CHO); no MUTE, no FX ON, no KIT card");
    {   /* the drum track's send levels (2026-10-12, "it should be like any other track"): 100 % by default */
        cell_t c;
        int16_t v0 = dsend_kit[0];
        mx.set = 2;
        mix_cell(MXR_DR, 0, &c);
        check(!FELUCCA_FX_REVERB || (c.vp == &dsend_kit[0] && !strcmp(c.val, "100") && c.d->fmt == F_PCT),
              "... DR's REV: the drum track's REV level, a level (100 %), not ON / OFF");
        dsend_kit[0] = v0;
        mx.set = 0;
    }
    check(mx_labels(MXR_LANE0 + 2u, 0, "LEVEL", "CUT", "DRIVE", 0) &&
              (!FELUCCA_FX_DIST || mx_labels(MXR_LANE0 + 2u, 1, "S1", "DST", "S2", FELUCCA_MASTER_COMP ? "DYN" : 0)),
          "a lane: LEVEL CUT DRIVE (no MUTE card), then its two insert slots S1 DST, S2 DYN (fx-rework: always heard)");
    check(!FELUCCA_FX_DIST || mx_labels(MXR_LANE0 + 2u, 2, "REV", "DLY", "CHO", 0),
          "... then REV DLY CHO (no SOUND card)");
    mst_defaults();
    check(mx_labels(MXR_MASTER, 0, "S1", 0, "S2", 0) &&
              (!FELUCCA_MASTER_COMP || mx_labels(MXR_MASTER, 1, "THRS", "RATIO", "GAIN", "CEIL")) &&
              mx_labels(MXR_MASTER, FELUCCA_MASTER_COMP ? 2 : 1, "REV RET", "DLY RET", "CHO RET", 0) &&
              mx_labels(MXR_MASTER, FELUCCA_MASTER_COMP ? 3 : 2, "DUCK", 0, 0, 0),
          "MASTER: its insert slots (S1 FILT, S2 DYN), DYN's THRS RATIO GAIN CEIL, the returns (a card a send a slot holds: "
          "REV DLY CHO RET, the one order), DUCK (fx-rework)");
    if (FELUCCA_FX_REVERB && FELUCCA_FX_DELAY && FELUCCA_FX_CHORUS && FELUCCA_FX_DIST) {   /* the user, 2026-10-15: "match the
                                                        * order between master return and the send": card k is R(k+1) */
        uint32_t ps = 2u, ok;                           /* (T1: MIX, INSERT, SENDS) */
        fxs_load(1, FXT_CHO);                           /* (A CHO, C REV: a swap) */
        ok = mx_labels(MXR_MASTER, FELUCCA_MASTER_COMP ? 2 : 1, "CHO RET", "DLY RET", "REV RET", 0) &&
             mx_labels(MXR_T1, ps, "CHO", "DLY", "REV", "PAN") && mx_labels(MXR_LANE0 + 2u, 2, "CHO", "DLY", "REV", 0);
        fxs_load(2, FXT_NONE);                          /* (B empty: its card blank, the others in place) */
        ok &= mx_labels(MXR_T1, ps, "CHO", 0, "REV", "PAN") && mx_labels(MXR_LANE0 + 2u, 2, "CHO", 0, "REV", 0);
        fxs_set(FXS_DEF);
        check(ok, "A CHO, C REV (swapped): the returns, a track's and a lane's sends follow, CHO DLY REV; B empty: a blank card");
    }
    {   /* the master's TYPE card: DYN out of its slots, its set gone; DUST / FILT never a card of their own */
        mitem_t it[MX_MAXI];
        uint32_t n, i, own = 0;
        mst_put((const uint8_t[2]){FXT_NONE, FXT_NONE});
        n = mx_items(MXR_MASTER, it);
        for (i = 0; i < n; i++)
            own |= it[i].kind == MI_GLOB && (it[i].id == G_DUST || it[i].id == G_FILT || it[i].id == G_CTHR);
        check(!own, "MASTER: no DUST FILT card, and no DYN set while no master slot holds DYN");
        mst_defaults();
    }
    {   /* MASTER's Returns (2026-10-12; batch D: REV DLY CHO, a card a send in a slot, the master FX's RETURNS order) */
        cell_t c;
        char h[40];
        int16_t r0 = song.g[G_RRET], c0 = song.g[G_CRET], d0 = song.g[G_DRET];
        uint32_t row0 = ui.row[SCR_HOME], k, any = 0;
        const uint32_t rs = FELUCCA_MASTER_COMP ? 2u : 1u;   /* (the returns' set: after the compressor's) */
        ui.row[SCR_HOME] = MXR_MASTER;
        mx.set = (uint8_t)rs;
        head_title(SCR_HOME, MXR_MASTER, h, sizeof h);
        check(!strcmp(h, "Returns"), "MASTER's last set: the header names it (Returns, under the MASTER badge)");
        mix_cell(MXR_MASTER, 0, &c);
        check(c.d == &GP[G_RRET] && !strcmp(c.val, "100") && c.d->def == 127, "... KNOB 1 REV RET, at unity (100 %) by default");
        mix_cell(MXR_MASTER, 3, &c);
        check(!c.d && !c.label && !c.val[0], "... KNOB 4: no send left, the knob's frame alone (no \"-\": the audit's #7)");
        turn(EN_K1, -5);
        check(song.g[G_RRET] < r0 && song.g[G_CRET] == c0 && song.g[G_DRET] == d0, "... KNOB 1 turned: REV RET down (song.g), the others kept");
        turn(EN_K4, -5);
        check(song.g[G_CRET] == c0 && song.g[G_DRET] == d0, "... KNOB 4 on the empty card: nothing");
        turn(EN_K3, -3);
        check(song.g[G_CRET] < c0, "... KNOB 3 (CHO RET): CHO RET down");
        fxs_load(1, FXT_NONE);                          /* REV in no slot: no card for its return, DLY and CHO move up */
        for (k = 0; k < 4u; k++) {
            mix_cell(MXR_MASTER, k, &c);
            any |= c.d == &GP[G_RRET];
        }
        check(!any && mx_labels(MXR_MASTER, rs, "DLY RET", "CHO RET", 0, 0), "... REV in no slot: no card for its return");
        fxs_set(FXS_DEF);
        song.g[G_RRET] = r0, song.g[G_CRET] = c0, song.g[G_DRET] = d0;
        ui.row[SCR_HOME] = (uint8_t)row0;
        mx.set = 0;
    }
    mx.set = 0;
    turn(EN_SELECT, 1);
    {
        char h[40];
        head_title(SCR_HOME, mx_row(), h, sizeof h);
        check(mx_set(MXR_T1) == 1u && !strcmp(h, "Insert"), "SELECT: the next knob set; the header names the set (Insert; the badge the row)");
    }
    ui.force = 1;
    frame();
    ppm("opt-mixer-h-set2");
    turn(EN_SELECT, 20);
    check(mx_set(MXR_T1) == last, "SELECT stops at the last set");
    turn(EN_SELECT, -20);
    check(mx_set(MXR_T1) == 0u, "... and at the first");
    for (n = 0; n < mx_sets(MXR_T1) && mx_set(MXR_T1) != last; n++)
        turn(EN_SELECT, 1);
    check(mx_set(MXR_T1) == last && n + 1u == mx_sets(MXR_T1), "SELECT alone walks every knob set (2026-10-12: GLO tapped no longer does)");
    turn(EN_SELECT, -20);
    tap(B_GLO);
    check(ui.scr == SCR_GLOBAL && mx_set(MXR_T1) == 0u, "GLO tapped on the mixer: GLOBAL, the mixer's set untouched");
    tap(B_HOME);
    check(ui.scr == SCR_HOME, "... HOME tapped: back to the mixer");
    mx.set = 0;
    {   /* the knobs on the selected row */
        int16_t lv;
        turn(EN_ALGO, 1);                               /* T2 */
        lv = trk[1].p[P_LEVEL];
        turn(EN_K1, -3);
        check(trk[1].p[P_LEVEL] < lv && ui.hot == 0u, "T2's row: KNOB 1 its VOLUME (the MUTE card is gone, 2026-10-14)");
        trk[1].p[P_LEVEL] = lv;
        ui.force = 1;
        frame();
        ppm("opt-mixer-h-t2");
        while (mx_row() < MXR_LANE0 + 1u)
            turn(EN_ALGO, 1);
        lv = dl.ofs[1][DE_LEVEL];
        turn(EN_K1, -2);
        check(dl.ofs[1][DE_LEVEL] < lv && lane_selected() == 1u, "a lane's row: KNOB 1 its LEVEL (the sound's offset)");
        dl.ofs[1][DE_LEVEL] = (int8_t)lv;
        tap(B_SAVE);
        check(ui.scr == SCR_PROJECT && lane_selected() == 1u, "SAVE on a lane's row, a cell turned: PROJECT (no YES on the mixer, D6)");
        tap(B_HOME);
        tap(B_EDIT);
        check(ui.scr == SCR_SOUND && snd_page(ui.row[SCR_SOUND]) == 0 && lane_selected() == 1u,
              "... EDIT: the SOUND row of that lane (D7)");
        reset_ui();
        mx.set = 0;
        ui.hot_lit = 0;
        tap(B_SAVE);
        check(ui.scr == SCR_PROJECT && !op_armed(), "SAVE tapped on the mixer, no cell picked: PROJECT");
        tap(B_HOME);
        check(ui.scr == SCR_HOME, "... HOME: back to the mixer");
        press(B_OCTDN);
        frames(HOLD_FRAMES);
        release(B_OCTDN);
        check(ui.scr == SCR_HOME, "SHIFT held alone past HOLD on the mixer: nothing (SYSTEM is in GLO's cycle: D1)");
        tap(B_HOME);
        check(ui.scr == SCR_SCOPE, "HOME tapped: still the scope (SCOPE keeps its entry)");
        tap(B_HOME);
        song.sel = TRK_DRUM;
        reset_ui();
        song.sel = TRK_DRUM;
        frame();
        press(B_OCTDN);
        frames(HOLD_FRAMES);
        key(4);
        release(B_OCTDN);
        check(ui.scr == SCR_HOME && lane_selected() != lane_of_key(4) && smute_lane_on(lane_of_key(4)),
              "SHIFT held long + a key: the lane's sequencer muted (the pick is not here), no SYSTEM");
        smute_lane = 0;
        press(B_OCTDN);
        frames(HOLD_FRAMES);
        press(B_GLO);
        release(B_GLO);
        release(B_OCTDN);
        check(ui.scr == SCR_HOME && lay.lock == LY_MIX, "SHIFT held long + a layer button: the layer locks, no SYSTEM");
        tap(B_SEQ);
        song.sel = 0;
        reset_ui();
        mx.set = 1;
        check(!mix_find("FX ON") && !mix_find("SOUND"), "the mixer has no FX ON and no SOUND card (the user, 2026-10-14)");
        tap(B_SAVE);
        check(ui.scr == SCR_PROJECT, "... SAVE then: PROJECT");
        tap(B_HOME);
        trk[0].p[P_FXOFF] = lv;
        mx.set = 0;
    }
    {   /* every set of every row: its values have forms */
        uint32_t r, s, before = forms_bad;
        for (r = 0; r < MXR_N; r++) {
            ui.row[SCR_HOME] = (uint8_t)r;
            for (s = 0; s < mx_sets(r); s++) {
                mx.set = (uint8_t)s;
                forms_of(SCR_HOME);
            }
        }
        check(forms_bad == before, "every knob set of every mixer row: each value with its form");
        mx.set = 0;
        reset_ui();
    }
}
static void mixer_rows_tests(void)
{
    track_t *dr = &trk[TRK_DRUM];
    uint32_t y;
    reset_ui();
    memset(&dr->dstep[0], 0, sizeof dr->dstep[0]);
    dstep_set(&dr->dstep[0], 2u, 2u, 0);                    /* lane 2 (snare) on step 1 */
    while (mx_row() < MXR_DR)
        turn(EN_ALGO, 1);
    ui.force = 1;
    frame();
    y = mx_row_y(MXR_DR);
    check(px_in(MXL_VX, y + MXL_SY, 11, MXL_SH, trk_col(TRK_DRUM)), "DR's sequence: every lane merged (the snare's step 1)");
    turn(EN_ALGO, 3);
    frame();
    y = mx_row_y(MXR_LANE0 + 2u);
    check(mx_row() == MXR_LANE0 + 2u && px_in(MXS_VX, y + MXS_SY, 5, MXS_SH, lane_col(2)), "the snare lane's cell: its own hit");
    y = mx_row_y(MXR_LANE0 + 3u);
    check(y + MXS_SY + MXS_SH <= 240u && !px_in(MXS_VX, y + MXS_SY, 5, MXS_SH, lane_col(3)), "the clap lane's cell: none");
    ppm("opt-mixer-sounds");
    memset(&dr->dstep[0], 0, sizeof dr->dstep[0]);
    song.sel = 0;
    reset_ui();
    {   /* a track's VU while it plays */
        uint32_t i, seen = 0;
        for (i = 0; i < 8u; i++) {
            mt.ui[0] = 12000;                           /* (the meters' main-loop side is not in the host: a peak) */
            frame();
            seen |= (uint32_t)px_in(MXL_VX, mx_row_y(MXR_T1) + MXL_VY, 40, MXL_VH, C_OK);
        }
        frames(30);
        check(seen, "T1's row: its VU meter fills from the left while it plays");
    }
#if FELUCCA_MASTER_COMP
    {   /* the compressors' reduction: a bar from the right, only where one works */
        uint32_t x = MXM_GX + MXM_GW;
        turn(EN_ALGO, -1);                              /* MASTER, alone */
        song.g[G_CTHR] = -10;
        mc_gr_view = -6;                                /* 6 dB: 18 px from the right */
        frame();
        y = OP_PY + MXM_GY;
        check(px_in(x - 17u, y, 4, MXM_GH, C_WARN) && !px_in(x - 22u, y, 3, MXM_GH, C_WARN),
              "MASTER alone: the compressor's 6 dB as an amber bar pushing in from the right (18 px)");
        ui.force = 1;
        frame();
        ppm("opt-mixer-h-master-gr");
        song.g[G_CTHR] = 0, song.g[G_CCEIL] = 0;        /* (CEIL is on by default since 2026-10-11) */
        frame();
        check(!px_in(MXM_GX, y, MXM_GW, MXM_GH, C_WARN), "THRS and CEIL off: no compressor, no bar");
        song.g[G_CCEIL] = GP[G_CCEIL].def;
        mc_gr_view = 0;
        turn(EN_ALGO, 1);
        ins_load(0, 1, FXT_COMP);                       /* T1's COMP insert (its S2), reducing 8 dB */
        trk[0].p[P_TCOMP] = 60;
        tcomp[0].gr16 = (int32_t)(8 * 65536 / 6.02);
        ui_draw();                                      /* (no audio block between: the reduction as set) */
        y = mx_row_y(MXR_T1);
        x = MXL_VX + MXL_VW;
        check(px_in(x - 23u, y + MXL_VY, 4, MXL_VH, C_WARN) && !px_in(MXL_VX, mx_row_y(MXR_T1 + 1u) + MXL_VY, MXL_VW, MXL_VH, C_WARN),
              "T1's COMP insert: its reduction on T1's row (8 dB: 24 px), none on T2's");
        trk[0].p[P_TCOMP] = 0;
        frame();
        check(!px_in(MXL_VX, y + MXL_VY, MXL_VW, MXL_VH, C_WARN), "its amount 0: no bar");
        tcomp[0].gr16 = 0;
        ins_defaults();
    }
#endif
    {   /* the GLO layer's four levels, as before */
        int16_t lv = trk[2].p[P_LEVEL];
        press(B_GLO);
        frames(HOLD_FRAMES);
        turn(EN_K3, -2);
        release(B_GLO);
        check(lay.held == LY_PLAY && trk[2].p[P_LEVEL] < lv && ui.scr == SCR_HOME, "GLO held: KNOB 3 T3's level (the balance)");
        trk[2].p[P_LEVEL] = lv;
    }
    {   /* the mixer keeps its 1x4 cards: the list keeps the four tracks in view */
        reset_ui();
        ui.force = 1;
        frame();
        check(!cards_2x2() && MXL_H == (OP_FOOT ? 36 : 41) && mxd.first == MXR_T1 && !px_at(30u, OY_CARD + BIG_H - 1u, C_LINE),
              "the mixer keeps the 1x4 cards and its four rows of 41 (36) px (every other screen is 2x2)");
        ppm("opt-mixer-h-2x2");
        reset_ui();
    }
}
static void mixer_tests(void)
{
    mixer_walk_tests();
    mixer_sets_tests();
    mixer_rows_tests();
}

static void scope_tests(void)
{
    uint32_t r, w0, i, s;
    int32_t pk;
    reset_ui();
    song.sel = 1;
    fm1_in.notes |= 1u << 10;                           /* (a note held from the mixer: SCOPE's keys play nothing, 2026-10-13) */
    frames(3);
    tap(B_HOME);
    check(ui.scr == SCR_SCOPE && scope_rows() == 1u && scope_src == 2u && !ui.master, "SCOPE: HOME tapped on the mixer; the source is the selected track (T2), one page");
    (void)r;
    /* one ring, switched: T2 plays a held note, T1 is silent, the drums too */
    turn(EN_SELECT, 2);                                 /* (SELECT pages the scope, which has one page: nothing moves) */
    frames(3);
    for (pk = 0, i = 0; i < VIS_RING; i++)
        pk = vis_pcm[0][i] > pk ? vis_pcm[0][i] : pk;
    check(scope_src == 2u && pk > 0, "SCOPE T2: the ISR copies part 2's block into the ring (a note held)");
    ui.force = 1;
    frame();
    frame();
    ppm("opt-scope-t2");
    check(px_in(0, (uint32_t)OP_PY, 240, SC_H, trk_col(1)),
          "SCOPE T2: the trace in T2's colour (no tabs under the 2x2 cards: the header names it)");
    turn(EN_ALGO, -1);                                  /* T1: silent, nothing written */
    frames(2);
    w0 = vis_wr;
    frames(2);
    check(scope_src == 1u && vis_wr == w0, "SCOPE T1 (silent): the ring is not written");
    turn(EN_ALGO, 3);                                   /* DR: the drums' part of the mix only */
    frames(3);
    for (pk = 0, i = 0; i < VIS_RING; i++)
        pk = (vis_pcm[0][i] < 0 ? -vis_pcm[0][i] : vis_pcm[0][i]) > pk ? (vis_pcm[0][i] < 0 ? -vis_pcm[0][i] : vis_pcm[0][i]) : pk;
    check(scope_src == SCOPE_DR && vis_wr != w0 && pk < 64, "SCOPE DR: the drums' part (silent here), not T2's note");
    turn(EN_ALGO, -4);                                  /* MASTER: the master output, still on SCOPE */
    frames(3);
    for (pk = 0, i = 0; i < VIS_RING; i++)
        pk = vis_pcm[0][i] > pk ? vis_pcm[0][i] : pk;
    ui.force = 1;
    frames(2);
    ppm("opt-scope-master");
    check(ui.scr == SCR_SCOPE && ui.master && scope_src == 0u && pk > 0 && px_in(0, OY_PANEL, 240, SC_H, C_HI), "SCOPE MASTER (ALGORITHM left of T1): the mix, in the palette's ink, still on SCOPE");
    fm1_in.notes &= ~(1u << 10);
    {
        cell_t c;
        scope_cell(0, 3, &c);                           /* (ui.master: the master's) */
        check(c.label && (strcmp(c.label, "GR") == 0) == (FELUCCA_MASTER_COMP != 0), "SCOPE MASTER's cards: FILT THRS RATIO GR");
        ui.master = 0;
        song.sel = 1;
        scope_cell(0, 0, &c);                           /* (the source is the walk's: T2) */
        check(c.d == &TP[P_LEVEL] && c.vp == &trk[1].p[P_LEVEL], "SCOPE T2's cards: the mixer's LEVEL PAN FX DRIVE");
    }
    /* the trigger: a sine written into the ring starts at a rising zero crossing */
    for (i = 0; i < VIS_RING; i++)
        vis_pcm[0][i] = vis_pcm[1][i] = (int32_t)(sin((double)(i + 37u) * 2.0 * 3.14159265 / 100.0) * 100000.0);
    s = sc_trigger(0, 12500);
    check(s > 0 && sc_at(0, s - 1u) < 0 && sc_at(0, s) >= 0 && s + SC_N * SC_DEC <= VIS_RING,
          "SCOPE: the trace starts at a rising zero crossing that leaves a whole screen");
    tap(B_HOME);
    check(ui.scr == SCR_HOME && (frame(), scope_src == 0u), "HOME on SCOPE: the mixer; the source back to the master");
    song.sel = 0;
}

/* the header's badge only on pages whose values belong to the selected track (op_draw.c HEAD_TRK_*): drawn header, pixel by pixel */
static int head_badge_drawn(void)
{
    ui.sig[0] = 0;
    draw_head();
    return px_in(2, 3, 20, 19, trk_col(song.sel));
}
static void header_track_tests(void)
{
    static const uint8_t SCR_WANT[SCR_N] = {[SCR_HOME] = 1, [SCR_SOUND] = 1, [SCR_STEP] = 1};   /* (the mixer on a track row: its badge) */
    uint32_t s, l, bad = 0;
    reset_ui();
    song.sel = 0;
    frames(120);                                        /* (the track-name flash over) */
    for (s = 0; s < SCR_N; s++) {
        ui.scr = (uint8_t)s;
        ui.master = 0;
        lay.shown = LY_PLAY;
        if (head_badge_drawn() != SCR_WANT[s]) {
            printf("  screen %s: badge %d, wanted %d\n", SCR_NAME[s], !SCR_WANT[s], SCR_WANT[s]);
            bad++;
        }
        check(head_has_track() == SCR_WANT[s], "the screen's flag (the mixer on a track, SOUND, STEPS: the track; the rest: none)");
    }
    check(!bad, "each non-track screen draws no track badge, each track screen does");
    ui.scr = SCR_SOUND;
    ui.master = 1;
    check(!head_badge_drawn(), "MASTER (SOUND on the master FX): no track badge");
    ui.master = 0;
    for (l = LY_FX; l < LY_COUNT; l++) {
        static const uint8_t LY_WANT[LY_COUNT] = {[LY_ERASE] = 1, [LY_STEP] = 1, [LY_SCALE] = 1, [LY_OPS] = 1};
        lay.shown = (uint8_t)l;
        if (head_badge_drawn() != LY_WANT[l]) {
            printf("  layer %u: badge %d, wanted %d\n", l, !LY_WANT[l], LY_WANT[l]);
            bad++;
        }
    }
    check(!bad, "layers: ERASE, STEP keys, KEY, FM6 OPS show the track; FX, REPEAT, MIX, SCENES, PATTERNS do not");
    lay.shown = LY_PLAY;
    ui.scr = SCR_HOME;
    ui.sig[0] = 0;
    reset_ui();
}

static void header_footer_tests(void)
{
    char t[32];
    reset_ui();
    op_enter(SCR_SOUND);
    frame();
    check(!strcmp(head_engine(), ENGINES[trk[0].eng_req % NENGINES]->name) &&
              px_in(2, 3, 20, 19, trk_col(0)), "the header: the selected track's engine in its colour (no T1 badge)");
    song.sel = TRK_DRUM;
    frame();
    check(!strcmp(head_engine(), "DRUMS") && px_in(2, 3, 20, 19, trk_col(TRK_DRUM)), "the drum track: DRUMS, its kit's colour");
    tap(B_SEQ);
    lane_select(4);
    ui.force = 1;
    frame();
    ppm("opt-step-head-pick");
    check(px_in(60, 4, 170, 16, C_HI), "STEP: the header says the page (Pattern 1/N; #17: the pick is the grid's lit lane)");
    tap(B_HOME);
    song.sel = 0;
    tap(B_EDIT);
    tap(B_EDIT);
    ui.force = 1;
    frame();
    check(px_in(0, 202, 240, 38, C_GRAY) || px_in(0, 202, 240, 38, C_HI),
          "no footer: SOUND's rows go to the screen's foot");
    op_enter(SCR_HOME);
    press(B_FX);
    frames(HOLD_FRAMES);
    check(lay_hint_t > 0 && ui.msg_t == 0, "#24: a layer opened: \"Home locks it\" on the help line, never in the header");
#if FELUCCA_PARAM_HELP
    frame();
    check(hl.on, "#24: the help line shows the layer's hint");
#endif
    release(B_FX);
    frames(2);
    check(lay_hint_t == 0, "the layer let go: its hint with it");
    press(B_FX);
    frames(HOLD_FRAMES);
    check(lay_hint_t == 0 && ui.msg_t == 0, "the hint once a power-on");
    release(B_FX);
    frames(2);
}
