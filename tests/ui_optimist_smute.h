/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, the user's rulings of 2026-10-13 and 2026-10-16: SHIFT held + a key MUTES THE SEQUENCER
 * (seq_mute.c: the black keys the 10 lanes, F#5 ACCENT, the first 4 white keys T1 T2 T3 DR), the one mute, on every page
 * with keys; STEP's white keys stay steps (SHIFT + a step: its locks cleared or a ghost hit). The row shows it (grey
 * steps, the M badge), the keys light it with SHIFT held. Included after ui_optimist_mute.h. */
static uint32_t smute_black(uint32_t b)                 /* track b's key (0..3): the white keys F G A B (2026-10-16: the black
                                                         * keys are the lanes') */
{
    return white_key(b);
}
static void smute_clear(void)
{
    uint32_t i;
    smute_trk = 0;
    smute_lane = 0;
    for (i = 0; i < NTRK; i++)
        trk[i].p[P_MUTE] = 0;
    for (i = 0; i < DRUM_LANES; i++)
        dlm_set_mute(i, 0);
}
static void smute_tests(void)
{
    uint32_t f, d, h, y, i;
    smute_clear();
    reset_ui();
    song.sel = TRK_DRUM;
    lane_select(0);
    go_home();
    frames(2);
    press(B_OCTDN);
    key(key_of_lane(2));                                /* (the third drum key: lane 2) */
    check(smute_lane_on(2) && smute_lane == 4u && smute_trk == 0u && lane_selected() == 0u && ui.scr == SCR_HOME,
          "the mixer, SHIFT held + the third drum key: lane 2's sequencer muted, no pick, still the mixer");
    check(!dlm_muted(2) && !TDRUM->p[P_MUTE], "... no sound mute (one mute: the sequencer's)");
    key(key_of_lane(2));
    check(!smute_lane_on(2), "... the key again: unmuted");
    key(key_of_lane(9));
    key(smute_black(1));
    check(smute_lane == 0x200u && smute_trk == 2u, "the 10th lane's key: lane 10; the second white key (G): T2");
    {   /* the mute is the sequencer's alone: no track's volume, pan, sound mute or send moved (the user, 2026-10-21: "it should not touch the volume") */
        int16_t lv[NTRK][4];
        uint32_t t;
        for (t = 0; t < NTRK; t++)
            lv[t][0] = trk[t].p[P_LEVEL], lv[t][1] = trk[t].p[P_PAN], lv[t][2] = trk[t].p[P_MUTE], lv[t][3] = trk[t].p[P_REV];
        key(smute_black(0));
        key(smute_black(0));
        key(smute_black(2));
        for (t = 0; t < NTRK; t++)
            check(lv[t][0] == trk[t].p[P_LEVEL] && lv[t][1] == trk[t].p[P_PAN] && lv[t][2] == trk[t].p[P_MUTE] && lv[t][3] == trk[t].p[P_REV],
                  "SHIFT + a track key: the track's volume, pan, sound mute and send untouched (the sequencer mute alone)");
        key(smute_black(2));
    }
    key(smute_black(3));
    check(smute_trk == 10u, "the fourth white key (B): DR");
    key(white_key(5));                                  /* (a white key past the four: nothing) */
    key(white_key(9));
    check(smute_trk == 10u && smute_lane == 0x200u, "another white key: nothing");
    release(B_OCTDN);
    check(ui.scr == SCR_HOME && song.octave == 0, "OCT- let go after keys: no octave (the mixer stays)");
    smute_clear();
    song.sel = 0;
    lane_select(0);
    reset_ui();
    frames(2);
    press(B_OCTDN);
    key(key_of_lane(3));
    check(smute_lane_on(3) && lane_selected() == 0u, "a synth track selected: the same, whatever the track");
    release(B_OCTDN);
    smute_clear();

    /* the rows: the steps grey, the M badge */
    song.sel = 0;
    reset_ui();
    go_home();
    for (i = 0; i < 4u; i++)
        dstep_set(&TDRUM->dstep[i], 0, LV_NORM, 0);
    frames(2);
    y = mx_row_y(MXR_T1 + 1u);
    check(!px_in(6, y + 18u, 8, 14, C_WARN), "T2 not muted: no M badge");
    smute_set_trk(1, 1);
    frames(2);
    ppm("opt-smute-t2");
    check(px_in(6, y + 18u, 8, 14, C_WARN) && !px_in(15, y + 18u, 8, 14, C_GRAY), "T2's sequencer muted: the M badge (one mute), no X");
    smute_set_trk(1, 0);
    frames(2);
    check(!px_in(6, y + 18u, 8, 14, C_WARN), "... unmuted: the badge gone");

    /* the keys' lights, SHIFT held */
    smute_clear();
    reset_ui();
    go_home();
    smute_set_lane(2, 1);                               /* (after reset_ui: it clears the mutes) */
    smute_set_trk(0, 1);
    frames(2);
    keys_lit(&f, &d, &h);
    check(!f && !d, "the mixer, no SHIFT: the keys dark");
    press(B_OCTDN);
    frames(HOLD_FRAMES);
    keys_lit(&f, &d, &h);
    {
        uint32_t lk = 0;
        for (i = 0; i < DRUM_LANES; i++)
            lk |= 1u << key_of_lane(i);
        uint32_t all = lk | keys_white(4) | (ACC_UI ? 1u << DACC_KEY : 0u), rets = (1u << white_key(13)) | (1u << white_key(14)) | (1u << white_key(15));
        uint32_t muted = (1u << key_of_lane(2)) | (1u << smute_black(0));
        memset(mx.lit, 0, sizeof mx.lit);
        memset(mx.llit, 0, sizeof mx.llit);
        memset(mx.tlit, 0, sizeof mx.tlit);
        trk_hits = smute_thits = 0;
        smute_lhits = 0;
        keys_lit(&f, &d, &h);
        check(f == rets && d == 0u && h == (all & ~muted),
              "SHIFT held, no note starting: the muted (T1, lane 2) dark, the heard 2/3, the returns' inputs (open) full (2026-10-22)");
        trk_hits = 1u << 1;                             /* (T2 starts a note) */
        drums.hits = 1u << 4;                           /* (lane 4 hit) */
        frames(1);
        keys_lit(&f, &d, &h);
        check(f == ((1u << smute_black(1)) | (1u << key_of_lane(4)) | (1u << smute_black(3)) | rets) && !(h & f) && !(d & f),
              "... a note starting on T2 and a lane hit (DR with it): their keys full, not half");
        frames(MX_FLASH);
        keys_lit(&f, &d, &h);
        check(f == rets && h == (all & ~muted), "... MX_FLASH frames later: back to 2/3 (a short flash, not the note's length)");
        smute_thits = 1u << 0;                          /* (muted T1 would play a note, muted lane 2 a hit) */
        smute_lhits = 1u << 2;
        frames(1);
        keys_lit(&f, &d, &h);
        check(f == (muted | (1u << smute_black(3)) | rets) && h == (all & ~muted & ~(1u << smute_black(3))),
              "... a muted track's and a muted lane's notes: their keys flash full too (something to unmute), DR with the lane");
        frames(MX_FLASH);
        keys_lit(&f, &d, &h);
        check(f == rets && !(h & muted) && !(d & muted), "... then dark again (muted)");
        memset(mx.llit, 0, sizeof mx.llit);
        memset(mx.lit, 0, sizeof mx.lit);
        {   /* the last three white keys: a return's input (A B C), full while open, dark while muted, no sound mute of the output */
            uint8_t rm = fx_rmute;
            uint32_t ln = fxs_line[2] < 3u ? fxs_line[2] : 0u;
            fx_imute = 0;
            fxs_lm_apply();
            check(fxs_lm[0] == 31u && fxs_lm[1] == 31u && fxs_lm[2] == 31u, "the returns' inputs open: the drum lanes' send masks full");
            key(white_key(14));                             /* (F: return B) */
            keys_lit(&f, &d, &h);
            check((fx_imute >> ln & 1u) == 1u && fx_imute == (1u << ln) && fx_rmute == rm && !((f | d | h) >> white_key(14) & 1u) && (f >> white_key(13) & 1u),
                  "SHIFT + the second of the last three white keys: return B's input muted (its output's mute untouched), its key dark, A's full");
            check(fxs_lm[2u - ln] == 0u && !FXS_IN(FXT_CHO) == (ln == 0u) && !FXS_IN(FXT_DLY) == (ln == 1u) && !FXS_IN(FXT_REV) == (ln == 2u),
                  "... no send reaches that line (the lanes' masks, the tracks' and the USB input's send gains), the others open");
            key(white_key(14));
            check(fx_imute == 0u && fxs_lm[0] == 31u && fxs_lm[1] == 31u && fxs_lm[2] == 31u, "... the key again: open");
        }
    }
    release(B_OCTDN);
    frames(2);
    keys_lit(&f, &d, &h);
    check(!f && !d, "SHIFT let go: the keys dark again");
    smute_clear();

    /* the master channel too (the user, 2026-10-22): the mixer's MASTER row and the master FX page, the keys silent */
    smute_clear();
    reset_ui();
    go_home();
    ui.row[SCR_HOME] = MXR_MASTER;
    frames(2);
    check(mx_on_master() && keys_none(), "(the mixer's MASTER row: a page with no track)");
    press(B_OCTDN);
    key(smute_black(1));
    check(smute_trk == 2u && smute_lane == 0u, "the MASTER row, SHIFT + the second white key: T2's sequencer muted");
    key(key_of_lane(2));
    check(smute_lane_on(2), "... a drum key: its lane");
    release(B_OCTDN);
    check(song.octave == 0, "... OCT- let go after keys: no octave");
    smute_clear();
    reset_ui();
    op_enter(SCR_FX);
    frames(2);
    press(B_OCTDN);
    key(smute_black(1));
    release(B_OCTDN);
    check(ui.scr == SCR_FX && smute_trk == 2u, "the master FX page, SHIFT + the second white key: T2's sequencer muted");
    smute_clear();

    /* every page with keys: the same; STEP's white keys stay steps, its black keys mute */
    reset_ui();
    song.sel = TRK_DRUM;
    lane_select(0);
    tap(B_EDIT);
    check(ui.scr == SCR_SOUND, "(the drum track's EDIT: the SOUND rows)");
    press(B_OCTDN);
    key(key_of_lane(4));
    release(B_OCTDN);
    check(lane_selected() == 0u && smute_lane_on(4) && smute_trk == 0u, "SOUND: SHIFT held + a lane's key mutes it, picks nothing");
    smute_clear();
    reset_ui();
    song.sel = TRK_DRUM;
    op_enter(SCR_STEP);
    frames(2);
    lane_select(4);
    dstep_set(&TDRUM->dstep[4], 4, LV_NORM, 0);
    press(B_OCTDN);
    key(white_key(4));
    key(key_of_lane(3));
    release(B_OCTDN);
    check(dstep_has(&TDRUM->dstep[4], 4) && !smute_trk && smute_lane_on(3) && lane_selected() == 4u,
          "STEP: SHIFT + a white key: a step (the hit stays), not a mute; SHIFT + a black key: its lane muted, no pick");
    smute_clear();
    song.sel = 0;
    reset_ui();
}
