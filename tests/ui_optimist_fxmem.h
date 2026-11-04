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
static const uint8_t FZ_FILL_K[3] = {2, 0, 3};          /* (a fragmented arena: the room may take more than one block) */
static void fz_fill(uint32_t leave)                     /* the arena's room down to leave bytes */
{
    uint32_t r, i, x;
    fm1_irq_off();
    for (i = 0; i < 3u; i++)
        arn_give(FZ_FILL_K[i]);
    for (i = 0; i < 3u; i++) {
        uint32_t c = FZ_FILL_K[i];
        r = arn_room();
        for (x = r > leave ? r - leave : 0u; x >= 256u; x -= 256u)
            if (arn_reserve(c, x) == 0) {
                (void)arn_take(c);
                break;
            }
    }
    fm1_irq_on();
}
static void fz_unfill(void)
{
    uint32_t i;
    fm1_irq_off();
    for (i = 0; i < 3u; i++)
        arn_give(FZ_FILL_K[i]);
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

/* the list's NO (the user, 2026-11-01: "when I go to a slot that needs more RAM and it asks me a question, using the same encoder
 * again means 'no, go to the next item'"): the encoder that moves the list, turned under its make-room question, answers NO and
 * goes on to the next item in its direction (that one may ask again); YES still answers; every other encoder as before */
static uint32_t fz_qeng(void)                           /* the engine the make-room question up asks for, 0xFF none */
{
    return op_armed() && ui.arm_scr == ARM_OP && ui.arm_row == OA_FXZ && part_mem_refused.on ? part_mem_refused.eng : 0xFFu;
}
static void fz_shift_turn(int32_t s)                    /* SHIFT (OCT-) + PRESETS: the next / previous engine */
{
    press(B_OCTDN);
    turn(EN_PRESET, s);
    release(B_OCTDN);
}
static void fz_ins_goto(uint32_t idx)                   /* INSERTS S1 TYPE (KNOB 1) turned from where it is to list entry idx */
{
    uint32_t i;
    for (i = 0; i < INS_NLIST && ins_list[i] != ins_slot[1][0]; i++)
        ;
    turn(EN_K1, (int32_t)idx - (int32_t)i);
    frame();
}
static void fz_squeeze_on(uint32_t eng, uint32_t leave)  /* T2 on engine eng, the arena's room then about leave bytes less what eng holds */
{
    set_engine_of(&trk[1], eng_silent_slot());
    fz_unfill();
    fz_frames(40);
    fz_fill(leave);
    set_engine_of(&trk[1], eng);
    fz_frames(20);
}
static void fz_list_no_tests(void)
{
    uint32_t k, t = FXT_XDLY, r, prev, prev0, n, e, g0 = 0, room, up;
    reset_ui();
    (void)preset_all_pos(&n);                            /* (the list built) */
    for (k = 0; k < NPART; k++)
        set_engine_of(&trk[k], eng_silent_slot());
    ins_defaults();
    fxs_set(FXS_DEF);
    song.g[G_FXZ_DLY] = song.g[G_FXZ_REV] = song.g[G_FXZ_XDL] = 0;
    trk[1].p[P_CHOR] = trk[1].p[P_DLY] = trk[1].p[P_REV] = 90;
    fz_frames(20);
    /* A. the engine list (SHIFT + PRESETS), A-Z since 2026-11-01: GRAIN and FM6 side by side (FM6 before GRAIN), neither fits;
     * the part starts on the engine after GRAIN (up), which does; the walk goes down. Found by their places, not their numbers */
    for (e = 0; e < NENGINES && ENG_ALPHA_SLOT[e] != 8u; e++)
        ;
    check(e > 0u && e + 1u < NENGINES && ENG_ALPHA_SLOT[e - 1u] == 9u, "list NO engines: A-Z: FM6 just before GRAIN, an engine after it");
    up = ENG_ALPHA_SLOT[e + 1u];
    song.sel = 1;
    op_enter(SCR_SOUND);
    fz_squeeze_on(up, 2048u);
    room = arn_room();
    fz_shift_turn(-1);
    fz_frames(6);
    check(fz_qeng() == 8u && !strncmp(ui.arm_q, "SHRINK ", 7) && trk[1].eng_req == up,
          "list NO engines: down onto GRAIN: refused, SHRINK asked, the part still on the engine after it");
    fz_shift_turn(-1);
    fz_frames(6);
    check(fz_qeng() == 9u && trk[1].eng_req == up && !song.g[G_FXZ_REV] && !song.g[G_FXZ_DLY] && arn_room() == room,
          "list NO engines: the same encoder again: NO, on to FM6, which asks (SHRINK again), nothing taken");
    fz_shift_turn(1);
    fz_frames(6);
    check(fz_qeng() == 8u && trk[1].eng_req == up, "list NO engines: turned back: GRAIN asks again");
    fz_shift_turn(1);
    fz_frames(6);
    check(!op_armed() && !part_mem_refused.on && trk[1].eng_req == up && trk[1].engine == up && !song.g[G_FXZ_REV],
          "list NO engines: back on the engine after GRAIN: no question, the part never left it, no size taken");
    fz_shift_turn(-1);
    fz_frames(6);
    fz_shift_turn(-1);
    fz_frames(6);
    check(fz_qeng() == 9u, "list NO engines: GRAIN, FM6 again");
    turn(EN_K4, 1);
    fz_frames(2);
    check(fz_qeng() == 9u && trk[1].eng_req == up, "list NO engines: another encoder (KNOB 4): the question stays");
    tap(B_SAVE);
    fz_frames(60);
    check((song.g[G_FXZ_REV] || song.g[G_FXZ_DLY]) && trk[1].engine == 9u && !part_mem_refused.on, "list NO engines: YES: the sizes taken, FM6 plays");
    song.g[G_FXZ_REV] = song.g[G_FXZ_DLY] = 0;
    /* the end of the list: WHEEL is the last engine (A-Z); from the one before it with too little room: WHEEL refused, then NO
     * at the end: nothing further, the part as it was */
    check(ENG_ALPHA_SLOT[NENGINES - 1u] == 7u, "list NO engines: A-Z: WHEEL the last engine");
    up = ENG_ALPHA_SLOT[NENGINES - 2u];
    fz_squeeze_on(up, 256u);
    fz_shift_turn(1);
    fz_frames(6);
    check(fz_qeng() == 7u, "list NO engines: the last engine asks");
    fz_shift_turn(1);
    fz_frames(6);
    check(!op_armed() && !part_mem_refused.on && trk[1].eng_req == up && trk[1].engine == up && !song.g[G_FXZ_REV],
          "list NO engines: past the last engine: NO, no question left, the part as it was");
    /* B. PRESETS' own list (the sounds, by engine A-Z): GRAIN's last sound just before the next engine's first; from that one down,
     * GRAIN's last and the one before it ask each */
    pl_whole = 1;
    for (n = 0, g0 = NLIST; n < NLIST; n++)
        if (preset_at(n, &k) == 8u)
            g0 = n;                                      /* (GRAIN's last sound) */
    check(g0 + 1u < NLIST && preset_at(g0 + 1u, &k) != 8u && preset_at(g0 - 1u, &k) == 8u, "list NO sounds: GRAIN's last two sounds, then another engine's");
    up = preset_at(g0 + 1u, &k);
    fz_squeeze_on(up, 2048u);
    op_preset_go(g0 + 1u);                               /* (the sound after GRAIN's last) */
    fz_frames(4);
    turn(EN_PRESET, -1);
    fz_frames(6);
    check(fz_qeng() == 8u && trk[1].eng_req == up, "list NO sounds: the next engine's first > GRAIN's last: refused, asks");
    turn(EN_PRESET, -1);
    fz_frames(6);
    check(fz_qeng() == 8u && trk[1].eng_req == up && !song.g[G_FXZ_REV],
          "list NO sounds: PRESETS again: NO, on to GRAIN's sound before it, which asks again");
    turn(EN_PRESET, 1);
    fz_frames(6);
    turn(EN_PRESET, 1);
    fz_frames(6);
    check(!op_armed() && !part_mem_refused.on && trk[1].eng_req == up && preset_pos(&n) == g0 + 1u,
          "list NO sounds: back to the sound after GRAIN's: no question, that sound");
    fz_unfill();
    set_engine_of(&trk[1], eng_silent_slot());
    fz_frames(40);
    /* C. an FX type's slot list: INSERTS S1 TYPE (the first and the last entry with no room: ins_list's 3rd and 13th) */
    fz_fill(30u * 1024u);
    r = ins_row("INSERTS");
    song.sel = 1;
    ui.row[SCR_SOUND] = (uint8_t)r;
    ins_load(1, 0, FXT_NONE);
    fz_frames(4);
    for (k = 0, e = INS_NLIST, n = INS_NLIST; k < INS_NLIST; k++)
        if (fxz_ins_mem(1, 0, ins_list[k])) {
            e = e == INS_NLIST ? k : e;
            n = k;
        }
    check(e < n && e > 1u && n + 1u == INS_NLIST, "list NO inserts: two entries with no room, one near the top, one the last");
    prev = ins_slot[1][0];
    room = arn_room();
    fz_ins_goto(e);
    fz_frames(1);
    check(ins_slot[1][0] == ins_list[e] && op_armed() && ui.arm_row == OA_FXZI, "list NO inserts: the first picked: the question");
    turn(EN_K1, 1);
    fz_frames(10);
    check(ins_slot[1][0] == ins_list[e + 1u] && !op_armed() && !fxq.on && !song.g[G_FXZ_DLY] && !song.g[G_FXZ_REV] &&
          strcmp(ui.msg, "NOT CHANGED"),
          "list NO inserts: the same encoder: NO, on to the next entry, which fits: placed, no question, nothing taken");
    turn(EN_K1, -1);                                     /* (back up: the first stop again) */
    fz_frames(1);
    check(ins_slot[1][0] == ins_list[e] && op_armed() && ui.arm_row == OA_FXZI && fxq.prev == ins_list[e + 1u],
          "list NO inserts: turned back to the first: it asks again");
    turn(EN_K1, -1);
    fz_frames(1);
    check(ins_slot[1][0] == ins_list[e - 1u] && !op_armed() && !fxq.on && strcmp(ui.msg, "NOT CHANGED"),
          "list NO inserts: on up: NO, the entry above, which fits");
    ins_load(1, 0, FXT_NONE);
    fz_frames(4);
    prev = ins_slot[1][0];
    fz_ins_goto(e);
    fz_frames(1);
    fz_ins_goto(n);                                      /* (the list has no two stops side by side: one turn of several detents to the last) */
    fz_frames(1);
    check(ins_slot[1][0] == ins_list[n] && op_armed() && ui.arm_row == OA_FXZI && fxq.on && fxq.prev == prev,
          "list NO inserts: the next entry has no room either: asks again, the slot to go back to still the one before the first");
    turn(EN_K1, 0);
    check(op_armed() && ui.arm_row == OA_FXZI, "list NO inserts: a turn of nothing: the question stays");
    tap(B_HOME);
    fz_frames(4);
    check(ins_slot[1][0] == prev && !strcmp(ui.msg, "NOT CHANGED") && !song.g[G_FXZ_DLY] && !song.g[G_FXZ_REV],
          "list NO inserts: HOME: the slot back to what it held before the first, nothing lost");
    prev0 = ins_slot[1][0];
    fz_ins_goto(n);
    fz_frames(1);
    check(ins_slot[1][0] == ins_list[n] && op_armed() && ui.arm_row == OA_FXZI, "list NO inserts: the last entry asks");
    turn(EN_K1, 1);
    fz_frames(1);
    fz_frames(4);
    check(ins_slot[1][0] == prev0 && !op_armed() && !fxq.on && !song.g[G_FXZ_DLY] && !song.g[G_FXZ_REV],
          "list NO inserts: the end of the list: NO, the question gone quietly, the original type kept");
    ins_load(1, 0, FXT_NONE);
    fz_frames(4);
    fz_ins_goto(e);
    fz_frames(1);
    turn(EN_K2, 1);
    fz_frames(1);
    check(ins_slot[1][0] == ins_list[e] && op_armed() && ui.arm_row == OA_FXZI && fxq.on,
          "list NO inserts: another encoder (KNOB 2): the question stays");
    tap(B_SAVE);
    fz_frames(60);
    check(ins_slot[1][0] == ins_list[e] && fxi_state(1, 0) == FXI_OK && (song.g[G_FXZ_DLY] || song.g[G_FXZ_REV]),
          "list NO inserts: YES still answers yes: the sizes taken, the type placed");
    for (k = 0; k < NTRK; k++)
        ins_load(k, 0, FXT_NONE);
    song.g[G_FXZ_DLY] = song.g[G_FXZ_REV] = song.g[G_FXZ_XDL] = 0;
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
    fz_list_no_tests();
}
#else
static void fxmem_ui_tests(void) {}
#endif
