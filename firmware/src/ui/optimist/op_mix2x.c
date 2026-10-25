/* SPDX-License-Identifier: GPL-3.0-only */
/* The mixer in the big face (builder item MIXER_2X; the user, 2026-10-25: "Just the mixer screen is not [readable]. ...
 * 2*2 cards. Condensed master meter"): the mixer's four cards as the other screens' 2x2 big cards (op_layers.c
 * cards_2x2: the same four values, KNOB 1..4 in the same order), and under them in the panel (99 rows over the help
 * band, 117 without; the rows below are over the band, without it in brackets):
 *   a channel, a lane, DR   the SELECTED row's sequence large ("steps on selected track only, but still thin progress
 *                           bar per track", the user's choice): the 16 steps of the page playing as two lines of 8
 *                           (y 2 and 21 (30), 27 x 16 (25) px a cell, 29 px apart), the same marks as the rows' strips
 *                           (op_mixdraw.c mx_step_cell: the levels' shades, the drum dots, the automation colour,
 *                           grey muted, the playhead white); the pattern's pages as thin bars under them (y 40 (58)),
 *                           then the row's meter, 3 px (y 43 (61)); under it the four tracks T1 T2 T3 DR, a row of
 *                           12 px each (y 50 62 74 86 (68 80 92 104)): the code (its caps 10 px; white on the selected
 *                           track, struck through muted) and beside it a thin bar (4 px, x 26..235) filling with the
 *                           track's pattern playback, its playhead over its length (empty when stopped), in the
 *                           track's colour, full on the selected track, dimmed on the others, grey muted. A lane
 *                           selected: the drum track's bar is the one marked (the lanes play the drum track's one
 *                           length, so a bar a lane would draw the same bar 16 times)
 *   the USB input           its name and whether it streams in place of the steps (it has none), its meter, the bars
 *   MASTER                  a thin line (5 px, y 4) for the level from the left and the compressor's gain reduction
 *                           in amber from the right (no dB scale), then the FX returns, a row each (28 (34) px apart
 *                           from y 14): the return's name in the large face and its meter (the same level as the
 *                           MASTER page draws today: fx.c fx_ret_tap) beside it
 * Never text on a filled area: the codes and names sit left of their bars. Off (the default): op_mixdraw.c as ever */
#define M2_BP 12                        /* a track's bar row: its code's caps (10 px), the bar beside them */
#define M2_BY (OP_PH - 4 * M2_BP - 1)   /* the four bars' first row: 50 (68) */
#define M2_BO 3                         /* a bar's top under its row's top */
#define M2_BX 26                        /* the bars: from right of the code (2 characters) to the edge */
#define M2_BW 210
#define M2_BH 4
#define M2_SX 4                         /* the steps: 8 a line, a step's pitch 29 px (its cell 27) */
#define M2_SP 29
#define M2_SY 2
#define M2_SL ((M2_BY - 12) / 2)        /* a line's pitch: 19 (28) */
#define M2_SH (M2_SL - 3)               /* a cell's height: 16 (25) */
#define M2_PY (M2_SY + 2 * M2_SL)       /* the pages' bars (LEN > 16): 40 (58), 2 px */
#define M2_SW (8 * M2_SP - 2)           /* the steps' width: 230 */
#define M2_VY (M2_BY - 7)               /* the selected row's meter: 43 (61), 3 px */
#define M2_VW 232
#define M2M_VX 8                        /* MASTER: the level / reduction line */
#define M2M_VY 4
#define M2M_VW 224
#define M2M_VH 5
#define M2M_RY 14                       /* the returns' rows: the first's top (its name's caps from there) */
#define M2M_RP ((OP_PH - M2M_RY) / 3)   /* 28 (34) */
#define M2M_RX 76                       /* a return's meter: right of its name (4 large characters at most) */
#define M2M_RW 156
#define M2M_RO 6                        /* its top under its row's top (the middle of the name's 20 px caps) */
#define M2M_RH 8
static struct {
    uint8_t bx[NTRK];                   /* the tracks' bars as drawn, px (0xFF: again) */
} m2;                                   /* (set by the first paint, then each change) */

static uint32_t m2_sel(void)                            /* the track whose bar is marked: the selected row's (NTRK: none) */
{
    uint32_t r = mx_row();
    return r == MXR_MASTER || mx_is_usb(r) ? NTRK : mx_trk_of(r);
}
static uint16_t m2_bar_col(uint32_t k)                  /* track k's bar: its colour, dimmed off the selected track, grey muted */
{
    uint16_t c = mx_col(MXR_T1 + k);
    return mx_smuted(MXR_T1 + k) ? C_DIM : k == m2_sel() ? c : col_shade(c, 5u);
}
static uint32_t m2_bar_px(uint32_t k)                   /* track k's playback as a width: to its playhead's step, 0 stopped */
{
    const track_t *t = &trk[k];
    uint32_t len = (uint32_t)ui_clamp(t->p[P_SLEN], 1, NSTEP);
    return song.playing ? (t->seq_idx % len + 1u) * M2_BW / len : 0u;
}
static void m2_bar_paint(uint32_t k, int32_t x, int32_t y, uint32_t px)   /* track k's bar at x, y of the canvas: the trough, the fill */
{
    cv_rect(x, y, M2_BW, M2_BH, C_BLACK);
    cv_rect(x, y + M2_BH - 1, M2_BW, 1, C_LINE);
    cv_rect(x, y, (int32_t)px, M2_BH, m2_bar_col(k));
}
static void m2_steps_paint(uint32_t r)                  /* the selected row's page of steps, large, at the canvas' top */
{
    const track_t *t = &trk[mx_trk_of(r)];
    uint32_t len = (uint32_t)ui_clamp(t->p[P_SLEN], 1, NSTEP), at = t->seq_idx % len, page = at & ~15u, i, np;
    uint16_t c = mx_smuted(r) ? C_DIM : mx_col(r);
    mrect_t q;
    memset(&q, 0, sizeof q);
    q.sp = M2_SP, q.sh = M2_SH;
    for (i = 0; i < 16u; i++)
        if (page + i < len)
            mx_step_cell(r, t, page + i, at, c, M2_SX + (int32_t)(i & 7u) * M2_SP, M2_SY + (int32_t)(i >> 3) * M2_SL, &q);
    np = (len + 15u) / 16u;
    if (np > 1u)                                        /* the pattern's pages: a thin bar each, the one playing lit */
        for (i = 0; i < np; i++)
            cv_rect(M2_SX + (int32_t)(i * (uint32_t)M2_SW / np), M2_PY, (int32_t)((uint32_t)M2_SW / np) - 2, 2,
                    i == page / 16u ? C_WHITE : C_DIM);
}
static void m2_paint_master(void)                       /* MASTER: the line, the returns' names and meters */
{
    uint32_t i, n;
    uint8_t ret[3];
    char b[8];
    mx_vu_band(MXR_MASTER, M2M_VX, M2M_VY, M2M_VW, M2M_VH, mxd.pk[MXR_MASTER], mx_gr_px(MXR_MASTER, M2M_VW));
    n = mx_rets(ret);
    for (i = 0; i < n; i++) {                           /* (a return's meter state: the row T1 + its line's, unseen here) */
        int32_t y = M2M_RY + (int32_t)i * M2M_RP;
        cv_text(M2M_VX, y - 4, font_big(), cut(b, fxs_line_name(ret[i]), 4), C_HI);
        mx_vu_band(MXR_T1 + i, M2M_RX, y + M2M_RO, M2M_RW, M2M_RH, mxd.pk[MXR_T1 + i], 0);
    }
}
static void m2_paint(void)                              /* the panel whole (cv_tall) */
{
    uint32_t r = mx_row(), k;
    if (r == MXR_MASTER) {
        m2_paint_master();
        return;
    }
    if (mx_is_usb(r)) {                                 /* (no sequence: its name and whether it streams) */
        char en[10], pr[16];
        mx_names(r, en, pr);
        cv_texts(cv_texts(M2_SX, M2_SY, en, C_HI) + 8, M2_SY, pr, C_WHITE);
    } else {
        m2_steps_paint(r);
    }
    if (!mx_is_acc(r))                                  /* (the ACCENT lane: no sound, no meter) */
        mx_vu_band(r, M2_SX, M2_VY, M2_VW, 3, mxd.pk[r], mx_gr_px(r, M2_VW));
    for (k = 0; k < NTRK; k++) {
        int32_t y = M2_BY + (int32_t)(k * M2_BP);
        text_st(M2_SX, y - 2, &FONT_S, trk_tag(k), k == m2_sel() ? C_WHITE : mx_col(MXR_T1 + k), mx_muted(MXR_T1 + k));
        m2_bar_paint(k, M2_BX, y + M2_BO, m2_bar_px(k));
        m2.bx[k] = (uint8_t)m2_bar_px(k);
    }
}
static uint32_t m2_sig(uint32_t sig)                    /* what the panel shows besides its live parts: the tracks' mutes, colours */
{
    uint32_t k;
    for (k = 0; k < NTRK; k++)
        sig = hu(hu(sig, mx_muted(MXR_T1 + k)), mx_col(MXR_T1 + k));
    return hu(sig, mx_smuted(mx_row()));
}
/* the selected row's steps: drawn again when they change, the playhead alone its two cells (as op_mixdraw.c mx_steps) */
static void m2_steps(uint32_t r)
{
    const track_t *t = &trk[mx_trk_of(r)];
    uint32_t len = (uint32_t)ui_clamp(t->p[P_SLEN], 1, NSTEP), at = t->seq_idx % len, page = at & ~15u, i, sig;
    sig = hu(hu(hu(hu(0x2A7u, song.playing), page), len), mx_smuted(r));
    for (i = 0; i < 16u; i++)
        sig = sig * 5u + mx_on(r, page + i) * 2u + (mx_on(r, page + i) && op_step_auto(t, page + i, r >= MXR_LANE0 ? r - MXR_LANE0 : 0xFFu))
            + (mx_on(r, page + i) ? lvl_rank(mx_lvl(r, page + i)) * 7u : 0u);
    if (sig == mxd.stp[r]) {
        uint32_t k, s[2] = {mxd.sta[r], at};
        uint16_t c = mx_smuted(r) ? C_DIM : mx_col(r);
        mrect_t q;
        if (!song.playing || at == mxd.sta[r])
            return;
        mxd.sta[r] = (uint8_t)at;
        memset(&q, 0, sizeof q);
        q.sp = M2_SP, q.sh = M2_SH;
        for (k = 0; k < 2u; k++) {
            if (s[k] < page || s[k] >= page + 16u)
                continue;
            cv_begin(M2_SP, (uint32_t)M2_SH, C_BLACK);
            mx_step_cell(r, t, s[k], at, c, 0, 0, &q);
            cv_blit((uint32_t)(M2_SX + (int32_t)((s[k] - page) & 7u) * M2_SP),
                    (uint32_t)(OP_PY + M2_SY + (int32_t)((s[k] - page) >> 3) * M2_SL));
        }
        return;
    }
    mxd.stp[r] = sig;
    mxd.sta[r] = (uint8_t)at;
    cv_begin(240, (uint32_t)(M2_PY + 2), C_BLACK);
    m2_steps_paint(r);
    cv_blit(0, (uint32_t)OP_PY);
}
/* the live parts: the meters, the selected row's steps, the tracks' bars (each drawn when it changed) */
static void m2_live(const uint32_t *trk_lv, const uint32_t *ret_lv, uint32_t n)
{
    uint32_t r = mx_row(), k, px;
    if (r == MXR_MASTER) {
        mx_vu_at(MXR_MASTER, M2M_VX, M2M_VY, M2M_VW, M2M_VH, mx_level(MXR_MASTER, trk_lv), mx_gr_px(MXR_MASTER, M2M_VW));
        for (k = 0; k < n; k++)
            mx_vu_at(MXR_T1 + k, M2M_RX, M2M_RY + (int32_t)k * M2M_RP + M2M_RO, M2M_RW, M2M_RH, ret_lv[k], 0);
        return;
    }
    if (!mx_is_acc(r))
        mx_vu_at(r, M2_SX, M2_VY, M2_VW, 3, mx_level(r, trk_lv), mx_gr_px(r, M2_VW));
    if (!mx_is_usb(r))
        m2_steps(r);
    for (k = 0; k < NTRK; k++) {
        if ((px = m2_bar_px(k)) == m2.bx[k])
            continue;
        m2.bx[k] = (uint8_t)px;
        cv_begin(M2_BW, M2_BH, C_BLACK);
        m2_bar_paint(k, 0, 0, px);
        cv_blit(M2_BX, (uint32_t)(OP_PY + M2_BY + (int32_t)(k * M2_BP) + M2_BO));
    }
}
static void m2_redraw(void) { memset(m2.bx, 0xFF, sizeof m2.bx); }
