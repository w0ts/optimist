/* SPDX-License-Identifier: GPL-3.0-only */
/* The load's memory list (ui/optimist/op_memlist.c; docs/SD-MODE-DESIGN.md 6b; the user, 2026-10-25: "the list page as
 * designed"), built with an arena placed by need that holds one FM6 part and the effects' lines (run_tests.sh
 * ui_optimist_memlist_test: FELUCCA_ARENA_BYTES, FELUCCA_FX_ARENA, FM6). A load with FM6 on the three parts: parts 2 and
 * 3 refused, both placeholders, the page lists them and part 1, then REV DLY CHO on the white keys 1-4; a key toggles a
 * drop; the gauge and FITS are the arena's trial (model.c mem_load_fits) for what is picked; SAVE without FITS says NO
 * ROOM and stays; SAVE with FITS drops (DLY bypassed) and places both parts; HOME leaves them placeholders (NONE); an
 * edit's change still has its one confirm. */
#if !ARN_FIXED && FXM_NEED
static void ml_frames(uint32_t n)                      /* frames with the main loop's arena passes (main.c) */
{
    while (n--) {
        fxm_poll();
        fxi_poll();
        frame();
    }
}
static void ml_load(uint32_t want)                     /* a load's engines as storage/project.c proj_apply sets them */
{
    uint32_t k;
    fm1_irq_off();
    part_mem_load_begin();
    for (k = 0; k < NPART; k++) {
        uint32_t e = (want >> k & 1u) ? ENG_IX_FM6 : eng_silent_slot(), no = part_mem_load(k, e) != 0;
        trk[k].silent = (uint8_t)no;
        trk[k].eng_req = (uint8_t)(no ? eng_silent_slot() : e);
    }
    fm1_irq_on();
}
static void memlist_tests(void)
{
    head_t h;
    uint32_t k;
    reset_ui();
    for (k = 0; k < NPART; k++)
        set_engine_of(&trk[k], k ? eng_silent_slot() : ENG_IX_FM6);
    ml_frames(20);
    check(trk[0].engine == ENG_IX_FM6 && cho_buf && dly_buf && rev_line && !ml_on() && !part_mem_refused.lmask,
          "memory list: FM6 on part 1, CHO DLY REV placed: the arena all but full");
    ml_load(7u);
    check(part_mem_refused.lmask == 6u && trk[1].silent && trk[2].silent && part_mem_refused.leng[1] == ENG_IX_FM6 &&
          part_mem_refused.leng[2] == ENG_IX_FM6, "memory list: a load with FM6 on three parts: parts 2 and 3 refused, both kept");
    ml_frames(40);
    head_make(&h);
    check(ml_on() && ml.n == 6u && ml.it[0] == 1u && ml.it[1] == 2u && ml.it[2] == 0u && ml.it[3] == NPART + FXM_REV &&
          ml.it[4] == NPART + FXM_DLY && ml.it[5] == NPART + FXM_CHO && !ml.fits && !strcmp(h.eng, "MEMORY") && !strcmp(h.t, "Load"),
          "memory list: the page (MEMORY / Load): parts 2 and 3 (refused), part 1, then REV DLY CHO; short");
    check(key_led(white_key(0)) == 1u && key_led(white_key(5)) == 1u && key_led(white_key(6)) == 0u &&
          px_in(5, OY_CARD + 25u, 40, 12, C_WARN) && !px_in(5, OY_CARD + 25u, 230, 12, C_OK) &&
          px_in(24, OY_PANEL + 2u, 80, 14, ENG_COL[ENG_IX_FM6]) && px_in(216 - 70, 240u - 16u, 70, 14, C_DIM),
          "memory list: keys 1-6 dim; the gauge amber; the refused parts in FM6's colour; SAVE dim");
    ppm("opt-memlist-short");
    key(white_key(2));
    check(ml.sel == 1u && ml.fits == (uint8_t)mem_load_fits(1u) && !ml.fits && key_led(white_key(2)) == 2u &&
          px_in(4, OY_PANEL + 2u + 2u * 19u + 4u, 16, 16, C_ERR),
          "memory list: key 3 drops part 1 (its key full, its row red): one part fits, not both (the arena's trial)");
    tap(B_SAVE);
    check(ml_on() && !strcmp(ui.msg, "NO ROOM") && !trk[0].silent && part_mem_refused.lmask == 6u,
          "memory list: SAVE while short: NO ROOM, nothing dropped, the page stays");
    key(white_key(2));
    key(white_key(5));
    check(ml.sel == 1u << (NPART + FXM_CHO) && !ml.fits && ml.fits == (uint8_t)mem_load_fits(ml.sel),
          "memory list: part 1 kept again; CHO dropped: still short");
    key(white_key(1));
    ml_frames(1);
    check(ml.sel == (1u << (NPART + FXM_CHO) | 1u << 2) && ml.fits && mem_load_fits(ml.sel) &&
          px_in(160, OY_PANEL + 2u + 19u, 76, 14, C_GRAY) && px_in(5, OY_CARD + 25u, 230, 12, C_OK),
          "memory list: part 3 left silent (key 2: NONE) and CHO dropped: part 2 fits (FITS, the need its alone)");
    ppm("opt-memlist-silent");
    key(white_key(1));
    key(white_key(5));
    key(white_key(4));
    ml_frames(1);
    check(ml.sel == 1u << (NPART + FXM_DLY) && ml.fits && mem_load_fits(ml.sel) && px_in(5, OY_CARD + 25u, 230, 12, C_OK) &&
          px_in(216 - 70, 240u - 16u, 70, 14, C_WHITE), "memory list: DLY dropped: FITS (the gauge green, SAVE lit)");
    ppm("opt-memlist-fits");
    tap(B_SAVE);
    check(!ml_on(), "memory list: SAVE: the page closed");
    ml_frames(60);
    check(!part_mem_refused.lmask && trk[1].engine == ENG_IX_FM6 && trk[2].engine == ENG_IX_FM6 && !trk[1].silent &&
          !trk[2].silent && trk[0].engine == ENG_IX_FM6 && !dly_buf && fxm_refused(FXT_DLY) && !ml_on(),
          "memory list: ... DLY bypassed, parts 2 and 3 on FM6 (the commit placed what FITS said)");
    set_engine_of(&trk[1], eng_silent_slot());
    set_engine_of(&trk[2], eng_silent_slot());
    ml_frames(40);
    check(dly_buf && !fxm_refused(FXT_DLY), "memory list: parts 2 and 3 off FM6: DLY back by itself");
    ml_load(7u);
    ml_frames(40);
    check(ml_on() && part_mem_refused.lmask == 6u, "memory list: the same load again: the page");
    tap(B_HOME);
    ml_frames(20);
    check(!ml_on() && !part_mem_refused.lmask && trk[1].silent && trk[2].silent && !strcmp(trk_eng_name(&trk[1]), "NONE") &&
          trk[0].engine == ENG_IX_FM6 && dly_buf, "memory list: HOME: parts 2 and 3 stay placeholders (NONE), the rest as loaded");
    ml_load(7u);                                        /* (the same load: part 3 left silent, CHO dropped, SAVE) */
    ml_frames(40);
    key(white_key(1));
    key(white_key(5));
    tap(B_SAVE);
    ml_frames(60);
    check(!ml_on() && !part_mem_refused.lmask && trk[1].engine == ENG_IX_FM6 && !trk[1].silent && trk[2].silent &&
          !strcmp(trk_eng_name(&trk[2]), "NONE") && trk[0].engine == ENG_IX_FM6 && !cho_buf && fxm_refused(FXT_CHO) && dly_buf,
          "memory list: part 3 left silent and CHO dropped, SAVE: part 2 on FM6, part 3 NONE, CHO bypassed");
    set_engine_of(&trk[1], eng_silent_slot());
    ml_frames(40);
    check(cho_buf && !fxm_refused(FXT_CHO), "memory list: part 2 off FM6: CHO back by itself");
    set_engine_of(&trk[1], ENG_IX_FM6);                 /* an edit's change: the one confirm, as before */
    ml_frames(4);
    check(!ml_on() && part_mem_refused.on && op_armed() && ui.arm_row == OA_DROP, "memory list: an edit's change keeps its one confirm (DROP)");
    reset_ui();
    part_mem_refused.on = 0;
    ml_load(7u);
    ml_frames(40);
    fuzz(3000, 977);                                    /* (random use with the page up and after: every draw on the screen) */
    check(!band_hits, "memory list: 3000 random frames, no blit in the help band but the help line's");
    mem_load_cancel();
    part_mem_refused.on = 0;
    reset_ui();
}
#else
static void memlist_tests(void) {}
#endif
