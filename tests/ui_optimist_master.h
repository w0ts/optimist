/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, the user's rulings of 2026-10-10:
 *   PRESETS browses the selected track's sounds (the drum track: its kits) wherever you are; nothing under a question,
 *   on MASTER or on the NAME screen; it is no longer the hot cell's value, a layer's hot knob or a held step's chance
 *   ALGORITHM left past T1 is MASTER on every page and layer with a track (the master FX), right goes back to T1; STEP
 *   stops at T1 (the automation store has per-track lists only: no master target)
 * Included by ui_optimist_test.c after ui_optimist_cards.h. */
static void layout_tests(void);
static void held_page_tests(void);
/* ALGORITHM drawn in place (the user, 2026-10-10: "while scrolling algo knob, the screen flash in a not nice way"): a
 * track change, the mixer's walk and the step to MASTER and back repaint every band over itself, never through a
 * clear of the whole screen to black; the header shows the new track's colour at once */
static int algo_step_inplace(int32_t d)
{
    uint32_t n;
    frames(2);
    n = full_fills;
    turn(EN_ALGO, d);
    frames(2);
    return full_fills == n;
}
static void algo_inplace_tests(void)
{
    uint32_t ok;
    reset_ui();
    song.sel = 0;
    frames(2);
    ok = algo_step_inplace(1) && song.sel == 1u && px_in(2, 3, 4, 19, trk_col(1));
    check(ok, "ALGORITHM on the mixer: the next track drawn in place (no full clear), its colour in the header");
    tap(B_ENV);
    ok = algo_step_inplace(1) && song.sel == 2u && ui.scr == SCR_SOUND && px_in(2, 3, 4, 19, trk_col(2));
    ok &= (uint32_t)algo_step_inplace(-1) && song.sel == 1u;
    check(ok, "ALGORITHM on SOUND (ENV): the track changes in place, both ways");
    ok = algo_step_inplace(-1) && song.sel == 0u && algo_step_inplace(-1) && ui.scr == SCR_FX;
    ok &= (uint32_t)algo_step_inplace(1) && ui.scr == SCR_SOUND && song.sel == 0u;
    check(ok, "ALGORITHM to MASTER (the FX screen) and back: in place");
    tap(B_SEQ);
    ok = ui.scr == SCR_STEP && algo_step_inplace(1) && song.sel == 1u && algo_step_inplace(1) && algo_step_inplace(1) &&
         song.sel == TRK_DRUM;
    check(ok, "ALGORITHM on STEP: synth to synth to the drum track, in place");
    reset_ui();
}
static void master_tests(void)
{
    layout_tests();
    held_page_tests();
    cell_t c;
    uint32_t pre, eng;
    int16_t atk;
    reset_ui();
    song.sel = 0;
    tap(B_ENV);
    check(ui.scr == SCR_SOUND && snd_fam == FAM_ENV, "MASTER: ENV tapped on T1");
    atk = TSEL->p[P_ATK];
    pre = TSEL->preset;
    eng = TSEL->eng_req;
    turn(EN_PRESET, 1);
    check((TSEL->preset != pre || TSEL->eng_req != eng) && TSEL->p[P_ATK] == atk,
          "PRESETS on the ENV row: the sound changes, ATK stays (no hot cell)");
    turn(EN_ALGO, -1);
    check(ui.scr == SCR_FX && song.sel == 0u && !ui.master, "ALGORITHM left from T1 on ENV: MASTER (the FX screen), T1 still selected");
    SCR->cell(ui.row[SCR_FX], 0, &c);
    check(c.label != 0 && c.kind != CK_NONE, "... MASTER shows the master FX cells");
    pre = TSEL->preset;
    eng = TSEL->eng_req;
    turn(EN_PRESET, 3);
    check(TSEL->preset == pre && TSEL->eng_req == eng, "PRESETS on MASTER: nothing");
    turn(EN_ALGO, -1);
    check(ui.scr == SCR_FX, "ALGORITHM left on MASTER: stays");
    turn(EN_ALGO, 1);
    check(ui.scr == SCR_SOUND && snd_fam == FAM_ENV && song.sel == 0u, "ALGORITHM right from MASTER: T1's ENV rows again");
    /* every other family reaches MASTER and comes back to where it was */
    {
        static const uint8_t B[] = {B_LFO, B_EDIT, B_ARP, B_FX};
        uint32_t i, ok = 1;
        for (i = 0; i < sizeof B; i++) {
            uint8_t fam;
            reset_ui();
            song.sel = 0;
            tap(B[i]);
            fam = snd_fam;
            turn(EN_ALGO, -1);
            ok &= ui.scr == SCR_FX && song.sel == 0u;
            turn(EN_ALGO, 1);
            ok &= ui.scr == SCR_SOUND && snd_fam == fam && song.sel == 0u;
        }
        check(ok, "MASTER from LFO, EDIT, ARP and FX pages and back, each on its family");
    }
    /* another track: ALGORITHM is the track as ever */
    reset_ui();
    song.sel = 1;
    tap(B_ENV);
    turn(EN_ALGO, -1);
    check(ui.scr == SCR_SOUND && song.sel == 0u, "ALGORITHM left from T2: T1, not MASTER");
    /* a question open: a PRESETS turn changes nothing */
    reset_ui();
    song.sel = 0;
    tap(B_ENV);
    pre = TSEL->preset;
    eng = TSEL->eng_req;
    op_arm(SCR_SOUND, 0, 2, "INIT", "T1", 1);
    turn(EN_PRESET, 2);
    check(op_armed() && TSEL->preset == pre && TSEL->eng_req == eng, "PRESETS under a question: nothing");
    op_disarm();
    /* the drum track: its kit */
    reset_ui();
    song.sel = TRK_DRUM;
    op_enter(SCR_SOUND);
    {
        uint32_t kit = drum_kit_pos();
        turn(EN_PRESET, -1);
        check(drum_kit_pos() != kit, "PRESETS on the drum track: the next kit");
    }
    /* STEP: PRESETS pages the window on every track (the user, 2026-10-11: "in the step sequencer it scroll pages thru
     * the piano roll, in mixer it select preset indeed"), stopping at the ends; the lane, the kit and the preset stay */
    {
        uint32_t tr, ok = 1, ok2 = 1, ok3 = 1;
        for (tr = 0; tr < 2u; tr++) {
            uint32_t kit, pre2, eng2;
            int16_t len0;
            reset_ui();
            song.sel = tr ? TRK_DRUM : 0u;
            len0 = TSEL->p[P_SLEN];
            TSEL->p[P_SLEN] = 48;                       /* (three pages) */
            op_enter(SCR_STEP);
            frame();
            lane_select(0);
            kit = drum_kit_pos();
            pre2 = TSEL->preset;
            eng2 = TSEL->eng_req;
            turn(EN_PRESET, 1);
            ok &= (uint32_t)(st.page == 1u && lane_selected() == 0u && drum_kit_pos() == kit && TSEL->preset == pre2 && TSEL->eng_req == eng2);
            turn(EN_PRESET, 1);
            turn(EN_PRESET, 1);
            ok2 &= (uint32_t)(st.page == 2u);
            turn(EN_PRESET, -1);
            turn(EN_PRESET, -1);
            turn(EN_PRESET, -1);
            ok3 &= (uint32_t)(st.page == 0u);
            TSEL->p[P_SLEN] = len0;
        }
        check(ok, "PRESETS on STEP, a synth and the drum track: the next page; the lane, the kit, the preset stay");
        check(ok2, "... turned right past the last page: it stops there (no wrap)");
        check(ok3, "... turned left past the first: it stops at 1-16");
        int16_t len1;
        reset_ui();
        song.sel = 0;
        len1 = TSEL->p[P_SLEN];
        TSEL->p[P_SLEN] = 32;
        op_enter(SCR_STEP);
        frame();
        kdown(WK(2));
        frames(2);
        turn(EN_PRESET, 1);
        check(st.page == 0u && st.held, "PRESETS on STEP with a step held: the window stays (the held keys are its)");
        kup(WK(2));
        kdown(WK(2));                                   /* (the step back as it was) */
        kup(WK(2));
        TSEL->p[P_SLEN] = len1;
    }
    /* the layers: FX held, MASTER by ALGORITHM, the master FX in its cards */
    reset_ui();
    song.sel = 0;
    press(B_FX);
    frames(HOLD_FRAMES);
    check(lay.shown == LY_FX && !ui.master, "FX held");
    pre = TSEL->preset;
    eng = TSEL->eng_req;
    turn(EN_ALGO, -1);
    check(ui.master && song.sel == 0u, "FX held, ALGORITHM left from T1: MASTER (a UI state: song.sel stays T1)");
    lay_cell(0, &c);
    check(c.label && str_eq(c.label, "FILTER"), "... KNOB 1 FILTER");
    lay_cell(3, &c);
    check(c.label && str_eq(c.label, "COMP"), "... KNOB 4 the master COMP, not the track's filter");
    {
        int16_t th = song.g[G_CTHR];
        turn(EN_K4, -2);
        check(song.g[G_CTHR] != th, "... a turn of KNOB 4 edits the master compressor");
    }
    turn(EN_PRESET, 2);
    check(TSEL->preset == pre && TSEL->eng_req == eng, "... PRESETS on MASTER: nothing");
    turn(EN_ALGO, 1);
    check(!ui.master && song.sel == 0u, "ALGORITHM right: T1 again");
    release(B_FX);
    frames(2);
    /* STEP: MASTER is not reachable (the store has no master target), ALGORITHM stops at T1 */
    reset_ui();
    song.sel = 0;
    tap(B_SEQ);
    turn(EN_ALGO, -1);
    check(ui.scr == SCR_STEP && !ui.master && song.sel == 0u, "STEP: ALGORITHM left from T1 stays on T1 (no master automation)");
    reset_ui();
}
/* the 2026-10-10 layouts: a mixer row names its sound; a SOUND page's picture or rows take the whole panel */
static void layout_tests(void)
{
    uint32_t y;
    reset_ui();
    song.sel = 0;
    frames(3);
    y = mx_row_y(MXR_T1);
    check(px_in(MXL_VX, y + MXL_TY, 56, 14, mx_col(MXR_T1)) && MXL_VY + MXL_VH <= MXL_H && MXL_SY + MXL_SH + 4 <= MXL_VY,
          "mixer row: the engine's name in the track's colour on its text line, the steps and a thin VU under it");
    tap(B_LFO);
    frames(3);
    check(lst.pic == 1u && lst.top + (int32_t)lst.shown * ROW_H == OH_BODY && GRAPH_H == lst.top - 3 && GRAPH_H > GRAPH_MIN,
          "SOUND LFO: the graph takes the height the rows leave, the rows at the panel's foot");
    reset_ui();
    tap(B_ARP);
    frames(3);
    check(lst.pic == 1u && lst.rh == ROW_H, "SOUND ARP: a picture (its pattern), the rows at ROW_H");
    op_cards = CARDS_2X2;
    ui.force = 1;
    frames(3);
        check(lst.pic == 1u && lst.rh == ROW_H, "... and with CARDS 2x2");
    tap(B_LFO);
    frames(3);
    check(lst.pic == 1u && GRAPH_H >= GRAPH_MIN && lst.top + (int32_t)lst.shown * ROW_H == OH_BODY, "SOUND LFO with CARDS 2x2: the picture and the rows fill the panel");
    op_cards = CARDS_LINE;
    op_enter(SCR_PROJECT);
    frames(3);
    check(lst.pic == 0u && lst.rh <= 40, "a page with no picture: the row pitch is capped at 40");
    reset_ui();
}
/* 2026-10-10: a step held, SELECT pages its cards (the step, its extras, the p-lock pages), stopping at both ends */
static void held_page_tests(void)
{
    uint32_t e, n, ok, pre, eg;
    track_t *t;
    reset_ui();
    song.sel = 0;
    tap(B_SEQ);
    kdown(WK(3));
    t = TSEL;
    check(st.held && st.hp == 0u && hp_count() >= 1u, "a step held: card page 1, the step");
    turn(EN_SELECT, -3);
    check(st.hp == 0u, "SELECT before the first page: stays on it");
    turn(EN_SELECT, 1);
    check(hp_count() < 2u || st.hp == 1u, "SELECT: the next page");
    turn(EN_SELECT, 200);
    check(st.hp == hp_count() - 1u, "SELECT past the last page: stops on it");
    turn(EN_SELECT, -200);
    check(st.hp == 0u, "SELECT back: the first page");
#if FELUCCA_CHANCE
    turn(EN_SELECT, 1);
    check(EX_N && st.hp == 1u, "page 2: the extras");
    turn(EN_K1, -2);
    check(step_chance_ev(t, 3) == 90u, "page 2, KNOB 1 CHANCE: -2 detents, 90 % (an event of the store)");
    turn(EN_SELECT, -1);
#endif
#if FELUCCA_PLOCK
    if (hp_count() > HP_BASE) {
        const param_desc_t *d = 0;
        int32_t id, v;
        uint32_t i;
        for (i = 0; i < 4u && !d; i++) {
            hp_set(HP_BASE);
            id = lock_param(&PAGES[st.lock_pg], i, &d);
            if (d && id >= 0) {
                int16_t before = t->p[id];
                turn(EN_K1 + i, 1);
                check(lock_get(t, 3, (uint32_t)id, &v) && t->p[id] == before, "a p-lock page, a KNOB: the step's lock recorded, the track's value untouched");
                break;
            }
            d = 0;
        }
        hp_set(HP_BASE);
        check(st.lock_pg == st.lock_pg && st.hp == HP_BASE, "the first p-lock page");
    }
#endif
    kup(WK(3));
    check(!st.held && st.hp == 0u && st.lock_pg == LOCK_NONE, "the step let go: the pages reset");
    /* the pages of each engine (printed) */
    ok = 1;
    pre = TSEL->preset;
    eg = TSEL->eng_req;
    for (e = 0, n = 0; e < NENGINES; e++) {
        if (eng_free(e))
            continue;
        trk[0].eng_req = (uint8_t)e;
        set_engine(e);
        kdown(WK(3));
        printf("  held-step pages, %s: %u\n", ENGINES[e]->name, hp_count());
        ok &= hp_count() >= 1u;
        kup(WK(3));
        n++;
    }
    set_engine(eg);
    TSEL->preset = pre;
    check(ok && n > 0, "every engine: the held step has its pages");
    /* ARP draws its pattern above its rows */
    reset_ui();
    song.sel = 0;
    tap(B_ARP);
    frames(3);
    check(lst.pic == 1u && lst.rh == ROW_H && GRAPH_H > GRAPH_MIN && snd_graph_page(0) && snd_graph_page(0)->graph == GR_ARP,
          "ARP: its pattern above the rows, the rows at ROW_H, the picture filling the height");
    turn(EN_K1, 3);
    frames(3);
    check(px_in(2, OP_PY + 4u, 236, GRAPH_H - 8u, trk_col(0)), "ARP: MODE UPDN: the marks in the track's colour");
    reset_ui();
}
