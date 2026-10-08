/* SPDX-License-Identifier: GPL-3.0-only */
/* The Felucca 1.0.2 / 1.0.3 small options' UI (firmware/src/core/backports.h), included by ui_pages_test.c: each block
 * only with its switch on (tests/run_tests.sh builds ui_pages_test once with FEL102_ON).
 *   bpm lock  SELECT on a page: the tempo stays, BPM LOCKED; GLO + SELECT: the tempo, no GLO page (a combo)
 *   div order the divisions step and show in length order (1/4 1/8 8T 1/16 16T 1/32), stored values unchanged
 *   latch     FX + a key: its effect stays with FX let go, the FX button lit; another key switches; FX + OCT-: off
 *   mark      MOTION PLAY on with an event for ATK: ENV's ATK card is marked (VIEW ALL and VIEW PAGE), the others not;
 *             PLAY off: no mark */
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
#if FELUCCA_PUNCH_LATCH
    {   /* (the FX button's light: ui_leds, not checked here: the host panel has no LED map) */
        const page_t *pg;
        song.sel = 0; go_home(); frames(2);
        pg = cur_page();
        punch.req = -1;
        press(B_FX); frames(2);
        fm1_in.notes = 1u << 4; frame(); fm1_in.notes = 0; frame();
        release(B_FX); frames(20);
        check(punch.req == 2 && cur_page() == pg, "latch: FX + key, both let go: the effect stays (no FX page)");
        press(B_FX); frames(2);
        fm1_in.notes = 1u << 0; frame(); fm1_in.notes = 0; frame();
        check(punch.req == 0, "latch: FX + another key: its effect instead");
        edges_btn |= BT(B_OCTDN); fm1_in.buttons |= BT(B_OCTDN); frame(); fm1_in.buttons &= ~BT(B_OCTDN); frame();
        check(punch.req == -1, "latch: FX + OCT-: all off");
        release(B_FX); frames(20);
        check(punch.req == -1 && cur_page() == pg, "latch: FX let go after: off, no FX page");
    }
#endif
#if FELUCCA_MOTION && FELUCCA_MOTION_MARK
    {   /* every cell drawn without and with the motion: exactly one differs, by the mark */
        static char off[OV_ROWS * 4][36], on[OV_ROWS * 4][36];
        uint32_t c, view, n, diff, marked, guard;
        song.sel = 0; go_home(); frames(2);
        motion.count = 1; motion.ev[0].place = 2; motion.ev[0].param = P_ATK; motion.ev[0].value = 20;
        for (view = 0; view < 2u; view++) {
            settings.view = (uint8_t)view; song.g[G_VIEW] = (int16_t)view;
            for (guard = 0, open_family(FAM_ENV); cur_page()->id[0] != P_ATK && guard < 8u; guard++)
                open_family(FAM_ENV);
            n = view ? OV_ROWS * 4u : 4u;
            motion.on = 0; ui.force = 1; frame();
            for (c = 0; c < n; c++) str_cpy(off[c], view ? ov.key[c / 4u][c % 4u] : ui.col[c], 36);
            motion.on = 1; ui.force = 1; frame();
            ppm(view ? "fel102-motion-mark-all" : "fel102-motion-mark-page");
            for (c = 0; c < n; c++) str_cpy(on[c], view ? ov.key[c / 4u][c % 4u] : ui.col[c], 36);
            for (diff = marked = 0, c = 0; c < n; c++)
                if (!str_eq(on[c], off[c])) {
                    diff++;
                    marked += str_len(on[c]) == str_len(off[c]) + 1u && on[c][str_len(on[c]) - 1u] == 'M';
                }
            check(cur_page()->id[0] == P_ATK && diff == 1u && marked == 1u,
                  view ? "motion mark: VIEW ALL: the ATK card alone marked" : "motion mark: VIEW PAGE, ENV: the ATK card alone marked");
        }
        motion.on = 0; motion.count = 0; settings.view = 1; song.g[G_VIEW] = 1; go_home(); frames(2);
    }
#endif
#if FELUCCA_DIV_ORDER
    {
        const param_desc_t *d = &TP[P_SDIV], *sl = &TP[P_SLRATE];
#if FELUCCA_DIV_LONG                 /* (SLOOP 2.4's long steps first: they are the longest) */
        static const char *const want[9] = {"2BAR", "1BAR", "1/2", "1/4", "1/8", "8T", "1/16", "16T", "1/32"};
        int32_t v = 8, k, ok = 1, n = 9, first = 8;
#else
        static const char *const want[6] = {"1/4", "1/8", "8T", "1/16", "16T", "1/32"};
        int32_t v = 0, k, ok = 1, n = 6, first = 0;
#endif
        for (k = 0; k < n; k++, v = param_step(d, v, 1))
            ok &= str_eq(d->names[v], want[k]) && enum_rank(d, v) == k;
        check(ok && param_step(d, 3, 1) == 3 && param_step(d, first, -1) == first && param_step(d, 1, 2) == 2,
              "div order: DIV steps (2BAR 1BAR 1/2) 1/4 1/8 8T 1/16 16T 1/32, its gauge in that order, clamped at both ends");
        check(param_step(sl, 0, 1) == 3 && param_step(sl, 3, 1) == 1 && enum_rank(sl, 5) == 5,
              "div order: SLICER RATE 1/8 8T 1/16 16T 1/32 32T");
        check(param_step(&TP[P_LEVEL], 10, 3) == 13, "div order: other parameters step as before");
    }
#endif
}
