/* SPDX-License-Identifier: GPL-3.0-only */
/* The patterns screen (LFO held, SONG's PATTERNS row), the user 2026-11-01, included by ui_optimist_test.c:
 *   no cards    the 2x2 cards are gone: the panel starts under the header (OY_CARD), the grid is taller, the numbers 1..16 on two
 *               lines so 10..16 do not crowd
 *   rows        each row names the knob that cues it (K1 T1, K2 T2, K3 T3, K4 DR), in the knob's colour with KNOB COLORS
 *   preview     the pattern last touched, drawn as the mixer draws a row (4 groups of 4 steps, shaded by level, the track's
 *               colour; the drums' lanes as rows), read from the store without loading it; an empty slot says EMPTY */
#if FELUCCA_PATTERNS
static int32_t pvt_gp(void) { return (OP_PH - 2 * PG_NUMH - PG_PVH - 8) / (int32_t)NTRK; }   /* the grid's row pitch */
static uint32_t pvt_row_y(uint32_t k) { return (uint32_t)(OP_PY + 2 * PG_NUMH + 2 + (int32_t)k * pvt_gp()); }
static uint32_t pvt_pv_y(void) { return (uint32_t)(OP_PY + 2 * PG_NUMH + 2 + (int32_t)NTRK * pvt_gp() + 2); }
static uint32_t pvt_step_x(uint32_t i) { return (uint32_t)PG_SX(i); }
static void patview_tests(void)
{
    uint32_t k, i, w, y;
    pat_peek_t d;
    reset_ui();
    song.sel = 0;
    song.playing = 0;
    for (k = 0; k < NTRK; k++)
        pw_edit(k, 48u + k);
    trk[0].step[4].time = ST_NOTE, trk[0].step[4].n = 1, trk[0].step[4].note[0] = 52;
    trk[0].step[8].time = ST_NOTE, trk[0].step[8].n = 1, trk[0].step[8].note[0] = 55, trk[0].step[8].lvl = (uint8_t)LV_HARD;
    pat_store_slot(0, 4);
    press(B_LFO);
    frames(HOLD_FRAMES);
    ui.force = 1;
    frame();
    check(lpk.on && song_on_pat_row() && pat_view() && OP_PY == OY_CARD && ui.sig[1] == 0u, "patview: LFO held: no 2x2 cards, the panel starts under the header");
    check(pvt_gp() >= 27 && pvt_row_y(NTRK - 1u) + (uint32_t)pvt_gp() <= (uint32_t)OP_FOOT_Y, "... a taller grid: rows 27 px or more, all of them above the help band");
    check(px_in((uint32_t)PG_X(4) + 1u, pvt_row_y(0) + 1u, PG_CW - 2, 2, C_OK) && px_in((uint32_t)PG_X(4) + 1u, pvt_row_y(0) + (uint32_t)pvt_gp() - 4u, PG_CW - 2, 1, C_OK),
          "... T1's playing pattern (green) fills its tall cell from top to foot");
    check(!px_in((uint32_t)PG_X(5), pvt_row_y(0) + 2u, 1, 1, C_OK) && px_in((uint32_t)PG_X(5), pvt_row_y(0) + 2u, 1, 1, C_LINE), "... the cell next to it an empty outline");
    for (i = 0, w = 1; i + 2u < PAT_N; i++)
        w &= (uint32_t)(PG_X(i + 2u) - PG_X(i)) >= 16u;
    check(w, "numbers: two numbers on a line are 16 px apart or more (a 2-digit number is 16 px wide): 10..16 do not crowd");
    for (i = 1, w = 1; i < PAT_N; i++)                  /* the gap after every 4th column (3/4, 7/8, 11/12 0-based): PG_GG more than the plain pitch; inside a group none */
        w &= (uint32_t)(PG_X(i) - PG_X(i - 1u)) == (uint32_t)(PG_PITCH + (i % 4u ? 0 : PG_GG)) && (i % 4u || PG_X(i) - PG_X(i - 1u) >= PG_CW + 2 + PG_GG);
    check(w && PG_GG == 3 && MXL_GG == 2, "groups: a gap of PG_GG (3 px, the mixer keeps its 2) more after every 4th column, the cells inside a group a 2 px space apart");
    check(PG_X(15) + PG_CW <= 240, "... the last cell inside 240 px");
    for (i = 0, w = 1; i < PAT_N; i++) {                /* every number's ink, as drawn (PG_NUMX): inside 0..240, and on its own column (never past the next number) */
        char nb[4];
        int32_t nw, nx;
        fmt_int(nb, (int32_t)i + 1);
        nw = text_ws(nb), nx = PG_NUMX(i, nw);
        w &= nx >= 0 && nx + nw <= 240 && nx + nw > PG_X(i) && (i + 2u >= PAT_N || nx + nw <= PG_X(i + 2u));
    }
    check(w, "... every number (1..16) fully inside 240 px, the last ('16') too");
    {
        int32_t nw = text_ws("16");
        check(px_in((uint32_t)(240 - nw), (uint32_t)OP_PY + PG_NUMH, (uint32_t)nw, PG_NUMH, C_GRAY) || px_in((uint32_t)(240 - nw), (uint32_t)OP_PY + PG_NUMH, (uint32_t)nw, PG_NUMH, C_OK),
              "... '16' is drawn on the lower line, ending at 240");
    }
    check(px_in((uint32_t)PG_X(4) - 2u, (uint32_t)OP_PY, 16, PG_NUMH, C_OK) && px_in((uint32_t)PG_X(9) - 2u, (uint32_t)OP_PY + PG_NUMH, 16, PG_NUMH, C_GRAY),
          "... 5 (playing, green) on the upper line, 10 (grey) on the lower line");
    check(!px_in(0, (uint32_t)OP_PY + 2u * PG_NUMH, 240, 1, C_OK) && !px_in(0, (uint32_t)OP_PY + 2u * PG_NUMH, 240, 1, C_GRAY), "... no number reaches the grid's top rule");
    check(pat_view() && PAT_KNOB_CELL[PAT_ROW_KNOB[0]] == 0u && PAT_KNOB_CELL[PAT_ROW_KNOB[1]] == 1u && PAT_KNOB_CELL[PAT_ROW_KNOB[2]] == 2u && PAT_KNOB_CELL[PAT_ROW_KNOB[3]] == 3u &&
              PAT_KNOB_CELL[0] == 0u && PAT_KNOB_CELL[1] == 1u && PAT_KNOB_CELL[2] == 2u && PAT_KNOB_CELL[3] == 3u,
          "knobs: the rows in order (K1 T1, K2 T2, K3 T3, K4 DR) and each row names its own knob");
    for (k = 0; k < NTRK; k++)
        check(px_in(3, pvt_row_y(k) + 12u, 16, 12, C_DIM), "... without KNOB COLORS the row's knob (K1 .. K4) is in dim grey under its tag");
    knob_colors = 1;
    ui.force = 1;
    frame();
    for (k = 0, w = 1; k < NTRK; k++)
        w &= (uint32_t)px_in(3, pvt_row_y(k) + 12u, 16, 12, knob_col(PAT_ROW_KNOB[k], C_DIM)) && knob_col(PAT_ROW_KNOB[k], C_DIM) != C_DIM;
    check(w, "... with KNOB COLORS each row's knob mark is in that knob's colour");
    knob_colors = 0;
    /* the preview of a stored slot */
    key(WK(4));
    ui.force = 1;
    frame();
    y = pvt_pv_y();
    check(pat_cur[0] == 4u && !px_in(0, y + 1u, PG_PVX - 1, PG_PVT + 14, trk_col(0)) && !px_in(0, y + 1u, 240, PG_PVT - 1, trk_col(0)), "preview: T1 slot 5 touched: no label (\"T1 5\" is gone: the track and slot are known), the steps start right under the rule");
    check(px_in(pvt_step_x(0) + 1u, y + PG_PVT, 9, 12, lvl_col(trk_col(0), LV_NORM)) && px_in(pvt_step_x(4) + 1u, y + PG_PVT, 9, 12, lvl_col(trk_col(0), LV_NORM)) &&
              px_in(pvt_step_x(8) + 1u, y + PG_PVT, 9, 12, lvl_col(trk_col(0), LV_HARD)),
          "... its steps 1, 5, 9 filled in the track's colour, the hard one brighter");
    check(!px_in(pvt_step_x(1) + 1u, y + PG_PVT, 9, 12, lvl_col(trk_col(0), LV_NORM)) && !px_in(pvt_step_x(1) + 1u, y + PG_PVT, 9, 12, lvl_col(trk_col(0), LV_HARD)),
          "... the steps between empty outlines");
    check(pvt_step_x(4) - pvt_step_x(3) == 13u + PG_GG && pvt_step_x(5) - pvt_step_x(4) == 13u && pvt_step_x(15) + 11u <= 240u && pvt_step_x(0) >= 0u, "... 4 groups of 4 with the grid's gap, all 16 steps inside 240 px");
    ppm("opt-patterns-gap");
    /* read, not loaded */
    trk[0].step[0].n = 0, trk[0].step[0].time = ST_REST;
    check(pat_peek(0, 4, &d) && d.hit[0] == (uint16_t)(1u | 1u << 4 | 1u << 8) && ((d.lvl[0] >> 16) & 3u) == LV_HARD && d.len == (uint8_t)trk[0].p[P_SLEN],
          "the store read without loading: the slot's steps and levels, its LEN");
    check(trk[0].step[0].n == 0u && pat_cur[0] == 4u && !pat_peek(0, 5, &d) && d.hit[0] == 0u, "... the working pattern untouched, an empty slot reads as nothing");
    /* an empty slot */
    key(WK(7));
    tap(B_SAVE);                                        /* (the cleared step is an edit: LOSE EDITS? yes) */
    ui.force = 1;
    frame();
    y = pvt_pv_y();
    ppm("opt-patterns-empty");
    check(!pat_has(0, 7) && px_in(237u - (uint32_t)text_ws("EMPTY"), y + 3u, (uint32_t)text_ws("EMPTY"), 12, C_DIM), "preview: an empty slot says EMPTY");
    check(!px_in(PG_PVX, y + PG_PVT, 215, PG_SYH, lvl_col(trk_col(0), LV_NORM)) && !px_in(PG_PVX, y + PG_PVT, 215, PG_SYH, lvl_col(trk_col(0), LV_HARD)), "... and draws no steps");
    /* a drum slot */
    if (is_drum(&trk[3])) {
        key(BK(3));
        check(song.sel == 3u, "drum preview: black 4 selects DR");
        memset(trk[3].dstep, 0, sizeof trk[3].dstep);   /* (pw_edit wrote a synth step over the drum track's: a clean one) */
        dstep_set(&trk[3].dstep[0], 2, LV_HARD, 0);
        dstep_set(&trk[3].dstep[5], 0, LV_NORM, 0);
        pat_store_slot(3, 6);
        key(WK(6));
        ui.force = 1;
        frame();
        y = pvt_pv_y();
        check(pat_peek(3, 6, &d) && d.hit[2] == 1u && d.hit[0] == (uint16_t)(1u << 5), "the drum slot read: lane 3's hit on step 1, lane 1's on step 6");
        check(px_in(pvt_step_x(0), y + PG_PVT + 2u * PG_LANE, 11, PG_LANE - 1, lvl_col(trk_col(3), LV_HARD)) && px_in(pvt_step_x(5), y + PG_PVT, 11, PG_LANE - 1, lvl_col(trk_col(3), LV_NORM)),
              "... drawn a row a lane: each hit in the drum colour at its lane's row");
        check(!px_in(pvt_step_x(0), y + PG_PVT, 11, PG_LANE - 1, lvl_col(trk_col(3), LV_HARD)) && !px_in(pvt_step_x(1), y + PG_PVT + 2u * PG_LANE, 11, PG_LANE - 1, lvl_col(trk_col(3), LV_HARD)),
              "... and no hit where the lane has none");
    }
    ui.force = 1;
    frame();
    ppm("opt-patterns-preview");
    release(B_LFO);
    reset_ui();
}
#endif
