/* SPDX-License-Identifier: GPL-3.0-only */
/* GATER on the SLOOP UI (2026-10-14, feat/gater-ui; fx/insfx/gater.c): the shared PAGES' GATER page (FX family, after
 * INSERTS and CMP): DEPTH PAT RATE SHAPE, shown only while one of the track's slots holds the GATER (the drum track's
 * too), the pattern's name, RATE's division icon, the picture and its playhead, the help lines. Included by
 * tests/ui_pages_test.c. */
#if FELUCCA_INS_GATER
static uint32_t sg_page(void)                           /* the index of the GATER page in PAGES */
{
    uint32_t i;
    for (i = 0; i < NPAGES; i++)
        if (!strcmp(PAGES[i].title, "GATER"))
            return i;
    return NPAGES;
}
static uint32_t sg_walk(void)                           /* tapping FX (past the first page): GATER among the pages of the family */
{
    uint32_t k, got = 0;
    open_family(FAM_FX);
    for (k = 0; k < 24u; k++) {
        got |= !strcmp(cur_page()->title, "GATER");
        tap(B_FX);
    }
    return got;
}
static int32_t sg_white_x(void)                         /* the playhead: the leftmost white pixel of the strip under the steps */
{
    uint32_t x, y, i;
    for (x = 0; x < 228u; x++)                          /* (12 white px in a row, in the picture's band: the readouts are narrower) */
        for (y = 60u; y < 200u; y++) {
            for (i = 0; i < 12u && screen[y * 240u + x + i] == swap16(C_WHITE); i++)
                ;
            if (i == 12u)
                return (int32_t)x;
        }
    return -1;
}
static void gater_sloop_tests(void)
{
    uint32_t pg = sg_page(), k, ok;
    int16_t *vp;
    const param_desc_t *d;
    song.sel = 0; song.playing = 0; song.rec = 0; transport_req = 0; ui.msg_t = 0; ui.menu = 0; settings.view = 0;
    song.g[G_VIEW] = 0;
    ins_defaults();
    check(pg < NPAGES && PAGES[pg].fam == FAM_FX && PAGES[pg].graph == GR_GATE && PAGES[pg].scope == SC_TRACK,
          "SLOOP UI: PAGES has a GATER page in the FX family, with its graph");
    if (pg >= NPAGES)
        return;
    check(PAGES[pg].id[0] == P_TGATE && PAGES[pg].id[1] == P_TGPAT && PAGES[pg].id[2] == P_TGRATE && PAGES[pg].id[3] == P_TGSHP,
          "... knobs DEPTH PAT RATE SHAPE: P_TGATE P_TGPAT P_TGRATE P_TGSHP");
    check(!page_shown(&PAGES[pg]) && !sg_walk(), "GATER: not shown while no slot of T1 holds it (the FX button never lands on it)");
    song.sel = TRK_DRUM;
    check(!page_shown(&PAGES[pg]), "... nor the drum track's");
    song.sel = 0;
    ins_load(0, 0, FXT_GATE);
    check(page_shown(&PAGES[pg]) && sg_walk(), "T1's S1 holds the GATER: shown, the FX button reaches it");
    d = page_desc(&PAGES[pg], 0, &vp);
    check(d && !strcmp(d->label, "DEPTH") && vp == &trk[0].p[P_TGATE], "KNOB 1: DEPTH, T1's P_TGATE");
    d = page_desc(&PAGES[pg], 1, &vp);
    check(d == &TP[P_TGPAT] && vp == &trk[0].p[P_TGPAT], "KNOB 2: PAT");
    d = page_desc(&PAGES[pg], 2, &vp);
    check(d == &TP[P_TGRATE] && vp == &trk[0].p[P_TGRATE], "KNOB 3: RATE");
    d = page_desc(&PAGES[pg], 3, &vp);
    check(d == &TP[P_TGSHP] && vp == &trk[0].p[P_TGSHP], "KNOB 4: SHAPE");
    {
        static const char *const N[16] = {"16TH", "8TH", "4TH", "HALF", "OFF8", "OFF16", "DOT8", "DOT4",
                                          "TRNC1", "TRNC2", "TRNC3", "TRNC4", "GALOP", "RVGAL", "CLAVE", "TRESL"};
        char v[16];
        const char *u;
        for (k = 0, ok = 1; k < 16u; k++) {
            param_format(&TP[P_TGPAT], (int32_t)k, v, &u);
            ok &= !strcmp(v, N[k]);
        }
        check(ok, "PAT: the 16 pattern names");
        param_format(&TP[P_TGRATE], 1, v, &u);
        check(!strcmp(v, "1/16") && param_icon(&TP[P_TGRATE], 1) == ICON_DIVISION, "RATE: a division, the division icon");
        d = page_desc(&PAGES[pg], 0, &vp);
        check(param_icon(d, 0) != ICON_GENERIC && param_icon(&TP[P_TGSHP], 0) == ICON_SHAPE && param_icon(&TP[P_TGATE], 0) == ICON_GATE,
              "icons: DEPTH, SHAPE, GATER (INSERTS' card) have their own");
    }
    /* the picture */
    trk[0].p[P_TGATE] = 100;
    trk[0].p[P_TGPAT] = 8;
    trk[0].p[P_TGSHP] = 20;
    open_family(FAM_FX);
    while (strcmp(cur_page()->title, "GATER"))
        tap(B_FX);
    ifx_gate[0].ph = 5u << 28;
    ui.hot_t = 0;
    ui.force = 1;
    frames(2);
    ppm("sloop-gater");
    check(sg_white_x() == 8 + 5 * (int32_t)IGT_PW, "GATER page: the picture, the playhead under step 5");
    ifx_gate[0].ph = 9u << 28;
    frames(2);
    check(sg_white_x() == 8 + 9 * (int32_t)IGT_PW, "... the gate on step 9: the playhead moved (no forced draw)");
    trk[0].p[P_TGATE] = 0;
    ui.force = 1;
    frames(2);
    check(sg_white_x() < 0, "... DEPTH 0 (off): no playhead");
    /* the drum track */
    song.sel = TRK_DRUM;
    ins_load(TRK_DRUM, 1, FXT_GATE);
    check(page_shown(&PAGES[pg]), "the drum track's S2 holds the GATER: the page is shown");
    d = page_desc(&PAGES[pg], 0, &vp);
    check(d && vp == &TDRUM->p[P_TGATE], "... DEPTH the drum bus's");
#if FELUCCA_PARAM_HELP
    {
        static const char *const W[4] = {"DEPTH", "PAT", "RATE", "SHAPE"};
        for (k = 0, ok = 1; k < 4u; k++) {
            d = page_desc(&PAGES[pg], k, &vp);
            ok &= d && ph_find(PH_PAGE, "GATER", d->label) != 0u && !strcmp(d->label, W[k]);
        }
        check(ok, "help: DEPTH, PAT, RATE and SHAPE have their lines");
    }
#endif
    song.sel = 0;
    ins_defaults();
    trk[0].p[P_TGATE] = 0;
    ifx_gate[0].ph = 0;
    go_home();
}
#endif
