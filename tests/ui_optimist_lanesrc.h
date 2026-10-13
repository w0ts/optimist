/* 2026-10-13 (the user: "this should be on third knob on page 1"): a drum lane's SRC is knob 3 of the SOUND row (page 1, with
 * KIT and LANE); PAGES' SOURCE page shows only HIT START LEN, on a lane playing a user sample, and is gone elsewhere */
static uint32_t lsrc_rows_titled(const char *t)         /* the SOUND screen's rows titled t */
{
    uint32_t r, n = 0, rows = SCR->rows();
    char nm[16];
    for (r = 0; r < rows; r++) {
        SCR->name(r, nm);
        n += str_eq(nm, t);
    }
    return n;
}
/* lane l on source src: page 1's knob 3 is its SRC; a lane that has it can be edited there */
static int lsrc_check_lane(uint32_t l, uint8_t src, const char *what)
{
    cell_t c;
    int ok;
    char msg[96];
    fm1_irq_off();
    memset(dl.ref[l], 0, sizeof dl.ref[l]);
    dl.src[l] = src;
    fm1_irq_on();
    lane_select(l);
    ui.lane = 1;
    op_enter(SCR_SOUND);
    snd_family(SND_ALL);
    ui.row[SCR_SOUND] = 0;
    frame();
    memset(&c, 0, sizeof c);
    SCR->cell(0, 2, &c);
    ok = c.label && str_eq(c.label, "SRC") && c.kind != CK_RO;
    (void)snprintf(msg, sizeof msg, "%s: SRC is page 1's knob 3", what);
    check(ok, msg);
    memset(&c, 0, sizeof c);
    SCR->cell(0, 3, &c);
    (void)snprintf(msg, sizeof msg, "%s: page 1's knob 4 stays empty", what);
    check(!c.label || !c.label[0], msg);
    return ok;
}
static void lane_src_tests(void)
{
    uint32_t src0, l = 2u, rows_kit;
    uint8_t before;
    cell_t c;
    reset_ui();
    song.sel = TRK_DRUM;
    TDRUM->p[P_E0] = DRUM_SAMPLED;
    op_enter(SCR_SOUND);
    check(lsrc_check_lane(l, DL_KIT, "a synthesised kit lane"), "(kit lane)");
    check(lsrc_rows_titled("SOURCE") == 0u, "a kit lane: no SOURCE page");
    rows_kit = SCR->rows();
    ui.force = 1;
    frame();
    ppm("opt-lane-src");
    memset(&c, 0, sizeof c);
    SCR->cell(0, 2, &c);
    check(c.d && c.d->label && str_eq(c.d->label, "SRC") && c.col == 0u + c.col, "the cell is SRC's descriptor (the help line's key)");
#if FELUCCA_PARAM_HELP
    check(hl_find(SCR_SOUND, 0, &c) != 0u, "SRC on page 1 keeps its help line");
#endif
    before = dl.src[l];
    ui.row[SCR_SOUND] = 0;
    turn(EN_K3, 1);
    check(dl.src[l] != before, "KNOB 3 on page 1: the lane's source changes");
    check(snd_page(0) == 0 && ui.row[SCR_SOUND] == 0u, "... and the cursor stays on page 1");
    turn(EN_K3, -1);
    check(dl.src[l] == before, "... and back");
    src0 = dl.src[l];
    turn(EN_K3, OP_RESET);
    check(dl.src[l] == src0, "HOME + KNOB 3 (reset): SRC has no default to go back to, unchanged");
    lsrc_check_lane(l, DL_KIT0, "a sampled kit lane");
    check(lsrc_rows_titled("SOURCE") == 0u && SCR->rows() == rows_kit, "a sampled kit lane: no SOURCE page, as many pages");
#if FELUCCA_DRUM_USR
    check(lsrc_check_lane(l, DL_USR, "a user-sample lane"), "(usr lane)");
    check(lsrc_rows_titled("SOURCE") == 1u && SCR->rows() == rows_kit + 1u, "a user-sample lane: SOURCE, one page more");
    {
        uint32_t r;
        char nm[16];
        for (r = 0; r < SCR->rows(); r++) {
            SCR->name(r, nm);
            if (str_eq(nm, "SOURCE"))
                break;
        }
        memset(&c, 0, sizeof c);
        SCR->cell(r, 0, &c);
        check(c.label && str_eq(c.label, "HIT"), "... its first card is HIT (SRC moved to page 1)");
        memset(&c, 0, sizeof c);
        SCR->cell(r, 3, &c);
        check(!c.label || !c.label[0], "... and its fourth card is empty");
        ui.row[SCR_SOUND] = (uint8_t)r;
        turn(EN_K1, 1);
        check(dl_hit(dl.ref[l]) == 1u, "... HIT edits the sample's hit");
        ui.row[SCR_SOUND] = 0;
        lane_select(l == 2u ? 3u : 2u);                 /* another lane (KIT): the row the cursor was on goes */
        ui.row[SCR_SOUND] = (uint8_t)r;
        op_rows_fix();
        check(ui.row[SCR_SOUND] < SCR->rows(), "a remembered row past the removed page clamps");
    }
#endif
#if DRUM_X0X
    {
        uint32_t x = FELUCCA_DRUM_X909 ? DL_X909 : DL_X808;
        check(lsrc_check_lane(l, (uint8_t)x, "an X0X lane"), "(x0x lane)");
        check(lsrc_rows_titled("SOURCE") == 0u, "an X0X lane: no SOURCE page");
    }
#endif
    {   /* page memory: EDIT remembers row 5 on a user-sample lane, the lane goes to a kit sound: the memory clamps */
        uint32_t n;
        lane_select(l);
        ui.lane = 1;
        snd_family(FAM_EDIT);
        n = SCR->rows();
        snd_mem[song.sel % NTRK][snd_slot(FAM_EDIT)] = 200u;
        check(snd_mem_row(FAM_EDIT) == n - 1u, "page memory past the pages there are: the last page");
    }
    fm1_irq_off();
    memset(&dl, 0, sizeof dl);
    fm1_irq_on();
    lane_select(0);
    reset_ui();
}
