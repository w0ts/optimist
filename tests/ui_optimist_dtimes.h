/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, SYSTEM > CPU TIMES (op_project.c sys_dt_cell, system/dual_load.h dual_tm; DUAL_LOAD builds
 * with the debug pages): the render's times fed as the render would (dual_tm_block), three-digit values included; the
 * table's rows: the names' row, a row a job, PRE JOBS WAIT POST CPU0 CPU1, each "AVG MAX WST C" in fixed columns (13
 * characters of the monospace face, right-aligned: the columns line up), the worst block's values, the core of each
 * job; drawn, nothing between the names and the columns (the user, 2026-10-30: "12 21 1CPU 1 0 0" ran together).
 * Included after the helpers (frame, ppm, px_in). */
#if OP_DTIMES
#define DTT_P(k) (DUAL_TM_BLK * (k) / 100u)            /* k % of a block, ticks */
static void dtt_blocks(uint32_t n, const uint32_t *pct, uint32_t c1, uint32_t ran)   /* n blocks: PRE OWN WAIT POST %, then the jobs' % */
{
    uint32_t b, p, t;
    for (b = 0; b < n; b++) {
        t = fm1_ticks();
        for (p = 0; p < DUAL_TM_NJ; p++)
            dual_tm.jt[p] = DTT_P(pct[4u + p]);
        dual_tm.c1_last = DTT_P(pct[4u + 1u] + pct[4u + NPART]);   /* (CPU1: T2 and the drums) */
        dual_tm_block(t, t + DTT_P(pct[0]), t + DTT_P(pct[0] + pct[1]), t + DTT_P(pct[0] + pct[1] + pct[2]),
                      t + DTT_P(pct[0] + pct[1] + pct[2] + pct[3]), c1, ran, fm1_ms);
    }
}
static uint32_t dtt_row(const char *nm)                 /* the page's row with this name, INFO_DTN none */
{
    uint32_t k;
    cell_t c;
    for (k = 0; k < INFO_DTN; k++) {
        info_cell(INFO_PG_DUAL, k, &c);
        if (!strcmp(c.label, nm))
            return k;
    }
    return INFO_DTN;
}
static void dtimes_tests(void)
{
    /* PRE 8, JOBS 30, WAIT 2, POST 12; T1 12, T2 21 (CPU1), T3 1, DR 16 (CPU1), REV 8 */
    static const uint32_t A[4u + DUAL_TM_NJ] = {8, 30, 2, 12, 12, 21, 1, 16, 8, 0};
    static const uint32_t S[4u + DUAL_TM_NJ] = {40, 140, 20, 50, 100, 21, 125, 16, 8, 0};   /* (a block of 250 %: T1, T3 slow) */
    uint32_t r, k, n, ok = 1, c1 = (1u << 1) | (1u << NPART), ran = (1u << (NPART + 2u)) - 1u, lmax = 0, vmin = 240;
    cell_t c;
    char b[20], v[16];
    reset_ui();
    memset(&dual_tm, 0, sizeof dual_tm);
    op_enter(SCR_SYSTEM);
    for (r = 0; r < SCREENS[SCR_SYSTEM].rows(); r++) {
        uint32_t e = gl_ent(1, r);
        if (sys_item(e >> 2, e & 3u) == SI_DUALT)
            break;
    }
    ui.row[SCR_SYSTEM] = (uint8_t)r;
    ui.msg_t = 0;
    frame();                                            /* (the page opened: a new worst block asked for) */
    check(r < SCREENS[SCR_SYSTEM].rows() && gl_info() == 1u + INFO_PG_DUAL, "CPU TIMES: a SYSTEM page of its own, drawn whole");
    dtt_blocks(200, A, c1, ran);
    dtt_blocks(1, S, c1, ran);
    dtt_blocks(8, A, c1, ran);
    ui.force = 1;
    frame();
    ppm("opt-dual-times");
    for (k = 0; k < INFO_DTN; k++) {                    /* fixed columns: 13 characters, right-aligned, so they line up */
        info_cell(INFO_PG_DUAL, k, &c);
        n = (uint32_t)text_ws(nm_card(c.label, 0, b, sizeof b));
        cut(v, c.val, 13);
        ok &= str_len(c.val) == 13u && !c.unit[0] && !strcmp(v, c.val);
        lmax = 6u + n > lmax ? 6u + n : lmax;
        vmin = 236u - (uint32_t)text_ws(c.val) < vmin ? 236u - (uint32_t)text_ws(c.val) : vmin;
    }
    check(ok, "CPU TIMES: every row's columns 13 characters (AVG MAX WST C), no unit: they line up under the names' row");
    check(lmax + 8u <= vmin, "CPU TIMES: a character's room at least between the longest name and the columns");
    {   /* drawn: nothing but black in the gap between the names and the columns, over the whole band */
        uint32_t x, y, lit = 0;
        for (y = OH_HEAD; y < OP_FOOT_Y; y++)
            for (x = lmax + 2u; x + 2u < vmin; x++)
                lit += screen[y * 240u + x] != 0u;
        check(!lit, "CPU TIMES: drawn, the gap between the names and the columns empty (no digit runs into a value)");
    }
    info_cell(INFO_PG_DUAL, 0, &c);
    check(!strcmp(c.val, "AVG MAX WST C"), "CPU TIMES: the names' row (AVG MAX WST C)");
    k = dtt_row("T2");
    info_cell(INFO_PG_DUAL, k, &c);
    check(k < INFO_DTN && !strcmp(c.val, " 21  21  21 1"), "CPU TIMES: T2 21 % throughout, on CPU1");
    k = dtt_row("T3");
    info_cell(INFO_PG_DUAL, k, &c);
    check(k < INFO_DTN && c.val[0] == ' ' && !strncmp(c.val + 4, "125 125 0", 9),
          "CPU TIMES: T3, 125 % in the worst block: MAX and WST 125 (three digits in their column), on CPU0");
    k = dtt_row("CPU0");
    info_cell(INFO_PG_DUAL, k, &c);
    check(k < INFO_DTN && !strncmp(c.val + 4, "250 250 0", 9) && c.col == C_WARN,
          "CPU TIMES: CPU0's whole block: MAX and WST 250 (PRE + JOBS + WAIT + POST), amber over 100");
    k = dtt_row("PRE");
    info_cell(INFO_PG_DUAL, k, &c);
    check(k < INFO_DTN && !strcmp(c.val + 8, " 40 0"), "CPU TIMES: the worst block's PRE (40)");
    k = dtt_row("REV");
    info_cell(INFO_PG_DUAL, k, &c);
    check(k < INFO_DTN && !strcmp(c.val, "  8   8   8 0"), "CPU TIMES: the reverb's kept block, 8 %, on CPU0");
    dtt_blocks(1, A, c1, ran & ~(1u << 2));              /* (T3 not run in a block: still the core it ran on last) */
    k = dtt_row("T3");
    info_cell(INFO_PG_DUAL, k, &c);
    check(c.val[12] == '0', "CPU TIMES: a job not run in the last block: the core of the last block it ran in (0)");
    for (k = 0; k < 2u * 1000u / 16u + 10u; k++)        /* (2 s more of the steady blocks: the spike leaves MAX) */
        dtt_blocks(2, A, c1, ran), frame();
    k = dtt_row("CPU0");
    info_cell(INFO_PG_DUAL, k, &c);
    check(!strncmp(c.val, " 52  52 250", 11), "CPU TIMES: 2 s on, the spike gone from MAX (52), still the worst block (250)");
    memset(&dual_tm, 0, sizeof dual_tm);
    reset_ui();
}
#endif
