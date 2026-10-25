/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, the mixer in the big face (builder item MIXER_2X, op_mix2x.c; the user, 2026-10-25: "Can we
 * add a double size builder flag for mixer: 2*2 cards. Condensed master meter"):
 *   on   every mixer row has the 2x2 big cards (the same four values, KNOB k = card k); under them the selected row's
 *        steps large (two lines of 8), its meter, then a thin playback bar a track (T1 T2 T3 DR), the selected track's
 *        marked; MASTER: one level / reduction line, then the FX returns, a row each in the large face with its meter
 *   off  the mixer as before: the 1x4 cards, the rows (op_mixdraw.c), nothing of op_mix2x.c built
 * Included by ui_optimist_test.c after ui_optimist_cards.h (cards_band_is) and ui_optimist_mixer.h. */
#if FELUCCA_MIXER_2X
/* the text s in the small face at x, y as the screen has it: each of its ink pixels drawn (not black) there */
static int m2t_text_at(int32_t x, int32_t y, const char *s)
{
    uint32_t w = (uint32_t)text_ws(s), i, j, ok = 1;
    cv_begin(w, 16, C_BLACK);
    cv_texts(0, 0, s, C_WHITE);
    for (j = 0; j < 16u; j++)
        for (i = 0; i < w; i++)
            if (cv_px[j * w + i] != swap16(C_BLACK))
                ok &= (uint32_t)(screen[((uint32_t)y + j) * 240u + (uint32_t)x + i] != swap16(C_BLACK));
    return (int)ok;
}
static int m2t_labels_ok(uint32_t r)                    /* the four big cards: card k's label at quadrant k */
{
    uint32_t k, ok = 1;
    for (k = 0; k < 4u; k++) {
        cell_t c;
        char l[16], b[16];
        memset(&c, 0, sizeof c);
        mix_cell(r, k, &c);
        if (!c.label)
            continue;
        ok &= (uint32_t)m2t_text_at((k & 1u ? 121 : 3) + 5, (int32_t)OY_CARD + (k & 2u ? BIG_H + 1 : 0) + 2,
                                    cut(b, card_label(&c, l, sizeof l), 6));
    }
    return (int)ok;
}
static uint32_t m2t_cell_x(uint32_t i) { return (uint32_t)(M2_SX + (int32_t)(i & 7u) * M2_SP); }
static uint32_t m2t_cell_y(uint32_t i) { return (uint32_t)(OP_PY + M2_SY + (int32_t)(i >> 3) * M2_SL); }
static uint32_t m2t_bar_y(uint32_t k) { return (uint32_t)(OP_PY + M2_BY + (int32_t)(k * M2_BP) + M2_BO); }
static uint32_t m2t_fill(uint32_t k)                    /* track k's bar as drawn: its filled width */
{
    uint32_t x = M2_BX;
    while (x < (uint32_t)(M2_BX + M2_BW) && px_at(x, m2t_bar_y(k), m2_bar_col(k)))
        x++;
    return x - M2_BX;
}
static void mixer2x_tests(void)
{
    track_t *t = &trk[0], *dr = &trk[TRK_DRUM];
    uint32_t i, k, ok, y;
    step_t keep[16];
    dstep_t dkeep[16];
    memcpy(keep, t->step, sizeof keep);
    memcpy(dkeep, dr->dstep, sizeof dkeep);
    for (i = 0; i < 16u; i++)                           /* T1: notes on 1, 6, 11 (a second line's), the rest rests */
        t->step[i].time = ST_REST, t->step[i].n = 0;
    for (i = 0; i < 16u; i += 5u)
        t->step[i].time = ST_NOTE, t->step[i].n = 1, t->step[i].note[0] = 60, t->step[i].vel = 100, t->step[i].lvl = LV_NORM;
    memset(dr->dstep, 0, sizeof dkeep);                 /* DR: the snare on 1 (SOFT), 3 (GHOST), 10 (HARD) */
    dstep_set(&dr->dstep[0], 2u, 2u, 0);
    dstep_set(&dr->dstep[2], 2u, LV_GHOST, 0);
    dstep_set(&dr->dstep[9], 2u, LV_HARD, 0);
    reset_ui();
    song.sel = 0;
    ui.force = 1;
    frame();
    frame();
    check(mx_row() == MXR_T1 && cards_2x2() && OP_PY == OY_PANEL_2X2 && cards_band_is(BAND_2X2) && m2t_labels_ok(MXR_T1),
          "MIXER_2X: a channel row has the 2x2 big cards, each card's label in its knob's quadrant");
    ok = 1;
    mx.set = 1;                                         /* (the inserts' set: four cards; the mix set has two) */
    for (k = 0; k < 4u; k++) {                          /* KNOB k + 1 is card k: hot, white in its quadrant */
        cell_t c;
        mix_cell(MXR_T1, k, &c);
        ok &= c.label != 0;
        turn((int)(EN_K1 + k), 1);
        frames(200);                                    /* (a list value's popup gone: the cards drawn again) */
        ui.force = 1;
        frame();
        ok &= ui.hot == k && px_in((k & 1u ? 121u : 3u) + 5u, OY_CARD + (k & 2u ? BIG_H + 1u : 0u) + 13u, 100, 28, C_WHITE);
        turn((int)(EN_K1 + k), -1);
    }
    frames(200);
    ui.force = 1;
    frame();
    ppm("opt-mixer2x-t1-set2");
    ok &= m2t_labels_ok(MXR_T1);
    mx.set = 0;
    check(ok, "MIXER_2X: KNOB 1..4 turn cards 1..4 (top left, top right, bottom left, bottom right), the hot one white");
    ui.force = 1;
    frame();
    ok = px_in(m2t_cell_x(0) + 2u, m2t_cell_y(0) + 2u, 20, (uint32_t)M2_SH - 4u, lvl_col(trk_col(0), LV_NORM)) &&
         px_in(m2t_cell_x(10) + 2u, m2t_cell_y(10) + 2u, 20, (uint32_t)M2_SH - 4u, lvl_col(trk_col(0), LV_NORM)) &&
         !px_in(m2t_cell_x(1) + 2u, m2t_cell_y(1) + 2u, 20, (uint32_t)M2_SH - 4u, lvl_col(trk_col(0), LV_NORM)) &&
         px_at(m2t_cell_x(1), m2t_cell_y(1) + 5u, C_LINE) && M2_SH >= 16 && M2_SP >= 29;
    check(ok, "MIXER_2X: the selected track's steps large (two lines of 8, 27 x 16 px or more): its notes filled, a rest an outline");
    ok = px_at(M2_SX + 2u, (uint32_t)OP_PY + M2_BY + 2u, C_WHITE) || px_in(M2_SX, (uint32_t)OP_PY + M2_BY, 16, 10, C_WHITE);
    for (k = 1; k < NTRK; k++)                          /* (the others' codes in their colours, none white) */
        ok &= px_in(M2_SX, (uint32_t)OP_PY + M2_BY + k * M2_BP, 16, 10, trk_col(k)) &&
              !px_in(M2_SX, (uint32_t)OP_PY + M2_BY + k * M2_BP, 16, 10, C_WHITE);
    for (k = 0; k < NTRK; k++)                          /* (every bar's trough) */
        ok &= px_in(M2_BX, m2t_bar_y(k) + M2_BH - 1u, M2_BW, 1, C_LINE);
    ok &= m2t_text_at(M2_SX, (int32_t)OP_PY + M2_BY - 2, "T1") && m2t_text_at(M2_SX, (int32_t)OP_PY + M2_BY + 3 * M2_BP - 2, trk_tag(TRK_DRUM));
    check(ok && OP_PY + M2_BY + 4 * M2_BP <= OP_FOOT_Y, "MIXER_2X: a bar a track under the steps, T1 T2 T3 DR named, T1's code white (selected), all above the help band");
    ppm("opt-mixer2x-t1");
    while (mx_row() < MXR_DR)
        turn(EN_ALGO, 1);
    ui.force = 1;
    frame();
    ok = px_in(m2t_cell_x(0) + 2u, m2t_cell_y(0) + 2u, 20, (uint32_t)M2_SH - 4u, lvl_col(trk_col(TRK_DRUM), 2u)) &&
         px_in(m2t_cell_x(2) + 2u, m2t_cell_y(2) + 2u, 20, (uint32_t)M2_SH - 4u, lvl_col(trk_col(TRK_DRUM), LV_GHOST)) &&
         px_in(m2t_cell_x(9) + 2u, m2t_cell_y(9) + 2u, 20, (uint32_t)M2_SH - 4u, lvl_col(trk_col(TRK_DRUM), LV_HARD)) &&
         !px_in(m2t_cell_x(1), m2t_cell_y(1), (uint32_t)M2_SP - 2u, (uint32_t)M2_SH, C_LINE) &&   /* (an empty drum step: a dot, no box) */
         px_in(m2t_cell_x(1), m2t_cell_y(1), (uint32_t)M2_SP - 2u, (uint32_t)M2_SH, C_DIM) &&
         px_in(M2_SX, (uint32_t)OP_PY + M2_BY + 3u * M2_BP, 16, 10, C_WHITE) &&
         !px_in(M2_SX, (uint32_t)OP_PY + M2_BY, 16, 10, C_WHITE) && cards_band_is(BAND_2X2) && m2t_labels_ok(MXR_DR);
    check(ok, "MIXER_2X on DR: the lanes merged large (SOFT, GHOST, HARD shaded), the empty steps dots, DR's code white, its cards 2x2");
    ppm("opt-mixer2x-dr");
    turn(EN_ALGO, 3);                                   /* the snare lane */
    ui.force = 1;
    frame();
    ok = mx_row() == MXR_LANE0 + 2u && px_in(m2t_cell_x(0) + 2u, m2t_cell_y(0) + 2u, 20, (uint32_t)M2_SH - 4u, lvl_col(lane_col(2), 2u)) &&
         px_in(m2t_cell_x(9) + 2u, m2t_cell_y(9) + 2u, 20, (uint32_t)M2_SH - 4u, lvl_col(lane_col(2), LV_HARD)) &&
         px_in(M2_SX, (uint32_t)OP_PY + M2_BY + 3u * M2_BP, 16, 10, C_WHITE) && cards_band_is(BAND_2X2) && m2t_labels_ok(mx_row());
    check(ok, "MIXER_2X on a lane: the lane's own hits large in its colour, the four track bars, DR's marked (the lanes play DR's length)");
    ppm("opt-mixer2x-lane");
    turn(EN_ALGO, 1);                                   /* the clap lane: no hit */
    ui.force = 1;
    frame();
    check(!px_in(m2t_cell_x(0), m2t_cell_y(0), 240, (uint32_t)M2_SL * 2u, lane_col(3)), "MIXER_2X: the clap lane, no hit: no cell in its colour");
    reset_ui();
    song.sel = 0;
    {   /* playing: every bar's fill moves; stopped: empty */
        uint32_t f0[NTRK], f1[NTRK], moved = 1, at0;
        ui.force = 1;
        frame();
        ok = 1;
        for (k = 0; k < NTRK; k++)
            ok &= m2t_fill(k) == 0u;
        check(ok, "MIXER_2X stopped: the bars empty");
        tap(B_PLAY);
        frames(6);
        for (k = 0; k < NTRK; k++)
            f0[k] = m2t_fill(k);
        at0 = t->seq_idx;
        frames(20);
        for (k = 0; k < NTRK; k++) {
            f1[k] = m2t_fill(k);
            moved &= f1[k] != f0[k] && f1[k] > 0u;
        }
        ui.force = 1;
        frame();
        ppm("opt-mixer2x-playing");
        check(song.playing && t->seq_idx != at0 && moved, "MIXER_2X playing: each track's bar fills with its playback, in its colour");
        y = (uint32_t)t->seq_idx % (uint32_t)ui_clamp(t->p[P_SLEN], 1, NSTEP) % 16u;
        check(px_in(m2t_cell_x(y) + 2u, m2t_cell_y(y) + 2u, 20, (uint32_t)M2_SH - 4u, C_WHITE), "MIXER_2X playing: the playhead's step white");
        tap(B_PLAY);
        frames(4);
        ok = 1;
        for (k = 0; k < NTRK; k++)
            ok &= m2t_fill(k) == 0u;
        check(!song.playing && ok, "MIXER_2X stopped again: the bars empty");
    }
    {   /* a muted track: its code struck, its bar grey */
        smute_trk = 1u << 1;
        ui.force = 1;
        frame();
        check(mx_muted(MXR_T1 + 1u) && m2_bar_col(1) == C_DIM, "MIXER_2X: a muted track's bar grey");
        smute_trk = 0;
    }
    reset_ui();
    turn(EN_ALGO, -1);                                  /* MASTER */
    ui.force = 1;
    frame();
    {
        uint8_t ret[3];
        uint32_t n = mx_rets(ret);
        static const char *const RN[3] = {"CHO", "DLY", "REV"};
        ok = mx_row() == MXR_MASTER && cards_band_is(BAND_2X2) && n == 3u &&
             px_in(M2M_VX, (uint32_t)OP_PY + M2M_VY + M2M_VH - 1u, M2M_VW, 1, C_LINE) &&
             !px_in(0, (uint32_t)OP_PY + M2M_VY + M2M_VH, 240, 6, C_LINE);   /* (one line: no dB scale's ticks under it) */
        for (i = 0; i < n; i++) {
            y = (uint32_t)(OP_PY + M2M_RY + (int32_t)i * M2M_RP);
            ok &= px_in(M2M_VX, y, 40, 20, C_HI) && !strcmp(fxs_line_name(ret[i]), RN[i]) &&
                  px_in(M2M_RX, y + M2M_RO + M2M_RH - 1u, M2M_RW, 1, C_LINE);
        }
        check(ok && OP_PY + M2M_RY + 3 * M2M_RP <= OP_FOOT_Y + 2,
              "MIXER_2X MASTER: the 2x2 cards, one level line (no scale), the returns CHO DLY REV large, each with its meter");
        mt.ret[2] = 12000;                              /* REV's return loud, CHO's and DLY's silent */
        mt.ret[1] = mt.ret[0] = 0;
        memset(fx_ret_pk, 0, sizeof fx_ret_pk);
        ui_draw();
        y = (uint32_t)(OP_PY + M2M_RY + M2M_RO);
        check(px_in(M2M_RX, y + 2u * M2M_RP, 40, M2M_RH, C_OK) && !px_in(M2M_RX, y + M2M_RP, 40, M2M_RH - 1u, C_OK) &&
              !px_in(M2M_RX, y, 40, M2M_RH - 1u, C_OK), "MIXER_2X MASTER: the REV return's meter fills, CHO's and DLY's stay empty");
        mt.ret[2] = 0;
#if FELUCCA_MASTER_COMP
        song.g[G_CTHR] = -10;
        mc_gr_view = -6;                                /* 6 dB: 18 px from the right, in the level's line */
        frame();
        ui.force = 1;
        frame();
        check(px_in(M2M_VX + M2M_VW - 17u, (uint32_t)OP_PY + M2M_VY, 4, M2M_VH, C_WARN) &&
              !px_in(M2M_VX + M2M_VW - 22u, (uint32_t)OP_PY + M2M_VY, 3, M2M_VH, C_WARN),
              "MIXER_2X MASTER: the compressor's 6 dB in amber from the right of the level line");
        ppm("opt-mixer2x-master");
        song.g[G_CTHR] = 0;
        mc_gr_view = 0;
        frame();
#else
        ppm("opt-mixer2x-master");
#endif
    }
    memcpy(t->step, keep, sizeof keep);
    memcpy(dr->dstep, dkeep, sizeof dkeep);
    reset_ui();
}
#else
static void mixer2x_tests(void)
{
    reset_ui();
    ui.force = 1;
    frame();
#ifdef M2_BP
    check(0, "MIXER_2X off: op_mix2x.c not built");
#endif
    check(mx_row() == MXR_T1 && !cards_2x2() && OP_PY == OY_PANEL && cards_band_is(BAND_1X4) && MXL_H == (OP_FOOT ? 36 : 41),
          "MIXER_2X off: the mixer as before, its cards 1x4 and the rows (op_mixdraw.c), nothing of op_mix2x.c");
}
#endif
