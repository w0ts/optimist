/* SPDX-License-Identifier: GPL-3.0-only */
/* LFO RATE with tempo sync (P_LRATE -LSYNC_N .. 127) on the SLOOP UI's screens, included by tests/ui_pages_test.c: a free
 * rate's gauge fills from 0 as before; a synced value (a division) is a tick on the line, no fill, and the ICON_DIVISION
 * icon; the LFO wave preview reads no rate, so -1 and -10 draw as any. LFORATE_SHOTS=prefix: DIR/prefix-*.ppm. */
static int lr_dark(int x, int y, int w)                  /* the box's pixels all black (the gauge's fill row) */
{
    int i;
    for (i = 0; i < w; i++)
        if (swap16(screen[y * 240 + x + i]) != C_BLACK)
            return 0;
    return 1;
}
static void lr_page_of(uint32_t rate)                    /* the LFO page whose first knob is RATE (LFO's or LFO 2's) */
{
    uint32_t i;
    for (i = 0; i < NPAGES; i++)
        if (PAGES[i].scope == SC_TRACK && PAGES[i].id[0] == rate) {
            ui.page = (uint8_t)i;
            break;
        }
    ui.hot_t = 0;
    ui.force = 1;
    frames(2);
}
static void lr_page(void) { lr_page_of(P_LRATE); }
static void lr_shot(const char *name)
{
    const char *pre = getenv("LFORATE_SHOTS");
    char n[64];
    if (!pre)
        return;
    snprintf(n, sizeof n, "%s-%s", pre, name);
    ppm(n);
}
static void lforate_tests(void)
{
    int16_t keep = trk[0].p[P_LRATE];
    int32_t v, ok = 1;
    song.sel = 0; song.playing = 0; song.rec = 0; transport_req = 0; ui.msg_t = 0; ui.menu = 0; settings.view = 0;
    song.g[G_VIEW] = 0;
    go_home();
    open_family(FAM_LFO);
    lr_page();
    trk[0].p[P_LRATE] = 0;
    ui.force = 1; frames(2);
    check(gauge_ratio(&TP[P_LRATE], 0) == 0 && gauge_ratio(&TP[P_LRATE], 127) == 1000 && gauge_ratio(&TP[P_LRATE], 64) == 503,
          "lfo rate: a free rate's gauge ratio runs 0 .. 1000 from 0 (as before the sync)");
    check(lr_dark(5, Y_GAUGE, 8), "lfo rate: RATE 0: nothing filled on the gauge");
    trk[0].p[P_LRATE] = 127;
    ui.force = 1; frames(2);
    check(!lr_dark(5, Y_GAUGE, 8), "lfo rate: RATE 127: the gauge filled");
    check(param_icon(&TP[P_LRATE], 5) != ICON_DIVISION, "lfo rate: a free rate's icon is not the division's");
    for (v = 1; v <= LSYNC_N; v++) {
        trk[0].p[P_LRATE] = (int16_t)-v;
        ui.force = 1; frames(2);
        ok &= param_icon(&TP[P_LRATE], -v) == ICON_DIVISION && gauge_ratio(&TP[P_LRATE], -v) >= SYNCED_R &&
              gauge_ratio(&TP[P_LRATE], -v) <= SYNCED_R + 1000;
    }
    check(ok, "lfo rate: -1 .. -10: the icon is ICON_DIVISION, the gauge ratio the tick form");
    check(gauge_ratio(&TP[P_LRATE], -1) == SYNCED_R && gauge_ratio(&TP[P_LRATE], -LSYNC_N) == SYNCED_R + 1000,
          "lfo rate: 4BAR the tick at the left end, 1/32 at the right");
    trk[0].p[P_LRATE] = -1;
    ui.force = 1; frames(2);
    lr_shot("sync-4bar");
    trk[0].p[P_LRATE] = -LSYNC_N;
    ui.force = 1; frames(2);
    lr_shot("sync");
    check(lr_dark(5, Y_GAUGE, 8), "lfo rate: 1/32: no fill, the tick alone at the right");
    check(!lr_dark(53, Y_GAUGE, 3), "lfo rate: ... the tick drawn");
    trk[0].p[P_LRATE] = 60;
    ui.force = 1; frames(2);
    lr_shot("free");
    check(1, "lfo rate: -1, -10 and a free rate: the LFO page and its wave preview drawn");
    trk[0].p[P_LRATE] = keep;
    ui.force = 1; frames(2);
    {   /* LFO 2's RATE: the same display (is_lrate: the gauge and the division icon) */
        int16_t keep2 = trk[0].p[P_L2RATE];
        lr_page_of(P_L2RATE);
        trk[0].p[P_L2RATE] = 0;
        ui.force = 1; frames(2);
        check(lr_dark(5, Y_GAUGE, 8), "lfo 2 rate: RATE 0: nothing filled on the gauge");
        trk[0].p[P_L2RATE] = 127;
        ui.force = 1; frames(2);
        check(!lr_dark(5, Y_GAUGE, 8), "lfo 2 rate: RATE 127: the gauge filled");
        trk[0].p[P_L2RATE] = -LSYNC_N;
        ui.force = 1; frames(2);
        check(lr_dark(5, Y_GAUGE, 8) && !lr_dark(53, Y_GAUGE, 3), "lfo 2 rate: 1/32: no fill, the tick at the right");
        check(param_icon(&TP[P_L2RATE], -LSYNC_N) == ICON_DIVISION && param_icon(&TP[P_L2RATE], 5) != ICON_DIVISION,
              "lfo 2 rate: a synced value's icon is the division's, a free rate's is not");
        trk[0].p[P_L2RATE] = keep2;
        ui.force = 1; frames(2);
    }
}
