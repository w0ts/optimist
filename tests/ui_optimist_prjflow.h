/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, PROJECT's load / save flow (the user, 2026-10-14: "select bank, the operation, then press one
 * of the 16 keys and confirm with the popup yes/no. Default action is load"; "one knob for the item, and below we
 * display the info about the item"): BANK (a store of more than 16 slots), OPERATION (LOAD, SAVE, CLEAR), ITEM (the
 * slot), NEW (a blank project, no slot touched). A key selects the item and runs the operation through the confirm; YES
 * runs it on the ITEM; a free slot to LOAD or CLEAR is a toast. Included after ui_optimist_actcards.h. */
static uint32_t pf_row(uint32_t kind)
{
    uint32_t r;
    reset_ui();
    song.sel = 0;
    op_enter(SCR_PROJECT);
    for (r = 0; r < NPRJ && prj_kind(r) != kind; r++)
        ;
    if (r >= NPRJ)
        return NPRJ;
    ui.row[SCR_PROJECT] = (uint8_t)r;
    ui.hot = 2;
    frames(2);
    return r;
}
static const char *pf_val(uint32_t r, uint32_t k)       /* a card's value */
{
    static cell_t c;
    SCREENS[SCR_PROJECT].cell(r, k, &c);
    return c.val;
}
static const char *pf_label(uint32_t r, uint32_t k)
{
    static cell_t c;
    SCREENS[SCR_PROJECT].cell(r, k, &c);
    return c.label ? c.label : "";
}
static void pf_key(uint32_t w)                          /* white key w down and up; the question stays */
{
    uint32_t k = WK(w);
    kdown(k);
    kup(k);
}
static void prjflow_tests(void)
{
    uint32_t r, r2, h0, kind, kinds[3] = {PR_PROJECT, PR_SNAP, PR_USER};
    cell_t c;
    /* every store: LOAD at first, the knob walks the operations, the BANK card exists only past 16 slots */
    for (kind = 0; kind < 3u; kind++) {
        uint32_t nops;
        r = pf_row(kinds[kind]);
        if (r >= NPRJ)
            continue;
        nops = prj_nops(kinds[kind]);
        check(!strcmp(pf_label(r, 1), "ACTION") && !strcmp(pf_val(r, 1), "LOAD"), "OPERATION: LOAD at first");
        turn(EN_K2, 1);
        check(!strcmp(pf_val(r, 1), "SAVE") && !op_armed(), "OPERATION knob: SAVE, nothing asked");
        turn(EN_K2, 1);
        check(!strcmp(pf_val(r, 1), nops == 3u ? "CLEAR" : "SAVE"), nops == 3u ? "OPERATION knob: CLEAR" : "PROJECT has no CLEAR: the knob stops at SAVE");
        turn(EN_K2, 5);
        turn(EN_K2, -1);
        turn(EN_K2, -1);
        check(!strcmp(pf_val(r, 1), "LOAD"), "OPERATION knob back: LOAD");
        check(!strcmp(pf_label(r, 2), "ITEM"), "the third card is the ITEM");
        check((prj_banks(kinds[kind]) > 1u) == !strcmp(pf_label(r, 0), "BANK"), "BANK only on a store of more than 16 slots");
        check(prj_banks(kinds[kind]) > 1u || !pf_label(r, 0)[0], "... otherwise an empty card");
        check(!pf_label(r, 3)[0] == (kinds[kind] != PR_PROJECT), "card 4: NEW on PROJECT, empty on the others");
    }
    /* USER: 32 slots, two banks; the test's store has U01 and U02 used */
    r = pf_row(PR_USER);
    ui.user_slot = 0;
    check(!strcmp(pf_label(r, 0), "BANK") && !strcmp(pf_val(r, 0), "1"), "USER: BANK 1 of 2");
    turn(EN_K3, 1);
    check(ui.user_slot == 1u && !strcmp(pf_val(r, 2), "U02"), "ITEM knob: the next slot");
    ui.user_slot = 15;
    turn(EN_K3, 1);
    check(ui.user_slot == 16u && !strcmp(pf_val(r, 0), "2") && !strcmp(pf_val(r, 2), "U17"), "ITEM knob walks across the banks, BANK follows");
    for (r2 = 0; r2 < 20u; r2++)
        turn(EN_K3, 1);
    check(ui.user_slot == 31u, "... stops at the last slot");
    ui.user_slot = 3;
    turn(EN_K1, 1);
    check(ui.user_slot == 19u && !strcmp(pf_val(r, 0), "2"), "BANK knob: the same place in bank 2");
    pf_key(0);
    check(ui.user_slot == 16u && !op_armed() && ui.toast_t, "bank 2: key 1 is U17 (selected; free, so a toast)");
    ui.user_slot = 19;
    turn(EN_K1, -1);
    check(ui.user_slot == 3u, "BANK knob back: bank 1, the same place");
    /* a key with each operation: the right question, HOME cancels, YES does it, LOAD again after */
    r = pf_row(PR_USER);
    ui.user_slot = 4;
    work_mark();                                        /* (the work as stored: a LOAD is neutral) */
    pf_key(1);
    check(ui.user_slot == 1u && op_armed() && !strcmp(ui.arm_q, "LOAD USER U02?") && !ui.arm_danger, "USER, LOAD: key 2 asks \"Load user U02?\" and the item follows");
    h0 = rk_state();
    tap(B_HOME);
    check(!op_armed() && rk_state() == h0 && up_ops[0] == 0u, "... HOME: nothing changed");
    pf_key(1);
    tap(B_SAVE);
    check(!op_armed() && up_ops[0] == 1u && !strcmp(pf_val(r, 1), "LOAD"), "... YES: loaded");
    turn(EN_K2, 1);
    pf_key(0);
    check(op_armed() && !strcmp(ui.arm_q, "SAVE USER U01?") && ui.arm_danger, "USER, SAVE over a used slot: asks, red");
    tap(B_HOME);
    pf_key(7);
    check(op_armed() && !strcmp(ui.arm_q, "SAVE USER U08?") && !ui.arm_danger, "SAVE into a free slot: asks, neutral");
    tap(B_HOME);
    check(!strcmp(pf_val(r, 1), "SAVE"), "HOME on the question keeps the operation");
    check(prj_op() == PO_SAVE, "... (OPERATION is SAVE)");
    turn(EN_K2, 1);
    h0 = up_ops[1];
    pf_key(1);
    check(op_armed() && !strcmp(ui.arm_q, "CLEAR USER U02?") && ui.arm_danger, "USER, CLEAR: asks, red");
    tap(B_SAVE);
    check(up_ops[1] == h0 + 1u && !strcmp(pf_val(r, 1), "LOAD"), "... YES: cleared, and LOAD again (a stray key never saves)");
    /* a free slot: LOAD and CLEAR have nothing to do, a toast and no question */
    h0 = rk_state();
    pf_key(9);
    check(!op_armed() && ui.toast_t && !strcmp(ui.msg, "SLOT U10 IS EMPTY") && rk_state() == h0 && ui.user_slot == 9u, "LOAD of a free slot: toast \"Slot U10 is empty\", no question");
    turn(EN_K2, 1);
    turn(EN_K2, 1);
    ui.toast_t = 0;
    pf_key(10);
    check(!op_armed() && ui.toast_t && !strcmp(ui.msg, "SLOT U11 IS EMPTY") && rk_state() == h0, "CLEAR of a free slot: the same toast");
    /* YES with no key: the operation on the knob's item */
    r = pf_row(PR_USER);
    ui.user_slot = 1;
    tap(B_SAVE);
    check(op_armed() && !strcmp(ui.arm_q, "LOAD USER U02?"), "YES on the row: asks to LOAD the knob's item");
    up_ops[0] = 0;
    tap(B_SAVE);
    check(up_ops[0] == 1u, "... YES: loaded");
    ui.hot = 0;
    ui.user_slot = 1;
    tap(B_SAVE);
    check(op_armed(), "YES on the BANK card runs the operation too");
    tap(B_HOME);
    /* the operation: back to LOAD on leaving the screen and on another row */
    r = pf_row(PR_USER);
    turn(EN_K2, 1);
    op_enter(SCR_HOME);
    op_enter(SCR_PROJECT);
    ui.row[SCR_PROJECT] = (uint8_t)r;
    check(prj_op() == PO_LOAD, "leaving PROJECT: LOAD again");
    turn(EN_K2, 1);
    turn(EN_SELECT, 1);
    check(prj_op() == PO_LOAD, "another row: LOAD again");
    /* PROJECT: NEW is its own card and touches no slot; SAVE asks; the item keys */
    r = pf_row(PR_PROJECT);
    ui.hot = 3;
    SCREENS[SCR_PROJECT].cell(r, 3, &c);
    check(c.kind == CK_ACT && !strcmp(c.label, "NEW") && c.danger, "PROJECT's fourth card: NEW, an action, red");
    h0 = loads + saves;
    tap(B_SAVE);
    check(op_armed() && !strcmp(ui.arm_q, "NEW PROJECT?") && ui.arm_danger && loads + saves == h0, "NEW asks \"New project?\", red; no slot touched");
    tap(B_HOME);
    song.g[G_SLOT] = 1;
    loads = 0;
    if (project_used(1)) {
        pf_key(1);
        check(song.g[G_SLOT] == 2 && op_armed() && !strcmp(ui.arm_q, "LOAD PROJECT 2?"), "PROJECT: key 2 asks \"Load project 2?\"");
        tap(B_SAVE);
#if SEC_LOGGED
        check(!op_armed(), "... YES: asked no more (project_load is the log's here)");
#else
        check(loads == 1u, "... YES: project 2 loaded");
#endif
    }
    ui.toast_t = 0;
    pf_key(3);
    check(song.g[G_SLOT] == 4 && (op_armed() != (ui.toast_t != 0u)), "PROJECT: key 4 selects slot 4: a question when used, a toast when free");
    op_disarm();
    /* the panel's info, read from what is stored */
    r = pf_row(PR_USER);
    prj_info(PR_USER, 0);
    check(!strcmp(pdp.name, "MY LEAD") && !strcmp(pdp.d1, "ENGINE") && pdp.d2[0], "info, a used user preset: its name and engine");
    prj_info(PR_USER, 7);
    check(!strcmp(pdp.name, "EMPTY") && !strcmp(pdp.d1, "FREE SLOT") && !pdp.d2[0], "info, a free user preset: Empty, free slot");
    if (project_used(0)) {
        prj_info(PR_PROJECT, 0);
        check(pdp.name[0] && !strcmp(pdp.d1, "STORED"), "info, a used project: a name, stored");
    }
    if (!project_used(3)) {
        prj_info(PR_PROJECT, 3);
        check(!strcmp(pdp.name, "EMPTY") && !strcmp(pdp.d1, "FREE SLOT"), "info, a free project");
    }
    ui.user_slot = 0;
    ui.force = 1;
    frame();
    check(px_in(0, OY_PANEL_2X2, 240, 14, C_GRAY), "the panel: the bank's strip, used slots filled");
    /* the keys light the slots (the strip's twin) and the item stays in sync with them */
    r = pf_row(PR_USER);
    ui.user_slot = 20;
    pf_key(2);
    op_disarm();
    check(ui.user_slot == 18u, "bank 2: key 3 is U19, the item and the key LEDs follow the same bank");
    reset_ui();
}
