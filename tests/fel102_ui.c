/* SPDX-License-Identifier: GPL-3.0-only */
/* The Felucca 1.0.2 / 1.0.3 small options' UI (firmware/src/backports.h), included by ui_pages_test.c: each block
 * only with its switch on (tests/run_tests.sh builds ui_pages_test once with FEL102_ON).
 *   bpm lock  SELECT on a page: the tempo stays, BPM LOCKED; GLO + SELECT: the tempo, no GLO page (a combo) */
static void fel102_ui_tests(void)
{
#if FELUCCA_BPM_LOCK
    {
        int16_t b0;
        song.sel = 0; go_home(); frames(20);
        b0 = song.g[G_BPM];
        encs[panel.enc[EN_SELECT]] = 3; frame();
        check(song.g[G_BPM] == b0 && str_eq(ui.msg, "BPM LOCKED"), "bpm lock: SELECT on a page: the tempo stays, BPM LOCKED");
        press(B_GLO); frame();
        encs[panel.enc[EN_SELECT]] = 2; frame();
        release(B_GLO); frames(2);
        check(song.g[G_BPM] > b0 && cur_page()->scope == SC_TRK, "bpm lock: GLO + SELECT: the tempo, no GLO page (a combo)");
        open_family(FAM_GLO); frames(2);
        b0 = song.g[G_BPM];
        encs[panel.enc[EN_SELECT]] = 1; frame();
        check(song.g[G_BPM] == b0, "bpm lock: SELECT on the GLO pages too: stays");
        song.g[G_BPM] = 120; go_home(); frames(2);
    }
#endif
}
