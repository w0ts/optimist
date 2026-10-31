/* SPDX-License-Identifier: GPL-3.0-only */
/* The horizontal mixer's rows (op_mixer.c), in the panel under the cards. The mixer shows ONE group at a time, the one
 * the cursor is in (the user, 2026-10-11: MASTER alone, or the four channels, or the drum sounds); ALGORITHM walks the
 * rows as before and the view follows it, drawn in place (cv_tall over the panel, no clear to black):
 *   MASTER     the master screen, under the 2x2 cards (99 px; 117 without the help band), ONE screen for the returns and the
 *              master (the user, 2026-10-30: "put ON ONE screen the master, not at the bottom, but where it was before, top,
 *              and the return track above it, on the same screen. AND not to move the screen"; "only change the master screen
 *              with the returns"): its rows RET A, RET B, RET C, then MASTER, top down, all four always drawn, the cursor's
 *              framed (moving among them moves only the frame). A return's line (15 px): its chip, its code A B C, its effect's
 *              name (REV DLY CHO or the type it holds; "----" empty), its meter from x 96 (what it gives back to the master,
 *              fx.c fx_ret_tap, after its POST and level; a muted one struck, its code amber). MASTER under them: M MASTER,
 *              its VU meter (the level from the left, its peak falling) over a dB scale, the compressor's gain reduction as an
 *              amber bar from the right with its dB (THRS or CEIL set: mc_gr_view)
 *   CHANNELS   T1 T2 T3 DR, the four rows of 41 px (the mixer keeps the 1x4 cards, op_layers.c
 *              cards_2x2):
 *                x 0..21    a colour chip and the row's code (T1 T2 T3 DR), its M / S badge under it; the selected
 *                           row's full name is the header's ("T2 mix", op_draw.c head_title)
 *                x 28..237  three lines. The text line (y +1): the engine's name in the row's colour (ANALOG, TRIO,
 *                           DRUMS), then the preset (the kit for DR, a user preset's name), cut to the room left; under
 *                           it (y +20, 6 px) the row's sequence, as SLOOP's TRACKS screen: the 16 steps of the page
 *                           playing (13 px a step: the steps set shaded by level as on STEP, in the row's colour on the selected row and grey on
 *                           the others (its colour chip too), flat dark grey when muted, past LEN empty, the playhead
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
#define MXS_NROW DRUM_UILANES          /* SOUNDS: the drum lanes in one column, a row each, the ACCENT lane last (the user,
                                         * 2026-10-18: all 11 on one screen; 13 px rows over the help band, 14 without) */
#define MXS_SY 2                        /* a lane's steps: their y in the row (the channels' pitch, MXL_SP), the meter under them */
#define MXS_VH 2
#define MXS_TY (-2)                     /* a lane's code: its 16 px cell from 2 px over the row (the caps' ink inside it) */
#define MXK_H 15                        /* the master screen: a return's line, RET A B C from the top */
#define MXK_Y 1
#define MXK_NX 22                       /* a line's name */
#define MXM_MY (MXK_Y + 3 * MXK_H + 2)  /* MASTER's line (48): M MASTER, its meter on it, the scale and GR under it */
#define MXM_X 96                        /* MASTER's meter (and the returns' meters: the same column) */
#define MXM_W 140
#define MXM_VY (MXM_MY + 2)
#define MXM_VH 8
#define MXM_GX 96                       /* its reduction bar, "GR" at its left */
#define MXM_GW 140
#define MXM_GY (MXM_MY + 30)
#define MXM_GH 6
#define MX_LV 255u                      /* a level: 0 .. 255, the meter's whole width */
#define MX_PH_HOLD 90u                  /* the peak-hold marker holds this many frames (~1.5 s at 60 a second), then falls */
#define MX_PH_W 2                       /* ... a marker: this wide, the meter's height */
enum { MV_MASTER, MV_CH, MV_SOUNDS, MV_USB };   /* (MV_USB: the USB input's row alone, past the drum sounds; MV_MASTER the
                                                 * master screen: RET A B C and MASTER) */
static struct {
    uint8_t first, last;                /* the rows in view: first .. last - 1 (the cursor's group) */
    uint8_t vu[MXR_N], gr[MXR_N];       /* the meters as drawn (0xFF: again) */
    uint32_t stp[MXR_N];                /* the steps as drawn (a signature, the playhead left out) */
    uint8_t sta[MXR_N];                 /* the playhead as drawn (0xFF: none) */
    uint8_t pk[MXR_N];                  /* the meters' peaks (0 .. MX_LV), falling */
    uint16_t ph[MXR_N];                 /* the peak-hold markers (level x 256): jump to a new peak, hold, then fall (mx_peak) */
    uint8_t clip[MXR_N], vcl[MXR_N];    /* the CLIP light: frames left lit; as drawn (0xFF: again) */
    uint8_t phd[MXR_N], vph[MXR_N];     /* ... frames left of the hold; the marker's level as drawn (0xFF: again) */
    uint8_t mgr;                        /* MASTER alone: its reduction as drawn, dB x 4 (0xFF: again) */
    uint8_t pk_first;                   /* the group the peaks are of (mx_draw) */
#if FELUCCA_ED_MIRROR
    uint8_t mvu[MXR_N], mgv[MXR_N];     /* the meters as the mirror's client has them (0xFF: again) */
    uint8_t mvph[MXR_N], mvcl[MXR_N];   /* ... and their peak-hold markers, CLIP lights */
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
static uint32_t mx_mode(uint32_t r)
{
    return mx_is_ret(r) || r == MXR_MASTER ? MV_MASTER : r < MXR_LANE0 ? MV_CH : mx_is_usb(r) ? MV_USB : MV_SOUNDS;
}
static void mx_view(void)                               /* the rows in view: the cursor's group */
{
    uint32_t m = mx_mode(mx_row());
    mxd.first = (uint8_t)(m == MV_MASTER ? MXR_RET0 : m == MV_CH ? MXR_T1 : m == MV_USB ? MXR_LN : MXR_LANE0);
    mxd.last = (uint8_t)(m == MV_MASTER ? MXR_MASTER + 1u : m == MV_CH ? MXR_LANE0 : m == MV_USB ? MXR_LN + 1u : MXR_LN);
}
static void mx_rect(uint32_t r, mrect_t *q)             /* row r's place in its group's view */
{
    memset(q, 0, sizeof *q);
    if (r == MXR_MASTER) {                              /* (the master screen's foot: its line, its scale, its GR) */
        q->y = MXM_MY - 2, q->w = 240, q->h = OP_PH - (MXM_MY - 2);
        q->vx = MXM_X, q->vy = MXM_VY - q->y, q->vw = MXM_W, q->vh = MXM_VH;
    } else if (mx_is_ret(r)) {                          /* (a return's line, above MASTER) */
        q->y = MXK_Y + (int32_t)(r - MXR_RET0) * MXK_H, q->w = 240, q->h = MXK_H;
        q->vx = MXM_X, q->vy = 4, q->vw = MXM_W, q->vh = 6;
    } else if (r < MXR_LANE0 || mx_is_usb(r)) {
        q->y = mx_is_usb(r) ? 0 : (int32_t)(r - MXR_T1) * MXL_H, q->w = 240, q->h = MXL_H;   /* (the USB input: a channel's row,
                                                         * alone, no steps) */
        q->sx = MXL_VX, q->sy = MXL_SY, q->sp = MXL_SP, q->sh = MXL_SH;
        q->vx = MXL_VX, q->vy = MXL_VY, q->vw = MXL_VW, q->vh = MXL_VH;
    } else {
        uint32_t row = r - MXR_LANE0;                   /* (the ACCENT lane: DRUM_ACC, the last row) */
        q->y = (int32_t)row * OP_PH / (int32_t)MXS_NROW;
        q->h = (int32_t)(row + 1u) * OP_PH / (int32_t)MXS_NROW - q->y;
        q->w = 240;
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
/* MASTER alone: the tall meter's dB scale (a tick each 6 dB from 0 down to the VU FLOOR, some named), "GR", the returns' names
 * (its name is the header's badge). A dB d sits at (|floor| + 6 + d) / (|floor| + 6) of the width (op_draw.c meter_h) */
static int32_t mx_db_x(int32_t d)
{
    int32_t w = -vuf_db(vu_floor_i) + 6;
    return MXM_X + (int32_t)(MXM_W * (uint32_t)(w + d) / (uint32_t)w);
}
static void mx_paint_master(void)
{
    /* the named ticks of each floor (-80 .. -20): 0 and every 12 dB, skipping the one the next label's text would touch; 0 ends on its tick */
    static const int8_t DB[VUF_N][6] = {{0, -12, -24, -48, -72, 1}, {0, -12, -24, -48, -60, 1}, {0, -12, -24, -48, 1, 1}, {0, -12, -24, -48, 1, 1},
                                        {0, -12, -24, -36, 1, 1}, {0, -12, -24, 1, 1, 1}, {0, -6, -12, -18, 1, 1}};
    const int8_t *nm = DB[vu_floor_i % VUF_N];
    uint32_t i;
    int32_t d;
    char b[16];
    mrect_t q;
    char en[10], pr[16];
    for (i = MXR_RET0; i < MXR_MASTER; i++) {           /* RET A B C: a line each */
        uint32_t mu = mx_muted(i);
        mx_rect(i, &q);
        mx_paint_ground(i, &q, C_HI);
        cv_rect(2, q.y + 3, 3, q.h - 6, mx_in_grp(i) ? C_HI : C_GRAY);
        text_st(6, q.y - 1, &FONT_S, mx_code(i), mu ? C_WARN : i == mx_row() ? C_WHITE : C_HI, mu);
        mx_names(i, en, pr);
        text_st(MXK_NX, q.y - 1, &FONT_S, cut(b, en, 8), i == mx_row() ? C_WHITE : C_HI, mu);
        mx_vu_band(i, q.vx, q.y + q.vy, q.vw, q.vh, mxd.pk[i], 0);
    }
    mx_rect(MXR_MASTER, &q);                            /* MASTER */
    mx_paint_ground(MXR_MASTER, &q, C_HI);
    cv_rect(2, q.y + 3, 3, q.h - 6, mx_in_grp(MXR_MASTER) ? C_HI : C_GRAY);   /* the colour chip */
    text_st(6, MXM_MY - 3, &FONT_S, "M", MXR_MASTER == mx_row() ? C_WHITE : C_HI, 0);
    text_st(MXK_NX, MXM_MY - 3, &FONT_S, "MASTER", MXR_MASTER == mx_row() ? C_WHITE : C_HI, 0);
    for (d = 0; d >= vuf_db(vu_floor_i); d -= 6)        /* the scale: a tick each 6 dB */
        cv_rect(mx_db_x(d) - 1, MXM_VY + MXM_VH + 1, 1, 3, C_LINE);
    for (i = 0, d = 240; i < 6u && nm[i] <= 0; i++) {   /* (right to left: a label that would touch the one right of it, or the
                                                         * screen's edge, left out: the meter is 140 px since 2026-10-30) */
        int32_t x = mx_db_x(nm[i]), w, l;
        fmt_int(b, nm[i]);
        w = text_ws(b);
        l = x - w + (i ? w / 2 : 0);
        if (l + w > d - 3 || l + w > 238)
            continue;
        cv_texts(l, MXM_VY + MXM_VH + 4, b, C_DIM);
        d = l;
    }
    cv_texts(MXM_GX - 20, MXM_GY - 5, "GR", C_DIM);
    mx_vu_band(MXR_MASTER, MXM_X, MXM_VY, MXM_W, MXM_VH, mxd.pk[MXR_MASTER], 0);   /* (the returns: their own rows) */
}
static void mx_paint_row(uint32_t i)                    /* a channel's or a lane's row */
{
    char en[10], pr[16], b[16];
    int32_t x;
    mrect_t q;
    uint16_t c = mx_col(i);
    uint16_t cc = mx_is_lane(i) && mx_muted(i) ? C_WARN : i == mx_row() ? C_WHITE : c;   /* (a muted lane: its code in the M badge's colour) */
    int32_t cy = mx_is_lane(i) ? MXS_TY : MXL_TY;
    uint32_t mu = mx_muted(i);
    mx_rect(i, &q);
    cy += q.y;
    mx_paint_ground(i, &q, c);
    cv_rect(q.x + 2, q.y + 3, 3, q.h - 7, mx_in_grp(i) ? c : C_GRAY);   /* the colour chip: grey off the knobs' rows */
    text_st(q.x + 6, cy, &FONT_S, mx_code(i), cc, mu);  /* (a muted row: its name struck through, the user, 2026-10-15) */
    if (!mx_is_lane(i)) {                               /* the USB input: its name and whether it streams; a channel: its engine
                                                         * and preset; a return: its effect and inserts; their M badge */
        uint16_t pc = i == mx_row() ? C_WHITE : C_GRAY;
        mx_names(i, en, pr);
        x = text_st(MXL_VX, q.y + MXL_TY, &FONT_S, cut(b, en, 8), c, mu);   /* ANALOG, in the row's colour ... (USB IN) */
        if (pr[0])                                      /* (USB: "streaming" whole, never struck) */
            text_st(x + 8, q.y + MXL_TY, &FONT_S, cut(b, pr, (uint32_t)(238 - x - 8) / 8u), pc, mu && !mx_is_usb(i) && !mx_is_ret(i));
        if (mu)                                         /* (muted: the M badge, the steps grey) */
            cv_texts(6, q.y + 18, "M", C_WARN);
    }
    mx_steps_paint(i, q.x + q.sx, q.y + q.sy);          /* the live parts too, as they stand: never a row without them */
    if (q.vw)
        mx_vu_band(i, q.x + q.vx, q.y + q.vy, q.vw, q.vh, mxd.pk[i], mx_gr_px(i, q.vw));
}
static void mx_paint(void)                              /* the group in view (cv_tall) */
{
    uint32_t i;
    if (mxd.first == MXR_RET0) {                        /* (the master screen: RET A B C, MASTER) */
        mx_paint_master();
        return;
    }
    for (i = mxd.first; i < mxd.last; i++)
        mx_paint_row(i);
}
static uint32_t mx_static_sig(void)
{
    uint32_t i, sig = hu(hu(hu(hu(0x3A1u, settings.palette), mx_row()), mxd.first), mx.across);
    char en[10], pr[16];
    for (i = mxd.first; i < mxd.last; i++) {
        sig = hs(hu(sig, mx_col(i)), mx_code(i));
        if (mx_is_usb(i) || mx_is_ret(i)) {             /* (the USB input: muted, streaming; a return: muted, its chain) */
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
/* the lit bar's colours by the signal's dBFS (the user, 2026-10-31: "green below -18 dBFS; amber from -18 to -6 dBFS; red from -6 (or -3)
 * dBFS; a separate held clip light at 0 dBFS"; -6 picked): green under -18, amber -18 .. -6, red from -6. The bands are cut
 * at the peaks 2^12 (-18.06 dB) and 2^14 (-6.02 dB) through mx_vu_lv, so they stand at the same dB whatever the VU FLOOR is.
 * A lane's row (its lit hit, no level) keeps its instrument colour. The CLIP light: the meter's last MX_CLIP_W px, dark red,
 * red while a sample reached 0 dBFS and MX_CLIP_HOLD frames after the last (~1 s at 60 a second) */
#define MX_AMBER_PK 4096                                /* -18 dBFS: green ends, amber starts */
#define MX_RED_PK 16384                                 /* -6 dBFS: amber ends, red starts */
#define MX_CLIP_HOLD 60u
#define MX_CLIP_W 4
static uint16_t mx_lerp(uint16_t a, uint16_t b, uint32_t t)   /* a to b, t 0 .. 256 */
{
    uint32_t rb = ((a & 0xF81Fu) * (256u - t) + (b & 0xF81Fu) * t) >> 8;
    uint32_t g = (((uint32_t)a & 0x07E0u) * (256u - t) + ((uint32_t)b & 0x07E0u) * t) >> 8;
    return (uint16_t)((rb & 0xF81Fu) | (g & 0x07E0u));
}
static int32_t mx_band_px(int32_t pk, int32_t w)        /* the column where the band starting at peak pk begins */
{
    return (int32_t)(mx_vu_lv(pk) * (uint32_t)w / MX_LV);
}
/* the colour of column c of a meter: green, amber or red by its band; col alone on a lane's row (col0 == 0 means bands) */
static uint16_t mx_vu_colat(int32_t c, int32_t w, uint16_t lane_col)
{
    if (lane_col)
        return lane_col;
    return c < mx_band_px(MX_AMBER_PK, w) ? C_OK : c < mx_band_px(MX_RED_PK, w) ? C_WARN : C_ERR;
}
static void mx_vu_fill(int32_t x, int32_t y, int32_t w, int32_t h, int32_t px, uint16_t lane_col)
{
    int32_t k1 = mx_band_px(MX_AMBER_PK, w), k2 = mx_band_px(MX_RED_PK, w);
    if (lane_col) {
        cv_rect(x, y, px, h, lane_col);
        return;
    }
    cv_rect(x, y, px < k1 ? px : k1, h, C_OK);
    if (px > k1)
        cv_rect(x + k1, y, (px < k2 ? px : k2) - k1, h, C_WARN);
    if (px > k2)
        cv_rect(x + k2, y, px - k2, h, C_ERR);
}
/* the peak-hold marker at level ph (0 .. MX_LV; 0: none, which is every peak under the VU FLOOR): a thin tick ending at its
 * place, in the colour of its band; red at 0 dBFS, within 0.5 dB (the master's knee stops just short of the full scale) (the user, 2026-10-29: "VU meters just get normal audio peaks") */
static void mx_vu_mark(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t ph, uint16_t lane_col)
{
    int32_t px = (int32_t)(ph * (uint32_t)w / MX_LV);
    if (!ph)
        return;
    px = px < MX_PH_W ? MX_PH_W : px;
    cv_rect(x + px - MX_PH_W, y, MX_PH_W, h, ph >= MX_LV - 2u ? C_ERR : mx_vu_colat(px - 1, w, lane_col));
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
    uint16_t col = mx_is_lane(r) ? mx_col(r) : 0u;      /* (a lane's lit hit: its colour; every level meter: the bands) */
    mx_vu_fill(x, y, w, h, px, col);
    mx_vu_mark(x, y, w, h, ph, col);
    if (!mx_is_lane(r))                                 /* the CLIP light, over the meter's last px, a black gap before it */
        cv_rect(x + w - MX_CLIP_W - 1, y, 1, h - 1, C_BLACK),
        cv_rect(x + w - MX_CLIP_W, y, MX_CLIP_W, h - 1, mxd.clip[r] ? C_ERR : mx_lerp(C_BLACK, C_ERR, 56u));
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
    uint32_t pk = mxd.pk[r], panel, phl, cl;
#if FELUCCA_ED_MIRROR
    uint32_t mir;
#endif
    pk = lv > pk ? lv : pk > 5u ? pk - 5u : 0u;         /* (falls ~4 px a frame on a channel's row) */
    mxd.pk[r] = (uint8_t)pk;
    mx_peak(r, lv);
    phl = MX_PHL(r);
    if (!mx_is_lane(r))                                 /* (the master's knee stops 2 units short of the full scale) */
        mxd.clip[r] = lv >= (r == MXR_MASTER ? MX_LV - 2u : MX_LV) ? (uint8_t)MX_CLIP_HOLD : mxd.clip[r] ? (uint8_t)(mxd.clip[r] - 1u) : 0u;
    cl = mxd.clip[r] != 0u;
    panel = pk != mxd.vu[r] || gr != mxd.gr[r] || phl != mxd.vph[r] || cl != mxd.vcl[r];
#if FELUCCA_ED_MIRROR
    mir = mxd.mir && (pk != mxd.mvu[r] || gr != mxd.mgv[r] || phl != mxd.mvph[r] || cl != mxd.mvcl[r]);
    if (!panel && !mir)
        return;
#else
    if (!panel)
        return;
#endif
    mxd.vu[r] = (uint8_t)pk;
    mxd.gr[r] = (uint8_t)gr;
    mxd.vph[r] = (uint8_t)phl;
    mxd.vcl[r] = (uint8_t)cl;
    cv_begin((uint32_t)w, (uint32_t)h, C_BLACK);
    mx_vu_paint(r, 0, 0, w, h, pk, gr, phl);
#if FELUCCA_ED_MIRROR
    if (mir)
        mxd.mvu[r] = (uint8_t)pk, mxd.mgv[r] = (uint8_t)gr, mxd.mvph[r] = (uint8_t)phl, mxd.mvcl[r] = (uint8_t)cl;
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
        str_app(b, " dB", sizeof b);
        cv_texts(0, 0, b, C_WARN);
    } else if (FELUCCA_MASTER_COMP) {
        cv_texts(0, 0, "off", C_DIM);
    }
#if FELUCCA_ED_MIRROR
    mir_mute = (uint8_t)mir_meters_off();               /* (a meter too: none over Bluetooth) */
#endif
    cv_blit(MXM_GX, (uint32_t)(OP_PY + MXM_GY - 5));
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
    if (np > 1u && r < MXR_LANE0)                       /* the pattern's pages: a thin bar each, the one playing lit */
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
    cv_begin((uint32_t)(16 * q.sp), (uint32_t)(q.sh + (r < MXR_LANE0 ? 4 : 0)), bg);
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
        if (mxd.first != mxd.pk_first) {                /* another group: no peak carried from one to the other */
            memset(mxd.pk, 0, sizeof mxd.pk);
            memset(mxd.ph, 0, sizeof mxd.ph);
            memset(mxd.phd, 0, sizeof mxd.phd);
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
    if (mxd.first == MXR_RET0) {                        /* the master screen: the returns' meters, MASTER's, its reduction */
        for (i = MXR_RET0; i < MXR_MASTER; i++) {
            mrect_t q;
            mx_rect(i, &q);
            mx_vu_at(i, q.vx, q.y + q.vy, q.vw, q.vh, mx_level(i, trk_lv), 0);
        }
        mx_vu_at(MXR_MASTER, MXM_X, MXM_VY, MXM_W, MXM_VH, mx_level(MXR_MASTER, trk_lv), 0);
        mx_master_gr();
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
    memset(mxd.vph, 0xFF, sizeof mxd.vph);
    memset(mxd.vcl, 0xFF, sizeof mxd.vcl);
#if FELUCCA_ED_MIRROR
    memset(mxd.mvph, 0xFF, sizeof mxd.mvph);
    memset(mxd.mvcl, 0xFF, sizeof mxd.mvcl);
    memset(mxd.mvu, 0xFF, sizeof mxd.mvu);              /* (a catch-up: the client's meters again too) */
    memset(mxd.mgv, 0xFF, sizeof mxd.mgv);
#endif
    memset(mxd.stp, 0, sizeof mxd.stp);
    mxd.mgr = 0xFF;
}
