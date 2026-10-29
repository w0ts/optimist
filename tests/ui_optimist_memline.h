/* The memory line (op_draw.c memline_draw; the user, 2026-10-29): the topmost 2 rows of every page, filled left to right by the
 * arena's fill (op_project.c info_ram_pct, INFO's RAM bar), dim below 80 %, red from 80 %, the rest dark, redrawn only when the
 * pixel width or the colour changes. Without an arena the fixed SRAM's use at link (stk_host_free here) */
#if PART_DROPS
static arn_blk_t ml_save_cur[ARN_PARTS];
static uint8_t ml_save_next[ARN_PARTS];
#endif
static void memline_set(uint32_t pct)                   /* the fill: pct percent of the total, exactly (its floor) */
{
    uint32_t used, total, n;
    ram_fill(&used, &total, &n);
    used = (total * pct + 99u) / 100u;                   /* (the least that gives pct: the percentage is a floor) */
#if PART_DROPS
    memset(arn_cur, 0, sizeof arn_cur);
    memset(arn_has_next, 0, sizeof arn_has_next);
    arn_cur[0].len = used;                               /* (one block as big as the use: arn_room counts it) */
#else
    stk_host_free = total - used;
#endif
}
static int memline_row_ok(uint32_t px, uint16_t on)       /* both rows: on for the first px, dark after, in every column */
{
    uint32_t x, y;
    for (y = 0; y < 2u; y++)
        for (x = 0; x < 240u; x++)
            if (!px_at(x, y, x < px ? on : C_LINE))
                return 0;
    return 1;
}
static void memline_tests(void)
{
    static const uint32_t PAGES[] = {SCR_HOME, SCR_STEP, SCR_SYSTEM, SCR_SCOPE};
    uint32_t i;
#if PART_DROPS
    memcpy(ml_save_cur, arn_cur, sizeof arn_cur);
    memcpy(ml_save_next, arn_has_next, sizeof arn_has_next);
#endif
    reset_ui();
    stk_boot();
    check(memline_px(0) == 0 && memline_px(50) == 120 && memline_px(100) == 240, "memory line: 0 / 50 / 100 % are 0 / 120 / 240 px");
    memline_set(79);
    check(info_ram_pct() == 79, "memory line: the test's 79 % is the fill INFO's RAM bar reads");
    for (i = 0; i < sizeof PAGES / sizeof PAGES[0]; i++) {
        op_enter(PAGES[i]);
        memline_set(30);
        ui.force = 1;
        frame();
        check(memline_row_ok(memline_px(30), C_DIM), "memory line: every page, rows 0-1: 30 % dim, the rest dark");
    }
    op_enter(SCR_SYSTEM);
    ui.force = 1;
    memline_set(79);
    frame();
    check(memline_row_ok(memline_px(79), C_DIM), "memory line: 79 % is dim, 190 px");
    memline_set(80);
    frame();
    check(memline_row_ok(memline_px(80), C_ERR), "memory line: 80 % is red, 192 px");
    memline_set(100);
    frame();
    check(memline_row_ok(240, C_ERR), "memory line: 100 % is red the whole width");
    memline_set(0);
    frame();
    check(memline_row_ok(0, C_DIM), "memory line: empty is dark the whole width");
    memline_set(50);
    frame();
    {   /* redrawn only when the pixel width or colour changes: a stray pixel in the line stays while the fill does not move */
        screen[1 * 240 + 239] = swap16(C_WHITE);
        frames(3);
        check(px_at(239, 1, C_WHITE), "memory line: at rest it is not redrawn (a stray pixel stays)");
        memline_set(51);                                 /* (122 px: 1 % = 2.4 px, the width changes) */
        frame();
        check(!px_at(239, 1, C_WHITE) && memline_row_ok(memline_px(51), C_DIM), "memory line: the fill moved: redrawn");
    }
    reset_ui();
#if PART_DROPS
    memcpy(arn_cur, ml_save_cur, sizeof arn_cur);
    memcpy(arn_has_next, ml_save_next, sizeof arn_has_next);
#endif
    stk_host_free = 0;
}
