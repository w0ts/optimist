/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, the action cards (the user, 2026-10-13, from screenshots): a card's big value is the verb
 * itself, never "YES"; running it asks first (the modal: SAVE yes, HOME no), changed work or not; and on PROJECT the
 * note keys pick the slot of the cursor row and play nothing. Included after ui_optimist_risk.h. */
static uint32_t ac_cards, ac_bad;
/* every action cell of every screen and row, on a synth track and on the drum track: the verb is shown (not "YES", not
 * empty), YES on it asks, HOME lets it go and changes nothing */
static void ac_walk(uint32_t scr, uint32_t tr)
{
    uint32_t r, k, n, h0;
    cell_t c;
    char nm[16];
    song.sel = tr;
    op_enter(scr);
    frame();
    if (ui.scr != scr)
        return;
    n = SCREENS[scr].rows();
    for (r = 0; r < n; r++)
        for (k = 0; k < 4u; k++) {
            SCREENS[scr].cell(r, k, &c);
            if (c.kind != CK_ACT)
                continue;
            SCREENS[scr].name(r, nm);
            ac_cards++;
            if (!act_verb(&c)[0] || !strcmp(act_verb(&c), "YES")) {
                printf("  actcards: screen %u row %s cell %u: verb \"%s\"\n", scr, nm, k, act_verb(&c));
                ac_bad++;
            }
#if FELUCCA_PARAM_HELP
            {                                           /* (no help line says "twice": every action asks with the modal) */
                char hb[PH_LINE];
                uint32_t o = hl_find(scr, r, &c);
                if (o && strstr(ph_text(o, hb), "twice")) {
                    printf("  actcards: screen %u row %s card %s: help \"%s\"\n", scr, nm, act_verb(&c), hb);
                    ac_bad++;
                }
            }
#endif
            if (scr == SCR_STEP && (!strcmp(c.label, "REVERSE") || !strcmp(c.label, "RANDOM") || !strcmp(c.label, "COOK")))
                continue;                               /* (STEP's tools: the verb shown, no question: undo covers them) */
            ui.row[scr] = (uint8_t)r;
            if (glo_cycle())
                gl_pick(r, k);                          /* (the settings lists: a page row's cell k is a line) */
            ui.hot = (uint8_t)k;
            ui.force = 1;
            frame();
            h0 = rk_state();
            turn(EN_K1 + k, 1);                         /* (a knob only picks the card: it never runs, never arms) */
            turn(EN_K1 + k, -3);
            if (op_armed() || rk_state() != h0) {
                printf("  actcards: screen %u row %s card %s: a knob turn ran or armed it\n", scr, nm, act_verb(&c));
                ac_bad++;
                op_disarm();
            }
            ui.row[scr] = (uint8_t)r;
            if (glo_cycle())
                gl_pick(r, k);
            ui.hot = (uint8_t)k;
            frame();
            tap(B_SAVE);
            if (!op_armed()) {
                printf("  actcards: screen %u row %s card %s: YES asked nothing\n", scr, nm, act_verb(&c));
                ac_bad++;
                continue;
            }
            tap(B_HOME);
            frame();
            if (op_armed() || rk_state() != h0) {
                printf("  actcards: screen %u row %s card %s: HOME did not cancel cleanly\n", scr, nm, act_verb(&c));
                ac_bad++;
            }
        }
}
static int ac_band_has(uint16_t col)                    /* the cards' band holds a pixel of col */
{
    return px_in(0, OY_CARD, 240, OP_CH, col);
}
static void ac_key_tests(void)
{
    uint32_t r, k, f, d, used = 0, w;
    reset_ui();
    song.sel = 0;
    op_enter(SCR_PROJECT);
    for (r = 0; r < NPRJ && prj_kind(r) != PR_PROJECT; r++)
        ;
    ui.row[SCR_PROJECT] = (uint8_t)r;
    frames(2);
    k = WK(2);
    kdown(k);
    check(song.g[G_SLOT] == 3 && ly_lock == LY_STEP && kb_kind[k] == KS_UI, "PROJECT, PROJECT row: key 3 picks slot 3, and plays nothing (the UI's)");
    kup(k);
    k = WK(12);
    kdown(k);
    check(song.g[G_SLOT] == (FELUCCA_SECTIONS > 12 ? 13 : 3), "... a key past the slots: nothing (4 here)");
    kup(k);
    k = WK(2);
    kdown(k);
    kup(k);
    k = 1u;                                             /* (a black key: silent, picks nothing) */
    kdown(k);
    check(song.g[G_SLOT] == 3 && kb_kind[k] == KS_UI, "... a black key: silent, picks nothing");
    kup(k);
    for (w = 0; w < FELUCCA_SECTIONS && w < 16u; w++)
        used |= (uint32_t)(project_used(w) != 0) << w;
    keys_lit(&f, &d);
    {
        uint32_t ok = 1;
        for (w = 0; w < 16u; w++) {
            uint32_t bit = 1u << key_of_lane(w), sel = w == 2u;
            if (w >= FELUCCA_SECTIONS)
                ok &= !((f | d) & bit);
            else if (!sel)
                ok &= ((used >> w) & 1u) ? (f & bit) != 0u : (d & bit) != 0u && !(f & bit);
        }
        check(ok, "... the keys light: a used slot full, a free one dim, past the slots dark");
    }
    {
        uint32_t bit = 1u << key_of_lane(2), seen_on = 0, seen_off = 0, i;
        for (i = 0; i < 40u; i++) {
            frames(1);
            keys_lit(&f, &d);
            if ((f | d) & bit)
                seen_on = 1;
            else
                seen_off = 1;
        }
        check(seen_on && seen_off, "... the selected slot's key blinks");
    }
    for (r = 0; r < NPRJ && prj_kind(r) != PR_USER; r++)
        ;
    ui.row[SCR_PROJECT] = (uint8_t)r;
    ui.user_slot = 20;                                  /* (page 2 of the 32 slots: U17 .. U32) */
    frames(2);
    k = WK(3);
    kdown(k);
    check(ui.user_slot == 19u, "USER row: key 4 picks the 4th slot of the page the cursor is on (U17 .. U32: U20)");
    kup(k);
    k = WK(15);
    ui.user_slot = 2;
    kdown(k);
    check(ui.user_slot == 15u, "... on page 1, key 16 is U16");
    kup(k);
#if FELUCCA_SNAPSHOTS
    for (r = 0; r < NPRJ && prj_kind(r) != PR_SNAP; r++)
        ;
    ui.row[SCR_PROJECT] = (uint8_t)r;
    frames(2);
    k = WK(1);
    kdown(k);
    check(ui.snap_slot == 1u && ly_lock == LY_STEP, "SNAPSHOT row: key 2 picks snapshot 2");
    kup(k);
#endif
    for (r = 0; r < NPRJ && prj_kind(r) != PR_TOOLS; r++)
        ;
    ui.row[SCR_PROJECT] = (uint8_t)r;
    frames(2);
    k = WK(2);
    kdown(k);
    check(ly_lock == LY_STEP && kb_kind[k] == KS_UI, "PROJECT's TOOLS row: the keys play nothing either");
    kup(k);
    keys_lit(&f, &d);
    check(!(f | d), "... and light nothing");
    op_enter(SCR_HOME);
    frames(2);
    kdown(k);
    check(ly_lock == LY_PLAY && kb_kind[k] != KS_UI, "leaving PROJECT: the keys play again");
    kup(k);
    reset_ui();
}
static void actcards_tests(void)
{
    uint32_t scr, tr;
    cell_t c;
    reset_ui();
    ac_cards = ac_bad = 0;
    for (scr = 0; scr < SCR_N; scr++)
        for (tr = 0; tr < 4u; tr += 3u)
            ac_walk(scr, tr);
    song.sel = 0;
    printf("  actcards: %u action cards walked\n", ac_cards);
    check(ac_cards >= 8u && !ac_bad, "every action card on every screen and row: the verb (never YES), YES asks, HOME cancels");
    reset_ui();
    rk_at(SCR_PROJECT, "PROJECT", "LOAD");
    SCREENS[SCR_PROJECT].cell(ui.row[SCR_PROJECT], 1, &c);
    check(!strcmp(act_verb(&c), "LOAD") && !strcmp(act_small(&c), "PROJECT") && !c.danger && !strcmp(c.label, "LOAD"),
          "PROJECT LOAD: the card says LOAD over PROJECT; the list column keeps its label");
    SCREENS[SCR_PROJECT].cell(ui.row[SCR_PROJECT], 3, &c);
    check(!strcmp(act_verb(&c), "NEW") && c.danger, "PROJECT NEW: NEW, in the alarm colour");
    rk_at(SCR_PROJECT, "USER", "ERASE");
    SCREENS[SCR_PROJECT].cell(ui.row[SCR_PROJECT], 2, &c);
    check(!strcmp(act_verb(&c), "ERASE") && !strcmp(act_small(&c), "USER") && c.danger, "USER ERASE: ERASE over USER, alarm");
    rk_at(SCR_SOUND, "SOUND", "INIT");
    SCREENS[SCR_SOUND].cell(ui.row[SCR_SOUND], 2, &c);
    check(!strcmp(act_verb(&c), "INIT") && !strcmp(act_small(&c), "SOUND") && c.danger, "SOUND INIT: INIT over SOUND, alarm");
    SCREENS[SCR_SOUND].cell(ui.row[SCR_SOUND], 3, &c);
    check(!strcmp(act_verb(&c), "SAVE AS") && !c.danger, "SOUND SAVE AS: SAVE AS, amber");
    ui.force = 1;
    frame();
    check(ac_band_has(C_ERR), "SOUND row 1: INIT drawn in the alarm colour");
    rk_at(SCR_SYSTEM, 0, "PANEL");
    SCREENS[SCR_SYSTEM].cell(ui.row[SCR_SYSTEM], 0, &c);
    check(!strcmp(act_verb(&c), "CALIBRATE") && !strcmp(act_small(&c), "PANEL"), "SYSTEM PANEL: CALIBRATE over PANEL");
    reset_ui();
    ac_key_tests();
}
