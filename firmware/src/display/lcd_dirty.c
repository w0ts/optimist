/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Charles Vestal (fm1-x0x, charlesvestal/fm1-x0x 201b95c, cv_commit_rect in ui.c) */
/* The graph strip sends only the rectangle that changed (FELUCCA_LCD_DIRTY), after X0X 201b95c.
 * The panel's tearing-effect line reaches no GPIO (X0X probed every PA/PB/PC/PH pin), so a
 * transfer the panel's refresh overtakes shows half old, half new; short transfers are what is
 * left. The graph strip (240 x 124, 59.5 KB) is the big one: 16 ms at 30 MHz, about a refresh;
 * a playhead step or a moved curve changes a few columns or rows. Rows and 8-pixel column blocks
 * of the canvas are hashed (FNV-1a) against the last strip sent; the changed rectangle is packed
 * to the front of the canvas (it is rebuilt every frame) and sent as one transfer.
 * Adapted for SLOOP-plus: the strip's position and its skipped top rows (cv_blit_from) are part
 * of the state; any other LCD write over the strip's rows (lcd_window -> lcd_touched) forgets it,
 * so the next strip goes out whole. lcd_px_sent counts the pixels sent (peek it in the emulator). */
#ifndef FELUCCA_LCD_DIRTY
#define FELUCCA_LCD_DIRTY 1
#endif

#if FELUCCA_LCD_DIRTY
#define LD_ROWS 124u                 /* CV_MAX / 240 */
#define LD_COLB (240u / 8u)
static struct {
    uint32_t row[LD_ROWS], col[LD_COLB];
    uint16_t y, h, skip;
    uint8_t valid, own;
} ld;

static void lcd_touched(uint32_t y0, uint32_t y1)
{
    if (!ld.own && ld.valid && y1 >= ld.y && y0 < (uint32_t)ld.y + ld.h)
        ld.valid = 0;
}

static void lcd_dirty_reset(void) { ld.valid = 0; }

/* the canvas (240 wide) to screen row y, its rows from skip on (cv_blit_from) */
static void cv_blit_dirty(uint32_t y, uint32_t skip)
{
    uint32_t colh[LD_COLB], r, c, k, r0 = LD_ROWS, r1 = 0, c0 = LD_COLB, c1 = 0, w, h, all;
    if (cv_w != 240u || cv_h > LD_ROWS || skip >= cv_h) {
        ld.valid = 0;
        cv_blit_from(0, y, skip);
        return;
    }
    all = !ld.valid || ld.y != y || ld.h != cv_h;
    for (c = 0; c < LD_COLB; c++)
        colh[c] = 2166136261u;
    for (r = skip; r < cv_h; r++) {
        const uint16_t *q = cv_px + r * 240u;
        uint32_t hr = 2166136261u;
        for (c = 0; c < LD_COLB; c++) {
            uint32_t hc = colh[c];
            for (k = 0; k < 8u; k++) {
                hr = (hr ^ q[k]) * 16777619u;
                hc = (hc ^ q[k]) * 16777619u;
            }
            colh[c] = hc;
            q += 8;
        }
        if (all || hr != ld.row[r]) {
            if (r0 == LD_ROWS)
                r0 = r;
            r1 = r;
        }
        ld.row[r] = hr;
    }
    if (r0 == LD_ROWS)
        return;                                      /* nothing changed */
    if (all || skip != ld.skip) {                    /* columns hashed over other rows: all of them */
        c0 = 0;
        c1 = LD_COLB - 1u;
    } else {
        for (c = 0; c < LD_COLB; c++)
            if (colh[c] != ld.col[c]) {
                if (c0 == LD_COLB)
                    c0 = c;
                c1 = c;
            }
        if (c0 == LD_COLB)                            /* rows differ, columns not: a hash collision */
            c0 = 0, c1 = LD_COLB - 1u;
    }
    for (c = 0; c < LD_COLB; c++)
        ld.col[c] = colh[c];
    ld.y = (uint16_t)y;
    ld.h = (uint16_t)cv_h;
    ld.skip = (uint16_t)skip;
    ld.valid = 1;
    w = (c1 - c0 + 1u) * 8u;
    h = r1 - r0 + 1u;
    lcd_sync();                                      /* (nothing reads cv_px now; to be sure) */
    for (r = 0; r < h; r++) {                        /* pack: the destination never passes the source */
        const uint16_t *src = cv_px + (r0 + r) * 240u + c0 * 8u;
        uint16_t *dst = cv_px + r * w;
        for (k = 0; k < w; k++)
            dst[k] = src[k];
    }
    ld.own = 1;
    lcd_blit(c0 * 8u, y + r0, w, h, cv_px);
    ld.own = 0;
}
#else
static void lcd_touched(uint32_t y0, uint32_t y1) { (void)y0; (void)y1; }
static void lcd_dirty_reset(void) {}
static void cv_blit_dirty(uint32_t y, uint32_t skip) { cv_blit_from(0, y, skip); }
#endif
