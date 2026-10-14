/* SPDX-License-Identifier: GPL-3.0-only */
/* The Optimist boot screen: the logo, an Optimist dinghy drawn from its geometry (no bitmap), "OPTIMIST" and the
 * build's version.
 *
 * The dinghy is line art: straight strokes (LG_SEGS, their end points in px of the logo's box) and the class
 * emblem on the sail. The emblem is a ring with a bar through its lower half, a small serif at each end of the bar.
 * In units of the ring's outer radius R (measured on the reference artwork, a ring 247 px across): inner radius
 * 0.686 R; the bar 0.288 R wide, centred, from 0.061 R below the centre to 1.36 R below it; the serifs 0.024 R past
 * the bar on each side and 0.028 R tall. Each pixel gets its coverage (Q8), kept in the canvas until the colour
 * pass: a stroke from the pixel's distance to its line and its overlap with the stroke's length, the ring from the
 * distance to the centre, the bar and serifs from their overlap with the pixel.
 *
 * FELUCCA_SPLASH 2 (builder: boot logo = ring) draws the emblem alone, large, as the boot screen did before the
 * dinghy. */
#if FELUCCA_SPLASH == 2
#define LOGO_R 50                                        /* the ring's outer radius, px */
#define LOGO_W (2u * LOGO_R)
#define LOGO_H ((uint32_t)(LOGO_R + LOGO_R * 1360 / 1000 + 1))
#define LOGO_Y 20u                                       /* screen row of the ring's top */
#define LOGO_TEXT_Y (LOGO_Y + LOGO_H + 14u)
#else
#define LOGO_R 6                                         /* the emblem's outer radius, px */
#define LG_QX 52                                         /* the emblem's centre in the box, px */
#define LG_QY 34
#define LG_HW 150                                        /* a stroke's half width, Q8 px */
#define LOGO_W 110u
#define LOGO_H 151u
#define LOGO_Y 10u                                       /* screen row of the box's top */
#define LOGO_TEXT_Y (LOGO_Y + LOGO_H + 12u)              /* "OPTIMIST" (FONT_L), the version under it */
#endif
#define LG(m) ((int32_t)(LOGO_R * 256 * (m) / 1000))     /* per mille of R -> Q8 px */
#define LG_RO LG(1000)                                   /* the ring's outer and inner radius */
#define LG_RI LG(686)
#define LG_BAR LG(144)                                   /* the bar's half width, the serifs' */
#define LG_SERIF LG(168)
#define LG_SH LG(28)                                     /* the serifs' height */
#define LG_TOP LG(61)                                    /* the bar's ends, below the centre */
#define LG_BOT LG(1360)
#define LOGO_C RGB(232, 232, 236)                        /* the logo: light on the dark screen */

static int32_t lg_clamp(int32_t v) { return v < 0 ? 0 : v > 256 ? 256 : v; }

/* the length of [lo, hi) inside the pixel [p, p + 256), Q8 */
static int32_t lg_span(int32_t lo, int32_t hi, int32_t p)
{
    int32_t a = lo > p ? lo : p, b = hi < p + 256 ? hi : p + 256;
    return b > a ? b - a : 0;
}

/* coverage of the rectangle |x| < hw, y0 <= y < y1 (centred on the ring) for the pixel at corner (px, py) */
static int32_t lg_rect(int32_t hw, int32_t y0, int32_t y1, int32_t px, int32_t py)
{
    return lg_span(-hw, hw, px) * lg_span(y0, y1, py) >> 8;
}

/* coverage of the emblem for the pixel at corner (px, py), Q8 relative to the ring's centre */
static int32_t lg_cover(int32_t px, int32_t py)
{
    int32_t cx = px + 128, cy = py + 128, d2 = cx * cx + cy * cy, a = 0, b;
    if (d2 < (LG_RO + 256) * (LG_RO + 256))             /* the ring: a disc minus a disc, (r^2 - d^2) / 2r ~ r - d */
        a = lg_clamp((LG_RO * LG_RO - d2) / (2 * LG_RO) + 128) - lg_clamp((LG_RI * LG_RI - d2) / (2 * LG_RI) + 128);
    b = lg_rect(LG_BAR, LG_TOP, LG_BOT, px, py);                                     /* the bar */
    a = b > a ? b : a;
    b = lg_rect(LG_SERIF, LG_TOP, LG_TOP + LG_SH, px, py) + lg_rect(LG_SERIF, LG_BOT - LG_SH, LG_BOT, px, py);
    return b > a ? b : a;                                                            /* (the serifs) */
}

#if FELUCCA_SPLASH == 2
/* the logo into the canvas (LOGO_W x LOGO_H, the ring's centre at (R, R)), then onto the screen */
static void optimist_logo(uint32_t x0, uint32_t y0)
{
    uint32_t x, y;
    cv_begin(LOGO_W, LOGO_H, C_BLACK);
    for (y = 0; y < LOGO_H; y++)
        for (x = 0; x < LOGO_W; x++) {
            uint32_t k = (uint32_t)lg_cover((int32_t)x * 256 - LG_RO, (int32_t)y * 256 - LG_RO), c = LOGO_C;
            if (k)
                cv_px[y * LOGO_W + x] = swap16((((c >> 11) * k >> 8) << 11) | ((((c >> 5) & 63u) * k >> 8) << 5) |
                                               ((c & 31u) * k >> 8));
        }
    cv_blit(x0, y0);
}
#else
/* the dinghy's strokes, x0 y0 x1 y1 in px of the box (an Optimist side on, bow to the left) */
static const uint8_t LG_SEGS[][4] = {
    {15, 49, 15, 113},  {16, 49, 53, 4},    {15, 80, 52, 6},    {53, 4, 89, 102},  /* mast, head, sprit, leech */
    {15, 102, 89, 102}, {70, 50, 58, 54},   {81, 80, 68, 84},                      /* boom, two battens */
    {3, 114, 102, 110}, {3, 114, 7, 122},   {7, 122, 30, 126},  {30, 126, 75, 125}, /* gunwale, the hull */
    {75, 125, 95, 121}, {95, 121, 102, 110},                                         /* (to the transom) */
    {40, 126, 40, 149}, {50, 126, 50, 149}, {40, 149, 50, 149},                      /* daggerboard */
    {96, 111, 95, 131}, {95, 131, 103, 138}, {103, 138, 106, 120}, {106, 120, 102, 110}, /* rudder */
    {75, 113, 100, 111},                                                             /* tiller */
};
#define LG_NSEGS (sizeof LG_SEGS / sizeof LG_SEGS[0])

/* floor(sqrt(v)) */
static uint32_t lg_isqrt(uint32_t v)
{
    uint32_t r = 0, b = 1u << 30;
    while (b > v)
        b >>= 2;
    for (; b; b >>= 2)
        if (v >= r + b) {
            v -= r + b;
            r = (r >> 1) + b;
        } else
            r >>= 1;
    return r;
}

/* keep the larger coverage at (x, y) of the canvas */
static void lg_max(uint32_t x, uint32_t y, int32_t k)
{
    uint16_t *p = &cv_px[y * LOGO_W + x];
    if (k > (int32_t)*p)
        *p = (uint16_t)k;
}

/* one stroke into the canvas's coverage: from its line (the distance, Q8) and its length, its ends a half width on */
static void lg_seg(const uint8_t *s)
{
    int32_t dx = s[2] - s[0], dy = s[3] - s[1], len = (int32_t)lg_isqrt((uint32_t)(dx * dx + dy * dy) << 8); /* Q4 */
    int32_t x0 = (s[0] < s[2] ? s[0] : s[2]) - 2, x1 = (s[0] > s[2] ? s[0] : s[2]) + 2, x, y;
    int32_t y0 = (s[1] < s[3] ? s[1] : s[3]) - 2, y1 = (s[1] > s[3] ? s[1] : s[3]) + 2;
    for (y = y0 < 0 ? 0 : y0; y <= y1 && y < (int32_t)LOGO_H; y++)
        for (x = x0 < 0 ? 0 : x0; x <= x1 && x < (int32_t)LOGO_W; x++) {
            int32_t rx = (x - s[0]) * 256 + 128, ry = (y - s[1]) * 256 + 128;   /* the pixel's centre from the start */
            int32_t d = (rx * dy - ry * dx) * 16 / len, t = (rx * dx + ry * dy) * 16 / len, a, b;
            a = lg_clamp(LG_HW + 128 - (d < 0 ? -d : d));
            b = lg_clamp(t + LG_HW + 128);
            t = lg_clamp(len * 16 - t + LG_HW + 128);
            lg_max((uint32_t)x, (uint32_t)y, a * (b < t ? b : t) >> 8);
        }
}

/* the logo into the canvas (LOGO_W x LOGO_H): the coverage of each part, then its colour; then onto the screen */
static void optimist_logo(uint32_t x0, uint32_t y0)
{
    uint32_t x, y, i;
    cv_begin(LOGO_W, LOGO_H, C_BLACK);
    for (i = 0; i < LG_NSEGS; i++)
        lg_seg(LG_SEGS[i]);
    for (y = LG_QY - LOGO_R - 1; y <= LG_QY + LOGO_R * 1360 / 1000 + 1; y++)
        for (x = LG_QX - LOGO_R - 1; x <= LG_QX + LOGO_R + 1; x++)
            lg_max(x, y, lg_cover(((int32_t)x - LG_QX) * 256, ((int32_t)y - LG_QY) * 256));
    for (i = 0; i < LOGO_W * LOGO_H; i++) {
        uint32_t k = cv_px[i], c = LOGO_C;
        if (k)
            cv_px[i] = swap16((((c >> 11) * k >> 8) << 11) | ((((c >> 5) & 63u) * k >> 8) << 5) |
                              ((c & 31u) * k >> 8));
    }
    cv_blit(x0, y0);
}
#endif

/* the boot screen: the logo, the name in the theme's colour, the version from the build (VERSION) */
static void boot_splash(void)
{
    const char *v = FELUCCA_VERSION, *p = "OPTIMIST ";
    uint32_t i = 0;
    while (p[i] && v[i] == p[i])
        i++;
    if (!p[i])
        v += i;                                          /* "OPTIMIST 0.1" -> "0.1" (a release says "0.1 BETA") */
    lcd_fill(0, 0, 240, 240, C_BLACK);
    optimist_logo((240u - LOGO_W) / 2u, LOGO_Y);
    cv_begin(240, 56, C_BLACK);
    cv_text(120 - text_w(&FONT_L, "OPTIMIST") / 2, 0, &FONT_L, "OPTIMIST", C_HI);
    cv_text(120 - text_w(&FONT_S, v) / 2, 40, &FONT_S, v, C_AMB);
    cv_blit(0, LOGO_TEXT_Y);
}
