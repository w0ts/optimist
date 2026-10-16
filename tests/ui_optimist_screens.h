/* SPDX-License-Identifier: GPL-3.0-only */
/* Per-screen oddities (batch E of the 2026-10-14 coherence audit; docs/UI-OPTIMIST-DESIGN.md section 36; the style guide
 * of optimist-work/visaudit). Each check: the old text or layout gone, the new one there.
 *   #2   the header's title is the cursor row's name verbatim ("Edit 1 2/7"; the SOUND walk in ui_optimist_pages.h
 *        checks every row), GLOBAL's second REC page "Rec 2 n/N"; short of room the title takes its short form and
 *        bar.beat stays (the user, 2026-10-14; ui_optimist_names.h: never a cut)
 *   #15  drum STEP, a step held: the selected lane's row under the cards, the grid back in place once they go
 *   #17  STEP: "Pattern 1/N" and bar.beat, never "Steps 1-16 C4"; a synth's pick marked in the roll's gutter
 *   #19  the drum track's EDIT pages: Lane, Body, Tone, Sends, Kit (never "Sound" five times)
 *   #20  its FX send pages: "Kit sends" (the kit's send levels) and "Bus sends", on a picked lane too
 *   #24  (ui_optimist_mixer.h) a layer's "Home locks it" on the help line, never in the header
 *   #32  the drum ENV's length in the label row, nothing on the baseline
 *   #33  a 1x4 card with a long name and an empty card after it: two cards wide, the name whole
 *   #35  the mixer's ACCENT lane row: "Accent", no set word, no n/N
 *   #36  the synth SOUND row's list cells: the name over two cells, INIT, SAVE (never "ANLG")
 *   #43  PROJECT: MISSING (was MISS), FREE (was MORE)
 *   #46  the ARP picture: no text line (MODE and RATE are the cards')
 *   #49  NAME: its hints on the help line's place, none at the field's top
 *   GLOBAL's REC LENGTH: "1" with the unit "bar" (was "1 BAR")
 * Included after ui_optimist_lanevals.h. */
static void se_title(char *t)                           /* the header's title now, as drawn (with its n/N) */
{
    head_t h;
    head_make(&h);
    str_cpy(t, h.t, 32);
}
static void se_drum_family(uint32_t b)                  /* the drum track (no lane picked), a family by its button */
{
    reset_ui();
    song.sel = TRK_DRUM;
    ui.lane = 0;
    go_home();
    frame();
    tap(b);
    frames(2);
}
/* the titles of the SOUND screen's rows (as the list and the header show them) contain want, and never bad */
static int se_rows_have(const char *want, const char *bad)
{
    uint32_t r, has = 0, n = SCR->rows();
    char raw[12], t[16];
    for (r = 0; r < n; r++) {
        snd_title_row(r, raw);
        nm_title(raw, t, sizeof t);
        if (getenv("SE_ROWS"))
            printf("  row %u '%s'\n", r, t);
        has |= !strcmp(t, want);
        if (bad && !strcmp(t, bad))
            return 0;
    }
    return (int)has;
}
static void se_names_tests(void)
{
#if DL_ANY
    se_drum_family(B_EDIT);
    check(ui.scr == SCR_SOUND && se_rows_have("Lane", "Sound") && se_rows_have("Body", 0) && se_rows_have("Tone", 0) &&
              se_rows_have("Kit", 0) && se_rows_have("Sends", 0),
          "#19: the drum EDIT pages Lane, Body, Tone, Sends, Kit (was \"Sound\" x5)");
    {
        char t[32];
        ui.row[SCR_SOUND] = 0;
        se_title(t);
        check(!strncmp(t, "Lane ", 5), "#19: ... the header says it too (\"Lane 1/5\")");
    }
    se_drum_family(B_FX);
    check(ui.scr == SCR_SOUND && se_rows_have("Kit sends", "Sends") && se_rows_have("Bus sends", "Bus send"),
          "#20: the drum FX send pages \"Kit sends\" / \"Bus sends\" (was \"Sends\" / \"Bus send\")");
#if DL_UI
    ui.lane = 1;
    lane_select(4);
    tap(B_FX);
    frames(2);
    check(lane_picked() && !se_rows_have("Kit sends", 0) && !se_rows_have("Bus sends", 0) && se_rows_have("Sends", "DR sends"),
          "#20: a picked lane: its own \"Sends\" only, no \"Kit sends\" / \"Bus sends\" (uniform with the tracks, 2026-10-14)");
#endif
#endif
    reset_ui();
    song.sel = 0;
    op_enter(SCR_SOUND);                                /* (no synth SOUND row: PRESETS is the one place for a sound, 2026-10-14) */
    snd_family(SND_ALL);
    check(snd_page(0) != 0, "a synth track's SOUND screen opens on a page: no PRESET ENGINE INIT SAVE AS row");
}
static void se_header_tests(void)
{
    char t[32];
    head_t h;
    reset_ui();
    song.sel = 0;
    tap(B_SEQ);
    frames(2);
    head_make(&h);
    se_title(t);
    check(!strncmp(t, "Pattern", 7) && (SCR->rows() < 2u || strchr(t, '/')) && h.r[0] && strncmp(t, "Steps", 5),
          "#17: STEP: \"Pattern 1/N\" and bar.beat (was \"Steps 1-16 C4\", no bar.beat)");
    tap(B_HOME);
    {   /* the user's ruling: bar.beat always stays; a long title takes its short form, never a cut */
        char a[32] = "Voice 2", b[32] = "Edit 1", c[32] = "Pattern", d[32] = "Kit sends";
        head_short(a, sizeof a, 5);
        head_short(b, sizeof b, 5);
        head_short(c, sizeof c, 5);
        head_short(d, sizeof d, 8);
        check(!strcmp(a, "Vox2") && !strcmp(b, "Edit1") && !strcmp(c, "Pat") && !strcmp(d, "Kit snd"),
              "#2: a title short of room: its short form (Voice 2 -> Vox2, Edit 1 -> Edit1, Pattern -> Pat), never cut");
    }
    reset_ui();
    {   /* the mixer's ACCENT lane */
#if ACC_UI
        head_title(SCR_HOME, MXR_ACC, t, sizeof t);
        {
            char p[12];
            head_pos(SCR_HOME, MXR_ACC, p);
            check(!strcmp(t, "Mix") && !p[0], "#35: the mixer's ACCENT row: the title \"Mix\" (the badge names the lane), no n/N (was \"accent mix\")");
        }
#endif
    }
#if FELUCCA_REC_MODES
    {   /* GLOBAL's QUANT page: "Rec 2 n/N" */
        uint32_t i, n = gl_n(0);
        char sn[24];
        for (i = 0; i < n; i++) {
            gl_sect(0, i, sn);
            if (gl_ent(0, i) >> 2 == TPR_REC2)
                break;
        }
        gl_ptitle(0, i < n ? i : 0, t, sizeof t);
        check(i < n && !strcmp(t, "Rec 2"), "#2: GLOBAL's REC 2 page: \"Rec 2 3/5\" (was \"Rec 3/5\")");
    }
#endif
    {   /* GLOBAL's LENGTH: the number and its unit */
        cell_t c;
        tp_cell(TPR_REC, 1, &c);
        check(!strcmp(c.val, "1") && !strcmp(c.unit, "bar"), "GLOBAL's REC LENGTH: \"1\" and the unit bar (was \"1 BAR\")");
    }
}
static void se_step_tests(void)
{
#if DL_UI
    {   /* #15: the drum grid under a held step's cards */
        int32_t top = OP_PY - SP_Y, y, lh;
        reset_ui();
        song.sel = TRK_DRUM;
        ui.lane = 0;
        tap(B_SEQ);
        lane_select(0);
        frames(2);
        kdown(WK(3));
        frames(HOLD_FRAMES + 2u);
        ui.force = 1;
        frame();
        y = SG_LY(lane_selected());
        lh = SG_LY(lane_selected() + 1u) - y;
        check(ui.cards_t > 0u && sg_dy != 0 && y >= top && px_in(2, (uint32_t)(SP_Y + y), 3, (uint32_t)(lh - 1), C_WHITE),
              "#15: drum STEP, a step held on the first lane: its row under the cards (was hidden behind them)");
        kup(WK(3));
        frames(OP_CARDS_FRAMES + 10u);
        check(ui.cards_t == 0u && sg_dy == 0, "#15: ... the cards gone: the grid back in place");
        lane_select(6);                                 /* (the user, 2026-10-16: a hit on the first lane, another lane selected) */
        memset(&trk[TRK_DRUM].dstep[3], 0, sizeof trk[TRK_DRUM].dstep[3]);
        dstep_set(&trk[TRK_DRUM].dstep[3], 0, LV_NORM, 0);
        frames(2);
        kdown(WK(3));
        frames(HOLD_FRAMES + 2u);
        ui.force = 1;
        frame();
        y = SG_LY(0);
        lh = SG_LY(1) - y;
        check(ui.cards_t > 0u && sg_dy != 0 && y >= top && y + lh <= SG_INFO_Y,
              "#15: drum STEP, a held step's hit on the first lane, lane 7 selected: the hit's row under the cards (was hidden)");
        kup(WK(3));
        frames(OP_CARDS_FRAMES + 10u);
        dstep_clr(&trk[TRK_DRUM].dstep[3], 0);
        tap(B_HOME);
    }
#endif
}
static void se_card_tests(void)
{
    cell_t c[4];
    uint32_t k, r = MXR_T1, s, ok = 0, seen = 0;
    reset_ui();
    op_enter(SCR_HOME);
    for (s = 0; s < mx_sets(r); s++) {                  /* T1's SOUND set: FX ON, SOUND, -, - */
        for (k = 0; k < 4u; k++)
            mix_cell(r + MXR_N * s, k, &c[k]);
        for (k = 0; k < 4u; k++)
            if (c[k].label && !strcmp(c[k].label, "SOUND")) {
                seen = 1;
                str_cpy(c[k].val, "ACID SQUELCH", sizeof c[k].val);   /* (a 12-letter name) */
                ok = (uint32_t)card_wide(c, k);
            }
    }
    check(!seen && ok == 0u, "#33: the mixer has no SOUND card any more (it was a PRESET name card; PRESETS is a button)");
}
static void se_picture_tests(void)
{
    reset_ui();
    song.sel = 0;
    op_enter(SCR_SOUND);
    tap(B_ARP);
    ui.force = 1;
    frames(2);
    ppm("opt-se-arp");
    check(ui.scr == SCR_SOUND && !px_in(150, (uint32_t)(OP_PY + GR_TOP), 86, 12, C_GRAY),
          "#46: the ARP picture: no text line (was \"OFF 1/16  C4 E4 G4\")");
#if DSND_ENV
    reset_ui();
    song.sel = TRK_DRUM;
    ui.lane = 1;
    lane_select(0);
    op_enter(SCR_SOUND);
    snd_family(FAM_ENV);
    ui.row[SCR_SOUND] = 0;
    ui.force = 1;
    frames(2);
    ppm("opt-se-denv");
    check(ui.scr == SCR_SOUND && px_in(100, (uint32_t)(OP_PY + GR_TOP), 100, 14, C_GRAY) &&
              !px_in(150, (uint32_t)(OP_PY + GR_BOT - 14), 86, 13, C_DIM),
          "#32: the drum ENV's length in the label row, nothing on the baseline (was \"505ms\" over it)");
#endif
    reset_ui();
}
static void se_project_tests(void)
{
    cell_t c;
    char b[16];
#if SEC_LOGGED
    prj_mem(&c, 1);
    check(!strcmp(c.label, "FREE"), "#43: PROJECT > MEMORY: FREE (was MORE)");
#endif
    cell_clear(&c);
    c.label = "MISS";
    check(!strcmp(card_label(&c, b, sizeof b), "MISSING"), "#43: PROJECT > TOOLS: MISSING (was MISS)");
}
static void se_name_tests(void)
{
    reset_ui();
    song.sel = 0;
    name_open(NK_USER, 2);
    ui.force = 1;
    frames(2);
    ppm("opt-se-name");
    check(name_on() && px_in(4, 240u - 16u, 200, 14, C_AMB) && !px_in(9, OY_CARD + 1u, 150, 15, C_GRAY),
          "#49: NAME: its hints on the help line's place, none at the field's top");
    name_close();                                       /* (nothing written) */
    frames(2);
    check(!name_on(), "#49: ... NAME closed");
    reset_ui();
}
static void screens_tests(void)
{
    se_names_tests();
    se_header_tests();
    se_step_tests();
    se_card_tests();
    se_picture_tests();
    se_project_tests();
    se_name_tests();
    reset_ui();
    ui.lane = 0;
    song.sel = 0;
    go_home();
    frames(2);
}
