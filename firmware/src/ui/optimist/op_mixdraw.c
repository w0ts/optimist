/* SPDX-License-Identifier: GPL-3.0-only */
/* The horizontal mixer's rows (op_mixer.c), in the panel under the cards: FOUR rows a screen (the user: "I would
 * stick with 4 tracks per screen"), each a quarter of the panel (41 px: the mixer keeps the 1x4 cards whatever CARDS
 * says, op_state.c cards_2x2), T1 T2 T3 DR when it opens; ALGORITHM past DR scrolls the window a row at a time
 * through the 16 lanes, past T1 up it shows MASTER. The user, then: "more space to display the VU meter, compressor
 * and sequencer", so the name is a code at the left and the rest of the width is the row's:
 *   x 0..21    a colour chip and the row's code (T1 T2 T3 DR, a lane's BD SD CH ..., M for MASTER), its M / S badge
 *              under it; the selected row's full name is the header's ("Snare levels", op_draw.c head_title)
 *   x 24..237  three lines. The text line (y +1): the engine's name in the row's colour (ANALOG, TRIO, DRUMS, a
 *              lane's name, MASTER), then the preset (the kit for DR, a user preset's name), cut to the room left;
 *              under it (y +20, 6 px) the row's sequence, as SLOOP's TRACKS screen: the 16 steps of the page playing
 *              (13 px a step: the steps set in the row's colour, past LEN empty, the playhead white), and under the
 *              steps a thin bar a page when LEN > 16 (the one playing lit); DR all the lanes merged, a lane its own
 *              hits; MASTER none; at the foot (y +34) the VU meter, a 3 px bar: the level filling from the left, its
 *              peak falling 4 px a frame; the compressor's gain reduction pushing in from the right in amber, 3 px a
 *              dB, only on a row whose compressor works (a part's COMP insert: fx.c tcomp_gr_q4; MASTER: THRS or
 *              CEIL set, mc_gr_view)
 * The selected row framed in its colour, its ground tinted. No pan, send or insert forms on the rows: the cards show
 * the selected row's values. A lane's meter is its hits (drums.hits), full at the hit and falling over 12 frames:
 * the drum voices have no level of their own to read. Each row's meter and steps are canvases of their own, drawn
 * again only when they change. */
#define MXL_N 4u                        /* rows a screen */
#define MXL_H ((int32_t)OP_PH / (int32_t)MXL_N)   /* a row: 41 px (the panel 164, the mixer's cards 1x4) */
#define MXL_VX 28                       /* the meter and the steps: the width left of the code */
#define MXL_VW 210
#define MXL_TY 1                        /* the text line: the engine, then the preset (the user: "each track shows the synth
                                         * name and the preset", so the meter and the steps went thin) */
#define MXL_SY 20                       /* the steps in their row: small cells, 6 px, the page bars under them */
#define MXL_SH 6
#define MXL_SP 13                       /* a step's pitch, its cell 11 */
#define MXL_VY 34                       /* the meter in its row: a thin bar */
#define MXL_VH 3
#define MX_GR_PX 3                      /* the reduction: px a dB */
static const char *const LANE_CODE[DRUM_LANES] = {"BD", "B2", "SD", "CP", "CH", "OH", "PH", "RS",
                                                  "S2", "LT", "HT", "CR", "RD", "SH", "CG", "CB"};
static struct {
    uint8_t first;                      /* the first row in view */
    uint8_t vu[MXR_N], gr[MXR_N];       /* the meters as drawn (0xFF: again) */
    uint32_t stp[MXR_N];                /* the steps as drawn (a signature) */
    uint8_t pk[MXR_N];                  /* the meters' peaks, falling */
} mxd = {.first = MXR_T1};

static void mx_view(void)                               /* the four rows in view: the cursor inside, a row a step */
{
    uint32_t cur = mx_row(), first = mxd.first;
    if (cur < first)
        first = cur;
    else if (cur >= first + MXL_N)
        first = cur - (MXL_N - 1u);
    if (first == MXR_MASTER && cur != MXR_MASTER)
        first = MXR_T1;                                 /* (MASTER only while the cursor is on it) */
    mxd.first = (uint8_t)first;
}
static const char *mx_code(uint32_t r)                  /* the row's code at its left */
{
    return r == MXR_MASTER ? "M" : r >= MXR_LANE0 ? LANE_CODE[r - MXR_LANE0] : trk_tag(mx_trk_of(r));
}
/* the row's sound as two words: the engine (DRUMS for the drum track, MASTER, a lane's name) and the preset (the kit
 * for the drum track and its lanes, a user preset's name); each cut to the room it has */
static void mx_names(uint32_t r, char *eng, char *pre)
{
    pre[0] = 0;
    if (r == MXR_MASTER) {
        str_cpy(eng, "MASTER", 9);
    } else if (r >= MXR_LANE0) {
        str_cpy(eng, LANE_NAME[r - MXR_LANE0], 9);
        str_cpy(pre, drum_kit_name(), 14);
    } else {
        uint32_t k = mx_trk_of(r);
        str_cpy(eng, is_drum(&trk[k]) ? "DRUMS" : ENGINES[trk[k].eng_req % NENGINES]->name, 9);
        snd_name(k, pre);
    }
}
static void mx_paint(void)                              /* the rows' grounds, chips and codes (cv_tall) */
{
    uint32_t i, cur = mx_row();
    char en[10], pr[16], b[16];
    int32_t x;
    for (i = mxd.first; i < MXR_N && i < (uint32_t)mxd.first + MXL_N; i++) {
        int32_t y = (int32_t)(i - mxd.first) * MXL_H;
        uint16_t c = mx_col(i);
        if (i == cur) {
            cv_rect(0, y, 240, MXL_H - 1, col_shade(c, 3u));
            cv_frame(0, y, 240, MXL_H - 1, c);
        } else {
            cv_rect(0, y, 240, MXL_H - 1, i >= MXR_LANE0 ? C_BLACK : OP_SURF);
        }
        cv_rect(2, y + 3, 3, MXL_H - 7, c);             /* the colour chip */
        cv_text(6, y + MXL_TY, &FONT_S, mx_code(i), i == cur ? C_WHITE : c);
        mx_names(i, en, pr);
        x = cv_text(MXL_VX, y + MXL_TY, &FONT_S, cut(b, en, 8), c);      /* ANALOG, in the row's colour ... */
        if (pr[0])
            cv_text(x + 8, y + MXL_TY, &FONT_S, cut(b, pr, (uint32_t)(238 - x - 8) / 8u), i == cur ? C_WHITE : C_GRAY);   /* ... then the preset */
        if (i >= MXR_T1 && i <= MXR_DR) {
            uint32_t k = mx_trk_of(i);
            if (trk[k].p[P_MUTE])
                cv_text(6, y + 18, &FONT_S, "M", C_WARN);
            else if ((song.solo >> k) & 1u)
                cv_text(6, y + 18, &FONT_S, "S", C_OK);
        }
    }
}
static uint32_t mx_static_sig(void)
{
    uint32_t i, sig = hu(hu(hu(0x3A1u, settings.palette), mx_row()), mxd.first);
    char en[10], pr[16];
    for (i = mxd.first; i < MXR_N && i < (uint32_t)mxd.first + MXL_N; i++) {
        sig = hs(hu(sig, mx_col(i)), mx_code(i));
        mx_names(i, en, pr);
        sig = hs(hs(sig, en), pr);
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
    cv_begin(MXL_VW, MXL_VH, C_LINE);
    cv_rect(0, 0, (int32_t)pk, MXL_VH, pk > MXL_VW - 8u ? C_ERR : r >= MXR_LANE0 ? mx_col(r) : C_OK);
    if (gr)
        cv_rect(MXL_VW - (int32_t)gr, 0, (int32_t)gr, MXL_VH, C_WARN);   /* the reduction, from the right */
    cv_blit(MXL_VX, (uint32_t)(OP_PY + y + MXL_VY));
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
    uint32_t len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP), at = t->seq_idx % len, page = at & ~15u, i, sig, np;
    uint16_t c = mx_col(r), bg = r == mx_row() ? col_shade(c, 3u) : r >= MXR_LANE0 ? C_BLACK : OP_SURF;
    if (r == MXR_MASTER)
        return;
    sig = hu(hu(hu(0x57u, song.playing ? at : 64u + page), len), bg);
    for (i = 0; i < 16u; i++)
        sig = sig * 3u + mx_on(r, page + i);
    if (sig == mxd.stp[r])
        return;
    mxd.stp[r] = sig;
    cv_begin(16u * MXL_SP, MXL_SH + 4, bg);
    for (i = 0; i < 16u; i++) {
        uint32_t s = page + i;
        if (s >= len)
            continue;
        cv_rect((int32_t)i * MXL_SP, 0, MXL_SP - 2, MXL_SH, song.playing && s == at ? C_WHITE : mx_on(r, s) ? c : C_LINE);
    }
    np = (len + 15u) / 16u;
    if (np > 1u)                                        /* the pattern's pages: a thin bar each, the one playing lit */
        for (i = 0; i < np; i++)
            cv_rect((int32_t)(i * 16u * MXL_SP / np), MXL_SH + 2, (int32_t)(16u * MXL_SP / np) - 2, 2,
                    i == page / 16u ? C_WHITE : C_DIM);
    cv_blit(MXL_VX, (uint32_t)(OP_PY + y + MXL_SY));
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
    for (i = mxd.first; i < MXR_N && i < (uint32_t)mxd.first + MXL_N; i++) {
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
