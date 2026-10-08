/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, the DRUM MIXER, SCOPE, the mixer's master column, the header's engine and no footer
 * (docs/UI-OPTIMIST-DESIGN.md section 11.5). Included by ui_optimist_test.c after its helpers. */
#include "../firmware/src/storage/settings_word.c"     /* DR MIX in the settings word (project.c's, not in PROJ_HOST) */

static void dm_pick(uint32_t lane)                     /* HOME held + the lane's key */
{
    fm1_in.buttons |= BT(B_HOME);
    frame();
    key(WK(lane));
    fm1_in.buttons &= ~BT(B_HOME);
    frame();
}
static uint32_t dm_row_of(const char *name)
{
    uint32_t r;
    for (r = 0; r < NDMX; r++)
        if (!strcmp(DMX[r].name, name))
            return r;
    return 0xFF;
}
static void drum_mixer_tests(void)
{
    char h[40];
    int32_t v;
    uint32_t r, l, i, lit_all;
    reset_ui();
    song.sel = TRK_DRUM;
    frame();
    dm_view = DMV_4;
    dm_pick(6);                                         /* PEDAL HAT: the second block */
    ui.force = 1;
    frame();
    ppm("opt-dmix-4");
    check(ui.scr == SCR_DMIX && dm.blk == 1u && lane_selected() == 6u && ui.hot == 2u,
          "drum mixer: HOME held + a drum key on the mixer opens it on the sound's block, the sound selected");
    head_title(SCR_DMIX, 0, h, sizeof h);
    check(!strcmp(h, "DRUMS LEVEL") && DMX[0].id == DE_LEVEL && DMX[1].id == DE_CUT && DMX[NDMX - 1u].kind == DK_ENTER,
          "drum mixer: the walk LEVEL, CUT (PAN's place), the sends, DRIVE, FILTER, FX, SOUND; the header Drums level");
    check(screen[(OY_CARD + 20u) * 240u + 113u] != swap16(OP_SURF) && px_in(MX_X(0), MIX_Y + 200u, MX_W, 10, OP_SURF),
          "drum mixer: no cards, the strips to the screen's foot (no footer)");
    check(px_in(MX_X(2), MIX_Y + 60u, 1, 40, lane_col(6)) && !px_in(MX_X(1), MIX_Y + 60u, 1, 40, lane_col(5)),
          "drum mixer: the selected sound's strip framed in its colour");
    lit_all = 1;
    for (i = 0; i < 4u; i++)                            /* the faders lit (framed) on the four strips */
        lit_all &= (uint32_t)px_in((uint32_t)MX_X(i) + dm_fader_x() - 2u, MIX_Y + MX_FADER_Y - 2u, 4, 1, lane_col(4u + i));
    check(lit_all, "drum mixer: LEVEL lit on the four strips, each in its sound's colour");
    turn(EN_SELECT, 1);
    lit_all = 1;
    for (i = 0; i < 4u; i++)
        lit_all &= (uint32_t)px_in((uint32_t)MX_X(i) + 2u, MIX_Y + MX_PAN_Y, MX_W - 4u, 1, lane_col(4u + i));
    check(ui.row[SCR_DMIX] == 1u && lit_all, "drum mixer: SELECT walks to CUT, lit at the four strips' foot");
#if FELUCCA_DRUM_EDIT
    turn(EN_SELECT, -1);
    v = dl.ofs[5][DE_LEVEL];
    turn(EN_K2, -3);
    check(dl.ofs[5][DE_LEVEL] < v && ui.hot == 1u, "drum mixer: KNOB 2 is the block's second sound's LEVEL (O.HAT)");
    v = dl.ofs[5][DE_LEVEL];
    turn(EN_PRESET, 1);
    check(dl.ofs[5][DE_LEVEL] == v + 1, "drum mixer: PRESETS the hot sound, one unit");
    fm1_in.buttons |= BT(B_HOME);
    turn(EN_K2, 1);
    fm1_in.buttons &= ~BT(B_HOME);
    frame();
    check(dl.ofs[5][DE_LEVEL] == 0 && ui.scr == SCR_DMIX, "drum mixer: HOME held + a knob, its default; no NO");
    {
        cell_t c;
        dm_cell(1, 0, &c);
        check(c.d && c.gk == GK_BIP, "drum mixer: CUT from the centre, as PAN");
    }
#endif
#if FELUCCA_FX_REVERB
    r = dm_row_of("REV");
    ui.row[SCR_DMIX] = (uint8_t)r;
    v = (int32_t)dsend_rev(dsend[7]);
    turn(EN_K4, 3);
    check(r != 0xFFu && (int32_t)dsend_rev(dsend[7]) > v, "drum mixer: REV, KNOB 4 the RIM's reverb send");
#endif
#if FELUCCA_FX_DIST
    {
        cell_t c;
        dm_cell(dm_row_of("DRIVE"), 0, &c);
        check(dm_row_of("DRIVE") != 0xFFu && (c.d || !strcmp(c.val, "-")), "drum mixer: DRIVE, or \"-\" on a sound with none");
    }
#endif
    {
        cell_t c;
        dm_cell(dm_row_of("FX ON"), 0, &c);
        check(!c.d && !strcmp(c.val, "-"), "drum mixer: FX ON \"-\" (the track's, not a sound's)");
    }
    ui.row[SCR_DMIX] = 0;
    dm_pick(13);                                        /* SHAKER: the last block */
    check(ui.scr == SCR_DMIX && dm.blk == 3u && lane_selected() == 13u, "drum mixer: the pick moves inside it");
    press(B_HOME);
    press(B_OCTDN);
    release(B_OCTDN);
    release(B_HOME);
    check(ui.scr == SCR_DMIX && dm.blk == 2u, "drum mixer: HOME + OCT- the block before, no NO");
    for (i = 0; i < 5u; i++) {
        press(B_HOME);
        press(B_OCTUP);
        release(B_OCTUP);
        release(B_HOME);
    }
    check(ui.scr == SCR_DMIX && dm.blk == 3u, "drum mixer: HOME + OCT+ the block after, stopping at the last");
    for (l = 0, i = 0; i < 30u && !dm.lit[12]; i++) {   /* a hit: the RIDE's key, its flash */
        fm1_in.notes |= 1u << WK(12);
        frame();
        fm1_in.notes &= ~(1u << WK(12));
        l = dm.lit[12];
    }
    frame();
    check(l == 6u || dm.lit[12] > 0u, "drum mixer: a hit lights its sound's flash (the kit pads' frames)");
    check(px_in((uint32_t)MX_X(0) + dm_flash_x(), MIX_Y + MX_FADER_Y + METER_H - 4u, dm_flash_w(), 4, lane_col(12)) ||
              px_in((uint32_t)MX_X(0) + dm_flash_x(), MIX_Y + MX_FADER_Y + METER_H - 4u, dm_flash_w(), 4, C_WHITE),
          "drum mixer: the flash drawn in the meter's place");
    frames(8);
    check(dm.lit[12] == 0u, "drum mixer: the flash falls off");
    ui.row[SCR_DMIX] = (uint8_t)(NDMX - 1u);
    turn(EN_K3, 1);
    tap(B_SAVE);
    check(ui.scr == SCR_SOUND && lane_selected() == 14u, "drum mixer: SOUND row, YES opens the hot sound's SOUND rows");
    dm_pick(2);
    tap(B_HOME);
    check(ui.scr == SCR_HOME, "drum mixer: HOME tapped, back to the mixer");
    dm_pick(2);
    turn(EN_ALGO, -1);                                  /* (a track a detent: T3) */
    frame();
    check(ui.scr == SCR_HOME && song.sel == TRK_DRUM - 1u, "drum mixer: ALGORITHM to a synth track, back to the mixer");
    /* the compact views: SYSTEM > SCREEN > DR MIX */
    reset_ui();
    op_enter(SCR_SYSTEM);
    ui.row[SCR_SYSTEM] = 0;
    turn(EN_K3, 1);
    check(dm_view == DMV_8 && dm_strips() == 8u, "SYSTEM > SCREEN > DR MIX: 4 to 8 strips");
    {
        cell_t c;
        sys_cell(0, 2, &c);
        check(!strcmp(c.val, "8") && c.gk == GK_PILL && c.gv == 1 && text_w(&FONT_S, c.label) <= CARD_W,
              "DR MIX's card: 8, a two-way pill (section 3's forms)");
    }
    check(((bp23_word() >> 21) & 3u) == DMV_8, "DR MIX kept in the settings word (bits 21..22)");
    bp23_from_word(bp23_word() & ~(3u << 21));
    check(dm_view == DMV_4, "a settings word without the bits: four strips");
    dm_view = DMV_8;
    song.sel = TRK_DRUM;
    op_enter(SCR_HOME);
    frame();
    dm_pick(6);
    ui.force = 1;
    frame();
    ppm("opt-dmix-8");
    check(dm_first() == 0u && dm_x(7) + dm_w() <= 240 && px_in((uint32_t)dm_x(5), MIX_Y + 120u, 1, 1, OP_SURF) &&
              px_in((uint32_t)dm_x(0) + 1u, MIX_Y + 120u, 1, 1, DM_BG_OFF),
          "8 strips: lanes 1-8 on one screen, the knobs' four on the lighter ground");
    dm_pick(13);
    check(ui.scr == SCR_DMIX && dm_first() == 8u && dm.blk == 3u, "8 strips: a pick in lanes 9-16 shows them");
    op_enter(SCR_SYSTEM);
    ui.row[SCR_SYSTEM] = 0;
    turn(EN_K3, 1);
    check(dm_view == DMV_8 && DMV_N == 2u, "DR MIX: 4 or 8, no 16 (left out: section 11.5)");
    bp23_from_word((bp23_word() & ~(3u << 21)) | 2u << 21);
    check(dm_view < DMV_N, "a settings word with bits 21..22 = 2 or 3: a view this build has");
    dm_view = DMV_4;
    reset_ui();
    song.sel = 0;
}

static void scope_tests(void)
{
    uint32_t r, w0, i, s;
    int32_t pk;
    reset_ui();
    for (r = 0; r < NMIX && !(MIX[r].kind == MK_ENTER && MIX[r].id == SCR_SCOPE); r++)
        ;
    check(r < NMIX, "the mixer's walk has a SCOPE row");
    ui.row[SCR_HOME] = (uint8_t)r;
    tap(B_SAVE);
    check(ui.scr == SCR_SCOPE && scope_rows() == 5u && scope_src == 0u, "SCOPE: YES on its row; MASTER T1 T2 T3 DR");
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

static void master_col_tests(void)
{
    reset_ui();
    check(MX_X(3) + MX_W < MX_COL_X && MX_COL_X + MX_COL_W <= 240 && MX_W >= 55,
          "the master column beside the four strips (55 px each, 2 px less than the cards)");
#if FELUCCA_MASTER_COMP
    mc_gr_view = -6;                                    /* 6 dB of reduction: 24 px down from the top */
    ui.force = 1;
    frame();
    ppm("opt-mixer-gr");
    check(px_in(MX_COL_X, MIX_Y + MX_FADER_Y, MX_COL_W, 4, C_WARN) &&
              !px_in(MX_COL_X, MIX_Y + MX_FADER_Y + 26u, MX_COL_W, 4, C_WARN),
          "the master column: the compressor's reduction, a bar pushing down from the top (6 dB: 24 px)");
    mc_gr_view = 0;
    frame();
    check(!px_in(MX_COL_X, MIX_Y + MX_FADER_Y, MX_COL_W, METER_H, C_WARN), "no reduction: no bar");
#endif
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
