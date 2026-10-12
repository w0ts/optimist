/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, LFO RATE with tempo sync (P_LRATE -LSYNC_N .. 127): a free rate's gauge is the bar from 0 as
 * before; a synced value (a division) is the list form, one of the ten divisions lit; the LFO graph does not depend on
 * the rate, so -1 and -10 draw as any rate. Screens opt-lforate-free, opt-lforate-sync. */
static uint32_t lr_row(void)                            /* the SOUND row of the LFO page whose first knob is RATE */
{
    uint32_t r;
    for (r = 0; r < SCR->rows(); r++)
        if (snd_page(r) && snd_page(r)->id[0] == P_LRATE)
            return r;
    return 0xFFFFu;
}
static void lr_cell(int32_t v, cell_t *c)
{
    TSEL->p[P_LRATE] = (int16_t)v;
    ui.force = 1;
    frame();
    page_cell(snd_page(ui.row[SCR_SOUND]), 0, c);
}
static void lforate_tests(void)
{
    cell_t c;
    uint32_t r;
    int ok = 1;
    int16_t keep;
    reset_ui();
    keep = TSEL->p[P_LRATE];
    tap(B_LFO);
    r = lr_row();
    check(r != 0xFFFFu, "LFO RATE: a row for the RATE page");
    if (r == 0xFFFFu)
        return;
    ui.row[SCR_SOUND] = (uint8_t)r;
    lr_cell(0, &c);
    check(c.gk == GK_BAR && c.gmin == 0 && c.gmax == 127 && c.gv == 0, "LFO RATE 0: the bar from 0 (as before the sync)");
    lr_cell(127, &c);
    check(c.gk == GK_BAR && c.gv == 127 && c.gmin == 0, "LFO RATE 127: the bar full, still from 0");
    lr_cell(64, &c);
    check(c.gk == GK_BAR && c.gv == 64 && c.gmin == 0 && c.gmax == 127, "LFO RATE 64: the bar at 64 of 0 .. 127, not shifted by the divisions");
    lr_cell(-1, &c);
    check(c.gk == GK_DOTS && c.gmin == 0 && c.gmax == LSYNC_N - 1 && c.gv == 0 && !strcmp(c.val, "4BAR"),
          "LFO RATE 4BAR (-1): ten cells, the first lit, the division as text");
    lr_cell(-10, &c);
    check(c.gk == GK_DOTS && c.gv == LSYNC_N - 1 && !strcmp(c.val, "1/32"), "LFO RATE 1/32 (-10): the tenth lit");
    ui.force = 1;
    frame();
    ppm("opt-lforate-sync");
    for (r = 1; r <= (uint32_t)LSYNC_N; r++) {         /* every division: its own cell, the graph drawn */
        lr_cell(-(int32_t)r, &c);
        ok &= c.gk == GK_DOTS && c.gv == (int16_t)(r - 1u);
    }
    check(ok, "LFO RATE -1 .. -10: the lit cell walks 0 .. 9");
    lr_cell(60, &c);
    ui.force = 1;
    frame();
    ppm("opt-lforate-free");
    check(1, "LFO RATE -1 .. -10 and free: the LFO page and its graph drawn (the graph reads no rate)");
    TSEL->p[P_LRATE] = keep;
    reset_ui();
}
