/* SPDX-License-Identifier: GPL-3.0-only */
/* GATER on the Optimist UI (2026-10-14, feat/gater-ui; fx/insfx/gater.c): SOUND's FX row GATER, DEPTH PAT RATE SHAPE
 * (P_TGATE P_TGPAT P_TGRATE P_TGSHP), shown only while one of the track's slots holds the GATER (the drum track too, it
 * gates the drum bus), the PATTERN's name on its card, RATE as a division, its help lines, the page remembered with
 * its family, and its picture: the 16 steps, the gate's shape, the step it is at in white. Built with FELUCCA_INS_GATER=1;
 * included by ui_optimist_test.c after ui_optimist_inslots.h. */
#if FELUCCA_INS_GATER
static const char *const GTU_PAT[16] = {"16TH", "8TH", "4TH", "HALF", "OFF8", "OFF16", "DOT8", "DOT4",
                                        "TRNC1", "TRNC2", "TRNC3", "TRNC4", "GALOP", "RVGAL", "CLAVE", "TRESL"};
static uint32_t gtu_row(void) { return ins_row("GATER"); }
static uint32_t gtu_row_now(void)                       /* the GATER row of the rows shown now (no op_enter: the family kept) */
{
    uint32_t r;
    for (r = 0; r < snd_rows(); r++)
        if (snd_page(r) && !strcmp(snd_page(r)->title, "GATER"))
            return r;
    return r;
}
static uint32_t gtu_panel_hash(void)                    /* the panel under the cards */
{
    uint32_t i, h = 2166136261u;
    for (i = (uint32_t)OP_PY * 240u; i < 240u * 240u; i++)
        h = (h ^ screen[i]) * 16777619u;
    return h;
}
static void gtu_fresh(void)
{
    reset_ui();
    ins_defaults();
    fxs_set(FXS_DEF);
    fxi_init();
    fxi_poll();
    song.sel = 0;
    trk[0].p[P_TGATE] = 0;                              /* (the earlier fuzz may have turned T1's GATER) */
    fxs_gx_def(0);
}
/* the leftmost white pixel of the panel below the cards (the playhead is the only white there), -1 none */
static int32_t gtu_white_x(void)
{
    uint32_t x, y;
    for (x = 0; x < 240u; x++)
        for (y = (uint32_t)OP_PY; y < 240u; y++)
            if (px_at(x, y, C_WHITE))
                return (int32_t)x;
    return -1;
}
static void gtu_page_tests(void)
{
    cell_t c;
    uint32_t r, k, ok;
    gtu_fresh();
    check(gtu_row() >= SCR->rows(), "GATER: not a row while no slot of T1 holds it");
    song.sel = TRK_DRUM;
    check(gtu_row() >= SCR->rows(), "... nor the drum track's");
    song.sel = 0;
    ins_load(0, 0, FXT_GATE);
    r = gtu_row();
    check(r < SCR->rows() && snd_page(r)->fam == FAM_FX && snd_page(r)->graph == GR_GATE, "T1's S1 holds the GATER: a row of the FX family");
    check(ins_row("INSERTS") < r && r < ins_row("SLICER"), "... after INSERTS, before SLICER");
    ui.row[SCR_SOUND] = (uint8_t)r;
    frame();
    SCR->cell(r, 0, &c);
    check(!strcmp(c.label, "DEPTH") && c.vp == &trk[0].p[P_TGATE] && c.d->max == 127, "KNOB 1 DEPTH: T1's P_TGATE");
    SCR->cell(r, 1, &c);
    check(!strcmp(c.label, "PAT") && c.vp == &trk[0].p[P_TGPAT] && c.d == &TP[P_TGPAT], "KNOB 2 PAT: P_TGPAT");
    SCR->cell(r, 2, &c);
    check(!strcmp(c.label, "RATE") && c.vp == &trk[0].p[P_TGRATE] && c.d == &TP[P_TGRATE], "KNOB 3 RATE: P_TGRATE");
    SCR->cell(r, 3, &c);
    check(!strcmp(c.label, "SHAPE") && c.vp == &trk[0].p[P_TGSHP] && c.d == &TP[P_TGSHP], "KNOB 4 SHAPE: P_TGSHP");
    for (k = 0, ok = 1; k < 16u; k++) {                 /* the PATTERN card shows the name */
        trk[0].p[P_TGPAT] = (int16_t)k;
        SCR->cell(r, 1, &c);
        ok &= !strcmp(c.val, GTU_PAT[k]) && c.gk == GK_POS;
    }
    check(ok, "PAT: the 16 names on the card (16TH 8TH 4TH HALF OFF8 OFF16 DOT8 DOT4 TRNC1-4 GALOP RVGAL CLAVE TRESL)");
    trk[0].p[P_TGPAT] = 8;
    SCR->cell(r, 1, &c);
    check(!strcmp(c.val, "TRNC1"), "... TRNC1 by default");
    turn(EN_K2, 1);
    SCR->cell(r, 1, &c);
    check(trk[0].p[P_TGPAT] == 9 && !strcmp(c.val, "TRNC2"), "... KNOB 2 turned: TRNC2");
    trk[0].p[P_TGRATE] = 1;
    SCR->cell(r, 2, &c);
    check(!strcmp(c.val, "1/16") && c.gk == GK_DOTS && c.gv == 1 && c.gmax == 5, "RATE: a division, 1/16, the list form (one of six lit)");
    turn(EN_K3, 1);
    SCR->cell(r, 2, &c);
    {                                                   /* one step in the order shown: 1/32 (stored 2), with DIV_ORDER (the user's) 16T (stored 4) */
        int32_t nx = FELUCCA_DIV_ORDER ? 4 : 2;
        check(nx == param_step(&TP[P_TGRATE], 1, 1) && trk[0].p[P_TGRATE] == nx && !strcmp(c.val, TP[P_TGRATE].names[nx]) && c.gv == nx,
              "... KNOB 3 turned: the next division (1/32; 16T with DIV_ORDER)");
    }
    turn(EN_K1, 40);
    check(trk[0].p[P_TGATE] == 40, "KNOB 1 turned: DEPTH 40");
    turn(EN_K4, 30);
    check(trk[0].p[P_TGSHP] == 30, "KNOB 4 turned: SHAPE 30");
    /* the drum track */
    song.sel = TRK_DRUM;
    ins_load(TRK_DRUM, 1, FXT_GATE);
    r = gtu_row();
    check(r < SCR->rows(), "the drum track's S2 holds the GATER: its row");
    ui.row[SCR_SOUND] = (uint8_t)r;
    frame();
    SCR->cell(r, 0, &c);
    check(c.vp == &TDRUM->p[P_TGATE] && !strcmp(c.label, "DEPTH"), "... KNOB 1 the drum bus's DEPTH");
    SCR->cell(r, 1, &c);
    check(c.vp == &TDRUM->p[P_TGPAT] && c.d == &TP[P_TGPAT], "... KNOB 2 its PAT");
    /* away again */
    song.sel = 0;
    ins_load(0, 0, FXT_DIST);
    check(gtu_row() >= SCR->rows(), "the slot given to another type: the row goes");
    gtu_fresh();
}
static void gtu_memory_tests(void)
{
    uint32_t r;
    gtu_fresh();
    ins_load(0, 0, FXT_GATE);
    tap(B_FX);
    frames(2);
    r = gtu_row_now();
    check(ui.scr == SCR_SOUND && snd_fam == FAM_FX && r < snd_rows(), "page memory: FX opens its rows, GATER among them");
    ui.row[SCR_SOUND] = (uint8_t)r;
    frames(2);
    tap(B_HOME);
    tap(B_ENV);
    frames(2);
    tap(B_HOME);
    tap(B_FX);
    frames(2);
    check(ui.scr == SCR_SOUND && snd_fam == FAM_FX && ui.row[SCR_SOUND] == r, "... away through ENV and back by FX: the GATER row again");
    {
        char t[32], p[12];
        head_title(ui.scr, ui.row[ui.scr], t, sizeof t);
        head_pos(ui.scr, ui.row[ui.scr], p);
        check(strstr(t, "Gater") != 0 && strchr(p, '/') != 0, "... the header names it and counts it (n/N)");
    }
    tap(B_HOME);
    gtu_fresh();
}
static void gtu_pic_tests(void)
{
    uint32_t r, cur, ok;
    uint8_t g[IGT_PN];
    char p[12];
    int32_t x0 = -1;
    gtu_fresh();
    ins_load(0, 0, FXT_GATE);
    trk[0].p[P_TGATE] = 100;
    trk[0].p[P_TGPAT] = 8;
    tap(B_FX);
    r = gtu_row_now();
    ui.row[SCR_SOUND] = (uint8_t)r;
    cp_draw();
    ppm("opt-gater-pic");
    check(snd_graph_page(r) && snd_graph_page(r)->graph == GR_GATE && lst.pic == 1u && lst.shown == 0u && cp_pos(p),
          "GATER: the picture alone under the cards, the n/N");
    check(px_in(0, (uint32_t)OP_PY, 240, 117, trk_col(song.sel)), "... in the track's colour");
    for (cur = 0, ok = 1; cur < 16u; cur++) {           /* the playhead follows the step the gate is at */
        ifx_gate[0].ph = (cur << 28) + 12345u;
        cp_draw();
        x0 = gtu_white_x();
        ok &= x0 == GT_X0 + (int32_t)(cur * IGT_PW);
    }
    check(ok, "... the playhead (white) under the step the gate is at, 0 to 15");
    trk[0].p[P_TGATE] = 0;
    cp_draw();
    check(gtu_white_x() < 0, "... DEPTH 0 (off): the picture dim, no playhead");
    trk[0].p[P_TGATE] = 100;
    ifx_gate[0].ph = 0;
    cp_draw();
    {   /* repainted without a forced draw: a change of SHAPE, of the step */
        uint32_t h1;
        trk[0].p[P_TGSHP] = 0;
        ifx_gate[0].ph = 3u << 28;
        cp_draw();
        h1 = gtu_panel_hash();
        trk[0].p[P_TGSHP] = 127;
        frame();
        check(gtu_panel_hash() != h1, "... a change of SHAPE repaints it");
        h1 = gtu_panel_hash();
        ifx_gate[0].ph = 4u << 28;
        frame();
        check(gtu_panel_hash() != h1 && gtu_white_x() == GT_X0 + 4 * (int32_t)IGT_PW, "... the gate moving on a step moves the playhead, no forced draw");
    }
    /* igt_profile: the envelope it draws */
    trk[0].p[P_TGPAT] = 0;                              /* 16TH: every step a pulse, chopped in halves */
    trk[0].p[P_TGATE] = 127;
    trk[0].p[P_TGSHP] = 0;
    igt_profile(&trk[0], g);
    check(g[0] == 255 && g[6] == 255 && g[7] == 0 && g[13] == 0 && g[14] == 255, "picture, 16TH hard: open 7 px, shut 7 px a step");
    trk[0].p[P_TGPAT] = 3;                              /* HALF: x.......x....... */
    igt_profile(&trk[0], g);
    check(g[0] == 255 && g[13] == 255 && g[14] == 0 && g[8 * IGT_PW] == 255 && g[8 * IGT_PW + IGT_PW] == 0,
          "picture, HALF: a whole step open, the next shut; again at step 8");
    trk[0].p[P_TGPAT] = 0;
    trk[0].p[P_TGATE] = 64;
    igt_profile(&trk[0], g);
    check(g[7] > 100 && g[7] < 160, "picture, DEPTH 64: a shut step stays about half open");
    trk[0].p[P_TGATE] = 127;
    trk[0].p[P_TGSHP] = 127;
    igt_profile(&trk[0], g);
    check(g[6] == 255 && g[7] > g[8] && g[8] > g[9] && g[7] > 200 && g[13] > 0, "picture, SHAPE smooth: the edge a ramp, not a step");
    trk[0].p[P_TGSHP] = 110;
    igt_profile(&trk[0], g);
    check(g[7] < 255 && g[7] > 0 && g[9] < g[7], "picture, SHAPE in between: a short ramp");
    {   /* the playhead's step is the gate's phase, 2^28 a step */
        ifx_gate[0].ph = 5u << 28;
        check(igt_step(0) == 5u, "igt_step: the gate's phase / 2^28");
    }
    {   /* playing: from the transport's clock, the gate's own phase (a silent track's, not run) left alone */
        uint32_t kb = clk_beat, kp = clk_pos;
        trk[0].p[P_TGRATE] = 1;                         /* 1/16: 4 steps a beat */
        song.playing = 1;
        clk_beat = 2, clk_pos = 0;
        ifx_gate[0].ph = 0;
        check(igt_step(0) == 8u, "igt_step playing: beat 2 at 1/16 is step 8, from the clock");
        clk_beat = 3, clk_pos = BEAT_U * 5u / 8u;
        check(igt_step(0) == 14u, "... half a beat on: step 14");
        song.playing = 0;
        clk_beat = kb, clk_pos = kp;
    }
    ifx_gate[0].ph = 0;
    tap(B_HOME);
    gtu_fresh();
}
#if FELUCCA_PARAM_HELP
static void gtu_help_tests(void)
{
    cell_t c;
    char b[PH_LINE];
    uint32_t r, k, ok = 1, fit = 1, t;
    static const char *const W[4] = {"How far the gate shuts steps", "Gate pattern: 16 steps", "Gate step length (in tempo)",
                                     "Gate edges: hard to smooth"};
    gtu_fresh();
    ins_load(0, 0, FXT_GATE);
    r = gtu_row();
    ui.row[SCR_SOUND] = (uint8_t)r;
    frame();
    for (k = 0; k < 4u; k++) {
        SCR->cell(r, k, &c);
        t = hl_find(SCR_SOUND, r, &c);
        ok &= t && !strcmp(ph_text(t, b), W[k]);
        fit &= t && text_w(&FONT_S, ph_text(t, b)) <= 232 && str_len(ph_text(t, b)) <= 29u;
    }
    check(ok, "help: DEPTH, PAT, RATE and SHAPE each have their line");
    check(fit, "... each within 29 characters and the 232 px line");
    song.sel = TRK_DRUM;
    ins_load(TRK_DRUM, 0, FXT_GATE);
    r = gtu_row();
    ui.row[SCR_SOUND] = (uint8_t)r;
    for (k = 0, ok = 1; k < 4u; k++) {
        SCR->cell(r, k, &c);
        ok &= hl_find(SCR_SOUND, r, &c) != 0u;
    }
    check(ok, "... the drum track's GATER row too");
    gtu_fresh();
}
#endif
static void gater_ui_tests(void)
{
    gtu_page_tests();
    gtu_memory_tests();
    gtu_pic_tests();
#if FELUCCA_PARAM_HELP
    gtu_help_tests();
#endif
    reset_ui();
}
#endif
