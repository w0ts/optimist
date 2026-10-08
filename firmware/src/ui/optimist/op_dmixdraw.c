/* SPDX-License-Identifier: GPL-3.0-only */
/* The drum mixer's strips (op_dmix.c), drawn with the mixer's pieces (op_draw.c strip_fader, strip_ctl, the steps):
 * each strip a sound, in its source's colour (lane_col: the drum synth, a sampled kit, an X0X voice, your sample);
 * from the top its name and its source, the LEVEL fader with the hit flash beside it in the meter's place (the kit
 * pads' 6 frames, falling), the controls' rows in the walk's order, the lane's 16 steps playing, CUT at the foot in
 * PAN's place. The control SELECT is on is lit on the knobs' four strips; the selected sound (lane_sel) is framed in
 * its colour with its head tinted, as the selected track on the mixer.
 *   4 strips   the mixer's geometry, 55 px each, the master column on the right
 *   8 strips   29 px: a two-letter name (BD SD CH ...), the knob's number under it on the knobs' four, no steps;
 *              the knobs' four on the lighter ground */
static const char *const LANE_CODE[DRUM_LANES] = {"BD", "B2", "SD", "CP", "CH", "OH", "PH", "RS",
                                                  "S2", "LT", "HT", "CR", "RD", "SH", "CG", "CB"};
#define DM_BG_OFF RGB(13, 13, 16)       /* the ground of a strip the knobs do not edit (8) */
#define DM_PITCH8 30                    /* 8 strips: a strip and its gap on 240 px */
static int32_t dm_x(uint32_t i)                         /* the i-th strip shown: its x */
{
    return dm_strips() == 4u ? MX_X(i) : (int32_t)i * DM_PITCH8;
}
static int32_t dm_w(void) { return dm_strips() == 4u ? MX_W : DM_PITCH8 - 1; }
static int32_t dm_fader_x(void) { return dm_strips() == 4u ? 8 : 4; }
static int32_t dm_fader_w(void) { return dm_strips() == 4u ? 14 : 10; }
static int32_t dm_flash_x(void) { return dm_strips() == 4u ? 28 : 17; }
static int32_t dm_flash_w(void) { return dm_strips() == 4u ? 8 : 7; }
static int dm_knob(uint32_t l) { return l / 4u == dm.blk; }   /* a lane of the knobs' block */

/* the head: the name (and the source, or the knob's number) */
static void dm_head(uint32_t l, int32_t w, uint16_t tc, uint32_t sel, uint32_t snd_row)
{
    char b[10];
    uint16_t nc = sel ? C_WHITE : tc;
    if (dm_strips() == 4u) {
        cv_text(3, 3, &FONT_S, cut(b, LANE_SHORT[l], 6), nc);   /* "c.hat" as the kit names it */
        cv_text(3, 19, &FONT_S, cut(b, DS_SRC_NAMES[dsnd_src_idx(dl.src[l])], 6), snd_row ? C_WHITE : C_AMB);
    } else {
        cv_text((w - 16) / 2, 3, &FONT_S, LANE_CODE[l], nc);
        if (dm_knob(l)) {
            b[0] = (char)('1' + l % 4u), b[1] = 0;      /* KNOB 1..4 */
            cv_text((w - 8) / 2, 19, &FONT_S, b, snd_row ? C_WHITE : C_GRAY);
        }
    }
    if (snd_row)
        cv_frame(1, 18, w - 2, 14, tc);
}
static void dm_strip(uint32_t i)
{
    uint32_t l = (dm_first() + i) % DRUM_LANES, r, j = 0, cur = ui.row[SCR_DMIX] % NDMX, knob = dm_knob(l);
    uint32_t sel = l == lane_selected(), n = dm_strips();
    int32_t w = dm_w();
    uint16_t tc = lane_col(l), fr = knob && ui.hot == l % 4u && ui.hot_lit ? C_WHITE : tc;
    cell_t e;
    cv_begin((uint32_t)w, MIX_H, knob || n == 4u ? OP_SURF : DM_BG_OFF);
    if (sel) {                                          /* the selected sound: framed, its head tinted */
        cv_rect(0, 0, w, 17, col_shade(tc, 3u));
        cv_frame(0, 0, w, MIX_H, tc);
    }
    dm_head(l, w, tc, sel, knob && DMX[cur].kind == DK_ENTER);
    for (r = 0; r < NDMX; r++) {                        /* the fader first, CUT at the foot, the rest between */
        uint32_t lit = r == cur && knob;
        int32_t y;
        if (DMX[r].kind == DK_ENTER)
            continue;
        dm_cell_lane(r, l, &e);
        if (!j) {
            strip_fader(&e, dm_fader_x(), dm_fader_w(), lit, tc, fr);
            j = 1;
            continue;
        }
        y = DMX[r].id == DM_FOOT_ID && DMX[r].kind == DK_SND ? MX_PAN_Y : MX_ROW_Y + (int32_t)(j - 1u) * MX_ROW_H;
        j += !(DMX[r].id == DM_FOOT_ID && DMX[r].kind == DK_SND);
        strip_ctl(&e, y, w, lit, tc, fr, 0);
    }
    cv_blit((uint32_t)dm_x(i), MIX_Y);
}
static void dm_redraw(void)                             /* the flashes and the steps over the strips: again */
{
    memset(dm.lit_drawn, 0xFF, sizeof dm.lit_drawn);
    memset(dm.step_drawn, 0xFF, sizeof dm.step_drawn);
}
static uint32_t dm_sig(void)
{
    uint32_t i, r, n = dm_strips(), sig = hu(hu(hu(hu(hu(13u, settings.palette), ui.row[SCR_DMIX]), ui.hot * 2u + ui.hot_lit),
                                          dm.blk * 64u + n), lane_selected());
    cell_t e;
    for (i = 0; i < n; i++) {
        uint32_t l = (dm_first() + i) % DRUM_LANES;
        sig = hu(hu(sig, lane_col(l)), dl.src[l]);
        for (r = 0; r < NDMX; r++) {
            dm_cell_lane(r, l, &e);
            sig = hs(hu(sig, (uint32_t)e.gv), e.val);
        }
    }
    return sig;
}
static void dm_draw(void)
{
    uint32_t i, n = dm_strips(), sig = dm_sig(), redrawn = 0;
    if (sig != ui.sig[2]) {
        ui.sig[2] = sig;
        redrawn = 1;
        for (i = 0; i < n; i++)
            dm_strip(i);
        dm_redraw();
    }
    for (i = 0; i < n; i++) {                           /* the hits: a flash in the meter's place, falling */
        uint32_t l = (dm_first() + i) % DRUM_LANES, h = (uint32_t)METER_H * dm.lit[l] / 6u;
        if (h == dm.lit_drawn[l])
            continue;
        dm.lit_drawn[l] = (uint8_t)h;
        cv_begin((uint32_t)dm_flash_w(), METER_H, C_LINE);
        cv_rect(0, METER_H - (int32_t)h, dm_flash_w(), (int32_t)h, h == METER_H ? C_WHITE : lane_col(l));
        cv_blit((uint32_t)(dm_x(i) + dm_flash_x()), MIX_Y + MX_FADER_Y);
    }
    if (n == 4u) {
        for (i = 0; i < n; i++) {
            uint32_t l = (dm_first() + i) % DRUM_LANES;
            draw_steps_of(TRK_DRUM, l, (uint32_t)dm_x(i) + 4u, MIX_Y + MX_STEPS_Y, OP_SURF, &dm.step_drawn[l]);
        }
        master_col(redrawn);
    }
}
