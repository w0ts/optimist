/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, LFO 2 (builder item LFO2; core/params.c PAGES, voice.c lfo2_tick):
 *   the pages   "LFO 2" (RATE WAVE TRIG POL) and "LFO2 DEST" (DEST AMT, two empty cards) are SOUND rows right after
 *               LFO DEST, on the synth tracks only, and only with LFO2 built in (a build without: no such row)
 *   the cards   each maps to its P_L2* value; RATE shows as LFO's does (the free bar from 0, a synced division as ten
 *               cells), HOME held + a knob puts the value back to its default
 *   the bars    AMT's second bar is the destination's actual value, equal to what the render used (ANALOG 2's cutoff)
 * Included after ui_optimist_inslots.h (ins_row). */
static void lfo2ui_off_tests(void)
{
    uint32_t r;
    song.sel = 0;
    r = ins_row("LFO 2");
    check(r >= SCR->rows(), "no LFO 2 page without LFO2 built in");
    r = ins_row("LFO2 DEST");
    check(r >= SCR->rows(), "no LFO2 DEST page without LFO2 built in");
}
#if FELUCCA_LFO2
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
    t->p[P_E4] = 50, t->p[P_E7] = 0, t->p[P_A2FENV] = 0, t->p[P_LD_FLT] = t->p[P_ED_FLT] = 0;
    t->p[P_LD_SHP] = t->p[P_ED_SHP] = t->p[P_LD_PIT] = t->p[P_ED_PIT] = 0;
    t->p[P_L2RATE] = 100, t->p[P_L2WAVE] = (int16_t)wave, t->p[P_L2POL] = (int16_t)pol, t->p[P_L2TRIG] = 1;
    t->p[P_L2DEST] = L2D_FLT, t->p[P_L2AMT] = (int16_t)amt;
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
    check(r == rl + 1u && rd == r + 1u && snd_page(r)->fam == FAM_LFO && snd_page(rd)->fam == FAM_LFO,
          "... right after LFO DEST, in the LFO family: LFO, LFO DEST, LFO 2, LFO2 DEST");
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
    for (k = 0; k < 4u; k++) {                          /* LFO2 DEST: DEST AMT, two empty */
        lfo2ui_cell(rd, k, &c);
        if (k == 0)
            ok &= c.vp == &TSEL->p[P_L2DEST] && c.kind == CK_VAL;
        else if (k == 1)
            ok &= c.vp == &TSEL->p[P_L2AMT] && c.d == &TP[P_L2AMT] && c.gk == GK_BIP;
        else
            ok &= c.label == 0 && c.kind == CK_NONE && c.vp == 0;
    }
    check(ok, "LFO2 DEST: DEST, AMT (a +/- card), two empty cards");
    /* RATE as LFO's */
    TSEL->p[P_L2RATE] = 0;
    lfo2ui_cell(r, 0, &c);
    check(c.gk == GK_BAR && c.gmin == 0 && c.gmax == 127 && c.gv == 0, "LFO 2 RATE 0: the bar from 0");
    TSEL->p[P_L2RATE] = 64;
    lfo2ui_cell(r, 0, &c);
    check(c.gk == GK_BAR && c.gv == 64 && c.gmax == 127, "LFO 2 RATE 64: the free bar at 64 of 0 .. 127");
    TSEL->p[P_L2RATE] = -1;
    lfo2ui_cell(r, 0, &c);
    check(c.gk == GK_DOTS && c.gmax == LSYNC_N - 1 && c.gv == 0 && !strcmp(c.val, "4BAR"), "LFO 2 RATE -1: ten cells, the first lit, 4BAR");
    TSEL->p[P_L2RATE] = -LSYNC_N;
    lfo2ui_cell(r, 0, &c);
    check(c.gk == GK_DOTS && c.gv == LSYNC_N - 1 && !strcmp(c.val, "1/32"), "LFO 2 RATE -10: the tenth lit, 1/32");
    ok = 1;
    for (k = 1; k <= (uint32_t)LSYNC_N; k++) {
        TSEL->p[P_L2RATE] = (int16_t)-(int32_t)k;
        lfo2ui_cell(r, 0, &c);
        ok &= c.gk == GK_DOTS && c.gv == (int16_t)(k - 1u);
    }
    check(ok, "LFO 2 RATE -1 .. -10: the lit cell walks 0 .. 9");
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
    check(TSEL->p[P_L2RATE] == -1, "... one more detent: the first division");
    /* HOME held + a knob: the default */
    TSEL->p[P_L2RATE] = -4, TSEL->p[P_L2WAVE] = 3, TSEL->p[P_L2TRIG] = 1, TSEL->p[P_L2POL] = 1;
    ui.row[SCR_SOUND] = (uint8_t)r;
    frame();
    for (k = 0; k < 4u; k++) {
        press(B_HOME);
        turn(EN_K1 + k, 1);
        release(B_HOME);
    }
    check(TSEL->p[P_L2RATE] == TP[P_L2RATE].def && TSEL->p[P_L2WAVE] == TP[P_L2WAVE].def && TSEL->p[P_L2TRIG] == TP[P_L2TRIG].def &&
              TSEL->p[P_L2POL] == TP[P_L2POL].def && ui.scr == SCR_SOUND,
          "HOME held + each knob on LFO 2: RATE WAVE TRIG POL back to their defaults");
    TSEL->p[P_L2DEST] = L2D_SHP, TSEL->p[P_L2AMT] = 30;
    ui.row[SCR_SOUND] = (uint8_t)rd;
    frame();
    turn(EN_K2, 2);
    check(TSEL->p[P_L2AMT] > 30 && TSEL->p[P_L2DEST] == L2D_SHP, "LFO2 DEST: KNOB 2 moves AMT, not DEST");
    press(B_HOME);
    turn(EN_K2, 1);
    release(B_HOME);
    check(TSEL->p[P_L2AMT] == TP[P_L2AMT].def, "HOME held + KNOB 2 on LFO2 DEST: AMT back to 0");
    press(B_HOME);
    turn(EN_K1, 1);
    release(B_HOME);
    check(TSEL->p[P_L2DEST] == TP[P_L2DEST].def, "HOME held + KNOB 1: DEST back to OFF");
    /* the LFO 2 wave drawn, LFO2 DEST under it */
    TSEL->p[P_L2WAVE] = 1, TSEL->p[P_L2POL] = 1;
    ui.row[SCR_SOUND] = (uint8_t)r;
    ui.force = 1;
    frames(2);
    ppm("opt-lfo2-page");
    ui.row[SCR_SOUND] = (uint8_t)rd;
    TSEL->p[P_L2DEST] = L2D_FLT, TSEL->p[P_L2AMT] = -40;
    ui.force = 1;
    frames(2);
    ppm("opt-lfo2-dest");
    check(snd_graph_page(rd) == snd_page(r) && snd_graph_page(rl) != snd_page(r) && snd_graph_page(rl)->graph == GR_LFO,
          "the graph: LFO2 DEST shows LFO 2's wave, LFO DEST still LFO's");
    /* the drum track has no LFO 2 */
    song.sel = TRK_DRUM;
    check(ins_row("LFO 2") >= SCR->rows() && ins_row("LFO2 DEST") >= SCR->rows(), "the drum track: no LFO 2 rows");
    song.sel = 0;
    /* the AMT card's second bar */
    trk[0] = keep;
    reset_ui();
    song.sel = 0;
    TSEL->p[P_L2DEST] = L2D_OFF, TSEL->p[P_L2AMT] = 0;
    {
        int32_t v, mn, mx;
        check(effv_synth(0, P_L2AMT, &v, &mn, &mx) == 0u, "AMT with DEST OFF: nothing moves it, one bar");
        TSEL->p[P_L2DEST] = L2D_PIT, TSEL->p[P_L2AMT] = 20;
        check(effv_synth(0, P_L2AMT, &v, &mn, &mx) == 2u && mn == -192 && mx == 192, "AMT on PIT: an amount, the pitch now (+-12 semitones)");
        TSEL->p[P_L2DEST] = L2D_AMP;
        check(effv_synth(0, P_L2AMT, &v, &mn, &mx) == 2u && mn == 0 && mx == 127, "AMT on AMP: the voice's level now, 0 .. 127");
        TSEL->p[P_L2AMT] = 0;
        check(effv_synth(0, P_L2AMT, &v, &mn, &mx) == 0u, "AMT 0: one bar");
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
#endif
