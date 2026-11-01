/* SPDX-License-Identifier: GPL-3.0-only */
/* PROJECT > FX MEM (2026-11-01; ui/optimist/op_fxmem.c, fx/fx_memsz.h), built with the user's lines (DLY_LEN 32768, FDN8
 * in the 32 KB ring, XDLY) and an arena (run_tests.sh ui_optimist_fxmem_test):
 *   the row: DLY MEM, REV MEM, XDLY MEM, FREE; the panel: the arena's bar, every big line's KB
 *   K1 turned: DLY 1/2, the delay's block halved, FREE the 32 KB more; K2: REV SMALL
 *   a part's engine change refused: the question offers the FX sizes first ("SHRINK?" with what frees enough); YES takes
 *   them and the change goes through; HOME: the next way (the existing chain), nothing changed by it
 *   the FX type list: an entry with no room drawn with the memory mark, the message line says what it needs */
#if FXZ_ANY && FXM_NEED
static uint32_t ins_row(const char *title);              /* (ui_optimist_inslots.h) */
static void fz_frames(uint32_t n)
{
    while (n--) {
        fxm_poll();
        fxi_poll();
        frame();
    }
}
static uint32_t fz_row(void)                            /* PROJECT's FX MEM row */
{
    uint32_t r;
    for (r = 0; r < prj_rows(); r++)
        if (prj_kind(r) == PR_FXM)
            return r;
    return 0;
}
static uint32_t fz_fill_k = 2u;                        /* (a silent part's client holds the filler: the arena all but full) */
static void fz_fill(uint32_t leave)                     /* the arena's room down to leave bytes */
{
    uint32_t r = arn_room();
    fm1_irq_off();
    arn_give(fz_fill_k);
    if (r > leave)
        (void)arn_reserve(fz_fill_k, r - leave), (void)arn_take(fz_fill_k);
    fm1_irq_on();
}
static void fz_unfill(void)
{
    fm1_irq_off();
    arn_give(fz_fill_k);
    fm1_irq_on();
}
static void fz_question_tests(void)
{
    uint32_t room, k, t = FXT_XDLY, r, prev;
    reset_ui();
    for (k = 0; k < NPART; k++)
        set_engine_of(&trk[k], eng_silent_slot());
    ins_defaults();
    fxs_set(FXS_DEF);
    song.g[G_FXZ_DLY] = song.g[G_FXZ_REV] = song.g[G_FXZ_XDL] = 0;
    trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = 90;
    fz_frames(20);
    /* 1. a part's engine change that does not fit: SHRINK first */
    fz_fill(4096u);
    room = arn_room();
    set_engine_of(&trk[0], ENG_IX_FM6);
    fz_frames(6);
    check(part_mem_refused.on && op_armed() && ui.arm_row == OA_FXZ && !strcmp(ui.arm_q, "SHRINK REV SMALL?") &&
          !strncmp(ui.msg, "FM6 NEEDS ", 10), "make room: FM6 refused: the question SHRINK REV SMALL?, the message FM6 NEEDS .. KB");
    ppm("opt-fxmem-ask");
    tap(B_HOME);
    fz_frames(4);
    check(!song.g[G_FXZ_REV] && arn_room() == room && (!op_armed() || ui.arm_row != OA_FXZ),
          "make room: HOME: nothing changed by it, the next way (or none) offered");
    op_disarm();
    part_mem_refused.on = 0;
    fz_frames(4);
    set_engine_of(&trk[0], ENG_IX_FM6);
    fz_frames(6);
    check(op_armed() && ui.arm_row == OA_FXZ, "make room: the change again: asked again");
    tap(B_SAVE);
    fz_frames(40);
    check(song.g[G_FXZ_REV] == 1 && fxsz_rev == 1u && trk[0].engine == ENG_IX_FM6 && !part_mem_refused.on && rev_line,
          "make room: YES: REV SMALL taken, the reverb placed again, the part on FM6");
    set_engine_of(&trk[0], eng_silent_slot());
    song.g[G_FXZ_REV] = 0;
    fz_unfill();
    fz_frames(40);
    /* 2. an FX type with no room in the INSERTS list */
    fz_fill(30u * 1024u);
    room = arn_room();
    r = ins_row("INSERTS");
    song.sel = 1;
    ui.row[SCR_SOUND] = (uint8_t)r;
    fz_frames(2);
    check(fxz_ins_mem(1, 0, t) && !fxi_can(1, t), "FX list: XDLY with no room (64 KB, 30 free): out for memory alone");
    for (k = 0; k < INS_NLIST && ins_list[k] != t; k++)
        ;
    prev = ins_slot[1][0];
    turn(EN_K1, (int32_t)k);                           /* (the list stops on it now) */
    fz_frames(1);
    check(ins_slot[1][0] == t && op_armed() && ui.arm_row == OA_FXZI && !strncmp(ui.arm_q, "SHRINK ", 7) &&
          !strncmp(ui.msg, "XDLY NEEDS 64 KB, ", 18), "FX list: XDLY picked: the message XDLY NEEDS 64 KB, .. KB FREE and SHRINK ..?");
    ppm("opt-fxmem-xdly-ask");
    tap(B_HOME);
    fz_frames(4);
    check(ins_slot[1][0] == prev && !strcmp(ui.msg, "NOT CHANGED") && !song.g[G_FXZ_DLY] && !song.g[G_FXZ_REV],
          "FX list: HOME: the slot back to what it held, no size changed");
    turn(EN_K1, (int32_t)k);
    fz_frames(1);
    tap(B_SAVE);
    fz_frames(60);
    check(ins_slot[1][0] == t && fxi_state(1, 0) == FXI_OK && (song.g[G_FXZ_DLY] || song.g[G_FXZ_REV]),
          "FX list: YES: the sizes taken, XDLY placed on the slot");
    for (k = 0; k < NTRK; k++)
        ins_load(k, 0, FXT_NONE);
    song.g[G_FXZ_DLY] = song.g[G_FXZ_REV] = song.g[G_FXZ_XDL] = 0;
    fz_frames(60);
    fz_fill(30u * 1024u);
    song.sel = 1;
    ui.row[SCR_SOUND] = (uint8_t)r;
    for (k = 0; k < INS_NLIST && ins_list[k] != t; k++)
        ;
    ins_load(1, 0, ins_list[k > 2u ? k - 2u : 0u]);    /* (two entries above XDLY: the list shows it below the cursor) */
    fz_frames(2);
    pop_enum_open(ui.row[SCR_SOUND], 0);
    fz_frames(2);
    check(pop_kind == 3u && pen_mem((int32_t)k) && !pen_mem((int32_t)(k > 2u ? k - 2u : 0u)),
          "FX list: the popup: XDLY's entry out for memory (its mark), the others not");
    ppm("opt-fxmem-list");
    op_disarm();
    for (k = 0; k < NTRK; k++)
        ins_load(k, 0, FXT_NONE);
    fz_unfill();
    fz_frames(40);
    (void)room;
    reset_ui();
}
static void fxmem_ui_tests(void)
{
    cell_t c;
    uint32_t room0, b0, k;
    reset_ui();
    for (k = 0; k < NPART; k++)
        set_engine_of(&trk[k], eng_silent_slot());
    fxs_set(FXS_DEF);
    trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = 90;
    fz_frames(20);
    op_enter(SCR_PROJECT);
    ui.row[SCR_PROJECT] = fz_row();
    fz_frames(3);
    prj_cell(ui.row[SCR_PROJECT], 0, &c);
    check(prj_kind(ui.row[SCR_PROJECT]) == PR_FXM && !strcmp(c.label, "DLY MEM") && !strcmp(c.val, "FULL"),
          "FX MEM: PROJECT's row, its first card DLY MEM FULL");
    prj_cell(ui.row[SCR_PROJECT], 3, &c);
    room0 = arn_room();
    b0 = arn_cur[FXM_K(FXM_DLY)].len;
    check(!strcmp(c.label, "FREE") && c.kind == CK_RO && (uint32_t)atoi(c.val) == room0 >> 10 && b0 == 65536u &&
          px_in(1, OY_PANEL_2X2 + 1u, 30, 10, C_HI), "FX MEM: FREE the arena's room; the panel's bar shows the effects' lines");
    ppm("opt-fxmem");
    turn(EN_K1, 1);
    fz_frames(10);
    prj_cell(ui.row[SCR_PROJECT], 3, &c);
    check(song.g[G_FXZ_DLY] == 1 && arn_cur[FXM_K(FXM_DLY)].len == 32768u && arn_room() - room0 == 32768u &&
          (uint32_t)atoi(c.val) == arn_room() >> 10, "FX MEM: K1 one step: DLY 1/2, its block 32 KB, FREE 32 KB more");
    turn(EN_K2, 1);
    fz_frames(10);
    check(song.g[G_FXZ_REV] == 1 && arn_room() - room0 > 32768u + 15000u, "FX MEM: K2: REV SMALL, 15 KB more again");
    fz_frames(120);                                     /* (the list popup gone) */
    ppm("opt-fxmem-small");
    turn(EN_K1, -1);
    turn(EN_K2, -1);
    fz_frames(10);
    check(arn_room() == room0 && !song.g[G_FXZ_DLY] && !song.g[G_FXZ_REV], "FX MEM: back to FULL: the room as before");
    reset_ui();
    fz_question_tests();
}
#else
static void fxmem_ui_tests(void) {}
#endif
