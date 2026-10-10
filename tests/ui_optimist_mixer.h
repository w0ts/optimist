/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, the horizontal mixer (docs/UI-OPTIMIST-DESIGN.md sections 4.1, 11.6), SCOPE, the header's
 * engine and no footer (11.5). Included by ui_optimist_test.c after its helpers. */
#include "../firmware/src/storage/settings_word.c"     /* the settings word (project.c's, not in PROJ_HOST) */

static uint32_t mx_row_y(uint32_t r) { return (uint32_t)OP_PY + (r - mxd.first) * MXL_H; }   /* row r on the screen */
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
    check(mx_row() == MXR_T1 && mxd.first == MXR_T1 && px_in(6, mx_row_y(MXR_T1) + 3u, 16, 14, C_WHITE) && MXL_H == 41 &&
              px_in(0, mx_row_y(MXR_DR) + 2u, 4, 30, trk_col(TRK_DRUM)) && !px_in(0, mx_row_y(MXR_DR) + (uint32_t)MXL_H + 2u, 4, 1, lane_col(0)),
          "the mixer opens on T1 T2 T3 DR, four rows of 41 px (MASTER above, out of view)");
    for (i = 1; i < MXR_N - MXR_T1; i++) {              /* ALGORITHM down: T2 T3 DR, then the 16 lanes */
        turn(EN_ALGO, 1);
        ok &= mx_row() == MXR_T1 + i;
        ok &= i < TRK_DRUM ? song.sel == i : song.sel == TRK_DRUM;
        if (MXR_T1 + i >= MXR_LANE0)
            ok &= lane_selected() == MXR_T1 + i - MXR_LANE0;
    }
    turn(EN_ALGO, 1);
    check(ok && mx_row() == MXR_N - 1u, "ALGORITHM walks T1 T2 T3 DR and the 16 lanes (each selected), stopping at the last");
    ui.force = 1;
    frame();
    check(mxd.first + MXL_N == MXR_N && px_in(0, mx_row_y(MXR_N - 1u), 240, MXL_H - 1, col_shade(lane_col(15), 3u)),
          "the four rows scroll with it, a row at a time; the selected row tinted in its colour (the 16th lane's)");
    ppm("opt-mixer-h-lane16");
    for (i = 0, ok = 1; i < DRUM_LANES; i++) {          /* back up through the lanes to DR, T3 ... */
        turn(EN_ALGO, -1);
        ok &= mx_row() == MXR_N - 2u - i;
    }
    check(ok && mx_row() == MXR_DR && song.sel == TRK_DRUM, "ALGORITHM back: the lanes, then DR");
    turn(EN_ALGO, -3);
    check(mx_row() == MXR_T1 && song.sel == 0, "... T3, T2, T1");
    turn(EN_ALGO, -1);
    ui.force = 1;
    frame();
    check(mx_row() == MXR_MASTER && song.sel == 0 && mxd.first == 0, "past T1 up: the MASTER row, in view now (the track stays)");
    turn(EN_ALGO, -1);
    check(mx_row() == MXR_MASTER, "... and it stops there");
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
    check(mx_labels(MXR_T1, 0, "VOLUME", 0, 0, "PAN") && n >= 2u,
          "a track: VOLUME INSERT SEND PAN, then the rest");
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
    {
        cell_t c;
        mx.set = 0;
        mix_cell(MXR_T1, 1, &c);
        check(c.d == &TP[FXS_ON(FXT_DIST) ? P_DIST : P_DIST], "INSERT: the first insert in the slots (DIST by default)");
        mix_cell(MXR_T1, 2, &c);
        check(!FELUCCA_FX_REVERB || c.d == &TP[P_REV], "SEND: REV (in a slot)");
#if FELUCCA_MASTER_COMP
        fxs_load(0, FXT_COMP);                          /* COMP in the first slot: the insert is its amount */
        mix_cell(MXR_T1, 1, &c);
        check(c.d == &TP[P_TCOMP], "INSERT: COMP loaded in a slot before DIST, its amount (the slot's main value)");
        fxs_load(0, FXT_DIST);
#endif
    }
    check(mx_labels(MXR_DR, 0, "VOLUME", FELUCCA_TRK_FILT ? "FILTER" : 0, "FX ON", "KIT"), "DR: VOLUME FILTER FX KIT");
    check(mx_labels(MXR_LANE0 + 2u, 0, "LEVEL", "DRIVE", "REV", "CUT") && mx_labels(MXR_LANE0 + 2u, 1, "DLY", "CHO", "SOUND", 0),
          "a lane: LEVEL DRIVE REV CUT, then DLY CHO SOUND");
#if FELUCCA_MASTER_COMP
    check(mx_labels(MXR_MASTER, 0, "FILT", "THRS", "RATIO", "DUCK") && mx_labels(MXR_MASTER, 1, "DUST", "GAIN", "CEIL", 0),
          "MASTER: FILT THRS RATIO DUCK, then DUST GAIN CEIL");
#else
    check(mx_labels(MXR_MASTER, 0, "FILT", "DUST", "DUCK", 0), "MASTER: FILT DUST DUCK");
#endif
    mx.set = 0;
    turn(EN_SELECT, 1);
    {
        char h[40];
        head_title(SCR_HOME, mx_row(), h, sizeof h);
        check(mx_set(MXR_T1) == 1u && !strcmp(h, "T1 MORE"), "SELECT: the next knob set; the header names the row and the set (T1 more)");
    }
    ui.force = 1;
    frame();
    ppm("opt-mixer-h-set2");
    turn(EN_SELECT, 20);
    check(mx_set(MXR_T1) == last, "SELECT stops at the last set");
    turn(EN_SELECT, -20);
    check(mx_set(MXR_T1) == 0u, "... and at the first");
    tap(B_GLO);
    check(ui.scr == SCR_HOME && mx_set(MXR_T1) == 1u, "GLO tapped on the mixer: the next set");
    for (n = 0; n < mx_sets(MXR_T1); n++)
        tap(B_GLO);
    check(mx_set(MXR_T1) == last, "GLO again and again: it stops at the last set (nothing wraps)");
    op_enter(SCR_SOUND);
    tap(B_GLO);
    check(ui.scr == SCR_HOME, "GLO tapped elsewhere: the mixer");
    mx.set = 0;
    {   /* the knobs on the selected row */
        int16_t lv;
        turn(EN_ALGO, 1);                               /* T2 */
        lv = trk[1].p[P_LEVEL];
        turn(EN_K1, -3);
        check(trk[1].p[P_LEVEL] < lv && ui.hot == 0u, "T2's row: KNOB 1 its VOLUME");
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
        check(ui.scr == SCR_SOUND && snd_page(ui.row[SCR_SOUND]) == 0 && lane_selected() == 1u,
              "YES on a lane's row: the SOUND rows of that lane");
        reset_ui();
        mx.set = 0;
        ui.hot_lit = 0;
        tap(B_SAVE);
        check(ui.scr == SCR_PROJECT && !op_armed(), "SAVE tapped on the mixer, no cell picked: PROJECT [provisional]");
        tap(B_HOME);
        check(ui.scr == SCR_HOME, "... HOME: back to the mixer");
        press(B_HOME);
        frames(HOLD_FRAMES);
        release(B_HOME);
        check(ui.scr == SCR_SYSTEM, "HOME held alone past HOLD on the mixer: SYSTEM [provisional]");
        tap(B_HOME);
        check(ui.scr == SCR_HOME, "... HOME: back to the mixer");
        tap(B_HOME);
        check(ui.scr == SCR_SCOPE, "HOME tapped: still the scope (SCOPE keeps its entry)");
        tap(B_HOME);
        song.sel = TRK_DRUM;
        reset_ui();
        song.sel = TRK_DRUM;
        frame();
        press(B_HOME);
        frames(HOLD_FRAMES);
        key(4);
        release(B_HOME);
        check(ui.scr == SCR_HOME && lane_selected() == lane_of_key(4), "HOME held long + a key: the lane picked, no SYSTEM");
        press(B_HOME);
        frames(HOLD_FRAMES);
        press(B_GLO);
        release(B_GLO);
        release(B_HOME);
        check(ui.scr == SCR_HOME && lay.lock == LY_MIX, "HOME held long + a layer button: the layer locks, no SYSTEM");
        tap(B_SEQ);
        song.sel = 0;
        reset_ui();
        mx.set = 1;
        check(mix_find("FX ON"), "a track's FX ON in its sets");
        lv = trk[0].p[P_FXOFF];
        tap(B_SAVE);
        check(trk[0].p[P_FXOFF] != lv && ui.scr == SCR_HOME, "YES on FX ON toggles it (no SOUND)");
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
    while (mx_row() < MXR_LANE0 + 2u)
        turn(EN_ALGO, 1);
    ui.force = 1;
    frame();
    y = mx_row_y(MXR_DR);
    check(px_in(MXL_VX, y + MXL_SY, 11, MXL_SH, trk_col(TRK_DRUM)), "DR's sequence: every lane merged (the snare's step 1)");
    y = mx_row_y(MXR_LANE0 + 2u);
    check(px_in(MXL_VX, y + MXL_SY, 11, MXL_SH, lane_col(2)), "the snare lane's row: its own hit");
    y = mx_row_y(MXR_LANE0 + 3u);
    check(!px_in(MXL_VX, y + MXL_SY, 11, MXL_SH, lane_col(3)), "the clap lane's row: none");
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
        uint32_t x = MXL_VX + MXL_VW;
        turn(EN_ALGO, -1);                              /* MASTER, in view */
        song.g[G_CTHR] = -10;
        mc_gr_view = -6;                                /* 6 dB: 18 px from the right */
        frame();
        y = mx_row_y(MXR_MASTER);
        check(px_in(x - 17u, y + MXL_VY + 4u, 4, 4, C_WARN) && !px_in(x - 22u, y + MXL_VY + 4u, 3, 4, C_WARN),
              "MASTER's row: the compressor's 6 dB as an amber bar pushing in from the right (18 px)");
        ui.force = 1;
        frame();
        ppm("opt-mixer-h-master-gr");
        song.g[G_CTHR] = 0;
        frame();
        check(!px_in(MXL_VX, y + MXL_VY, MXL_VW, MXL_VH, C_WARN), "THRS and CEIL off: no compressor, no bar");
        mc_gr_view = 0;
        turn(EN_ALGO, 1);
        fxs_load(0, FXT_COMP);                          /* T1's COMP insert, reducing 8 dB */
        trk[0].p[P_TCOMP] = 60;
        tcomp[0].gr16 = (int32_t)(8 * 65536 / 6.02);
        ui_draw();                                      /* (no audio block between: the reduction as set) */
        y = mx_row_y(MXR_T1);
        check(px_in(x - 23u, y + MXL_VY + 4u, 4, 4, C_WARN) && !px_in(MXL_VX, mx_row_y(MXR_T1 + 1u) + MXL_VY, MXL_VW, MXL_VH, C_WARN),
              "T1's COMP insert: its reduction on T1's row (8 dB: 24 px), none on T2's");
        trk[0].p[P_TCOMP] = 0;
        frame();
        check(!px_in(MXL_VX, y + MXL_VY, MXL_VW, MXL_VH, C_WARN), "its amount 0: no bar");
        tcomp[0].gr16 = 0;
        fxs_load(0, FXT_DIST);
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
    {   /* 2x2: the list keeps the four tracks in view */
        op_cards = CARDS_2X2;
        reset_ui();
        ui.force = 1;
        frame();
        check(!cards_2x2() && MXL_H == 41 && mxd.first == MXR_T1 && !px_in(0, OY_CARD + OH_CARD, 240, 2, OP_SURF),
              "CARDS 2x2: the mixer keeps the 1x4 cards and its four 41 px rows (the other screens go 2x2)");
        ppm("opt-mixer-h-2x2");
        op_cards = CARDS_LINE;
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
    tap(B_HOME);
    check(ui.scr == SCR_SCOPE && scope_rows() == 5u && scope_src == 0u, "SCOPE: HOME tapped on the mixer; MASTER T1 T2 T3 DR");
    (void)r;
    /* one ring, switched: T2 plays a held note, T1 is silent, the drums too */
    song.sel = 1;
    fm1_in.notes |= 1u << 10;
    frames(3);
    turn(EN_SELECT, 2);                                 /* T2 */
    frames(3);
    for (pk = 0, i = 0; i < VIS_RING; i++)
        pk = vis_pcm[0][i] > pk ? vis_pcm[0][i] : pk;
    check(scope_src == 2u && pk > 0, "SCOPE T2: the ISR copies part 2's block into the ring (a note held)");
    ui.force = 1;
    frame();
    frame();
    ppm("opt-scope-t2");
    check(px_in(0, OY_PANEL, 240, SC_H, trk_col(1)) && px_in(0, SC_TAB_Y, 240, SC_TAB_H, trk_col(1)),
          "SCOPE T2: the trace and its tab in T2's colour");
    turn(EN_SELECT, -1);                                /* T1: silent, nothing written */
    frames(2);
    w0 = vis_wr;
    frames(2);
    check(scope_src == 1u && vis_wr == w0, "SCOPE T1 (silent): the ring is not written");
    turn(EN_SELECT, 3);                                 /* DR: the drums' part of the mix only */
    frames(3);
    for (pk = 0, i = 0; i < VIS_RING; i++)
        pk = (vis_pcm[0][i] < 0 ? -vis_pcm[0][i] : vis_pcm[0][i]) > pk ? (vis_pcm[0][i] < 0 ? -vis_pcm[0][i] : vis_pcm[0][i]) : pk;
    check(scope_src == SCOPE_DR && vis_wr != w0 && pk < 64, "SCOPE DR: the drums' part (silent here), not T2's note");
    turn(EN_SELECT, -4);                                /* MASTER */
    frames(3);
    for (pk = 0, i = 0; i < VIS_RING; i++)
        pk = vis_pcm[0][i] > pk ? vis_pcm[0][i] : pk;
    ui.force = 1;
    frames(2);
    ppm("opt-scope-master");
    check(scope_src == 0u && pk > 0 && px_in(0, OY_PANEL, 240, SC_H, C_HI), "SCOPE MASTER: the mix, in the palette's ink");
    fm1_in.notes &= ~(1u << 10);
    {
        cell_t c;
        scope_cell(0, 3, &c);
        check(c.label && (strcmp(c.label, "GR") == 0) == (FELUCCA_MASTER_COMP != 0), "SCOPE MASTER's cards: FILT THRS RATIO GR");
        scope_cell(2, 0, &c);
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

static void header_footer_tests(void)
{
    char t[32];
    reset_ui();
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
    check(px_in(60, 4, 170, 16, C_HI), "STEP: the header says the window and the pick (Steps 1-16 c.hat)");
    tap(B_HOME);
    song.sel = 0;
    op_enter(SCR_SOUND);
    ui.force = 1;
    frame();
    check(px_in(0, 202, 240, 38, C_GRAY) || px_in(0, 202, 240, 38, C_HI),
          "no footer: SOUND's rows go to the screen's foot");
    op_enter(SCR_HOME);
    press(B_FX);
    frames(HOLD_FRAMES);
    str_cpy(t, ui.msg, sizeof t);
    check(ui.msg_t > 0 && !strcmp(t, "HOME LOCKS IT"), "a layer opened: \"Home locks it\" in the header");
    release(B_FX);
    frames(2);
    check(ui.msg_t == 0, "the layer let go: its hint with it");
    press(B_FX);
    frames(HOLD_FRAMES);
    check(ui.msg_t == 0, "the hint once a power-on");
    release(B_FX);
    frames(2);
}
