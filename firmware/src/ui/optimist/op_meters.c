/* SPDX-License-Identifier: GPL-3.0-only */
/* The load meters (SYSTEM > METERS: OFF / CPUS / MEMORY / BOTH, core/meters.h; the user, 2026-11-01: "one or two pixel lines at the
 * bottom of the screen for cpu ... meters: cpus, memory, both"). Thin lines on the screen's last OP_MTR rows (op_graph.c: every
 * screen's content, the help band and NAME's hints end above them, so nothing is drawn over):
 *   CPUS     two 1-px lines, CPU0 (row 238) then CPU1 (row 239)
 *   MEMORY   one 2-px line (rows 238-239): the arena's fill (op_project.c info_ram_pct), as INFO's RAM bar
 *   BOTH     memory (row 237, 1 px), CPU0, CPU1
 * A line runs from the left over 240 px = 100 % in the load's colours (op_project.c load_col: green under 60 %, amber from 60,
 * red over 85), the rest of it black. CPU0 is the block's time against its budget (song.cpu_q8, the figure of INFO's CPU0 line),
 * CPU1 its share (dual_load.h; a single core: its line stays black). Shown with a short peak-hold: a rise at once, a fall by a
 * quarter of the gap a look, so a spike stays readable.
 * Cost: a look every MTR_PERIOD frames (~5 a second at the UI's ~60), a line drawn again only where its pixel width or colour
 * changed, and then only the pixels that changed (a fill of the gap): the screen mirror carries a few small fills, never a
 * 240-px blit. The memory line of the header (op_draw.c memline_*: the same fill on rows 0-1, always there since 2026-10-29)
 * stays as it is. */
#define MTR_PERIOD 12u                                  /* frames between looks: at most 5 redraws a second */
enum { ML_MEM, ML_C0, ML_C1, ML_N };
static struct {
    uint8_t t;                                          /* frames since the last look */
    uint8_t mode;                                       /* the mode drawn (+ 1: 0 = nothing known, the strip to clear) */
    int16_t px[ML_N];                                   /* each line as on the screen: its filled width, -1 not drawn */
    uint16_t col[ML_N];                                 /* ... and its colour */
    uint16_t hold[ML_N];                                /* the held values, 1/256 of the full scale */
    uint32_t draws;                                     /* looks that drew (the tests count them) */
} mtr;
/* the pure parts (the tests check them): the width of a line at q8 / 256 of the scale, and the line's colour */
static int32_t mtr_px(uint32_t q8) { return (int32_t)((q8 > 256u ? 256u : q8) * 240u / 256u); }
static uint16_t mtr_col(uint32_t q8) { return load_col((int32_t)(q8 * 100u / 256u)); }
static void mtr_rows(uint32_t l, int32_t *y, int32_t *h)   /* the rows of line l in the mode shown (h 0: not shown) */
{
    *y = 0;
    *h = 0;
    if (l == ML_MEM && meters_mem()) {
        *h = meters_mode == MTR_MEM ? 2 : 1;
        *y = meters_mode == MTR_MEM ? 238 : 237;
    } else if (l == ML_C0 && meters_cpu()) {
        *h = 1;
        *y = 238;
    } else if (l == ML_C1 && meters_cpu()) {
        *h = 1;
        *y = 239;
    }
}
static uint32_t mtr_now(uint32_t l)                     /* the load of line l now, 1/256 */
{
    uint32_t v = 0;
    if (l == ML_MEM)
        v = info_ram_pct() * 256u / 100u;
    else if (l == ML_C0)
        v = song.cpu_q8;
#if DUAL_LOAD
    else if (dual_c1_q8 != DUAL_C1_OFF)
        v = dual_c1_q8;
#endif
    return v > 256u ? 256u : v;
}
static void mtr_line(uint32_t l, int32_t px, uint16_t col)   /* line l drawn as px wide in col, the pixels that changed only */
{
    int32_t y, h, was = mtr.px[l];
    mtr_rows(l, &y, &h);
    if (!h)
        return;
    if (was < 0 || (mtr.col[l] != col && px && was)) {   /* (not drawn yet, or the colour moved: the whole line) */
        if (px)
            lcd_fill(0, y, px, h, col);
        if (px < 240)
            lcd_fill(px, y, 240 - px, h, C_BLACK);
    } else if (px > was) {
        lcd_fill(was, y, px - was, h, col);
    } else if (px < was) {
        lcd_fill(px, y, was - px, h, C_BLACK);
    }
    mtr.px[l] = (int16_t)px;
    mtr.col[l] = col;
}
static void mtr_clear(void)                             /* the strip black, every line to be drawn again */
{
    uint32_t l;
    lcd_fill(0, 240 - OP_MTR, 240, OP_MTR, C_BLACK);
    for (l = 0; l < ML_N; l++)
        mtr.px[l] = -1;
}
/* a repaint (op_state.c op_repaint): every row of the strip drawn again as it is, a line's (its width and colour as shown) or
 * black, nothing looked at anew (a mirror's repair or self-check gets the same rectangles frame after frame) */
static void mtr_again(void)
{
    uint32_t l, r, own = 0;
    int32_t y, h, px;
    for (l = 0; l < ML_N; l++) {
        mtr_rows(l, &y, &h);
        if (!h || mtr.px[l] < 0)
            continue;
        own |= ((1u << h) - 1u) << (y - (240 - OP_MTR));
        px = mtr.px[l];
        mtr.px[l] = -1;                                 /* (drawn whole: mtr_line's first draw) */
        mtr_line(l, px, mtr.col[l]);
    }
    for (r = 0; r < OP_MTR; r++)
        if (!(own >> r & 1u))
            lcd_fill(0, 240 - OP_MTR + r, 240, 1, C_BLACK);
}
static void mtr_draw(void)
{
    uint32_t l, now = (uint32_t)meters_mode + 1u, changed = 0;
    if (ui.force)                                       /* (the screen was blanked: the lines are gone) */
        mtr.mode = 0;
    if (mtr.mode != now) {                              /* (a new mode, or the screen blank: the strip black, a look now) */
        mtr.mode = (uint8_t)now;
        mtr_clear();
        mtr.t = (uint8_t)MTR_PERIOD;
    } else if (op_again & OPA_MTR)                      /* (a repaint, op_state.c op_repaint: the strip drawn again as it is) */
        mtr_again();
    op_again &= (uint8_t)~OPA_MTR;
    if (meters_mode == MTR_OFF)
        return;
    if (++mtr.t < MTR_PERIOD)
        return;
    mtr.t = 0;
    for (l = 0; l < ML_N; l++) {
        uint32_t v = mtr_now(l), h = mtr.hold[l];
        h = v >= h ? v : h - (h - v + 3u) / 4u;          /* (the fall: a quarter of the gap, at least 1) */
        mtr.hold[l] = (uint16_t)h;
        if (mtr_px(h) != mtr.px[l] || mtr.px[l] < 0 || mtr_col(h) != mtr.col[l]) {
            mtr_line(l, mtr_px(h), mtr_col(h));
            changed = 1;
        }
    }
    mtr.draws += changed;
}
