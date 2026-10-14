/* 2026-10-14 (the user: "its first card should not allow to switch kit"): the drum track's SOUND row, first card, follows the
 * lane's source. DR itself and a lane on the KIT: KIT (the track's). A lane on an instrument: its PRESET (the lane's own
 * sound, as PRESETS steps it). A lane on a user sample: its SLOT (USR1..3). Nothing to pick (the ACCENT lane): KIT dim "-".
 * A turn never changes the kit of a lane off the KIT; HOME + the knob goes to the first sound of the kind (USR1) */
static uint8_t lk_inst(uint32_t l)                      /* the first source this build plays that is not the KIT, a sample or the kit's own sound */
{
    uint32_t i, n = ls_count(l);
    for (i = 4u; i < n && (!ls_ok(i) || str_eq(DS_SRC_NAMES[i], drum_kit_name())); i++)
        ;
    return (uint8_t)dsnd_idx_src(i < n ? i : 4u);
}
static void lk_show(uint32_t l, uint8_t src)            /* lane l on source src, the SOUND row's page 1 up */
{
    fm1_irq_off();
    memset(dl.ref[l], 0, sizeof dl.ref[l]);
    dl.src[l] = src;
    fm1_irq_on();
    lane_select(l);
    ui.lane = 1;
    op_enter(SCR_SOUND);
    snd_family(SND_ALL);
    ui.row[SCR_SOUND] = 0;
    ui.force = 1;
    snd_mark(TRK_DRUM);                                 /* (the source set here is the sound as loaded, not an edit) */
    frame();
}
static void lk_cell(cell_t *c)
{
    memset(c, 0, sizeof *c);
    SCR->cell(0, 0, c);
}
static int lk_card(const char *label, const char *val)  /* card 1 reads label / val */
{
    cell_t c;
    lk_cell(&c);
    return c.label && str_eq(c.label, label) && (!val || str_eq(c.val, val));
}
#if FELUCCA_PARAM_HELP
static int lk_help(const char *want)                    /* the card's help line is want */
{
    cell_t c;
    char b[PH_LINE];
    uint32_t o;
    lk_cell(&c);
    o = hl_find(SCR_SOUND, 0, &c);
    return o && !strcmp(ph_text(o, b), want);
}
#endif
static void lanekit_tests(void)
{
    uint32_t l = 2u, kit0, pos0;
    uint8_t s0;
    cell_t c;
    reset_ui();
    song.sel = TRK_DRUM;
    TDRUM->p[P_E0] = DRUM_SAMPLED;
    op_enter(SCR_SOUND);
    /* a kit lane: KIT, a turn changes the kit */
    lk_show(l, DL_KIT);
    check(lk_card("KIT", drum_kit_name()), "a kit lane: the first card is KIT, the kit's name");
    check(lane_card_form() == LCF_KIT, "... its form is the kit's");
    ppm("lanekit-kit");
    kit0 = drum_kit_pos();
    turn(EN_K1, 1);
    check(drum_kit_pos() != kit0 && dl.src[l] == DL_KIT, "... a turn changes the kit, the lane stays on it");
    turn(EN_K1, -1);
    check(drum_kit_pos() == kit0, "... and back");
    turn(EN_K1, OP_RESET);
    check(drum_kit_pos() == kit0 && dl.src[l] == DL_KIT, "... HOME + the knob: nothing (the kit has no default)");
#if FELUCCA_PARAM_HELP
    check(lk_help("The drum kit"), "... its help line is the kit's");
#endif
    /* DR itself selected: KIT */
    lane_select(0);
    ui.lane = 0;
    frame();
    check(lk_card("KIT", 0) && lane_card_form() == LCF_KIT, "DR itself: KIT");
    /* an instrument lane (another kit's sound): PRESET, the lane's own sound */
    lk_show(l, lk_inst(l));
    check(lane_card_form() == LCF_PRESET && lk_card("PRESET", lane_sound_name(l)), "an instrument lane: PRESET, the lane's sound");
    lk_cell(&c);
    check(c.kind == CK_VAL && !c.dim && !c.dr && str_eq(c.val, lane_sound_name(l)) && !str_eq(c.val, drum_kit_name()),
          "... a value card, not dim, not tagged DR, not the kit's name");
    ppm("lanekit-preset");
    kit0 = drum_kit_pos();
    s0 = dl.src[l];
    turn(EN_K1, 1);
    check(dl.src[l] != s0 && dl.src[l] != DL_KIT && drum_kit_pos() == kit0, "... a turn browses the lane's presets, the kit unchanged");
    turn(EN_K1, -1);
    check(dl.src[l] == s0, "... and back");
    turn(EN_K1, 1);
    turn(EN_K1, OP_RESET);
    check(drum_kit_pos() == kit0 && dl.src[l] != DL_KIT && dl.src[l] >= DL_KIT0 && dl.src[l] < DL_KIT0 + DRUM_KITS,
          "... HOME + the knob: the first sound of its kind, still a lane sound, the kit unchanged");
    check(lk_card("PRESET", lane_sound_name(l)) && lane_sound_name(l)[0], "... the card shows it");
#if FELUCCA_PARAM_HELP
    check(lk_help("This lane's sound (asks)"), "... its help line is the lane's preset");
#endif
    /* the same lane over an edited sound: the risk-ask, as PRESETS */
    lk_show(l, lk_inst(l));
    snd_mark(TRK_DRUM);
    dl.ofs[0][0] = (int8_t)(dl.ofs[0][0] ^ 1);
    s0 = dl.src[l];
    turn(EN_K1, 1);
    check(op_armed() && !strcmp(ui.arm_q, "DISCARD DR EDITS?") && dl.src[l] == s0, "... over an edited sound: \"Discard DR edits?\", nothing yet");
    tap(B_HOME);
    check(!op_armed() && dl.src[l] == s0, "... HOME: the sound kept");
    turn(EN_K1, 1);
    tap(B_SAVE);
    check(!op_armed() && dl.src[l] != s0 && dl.src[l] != DL_KIT, "... YES: the lane's next sound");
    dl.ofs[0][0] = 0;
#if FELUCCA_DRUM_USR
    /* a user sample lane: SLOT */
    lk_show(l, DL_USR);
    check(lane_card_form() == LCF_SLOT && lk_card("SLOT", "USR1"), "a user-sample lane: SLOT, USR1");
    ppm("lanekit-slot");
    kit0 = drum_kit_pos();
    turn(EN_K1, 1);
    check(dl.src[l] == DL_USR + 1u && lk_card("SLOT", "USR2") && drum_kit_pos() == kit0, "... a turn: USR2, the kit unchanged");
    turn(EN_K1, 1);
    turn(EN_K1, 1);
    check(dl.src[l] == DL_USR && drum_kit_pos() == kit0, "... USR3 then round to USR1: it stays on the samples");
    turn(EN_K1, -1);
    check(dl.src[l] == DL_USR + SMP_USER_SLOTS - 1u, "... backwards: USR3");
    turn(EN_K1, OP_RESET);
    check(dl.src[l] == DL_USR && lk_card("SLOT", "USR1") && drum_kit_pos() == kit0, "... HOME + the knob: USR1");
#if FELUCCA_PARAM_HELP
    check(lk_help("User sample of this lane"), "... its help line is the slot's");
#endif
#endif
    /* nothing to pick: the ACCENT lane */
    if (ACC_UI) {
        lk_show(DRUM_ACC, DL_KIT);
        check(lane_card_form() == LCF_NONE && lk_card("KIT", "-"), "the ACCENT lane: KIT, \"-\"");
        lk_cell(&c);
        check(c.dim && c.kind == CK_RO && !c.dr, "... dim, a read-out");
        ppm("lanekit-none");
        kit0 = drum_kit_pos();
        pos0 = ui.row[SCR_SOUND];
        turn(EN_K1, 1);
        turn(EN_K1, -1);
        turn(EN_K1, OP_RESET);
        check(drum_kit_pos() == kit0 && pos0 == ui.row[SCR_SOUND], "... a turn and HOME + a turn do nothing, the kit unchanged");
#if FELUCCA_PARAM_HELP
        check(lk_help("Nothing to pick for this lane"), "... its help line says so");
#endif
    }
    /* the mixer's lane rows name the lane's sound, "-" on the ACCENT lane; the kit's name only on a kit lane */
    lk_show(l, lk_inst(l));
    {
        char pre[16], eng[16];
        mx_names(MXR_LANE0 + l, eng, pre);
        check(str_eq(pre, lane_sound_name(l)) && !str_eq(pre, drum_kit_name()), "the mixer's lane row: the lane's sound, not the kit's");
        if (ACC_UI) {
            mx_names(MXR_LANE0 + DRUM_ACC, eng, pre);
            check(!pre[0], "the mixer's ACCENT lane row: no sound named");
        }
    }
    fm1_irq_off();
    memset(&dl, 0, sizeof dl);
    fm1_irq_on();
    lane_select(0);
    reset_ui();
}
