/* SPDX-License-Identifier: GPL-3.0-only */
/* The SLOOP 2.4 fixes' UI (isod89/sloop-fm1 v2.4, 8d3823f), included by ui_pages_test.c:
 *   #102  a knob's detent counted while a layer has the knobs (between the layer's read and the page's, in one UI
 *         pass) never reaches the page under it: as the layer is let go, and with PLAY pressed in a layer
 *         (encs_late: the detent the encoder ISR counts after the first read of a pass)
 *   units a filter cutoff of 10 kHz and up shows its unit "kHz" (whole kHz), a level its "dB" at every value (the card's
 *         value and unit share 54 pixels: "12.5" left no room for the unit) */
static int sl24_has_unit(uint32_t c, const char *u)   /* the card's cache key: label|value|unit, then flags */
{
    char want[8];
    uint32_t i, n;
    want[0] = '|';
    str_cpy(want + 1, u, sizeof want - 1);
    n = str_len(want);
    for (i = 0; ui.col[c][i]; i++)
        if (!memcmp(ui.col[c] + i, want, n) && (ui.col[c][i + n] < 'a' || ui.col[c][i + n] > 'z'))
            return 1;
    return 0;
}
static int sl24_card_unit(const param_desc_t *d, int32_t v, const char *u)   /* a card of d at v: its unit whole? */
{
    char val[12];
    const char *unit = "";
    param_format(d, v, val, &unit);
    ui.force = 1;
    draw_column(2, d->label, val, unit, C_WHITE, 500, ICON_AUTO);
    if (!sl24_has_unit(2, u))
        printf("ui: units: %s at %d: the card %s\n", d->label, (int)v, ui.col[2]);
    return sl24_has_unit(2, u);
}
static void sl24_units_tests(void)
{
    static const param_desc_t CUT = {"CUT", F_CUTOFF, 0, 127, 90, 0, 0};
    uint32_t v, ok = 1;
    for (v = 0; v < 128u; v++)                        /* every cutoff: Hz below 1 kHz, kHz from there up */
        ok &= sl24_card_unit(&CUT, (int32_t)v, CUTOFF_HZ[v] < 1000u ? "Hz" : "kHz");
    check(ok, "units: a cutoff card shows its unit at every value (10.2 .. 16 kHz too: a whole kHz)");
    for (ok = 1, v = 1; v < 128u; v++)                /* every level: dB (-55.5 .. +6) */
        ok &= sl24_card_unit(&TP[P_LEVEL], (int32_t)v, "dB");
    check(ok, "units: a LEVEL card shows dB at every value (-55.5 dB too)");
    ui.force = 1; frame();
}
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
    sl24_units_tests();
    go_home(); frames(2);
}
