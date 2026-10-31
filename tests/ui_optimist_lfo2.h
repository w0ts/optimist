/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, LFO 2 (core/params.c PAGES, voice.c lfo2_tick):
 *   the pages   "LFO 2" (RATE WAVE TRIG POL) and "LFO2 DEST" (DEST AMT, two empty cards) are SOUND rows right after
 *               LFO DEST, on the synth tracks only
 *   the cards   each maps to its P_L2* value; RATE shows as LFO's does (the free bar from 0, a synced division as ten
 *               cells), SHIFT held + a knob puts the value back to its default
 *   SLEW        (issue 129) both WAVE cards read SLEW; the picture (opt-lfo2-slew, opt-lfo-slew) differs from S&H's and
 *               glides through S&H's steps
 *   the bars    AMT's second bar is the destination's actual value, equal to what the render used (ANALOG 2's cutoff)
 * Included after ui_optimist_inslots.h (ins_row). */
static void lfo2ui_cell(uint32_t r, uint32_t k, cell_t *c)
{
    ui.row[SCR_SOUND] = (uint8_t)r;
    ui.force = 1;
    frame();
    SCR->cell(r, k, c);
}
#if FELUCCA_ANALOG2 && FELUCCA_ENG_ANALOG
/* part 0 on ANALOG 2 with LFO 2 on FLT alone: effv_mods (after the block, lfo2's phase as the render used it) gives the cutoff
 * the render got (v->s[6]), block after block, for each wave and both POLs */
static int lfo2ui_cut_same(uint32_t wave, uint32_t pol, int32_t amt)
{
    track_t *t = &trk[0];
    int32_t o[CTL];
    uint32_t b, ok = 1, seen = 0;
    memset(t->v, 0, sizeof t->v);
    t->p[P_E4] = 50, t->p[P_E7] = 0, t->p[P_A2FENV] = 0, t->p[P_ED_FLT] = 0;
    t->p[P_L1DA] = t->p[P_L1AA] = t->p[P_L1DB] = t->p[P_L1AB] = t->p[P_ED_SHP] = t->p[P_ED_PIT] = 0;
    t->p[P_L2RATE] = 100, t->p[P_L2WAVE] = (int16_t)wave, t->p[P_L2POL] = (int16_t)pol, t->p[P_L2TRIG] = 1;
    t->p[P_L2DA] = LT_FLT, t->p[P_L2AA] = (int16_t)(2 * amt), t->p[P_L2DB] = t->p[P_L2AB] = 0;   /* (AMT: 2 x the one-slot one) */
    t->p[P_ATK] = 30, t->p[P_DEC] = 50, t->p[P_SUS] = 40;

    t->pold = 0, t->p[P_VOICE] = 0;                     /* (no sound load pending; POLY) */
    trk_note_on(t, 60, 100);
    for (b = 0; b < 200u && ok; b++) {
        const voice_t *v;
        effv_mod_t m;
        track_render(t, o, CTL);
        v = effv_voice(t);
        if (!v || v->s[6] == A2_NOCUT)
            continue;
        effv_mods(0, v, 0, &m);
        ok &= clamp((t->p[P_E4] << 8) + m.cut, 0, 127 << 8) == v->s[6];
        seen++;
    }
    trk_all_off(t);
    memset(t->v, 0, sizeof t->v);
    return ok && seen > 20u;
}
#endif
static void lfo2ui_tests(void)
{
    cell_t c;
    uint32_t r, rd, rl, k;
    int ok;
    track_t keep = trk[0];
    reset_ui();
    song.sel = 0;
    rl = ins_row("LFO DEST");
    r = ins_row("LFO 2");
    rd = ins_row("LFO2 DEST");
    check(r < SCR->rows() && rd < SCR->rows(), "LFO2 built in: the LFO 2 and LFO2 DEST rows");
    if (r >= SCR->rows() || rd >= SCR->rows())
        return;
    check(r == rl + 1u && rd == r + 2u && snd_page(r)->fam == FAM_LFO && snd_page(rd)->fam == FAM_LFO &&
              !strcmp(snd_page(r + 1u)->title, "LFO2 PHS") && !strcmp(snd_page(rl - 1u)->title, "LFO PHS") &&
              !strcmp(snd_page(rl - 2u)->title, "LFO"),
          "... right after LFO DEST, in the LFO family: LFO, LFO PHS, LFO DEST, LFO 2, LFO2 PHS, LFO2 DEST");
    ok = 1;
    for (k = 0; k < 4u; k++) {                          /* LFO2 PHS: PHS FADE, two empty cards; LFO's the same (issue 129) */
        static const uint8_t ID[2][2] = {{P_L2PHS, P_L2FADE}, {P_LPHASE, P_LFADE}};
        uint32_t j;
        for (j = 0; j < 2u; j++) {
            lfo2ui_cell(j ? rl - 1u : r + 1u, k, &c);
            ok &= k < 2u ? c.vp == &TSEL->p[ID[j][k]] && c.d == &TP[ID[j][k]] : !c.d;
        }
        lfo2ui_cell(rl - 2u, k, &c);
        {
            static const uint8_t L1[4] = {P_LRATE, P_LWAVE, P_L1TRIG, P_L1POL};
            ok &= c.vp == &TSEL->p[L1[k]] && c.d == &TP[L1[k]];
        }
    }
    check(ok, "LFO and LFO 2 the same: RATE WAVE TRIG POL, then PHS FADE (two cards)");
    ok = 1;
    for (k = 0; k < 4u; k++) {                          /* LFO 2: RATE WAVE TRIG POL */
        static const uint8_t ID[4] = {P_L2RATE, P_L2WAVE, P_L2TRIG, P_L2POL};
        lfo2ui_cell(r, k, &c);
        ok &= c.vp == &TSEL->p[ID[k]] && c.d == &TP[ID[k]] && c.kind == CK_VAL;
    }
    check(ok, "LFO 2: the four cards are RATE, WAVE, TRIG, POL");
    lfo2ui_cell(r, 2, &c);
    check(!strcmp(c.label, "TRIG") && !strcmp(c.val, "NOTE"), "TRIG: NOTE by default");
    ok = 1;
    for (k = 0; k < 4u; k++) {                          /* LFO2 DEST: DEST A, AMT A, DEST B, AMT B */
        lfo2ui_cell(rd, k, &c);
        if (k == 0 || k == 2)
            ok &= c.vp == &TSEL->p[P_L2DA + k] && c.kind == CK_VAL;
        else
            ok &= c.vp == &TSEL->p[P_L2DA + k] && c.d == &TP[P_L2DA + k] && c.gk == GK_BIP;
    }
    check(ok, "LFO2 DEST: DEST A, AMT A (a +/- card), DEST B, AMT B");
    /* RATE as LFO's */
    TSEL->p[P_L2RATE] = 0;
    lfo2ui_cell(r, 0, &c);
    check(c.gk == GK_BAR && c.gmin == 0 && c.gmax == 127 && c.gv == 0, "LFO 2 RATE 0: the bar from 0");
    TSEL->p[P_L2RATE] = 64;
    lfo2ui_cell(r, 0, &c);
    check(c.gk == GK_BAR && c.gv == 64 && c.gmax == 127, "LFO 2 RATE 64: the free bar at 64 of 0 .. 127");
    TSEL->p[P_L2RATE] = -25;
    lfo2ui_cell(r, 0, &c);
    check(c.gk == GK_POS && c.gmax == LSYNC_N - 1 && c.gv == 0 && !strcmp(c.val, "16BAR"), "LFO 2 RATE -25: 25 places, the tick at the first, 16BAR");
    TSEL->p[P_L2RATE] = -DUR_N;
    lfo2ui_cell(r, 0, &c);
    check(c.gk == GK_POS && c.gv == LSYNC_N - 1 && !strcmp(c.val, "1/64"), "LFO 2 RATE -22: the tick at the last place, 1/64");
    ok = 1;
    for (k = 1; k <= (uint32_t)LSYNC_N; k++) {
        TSEL->p[P_L2RATE] = (int16_t)-(int32_t)k;
        lfo2ui_cell(r, 0, &c);
        ok &= c.gk == GK_POS && c.gv == (int16_t)(k <= (uint32_t)DUR_N ? k + 2u : (uint32_t)LSYNC_N - k);
    }
    check(ok, "LFO 2 RATE -1 .. -25: the tick walks by length (16BAR first, 1/64 last)");
    ui.force = 1;
    frame();
    ppm("opt-lfo2-sync");
    /* the knob: a fast turn down from a free rate stops at 0, then the divisions one a detent (lrate_step) */
    TSEL->p[P_L2RATE] = 5;
    ui.row[SCR_SOUND] = (uint8_t)r;
    frame();
    turn(EN_K1, -40);
    check(TSEL->p[P_L2RATE] == 0, "KNOB 1 turned down fast: LFO 2's RATE stops at 0 (as LFO's)");
    turn(EN_K1, -1);
    check(TSEL->p[P_L2RATE] == -25, "... one more detent: the first division, the longest (16BAR)");
    turn(EN_K1, -1);
    turn(EN_K1, -1);
    turn(EN_K1, -1);
    check(TSEL->p[P_L2RATE] == -1, "... three more: 8BAR 6BAR 4BAR");
    {   /* PHS (issue 129): a detent 45 degrees on the grid; LFO held + the knob (or PRESETS) by one */
        TSEL->p[P_L2PHS] = 0;
        ui.row[SCR_SOUND] = (uint8_t)(r + 1u);
        frame();
        turn(EN_K1, 2);
        ok = TSEL->p[P_L2PHS] == 32;
        lfo2ui_cell(r + 1u, 0, &c);
        ok &= !strcmp(c.val, "90") && !strcmp(c.unit ? c.unit : "", "deg");
        TSEL->p[P_L2PHS] = 40;
        turn(EN_K1, 1);
        ok &= TSEL->p[P_L2PHS] == 48;
        TSEL->p[P_L2PHS] = 40;
        turn(EN_K1, -1);
        ok &= TSEL->p[P_L2PHS] == 32;
        TSEL->p[P_L2PHS] = 112;
        turn(EN_K1, 3);
        ok &= TSEL->p[P_L2PHS] == 112;                  /* (315: the last, no wrap) */
        turn(EN_K1, -40);
        ok &= TSEL->p[P_L2PHS] == 0;
        press(B_LFO);
        turn(EN_K1, 3);
        release(B_LFO);
        ok &= TSEL->p[P_L2PHS] == 3;
        check(ok, "PHS: a detent 45 degrees (32 = 90 deg; off the grid: to the next grid value; 315 the last), LFO held + "
                  "the knob by one");
        TSEL->p[P_L2PHS] = 0;
        ui.row[SCR_SOUND] = (uint8_t)r;
        frame();
    }
    /* SHIFT held + a knob: the default */
    TSEL->p[P_L2RATE] = -4, TSEL->p[P_L2WAVE] = 3, TSEL->p[P_L2TRIG] = 1, TSEL->p[P_L2POL] = 1;
    ui.row[SCR_SOUND] = (uint8_t)r;
    frame();
    for (k = 0; k < 4u; k++) {
        press(B_OCTDN);
        turn(EN_K1 + k, 1);
        release(B_OCTDN);
    }
    check(TSEL->p[P_L2RATE] == TP[P_L2RATE].def && TSEL->p[P_L2WAVE] == TP[P_L2WAVE].def && TSEL->p[P_L2TRIG] == TP[P_L2TRIG].def &&
              TSEL->p[P_L2POL] == TP[P_L2POL].def && ui.scr == SCR_SOUND,
          "SHIFT held + each knob on LFO 2: RATE WAVE TRIG POL back to their defaults");
    TSEL->p[P_L2DA] = LT_SHP, TSEL->p[P_L2AA] = 30;
    ui.row[SCR_SOUND] = (uint8_t)rd;
    frame();
    turn(EN_K2, 2);
    check(TSEL->p[P_L2AA] > 30 && TSEL->p[P_L2DA] == LT_SHP, "LFO2 DEST: KNOB 2 moves AMT, not DEST");
    press(B_OCTDN);
    turn(EN_K2, 1);
    release(B_OCTDN);
    check(TSEL->p[P_L2AA] == TP[P_L2AA].def, "SHIFT held + KNOB 2 on LFO2 DEST: AMT back to 0");
    press(B_OCTDN);
    turn(EN_K1, 1);
    release(B_OCTDN);
    check(TSEL->p[P_L2DA] == TP[P_L2DA].def, "SHIFT held + KNOB 1: DEST back to OFF");
    /* the LFO 2 wave drawn, LFO2 DEST under it */
    TSEL->p[P_L2WAVE] = 1, TSEL->p[P_L2POL] = 1;
    ui.row[SCR_SOUND] = (uint8_t)r;
    ui.force = 1;
    frames(2);
    ppm("opt-lfo2-page");
    ui.row[SCR_SOUND] = (uint8_t)rd;
    TSEL->p[P_L2DA] = LT_FLT, TSEL->p[P_L2AA] = -40;
    ui.force = 1;
    frames(2);
    ppm("opt-lfo2-dest");
    check(snd_graph_page(rd)->graph == GR_LFO2 && snd_graph_page(rl) != snd_page(r) && snd_graph_page(rl)->graph == GR_LFO,
          "the graph: LFO2 DEST shows LFO 2's wave, LFO DEST still LFO's");
    {   /* SLEW (issue 129): its name on both LFOs' WAVE card; its picture a glide through the S&H picture's steps */
        static uint16_t shot[240 * 240];
        uint32_t rw = ins_row("LFO");
        int32_t x, v, pv = 0, jump = 0, sjump = 0, ps = 0;
        TSEL->p[P_L2DA] = LT_OFF, TSEL->p[P_L2AA] = 0, TSEL->p[P_L2POL] = 0, TSEL->p[P_L2WAVE] = LFO_WAVE_SLEW;
        lfo2ui_cell(r, 1, &c);
        check(!strcmp(c.label, "WAVE") && !strcmp(c.val, "SLEW"), "SLEW: LFO 2's WAVE card reads SLEW");
        frames(2);
        ppm("opt-lfo2-slew");
        memcpy(shot, screen, sizeof shot);
        TSEL->p[P_L2WAVE] = LFO_WAVE_SH;
        ui.force = 1;
        frames(2);
        check(memcmp(shot, screen, sizeof shot) != 0, "SLEW: LFO 2's picture is not S&H's");
        TSEL->p[P_LWAVE] = LFO_WAVE_SLEW;
        check(rw < SCR->rows(), "SLEW: the LFO row");
        if (rw < SCR->rows()) {
            lfo2ui_cell(rw, 1, &c);
            check(!strcmp(c.label, "WAVE") && !strcmp(c.val, "SLEW") && c.vp == &TSEL->p[P_LWAVE], "SLEW: LFO's WAVE card reads SLEW");
            frames(2);
            ppm("opt-lfo-slew");
        }
        TSEL->p[P_LWAVE] = 0, TSEL->p[P_L2WAVE] = 1;
        for (x = 0, v = 1; x < 240; x++) {              /* the picture's values (op_graph.c graph_lfo_v) */
            int32_t a = graph_lfo_v(LFO_WAVE_SLEW, 0, x), b = graph_lfo_v(LFO_WAVE_SH, 0, x);
            if (x) {
                jump = a - pv > jump ? a - pv : pv - a > jump ? pv - a : jump;
                sjump = b - ps > sjump ? b - ps : ps - b > sjump ? ps - b : sjump;
            }
            v &= x % 20 || a == b;
            pv = a, ps = b;
        }
        check(v && jump <= 65535 * 3 / 40 + 3 && sjump > 2 * jump,
              "SLEW: the picture glides (no column further than the steepest glide from the last), meeting each S&H step at its start");
    }
    /* the drum track has no LFO 2 */
    song.sel = TRK_DRUM;
#if KLFO_ON
    {
        uint32_t a = ins_row("LFO 2"), b = ins_row("LFO2 DEST");
        check((a >= SCR->rows() || kit_lfo_pg(snd_page(a))) && (b >= SCR->rows() || kit_lfo_pg(snd_page(b))),
              "the drum track: no synth LFO 2 rows (LFO 2 there is the kit's, GRIDS_LFO)");
    }
#else
    check(ins_row("LFO 2") >= SCR->rows() && ins_row("LFO2 DEST") >= SCR->rows(), "the drum track: no LFO 2 rows");
#endif
    song.sel = 0;
    /* the AMT card's second bar */
    trk[0] = keep;
    reset_ui();
    song.sel = 0;
    TSEL->p[P_L2DA] = LT_OFF, TSEL->p[P_L2AA] = 0;
    {
        int32_t v, mn, mx;
        check(effv_synth(0, P_L2AA, &v, &mn, &mx) == 0u, "AMT with DEST OFF: nothing moves it, one bar");
        TSEL->p[P_L2DA] = LT_PIT, TSEL->p[P_L2AA] = 20;
        check(effv_synth(0, P_L2AA, &v, &mn, &mx) == 2u && mn == -384 && mx == 384, "AMT on PIT: an amount, the pitch now (+-24 semitones, core.h pit_depth)");
        TSEL->p[P_L2DA] = LT_AMP;
        check(effv_synth(0, P_L2AA, &v, &mn, &mx) == 2u && mn == 0 && mx == 127, "AMT on AMP: the voice's level now, 0 .. 127");
        TSEL->p[P_L2AA] = 0;
        check(effv_synth(0, P_L2AA, &v, &mn, &mx) == 0u, "AMT 0: one bar");
    }
#if FELUCCA_ANALOG2 && FELUCCA_ENG_ANALOG
    {
        track_t *t = &trk[0];
        track_t sv = *t;
        set_engine_of(t, 0);                            /* (slot 0: ANALOG) */
        t->engine = t->eng_req, t->xf_on = 0, t->xf = 0;
        check(ENG_IS(ENGINES[t->engine], ANALOG) && lfo2ui_cut_same(0, 0, 50) && lfo2ui_cut_same(1, 1, -40) &&
                  lfo2ui_cut_same(2, 0, 63) && lfo2ui_cut_same(3, 1, 30) && lfo2ui_cut_same(4, 0, 45),
              "live values: ANALOG 2's cutoff as rendered with LFO 2 on FLT (each wave, both POLs)");
        *t = sv;
    }
#endif
    trk[0] = keep;
    reset_ui();
}
