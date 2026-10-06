/* SPDX-License-Identifier: GPL-3.0-only */
/* The Optimist boot screen: the logo, drawn from its geometry (no bitmap), "OPTIMIST" and the build's version.
 *
 * The logo is a ring with a bar through its lower half, a small serif at each end of the bar. In units of the
 * ring's outer radius R (measured on the reference artwork, a ring 247 px across): inner radius 0.686 R; the
 * bar 0.288 R wide, centred, from 0.061 R below the centre to 1.36 R below it; the serifs 0.024 R past the bar
 * on each side and 0.028 R tall. Each pixel gets its coverage (Q8): the ring from the distance to the centre,
 * the bar and serifs from their overlap with the pixel. */
#define LOGO_R 50                                        /* outer radius, px */
#define LG(m) ((int32_t)(LOGO_R * 256 * (m) / 1000))     /* per mille of R -> Q8 px */
#define LG_RO LG(1000)                                   /* the ring's outer and inner radius */
#define LG_RI LG(686)
#define LG_BAR LG(144)                                   /* the bar's half width, the serifs' */
#define LG_SERIF LG(168)
#define LG_SH LG(28)                                     /* the serifs' height */
#define LG_TOP LG(61)                                    /* the bar's ends, below the centre */
#define LG_BOT LG(1360)
#define LOGO_W (2u * LOGO_R)
#define LOGO_H ((uint32_t)(LOGO_R + LOGO_R * 1360 / 1000 + 1))
#define LOGO_Y 20u                                       /* screen row of the ring's top */
#define LOGO_TEXT_Y (LOGO_Y + LOGO_H + 14u)              /* "OPTIMIST" (FONT_L), the version under it */
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

/* coverage of the logo for the pixel at corner (px, py), Q8 relative to the ring's centre */
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
