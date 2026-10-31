/* SPDX-License-Identifier: GPL-3.0-only */
/* The horizontal mixer's rows (op_mixer.c), in the panel under the 1x4 cards (MASTER's too since 2026-10-30). ONE screen
 * (the user, 2026-10-30: "i told to put ON ONE screen the master at the bottom, and the return track above it, on the same
 * screen. AND not to move the screen"):
 *   the block  pinned at the panel's foot, always drawn, never moved: RET A, RET B, RET C, then MASTER the last line, 14 px a
 *              line (MXB_H): the chip, the code (A B C M), the effect's name (REV DLY CHO or the type the return holds; "----"
 *              empty; MASTER), its meter from x 96 (a return's: what it gives back to the master, fx.c fx_ret_tap, after its
 *              POST and level; MASTER's: the master's peak, the compressor's reduction pushing in from the right in amber); a
 *              muted return struck, its code in the M badge's colour. A rule over the block
 *   the upper  the area above it shows one group of the other rows, the one the cursor was last in (the cursor in the block:
 *   area       the group stays where it was, nothing moves):
 *     CHANNELS   T1 T2 T3 DR, four rows (MXL_H: 22 px over the help band, 26 without): the chip, the code, the engine's name in
 *                the row's colour and the preset (the kit for DR), the 16 steps of the page playing in a 4 px strip (shaded by
 *                level as on STEP, grey when muted, the playhead white), the VU meter a 2 px bar at the foot with the part's
 *                compressor reduction from the right (fx.c tcomp_gr_q4)
 *     SOUNDS     the drum lanes and ACCENT, 13 px a lane (code, hits on the page's 16 steps, the hit meter): as many as fit (6
 *                over the help band, 8 without), the window scrolling with the cursor (mxd.lf)
 *     USB IN     its row alone, at the top
 * The selected row framed in its colour. Each row's meter and steps are canvases of their own, drawn again only when they
 * change. */
#define MXB_N 4u                        /* the pinned block: RET A B C, MASTER */
#define MXB_H 14                        /* a line of it */
#define MXB_Y ((int32_t)OP_PH - (int32_t)MXB_N * MXB_H)   /* its top in the panel (the rule one row over it) */
#define MXB_NX 22                       /* a line's name */
#define MXB_VX 96                       /* its meter */
#define MXB_VW 140
#define MXB_VY 5
#define MXB_VH 4
#define MXU_H (MXB_Y - 1)               /* the upper area */
#define MXL_N 4u                        /* the channels a screen */
#define MXL_H (MXU_H / (int32_t)MXL_N)  /* a channel's row: 22 px over the help band, 26 without */
#define MXL_VX 28                       /* a channel's meter and steps: the width left of the code */
#define MXL_VW 210
#define MXL_TY (-2)                     /* the text line (its 16 px cell from 2 px over the row: the caps' ink inside it) */
#define MXL_SY (MXL_H - 8)              /* the steps in their row: a 4 px strip over the meter */
#define MXL_SH 4
#define MXL_SP 13                       /* a step's pitch, its cell 11 */
#define MXL_VY (MXL_H - 3)              /* the meter: a thin bar, over the row's rule */
#define MXL_VH 2
#define MX_GR_PX 3                      /* the reduction: px a dB */
#define MXS_RH 13                       /* SOUNDS: a lane's row */
#define MXS_VIS ((uint32_t)(MXU_H / MXS_RH))   /* the lanes shown at once (the window: mxd.lf) */
#define MXS_SY 2                        /* a lane's steps: their y in the row (the channels' pitch, MXL_SP), the meter under them */
#define MXS_VH 2
#define MXS_TY (-2)                     /* a lane's code: its 16 px cell from 2 px over the row (the caps' ink inside it) */
#define MX_LV 255u                      /* a level: 0 .. 255, the meter's whole width */
#define MX_PH_HOLD 90u                  /* the peak-hold marker holds this many frames (~1.5 s at 60 a second), then falls */
#define MX_PH_W 2                       /* ... a marker: this wide, the meter's height */
enum { MV_CH, MV_SOUNDS, MV_USB };      /* the upper area's group */
static struct {
    uint8_t first, last;                /* the upper rows in view: first .. last - 1 */
    uint8_t ug;                         /* the upper area's group (MV_*): the cursor's when it is there, else as it was */
    uint8_t lf;                         /* SOUNDS: the first lane in view */
    uint8_t vu[MXR_N], gr[MXR_N];       /* the meters as drawn (0xFF: again) */
    uint32_t stp[MXR_N];                /* the steps as drawn (a signature, the playhead left out) */
    uint8_t sta[MXR_N];                 /* the playhead as drawn (0xFF: none) */
    uint8_t pk[MXR_N];                  /* the meters' peaks (0 .. MX_LV), falling */
    uint16_t ph[MXR_N];                 /* the peak-hold markers (level x 256): jump to a new peak, hold, then fall (mx_peak) */
    uint8_t phd[MXR_N], vph[MXR_N];     /* ... frames left of the hold; the marker's level as drawn (0xFF: again) */
    uint8_t pk_first;                   /* the upper rows the peaks are of (mx_draw) */
#if FELUCCA_ED_MIRROR
    uint8_t mvu[MXR_N], mgv[MXR_N];     /* the meters as the mirror's client has them (0xFF: again) */
    uint8_t mvph[MXR_N];                /* ... and their peak-hold markers */
    uint8_t mir;                        /* this frame the meters may go to it (io/editor/ed_mirror.c mir_meters_due) */
#endif
} mxd = {.first = MXR_T1, .last = MXR_LANE0, .pk_first = MXR_T1};
static uint32_t mx_rlv[3];             /* the returns' lines' levels this frame (mx_draw, meter_ret_take) */
typedef struct {
    int32_t x, y, w, h;                 /* a row's place in the panel */
    int32_t sx, sy, sp, sh;             /* its steps: x, y in the row, a step's pitch and height */
    int32_t vx, vy, vw, vh;             /* its meter */
} mrect_t;

static uint32_t mx_smuted(uint32_t r) { return !mx_is_usb(r) && mx_muted(r); }   /* row r muted: its steps grey, no playhead */
static int mx_in_block(uint32_t r) { return mx_is_ret(r) || r == MXR_MASTER; }   /* RET A B C, MASTER: the pinned block */
static void mx_view(void)                               /* the upper area's rows in view */
{
    uint32_t r = mx_row();
    if (!mx_in_block(r))                                /* (the cursor in the block: the upper area stays as it was) */
        mxd.ug = (uint8_t)(r < MXR_LANE0 ? MV_CH : mx_is_usb(r) ? MV_USB : MV_SOUNDS);
    if (mxd.ug == MV_SOUNDS) {                          /* (the lanes' window: the cursor's lane in it) */
        uint32_t n = MXR_LN - MXR_LANE0, v = MXS_VIS < n ? MXS_VIS : n, lf = mxd.lf;
        if (mx_is_lane(r) && r - MXR_LANE0 < lf)
            lf = r - MXR_LANE0;
        if (mx_is_lane(r) && r - MXR_LANE0 >= lf + v)
            lf = r - MXR_LANE0 + 1u - v;
        lf = lf + v > n ? n - v : lf;
        mxd.lf = (uint8_t)lf;
        mxd.first = (uint8_t)(MXR_LANE0 + lf), mxd.last = (uint8_t)(MXR_LANE0 + lf + v);
    } else if (mxd.ug == MV_USB) {
        mxd.first = (uint8_t)MXR_LN, mxd.last = (uint8_t)(MXR_LN + 1u);
    } else {
        mxd.first = (uint8_t)MXR_T1, mxd.last = (uint8_t)MXR_LANE0;
    }
}
static void mx_rect(uint32_t r, mrect_t *q)             /* row r's place in the panel (an upper row out of view: its place as if
                                                         * the window were on it, off the area) */
{
    memset(q, 0, sizeof *q);
    q->w = 240;
    if (mx_in_block(r)) {
        q->y = MXB_Y + (int32_t)(r - MXR_RET0) * MXB_H, q->h = MXB_H;
        q->vx = MXB_VX, q->vy = MXB_VY, q->vw = MXB_VW, q->vh = MXB_VH;
    } else if (r < MXR_LANE0 || mx_is_usb(r)) {
        q->y = mx_is_usb(r) ? 0 : (int32_t)(r - MXR_T1) * MXL_H, q->h = MXL_H;   /* (the USB input: a channel's row, alone, no steps) */
        q->sx = MXL_VX, q->sy = MXL_SY, q->sp = MXL_SP, q->sh = MXL_SH;
        q->vx = MXL_VX, q->vy = MXL_VY, q->vw = MXL_VW, q->vh = MXL_VH;
    } else {
        int32_t row = (int32_t)(r - MXR_LANE0) - (int32_t)mxd.lf;   /* (the ACCENT lane: DRUM_ACC, the last row) */
        q->y = row * MXS_RH, q->h = MXS_RH;
        q->sx = MXL_VX, q->sy = MXS_SY, q->sp = MXL_SP, q->sh = q->h - 7;
        q->vx = MXL_VX, q->vy = q->h - 4, q->vw = MXL_VW, q->vh = MXS_VH;   /* (the meter over the row's rule) */
        if (mx_is_acc(r)) {                             /* (no sound, no meter: its steps taller) */
            q->sh = q->h - 5;
            q->vw = 0;
        }
    }
}
static const char *mx_code(uint32_t r)                  /* the row's code at its left */
{
    static const char *const RC[3] = {"A", "B", "C"};
    return r == MXR_MASTER ? "M" : mx_is_usb(r) ? "U" : mx_is_ret(r) ? RC[(r - MXR_RET0) % 3u] : mx_is_lane(r) ? lane_code(r - MXR_LANE0) :
           trk_tag(mx_trk_of(r));
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
    } else if (mx_is_ret(r)) {                          /* (a return: its effect, then its inserts PRE and POST) */
        uint32_t j = mx_ret_ln(r), a = j < 3u ? rp_ty[j][RP_PRE] : (uint32_t)FXT_NONE, b = j < 3u ? rp_ty[j][RP_POST] : (uint32_t)FXT_NONE;
        str_cpy(eng, j < 3u ? fxs_line_name(j) : "----", 9);
        if (a != FXT_NONE || b != FXT_NONE) {
            str_cpy(pre, a != FXT_NONE ? TP[FXT_AMT[a]].label : "-", 14);
            str_app(pre, " > ", 14);
            str_app(pre, b != FXT_NONE ? TP[FXT_AMT[b]].label : "-", 14);
        }
    } else if (r >= MXR_LANE0) {
        str_cpy(eng, lane_name(r - MXR_LANE0), 9);
        if (!mx_is_acc(r))
            str_cpy(pre, lane_sound_name(r - MXR_LANE0), 14);
    } else {
        uint32_t k = mx_trk_of(r);
        str_cpy(eng, is_drum(&trk[k]) ? "DRUMS" : trk_eng_name(&trk[k]), 9);
        snd_name(k, pre);
    }
}
static void mx_vu_paint(uint32_t r, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t pk, uint32_t gr, uint32_t ph);
static void mx_vu_band(uint32_t r, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t pk, uint32_t gr);
static void mx_steps_paint(uint32_t r, int32_t x, int32_t y);
static uint32_t mx_gr_px(uint32_t r, int32_t w);
/* the selected row: framed in its colour on black, no fill (the user, 2026-10-18: the tinted ground made it hard to read);
 * its code and preset white */
static int mx_in_grp(uint32_t i)                        /* row i one of the knobs' rows: the cursor's, across its group's */
{
    return i == mx_row() || (mx_across(mx_row()) && i >= mx_grp0(mx_row()) && i < mx_grp_end(mx_row()));
}
static void mx_paint_ground(uint32_t i, const mrect_t *q, uint16_t c)
{
    if (mx_in_grp(i)) {
        cv_frame(q->x, q->y, q->w, q->h - 1, c);
    } else {
        cv_rect(q->x, q->y + q->h - 1, q->w, 1, C_LINE);   /* (black, a rule under it: no grey ground) */
    }
}
static void mx_paint_row(uint32_t i)                    /* a channel's or a lane's row */
{
    char en[10], pr[16], b[16];
    int32_t x;
    mrect_t q;
    uint16_t c = mx_col(i);
    uint16_t cc = mx_muted(i) ? C_WARN : i == mx_row() ? C_WHITE : c;   /* (a muted row: its code amber, struck; the
                                                         * rows' room under the pinned block has no M badge line) */
    int32_t cy = mx_is_lane(i) || mx_in_block(i) ? MXS_TY : MXL_TY;
    uint32_t mu = mx_muted(i);
    mx_rect(i, &q);
    cy += q.y;
    mx_paint_ground(i, &q, c);
    cv_rect(q.x + 2, q.y + 3, 3, q.h - 7, mx_in_grp(i) ? c : C_GRAY);   /* the colour chip: grey off the knobs' rows */
    text_st(q.x + 6, cy, &FONT_S, mx_code(i), cc, mu);  /* (a muted row: its name struck through, the user, 2026-10-15) */
    if (mx_in_block(i)) {                               /* a line of the block: its effect's name (MASTER), its meter */
        mx_names(i, en, pr);
        text_st(MXB_NX, cy, &FONT_S, cut(b, en, 8), i == mx_row() ? C_WHITE : c, mu);
        mx_vu_band(i, q.x + q.vx, q.y + q.vy, q.vw, q.vh, mxd.pk[i], mx_gr_px(i, q.vw));
        return;
    }
    if (!mx_is_lane(i)) {                               /* the USB input: its name and whether it streams; a channel: its engine
                                                         * and preset; a return: its effect and inserts; their M badge */
        uint16_t pc = i == mx_row() ? C_WHITE : C_GRAY;
        mx_names(i, en, pr);
        x = text_st(MXL_VX, q.y + MXL_TY, &FONT_S, cut(b, en, 8), c, mu);   /* ANALOG, in the row's colour ... (USB IN) */
        if (pr[0])                                      /* (USB: "streaming" whole, never struck) */
            text_st(x + 8, q.y + MXL_TY, &FONT_S, cut(b, pr, (uint32_t)(238 - x - 8) / 8u), pc, mu && !mx_is_usb(i) && !mx_is_ret(i));
    }
    mx_steps_paint(i, q.x + q.sx, q.y + q.sy);          /* the live parts too, as they stand: never a row without them */
    if (q.vw)
        mx_vu_band(i, q.x + q.vx, q.y + q.vy, q.vw, q.vh, mxd.pk[i], mx_gr_px(i, q.vw));
}
static void mx_paint(void)                              /* the upper area's rows, the rule, the pinned block (cv_tall) */
{
    uint32_t i;
    for (i = mxd.first; i < mxd.last; i++)
        mx_paint_row(i);
    cv_rect(0, MXB_Y - 1, 240, 1, C_DIM);               /* (the block's rule) */
    for (i = MXR_RET0; i < MXR_N; i++)
        mx_paint_row(i);
}
static uint32_t mx_static_sig(void)
{
    uint32_t i, sig = hu(hu(hu(hu(hu(0x3A1u, settings.palette), mx_row()), mxd.first), mx.across), mxd.lf);
    char en[10], pr[16];
    for (i = mxd.first; i < MXR_N; i++) {
        if (i == mxd.last && i < MXR_RET0)
            i = MXR_RET0;                               /* (then the block, always) */
        sig = hs(hu(sig, mx_col(i)), mx_code(i));
        if (mx_is_usb(i) || mx_in_block(i)) {             /* (the USB input: muted, streaming; a return: muted, its chain) */
            mx_names(i, en, pr);
            sig = hs(hs(hu(sig, mx_muted(i)), pr), en);
            continue;
        }
        if (i >= MXR_LANE0) {                           /* (a lane: its code, muted or not; the kit is the header's) */
            sig = hu(sig, mx_muted(i));
            continue;
        }
        mx_names(i, en, pr);
        sig = hs(hs(sig, en), pr);
        if (i >= MXR_T1 && i <= MXR_DR)
            sig = hu(sig, mx_muted(i));
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
        return MST_ON(FXT_DYN) && (song.g[G_CTHR] || song.g[G_CCEIL]) ? (uint32_t)ui_clamp(-(int32_t)mc_gr_view * 4, 0, 127) : 0u;
    if (r >= MXR_T1 && r < MXR_T1 + NPART) {          /* (the synth parts: T4 too without drums) */
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
    if (mx_is_ret(r))
        return mx_ret_ln(r) < 3u ? mx_rlv[mx_ret_ln(r)] : 0u;   /* (its line's return: fx.c fx_ret_pk) */
    if (r >= MXR_LANE0)
        return MX_LV * mx.lit[r - MXR_LANE0] / MX_LIT;
    return trk_lv[mx_trk_of(r)];
}
static uint32_t mx_gr_px(uint32_t r, int32_t w)         /* row r's reduction as a width, within w */
{
    uint32_t gr = mx_gr_q4(r) * MX_GR_PX / 4u;
    return gr > (uint32_t)w ? (uint32_t)w : gr;
}
/* the lit bar's colour: its own below -12 dB; from there each column goes on to amber, then red where the bar tops out
 * (a full-scale peak: 32767 lights the whole width, the 0 tick).
 * The scale is op_draw.c meter_h's: log2 of the peak in 6 dB steps, a tick each at i * 4 + 4 of 44, so the columns stand for dB. Integer RGB565 lerps */
#define MX_KNEE_PK 8192                                 /* -12 dBFS: the bar it lights ends at the -12 tick, 36/44 of the width */
static uint16_t mx_lerp(uint16_t a, uint16_t b, uint32_t t)   /* a to b, t 0 .. 256 */
{
    uint32_t rb = ((a & 0xF81Fu) * (256u - t) + (b & 0xF81Fu) * t) >> 8;
    uint32_t g = (((uint32_t)a & 0x07E0u) * (256u - t) + ((uint32_t)b & 0x07E0u) * t) >> 8;
    return (uint16_t)((rb & 0xF81Fu) | (g & 0x07E0u));
}
/* the colour of column c of a meter: col up to the knee k, from there on to amber, then red at the end e (the fill's gradient) */
static uint16_t mx_vu_colat(int32_t c, int32_t k, int32_t e, uint16_t col)
{
    uint32_t u;
    if (c < k)
        return col;
    u = e > k ? (uint32_t)(c - k) * 256u / (uint32_t)(e - k - 1 > 0 ? e - k - 1 : 1) : 256u;
    if (u > 256u)
        u = 256u;
    return u < 128u ? mx_lerp(col, C_WARN, u * 2u) : mx_lerp(C_WARN, C_ERR, (u - 128u) * 2u);
}
static void mx_vu_fill(int32_t x, int32_t y, int32_t w, int32_t h, int32_t px, uint16_t col)
{
    int32_t k = (int32_t)(mx_vu_lv(MX_KNEE_PK) * (uint32_t)w / MX_LV), e = (int32_t)(mx_vu_lv(32767) * (uint32_t)w / MX_LV), c;
    cv_rect(x, y, px < k ? px : k, h, col);
    for (c = k; c < px; c++)                            /* (a column each: at most ~20 above the knee) */
        cv_rect(x + c, y, 1, h, mx_vu_colat(c, k, e, col));
}
/* the peak-hold marker at level ph (0 .. MX_LV; 0: none, which is every peak under the VU FLOOR): a thin tick ending at its
 * place, in the meter's own colour there; red at 0 dBFS, within 0.5 dB (the master's knee stops just short of the full scale) (the user, 2026-10-29: "VU meters just get normal audio peaks") */
static void mx_vu_mark(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t ph, uint16_t col)
{
    int32_t px = (int32_t)(ph * (uint32_t)w / MX_LV), k = (int32_t)(mx_vu_lv(MX_KNEE_PK) * (uint32_t)w / MX_LV), e = (int32_t)(mx_vu_lv(32767) * (uint32_t)w / MX_LV);
    if (!ph)
        return;
    px = px < MX_PH_W ? MX_PH_W : px;
    cv_rect(x + px - MX_PH_W, y, MX_PH_W, h, ph >= MX_LV - 2u ? C_ERR : mx_vu_colat(px - 1, k, e, col));
}
/* the marker's fall, level x 256 a frame, for a 20 dB/s fall on the scale the VU FLOOR sets (floor + 6 dB wide = MX_LV: 20 * 255 * 256 /
 * ((|floor| + 6) * 60), per floor -80 .. -20) */
static const uint16_t MX_PH_FALL[VUF_N] = {253, 286, 330, 389, 473, 604, 837};
#define MX_PHL(r) ((uint32_t)(mxd.ph[r] + 128u) >> 8)   /* row r's marker as a level */
/* row r's marker, a frame on: it jumps to a level at or over it and holds MX_PH_HOLD frames, then falls steadily until the level
 * catches it. Only the audio meters have one (a lane's lit hit is no level) */
static void mx_peak(uint32_t r, uint32_t lv)
{
    uint32_t q = mxd.ph[r];
    if (mx_is_lane(r))
        return;
    if ((lv << 8) >= q) {
        q = lv << 8;
        mxd.phd[r] = (uint8_t)MX_PH_HOLD;
    } else if (mxd.phd[r]) {
        mxd.phd[r]--;
    } else {
        uint32_t f = MX_PH_FALL[vu_floor_i % VUF_N];
        q = q > f ? q - f : 0u;
    }
    mxd.ph[r] = (uint16_t)q;
}
/* row r's meter at x, y of the canvas, w x h: its level pk (0 .. MX_LV) from the left, the reduction gr px from the
 * right (its own canvas, or the band: mx_paint draws the live parts with the rows, so a row drawn again never shows
 * them gone) */
static void mx_vu_paint(uint32_t r, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t pk, uint32_t gr, uint32_t ph)
{
    int32_t px = (int32_t)(pk * (uint32_t)w / MX_LV);
    cv_rect(x, y, w, h, C_BLACK);
    cv_rect(x, y + h - 1, w, 1, C_LINE);                /* the track: a line on black */
    uint16_t col = mx_is_lane(r) || mx_is_usb(r) ? mx_col(r) : C_OK;   /* (the returns' meters too: the channels' green) */
    mx_vu_fill(x, y, w, h, px, col);
    mx_vu_mark(x, y, w, h, ph, col);
    if (gr)
        cv_rect(x + w - (int32_t)gr, y, (int32_t)gr, h, C_WARN);   /* the reduction, from the right */
}
/* a meter in a row drawn whole (mx_paint): empty while the mirror is over Bluetooth, which takes no meters (the user,
 * 2026-10-23: "we definitly dont want the vu meters over BT"); the panel's own follows this frame (mx_vu_at) */
static void mx_vu_band(uint32_t r, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t pk, uint32_t gr)
{
#if FELUCCA_ED_MIRROR
    if (mir_meters_off())
        pk = 0, gr = 0, mxd.vu[r] = 0xFF;
#endif
    mx_vu_paint(r, x, y, w, h, pk, gr, MX_PHL(r));
}
/* a meter: drawn when it changed; to the mirror's client at most every MIR_METER_MS (io/editor/ed_mirror.c), what changed since it
 * last had it, even when the panel's did not (the user, 2026-10-23: "send the meter ... 5 or 10 times per sec") */
static void mx_vu_at(uint32_t r, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t lv, uint32_t gr)
{
    uint32_t pk = mxd.pk[r], panel, phl;
#if FELUCCA_ED_MIRROR
    uint32_t mir;
#endif
    pk = lv > pk ? lv : pk > 5u ? pk - 5u : 0u;         /* (falls ~4 px a frame on a channel's row) */
    mxd.pk[r] = (uint8_t)pk;
    mx_peak(r, lv);
    phl = MX_PHL(r);
    panel = pk != mxd.vu[r] || gr != mxd.gr[r] || phl != mxd.vph[r];
#if FELUCCA_ED_MIRROR
    mir = mxd.mir && (pk != mxd.mvu[r] || gr != mxd.mgv[r] || phl != mxd.mvph[r]);
    if (!panel && !mir)
        return;
#else
    if (!panel)
        return;
#endif
    mxd.vu[r] = (uint8_t)pk;
    mxd.gr[r] = (uint8_t)gr;
    mxd.vph[r] = (uint8_t)phl;
    cv_begin((uint32_t)w, (uint32_t)h, C_BLACK);
    mx_vu_paint(r, 0, 0, w, h, pk, gr, phl);
#if FELUCCA_ED_MIRROR
    if (mir)
        mxd.mvu[r] = (uint8_t)pk, mxd.mgv[r] = (uint8_t)gr, mxd.mvph[r] = (uint8_t)phl;
    if (!panel) {
        cv_mirror((uint32_t)x, (uint32_t)(OP_PY + y));  /* (the client's alone: the panel shows it already) */
        return;
    }
    mir_mute = (uint8_t)!mir;                           /* (not due: the client keeps its last until the next window) */
#endif
    cv_blit((uint32_t)x, (uint32_t)(OP_PY + y));
#if FELUCCA_ED_MIRROR
    mir_mute = 0;
#endif
}
static uint32_t mx_on(uint32_t r, uint32_t i)           /* step i is set on row r */
{
    const track_t *t = &trk[mx_trk_of(r)];
    if (r >= MXR_LANE0)
        return dstep_has(&t->dstep[i % NSTEP], r - MXR_LANE0);
    if (is_drum(t))                                     /* (DR: the sounds' hits only; the ACCENT lane's own row shows */
        return dstep_mask(&t->dstep[i % NSTEP]) != 0u;  /* the accents, the user 2026-10-23) */
    return trk_on_step(mx_trk_of(r), i);
}
/* the level step i of row r is shaded by, as STEP shades it (lvl_col): a lane's hit's own, a channel's strongest note,
 * DR's strongest hit of the sounds (the accents not counted) */
static uint32_t mx_lvl(uint32_t r, uint32_t i)
{
    const track_t *t = &trk[mx_trk_of(r)];
    const step_t *s;
    uint32_t n, lv = LV_GHOST;
    if (r >= MXR_LANE0)
        return dstep_lvl(&t->dstep[i % NSTEP], r - MXR_LANE0);
    if (is_drum(t)) {
        const dstep_t *d = &t->dstep[i % NSTEP];
        for (n = 0; n < DRUM_LANES; n++)
            if (dstep_has(d, n) && lvl_rank(dstep_lvl(d, n)) > lvl_rank(lv))
                lv = dstep_lvl(d, n);
        return lv;
    }
    s = &t->step[i % NSTEP];
    for (n = 0; n < s->n && n < 4u; n++)
        if (lvl_rank((s->lvl >> (2u * n)) & 3u) > lvl_rank(lv))
            lv = (s->lvl >> (2u * n)) & 3u;
    return lv;
}
/* row r's steps at x, y of the canvas (on the row's ground): the page playing, the playhead white, the pages' bars (a
 * channel's row) */
/* step s's cell of row r at x, y (at: the playhead; c: the row's colour, grey muted) */
static void mx_step_cell(uint32_t r, const track_t *t, uint32_t s, uint32_t at, uint16_t c, int32_t x, int32_t y, const mrect_t *q)
{
    uint16_t sc = song.playing && s == at && !mx_smuted(r) ? C_WHITE
                : !mx_on(r, s) ? C_BLACK : mx_smuted(r) ? c   /* (muted: flat dark grey, no levels) */
                : r != mx_row() ? col_shade(C_WHITE, 3u + lvl_rank(mx_lvl(r, s)))   /* (not selected: grey 3/8 .. 6/8, */
                : op_step_auto(t, s, r >= MXR_LANE0 ? r - MXR_LANE0 : 0xFFu) ? lvl_acol(mx_lvl(r, s))   /* under the */
                : lvl_col(c, mx_lvl(r, s));                 /* playhead's white; the user, 2026-10-23) */
    if (sc == C_BLACK && (r >= MXR_LANE0 || is_drum(t)))   /* (a drum row's empty step: a small dot, a beat's bigger, as */
        step_dot(x, y, q->sp - 2 + (r >= MXR_LANE0), q->sh, !(s & 3u));   /* STEP's; no box: 16 boxes read as noise) */
    else
        cv_box(x, y, q->sp - 2 + (r >= MXR_LANE0), q->sh, sc);
}
static void mx_steps_paint(uint32_t r, int32_t x, int32_t y)
{
    if (mx_is_usb(r) || mx_is_ret(r))
        return;                                         /* (no sequence) */
    const track_t *t = &trk[mx_trk_of(r)];
    uint32_t len = (uint32_t)ui_clamp(t->p[P_SLEN], 1, NSTEP), at = t->seq_idx % len, page = at & ~15u, i, np;
    uint16_t c = mx_smuted(r) ? C_DIM : mx_col(r);   /* (the sequencer muted: the steps grey, no playhead) */
    mrect_t q;
    if (r == MXR_MASTER)
        return;
    mx_rect(r, &q);
    for (i = 0; i < 16u; i++)
        if (page + i < len)
            mx_step_cell(r, t, page + i, at, c, x + (int32_t)i * q.sp, y, &q);
    np = (len + 15u) / 16u;
    if (np > 1u && r < MXR_LANE0 && MXL_H >= 30)        /* the pattern's pages: a thin bar each, the one playing lit (a row with
                                                         * the room: none since the block took the panel's foot) */
        for (i = 0; i < np; i++)
            cv_rect(x + (int32_t)(i * 16u * MXL_SP / np), y + MXL_SH + 2, (int32_t)(16u * MXL_SP / np) - 2, 2,
                    i == page / 16u ? C_WHITE : C_DIM);
}
static void mx_steps(uint32_t r)
{
    if (mx_is_usb(r) || mx_is_ret(r))
        return;
    const track_t *t = &trk[mx_trk_of(r)];
    uint32_t len = (uint32_t)ui_clamp(t->p[P_SLEN], 1, NSTEP), at = t->seq_idx % len, page = at & ~15u, i, sig;
    uint16_t bg = C_BLACK;
    mrect_t q;
    if (r == MXR_MASTER)
        return;
    mx_rect(r, &q);
    sig = hu(hu(hu(hu(hu(hu(0x57u, song.playing), page), len), bg), mx_smuted(r)), r == mx_row());
    for (i = 0; i < 16u; i++)
        sig = sig * 5u + mx_on(r, page + i) * 2u + (mx_on(r, page + i) && op_step_auto(t, page + i, r >= MXR_LANE0 ? r - MXR_LANE0 : 0xFFu))
            + (mx_on(r, page + i) ? lvl_rank(mx_lvl(r, page + i)) * 7u : 0u);   /* (a level turned: drawn again) */
    if (sig == mxd.stp[r]) {            /* the playhead alone moved on the page: its two cells (the screen mirror over Bluetooth, */
        uint32_t k, s[2] = {mxd.sta[r], at};   /* 2026-10-23: every row's whole strip a step was most of a playing mixer's bytes) */
        uint16_t c = mx_smuted(r) ? C_DIM : mx_col(r);
        if (!song.playing || at == mxd.sta[r])
            return;
        mxd.sta[r] = (uint8_t)at;
        for (k = 0; k < 2u; k++) {
            if (s[k] < page || s[k] >= page + 16u)
                continue;
            cv_begin((uint32_t)q.sp, (uint32_t)q.sh, bg);
            mx_step_cell(r, t, s[k], at, c, 0, 0, &q);
            cv_blit((uint32_t)(q.x + q.sx + (int32_t)(s[k] - page) * q.sp), (uint32_t)(OP_PY + q.y + q.sy));
        }
        return;
    }
    mxd.stp[r] = sig;
    mxd.sta[r] = (uint8_t)at;
    cv_begin((uint32_t)(16 * q.sp), (uint32_t)(q.sh + (r < MXR_LANE0 && MXL_H >= 30 ? 4 : 0)), bg);   /* (the page bars' rows: only
                                                         * with the room, else the meter under the steps is kept) */
    mx_steps_paint(r, 0, 0);
    cv_blit((uint32_t)(q.x + q.sx), (uint32_t)(OP_PY + q.y + q.sy));
}
/* (noinline: a call from op_frame_draw, which is smaller without it inlined) */
static __attribute__((noinline)) void mx_draw(void)
{
    uint32_t i, sig, trk_lv[NTRK];
    for (i = 0; i < NTRK; i++)                          /* (taken every frame, seen or not: no stale peak) */
        trk_lv[i] = mx_vu_lv(meter_ui_take(i));
    for (i = 0; i < 3u; i++)                            /* (the returns' lines too) */
        mx_rlv[i] = mx_vu_lv(meter_ret_take(i));
    mx_view();
    sig = mx_static_sig();
    if (sig != ui.sig[2]) {
        if (mxd.first != mxd.pk_first) {                /* another group up there: no peak carried from one to the other (the
                                                         * block's own are kept: it never moves) */
            for (i = 0; i < MXR_RET0; i++)
                mxd.pk[i] = 0, mxd.ph[i] = 0, mxd.phd[i] = 0;
            mxd.pk_first = mxd.first;
        }
        ui.sig[2] = sig;
        cv_tall(OP_PY, OH_BODY, C_BLACK, mx_paint);
        mx_redraw();
    }
    if (ui.overlay == 2u)
        return;                                         /* (under a toast: the rows wait) */
#if FELUCCA_ED_MIRROR
    mxd.mir = (uint8_t)mir_meters_due();
#endif
    for (i = mxd.first; i < MXR_N; i++) {               /* the upper rows in view, then the block */
        mrect_t q;
        if (i == mxd.last && i < MXR_RET0)
            i = MXR_RET0;
        mx_rect(i, &q);
        if (q.vw)                                       /* (the ACCENT lane: no meter) */
            mx_vu_at(i, q.x + q.vx, q.y + q.vy, q.vw, q.vh, mx_level(i, trk_lv), mx_gr_px(i, q.vw));
        if (!mx_in_block(i))
            mx_steps(i);
    }
}
static void mx_redraw(void)                             /* the live parts again (an overlay went) */
{
    memset(mxd.vu, 0xFF, sizeof mxd.vu);
    memset(mxd.gr, 0xFF, sizeof mxd.gr);
    memset(mxd.vph, 0xFF, sizeof mxd.vph);
#if FELUCCA_ED_MIRROR
    memset(mxd.mvph, 0xFF, sizeof mxd.mvph);
    memset(mxd.mvu, 0xFF, sizeof mxd.mvu);              /* (a catch-up: the client's meters again too) */
    memset(mxd.mgv, 0xFF, sizeof mxd.mgv);
#endif
    memset(mxd.stp, 0, sizeof mxd.stp);
}
