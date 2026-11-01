/* SPDX-License-Identifier: GPL-3.0-only */
/* PROJECT > FX MEM (2026-11-01; ui/optimist/op_fxmem.c, fx/fx_memsz.h), built with the user's lines (DLY_LEN 32768, FDN8
 * in the 32 KB ring, XDLY) and an arena (run_tests.sh ui_optimist_fxmem_test):
 *   the row: DLY MEM, REV MEM, XDLY MEM, FREE; the panel: the arena's bar, every big line's KB
 *   K1 turned: DLY 1/2, the delay's block halved, FREE the 32 KB more; K2: REV SMALL
 *   a part's engine change refused: the question offers the FX sizes first ("SHRINK?" with what frees enough); YES takes
 *   them and the change goes through; HOME: the next way (the existing chain), nothing changed by it
 *   the FX type list: an entry with no room drawn with the memory mark, the message line says what it needs */
#if FXZ_ANY && FXM_NEED
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
}
#else
static void fxmem_ui_tests(void) {}
#endif
