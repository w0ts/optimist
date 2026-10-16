/* SPDX-License-Identifier: GPL-3.0-only */
/* The horizontal mixer's rows (op_mixer.c), in the panel under the cards. The mixer shows ONE group at a time, the one
 * the cursor is in (the user, 2026-10-11: MASTER alone, or the four channels, or the drum sounds); ALGORITHM walks the
 * rows as before and the view follows it, drawn in place (cv_tall over the panel, no clear to black):
 *   MASTER     past T1 up: the master alone on the whole panel (164 px): its name, a tall VU meter (the level from the
 *              left, its peak falling) over a dB scale, the compressor's gain reduction as an amber bar from the right
 *              with its dB (THRS or CEIL set: mc_gr_view), and under it the FX returns' meters, REV DLY CHO (those
 *              built; the user, 2026-10-12: the four channels' meters here were "useless", the channels' rows show
 *              them): what the effects give back to the master (fx.c fx_ret_tap, a sample a block)
 *   CHANNELS   T1 T2 T3 DR, the four rows of 41 px (the mixer keeps the 1x4 cards, op_layers.c
 *              cards_2x2):
 *                x 0..21    a colour chip and the row's code (T1 T2 T3 DR), its M / S badge under it; the selected
 *                           row's full name is the header's ("T2 mix", op_draw.c head_title)
 *                x 28..237  three lines. The text line (y +1): the engine's name in the row's colour (ANALOG, TRIO,
 *                           DRUMS), then the preset (the kit for DR, a user preset's name), cut to the room left; under
 *                           it (y +20, 6 px) the row's sequence, as SLOOP's TRACKS screen: the 16 steps of the page
 *                           playing (13 px a step: the steps set in the row's colour, past LEN empty, the playhead
 *                           white), and under the steps a thin bar a page when LEN > 16 (the one playing lit); DR all
 *                           the lanes merged; at the foot (y +34) the VU meter, a 3 px bar: the level filling from the
 *                           left, its peak falling; the compressor's gain reduction pushing in from the right in amber,
 *                           3 px a dB, only on a row whose compressor works (a part's COMP insert: fx.c tcomp_gr_q4)
 *   SOUNDS     past DR: the 16 drum lanes at once, two columns of eight (lanes 1-8 left, 9-16 right), the ACCENT lane a
 *              ninth row across both (its steps as a channel's, no meter: it has no sound), 120 x 18 px a
 *              lane: its colour chip and code (BD SD CH ...; muted: the code in the M badge's colour), its own hits
 *              on the 16 steps of the page playing (6 px a step) and its meter (its hits: drums.hits, full at the hit and falling over 12 frames; the drum voices
 *              have no level of their own to read). The header names the lane ("Snare mix")
 * The selected row framed in its colour, its ground tinted. No pan, send or insert forms on the rows: the cards show the
 * selected row's values. Each row's meter and steps are canvases of their own, drawn again only when they change. */
#define MXL_N 4u                        /* the channels a screen */
#define MXL_H ((int32_t)OP_PH / (int32_t)MXL_N)   /* a channel's row: 41 px (the panel 164, the mixer's cards 1x4; over the
                                         * help band 36: the panel 146) */
#define MXL_VX 28                       /* a channel's meter and steps: the width left of the code */
#define MXL_VW 210
#define MXL_TY 1                        /* the text line: the engine, then the preset (the user: "each track shows the synth
                                         * name and the preset", so the meter and the steps went thin) */
#define MXL_SY (MXL_H - 21)             /* the steps in their row: small cells, 6 px, the page bars under them (20; 15) */
#define MXL_SH 6
#define MXL_SP 13                       /* a step's pitch, its cell 11 */
#define MXL_VY (MXL_H - 7)              /* the meter in its row: a thin bar, over the row's rule (34; 29) */
#define MXL_VH 3
#define MX_GR_PX 3                      /* the reduction: px a dB */
#define MXS_ROWS 8u                     /* SOUNDS: eight lanes a column, two columns */
#define MXS_NROW (MXS_ROWS + (uint32_t)ACC_UI)   /* ... and under them, the ACCENT lane's row across both, a ninth row of the
                                         * same height (the audit's #34; core.h DRUM_UILANES) */
#define MXS_W 120
#define MXS_VX 22                       /* a lane's steps and meter, from its cell's left */
#define MXS_SP 6                        /* a step's pitch, its cell 5 */
#define MXS_SY 3
#define MXS_SH 7
#define MXS_VW 94
#define MXS_VH 3
#define MXM_X 8                         /* MASTER: the tall meter */
#define MXM_W 224
#define MXM_VY 24
#define MXM_VH 14
#define MXM_GX 32                       /* its reduction bar, "GR" at its left */
#define MXM_GW 200
#define MXM_GY 68
#define MXM_GH 6
#define MXM_CY 82                       /* the FX returns' meters: a line each */
#define MXM_CP 22
#define MXM_CH 10                       /* (a return's meter: as thick as its name reads) */
#define MXM_CV 3                        /* its top under its name's line */
#define MX_LV 255u                      /* a level: 0 .. 255, the meter's whole width */
enum { MV_MASTER, MV_CH, MV_SOUNDS, MV_USB };   /* (MV_USB: the USB input's row alone, past the drum sounds) */
static struct {
    uint8_t first, last;                /* the rows in view: first .. last - 1 (the cursor's group) */
    uint8_t vu[MXR_N], gr[MXR_N];       /* the meters as drawn (0xFF: again) */
    uint32_t stp[MXR_N];                /* the steps as drawn (a signature) */
    uint8_t pk[MXR_N];                  /* the meters' peaks (0 .. MX_LV), falling */
    uint8_t mgr;                        /* MASTER alone: its reduction as drawn, dB x 4 (0xFF: again) */
    uint8_t pk_first;                   /* the group the peaks are of (mx_draw) */
} mxd = {.first = MXR_T1, .last = MXR_LANE0, .pk_first = MXR_T1};
typedef struct {
    int32_t x, y, w, h;                 /* a row's place in the panel */
    int32_t sx, sy, sp, sh;             /* its steps: x, y in the row, a step's pitch and height */
    int32_t vx, vy, vw, vh;             /* its meter */
} mrect_t;

static uint32_t mx_smuted(uint32_t r)                   /* row r's sequencer muted (seq_mute.c): its steps dimmed, a badge */
{
    return r == MXR_MASTER || mx_is_usb(r) ? 0u : r >= MXR_LANE0 ? (uint32_t)smute_lane_on(r - MXR_LANE0) :
           (uint32_t)smute_trk_on(mx_trk_of(r));
}
static uint32_t mx_lmuted(uint32_t r) { return mx_is_lane(r) && !mx_is_acc(r) && dlm_muted(r - MXR_LANE0); }   /* (the accent: no mute) */
static uint32_t mx_mode(uint32_t r) { return r == MXR_MASTER ? MV_MASTER : r < MXR_LANE0 ? MV_CH : mx_is_usb(r) ? MV_USB : MV_SOUNDS; }
static void mx_view(void)                               /* the rows in view: the cursor's group */
{
    uint32_t m = mx_mode(mx_row());
    mxd.first = (uint8_t)(m == MV_MASTER ? MXR_MASTER : m == MV_CH ? MXR_T1 : m == MV_USB ? MXR_LN : MXR_LANE0);
    mxd.last = (uint8_t)(m == MV_MASTER ? MXR_T1 : m == MV_CH ? MXR_LANE0 : m == MV_USB ? MXR_N : MXR_LN);
}
static void mx_rect(uint32_t r, mrect_t *q)             /* row r's place in its group's view */
{
    memset(q, 0, sizeof *q);
    if (r == MXR_MASTER) {
        q->w = 240, q->h = OP_PH;
        q->vx = MXM_X, q->vy = MXM_VY, q->vw = MXM_W, q->vh = MXM_VH;
    } else if (r < MXR_LANE0 || mx_is_usb(r)) {
        q->y = mx_is_usb(r) ? 0 : (int32_t)(r - MXR_T1) * MXL_H, q->w = 240, q->h = MXL_H;   /* (the USB input: a channel's row,
                                                         * alone, no steps) */
        q->sx = MXL_VX, q->sy = MXL_SY, q->sp = MXL_SP, q->sh = MXL_SH;
        q->vx = MXL_VX, q->vy = MXL_VY, q->vw = MXL_VW, q->vh = MXL_VH;
    } else {
        uint32_t l = r - MXR_LANE0, row = mx_is_acc(r) ? MXS_ROWS : l % MXS_ROWS;
        q->x = mx_is_acc(r) ? 0 : (int32_t)(l / MXS_ROWS) * MXS_W;   /* (the ACCENT lane: the ninth row, across both columns) */
        q->y = (int32_t)row * OP_PH / (int32_t)MXS_NROW;
        q->h = (int32_t)(row + 1u) * OP_PH / (int32_t)MXS_NROW - q->y;
        q->w = mx_is_acc(r) ? 240 : MXS_W;
        q->sx = MXS_VX, q->sy = MXS_SY, q->sp = MXS_SP, q->sh = MXS_SH;
        q->vx = MXS_VX, q->vy = q->h - 4, q->vw = MXS_VW, q->vh = MXS_VH;   /* (the meter over the row's rule: 18 px rows, 16) */
        if (mx_is_acc(r)) {                             /* (no sound, no meter: its steps over the width, as a channel's) */
            q->sx = MXL_VX, q->sp = MXL_SP, q->sh = q->h - 6;
            q->vw = 0;
        }
    }
}
static const char *mx_code(uint32_t r)                  /* the row's code at its left */
{
    return r == MXR_MASTER ? "M" : mx_is_usb(r) ? "U" : r >= MXR_LANE0 ? lane_code(r - MXR_LANE0) : trk_tag(mx_trk_of(r));
}
/* the row's sound as two words: the engine (DRUMS for the drum track, MASTER, a lane's name) and the preset (the kit
 * for the drum track, a lane's own sound, a user preset's name); each cut to the room it has */
static void mx_names(uint32_t r, char *eng, char *pre)
{
    pre[0] = 0;
    if (r == MXR_MASTER) {
        str_cpy(eng, "MASTER", 9);
#if FELUCCA_USB_AUDIO
    } else if (mx_is_usb(r)) {                          /* (the computer's audio: streaming or not) */
        str_cpy(eng, "USB IN", 9);
        str_cpy(pre, ut_live ? "streaming" : "no stream", 14);
#endif
    } else if (r >= MXR_LANE0) {
        str_cpy(eng, lane_name(r - MXR_LANE0), 9);
        if (!mx_is_acc(r))
            str_cpy(pre, lane_sound_name(r - MXR_LANE0), 14);
    } else {
        uint32_t k = mx_trk_of(r);
        str_cpy(eng, is_drum(&trk[k]) ? "DRUMS" : ENGINES[trk[k].eng_req % NENGINES]->name, 9);
        snd_name(k, pre);
    }
}
static void mx_vu_paint(uint32_t r, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t pk, uint32_t gr);
static void mx_steps_paint(uint32_t r, int32_t x, int32_t y);
static uint32_t mx_gr_px(uint32_t r, int32_t w);
static void mx_paint_ground(uint32_t i, const mrect_t *q, uint16_t c)
{
    if (i == mx_row()) {
        cv_rect(q->x, q->y, q->w, q->h - 1, col_shade(c, 3u));
        cv_frame(q->x, q->y, q->w, q->h - 1, c);
    } else {
        cv_rect(q->x, q->y + q->h - 1, q->w, 1, C_LINE);   /* (black, a rule under it: no grey ground) */
    }
}
/* the MASTER page's return lines, top down: A B C, those holding something (fx_slots.c fxs_card, the one order; ret:
 * each one's line, fx.c fx_ret_pk's index) */
static uint32_t mx_rets(uint8_t *ret)
{
    uint32_t n = 0, k, t;
    for (k = 0; k < 3u; k++)
        if ((t = fxs_card(k)) != FXT_NONE)
            ret[n++] = (uint8_t)FXS_LN(t);
    return n;
}
/* MASTER alone: its name, the tall meter's dB scale (a tick each 6 dB, 0 -12 -24 -48 named), "GR", the returns' names */
static void mx_paint_master(void)
{
    static const int8_t DB[4] = {0, -12, -24, -48};
    uint32_t i, n;
    uint8_t ret[3];
    char b[8];
    cv_rect(0, OP_PH - 1, 240, 1, C_LINE);
    cv_rect(2, 3, 3, OP_PH - 7, C_HI);                  /* the colour chip */
    cv_text(MXM_X, 2, &FONT_S, "MASTER", C_WHITE);
    for (i = 0; i <= 10u; i++)                          /* the scale: lg 5 .. 15 (op_draw.c meter_h), 6 dB a tick */
        cv_rect(MXM_X + (int32_t)((i * 4u + 4u) * MXM_W / 44u) - 1, MXM_VY + MXM_VH + 1, 1, 3, C_LINE);
    for (i = 0; i < 4u; i++) {
        int32_t x = MXM_X + (int32_t)(((15u - (uint32_t)(-DB[i] / 6) - 5u) * 4u + 4u) * MXM_W / 44u);
        fmt_int(b, DB[i]);
        cv_text(x - text_w(&FONT_S, b) + (i ? text_w(&FONT_S, b) / 2 : 0), MXM_VY + MXM_VH + 4, &FONT_S, b, C_DIM);
    }
    cv_text(MXM_X, MXM_GY - 5, &FONT_S, "GR", C_DIM);
    n = mx_rets(ret);
    for (i = 0; i < n; i++)                             /* (the type each return holds: a send's, or the other one's) */
        cv_text(MXM_X, MXM_CY + (int32_t)i * MXM_CP, &FONT_S, fxs_line_name(ret[i]), C_HI);
    mx_vu_paint(MXR_MASTER, MXM_X, MXM_VY, MXM_W, MXM_VH, mxd.pk[MXR_MASTER], 0);
    for (i = 0; i < n; i++)                             /* (a return's meter state: the row T1 + its line's, unseen here) */
        mx_vu_paint(MXR_T1 + i, MXM_GX, MXM_CY + (int32_t)i * MXM_CP + MXM_CV, MXM_GW, MXM_CH, mxd.pk[MXR_T1 + i], 0);
}
static void mx_paint_row(uint32_t i)                    /* a channel's or a lane's row */
{
    char en[10], pr[16], b[16];
    int32_t x;
    mrect_t q;
    uint16_t c = mx_col(i);
    uint16_t cc = mx_lmuted(i) ? C_WARN : i >= MXR_LANE0 && mx_soloed(i) ? C_OK : i >= MXR_LANE0 && mx_smuted(i) ? C_GRAY : i == mx_row() ? C_WHITE : c;   /* (a muted lane: its code in the M badge's colour) */
    int32_t cy = i >= MXR_LANE0 && !mx_is_usb(i) ? 2 : MXL_TY;
    mx_rect(i, &q);
    cy += q.y;
    mx_paint_ground(i, &q, c);
    cv_rect(q.x + 2, q.y + 3, 3, q.h - 7, c);           /* the colour chip */
    cv_text(q.x + 6, cy, &FONT_S, mx_code(i), cc);
    if (mx_muted(i))                                    /* (a muted row: its name struck through, the user, 2026-10-15) */
        strike_text(q.x + 6, cy, &FONT_S, mx_code(i), cc);
    if (mx_is_usb(i)) {                                 /* the USB input: its name and whether it streams, its M badge */
        uint16_t pc = i == mx_row() ? C_WHITE : C_GRAY;
        mx_names(i, en, pr);
        x = cv_text(MXL_VX, q.y + MXL_TY, &FONT_S, en, c);
        if (mx_muted(i))
            strike_text(MXL_VX, q.y + MXL_TY, &FONT_S, en, c);
        cv_text(x + 8, q.y + MXL_TY, &FONT_S, pr, pc);
        if (mx_muted(i))
            cv_text(6, q.y + 18, &FONT_S, "M", C_WARN);
        else if (mx_soloed(i))
            cv_text(6, q.y + 18, &FONT_S, "S", C_OK);
    } else if (i < MXR_LANE0) {                         /* a channel: its engine and preset, its M / S badge */
        mx_names(i, en, pr);
        x = cv_text(MXL_VX, q.y + MXL_TY, &FONT_S, cut(b, en, 8), c);      /* ANALOG, in the row's colour ... */
        if (mx_muted(i))
            strike_text(MXL_VX, q.y + MXL_TY, &FONT_S, b, c);
        if (pr[0]) {
            uint16_t pc = i == mx_row() ? C_WHITE : C_GRAY;
            cv_text(x + 8, q.y + MXL_TY, &FONT_S, cut(b, pr, (uint32_t)(238 - x - 8) / 8u), pc);
            if (mx_muted(i))
                strike_text(x + 8, q.y + MXL_TY, &FONT_S, b, pc);
        }
        if (trk[mx_trk_of(i)].p[P_MUTE])
            cv_text(6, q.y + 18, &FONT_S, "M", C_WARN);
        else if ((song.solo >> mx_trk_of(i)) & 1u)
            cv_text(6, q.y + 18, &FONT_S, "S", C_OK);
        if (mx_smuted(i))                               /* (the sequencer muted: an X beside, the steps grey) */
            cv_text(15, q.y + 18, &FONT_S, "X", C_GRAY);
    }
    mx_steps_paint(i, q.x + q.sx, q.y + q.sy);          /* the live parts too, as they stand: never a row without them */
    if (q.vw)
        mx_vu_paint(i, q.x + q.vx, q.y + q.vy, q.vw, q.vh, mxd.pk[i], mx_gr_px(i, q.vw));
}
static void mx_paint(void)                              /* the group in view (cv_tall) */
{
    uint32_t i;
    if (mxd.first == MXR_MASTER) {
        mx_paint_master();
        return;
    }
    if (mxd.first == MXR_LANE0)
        cv_rect(MXS_W - 1, 0, 1, (int32_t)MXS_ROWS * OP_PH / (int32_t)MXS_NROW, C_LINE);   /* (the two columns) */
    for (i = mxd.first; i < mxd.last; i++)
        mx_paint_row(i);
}
static uint32_t mx_static_sig(void)
{
    uint32_t i, sig = hu(hu(hu(0x3A1u, settings.palette), mx_row()), mxd.first);
    char en[10], pr[16];
    for (i = mxd.first; i < mxd.last; i++) {
        sig = hs(hu(sig, mx_col(i)), mx_code(i));
        if (mx_is_usb(i)) {                             /* (the USB input: muted, streaming) */
            mx_names(i, en, pr);
            sig = hs(hu(sig, mx_muted(i) * 2u + mx_soloed(i)), pr);
            continue;
        }
        if (i >= MXR_LANE0) {                           /* (a lane: its code, muted or not; the kit is the header's) */
            sig = hu(sig, (mx_lmuted(i) * 2u + mx_smuted(i)) * 2u + mx_soloed(i));
            continue;
        }
        mx_names(i, en, pr);
        sig = hs(hs(sig, en), pr);
        if (i >= MXR_T1 && i <= MXR_DR)
            sig = hu(sig, ((uint32_t)trk[mx_trk_of(i)].p[P_MUTE] * 2u + ((song.solo >> mx_trk_of(i)) & 1u)) * 2u + mx_smuted(i));
    }
    return sig;
}

/* ---- the live parts */
static uint32_t mx_vu_lv(int32_t pk)                    /* a peak (32767 = 0 dBFS) as a level, 0 .. MX_LV: 6 dB a step */
{
    return meter_h(pk) * MX_LV / METER_H;
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
        return MST_ON(FXT_DYN) && (song.g[G_CTHR] || song.g[G_CCEIL]) ? (uint32_t)clamp(-(int32_t)mc_gr_view * 4, 0, 127) : 0u;
    if (r >= MXR_T1 && r < MXR_DR) {
        const track_t *t = &trk[r - MXR_T1];
        return fx_on(t) && INS_ON(r - MXR_T1, FXT_COMP) && t->p[P_TCOMP] > 0 ? tcomp_gr_q4(r - MXR_T1) : 0u;
    }
#else
    (void)r;
#endif
    return 0;
}
static uint32_t mx_level(uint32_t r, const uint32_t *trk_lv)   /* row r's level now, 0 .. MX_LV (before the fall) */
{
    if (r == MXR_MASTER)
        return mx_vu_lv(mx_master_pk());
    if (mx_is_acc(r))
        return 0u;                                      /* (no sound: no hits to light) */
#if FELUCCA_USB_AUDIO
    if (mx_is_usb(r)) {                                 /* (its peak since the last frame, fx.c ut_mix) */
        int32_t pk = ut_pk;
        ut_pk = 0;
        return mx_vu_lv(pk);
    }
#endif
    if (r >= MXR_LANE0)
        return MX_LV * mx.lit[r - MXR_LANE0] / 12u;
    return trk_lv[mx_trk_of(r)];
}
static uint32_t mx_gr_px(uint32_t r, int32_t w)         /* row r's reduction as a width, within w */
{
    uint32_t gr = mx_gr_q4(r) * MX_GR_PX / 4u;
    return gr > (uint32_t)w ? (uint32_t)w : gr;
}
/* row r's meter at x, y of the canvas, w x h: its level pk (0 .. MX_LV) from the left, the reduction gr px from the
 * right (its own canvas, or the band: mx_paint draws the live parts with the rows, so a row drawn again never shows
 * them gone) */
static void mx_vu_paint(uint32_t r, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t pk, uint32_t gr)
{
    int32_t px = (int32_t)(pk * (uint32_t)w / MX_LV);
    cv_rect(x, y, w, h, C_BLACK);
    cv_rect(x, y + h - 1, w, 1, C_LINE);                /* the track: a line on black */
    cv_rect(x, y, px, h, pk > MX_LV - 10u ? C_ERR : r >= MXR_LANE0 ? mx_col(r) : C_OK);   /* (the returns' meters too: the channels' green) */
    if (gr)
        cv_rect(x + w - (int32_t)gr, y, (int32_t)gr, h, C_WARN);   /* the reduction, from the right */
}
static void mx_vu_at(uint32_t r, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t lv, uint32_t gr)
{
    uint32_t pk = mxd.pk[r];
    pk = lv > pk ? lv : pk > 5u ? pk - 5u : 0u;         /* (falls ~4 px a frame on a channel's row) */
    mxd.pk[r] = (uint8_t)pk;
    if (pk == mxd.vu[r] && gr == mxd.gr[r])
        return;
    mxd.vu[r] = (uint8_t)pk;
    mxd.gr[r] = (uint8_t)gr;
    cv_begin((uint32_t)w, (uint32_t)h, C_BLACK);
    mx_vu_paint(r, 0, 0, w, h, pk, gr);
    cv_blit((uint32_t)x, (uint32_t)(OP_PY + y));
}
/* MASTER alone: its reduction bar and the dB ("off" while THRS and CEIL are 0), a canvas of their own */
static void mx_master_gr(void)
{
    uint32_t q4 = mx_gr_q4(MXR_MASTER), gr = mx_gr_px(MXR_MASTER, MXM_GW);
    char b[12];
    if (q4 == mxd.mgr)
        return;
    mxd.mgr = (uint8_t)q4;
    cv_begin(240 - MXM_GX, 16, C_BLACK);
    cv_rect(0, 5 + MXM_GH - 1, MXM_GW, 1, C_LINE);
    if (q4) {
        cv_rect(MXM_GW - (int32_t)gr, 5, (int32_t)gr, MXM_GH, C_WARN);
        b[0] = '-';
        fmt_int(b + 1, (int32_t)(q4 / 4u));
        str_cpy(b + str_len(b), " dB", sizeof b - str_len(b));
        cv_text(0, 0, &FONT_S, b, C_WARN);
    } else if (FELUCCA_MASTER_COMP) {
        cv_text(0, 0, &FONT_S, "off", C_DIM);
    }
    cv_blit(MXM_GX, (uint32_t)(OP_PY + MXM_GY - 5));
}
static uint32_t mx_on(uint32_t r, uint32_t i)           /* step i is set on row r */
{
    const track_t *t = &trk[mx_trk_of(r)];
    if (r >= MXR_LANE0)
        return dstep_has(&t->dstep[i % NSTEP], r - MXR_LANE0);
    return trk_on_step(mx_trk_of(r), i);
}
/* row r's steps at x, y of the canvas (on the row's ground): the page playing, the playhead white, the pages' bars (a
 * channel's row) */
static void mx_steps_paint(uint32_t r, int32_t x, int32_t y)
{
    if (mx_is_usb(r))
        return;                                         /* (no sequence) */
    const track_t *t = &trk[mx_trk_of(r)];
    uint32_t len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP), at = t->seq_idx % len, page = at & ~15u, i, np;
    uint16_t c = mx_smuted(r) ? C_DIM : mx_col(r);   /* (the sequencer muted: the steps grey, no playhead) */
    mrect_t q;
    if (r == MXR_MASTER)
        return;
    mx_rect(r, &q);
    for (i = 0; i < 16u; i++) {
        uint32_t s = page + i;
        if (s >= len)
            continue;
        uint16_t sc = song.playing && s == at && !mx_smuted(r) ? C_WHITE
                    : mx_on(r, s) ? (!mx_smuted(r) && op_step_auto(t, s, r >= MXR_LANE0 ? r - MXR_LANE0 : 0xFFu) ? C_AUTO : c) : C_BLACK;
        if (r >= MXR_LANE0 && sc == C_BLACK)            /* (a lane's empty step: a foot line, no box: 16 small boxes a */
            cv_rect(x + (int32_t)i * q.sp, y + q.sh - 1, q.sp - 1, 1, C_LINE);   /* lane read as noise) */
        else
            cv_box(x + (int32_t)i * q.sp, y, q.sp - 2 + (r >= MXR_LANE0), q.sh, sc);
    }
    np = (len + 15u) / 16u;
    if (np > 1u && r < MXR_LANE0)                       /* the pattern's pages: a thin bar each, the one playing lit */
        for (i = 0; i < np; i++)
            cv_rect(x + (int32_t)(i * 16u * MXL_SP / np), y + MXL_SH + 2, (int32_t)(16u * MXL_SP / np) - 2, 2,
                    i == page / 16u ? C_WHITE : C_DIM);
}
static void mx_steps(uint32_t r)
{
    if (mx_is_usb(r))
        return;
    const track_t *t = &trk[mx_trk_of(r)];
    uint32_t len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP), at = t->seq_idx % len, page = at & ~15u, i, sig;
    uint16_t c = mx_col(r), bg = r == mx_row() ? col_shade(c, 3u) : C_BLACK;
    mrect_t q;
    if (r == MXR_MASTER)
        return;
    mx_rect(r, &q);
    sig = hu(hu(hu(hu(0x57u, song.playing ? at : 64u + page), len), bg), mx_smuted(r));
    for (i = 0; i < 16u; i++)
        sig = sig * 5u + mx_on(r, page + i) * 2u + (mx_on(r, page + i) && op_step_auto(t, page + i, r >= MXR_LANE0 ? r - MXR_LANE0 : 0xFFu));
    if (sig == mxd.stp[r])
        return;
    mxd.stp[r] = sig;
    cv_begin((uint32_t)(16 * q.sp), (uint32_t)(q.sh + (r < MXR_LANE0 ? 4 : 0)), bg);
    mx_steps_paint(r, 0, 0);
    cv_blit((uint32_t)(q.x + q.sx), (uint32_t)(OP_PY + q.y + q.sy));
}
static void mx_draw(void)
{
    uint32_t i, sig, trk_lv[NTRK], ret_lv[3], n;
    uint8_t ret[3];
    for (i = 0; i < NTRK; i++)                          /* (taken every frame, seen or not: no stale peak) */
        trk_lv[i] = mx_vu_lv(meter_ui_take(i));
    n = mx_rets(ret);
    for (i = 0; i < n; i++)                             /* (the returns too) */
        ret_lv[i] = mx_vu_lv(meter_ret_take(ret[i]));
    mx_view();
    sig = mx_static_sig();
    if (sig != ui.sig[2]) {
        if (mxd.first != mxd.pk_first) {                /* another group: MASTER's lines are the returns, kept in the */
            memset(mxd.pk, 0, sizeof mxd.pk);           /* channels' places: no peak carried from one to the other */
            mxd.pk_first = mxd.first;
        }
        ui.sig[2] = sig;
        cv_tall(OP_PY, OH_BODY, C_BLACK, mx_paint);
        memset(mxd.vu, 0xFF, sizeof mxd.vu);
        memset(mxd.gr, 0xFF, sizeof mxd.gr);
        memset(mxd.stp, 0, sizeof mxd.stp);
        mxd.mgr = 0xFF;
    }
    if (ui.overlay == 2u)
        return;                                         /* (under a toast: the rows wait) */
    if (mxd.first == MXR_MASTER) {                      /* MASTER alone: its meter, its reduction, the returns' meters */
        mx_vu_at(MXR_MASTER, MXM_X, MXM_VY, MXM_W, MXM_VH, mx_level(MXR_MASTER, trk_lv), 0);
        mx_master_gr();
        for (i = 0; i < n; i++)
            mx_vu_at(MXR_T1 + i, MXM_GX, MXM_CY + (int32_t)i * MXM_CP + MXM_CV, MXM_GW, MXM_CH, ret_lv[i], 0);
        return;
    }
    for (i = mxd.first; i < mxd.last; i++) {
        mrect_t q;
        mx_rect(i, &q);
        if (q.vw)                                       /* (the ACCENT lane: no meter) */
            mx_vu_at(i, q.x + q.vx, q.y + q.vy, q.vw, q.vh, mx_level(i, trk_lv), mx_gr_px(i, q.vw));
        mx_steps(i);
    }
}
static void mx_redraw(void)                             /* the live parts again (an overlay went) */
{
    memset(mxd.vu, 0xFF, sizeof mxd.vu);
    memset(mxd.gr, 0xFF, sizeof mxd.gr);
    memset(mxd.stp, 0, sizeof mxd.stp);
    mxd.mgr = 0xFF;
}
