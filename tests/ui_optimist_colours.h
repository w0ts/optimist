/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_colours.h, batch C of the coherence audit (docs/UI-OPTIMIST-DESIGN.md, style guide 7): colour means
 * something. The GLO layer's mute tiles are filled only when muted; red only on what destroys or replaces work (the
 * action cards walked against a list, the modals agree with their cards); recording is green, waiting amber, never red;
 * dim only on a free / unused thing; no read-out in the palette's secondary step; SUPER, ANALOG and the drum coral differ
 * by hue. Included after ui_optimist_layout.h. */
static double cc_hue(uint16_t c)                        /* the hue (degrees) of an RGB565 colour */
{
    uint32_t r5 = (c >> 11) & 31u, g6 = (c >> 5) & 63u, b5 = c & 31u;
    double r = (double)(r5 << 3 | r5 >> 2), g = (double)(g6 << 2 | g6 >> 4), b = (double)(b5 << 3 | b5 >> 2);
    double mx = r > g ? (r > b ? r : b) : (g > b ? g : b), mn = r < g ? (r < b ? r : b) : (g < b ? g : b), d = mx - mn, h;
    if (d < 1.0)
        return 0.0;
    h = mx == r ? fmod((g - b) / d, 6.0) : mx == g ? (b - r) / d + 2.0 : (r - g) / d + 4.0;
    h *= 60.0;
    return h < 0.0 ? h + 360.0 : h;
}
static double cc_dist(uint16_t a, uint16_t b)           /* the hue distance, 0 .. 180 */
{
    double d = fabs(cc_hue(a) - cc_hue(b));
    return d > 180.0 ? 360.0 - d : d;
}
static void cc_hue_tests(void)
{
    uint16_t sup = COL_ENG_SUPER, ana = COL_ENG_ANALOG;
    uint32_t i, ok = 1;
    uint16_t kinds[4] = {COL_KIND_SYNTH, COL_KIND_SAMPLED, COL_KIND_X0X, COL_KIND_USERKIT};
    check(cc_dist(sup, COL_KIND_SYNTH) >= 15.0 && cc_dist(ana, COL_KIND_SYNTH) >= 15.0 && cc_dist(sup, ana) >= 15.0,
          "SUPER, ANALOG and the drum coral (DR) are three distinct hues, >= 15 degrees apart");
    for (i = 0; i < NENGINES; i++)
        if (cc_dist(ENG_COL[i], sup) < 15.0 && ENG_COL[i] != sup) {
            printf("  colours: engine %u is %.1f degrees from SUPER\n", i, cc_dist(ENG_COL[i], sup));
            ok = 0;
        }
    for (i = 0; i < 4u; i++)
        if (cc_dist(kinds[i], sup) < 15.0) {
            printf("  colours: kind %u is %.1f degrees from SUPER\n", i, cc_dist(kinds[i], sup));
            ok = 0;
        }
    check(ok, "SUPER is >= 15 degrees from every other engine and kit-kind colour");
    check(cl_contrast(sup) >= 3.0, "SUPER holds white text at >= 3:1");
}
static void cc_mute_tiles_tests(void)
{
    tile_t tl[16];
    char sub[32];
    uint32_t i, ok = 1;
    reset_ui();
    for (i = 0; i < NTRK; i++)
        trk[i].p[P_MUTE] = 0;
    lay.shown = LY_MIX;
    tiles_fill(tl, sub, sizeof sub);
    for (i = 0; i < 4u; i++)
        ok &= tl[i].bg == OP_SURF && tl[i].top == trk_col(i) && tl[i].fg != C_DIM;
    check(ok, "GLO layer, nothing muted: the four mute tiles are outlined (black), a band in the track colour");
    trk[1].p[P_MUTE] = 1;
    tiles_fill(tl, sub, sizeof sub);
    check(tl[1].bg == trk_col(1) && tl[0].bg == OP_SURF && tl[2].bg == OP_SURF && tl[3].bg == OP_SURF,
          "... T2 muted: its tile alone is filled in T2's colour");
    trk[1].p[P_MUTE] = 0;
    lay.shown = LY_PLAY;
    reset_ui();
}
/* every action cell of every screen on a synth track and on the drum track, against the list of what may be red */
static uint32_t cc_cards, cc_bad;
static void cc_red_walk(uint32_t scr, uint32_t tr)
{
    static const char *const RED_OK[] = {"NEW", "CLEAR", "ERASE", "INIT", "RESET", "DELETE", "IMPORT", "SET", "STORE", "LOAD", "SAVE"};
    uint32_t r, k, n, j;
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
            int allowed = 0;
            SCREENS[scr].cell(r, k, &c);
            if (c.kind != CK_ACT)
                continue;
            SCREENS[scr].name(r, nm);
            cc_cards++;
            if (!c.danger)
                continue;
            for (j = 0; j < sizeof RED_OK / sizeof RED_OK[0]; j++)
                allowed |= !strcmp(act_verb(&c), RED_OK[j]) || !strcmp(c.label, RED_OK[j]) || !strcmp(c.label, "CLRSQ");
            if (!allowed || (!strcmp(act_verb(&c), "SAVE") && scr != SCR_PROJECT)) {
                printf("  colours: screen %u row %s: \"%s\" is red but neither destroys nor replaces\n", scr, nm, act_verb(&c));
                cc_bad++;
            }
        }
}
static void cc_red_tests(void)
{
    uint32_t scr, tr, r, kind, ok = 1;
    cell_t c;
    reset_ui();
    cc_cards = cc_bad = 0;
    for (scr = 0; scr < SCR_N; scr++)
        for (tr = 0; tr < 4u; tr += 3u)
            cc_red_walk(scr, tr);
    song.sel = 0;
    printf("  colours: %u action cards walked for red\n", cc_cards);
    check(cc_cards >= 8u && !cc_bad, "red only on destroy / replace: every red action card's verb is on the list");
    /* SONG REC is never red; its card and the song layer's read-out are green recording, amber armed */
    reset_ui();
    op_enter(SCR_SONG);
    for (r = 0; r < SCREENS[SCR_SONG].rows(); r++) {
        SCREENS[SCR_SONG].cell(r, 1, &c);
        if (c.kind == CK_ACT && !strcmp(act_verb(&c), "REC"))
            break;
    }
    check(r < SCREENS[SCR_SONG].rows() && !c.danger && c.col == 0, "SONG REC: not red (not destructive), no colour while off");
    srec = 1;
    SCREENS[SCR_SONG].cell(r, 1, &c);
    check(!c.danger && c.col == C_WARN && act_col(&c, 0) == C_WARN, "SONG REC armed: amber (pending)");
    srec = 2;
    SCREENS[SCR_SONG].cell(r, 1, &c);
    check(!c.danger && c.col == C_OK && act_col(&c, 0) == C_OK, "SONG REC recording: green (running)");
    srec = 0;
    check(rec_col(1, 0) == C_OK && rec_col(0, 1) == C_WARN && rec_col(0, 0) == 0 && rec_col(1, 1) == C_OK,
          "the header's REC mark: recording green, waiting amber");
    /* the modal is red exactly when its card is, on PROJECT / SNAPSHOT / USER: SAVE over a used slot is red, into a free one not */
    for (kind = 0; kind < 3u; kind++) {
        uint32_t row;
        reset_ui();
        song.sel = 0;
        op_enter(SCR_PROJECT);
        for (row = 0; row < NPRJ && prj_kind(row) != kind; row++)
            ;
        if (row >= NPRJ)
            continue;
        ui.row[SCR_PROJECT] = (uint8_t)row;
        prj_op_row = (uint8_t)row;
        prj_op_v = PO_SAVE;                             /* (OPERATION on SAVE) */
        SCREENS[SCR_PROJECT].cell(row, 1, &c);
        ok &= !strcmp(c.val, "SAVE") && c.col == (prj_slot_used(kind, prj_slot_cur(kind)) ? C_ERR : 0);
        ui.hot = 1;
        frame();
        tap(B_SAVE);
        ok &= op_armed() && ui.arm_danger == (uint8_t)(prj_slot_used(kind, prj_slot_cur(kind)) != 0u);
        op_disarm();
    }
    check(ok, "PROJECT / SNAPSHOT / USER: SAVE is red over a used slot, neutral into a free one; the modal agrees with the card");
    reset_ui();
    op_enter(SCR_SOUND);
    op_arm(SCR_SOUND, 0, 3, "SAVE AS", "PRESET", 0);
    check(!ui.arm_danger, "the Save as modal is neutral");
    op_disarm();
    reset_ui();
}
/* dim is free / unused: a cell drawn dim is an empty slot, a free scene or slot, or a feature not in the build; and no cell
 * carries the palette's secondary step (C_AMB: a read-out is grey, amber is C_WARN) */
static void cc_dim_walk(uint32_t scr, uint32_t tr, uint32_t *bad_dim, uint32_t *bad_amb)
{
    uint32_t r, k, n;
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
            SCREENS[scr].name(r, nm);
            if (c.label && (c.dim || c.col == C_DIM) &&
                !(!strcmp(c.val, "EMPTY") || !strcmp(c.val, "NOT IN BUILD") || !strcmp(c.label, "SLOT") || !strcmp(c.label, "ITEM") || !strcmp(c.label, "SCENE"))) {
                printf("  colours: screen %u row %s cell %u (%s = %s) is dim but not free / unused\n", scr, nm, k, c.label, c.val);
                (*bad_dim)++;
            }
            if (c.col == C_AMB) {
                printf("  colours: screen %u row %s cell %u (%s) is in the secondary step\n", scr, nm, k, c.label ? c.label : "");
                (*bad_amb)++;
            }
        }
}
static void cc_dim_tests(void)
{
    uint32_t scr, tr, bad_dim = 0, bad_amb = 0, r, a;
    cell_t c;
    reset_ui();
    for (scr = 0; scr < SCR_N; scr++)
        for (tr = 0; tr < 4u; tr += 3u)
            cc_dim_walk(scr, tr, &bad_dim, &bad_amb);
    song.sel = 0;
    check(!bad_dim, "dim only on free / unused: EMPTY, a free slot or scene, a feature not in the build");
    check(!bad_amb, "no cell of any screen is drawn in the secondary step: amber is C_WARN, a read-out is grey");
    /* SONG's SCENE value: dim exactly when nothing is stored in that scene */
    op_enter(SCR_SONG);
    for (r = 0; r < SCREENS[SCR_SONG].rows(); r++) {
        SCREENS[SCR_SONG].cell(r, 0, &c);
        if (c.label && !strcmp(c.label, "SCENE") && c.kind == CK_VAL) {
            a = (uint32_t)(c.val[0] - 'A');
            check((c.col == C_DIM) == !((sg.used >> a) & 1u), "SONG's SCENE value is dim only when that scene is free");
            break;
        }
    }
    reset_ui();
}
static void colours_tests(void)
{
    cc_hue_tests();
    cc_mute_tiles_tests();
    cc_red_tests();
    cc_dim_tests();
}
