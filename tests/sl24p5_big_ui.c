/* SPDX-License-Identifier: GPL-3.0-only */
/* SLOOP 2.4 phase 5: bigger values (FELUCCA_BIGVALS), included by ui_pages_test.c. A page without a graph shows its
 * four values large in the graph strip, 2 x 2 as the knobs; the one turned is white; a page with a graph does not. */
static uint32_t big_strip_hash(void)               /* the graph strip's pixels (rows 74..197) */
{
    uint32_t h = 2166136261u, i;
    for (i = 74u * 240u; i < 198u * 240u; i++)
        h = (h ^ screen[i]) * 16777619u;
    return h;
}
static uint32_t big_white(void)                    /* white pixels in the graph strip */
{
    uint32_t n = 0, i;
    for (i = 74u * 240u; i < 198u * 240u; i++)
        n += screen[i] == (uint16_t)0xFFFFu;
    return n;
}
static void sl24p5_big_tests(void)
{
#if FELUCCA_BIGVALS
    uint32_t h0, w0;
    song.sel = 0; song.playing = 0; go_home(); frames(20);
    open_family(FAM_GLO); frames(4);
    check(PAGES[ui.page].graph == GR_NONE && big_page(&PAGES[ui.page]), "bigvals: GLOBAL has no graph: a big page");
    ui.force = 1; frames(3);
    check(ui.big_l[0][0] && ui.big_l[1][0] && ui.big_l[2][0] && ui.big_l[3][0], "bigvals: the four values are kept for the big view");
    check(ui.big_v[0][0] != 0, "bigvals: KNOB 1's value (BPM) has its large text");
    h0 = big_strip_hash(); w0 = big_white();
    encs[panel.enc[EN_K2]] = 2; frames(3);
    check(big_strip_hash() != h0, "bigvals: turning KNOB 2 changes the large values");
    check(ui.big_c[1] == C_WHITE && ui.big_c[0] != C_WHITE, "bigvals: the knob turned is white, the others not");
    check(big_white() > w0, "bigvals: ... and white pixels show in the strip");
    ui.force = 1; frames(2); ppm("bigvals-global");
    open_family(FAM_ENV); ui.page = (uint8_t)page_first(FAM_ENV); frames(4);
    ui.force = 1; frames(2);
    check(!big_page(&PAGES[ui.page]), "bigvals: ENV has a graph: not a big page");
    {   /* ENV DEST: no graph, a page of three values: the 2 x 2 grid */
        uint32_t i;
        for (i = 0; i < NPAGES; i++)
            if (PAGES[i].fam == FAM_ENV && PAGES[i].scope == SC_TRACK && PAGES[i].graph == GR_NONE) { ui.page = (uint8_t)i; break; }
        ui.force = 1; frames(4);
        check(big_page(&PAGES[ui.page]), "bigvals: ENV DEST is a big page");
        encs[panel.enc[EN_K1]] = 1; frames(3); ui.force = 1; frames(2);
        ppm("bigvals-env-dest");
    }
    go_home(); frames(4);
#endif
}
