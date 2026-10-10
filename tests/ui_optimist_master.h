/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, the user's rulings of 2026-10-10:
 *   PRESETS browses the selected track's sounds (the drum track: its kits) wherever you are; nothing under a question,
 *   on MASTER or on the NAME screen; it is no longer the hot cell's value, a layer's hot knob or a held step's chance
 *   ALGORITHM left past T1 is MASTER on every page and layer with a track (the master FX), right goes back to T1; STEP
 *   stops at T1 (the automation store has per-track lists only: no master target)
 * Included by ui_optimist_test.c after ui_optimist_cards.h. */
static void layout_tests(void);
static void master_tests(void)
{
    layout_tests();
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
    check(lst.pic == 0u && lst.rh > ROW_H && (int32_t)lst.shown * lst.rh > OH_BODY - (int32_t)lst.shown,
          "SOUND ARP (no graph): taller rows fill the panel");
    op_cards = CARDS_2X2;
    ui.force = 1;
    frames(3);
    check(lst.rh > ROW_H && (int32_t)lst.shown * lst.rh > OP_PH - (int32_t)lst.shown, "... and with CARDS 2x2");
    tap(B_LFO);
    frames(3);
    check(lst.pic == 1u && GRAPH_H >= GRAPH_MIN && lst.top + (int32_t)lst.shown * ROW_H == OH_BODY, "SOUND LFO with CARDS 2x2: the picture and the rows fill the panel");
    op_cards = CARDS_LINE;
    reset_ui();
}
