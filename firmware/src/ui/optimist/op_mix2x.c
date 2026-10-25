/* SPDX-License-Identifier: GPL-3.0-only */
/* The mixer in the big face (builder item MIXER_2X; the user, 2026-10-25: "Just the mixer screen is not [readable]. ...
 * 2*2 cards. Condensed master meter"): the mixer's four cards as the other screens' 2x2 big cards (op_layers.c
 * cards_2x2: the same four values, KNOB 1..4 in the same order), and under them in the panel (99 rows over the help
 * band, 117 without). Round 2 (the user, after the FM-1, 2026-10-25: "put the sequence below the tracks. returns can be
 * small font (exception!) to allow normal master level + comp. drum lane should be displayed ... each channel / lane
 * should also have a one pixel vu meter"; "the piano step view should be one 16 step on one line"; "that would allow a
 * one pixel line of step per track"):
 *   T1 T2 T3 (or USB)  the four tracks, a row of 12 px each from y 2 (2 14 26 38): the code (its caps 10 px; white on
 *                      the selected row, struck through and grey-stepped muted) and right of it, on the big line's 16
 *                      columns (x 18 + 14 a step), a 1 px strip of the row's own steps of the page playing (y +3: a hit
 *                      in the row's colour shaded by its level, an empty step dark grey, the playhead's column white,
 *                      grey muted) and its 1 px meter (y +6: the level from the left, black when quiet, the meters the
 *                      normal mixer reads). No playback bar: the strip's playhead says where the track is, and the
 *                      page it plays; only the pages of a LEN over 16 were the bar's alone, and the selected row's
 *                      page bars keep them
 *   DR or a lane       the drum lanes instead (the normal mixer's SOUNDS rows; ALGORITHM walks DR, then the lanes, as
 *                      there): two columns of 6 (lanes 1-6 left, the rest right), 12 px a row, 120 px a column: the
 *                      lane's code, its 1 px strip (6 px a step: 11 lanes at 10 px caps do not fit one column of the
 *                      big line's width) and its 1 px meter (its hits, mx.lit; the ACCENT lane none: no sound). DR
 *                      selected: no lane marked, its steps merged below; a lane: its code white, its steps below
 *   the selected row   under the rows, 3 px down: its 16 steps of the page playing as ONE line (14 px a step, a cell
 *                      12 wide, up to 20 tall: 20 but under 6 rows of lanes (the ACCENT lane built) over the help band, 13), the same
 *                      marks as the rows' strips of the normal mixer (op_mixdraw.c mx_step_cell: the levels' shades,
 *                      the drum dots, the automation colour, grey muted, the playhead white), the pattern's pages as
 *                      thin bars (2 px, +2) and the row's meter (3 px, +5)
 *   MASTER             the normal mixer's MASTER as it is (op_mixdraw.c mx_paint_master, mx_master_gr): the level
 *                      meter over its dB scale, the compressor's reduction and its dB, then the returns CHO DLY REV in
 *                      the small face with their meters (the one exception to the big face; it fits the 99 rows)
 * Never text on a filled area: the codes sit left of their strips. MIXER_2X 0 "small" (the default): op_mixdraw.c as
 * ever, none of this built; 1 "big": this alone; 2 "both": this and the small one, mx_big picks (HOME at the root) */
#define M2_RY 2                         /* the rows: the first's top, a row's pitch */
#define M2_RP 12
#define M2_TO 3                         /* in a row: the steps' 1 px strip, the 1 px meter, under its top */
#define M2_UO 6
#define M2_SX 18                        /* the 16 columns: the first's x (right of a 2-character code), a step's pitch */
#define M2_SP 14
#define M2_SW (16 * M2_SP - 2)          /* their width: 222 */
#define M2_LW 120                       /* the lanes: a column's width, its strip's x in it, a step's pitch */
#define M2_LX 20
#define M2_LP 6
#define M2_NR (MXR_LN - MXR_T1)         /* the rows' slots: T1 .. DR, the lanes */
#define M2_HALF ((DRUM_UILANES + 1u) / 2u)   /* the lanes in the left column */
static struct {
    uint32_t ss[M2_NR];                 /* the rows' strips as drawn (a signature; 0: again) */
    uint8_t pk[M2_NR], vu[M2_NR];       /* the rows' 1 px meters: the peak (falling), as drawn (0xFF: again) */
#if FELUCCA_ED_MIRROR
    uint8_t mv[M2_NR];                  /* ... as the mirror's client has them (0xFF: again) */
#endif
} m2;

static uint32_t m2_lanes(void) { return mx_row() >= MXR_DR && mx_row() < MXR_LN; }   /* DR or a lane: the lanes' rows */
static uint32_t m2_nrows(void) { return m2_lanes() ? DRUM_UILANES : NTRK; }
static uint32_t m2_row_of(uint32_t i) { return m2_lanes() ? MXR_LANE0 + i : MXR_T1 + i; }
static uint32_t m2_sy(void) { return M2_RY + (m2_lanes() ? M2_HALF : NTRK) * M2_RP + 3u; }   /* the big line's top */
static uint32_t m2_sh(void)                             /* its height: up to 20, its bars and meter under it */
{
    int32_t h = OP_PH - (int32_t)m2_sy() - 9;
    return (uint32_t)(h > 20 ? 20 : h);
}
static int32_t m2_rx(uint32_t i) { return m2_lanes() ? (int32_t)(i / M2_HALF) * M2_LW : 0; }   /* row i's place */
static int32_t m2_ry(uint32_t i) { return M2_RY + (int32_t)((m2_lanes() ? i % M2_HALF : i) * M2_RP); }
static int32_t m2_sx(uint32_t i) { return m2_rx(i) + (m2_lanes() ? M2_LX : M2_SX); }   /* its strip and meter */
static int32_t m2_sp(void) { return m2_lanes() ? M2_LP : M2_SP; }
/* row r's strip at x, y of the canvas: a step's column sp - 2 px, 1 px high (past LEN: nothing) */
static void m2_strip_paint(uint32_t r, int32_t x, int32_t y, int32_t sp)
{
    const track_t *t = &trk[mx_trk_of(r)];
    uint32_t len = (uint32_t)ui_clamp(t->p[P_SLEN], 1, NSTEP), at = t->seq_idx % len, page = at & ~15u, i, s;
    uint16_t c = mx_col(r);
    for (i = 0; i < 16u && (s = page + i) < len; i++)
        cv_rect(x + (int32_t)i * sp, y, sp - 2, 1, song.playing && s == at && !mx_smuted(r) ? C_WHITE
                : !mx_on(r, s) ? C_LINE : mx_smuted(r) ? C_DIM : lvl_col(c, mx_lvl(r, s)));
}
static uint32_t m2_strip_sig(uint32_t r)                /* what row r's strip shows */
{
    const track_t *t = &trk[mx_trk_of(r)];
    uint32_t len = (uint32_t)ui_clamp(t->p[P_SLEN], 1, NSTEP), at = t->seq_idx % len, page = at & ~15u, i, sig;
    sig = hu(hu(hu(hu(0x2B1u, song.playing ? at : 0xFFu), page), len * 2u + mx_smuted(r)), mx_col(r));
    for (i = 0; i < 16u; i++)
        sig = sig * 5u + (mx_on(r, page + i) ? 1u + lvl_rank(mx_lvl(r, page + i)) : 0u);
    return sig | 1u;
}
/* row r's 1 px meter at x, y of the canvas, w wide: its level from the left in the meters' colours, black when quiet */
static void m2_vu1_paint(uint32_t r, int32_t x, int32_t y, int32_t w, uint32_t pk)
{
    cv_rect(x, y, w, 1, C_BLACK);
    cv_rect(x, y, (int32_t)(pk * (uint32_t)w / MX_LV), 1, pk > MX_LV - 10u ? C_ERR : r >= MXR_LANE0 ? mx_col(r) : C_OK);
}
static uint32_t m2_vu1_pk(uint32_t k)                   /* a row's meter as it is shown (none while the mirror is over Bluetooth) */
{
#if FELUCCA_ED_MIRROR
    if (mir_meters_off())
        return 0u;
#endif
    return m2.pk[k];
}
/* the selected row's page of steps as one line at y of the canvas, h tall */
static void m2_steps_paint(uint32_t r, int32_t y, uint32_t h)
{
    const track_t *t = &trk[mx_trk_of(r)];
    uint32_t len = (uint32_t)ui_clamp(t->p[P_SLEN], 1, NSTEP), at = t->seq_idx % len, page = at & ~15u, i, np;
    uint16_t c = mx_smuted(r) ? C_DIM : mx_col(r);
    mrect_t q;
    memset(&q, 0, sizeof q);
    q.sp = M2_SP, q.sh = (int32_t)h;
    for (i = 0; i < 16u; i++)
        if (page + i < len)
            mx_step_cell(r, t, page + i, at, c, M2_SX + (int32_t)i * M2_SP, y, &q);
    np = (len + 15u) / 16u;
    if (np > 1u)                                        /* the pattern's pages: a thin bar each, the one playing lit */
        for (i = 0; i < np; i++)
            cv_rect(M2_SX + (int32_t)(i * (uint32_t)M2_SW / np), y + (int32_t)h + 2, (int32_t)((uint32_t)M2_SW / np) - 2, 2,
                    i == page / 16u ? C_WHITE : C_DIM);
}
static void m2_paint_row(uint32_t i)                    /* row i: its code, strip and meter */
{
    uint32_t r = m2_row_of(i), k = r - MXR_T1;
    int32_t x = m2_rx(i), y = m2_ry(i), sx = m2_sx(i), sp = m2_sp();
    text_st(x + 1, y - 2, &FONT_S, mx_code(r), r == mx_row() ? C_WHITE : mx_smuted(r) ? C_DIM : mx_col(r), mx_muted(r));
    m2_strip_paint(r, sx, y + M2_TO, sp);
    m2.ss[k] = m2_strip_sig(r);
    if (!mx_is_acc(r))                                  /* (the ACCENT lane: no sound, no meter) */
        m2_vu1_paint(r, sx, y + M2_UO, 16 * sp - 2, m2_vu1_pk(k));
}
static void m2_paint(void)                              /* the panel whole (cv_tall) */
{
    uint32_t r = mx_row(), i, sy = m2_sy(), sh = m2_sh();
    if (r == MXR_MASTER) {
        mx_paint_master();
        return;
    }
    for (i = 0; i < m2_nrows(); i++)
        m2_paint_row(i);
    if (mx_is_usb(r)) {                                 /* (no sequence: its name and whether it streams) */
        char en[10], pr[16];
        mx_names(r, en, pr);
        cv_texts(cv_texts(M2_SX, (int32_t)sy, en, C_HI) + 8, (int32_t)sy, pr, C_WHITE);
    } else {
        m2_steps_paint(r, (int32_t)sy, sh);
    }
    if (!mx_is_acc(r))
        mx_vu_band(r, M2_SX, (int32_t)(sy + sh + 5u), M2_SW, 3, mxd.pk[r], mx_gr_px(r, M2_SW));
}
static uint32_t m2_sig(uint32_t sig)                    /* what the panel shows besides its live parts: the rows' mutes, colours */
{
    uint32_t i;
#if FELUCCA_MIXER_2X == 2
    sig = hu(sig, mx_big);                              /* ("both": the face switched, drawn whole again) */
    if (!mx_big)
        return sig;
#endif
    for (i = 0; i < m2_nrows(); i++)
        sig = hu(hu(sig, mx_muted(m2_row_of(i))), mx_col(m2_row_of(i)));
    return hu(sig, mx_smuted(mx_row()));
}
/* the selected row's steps: drawn again when they change, the playhead alone its two cells (as op_mixdraw.c mx_steps) */
static void m2_steps(uint32_t r)
{
    const track_t *t = &trk[mx_trk_of(r)];
    uint32_t len = (uint32_t)ui_clamp(t->p[P_SLEN], 1, NSTEP), at = t->seq_idx % len, page = at & ~15u, i, sig;
    uint32_t sy = m2_sy(), sh = m2_sh();
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
        q.sp = M2_SP, q.sh = (int32_t)sh;
        for (k = 0; k < 2u; k++) {
            if (s[k] < page || s[k] >= page + 16u)
                continue;
            cv_begin(M2_SP, sh, C_BLACK);
            mx_step_cell(r, t, s[k], at, c, 0, 0, &q);
            cv_blit((uint32_t)(M2_SX + (int32_t)(s[k] - page) * M2_SP), (uint32_t)OP_PY + sy);
        }
        return;
    }
    mxd.stp[r] = sig;
    mxd.sta[r] = (uint8_t)at;
    cv_begin(240, sh + 4u, C_BLACK);
    m2_steps_paint(r, 0, sh);
    cv_blit(0, (uint32_t)OP_PY + sy);
}
/* row i's live parts: its strip when it changed, its meter (falling as the normal mixer's; with the mirror over USB only
 * on the frames its meters go, so the client has every one it shows) */
static void m2_live_row(uint32_t i, const uint32_t *trk_lv)
{
    uint32_t r = m2_row_of(i), k = r - MXR_T1, sig = m2_strip_sig(r), lv, pk;
    int32_t y = m2_ry(i), sx = m2_sx(i), sp = m2_sp();
    if (sig != m2.ss[k]) {
        m2.ss[k] = sig;
        cv_begin((uint32_t)(16 * sp - 2), 1, C_BLACK);
        m2_strip_paint(r, 0, 0, sp);
        cv_blit((uint32_t)sx, (uint32_t)(OP_PY + y + M2_TO));
    }
    if (mx_is_acc(r))
        return;
    lv = mx_level(r, trk_lv);
    pk = m2.pk[k];
    m2.pk[k] = (uint8_t)(lv > pk ? lv : pk > 5u ? pk - 5u : 0u);
    pk = m2_vu1_pk(k);
#if FELUCCA_ED_MIRROR
    {   /* (as op_mixdraw.c mx_vu_at: the panel's when it changed, the client's on the frames its meters go) */
        uint32_t panel = pk != m2.vu[k], mir = mxd.mir && pk != m2.mv[k];
        if (!panel && !mir)
            return;
        m2.vu[k] = (uint8_t)pk;
        if (mir)
            m2.mv[k] = (uint8_t)pk;
        cv_begin((uint32_t)(16 * sp - 2), 1, C_BLACK);
        m2_vu1_paint(r, 0, 0, 16 * sp - 2, pk);
        if (!panel) {
            cv_mirror((uint32_t)sx, (uint32_t)(OP_PY + y + M2_UO));
            return;
        }
        mir_mute = (uint8_t)!mir;
        cv_blit((uint32_t)sx, (uint32_t)(OP_PY + y + M2_UO));
        mir_mute = 0;
    }
#else
    if (pk == m2.vu[k])
        return;
    m2.vu[k] = (uint8_t)pk;
    cv_begin((uint32_t)(16 * sp - 2), 1, C_BLACK);
    m2_vu1_paint(r, 0, 0, 16 * sp - 2, pk);
    cv_blit((uint32_t)sx, (uint32_t)(OP_PY + y + M2_UO));
#endif
}
/* the live parts: MASTER's as the normal mixer's; else the rows', the selected row's meter and steps */
static void m2_live(const uint32_t *trk_lv, const uint32_t *ret_lv, uint32_t n)
{
    uint32_t r = mx_row(), i, sy = m2_sy(), sh = m2_sh();
    if (r == MXR_MASTER) {
        mx_vu_at(MXR_MASTER, MXM_X, MXM_VY, MXM_W, MXM_VH, mx_level(MXR_MASTER, trk_lv), 0);
        mx_master_gr();
        for (i = 0; i < n; i++)
            mx_vu_at(MXR_T1 + i, MXM_GX, MXM_CY + (int32_t)i * MXM_CP + MXM_CV, MXM_GW, MXM_CH, ret_lv[i], 0);
        return;
    }
    for (i = 0; i < m2_nrows(); i++)
        m2_live_row(i, trk_lv);
    if (!mx_is_acc(r))
        mx_vu_at(r, M2_SX, (int32_t)(sy + sh + 5u), M2_SW, 3, mx_level(r, trk_lv), mx_gr_px(r, M2_SW));
    if (!mx_is_usb(r))
        m2_steps(r);
}
static void m2_redraw(void)
{
    memset(m2.ss, 0, sizeof m2.ss);
    memset(m2.vu, 0xFF, sizeof m2.vu);
#if FELUCCA_ED_MIRROR
    memset(m2.mv, 0xFF, sizeof m2.mv);
#endif
}
