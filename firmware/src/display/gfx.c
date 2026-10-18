/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Copyright (C) 2026 Zakaria Chowdhury (Flowstate, flowstate-fm1 6a8ef32: the scaled font in cv_text / text_w) */
/* Small-canvas renderer (no full framebuffer). Draw text/lines into
 * an off-screen strip, then blit it in one DMA transfer. Pixels are stored
 * byte-swapped (the panel takes RGB565 big-endian). */
typedef struct {               /* proportional, see tools/gen_font.py */
    uint8_t h;                 /* drawn height (the bitmap's rows x scale) */
    uint8_t pad;               /* bitmap starts this many pixels left of the pen (drawn) */
    uint8_t first, last;
    uint8_t scale;             /* each bitmap pixel drawn scale x scale: FONT_L is FONT_S's arrays at 2 */
    const uint8_t *adv;        /* advance per glyph (bitmap pixels) */
    const uint8_t *bw;         /* bitmap width per glyph (starts FONT_PAD left of the pen) */
    const uint16_t *off;       /* byte offset of each glyph */
    const uint8_t *data;
} felucca_font_t;
#include "felucca_font.h"

#define CV_MAX (240u * 124u)      /* the graph strip is 240 x 124 */
static uint16_t cv_px[CV_MAX] __attribute__((section(".pool")));
static uint32_t cv_w, cv_h;
static int32_t cv_oy;            /* y offset for graph drawing */

#define RGB(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))
#define C_BLACK 0x0000u
#define C_WHITE 0xFFFFu              /* accent only: what is being touched / where we are */
/* the status colours, the web editor's (tools/colors.json "status", build/gen/felucca_colors.h): colour carries
 * meaning, in every palette. The engine and drum-kind colours: ui_colors.c */
#include "felucca_colors.h"
#define C_OK COL_ST_OK               /* saved, fine, playing */
#define C_WARN COL_ST_WARN           /* a notice: another build, MISSING, EXPERIMENTAL */
#define C_ERR COL_ST_ERR             /* an error: damaged, full, clipping; recording */
#define C_AUTO COL_MK_AUTO           /* a step with automation (a lock or motion): STEP's grid, the mixer's strips */
#define C_NOTE COL_MK_NOTE           /* the note SELECT picked on a held step of several: STEP's roll */
static const uint16_t C_STATUS[4] = {0, C_OK, C_WARN, C_ERR};
/* The screen is five steps of one colour, darkest to brightest, plus white, on black: nothing fills a background
 * with grey (the user, 2026-10-10: "the grey background is just too bright. use black"). Step 1 draws lines and
 * outlines only, so it is bright enough to read as a line on black; step 2 (dim text, units) stays legible there.
 * Palettes are picked in the HOME-hold menu (COLOR). */
typedef struct {
    const char *name;
    uint16_t c[5];
} palette_t;
static const palette_t PALETTES[] = {
    {"GREEN", {RGB(0, 58, 18), RGB(0, 104, 38), RGB(16, 150, 58), RGB(56, 200, 92), RGB(120, 255, 146)}},
    {"AMBER", {RGB(84, 36, 0), RGB(136, 62, 0), RGB(180, 88, 0), RGB(225, 120, 8), RGB(255, 166, 40)}},
    {"CYAN", {RGB(0, 44, 72), RGB(0, 78, 120), RGB(16, 120, 170), RGB(56, 172, 222), RGB(140, 222, 255)}},
    {"RED", {RGB(74, 12, 12), RGB(124, 24, 18), RGB(180, 40, 28), RGB(226, 64, 48), RGB(255, 112, 92)}},
    {"MONO", {RGB(58, 58, 58), RGB(104, 104, 104), RGB(146, 146, 146), RGB(190, 190, 190), RGB(228, 228, 228)}},
};
#define NPALETTES (sizeof(PALETTES) / sizeof(PALETTES[0]))
static uint16_t pal[5];
#define C_LINE pal[0]                /* 1 rules, separators */
#define C_DIM pal[1]                 /* 2 inactive, empty steps, units */
#define C_GRAY pal[2]                /* 3 labels */
#define C_AMB pal[3]                 /* 4 secondary text */
#define C_HI pal[4]                  /* 5 values, curves */

static void palette_set(uint32_t i)
{
    uint32_t k;
    for (k = 0; k < 5u; k++)
        pal[k] = PALETTES[i % NPALETTES].c[k];
}

/* the text colour on a filled background bg: black or white, whichever contrasts more (WCAG: black when bg's relative
 * luminance is over 0.179). sRGB's curve as 0.75 x^2 + 0.25 x^3, x in 1/1024, the threshold 184 / 1024 (the middle of
 * the range that agrees with WCAG's exact choice over the 565 space but at near ties: tests/ui_optimist_curline.h) (the user,
 * 2026-10-18: text on a coloured background "very hard to read") */
#define INK_LIN(x) ((3u * (x) * (x) + (x) * (x) / 1024u * (x)) / 4096u)
static uint16_t ink_on(uint16_t bg)
{
    uint32_t r = (bg >> 11) * 1024u / 31u, g = ((bg >> 5) & 63u) * 1024u / 63u, b = (bg & 31u) * 1024u / 31u;
    return 2126u * INK_LIN(r) + 7152u * INK_LIN(g) + 722u * INK_LIN(b) > 184u * 10000u ? C_BLACK : C_WHITE;
}

static inline uint16_t swap16(uint32_t c) { return (uint16_t)(((c >> 8) & 0xFFu) | ((c & 0xFFu) << 8)); }

static void cv_begin(uint32_t w, uint32_t h, uint16_t bg)
{
    uint32_t i, n;
    if (w * h > CV_MAX)
        h = CV_MAX / w;
    lcd_sync();                     /* the last blit may still read cv_px */
    cv_w = w;
    cv_h = h;
    n = w * h;
    for (i = 0; i < n; i++)
        cv_px[i] = swap16(bg);
}

/* (the Optimist UI's help line has a band of its own, op_graph.c OP_FOOT: no clip) */
static void cv_blit(uint32_t x, uint32_t y) { lcd_blit(x, y, cv_w, cv_h, cv_px); }

/* canvas rows r0 .. cv_h-1 only, to screen row y + r0 */
static void cv_blit_from(uint32_t x, uint32_t y, uint32_t r0)
{
    if (r0 < cv_h)
        lcd_blit(x, y + r0, cv_w, cv_h - r0, cv_px + r0 * cv_w);
}

static inline void cv_pset(int32_t x, int32_t y, uint16_t c)
{
    y += cv_oy;
    if ((uint32_t)x < cv_w && (uint32_t)y < cv_h)
        cv_px[(uint32_t)y * cv_w + (uint32_t)x] = swap16(c);
}

static void cv_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)
{
    int32_t x1 = x + w, y1 = y + h + cv_oy, i;
    uint16_t sc = swap16(c);
    y += cv_oy;                             /* clipped once, then filled row by row */
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    if (x1 > (int32_t)cv_w)
        x1 = (int32_t)cv_w;
    if (y1 > (int32_t)cv_h)
        y1 = (int32_t)cv_h;
    for (; y < y1; y++)
        for (i = x; i < x1; i++)
            cv_px[(uint32_t)y * cv_w + (uint32_t)i] = sc;
}

static void cv_frame(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)   /* a 1 px outline */
{
    cv_rect(x, y, w, 1, c);
    cv_rect(x, y + h - 1, w, 1, c);
    cv_rect(x, y, 1, h, c);
    cv_rect(x + w - 1, y, 1, h, c);
}
/* a tile, a card, a cell: filled in its colour when it has one; black (empty, off) is an outline in C_LINE on the
 * black ground, never a grey fill */
static void cv_box(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)
{
    if (c == C_BLACK)
        cv_frame(x, y, w, h, C_LINE);
    else
        cv_rect(x, y, w, h, c);
}

static void cv_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c)
{
    int32_t dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int32_t dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int32_t err = dx + dy, guard = 2000;
    while (guard--) {                       /* bounded: a line is never longer than 480 px */
        int32_t e2 = 2 * err;               /* both tests use the same error value */
        cv_pset(x0, y0, c);
        if (x0 == x1 && y0 == y1)
            break;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

/* glyph index of a character: lower case folds to upper case when the font
 * has none, anything missing (and C1 controls) draws as '?' */
static uint32_t glyph(const felucca_font_t *f, uint32_t ch)
{
    if (ch >= 'a' && ch <= 'z' && f->last < 'a')
        ch -= 32u;
    if (ch < f->first || ch > f->last || (ch >= 127u && ch < 160u))
        ch = '?';
    return ch - f->first;
}

/* text, alpha-blended onto black with colour c; returns the end x. A scaled font draws each bitmap pixel as a
 * k x k block: FONT_L (k 2) is pixel for pixel the 2x nearest-neighbour bitmap it used to have (tools/gen_font.py) */
#if FONT_BITS == 1
/* the pixel font (tools/gen_font.py FONT_BITS 1): a bit a pixel, alpha 15 = colour c, the PAD columns not stored
 * (empty): the same pixels as the alpha format's (tests/font_test.c) */
static int32_t cv_text(int32_t x, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    uint32_t k = f->scale;
    for (; *s; s++) {
        uint32_t gi = glyph(f, (uint8_t)*s), gx, gy, w, bpr;
        const uint8_t *gd;
        int32_t x0;
        w = f->bw[gi] - 2u * FONT_PAD;
        bpr = (w + 7u) / 8u;
        gd = f->data + f->off[gi];
        x0 = x - f->pad + (int32_t)(FONT_PAD * k);
        if (k == 1u) {
            for (gy = 0; gy < f->h; gy++)
                for (gx = 0; gx < w; gx++)
                    if ((gd[gy * bpr + gx / 8u] << (gx & 7u)) & 0x80u)
                        cv_pset(x0 + (int32_t)gx, y + (int32_t)gy, c);
        } else {                                     /* (cv_rect clips as cv_pset does, pixel by pixel) */
            for (gy = 0; gy * k < f->h; gy++)          /* (the bitmap rows: h / k, without a divide) */
                for (gx = 0; gx < w; gx++)
                    if ((gd[gy * bpr + gx / 8u] << (gx & 7u)) & 0x80u)
                        cv_rect(x0 + (int32_t)(gx * k), y + (int32_t)(gy * k), (int32_t)k, (int32_t)k, c);
        }
        x += f->adv[gi] * (int32_t)k;
    }
    return x;
}
#else
static int32_t cv_text(int32_t x, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    uint16_t ramp[16];
    uint32_t r = c >> 11, g = (c >> 5) & 63u, b = c & 31u, a, k = f->scale;
    for (a = 0; a < 16u; a++)
        ramp[a] = (uint16_t)(((r * a / 15u) << 11) | ((g * a / 15u) << 5) | (b * a / 15u));
    for (; *s; s++) {
        uint32_t gi = glyph(f, (uint8_t)*s), gx, gy, w, bpr;
        const uint8_t *gd;
        w = f->bw[gi];
        bpr = (w + 1u) / 2u;
        gd = f->data + f->off[gi];
        if (k == 1u) {
            for (gy = 0; gy < f->h; gy++)
                for (gx = 0; gx < w; gx++) {
                    uint32_t v = gd[gy * bpr + gx / 2u];
                    v = (gx & 1u) ? (v & 15u) : (v >> 4);
                    if (v)
                        cv_pset(x - f->pad + (int32_t)gx, y + (int32_t)gy, ramp[v]);
                }
        } else {                                     /* (cv_rect clips as cv_pset does, pixel by pixel) */
            for (gy = 0; gy * k < f->h; gy++)          /* (the bitmap rows: h / k, without a divide) */
                for (gx = 0; gx < w; gx++) {
                    uint32_t v = gd[gy * bpr + gx / 2u];
                    v = (gx & 1u) ? (v & 15u) : (v >> 4);
                    if (v)
                        cv_rect(x - f->pad + (int32_t)(gx * k), y + (int32_t)(gy * k), (int32_t)k, (int32_t)k, ramp[v]);
                }
        }
        x += f->adv[gi] * (int32_t)k;
    }
    return x;
}
#endif

static int32_t text_w(const felucca_font_t *f, const char *s)
{
    int32_t w = 0;
    for (; *s; s++)
        w += f->adv[glyph(f, (uint8_t)*s)];
    return w * f->scale;
}

/* one-shot: text in a box, cleared to black, blitted */
static void draw_text_box(uint32_t x, uint32_t y, uint32_t w, const felucca_font_t *f, const char *s,
                          uint16_t c, int align)
{
    int32_t tw = text_w(f, s), tx = 0;
    cv_begin(w, f->h, C_BLACK);
    if (align == 1)
        tx = ((int32_t)w - tw) / 2;
    else if (align == 2)
        tx = (int32_t)w - tw;
    cv_text(tx, 0, f, s, c);
    cv_blit(x, y);
    lcd_sync();                     /* one-shots (boot, crash, UBOOT, update) finish here */
}

#include "lcd_dirty.c"           /* the graph strips: only the changed rectangle (FELUCCA_LCD_DIRTY) */
