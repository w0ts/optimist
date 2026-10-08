/* SPDX-License-Identifier: GPL-3.0-only */
/* The horizontal mixer's rows (op_mixer.c), in the panel under the cards: a row a track, 20 px, a scrolling list
 * (MASTER above T1, out of view until the cursor goes up; the drum lanes after DR, indented):
 *   x 0..57    the name in the row's colour (T1 and its sound; a lane's short name; MSTR), M / S badges
 *   x 60..235  the VU meter (y +2, 7 px): the level filling from the left, its peak falling 4 px a frame; the
 *              compressor's gain reduction pushing in from the right in amber, 3 px a dB, only on a row whose
 *              compressor works (a part's COMP insert: fx.c tcomp_gr_q4; MASTER: THRS or CEIL set, mc_gr_view)
 *              under it (y +11, 7 px) the row's sequence, as SLOOP's TRACKS screen: the 16 steps of the page playing
 *              (the steps set in the row's colour, past LEN empty, the playhead white), a mark a page when LEN > 16 (the one playing lit); DR all
 *              the lanes merged, a lane its own hits; MASTER none
 * The selected row framed in its colour, its ground tinted. The small forms of pan, sends and insert are left out:
 * at 20 px a row holds the meter and the sequence readably, and the cards show the selected row's values. A lane's
 * meter is its hits (drums.hits), full at the hit and falling over 12 frames: the drum voices have no level of
 * their own to read. Each row's meter and steps are canvases of their own, drawn again only when they change. */
#define MXL_H 20                        /* a row */
#define MXL_VX 60                       /* the meter and the steps */
#define MXL_VW 176
#define MXL_SP 9                        /* a step's pitch, its cell 7 */
#define MX_GR_PX 3                      /* the reduction: px a dB */
static struct {
    uint8_t first, shown;               /* the rows in view */
    uint8_t vu[MXR_N], gr[MXR_N];       /* the meters as drawn (0xFF: again) */
    uint32_t stp[MXR_N];                /* the steps as drawn (a signature) */
    uint8_t pk[MXR_N];                  /* the meters' peaks, falling */
} mxd;

static void mx_view(void)                               /* the rows in view: the cursor inside, MASTER only up there */
{
    uint32_t cur = mx_row(), shown = (uint32_t)OP_PH / MXL_H, first;
    if (cur == MXR_MASTER)
        first = 0;
    else {
        first = cur > shown / 2u ? cur - shown / 2u : 1u;
        first = first < 1u ? 1u : first;
        if (first + shown > MXR_N)
            first = MXR_N - shown;
    }
    mxd.first = (uint8_t)first;
    mxd.shown = (uint8_t)shown;
}
static void mx_name(uint32_t r, char *b, uint32_t n)    /* the row's name as drawn */
{
    char s[16];
    if (r == MXR_MASTER) {
        str_cpy(b, "MSTR", n);
    } else if (r >= MXR_LANE0) {
        str_cpy(b, LANE_SHORT[r - MXR_LANE0], n);       /* ("c.hat", as the kit names it) */
    } else {
        str_cpy(b, trk_tag(mx_trk_of(r)), n);
        snd_name(mx_trk_of(r), s);
        s[4] = 0;
        str_cpy(b + str_len(b), " ", n - str_len(b));
        op_case(b + str_len(b), s, n - str_len(b));
    }
}
static void mx_paint(void)                              /* the rows' grounds and names (cv_tall) */
{
    uint32_t i, cur = mx_row();
    char b[16];
    for (i = mxd.first; i < MXR_N && i < (uint32_t)mxd.first + mxd.shown; i++) {
        int32_t y = (int32_t)(i - mxd.first) * MXL_H, x = i >= MXR_LANE0 ? 10 : 4;
        uint16_t c = mx_col(i);
        if (i == cur) {
            cv_rect(0, y, 240, MXL_H - 1, col_shade(c, 3u));
            cv_frame(0, y, 240, MXL_H - 1, c);
        } else {
            cv_rect(0, y, 240, MXL_H - 1, i >= MXR_LANE0 ? C_BLACK : OP_SURF);
        }
        cv_rect(1, y + 2, 2, MXL_H - 5, c);
        mx_name(i, b, sizeof b);
        cv_text(x, y + 2, &FONT_S, cut(b, b, i >= MXR_LANE0 ? 5u : 6u), i == cur ? C_WHITE : c);
        if (i >= MXR_T1 && i <= MXR_DR) {
            uint32_t k = mx_trk_of(i);
            if (trk[k].p[P_MUTE])
                cv_text(50, y + 2, &FONT_S, "M", C_WARN);
            else if ((song.solo >> k) & 1u)
                cv_text(50, y + 2, &FONT_S, "S", C_OK);
        }
    }
}
static uint32_t mx_static_sig(void)
{
    uint32_t i, sig = hu(hu(hu(0x3A1u, settings.palette), mx_row()), (uint32_t)mxd.first * 64u + mxd.shown);
    char b[16];
    for (i = mxd.first; i < MXR_N && i < (uint32_t)mxd.first + mxd.shown; i++) {
        mx_name(i, b, sizeof b);
        sig = hs(hu(sig, mx_col(i)), b);
        if (i >= MXR_T1 && i <= MXR_DR)
            sig = hu(sig, (uint32_t)trk[mx_trk_of(i)].p[P_MUTE] * 2u + ((song.solo >> mx_trk_of(i)) & 1u));
    }
    return sig;
}

/* ---- the live parts */
static uint32_t mx_vu_px(int32_t pk)                    /* a peak (32767 = 0 dBFS) as a width: 6 dB a step */
{
    return meter_h(pk) * MXL_VW / METER_H;
}
static int32_t mx_master_pk(void)                       /* the master's last frame (the scope's ring, MASTER up) */
{
    uint32_t i, w = vis_wr, pk = 0;
    for (i = VIS_RING - 736u; i < VIS_RING; i++) {
        int32_t x = vis_pcm[0][(w + i) & (VIS_RING - 1u)];
        uint32_t a = (uint32_t)(x < 0 ? -x : x);
        pk = a > pk ? a : pk;
    }
    return knee((int32_t)(pk > 0x7FFFFFF ? 0x7FFFFFF : pk));
}
/* row r's compressor's reduction, dB x 4; 0: none working (the bar not drawn) */
static uint32_t mx_gr_q4(uint32_t r)
{
#if FELUCCA_MASTER_COMP
    if (r == MXR_MASTER)
        return song.g[G_CTHR] || song.g[G_CCEIL] ? (uint32_t)clamp(-(int32_t)mc_gr_view * 4, 0, 127) : 0u;
    if (r >= MXR_T1 && r < MXR_DR) {
        const track_t *t = &trk[r - MXR_T1];
        return fx_on(t) && FXS_ON(FXT_COMP) && t->p[P_TCOMP] > 0 ? tcomp_gr_q4(r - MXR_T1) : 0u;
    }
#else
    (void)r;
#endif
    return 0;
}
static uint32_t mx_level_px(uint32_t r, const uint32_t *trk_px)   /* row r's level now, px (before the fall) */
{
    if (r == MXR_MASTER)
        return mx_vu_px(mx_master_pk());
    if (r >= MXR_LANE0)
        return MXL_VW * mx.lit[r - MXR_LANE0] / 12u;
    return trk_px[mx_trk_of(r)];
}
static void mx_vu(uint32_t r, int32_t y, uint32_t lv)
{
    uint32_t gr = mx_gr_q4(r) * MX_GR_PX / 4u, pk = mxd.pk[r];
    pk = lv > pk ? lv : pk > 4u ? pk - 4u : 0u;         /* (falls 4 px a frame) */
    mxd.pk[r] = (uint8_t)pk;
    gr = gr > MXL_VW ? MXL_VW : gr;
    if (pk == mxd.vu[r] && gr == mxd.gr[r])
        return;
    mxd.vu[r] = (uint8_t)pk;
    mxd.gr[r] = (uint8_t)gr;
    cv_begin(MXL_VW, 7, C_LINE);
    cv_rect(0, 0, (int32_t)pk, 7, pk > MXL_VW - 8u ? C_ERR : r >= MXR_LANE0 ? mx_col(r) : C_OK);
    if (gr)
        cv_rect(MXL_VW - (int32_t)gr, 1, (int32_t)gr, 5, C_WARN);   /* the reduction, from the right */
    cv_blit(MXL_VX, (uint32_t)(OP_PY + y + 2));
}
static uint32_t mx_on(uint32_t r, uint32_t i)           /* step i is set on row r */
{
    const track_t *t = &trk[mx_trk_of(r)];
    if (r >= MXR_LANE0)
        return dstep_has(&t->dstep[i % NSTEP], r - MXR_LANE0);
    return trk_on_step(mx_trk_of(r), i);
}
static void mx_steps(uint32_t r, int32_t y)
{
    const track_t *t = &trk[mx_trk_of(r)];
    uint32_t len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP), at = t->seq_idx % len, page = at & ~15u, i, sig;
    uint16_t c = mx_col(r), bg = r == mx_row() ? col_shade(c, 3u) : r >= MXR_LANE0 ? C_BLACK : OP_SURF;
    if (r == MXR_MASTER)
        return;
    sig = hu(hu(hu(0x57u, song.playing ? at : 64u + page), len), bg);
    for (i = 0; i < 16u; i++)
        sig = sig * 3u + mx_on(r, page + i);
    if (sig == mxd.stp[r])
        return;
    mxd.stp[r] = sig;
    cv_begin(MXL_VW, 7, bg);
    for (i = 0; i < 16u; i++) {
        uint32_t s = page + i;
        if (s >= len)
            continue;
        cv_rect((int32_t)i * MXL_SP, 0, MXL_SP - 2, 7, song.playing && s == at ? C_WHITE : mx_on(r, s) ? c : C_LINE);
    }
    if (len > 16u)                                      /* the pattern's pages: a mark each, the one playing lit */
        for (i = 0; i < (len + 15u) / 16u; i++)
            cv_rect(148 + (int32_t)i * 7, 1, 5, 5, i == page / 16u ? C_WHITE : C_DIM);
    cv_blit(MXL_VX, (uint32_t)(OP_PY + y + 11));
}
static void mx_draw(void)
{
    uint32_t i, sig, trk_px[NTRK];
    for (i = 0; i < NTRK; i++)                          /* (taken every frame, seen or not: no stale peak) */
        trk_px[i] = mx_vu_px(meter_ui_take(i));
    mx_view();
    sig = mx_static_sig();
    if (sig != ui.sig[2]) {
        ui.sig[2] = sig;
        cv_tall(OP_PY, OH_BODY, C_BLACK, mx_paint);
        memset(mxd.vu, 0xFF, sizeof mxd.vu);
        memset(mxd.gr, 0xFF, sizeof mxd.gr);
        memset(mxd.stp, 0, sizeof mxd.stp);
    }
    if (ui.overlay == 2u)
        return;                                         /* (under a toast: the rows wait) */
    for (i = mxd.first; i < MXR_N && i < (uint32_t)mxd.first + mxd.shown; i++) {
        int32_t y = (int32_t)(i - mxd.first) * MXL_H;
        mx_vu(i, y, mx_level_px(i, trk_px));
        mx_steps(i, y);
    }
}
static void mx_redraw(void)                             /* the live parts again (an overlay went) */
{
    memset(mxd.vu, 0xFF, sizeof mxd.vu);
    memset(mxd.stp, 0, sizeof mxd.stp);
}
