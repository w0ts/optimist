/* SPDX-License-Identifier: GPL-3.0-only */
/* The scope screen (FELUCCA_SCOPE, on unless the full visualiser is built): HOME tapped on TRACKS shows the master mix as
 * a wave that stands still (a rising zero crossing), HOME again or any page closes it. The slim cousin of ui_vis.c's
 * style 1 (SLOOP 2.4, isod89): it reads the same tap (fx.c vis_tap_block, here a 512-frame ring in the pool), but
 * none of the visualiser's other styles, FFT, peaks or note hits. Drawn in the main loop every other frame, in two
 * bands of 120 rows (the canvas holds 124). The scope's own picture, as vis_scope draws it. */
#define SC_N 480u                                       /* frames read: 240 to draw + 240 to find a trigger in */
static int16_t sc_w[SC_N];                              /* this frame's snapshot */
static int32_t sc_scale;                                /* the auto-scale (a smoothed peak) */

static int scope_shown(void) { return scope_on && !ui.menu && cur_page()->scope == SC_TRK; }
static void scope_open(void)
{
    scope_on = 1;
    ui.force = 1;
}
static void scope_update(void)
{
    uint32_t i, w = vis_wr + (VIS_RING - SC_N);          /* (the oldest frames first) */
    int32_t pk = 0, target;
    for (i = 0; i < SC_N; i++) {
        int32_t v = clamp(knee(vis_pcm[0][(w + i) & (VIS_RING - 1u)]), -32767, 32767);
        sc_w[i] = (int16_t)v;
        if (v > pk) pk = v;
        if (-v > pk) pk = -v;
    }
    target = pk < 1600 ? 1600 : pk;
    sc_scale += (target - sc_scale) / 5;
}
static void scope_wave(void)
{
    uint32_t i0 = 0, i;
    int32_t py = 0;
    for (i = 1; i + 240u < SC_N; i++)                   /* a rising zero crossing: the wave stands still */
        if (sc_w[i - 1u] < 0 && sc_w[i] >= 0) {
            i0 = i;
            break;
        }
    cv_rect(0, 120, 240, 1, C_LINE);
    for (i = 0; i < 240u; i += 40u)
        cv_rect((int32_t)i, 116, 1, 9, C_LINE);
    for (i = 0; i < 240u; i++) {
        int32_t y = clamp(120 - sc_w[i0 + i] * 100 / (sc_scale ? sc_scale : 1), 2, 237);
        if (i) {
            cv_line((int32_t)i - 1, py - 1, (int32_t)i, y - 1, TRK_DIM(0));
            cv_line((int32_t)i - 1, py + 2, (int32_t)i, y + 2, TRK_DIM(0));
            cv_line((int32_t)i - 1, py, (int32_t)i, y, trk_col(0));
            cv_line((int32_t)i - 1, py + 1, (int32_t)i, y + 1, trk_col(0));
        }
        py = y;
    }
}
static void scope_draw(void)
{
    uint32_t pass;
    if (!scope_last || ui.force) {
        scope_last = 1;
        lcd_fill(0, 0, 240, 240, C_BLACK);
    } else if (ui.frame & 1u) {
        return;
    }
    scope_update();
    for (pass = 0; pass < 2u; pass++) {
        cv_begin(240, 120, C_BLACK);
        cv_oy = -(int32_t)(pass * 120u);
        scope_wave();
        if (!pass) {                                    /* the name, or a message over it */
            const char *s = ui.msg_t ? ui.msg : "scope";
            cv_rect(0, 0, 240, 20, C_BLACK);
            if (ui.msg_t)
                cv_text(120 - text_w(&FONT_S, s) / 2, 2, &FONT_S, s, C_WHITE);
            else
                cv_text(4, 2, &FONT_S, s, C_DIM);
        }
        cv_oy = 0;
        cv_blit(0, pass * 120u);
    }
}
