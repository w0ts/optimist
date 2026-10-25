/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, the mixer in the big face (builder item MIXER_2X, op_mix2x.c; the user, 2026-10-25: "Can we
 * add a double size builder flag for mixer: 2*2 cards. Condensed master meter"; round 2: "put the sequence below the
 * tracks ... normal master level + comp. drum lane should be displayed ... a one pixel vu meter"; "one 16 step on one
 * line"; "a one pixel line of step per track"):
 *   on   every mixer row has the 2x2 big cards (the same four values, KNOB k = card k); under them the rows (T1 T2 T3 DR,
 *        or on DR and its lanes the lanes in two columns), each its code, a 1 px strip of its steps and a 1 px meter;
 *        under the rows the selected row's 16 steps as one line on the tracks' strips' columns, its pages, its meter;
 *        MASTER: the normal mixer's level meter with its dB scale and the compressor's reduction, the returns small
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
static uint32_t m2t_cell_x(uint32_t i) { return (uint32_t)(M2_SX + (int32_t)i * M2_SP); }   /* the big line's step i */
static uint32_t m2t_cell_y(void) { return (uint32_t)OP_PY + m2_sy(); }
static uint32_t m2t_row_y(uint32_t i) { return (uint32_t)(OP_PY + m2_ry(i)); }               /* row i's top */
static uint32_t m2t_strip_x(uint32_t i, uint32_t s) { return (uint32_t)(m2_sx(i) + (int32_t)s * m2_sp()); }
static int m2t_strip_is(uint32_t i, uint32_t s, uint16_t c) { return px_at(m2t_strip_x(i, s) + 1u, m2t_row_y(i) + M2_TO, c); }
static int m2t_vu_lit(uint32_t i)                       /* row i's 1 px meter: anything lit */
{
    uint32_t x;
    for (x = 0; x < (uint32_t)(16 * m2_sp() - 2); x++)
        if (!px_at((uint32_t)m2_sx(i) + x, m2t_row_y(i) + M2_UO, C_BLACK))
            return 1;
    return 0;
}
static int m2t_fits(void) { return OP_PY + (int32_t)m2_sy() + (int32_t)m2_sh() + 8 <= OP_FOOT_Y && m2_sh() >= 12u; }
static void m2t_quiet(void)                             /* every meter fallen: no audio, frames drawn */
{
    uint32_t k;
    for (k = 0; k < 80u; k++) {
        memset(mt.ui, 0, sizeof mt.ui);
        memset(mx.lit, 0, sizeof mx.lit);
        ui_draw();
        fm1_ms += 16;
    }
}
#if FELUCCA_MIXER_2X == 2
/* "both": HOME at the root goes small -> big -> scope -> small; back from another page the face last used; the face kept
 * in the settings record (an older record: small) */
static void m2t_both_tests(void)
{
    uint32_t ok;
    reset_ui();
    mx_big = 0;
    ui.force = 1;
    frame();
    ok = ui.scr == SCR_HOME && !cards_2x2() && cards_band_is(BAND_1X4) && OP_PY == OY_PANEL &&
         m2t_text_at(6, (int32_t)OP_PY + MXL_TY, "T1") && m2t_text_at(6, (int32_t)OP_PY + MXL_H + MXL_TY, "T2");   /* (the rows of 41 px) */
    tap(B_HOME);
    frame();
    ok &= ui.scr == SCR_HOME && mx_big && cards_2x2() && cards_band_is(BAND_2X2) && OP_PY == OY_PANEL_2X2 &&
          m2t_text_at(1, (int32_t)m2t_row_y(0) - 2, "T1");
    ppm("opt-mixer2x-both-big");
    tap(B_HOME);
    ok &= ui.scr == SCR_SCOPE;
    tap(B_HOME);
    frame();
    ok &= ui.scr == SCR_HOME && !mx_big && !cards_2x2() && cards_band_is(BAND_1X4);
    check(ok, "MIXER_2X both: HOME at the root: small -> big -> scope -> small, each drawn in its own face");
    tap(B_HOME);                                        /* big, then away and back: still big */
    tap(B_SEQ);
    ok = ui.scr == SCR_STEP && mx_big;
    tap(B_HOME);
    frame();
    ok &= ui.scr == SCR_HOME && mx_big && cards_band_is(BAND_2X2);
    check(ok, "MIXER_2X both: back to the mixer from another page: the face last used (big)");
    settings_later = 0;                                 /* a switch asks the settings record to be written (once quiet) */
    tap(B_HOME);
    ok = ui.scr == SCR_SCOPE && !settings_later;        /* (big -> the scope: the face unchanged) */
    tap(B_HOME);
    ok &= ui.scr == SCR_HOME && !mx_big && settings_later;
    settings_later = 0;
    tap(B_HOME);
    ok &= ui.scr == SCR_HOME && mx_big && settings_later;
    settings_later = 0;
    check(ok, "MIXER_2X both: each switch of the face asks for the settings record to be written");
    mx_face_in(1);                                      /* the record's byte (project.c persist_mix_load): 1 big, an older */
    ok = mx_big == 1u;                                  /* record's 0 small, anything else small */
    mx_face_in(0);
    ok &= mx_big == 0u;
    mx_face_in(0xFF);
    check(ok && !mx_big, "MIXER_2X both: the face read back from the settings record (1 big; 0, an older record, small)");
    reset_ui();
}
#endif
static void mixer2x_tests(void)
{
    track_t *t = &trk[0], *dr = &trk[TRK_DRUM];
    uint32_t i, k, ok, y;
    step_t keep[16];
    dstep_t dkeep[16];
    memcpy(keep, t->step, sizeof keep);
    memcpy(dkeep, dr->dstep, sizeof dkeep);
    for (i = 0; i < 16u; i++)                           /* T1: notes on 1, 6, 11, 16, the rest rests */
        t->step[i].time = ST_REST, t->step[i].n = 0;
    for (i = 0; i < 16u; i += 5u)
        t->step[i].time = ST_NOTE, t->step[i].n = 1, t->step[i].note[0] = 60, t->step[i].vel = 100, t->step[i].lvl = LV_NORM;
    memset(dr->dstep, 0, sizeof dkeep);                 /* DR: the snare on 1 (SOFT), 3 (GHOST), 10 (HARD) */
    dstep_set(&dr->dstep[0], 2u, 2u, 0);
    dstep_set(&dr->dstep[2], 2u, LV_GHOST, 0);
    dstep_set(&dr->dstep[9], 2u, LV_HARD, 0);
#if FELUCCA_MIXER_2X == 2
    m2t_both_tests();                                   /* ("both": HOME's cycle, the face kept; then the big face's tests) */
    mx_big = 1;
#endif
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
    ok &= m2t_labels_ok(MXR_T1);
    mx.set = 0;
    check(ok, "MIXER_2X: KNOB 1..4 turn cards 1..4 (top left, top right, bottom left, bottom right), the hot one white");
    m2t_quiet();
    ui.force = 1;
    frame();
    /* the order: the rows first, the selected row's steps below them */
    ok = !m2_lanes() && m2_nrows() == NTRK && m2t_cell_y() >= m2t_row_y(NTRK - 1u) + (uint32_t)M2_RP && m2t_fits();
    ok &= m2t_text_at(1, (int32_t)m2t_row_y(0) - 2, "T1") && m2t_text_at(1, (int32_t)m2t_row_y(TRK_DRUM) - 2, trk_tag(TRK_DRUM)) &&
          px_in(1, m2t_row_y(0), 16, 10, C_WHITE);
    for (k = 1; k < NTRK; k++)                          /* (the others' codes in their colours, none white) */
        ok &= px_in(1, m2t_row_y(k), 16, 10, trk_col(k)) && !px_in(1, m2t_row_y(k), 16, 10, C_WHITE);
    check(ok, "MIXER_2X: the track rows T1 T2 T3 DR first (T1's code white), the selected row's steps under them, above the help band");
    ok = px_in(m2t_cell_x(0) + 2u, m2t_cell_y() + 2u, 8, m2_sh() - 4u, lvl_col(trk_col(0), LV_NORM)) &&
         px_in(m2t_cell_x(15) + 2u, m2t_cell_y() + 2u, 8, m2_sh() - 4u, lvl_col(trk_col(0), LV_NORM)) &&
         !px_in(m2t_cell_x(1) + 2u, m2t_cell_y() + 2u, 8, m2_sh() - 4u, lvl_col(trk_col(0), LV_NORM)) &&
         px_at(m2t_cell_x(1), m2t_cell_y() + 5u, C_LINE) && m2t_cell_x(15) + (uint32_t)M2_SP - 2u <= 240u && M2_SP >= 14;
    check(ok, "MIXER_2X: the selected track's 16 steps as ONE line (14 px a step): its notes filled, a rest an outline");
    ok = 1;
    for (i = 0; i < 16u; i++)                           /* T1's 1 px strip: the hits in its colour, the rests dark, on the */
        ok &= m2t_strip_is(0, i, i % 5u ? C_LINE : lvl_col(trk_col(0), LV_NORM)) &&   /* big line's columns */
              m2t_strip_x(0, i) == m2t_cell_x(i);
    for (k = 0; k < NTRK; k++)                          /* (quiet: no meter lit) */
        ok &= !m2t_vu_lit(k);
    check(ok, "MIXER_2X: each track row's 1 px strip of its steps on the big line's 16 columns (hits lit, rests dark); quiet: its 1 px meter dark");
    ppm("opt-mixer2x-t1");
    memset(mt.ui, 0, sizeof mt.ui);                     /* T2 sounding, the others quiet: T2's meter alone lit */
    mt.ui[1] = 12000;
    ui_draw();
    ok = m2t_vu_lit(1) && px_at((uint32_t)m2_sx(1), m2t_row_y(1) + M2_UO, C_OK) && !m2t_vu_lit(0) && !m2t_vu_lit(2) && !m2t_vu_lit(3);
    check(ok, "MIXER_2X: a track's 1 px meter lit while it sounds (the others dark)");
    m2t_quiet();
    check(!m2t_vu_lit(1), "MIXER_2X: ... and dark again once quiet");
    while (mx_row() < MXR_DR)
        turn(EN_ALGO, 1);
    ui.force = 1;
    frame();
    m2t_quiet();
    ok = m2_lanes() && m2_nrows() == DRUM_UILANES && m2t_fits() && cards_band_is(BAND_2X2) && m2t_labels_ok(MXR_DR);
    for (i = 0; i < DRUM_UILANES; i++)                  /* every lane named in its row, none white (DR selected) */
        ok &= m2t_text_at(m2_rx(i) + 1, (int32_t)m2t_row_y(i) - 2, lane_code(i)) && !px_in((uint32_t)m2_rx(i) + 1u, m2t_row_y(i), 16, 10, C_WHITE);
    ok &= m2t_strip_is(2, 0, lvl_col(lane_col(2), 2u)) && m2t_strip_is(2, 9, lvl_col(lane_col(2), LV_HARD)) && m2t_strip_is(2, 1, C_LINE) &&
          m2t_strip_is(0, 0, C_LINE);
    ok &= px_in(m2t_cell_x(0) + 2u, m2t_cell_y() + 2u, 8, m2_sh() - 4u, lvl_col(trk_col(TRK_DRUM), 2u)) &&
          px_in(m2t_cell_x(9) + 2u, m2t_cell_y() + 2u, 8, m2_sh() - 4u, lvl_col(trk_col(TRK_DRUM), LV_HARD)) &&
          !px_in(m2t_cell_x(1), m2t_cell_y(), (uint32_t)M2_SP - 2u, m2_sh(), C_LINE) &&   /* (an empty drum step: a dot) */
          px_in(m2t_cell_x(1), m2t_cell_y(), (uint32_t)M2_SP - 2u, m2_sh(), C_DIM);
    check(ok, "MIXER_2X on DR: the drum lanes as the rows (two columns, each named, its 1 px strip), DR's steps merged below, the cards 2x2");
    ppm("opt-mixer2x-dr");
    turn(EN_ALGO, 3);                                   /* the snare lane */
    ui.force = 1;
    frame();
    ok = mx_row() == MXR_LANE0 + 2u && m2_lanes() && px_in((uint32_t)m2_rx(2) + 1u, m2t_row_y(2), 16, 10, C_WHITE) &&
         !px_in((uint32_t)m2_rx(0) + 1u, m2t_row_y(0), 16, 10, C_WHITE) &&
         px_in(m2t_cell_x(0) + 2u, m2t_cell_y() + 2u, 8, m2_sh() - 4u, lvl_col(lane_col(2), 2u)) &&
         px_in(m2t_cell_x(9) + 2u, m2t_cell_y() + 2u, 8, m2_sh() - 4u, lvl_col(lane_col(2), LV_HARD)) &&
         cards_band_is(BAND_2X2) && m2t_labels_ok(mx_row());
    check(ok, "MIXER_2X on a lane: ALGORITHM moves into it, its code white, its own hits large below, its cards 2x2");
    memset(mx.lit, 0, sizeof mx.lit);                   /* the snare hit: its meter alone lit, in its colour */
    mx.lit[2] = MX_LIT;
    ui_draw();
    ok = m2t_vu_lit(2) && px_at((uint32_t)m2_sx(2), m2t_row_y(2) + M2_UO, lane_col(2)) && !m2t_vu_lit(0) && !m2t_vu_lit(3);
    ppm("opt-mixer2x-lane");
    m2t_quiet();
    check(ok && !m2t_vu_lit(2), "MIXER_2X: a lane's 1 px meter lit at its hit (in its colour, the others dark), dark once quiet");
    turn(EN_ALGO, 1);                                   /* the clap lane: no hit */
    ui.force = 1;
    frame();
    check(mx_row() == MXR_LANE0 + 3u && !px_in(M2_SX, m2t_cell_y(), 222, m2_sh(), lane_col(3)), "MIXER_2X: the clap lane, no hit: no cell in its colour");
    reset_ui();
    song.sel = 0;
    {   /* playing: the strips' playhead moves, the meters light */
        uint32_t lit = 0, at0;
        ui.force = 1;
        frame();
        tap(B_PLAY);
        frames(6);
        at0 = t->seq_idx;
        for (k = 0; k < 30u; k++) {
            mt.ui[0] = 12000;                           /* (the meters' main-loop side is not in the host: a peak) */
            frame();
            lit |= (uint32_t)m2t_vu_lit(0);
        }
        y = (uint32_t)t->seq_idx % (uint32_t)ui_clamp(t->p[P_SLEN], 1, NSTEP) % 16u;
        ok = song.playing && t->seq_idx != at0 && px_in(m2t_cell_x(y) + 2u, m2t_cell_y() + 2u, 8, m2_sh() - 4u, C_WHITE);
        for (k = 0; k < NTRK; k++) {                    /* (each track's playhead column white in its strip) */
            uint32_t len = (uint32_t)ui_clamp(trk[k].p[P_SLEN], 1, NSTEP);
            ok &= m2t_strip_is(k, trk[k].seq_idx % len % 16u, C_WHITE);
        }
        ppm("opt-mixer2x-playing");
        check(ok && lit, "MIXER_2X playing: the playhead white on the big line and in every track's strip, T1's meter lit");
        tap(B_PLAY);
        frames(4);
        check(!song.playing && m2t_strip_is(0, 1, C_LINE) && m2t_strip_is(0, 0, lvl_col(trk_col(0), LV_NORM)), "MIXER_2X stopped: no playhead in the strips");
    }
    {   /* a muted track: its code struck and grey, its strip's hits grey */
        smute_trk = 1u;
        ui.force = 1;
        frame();
        check(mx_muted(MXR_T1) && px_in(1, m2t_row_y(0), 16, 10, C_WHITE) && m2t_strip_is(0, 0, C_DIM), "MIXER_2X: a muted track's strip grey");
        smute_trk = 1u << 1;
        ui.force = 1;
        frame();
        check(px_in(1, m2t_row_y(1), 16, 10, C_DIM) && !px_in(1, m2t_row_y(1), 16, 10, trk_col(1)), "MIXER_2X: a muted track's code grey");
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
             px_in(MXM_X, (uint32_t)OP_PY + MXM_VY + MXM_VH - 1u, MXM_W, 1, C_LINE) &&
             px_in(0, (uint32_t)OP_PY + MXM_VY + MXM_VH + 1u, 240, 3, C_LINE) &&   /* (the dB scale's ticks) */
             m2t_text_at(MXM_X, (int32_t)OP_PY + MXM_GY - 5, "GR") && OP_PY + MXM_CY + 2 * MXM_CP + 12 <= OP_FOOT_Y;
        for (i = 0; i < n; i++)                         /* (the returns in the small face, each with its meter) */
            ok &= !strcmp(fxs_line_name(ret[i]), RN[i]) && m2t_text_at(MXM_X, (int32_t)OP_PY + MXM_CY + (int32_t)i * MXM_CP, RN[i]) &&
                  px_in(MXM_GX, (uint32_t)(OP_PY + MXM_CY + (int32_t)i * MXM_CP + MXM_CV + MXM_CH - 1), MXM_GW, 1, C_LINE);
        check(ok, "MIXER_2X MASTER: the normal level meter with its dB scale, GR, the returns CHO DLY REV small, each with its meter");
        mt.ret[2] = 12000;                              /* REV's return loud, CHO's and DLY's silent */
        mt.ret[1] = mt.ret[0] = 0;
        memset(fx_ret_pk, 0, sizeof fx_ret_pk);
        ui_draw();
        y = (uint32_t)(OP_PY + MXM_CY + MXM_CV);
        check(px_in(MXM_GX, y + 2u * MXM_CP, 40, MXM_CH, C_OK) && !px_in(MXM_GX, y + MXM_CP, 40, MXM_CH - 1u, C_OK) &&
              !px_in(MXM_GX, y, 40, MXM_CH - 1u, C_OK), "MIXER_2X MASTER: the REV return's meter fills, CHO's and DLY's stay empty");
        mt.ret[2] = 0;
#if FELUCCA_MASTER_COMP
        song.g[G_CTHR] = -10;
        mc_gr_view = -6;                                /* 6 dB: 18 px from the right of the GR bar, its dB */
        ui_draw();
        check(px_in(MXM_GX + MXM_GW - 17u, (uint32_t)OP_PY + MXM_GY, 4, MXM_GH, C_WARN) &&
              !px_in(MXM_GX + MXM_GW - 22u, (uint32_t)OP_PY + MXM_GY, 3, MXM_GH, C_WARN) &&
              m2t_text_at(MXM_GX, (int32_t)OP_PY + MXM_GY - 5, "-6 dB"),
              "MIXER_2X MASTER: the compressor's 6 dB as the normal mixer's GR bar and its dB");
        ppm("opt-mixer2x-master");
        song.g[G_CTHR] = 0;
        mc_gr_view = 0;
        ui_draw();
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
    reset_ui();                                         /* (no frame: its audio would move MASTER's GR, the SCRS golden) */
#ifdef M2_RP
    check(0, "MIXER_2X off: op_mix2x.c not built");
#endif
    check(mx_row() == MXR_T1 && !cards_2x2() && OP_PY == OY_PANEL && cards_band_is(BAND_1X4) && MXL_H == (OP_FOOT ? 36 : 41),
          "MIXER_2X off: the mixer as before, its cards 1x4 and the rows (op_mixdraw.c), nothing of op_mix2x.c");
}
#endif
