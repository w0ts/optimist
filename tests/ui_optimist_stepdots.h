/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c, the drum steps as dots (the user, 2026-10-24, after Melodee): on STEP's drum grid and in the
 * mixer's drum rows an empty step is a very small dot at its cell's centre, every 4th (a beat's first) a little bigger;
 * a hit stays its square. No foot line under the steps any more. Included after the helpers */

/* the dot op_draw.c's step_dot draws in a cell at x, y, w x h: 1 x 1, a beat's 2 x 2, C_DIM, the rest of the cell black */
static int dots_cell(uint32_t x, uint32_t y, uint32_t w, uint32_t h, int beat)
{
    uint32_t s = beat ? 2u : 1u, cx = x + (w - s) / 2u, cy = y + (h - s) / 2u, i, j, n = 0;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            n += !px_at(x + i, y + j, C_BLACK);
    return px_in(cx, cy, s, s, C_DIM) && px_at(cx, cy, C_DIM) && px_at(cx + s - 1u, cy + s - 1u, C_DIM) && n == s * s;
}

static void stepdots_tests(void)
{
    track_t *t = TDRUM;
    uint32_t l = 2, y, h, x0, x1;
    reset_ui();
    lane_transport(0);
    song.sel = TRK_DRUM;
    track_defaults_steps(t);
    memset(t->dstep, 0, sizeof t->dstep);
    t->p[P_SLEN] = 16;
    dstep_set(&t->dstep[5], l, LV_NORM, 0);
    lane_select(l);
    ui.lane = 1;
    op_enter(SCR_STEP);
    ui.force = 1;
    frames(2);
    y = SP_Y + (uint32_t)SG_LY(l), h = (uint32_t)(SG_LY(l + 1u) - SG_LY(l)) - 1u;
    x0 = (uint32_t)SG_X, x1 = (uint32_t)(SG_X + SG_CW);
    check(dots_cell(x1, y, SG_CW - 2, h, 0), "drum STEP: an empty step is a 1 px dot at its cell's centre, nothing else in the cell");
    check(dots_cell(x0, y, SG_CW - 2, h, 1) && dots_cell(x0 + 4u * SG_CW, y, SG_CW - 2, h, 1),
          "... a beat's first step (1, 5, ...) a 2 x 2 dot");
    check(px_in(x0 + 5u * SG_CW, y, SG_CW - 2, h, lvl_col(op_lane_col(l), LV_NORM)) &&
          px_at(x0 + 5u * SG_CW, y, lvl_col(op_lane_col(l), LV_NORM)) && px_at(x0 + 5u * SG_CW + SG_CW - 3u, y + h - 1u, lvl_col(op_lane_col(l), LV_NORM)),
          "... a hit is still its square, corner to corner");
    check(!px_in(x1, SP_Y + (uint32_t)SG_TOP + (uint32_t)SG_H - 1u, SG_CW - 2, 1, C_LINE), "... and no foot line under the steps");
    ppm("opt-step-dots");
    {   /* the mixer's drum rows: a lane's empty step the same dot */
        mrect_t q;
        uint32_t r = MXR_LANE0 + l, cy, cx;
        lane_select(l);
        op_enter(SCR_HOME);
        ui.row[SCR_HOME] = (uint8_t)r;
        ui.force = 1;
        frames(2);
        mx_rect(r, &q);
        cy = (uint32_t)(OP_PY + q.y + q.sy), cx = (uint32_t)(q.x + q.sx);
        check(dots_cell(cx + (uint32_t)q.sp, cy, (uint32_t)q.sp - 1u, (uint32_t)q.sh, 0) &&
              dots_cell(cx, cy, (uint32_t)q.sp - 1u, (uint32_t)q.sh, 1),
              "the mixer's lane row: an empty step a 1 px dot, a beat's a 2 x 2 dot");
        ppm("opt-mixer-dots");
    }
}
