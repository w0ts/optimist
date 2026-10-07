/* SPDX-License-Identifier: GPL-3.0-only */
/* The SLOOP 2.4 fixes' UI (isod89/sloop-fm1 v2.4, 8d3823f), included by ui_pages_test.c:
 *   #102  a knob's detent counted while a layer has the knobs (between the layer's read and the page's, in one UI
 *         pass) never reaches the page under it: as the layer is let go, and with PLAY pressed in a layer
 *         (encs_late: the detent the encoder ISR counts after the first read of a pass) */
static uint32_t sl24_moved(const int16_t *p0, const int16_t *g0, uint32_t skip_g)
{
    uint32_t k, n = 0;
    for (k = 0; k < P_COUNT; k++)
        n += TSEL->p[k] != p0[k];
    for (k = 0; k < G_COUNT; k++)
        n += k != skip_g && k != G_DUST && k != G_FILT && song.g[k] != g0[k];
    return n;
}
static void sl24_ui_tests(void)
{
    int16_t p0[P_COUNT], g0[G_COUNT];
    uint32_t moved;
    /* ---- #102: PLAY pressed in a layer: the knobs stay the layer's for that pass */
    song.sel = 0; go_home(); frames(30);
    memcpy(p0, TSEL->p, sizeof p0); memcpy(g0, song.g, sizeof g0);
    press(B_FX); frames(4);
    encs_late[panel.enc[EN_K1]] = 3;                  /* a detent counted after the layer read KNOB 1 */
    edges_btn |= BT(B_PLAY); fm1_in.buttons |= BT(B_PLAY); frame();
    fm1_in.buttons &= ~BT(B_PLAY); frame();
    moved = sl24_moved(p0, g0, G_COUNT);
    if (moved)
        printf("ui: #102 PLAY in the FX layer, a late KNOB 1 detent: %u page values moved\n", moved);
    check(moved == 0, "#102: PLAY pressed in a layer: a knob's late detent does not edit the page under it");
    release(B_FX); frames(30);
    transport_req = 2; frame(); frames(2);
    memcpy(TSEL->p, p0, sizeof p0); memcpy(song.g, g0, sizeof g0);
#if FELUCCA_LAYER_QUIET
    /* ---- #102: the frame a layer is let go: its knobs dropped (#39), and none counted after that read either */
    go_home(); frames(30);
    memcpy(p0, TSEL->p, sizeof p0); memcpy(g0, song.g, sizeof g0);
    press(B_FX); frames(4);
    encs[panel.enc[EN_K2]] = 2; frame();              /* FX used (KNOB 2: DUST) */
    encs_late[panel.enc[EN_K1]] = 3;
    release(B_FX);
    moved = sl24_moved(p0, g0, G_COUNT);
    if (moved)
        printf("ui: #102 FX let go, a late KNOB 1 detent in that frame: %u page values moved\n", moved);
    check(moved == 0, "#102: a layer let go: a knob's late detent in that pass does not edit the page");
    frames(30);
    memcpy(TSEL->p, p0, sizeof p0); memcpy(song.g, g0, sizeof g0);
#endif
    memset(encs_late, 0, sizeof encs_late);
    go_home(); frames(2);
}
