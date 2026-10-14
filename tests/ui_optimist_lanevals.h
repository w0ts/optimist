/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, a picked drum lane shows the lane's values (the user, 2026-10-14: "on SEQ while i edit the BD
 * lane, i think the fx sends are the ones from the main drum track, not from the lane; when i switch drum lane the value
 * remain the same; remember on a lane i want to see the lane"):
 *   SOUND > FX     with a lane picked, INSERTS (DST CMP DRIVE) and SENDS (its CHO DLY REV, in the master slots' order) are the
 *                  lane's; the drum track's are "DR INSERT", "DR SENDS", BUS SENDS, SLICER ..., every card tagged DR (cell_t dr)
 *   STEP -> FX     the same, reached from STEP with the lane picked by SEQ + a key
 *   DR itself      the track's values, as before, no tag
 *   DR-only cards  PATTERN, the KIT card and its slot actions: tagged DR under a lane
 * Included after the helpers (WK, kdown, kup, ins_row). */
#if DL_UI && FELUCCA_FX_CHORUS && FELUCCA_FX_DELAY && FELUCCA_FX_REVERB
static const cell_t *lv_card(uint32_t r, const char *label)   /* row r's card named label (0: none) */
{
    static cell_t c[4];
    uint32_t k;
    for (k = 0; k < 4u; k++) {
        SCR->cell(r, k, &c[k]);
        if (c[k].label && !strcmp(c[k].label, label))
            return &c[k];
    }
    return 0;
}
static int lv_row_dr(uint32_t r)                        /* every card of row r that shows a value carries the DR tag */
{
    cell_t c;
    uint32_t k, n = 0, d = 0;
    for (k = 0; k < 4u; k++) {
        SCR->cell(r, k, &c);
        n += c.label && c.label[0];
        d += c.label && c.label[0] && c.dr;
    }
    return n && n == d;
}
static int lv_row_plain(uint32_t r)                     /* no card of row r is tagged */
{
    cell_t c;
    uint32_t k, d = 0;
    for (k = 0; k < 4u; k++) {
        SCR->cell(r, k, &c);
        d += c.dr;
    }
    return d == 0u;
}
static void lv_fx_family(void)                          /* SOUND, the FX family, its first row */
{
    op_enter(SCR_SOUND);
    snd_family(FAM_FX);
    ui.row[SCR_SOUND] = 0;
    frames(2);
}
static void lv_name(uint32_t r, char *b) { SCR->name(r, b); }
static uint32_t lv_row_named(const char *nm)            /* the FX family's row named nm, SCR->rows() none */
{
    uint32_t r;
    char b[12];
    for (r = 0; r < SCR->rows(); r++) {
        lv_name(r, b);
        if (!strcmp(b, nm))
            return r;
    }
    return r;
}
static void lv_set(void)                                /* the sends of two lanes and the drum track, all different */
{
    reset_ui();
    ins_defaults();
    fxs_set(FXS_DEF);
    song.sel = TRK_DRUM;
    TDRUM->p[P_E0] = DRUM_SAMPLED;
    dsend_kit[0] = 50, dsend_kit[1] = 60, dsend_kit[2] = 70;
    dsend[4] = dsend_word(9, 3, 5);                     /* REV 9, DLY 3, CHO 5 */
    dsend[7] = dsend_word(2, 11, 20);
    ui.lane = 1;
}
static void lv_sends_tests(const char *how)
{
    char msg[160];
    const cell_t *c;
    uint32_t r;
    lane_select(4);
    lv_fx_family();
    r = lv_row_named("SENDS");
    snprintf(msg, sizeof msg, "%s: a lane picked, FX: a row SENDS, ahead of the drum track's", how);
    check(r == 1u || r == 0u, msg);
    c = lv_card(r, "CHO");
    snprintf(msg, sizeof msg, "%s: lane A's SENDS: CHO is its own (5), not the drum track's level", how);
    check(c && c->gv == 5 && !c->dr, msg);
    c = lv_card(r, "REV");
    check(c && c->gv == 9 && !c->dr, "... REV 9, DLY 3 the lane's too");
    c = lv_card(r, "DLY");
    check(c && c->gv == 3, "... DLY 3");
    lane_select(7);
    frames(2);
    r = lv_row_named("SENDS");
    c = lv_card(r, "CHO");
    snprintf(msg, sizeof msg, "%s: lane B picked: SENDS shows lane B's values (CHO 20, not lane A's)", how);
    check(c && c->gv == 20, msg);
    c = lv_card(r, "DLY");
    check(c && c->gv == 11, "... DLY 11");
    c = lv_card(r, "REV");
    check(c && c->gv == 2, "... REV 2");
    c = lv_card(r, "PAN");
    check(c && c->gv == dlm_pan[7], "... card 4 PAN: lane B's own pan");
    ui.row[SCR_SOUND] = (uint8_t)r;
    ui.hot = 0;
    turn(EN_K1, 3);                                     /* KNOB 1: the first send, REV (REV DLY CHO, the one order) */
    check(dsend_rev(dsend[7]) == 5u && dsend_rev(dsend[4]) == 9u && dsend_dly(dsend[7]) == 11u && dsend_cho(dsend[7]) == 20u,
          "KNOB 1 on lane B's SENDS: lane B's REV up, lane A's and the other sends kept");
    check(dsend_kit[0] == 50 && dsend_kit[1] == 60 && dsend_kit[2] == 70, "... the drum track's send levels untouched");
    turn(EN_K1, -3);
    lane_select(4);
    frames(2);
    r = lv_row_named("SENDS");
    c = lv_card(r, "CHO");
    check(c && c->gv == 5, "... back on lane A: its CHO as it was");
}
static void lv_dr_tag_tests(void)
{
    uint32_t r;
    char b[12];
    lane_select(4);
    lv_fx_family();
    r = lv_row_named("INSERTS");
    check(r < SCR->rows() && lv_row_plain(r) && lv_card(r, "DST"), "a lane picked: INSERTS is the lane's (its S1 TYPE, DST, S2 TYPE, DYN), no tag");
    check(lv_card(r, "S1") && lv_card(r, "S1")->vp != &ins_v[0], "... its own slots, not the drum bus's S1 / S2");
    ui.row[SCR_SOUND] = (uint8_t)r;
    frames(2);
    ppm("opt-lane-inserts");                            /* (fx-rework: the lane's S1 TYPE, its amount, S2 TYPE, its amount) */
    r = lv_row_named("DR INSERT");                      /* (uniform with the other tracks, 2026-10-14: a lane shows its INSERTS and SENDS only) */
    check(r >= SCR->rows(), "a lane picked: no \"DR INSERT\" row");
    r = lv_row_named("DR SENDS");
    check(r >= SCR->rows(), "a lane picked: no \"DR SENDS\" (kit sends) row");
    r = lv_row_named("BUS SENDS");
    check(r >= SCR->rows(), "a lane picked: no BUS SENDS row");
    r = lv_row_named("SLICER");
    check(r >= SCR->rows(), "a lane picked: no SLICER row");
    op_enter(SCR_SOUND);
    snd_family(SND_ALL);
    r = lv_row_named("PATTERN");
    check(r < SCR->rows() && lv_row_dr(r), "PATTERN (LEN DIV SWG GATE): the track's, tagged DR");
    r = lv_row_named("SOUND");
    {
        cell_t c;
        SCR->cell(r, 0, &c);
        check(!strcmp(c.label, "KIT") && c.dr, "the SOUND row's first card: the KIT, tagged DR (a lane on the kit)");
        SCR->cell(r, 1, &c);
        check(!c.label && c.kind == CK_NONE, "... no second card: the LANE read-out is gone (2026-10-14)");
    }
    r = lv_row_named("SOUND 3");
    check(r < SCR->rows() && lv_row_plain(r), "SOUND 3 (the lane's DST CHO DLY REV): no tag");
    r = lv_row_named("ENV");
    check(r < SCR->rows() && lv_row_plain(r), "the lane's ENV: no tag");
    (void)b;
}
static void lv_dr_tests(void)
{
    uint32_t r, n;
    cell_t c;
    lane_select(0);
    ui.lane = 0;
    lv_fx_family();
    r = lv_row_named("SENDS");
    check(r < SCR->rows(), "DR itself picked, FX: SENDS");
    SCR->cell(r, 0, &c);
    check(c.vp == &dsend_kit[0] && c.gv == 50 && !c.dr, "... the drum track's send levels (REV 50 first), as before");
    check(lv_row_named("DR SENDS") >= SCR->rows() && lv_row_named("DR INSERT") >= SCR->rows(), "... no \"DR\" rows: the names are the plain ones");
    r = lv_row_named("INSERTS");
    check(r < SCR->rows() && lv_card(r, "S1") && lv_row_plain(r), "... INSERTS: the drum bus's S1 / S2, no tag");
    check(lv_row_plain(lv_row_named("BUS SENDS")) && lv_row_plain(lv_row_named("SLICER")), "... BUS SENDS, SLICER: no tag");
    op_enter(SCR_SOUND);
    snd_family(SND_ALL);
    n = SCR->rows();
    r = lv_row_named("PATTERN");
    check(r < n && lv_row_plain(r), "... PATTERN: no tag");
    SCR->cell(lv_row_named("SOUND"), 0, &c);
    check(!c.dr, "... the KIT card: no tag");
}
static void lv_step_tests(void)                         /* from STEP: the lane picked with HOME + a key, FX */
{
    uint32_t kick = lane_of_key(WK(0)), snare = lane_of_key(WK(2)), r;
    const cell_t *c;
    cell_t d;
    lv_set();
    lane_select(0);
    ui.lane = 0;
    dsend[kick] = dsend_word(9, 3, 5);
    dsend[snare] = dsend_word(2, 11, 20);
    op_enter(SCR_STEP);
    frames(2);
    press(B_OCTDN);
    kdown(WK(0));
    kup(WK(0));
    release(B_OCTDN);
    check(lane_selected() == kick && ui.lane == 1u && ui.scr == SCR_STEP, "STEP, HOME held + a key: the lane picked");
    tap(B_FX);
    check(ui.scr == SCR_SOUND, "... FX opens the FX family");
    r = lv_row_named("SENDS");
    c = lv_card(r, "CHO");
    check(c && c->gv == 5 && !c->dr, "... the kick's SENDS: its CHO 5");
    ui.hot = 0;
    SCR->cell(r, 0, &d);
    ppm("opt-lane-sends-kick");
    tap(B_SEQ);
    check(ui.scr == SCR_STEP, "SEQ: back on STEP");
    press(B_OCTDN);
    kdown(WK(2));
    kup(WK(2));
    release(B_OCTDN);
    check(lane_selected() == snare, "STEP, HOME held + another key: that lane");
    tap(B_FX);
    r = lv_row_named("SENDS");
    c = lv_card(r, "CHO");
    check(c && c->gv == 20, "... FX: the snare's SENDS: its CHO 20, the value changed with the lane");
    frames(2);
    ppm("opt-lane-sends-snare");
    tap(B_SEQ);                                         /* (STEP has no lane page any more: its LEVEL TUNE DECAY REV left, 2026-10-14) */
    {   /* STEP's first page: PATTERN, the track's */
        ui.row[SCR_STEP] = 0;
        frames(2);
        SCR->cell(0, 0, &d);
        check(d.label && !strcmp(d.label, "LEN") && d.dr, "STEP's PATTERN page under a lane: tagged DR");
        lane_select(0);
        ui.lane = 0;
        frames(2);
        SCR->cell(0, 0, &d);
        check(!d.dr, "... DR itself: no tag");
    }
}
static void lv_acc_tests(void)
{
#if ACC_UI
    uint32_t r;
    lv_set();
    lane_select(DRUM_ACC);
    lv_fx_family();
    check(lv_row_named("SENDS") >= SCR->rows() && lv_row_named("DR SENDS") < SCR->rows(),
          "the ACCENT lane (no sound): no lane SENDS of its own, the drum track's are \"DR SENDS\"");
    r = lv_row_named("DR INSERT");
    check(r < SCR->rows() && lv_row_dr(r), "... and its INSERTS the drum bus's, tagged DR");
#endif
}
static void lanevals_tests(void)
{
    lv_set();
    lane_select(4);
    lv_sends_tests("SOUND");
    lv_set();
    lv_dr_tag_tests();
    lv_set();
    lv_dr_tests();
    lv_step_tests();
    lv_acc_tests();
    lv_set();
    dsend_kit[0] = dsend_kit[1] = dsend_kit[2] = DKS_DEF;
    memset(dsend, 0, sizeof dsend);
    ui.lane = 0;
    lane_select(0);
    reset_ui();
}
#else
static void lanevals_tests(void) {}
#endif
