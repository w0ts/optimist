/* SPDX-License-Identifier: GPL-3.0-only */
/* SLOOP 2.4 phase 5 (the UI): included by ui_pages_test.c, each block only with its switch on.
 *   sel pages  SELECT on a page goes to the previous / next page of its family (stops at the ends); the tempo on
 *              TRACKS and while a layer is held */
static void sl24p5_ui_tests(void)
{
#if FELUCCA_SEL_PAGES
    {
        int16_t b0;
        uint32_t p0;
        song.sel = 0; song.playing = 0; go_home(); frames(20);
        open_family(FAM_ENV); frames(2);
        p0 = ui.page;
        check(PAGES[p0].fam == FAM_ENV && PAGES[p0].scope != SC_FM6K, "sel pages: on the ENV page");
        b0 = song.g[G_BPM];
        encs[panel.enc[EN_SELECT]] = -3; frames(2);
        check(ui.page == p0 && song.g[G_BPM] == b0, "sel pages: SELECT left on the first page: stays (stops at the end)");
        encs[panel.enc[EN_SELECT]] = 1; frames(2);
        check(ui.page != p0 && PAGES[ui.page].fam == FAM_ENV && song.g[G_BPM] == b0, "sel pages: SELECT right: the next ENV page, the tempo untouched");
        ui.force = 1; frame(); ppm("sel-pages-env-next");
        encs[panel.enc[EN_SELECT]] = -1; frames(2);
        check(ui.page == p0, "sel pages: SELECT left: back");
        encs[panel.enc[EN_SELECT]] = 20; frames(2);
        check(PAGES[ui.page].fam == FAM_ENV && PAGES[ui.page].scope != SC_FM6K, "sel pages: SELECT far right: the last ENV page, stays in the family");
        open_family(FAM_FX); frames(2);
        encs[panel.enc[EN_SELECT]] = -5; frames(2);
        p0 = ui.page;
        encs[panel.enc[EN_SELECT]] = 1; frames(2);
        check(ui.page != p0 && PAGES[ui.page].fam == FAM_FX, "sel pages: FX: the next FX page");
        go_home(); frames(4);
        b0 = song.g[G_BPM];
        encs[panel.enc[EN_SELECT]] = 3; frames(2);
        check(song.g[G_BPM] != b0 && PAGES[ui.page].fam == FAM_TRK, "sel pages: TRACKS: SELECT is the tempo");
        open_family(FAM_ENV); frames(2);
        p0 = ui.page; b0 = song.g[G_BPM];
        press(B_FX); frames(12);
        p0 = ui.page;
        encs[panel.enc[EN_SELECT]] = 2; frames(2);
        check(song.g[G_BPM] != b0 && ui.page == p0 && ui.layer == LY_FX, "sel pages: while a layer is held: SELECT is the tempo");
        release(B_FX); frames(4);
        song.g[G_BPM] = 120; go_home(); frames(4);
    }
#endif
}
