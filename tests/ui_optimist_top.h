/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, SYSTEM > TOP (builder item SYS_TOP: op_project.c, op_top.c; tests/run_tests.sh builds it with
 * the CPU1 load, no debug pages, an arena and the generator's table tests/top_ram_test.h): a page of its own after INFO,
 * drawn whole; the CPU's lines CPU0 CPU1 then the jobs by their average (the render's times fed as the render would,
 * dual_load.h dual_tm_block; a job timed after the block, Bluetooth's SBC, folded in: DUAL_TM_XJOB), a job not run in
 * 2 s left out; the RAM's lines SRAM KB then the owners by size: the arena's blocks as they are now (a block placed
 * shows, given back goes), the build's table, OTHER last; KNOB 2 scrolls, KNOB 1 switches CPU / RAM, SELECT and ALGORITHM go between the pages.
 * Without the item: no TOP. Screen opt-top.ppm. Included after the helpers (frame, ppm) */
#if OP_TOP && DUAL_TIMES
#define TOPT_P(k) (DUAL_TM_BLK * (k) / 100u)            /* k % of a block, ticks */
static uint32_t topt_v = 0;                             /* the view the helpers read: 0 CPU, 1 RAM */
static void topt_line(uint32_t r, cell_t *c)            /* the view's line r (the header apart; whatever is on screen) */
{
    uint32_t o = top.off, v = top.view;
    top.off = (uint8_t)r, top.view = (uint8_t)topt_v;
    top_cell(1, c);
    top.off = (uint8_t)o, top.view = (uint8_t)v;
}
static uint32_t topt_all(void)                          /* the view's lines */
{
    return topt_v ? top.nr : top.nc;
}
static uint32_t topt_find(const char *nm, cell_t *c)    /* the line named nm (its index; topt_all() none) */
{
    uint32_t r;
    for (r = 0; r < topt_all(); r++) {
        topt_line(r, c);
        if (!strcmp(c->label, nm))
            return r;
    }
    return topt_all();
}
#if DUAL_TIMES
/* n blocks: PRE OWN WAIT POST %, then the jobs' %; c1 the jobs CPU1 ran, ran the jobs run; a frame every 16 blocks */
static void topt_blocks(uint32_t n, const uint32_t *pct, uint32_t c1, uint32_t ran)
{
    uint32_t b, p, t, s1 = 0;
    for (p = 0; p < DUAL_TM_NJ; p++)
        s1 += ((c1 >> p) & 1u) ? pct[4u + p] : 0u;
    for (b = 0; b < n; b++) {
        t = fm1_ticks();
        for (p = 0; p < DUAL_TM_NJ; p++)
            if ((ran >> p) & 1u)                        /* (the jobs run: their times, as dual_run writes them) */
                dual_tm.jt[p] = TOPT_P(pct[4u + p]);
        dual_tm.c1_last = TOPT_P(s1);
        dual_tm_block(t, t + TOPT_P(pct[0]), t + TOPT_P(pct[0] + pct[1]), t + TOPT_P(pct[0] + pct[1] + pct[2]),
                      t + TOPT_P(pct[0] + pct[1] + pct[2] + pct[3]), c1, ran, fm1_ms);
        if (b % 16u == 15u)
            frame();
    }
}
#endif
#if PART_DROPS
#define TOPT_FREE arn_room()
#else
#define TOPT_FREE stk_sram_free()
#endif
static void top_tests(void)
{
    /* PRE 8, OWN 30, WAIT 2, POST 10; T1 12, T2 21 (CPU1), T3 1, DRUMS 16 (CPU1), REV 8 */
    static const uint32_t A[4u + DUAL_TM_NJ] = {8, 30, 2, 10, 12, 21, 1, 16, 8, 0};
    uint32_t r, k, n, ok = 1, row;
    cell_t c;
    char nm[12], b[16];
    reset_ui();
    memset(&dual_tm, 0, sizeof dual_tm);
    op_enter(SCR_SYSTEM);
    for (row = 0; row < SCREENS[SCR_SYSTEM].rows(); row++) {
        uint32_t e = gl_ent(1, row);
        if (sys_item(e >> 2, e & 3u) == SI_TOP)
            break;
    }
    ui.row[SCR_SYSTEM] = (uint8_t)row;
    ui.msg_t = 0;
    frame();
    SCREENS[SCR_SYSTEM].name(row ? row - 1u : 0u, nm);
    check(row < SCREENS[SCR_SYSTEM].rows() && gl_info() == 1u + INFO_PG_TOP && !strcmp(nm, FELUCCA_SYS_DEBUG ? "CPU TIMES" : "INFO"),
          "TOP: a SYSTEM page of its own after INFO (CPU TIMES with the debug pages), drawn whole");
#if DUAL_TIMES
    topt_v = 0;
    topt_blocks(64, A, (1u << 1) | (1u << NPART), (1u << (NPART + 2u)) - 1u);
    frames(40);                                         /* (the figures taken again: every 0.5 s) */
    top_cell(0, &c);
    ok = top.view == 0u && !strcmp(c.label, "CPU") && !strcmp(c.val, "AVG MAX");
    check(ok, "TOP: CPU is the view at first, its header names the columns AVG MAX");
    topt_line(0, &c);
    ok = !strcmp(c.label, "CPU0") && !strcmp(c.val, " 50  50");
    topt_line(1, &c);
    ok &= !strcmp(c.label, "CPU1") && !strcmp(c.val, " 37  37");
    check(ok, "TOP: CPU0 (PRE + JOBS + WAIT + POST: 50 %) and CPU1 (its jobs: 37 %), AVG MAX");
    {
        static const char *const ORD[] = {"T2", "DRUMS", "T1", "POST", "PRE", "REV", "T3"};
        static const char *const V[] = {" 21  21", " 16  16", " 12  12", " 10  10", "  8   8", "  8   8", "  1   1"};
        for (k = 0, ok = top.nc == 9u; k < 7u; k++) {
            topt_line(2u + k, &c);
            ok &= !strncmp(c.label, ORD[k], str_len(ORD[k])) && !strcmp(c.val, V[k]);
            if (!ok)
                printf("  top: line %u %s %s\n", 2u + k, c.label, c.val);
        }
        check(ok, "TOP: the jobs by their average, largest first, (T2 and the drums run on CPU1, shown by CPU1's share)");
    }
    topt_line(2, &c);
    str_cpy(b, "T2 ", sizeof b);
    str_app(b, trk_eng_name(&trk[1]), sizeof b);
    check(!strncmp(c.label, b, 11), "TOP: a part's line names its engine (\"T2 ANALOG\")");
    check(topt_find("BT SBC", &c) == topt_all(), "TOP: a job that did not run (Bluetooth's SBC frames): no line");
    for (k = 0; k < 64u; k++) {                         /* (an SBC frame encoded after each block: the next block takes it) */
        DUAL_TM_XJOB(NPART + 2u, TOPT_P(3));
        topt_blocks(1, A, (1u << 1) | (1u << NPART), (1u << (NPART + 2u)) - 1u);
    }
    frames(40);
    r = topt_find("BT SBC", &c);
    check(r < topt_all() && !strcmp(c.val, "  3   3") && r == 2u + 6u,
          "TOP: Bluetooth's SBC timed after the block: its line (3 %, CPU0), in its place by average (before T3)");
#endif
    /* the RAM view: its header SRAM used / total, the owners by size, OTHER last */
    topt_v = 1;
    snprintf(b, sizeof b, "%u/%u KB", (unsigned)(top.used / 1024u), (unsigned)(INFO_SRAM / 1024u));
    top.view = 1;
    top_cell(0, &c);
    check(!strcmp(c.label, "RAM") && !strcmp(c.val, b) && top.used == INFO_SRAM - TOPT_FREE,
          "TOP: the RAM view's header: SRAM used / total KB, the SRAM to the stacks, what is not free (the arena's room; without one, the link's)");
    top.view = 0;
    k = topt_find("RAM CODE", &c);
    ok = k < topt_all() && !strcmp(c.val, "12");
    k = topt_find("BT AUDIO", &c);
    ok &= k < topt_all() && !strcmp(c.val, "1");
    topt_line(topt_all() - 1u, &c);
    check(ok && !strcmp(c.label, "OTHER") && !strcmp(c.val, "1"),
          "TOP: the build's table (tests/top_ram_test.h: RAM CODE 12 KB, BT AUDIO 1 KB), OTHER last");
    for (k = 1, ok = 1; k + 1u < top.nr; k++)
        ok &= top.rb[k - 1u] >= top.rb[k];
    check(ok && top.nr >= 13u, "TOP: the owners sorted by size, largest first");
    for (k = 0, ok = 1; k < top.nr; k++) {              /* (no two rows alike: the arena's lent blocks against the build's modules) */
        uint32_t j;
        char l1[16];
        cell_t d;
        topt_line(k, &c);
        str_cpy(l1, c.label, sizeof l1);                /* (the label is top_cell's one buffer) */
        for (j = k + 1u; j < top.nr; j++) {
            topt_line(j, &d);
            if (!strcmp(l1, d.label)) {
                printf("  top: two rows named %s\n", l1);
                ok = 0;
            }
        }
    }
    check(ok, "TOP: every row of the RAM view has its own name");
#if PART_DROPS && FXM_NEED                              /* (the arena: run_tests.sh's UI_TOP build) */
    for (k = 0, ok = 1, n = 0; k < ARN_PARTS; k++) {    /* every arena block now: a line of its KB */
        uint32_t i, f = 0;
        if (!arn_cur[k].len)
            continue;
        n++;
        for (i = 0; i < top.nr; i++)
            f |= top.rk[i] == k && top.rb[i] == arn_cur[k].len;
        ok &= f;
    }
    check(ok, "TOP: every block of the arena now has its line, its size");
    for (k = 0, ok = 1; k < ARN_PARTS; k++) {           /* no arena client is named as a module of the build's table, nor as the undo's row */
        uint32_t j;
        char an[12];
        top_arn_name(k, an);
        ok &= strcmp(an, "UNDO") != 0;
        for (j = 0; j < TOP_RAM_N; j++)
            ok &= !TOP_RAM[j].nm[0] || strcmp(an, TOP_RAM[j].nm) != 0;
        if (!ok)
            printf("  top: arena client %u is named %s\n", k, an);
    }
    check(ok, "TOP: no arena block carries the name of a module of the build's table (BT AUDIO, WI-FI: their lent buffers are BUF)");
    k = NPART + FXM_CHO;
    ok = !arn_cur[k].len && !arn_reserve(k, 40u * 1024u) && arn_take(k);
    frames(40);
    k = topt_find("CHO", &c);
    ok &= k < topt_all() && !strcmp(c.val, "40");
    ok &= k < top.nr && top.rb[k] == 40u * 1024u && (!k || top.rb[k - 1u] >= 40u * 1024u) &&
          (k + 1u >= top.nr || top.rb[k + 1u] <= 40u * 1024u);
    check(ok, "TOP: a block placed in the arena (the chorus line, 40 KB): its line within 0.5 s, in its place by size");
    arn_give(NPART + FXM_CHO);
    frames(40);
    check(topt_find("CHO", &c) == topt_all(), "TOP: given back: its line gone");
#endif
    topt_v = 0;
    /* KNOB 1: CPU <-> RAM; KNOB 2: the lines; SELECT, ALGORITHM: the pages */
    ui.row[SCR_SYSTEM] = (uint8_t)row;
    frames(70);                                         /* (the page shown again after a second away: from its first line) */
    ok = top.view == 0u && top.off == 0u;
    turn(EN_K1, 1);
    ok &= top.view == 1u && top.off == 0u && ui.row[SCR_SYSTEM] == row;
    frames(2);
    top_cell(0, &c);
    ok &= !strcmp(c.label, "RAM");
    turn(EN_K1, -1);
    ok &= top.view == 0u;
    top_cell(0, &c);
    ok &= !strcmp(c.label, "CPU");
    check(ok, "TOP: KNOB 1 switches the view, CPU then RAM then CPU, from the first line, the page kept");
    turn(EN_K1, 1);
    n = top_all();
    ok = top.off == 0u && n > TOP_VIS;
    turn(EN_K2, 1);
    ok &= top.off == 1u && ui.row[SCR_SYSTEM] == row;
    for (k = 1; k < n - TOP_VIS; k++)
        turn(EN_K2, 1);
    ok &= top.off == n - TOP_VIS && ui.row[SCR_SYSTEM] == row;
    turn(EN_K2, 5);
    ok &= top.off == n - TOP_VIS && ui.row[SCR_SYSTEM] == row;
    check(ok, "TOP: KNOB 2 scrolls the lines a detent each, to the last line, clamped there: the page stays");
    for (k = 0; k < n - TOP_VIS; k++)
        turn(EN_K2, -1);
    ok = top.off == 0u && ui.row[SCR_SYSTEM] == row;
    turn(EN_K2, -3);
    ok &= top.off == 0u && ui.row[SCR_SYSTEM] == row;
    turn(EN_K2, 2);
    ok &= top.off == 2u;
    turn(EN_K1, 1);
    ok &= top.off == 0u && top.view == 0u;
    check(ok, "TOP: ... and back to the first line, clamped there; KNOB 1 starts the other view from its first line");
    turn(EN_ALGO, 1);
    SCREENS[SCR_SYSTEM].name(ui.row[SCR_SYSTEM], nm);
    ok = ui.row[SCR_SYSTEM] != row && strcmp(nm, "TOP");
    turn(EN_ALGO, -1);
    ok &= ui.row[SCR_SYSTEM] == row;
    turn(EN_SELECT, 1);
    SCREENS[SCR_SYSTEM].name(ui.row[SCR_SYSTEM], nm);
    ok &= ui.row[SCR_SYSTEM] != row && strcmp(nm, "TOP") && top.off == 0u;
    turn(EN_SELECT, -1);
    ok &= ui.row[SCR_SYSTEM] == row && top.off == 0u;
    turn(EN_SELECT, -1);
    ok &= ui.row[SCR_SYSTEM] != row;
    check(ok, "TOP: ALGORITHM and SELECT go to the next / previous page as on every SYSTEM page, they never scroll the lines");
    ui.row[SCR_SYSTEM] = (uint8_t)row;
    frames(70);
#if DUAL_TIMES
    topt_blocks(64, A, (1u << 1) | (1u << NPART), (1u << (NPART + 2u)) - 1u);
#endif
    frames(40);
    ui.force = 1;
    top.view = 0;
    frame();
    ppm("opt-top");
    for (k = 0; k < top_all(); k++) {                   /* (the page as text: the report's mock) */
        topt_line(k, &c);
        printf("  top| %-14s %13s\n", c.label, c.val);
    }
    for (k = 0, ok = 1; k < info_lines(INFO_PG_TOP); k++) {   /* every line on screen: its name and a value, no unit */
        info_cell(INFO_PG_TOP, k, &c);
        ok &= c.label && c.label[0] && c.val[0] && !c.unit[0] && c.kind == CK_RO;
    }
    check(ok && info_lines(INFO_PG_TOP) == 1u + TOP_VIS, "TOP: drawn, the header and 7 lines at once, each a name and a value");
    top.view = 1;
    ui.force = 1;
    frame();
    ppm("opt-top-ram");
    top.view = 0;
    memset(&dual_tm, 0, sizeof dual_tm);
    reset_ui();
}
#elif !OP_TOP
static void top_tests(void)
{
    uint32_t r, ok = 1;
    char nm[12];
    for (r = 0; r < SCREENS[SCR_SYSTEM].rows(); r++) {
        SCREENS[SCR_SYSTEM].name(r, nm);
        ok &= strcmp(nm, "TOP") != 0;
    }
    check(ok, "SYS_TOP off: SYSTEM has no TOP page");
}
#else
static void top_tests(void) {}                          /* (TOP without the render's times or an arena: not built for the tests) */
#endif
