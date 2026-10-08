/* SPDX-License-Identifier: GPL-3.0-only */
/* SCOPE (docs/UI-OPTIMIST-DESIGN.md sections 4.10, 11.5) and the mixer's master column. The user: "an osc view for
 * master, per track, and levels on the mixer with the compressor effect (a bar pushing down)".
 *   rows   the sources: MASTER, T1, T2, T3, DR; the cards the cursor source's values (a track: LEVEL PAN FX DRIVE,
 *          the mixer's; the master: FILT, the compressor's THRS and RATIO, LIMIT's GR read-out)
 *   panel  the oscilloscope of the cursor's source, trigger-stable (a rising zero crossing after a swing below,
 *          the latest that leaves a whole screen of samples), in the source's colour (the master in the palette's
 *          ink, a track in its instrument's), scaled to its peak; the sources as tabs under it
 * One ring, the visualiser's (fx.c vis_pcm, VIS_RING frames a side): the main loop sets scope_src to the cursor's
 * source and the audio ISR copies that block (the master's mix, a part's block after its inserts, the drums' part of
 * the mix). Off SCOPE the source is the master, which the mixer's master column reads for its meter.
 * Entry: the mixer's SCOPE row (YES), and HOME tapped on the mixer (HOME tapped on SCOPE goes back). */
static const page_t SCOPE_MASTER = {"MASTER", FAM_GLO, SC_GLOBAL, GR_NONE, {G_FILT, G_CTHR, G_CRAT, G_CGR}};
static const uint8_t SCOPE_TRK_ID[4] = {P_LEVEL, P_PAN, P_FXOFF, P_DIST};
static uint32_t scope_rows(void) { return 1u + NTRK; }
static void scope_name(uint32_t r, char *b) { str_cpy(b, r ? trk_tag(r - 1u) : "MASTER", 12); }
static uint16_t scope_col(uint32_t r) { return r ? trk_col(r - 1u) : C_HI; }
static const param_desc_t *scope_desc(uint32_t r, uint32_t k, int16_t **vp)
{
    uint32_t id = SCOPE_TRK_ID[k & 3u];
    *vp = 0;
    if (id == P_DIST && !FELUCCA_FX_DIST)
        return 0;
    return mix_desc(id, r - 1u, vp);                    /* (the mixer's: the drum track's own LEVEL, "-" PAN DRIVE) */
}
static void scope_cell(uint32_t r, uint32_t k, cell_t *c)
{
    int16_t *vp;
    if (!r) {
        page_cell(&SCOPE_MASTER, k, c);
        return;
    }
    cell_param(c, scope_desc(r, k, &vp), vp);
    if (!c->d) {
        cell_clear(c);
        c->label = TP[SCOPE_TRK_ID[k & 3u]].label;
        str_cpy(c->val, "-", sizeof c->val);
    }
    c->col = trk_col(r - 1u);
}
static void scope_turn(uint32_t r, uint32_t k, int32_t s, int fine)
{
    int16_t *vp;
    const param_desc_t *d;
    if (!r) {
        page_turn(&SCOPE_MASTER, k, s, fine);
        return;
    }
    d = scope_desc(r, k, &vp);
    val_turn(d, vp, k, s, fine);
}
static int scope_yes(uint32_t r, uint32_t k, uint32_t ok)
{
    int16_t *vp;
    if (!r)
        return page_yes(SCR_SCOPE, r, &SCOPE_MASTER, k, ok);
    return val_toggle(scope_desc(r, k, &vp), vp);       /* (FX: on / dry) */
}

/* ---- the source, once a frame from the main loop: the ISR reads it at the next block */
static void scope_tick(void)
{
    uint32_t s = ui.scr == SCR_SCOPE ? ui.row[SCR_SCOPE] % (1u + NTRK) : 0u;
    if (scope_src != s)
        scope_src = (uint8_t)s;
}

/* ---- the trace */
#define SC_H 141                        /* the trace (two canvases: op_draw.c cv_tall); the tabs under it */
#define SC_TAB_Y (OY_PANEL + SC_H + 1)
#define SC_TAB_H (OH_BODY - SC_H - 1)
#define SC_N 236u                       /* points across, one every SC_DEC frames: 10.7 ms */
#define SC_DEC 2u
static struct {
    uint32_t wr;                        /* vis_wr at the last trace: a silent source writes nothing */
    uint32_t shift;                     /* the scale: a sample >> shift is its height (smoothed by its peak) */
    uint8_t frame, tabs_row, flat;
    uint8_t y[SC_N];                    /* the trace as drawn: a height a point */
    uint16_t col;
} sc = {.tabs_row = 0xFF};
static int32_t sc_at(uint32_t w, uint32_t i)            /* frame i of the ring from w (its oldest): mono */
{
    uint32_t j = (w + i) & (VIS_RING - 1u);
    return (vis_pcm[0][j] >> 1) + (vis_pcm[1][j] >> 1);
}
/* the start of the trace: the latest rising zero crossing, after a swing below -thr, that leaves SC_N points */
static uint32_t sc_trigger(uint32_t w, int32_t thr)
{
    uint32_t i, last = VIS_RING - SC_N * SC_DEC, at = last, armed = 0;
    int32_t prev = sc_at(w, 0);
    for (i = 1; i <= last; i++) {
        int32_t x = sc_at(w, i);
        if (x < -thr)
            armed = 1;
        else if (armed && prev < 0 && x >= 0) {
            at = i;
            armed = 0;
        }
        prev = x;
    }
    return at;
}
static void scope_paint(void)                           /* the trace as computed (scope_trace) */
{
    uint32_t i;
    int32_t mid = SC_H / 2, y0 = sc.y[0];
    cv_rect(0, mid, 240, 1, C_LINE);                    /* the zero line, the quarters */
    cv_rect(0, mid / 2, 240, 1, col_shade(C_LINE, 5u));
    cv_rect(0, mid + mid / 2, 240, 1, col_shade(C_LINE, 5u));
    for (i = 0; i < SC_N; i++) {                        /* the wave, joined point to point */
        int32_t y = sc.y[i];
        cv_rect(2 + (int32_t)i, y < y0 ? y : y0, 1, (y < y0 ? y0 - y : y - y0) + 1, sc.col);
        y0 = y;
    }
}
static void scope_trace(void)
{
    uint32_t w = vis_wr, i, at, sh, pk = 0;
    int32_t mid = SC_H / 2;
    uint16_t col = scope_col(ui.row[SCR_SCOPE] % (1u + NTRK));
    for (i = 0; i < VIS_RING; i++) {
        int32_t x = sc_at(w, i);
        uint32_t a = (uint32_t)(x < 0 ? -x : x);
        pk = a > pk ? a : pk;
    }
    for (sh = 0; (pk >> sh) > (uint32_t)(SC_H / 2 - 4); sh++)   /* (the peak within the half height) */
        ;
    if (sh + 1u < sc.shift)
        sh = sc.shift - 1u;                             /* (a quieter wave grows one step a frame) */
    sc.shift = sh;
    at = sc_trigger(w, (int32_t)(pk >> 3));
    for (i = 0; i < SC_N; i++) {
        int32_t x = sc.flat ? 0 : sc_at(w, at + i * SC_DEC);
        sc.y[i] = (uint8_t)clamp(mid - (x >> sh), 1, SC_H - 2);
    }
    sc.col = col;
    cv_tall(OY_PANEL, SC_H, C_BLACK, scope_paint);
}
static void scope_tabs(void)                            /* the sources under the trace, the cursor's lit */
{
    uint32_t r, cur = ui.row[SCR_SCOPE] % (1u + NTRK);
    char nm[12], b[12];
    if (sc.tabs_row == cur)
        return;
    sc.tabs_row = (uint8_t)cur;
    cv_begin(240, SC_TAB_H, C_BLACK);
    for (r = 0; r <= NTRK; r++) {
        int32_t x = (int32_t)r * 48;
        scope_name(r, nm);
        if (r == cur)
            cv_rect(x + 1, 0, 46, SC_TAB_H, scope_col(r));
        cv_text(x + (48 - text_w(&FONT_S, op_case(b, nm, sizeof b))) / 2, 1, &FONT_S, b, r == cur ? C_BLACK : scope_col(r));
    }
    cv_blit(0, SC_TAB_Y);
}
static void scope_draw(void)
{
    uint32_t w = vis_wr, row = ui.row[SCR_SCOPE] % (1u + NTRK);
    if (ui.sig[2] != 0x5C0Fu + row) {                   /* entered, another source, the overlay gone: all again */
        ui.sig[2] = 0x5C0Fu + row;
        sc.tabs_row = 0xFF;
        sc.wr = w - 1u;
        sc.flat = 0;
    }
    scope_tabs();
    if (++sc.frame & 1u)                                /* (every other frame: 30 a second) */
        return;
    if (w == sc.wr) {                                   /* nothing written: a silent part, a flat line once */
        if (sc.flat)
            return;
        sc.flat = 1;
    } else {
        sc.flat = 0;
    }
    sc.wr = w;
    scope_trace();
}

/* ---- the mixer's master column: the master's meter (the ring's last frames, as heard with MASTER up) and the
 * master compressor's gain reduction as a bar pushing down from the top (LIMIT > GR: meters.c mc_gr_view, the COMP's
 * and the LIMIT's together, whole dB), 4 px a dB */
static struct { uint8_t lv, gr; } mcol = {0xFF, 0xFF};
static void master_col(uint32_t force)
{
    uint32_t i, w = vis_wr, pk = 0, h, gr = 0;
    if (force) {
        mcol.lv = mcol.gr = 0xFF;
        cv_begin(MX_COL_W, 16, C_BLACK);
        cv_text(1, 1, &FONT_S, "M", C_GRAY);
        cv_blit(MX_COL_X, MIX_Y + 2u);
    }
    for (i = VIS_RING - 736u; i < VIS_RING; i++) {      /* (the last 16 ms: a frame's worth) */
        int32_t x = vis_pcm[0][(w + i) & (VIS_RING - 1u)];
        uint32_t a = (uint32_t)(x < 0 ? -x : x);
        pk = a > pk ? a : pk;
    }
    h = meter_h(knee((int32_t)(pk > 0x7FFFFFF ? 0x7FFFFFF : pk)));
    if (mcol.lv != 0xFF && h + 2u < mcol.lv)
        h = mcol.lv - 2u;                               /* (falls 2 px a frame, as the tracks' meters) */
#if FELUCCA_MASTER_COMP
    gr = (uint32_t)clamp(-(int32_t)mc_gr_view * 4, 0, METER_H);
#endif
    if (h == mcol.lv && gr == mcol.gr)
        return;
    mcol.lv = (uint8_t)h;
    mcol.gr = (uint8_t)gr;
    cv_begin(MX_COL_W, METER_H, C_LINE);
    cv_rect(0, METER_H - (int32_t)h, MX_COL_W, (int32_t)h, h > METER_H - 4u ? C_ERR : C_OK);
    cv_rect(0, 0, MX_COL_W, (int32_t)gr, C_WARN);       /* the reduction, pushing down */
    cv_blit(MX_COL_X, MIX_Y + MX_FADER_Y);
}
