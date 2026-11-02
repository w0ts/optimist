/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, the level meters' colours (op_mixdraw.c mx_vu_fill; the user, 2026-10-31: "green below -18 dBFS; amber from
 * -18 to -6 dBFS; red from -6 (or -3) dBFS; a separate held clip light at 0 dBFS"): the bar's top (its rightmost lit column) is green at
 * -30 dBFS, amber at -12, red at -3, at the fixed -40 floor; a full-scale sample lights CLIP, which goes out about a second after the last. */
#define VUC_Y ((uint32_t)(OP_PY + MXM_VY + 5))
static uint16_t vuc_top(void)                           /* the colour of MASTER's bar at its rightmost lit column, 0: none (the clip box left out) */
{
    int32_t x;
    for (x = MXM_X + MXM_W - MX_CLIP_W - 2; x >= MXM_X; x--) {
        uint16_t c = swap16(screen[VUC_Y * 240u + (uint32_t)x]);
        if (c == C_OK || c == C_WARN || c == C_ERR)
            return c;
    }
    return 0u;
}
static void vuc_feed(double amp)
{
    uint32_t i;
    for (i = 0; i < VIS_RING; i++)
        vis_pcm[0][i] = vis_pcm[1][i] = (int32_t)(sin((double)i * 2.0 * 3.14159265 / 100.0) * amp);
}
static uint16_t vuc_at(double amp)                      /* a sine of amplitude amp for a few frames: the bar's top colour */
{
    reset_ui();
    ui.row[SCR_HOME] = MXR_MASTER;
    vuc_feed(amp);
    ui.force = 1;
    frames(4);
    return vuc_top();
}
static uint32_t vuc_lane(uint32_t sel, uint32_t lit)       /* lane 0 selected (sel) or not (lane 1 picked), the other lit to lit/MX_LIT: C_OK + 2 * C_ERR drawn on the screen */
{
    uint32_t l = sel ? 0u : 1u, r;
    reset_ui();
    for (r = MXR_T1; r < MXR_LANE0 + (sel ? 0u : 3u); r++)   /* (the walk into the lanes' view) */
        turn(EN_ALGO, 1);
    ui.force = 1;
    frame();
    mx.lit[l] = (uint8_t)lit;
    ui.force = 1;
    frame();
    ppm(sel ? (lit > 6u ? "vuc-lane-sel-loud" : "vuc-lane-sel-quiet") : (lit > 6u ? "vuc-lane-uns-loud" : "vuc-lane-uns-quiet"));
    r = (uint32_t)px_in(0, 20, 240, 200, C_OK) + 2u * (uint32_t)px_in(0, 20, 240, 200, C_ERR);
    return r;
}
static void vucolor_lane_tests(void)                    /* the drum lanes' meters: the same gradient as the tracks', selected or not (the user, 2026-11-01: "the peak meters in drum lanes are red") */
{
    uint32_t ok = 1u;
    ok &= vuc_lane(1u, 3u) == 1u;                       /* a quiet hit: green, selected */
    ok &= vuc_lane(0u, 3u) == 1u;                       /* ... unselected */
    ok &= (vuc_lane(1u, 12u) & 2u) != 0u;               /* a full hit: red at its top, selected */
    ok &= (vuc_lane(0u, 12u) & 2u) != 0u;               /* ... unselected */
    check(ok, "VU colours: a drum lane's meter is green when quiet and red only when loud, selected or not (never the lane's kind colour)");
    reset_ui();
}
static void vucolor_tests(void)
{
    uint32_t ok = 1u;
    ok &= vuc_at(32767.0 * 0.0316) == C_OK;             /* -30 dBFS */
    ok &= vuc_at(32767.0 * 0.2512) == C_WARN;           /* -12 dBFS */
    ok &= vuc_at(32767.0 * 0.7079) == C_ERR;            /* -3 dBFS */
    check(ok, "VU colours: -30 dBFS green, -12 amber, -3 red at the bar's top (floor -40)");
    {                                                   /* gradients: whole at the thresholds, no jump between two columns */
        int32_t k1 = mx_band_px(MX_AMBER_PK, MXM_W), k2 = mx_band_px(MX_RED_PK, MXM_W), c, step = 0, mid = 0;
        for (c = 1; c < MXM_W; c++) {
            uint16_t a = mx_vu_grad(c - 1, k1, k2), b = mx_vu_grad(c, k1, k2);
            int32_t d = abs((int32_t)(a >> 11) - (int32_t)(b >> 11)) + abs((int32_t)((a >> 5) & 63u) - (int32_t)((b >> 5) & 63u)) / 2;
            step = d > step ? d : step;
            mid += b != C_OK && b != C_WARN && b != C_ERR;
        }
        check(mx_vu_grad(k1, k1, k2) == C_WARN && mx_vu_grad(k2, k1, k2) == C_ERR && mx_vu_grad(k1 - (k2 - k1) / 2 - 1, k1, k2) == C_OK
              && mid > 4 && step <= 4, "VU colours: green blends into amber under -18 dBFS and amber into red under -6, whole from each threshold");
    }
    reset_ui();
    ui.row[SCR_HOME] = MXR_MASTER;
    vuc_feed(1036.0);
    ui.force = 1;
    frames(4);
    check(!mxd.clip[MXR_MASTER], "CLIP: dark with a quiet signal");
    vuc_feed(200000.0);                                 /* (the master's soft knee reaches 32767 only for a hot input) */
    frames(2);
    check(mxd.clip[MXR_MASTER] && px_in((uint32_t)(MXM_X + MXM_W - MX_CLIP_W), VUC_Y, MX_CLIP_W, 1, C_ERR), "CLIP: a full-scale sample lights it");
    vuc_feed(1036.0);
    frames(MX_CLIP_HOLD - 10u);
    check(mxd.clip[MXR_MASTER], "CLIP: held for about a second after the last full-scale sample");
    frames(20);
    check(!mxd.clip[MXR_MASTER] && !px_in((uint32_t)(MXM_X + MXM_W - MX_CLIP_W), VUC_Y, MX_CLIP_W, 1, C_ERR), "CLIP: out after the hold");
    reset_ui();
    vucolor_lane_tests();
}
