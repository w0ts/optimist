/* SPDX-License-Identifier: GPL-3.0-only */
/* The Felucca 1.0.2 / 1.0.3 small options' UI (firmware/src/backports.h), included by ui_pages_test.c: each block
 * only with its switch on (tests/run_tests.sh builds ui_pages_test once with FEL102_ON).
 *   bpm lock  SELECT on a page: the tempo stays, BPM LOCKED; GLO + SELECT: the tempo, no GLO page (a combo)
 *   div order the divisions step and show in length order (1/4 1/8 8T 1/16 16T 1/32), stored values unchanged */
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
#if FELUCCA_DIV_ORDER
    {
        const param_desc_t *d = &TP[P_SDIV], *sl = &TP[P_SLRATE];
        static const char *const want[6] = {"1/4", "1/8", "8T", "1/16", "16T", "1/32"};
        int32_t v = 0, k, ok = 1;
        for (k = 0; k < 6; k++, v = param_step(d, v, 1))
            ok &= str_eq(d->names[v], want[k]) && enum_rank(d, v) == k;
        check(ok && param_step(d, 3, 1) == 3 && param_step(d, 0, -1) == 0 && param_step(d, 1, 2) == 2,
              "div order: DIV steps 1/4 1/8 8T 1/16 16T 1/32, its gauge in that order, clamped at both ends");
        check(param_step(sl, 0, 1) == 3 && param_step(sl, 3, 1) == 1 && enum_rank(sl, 5) == 5,
              "div order: SLICER RATE 1/8 8T 1/16 16T 1/32 32T");
        check(param_step(&TP[P_LEVEL], 10, 3) == 13, "div order: other parameters step as before");
    }
#endif
}
