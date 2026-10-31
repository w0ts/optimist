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
static void vucolor_tests(void)
{
    uint32_t ok = 1u;
    ok &= vuc_at(32767.0 * 0.0316) == C_OK;             /* -30 dBFS */
    ok &= vuc_at(32767.0 * 0.2512) == C_WARN;           /* -12 dBFS */
    ok &= vuc_at(32767.0 * 0.7079) == C_ERR;            /* -3 dBFS */
    check(ok, "VU colours: -30 dBFS green, -12 amber, -3 red at the bar's top (floor -40)");
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
}
