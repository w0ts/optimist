/* SPDX-License-Identifier: GPL-3.0-only */
/* The text renderer (gfx.c cv_text, text_w) over every glyph of FONT_S and FONT_L: each character alone and in
 * strings, at positions that clip on every side, in several colours; the canvas is written to OUT. Built twice by
 * tests/run_tests.sh, once with the font header as the build makes it (FONT_BITS 1, a bit a pixel) and once with
 * the alpha format (tools/gen_font.py OUT 4): the two outputs must be the same bytes. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define __attribute__(x)               /* (cv_px's .pool section: the target's) */
static void lcd_sync(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{
    (void)x, (void)y, (void)w, (void)h, (void)p;
}
#include "../firmware/src/gfx.c"

static const uint16_t COLS[] = {0xFFFFu, 0xF800u, 0x07E0u, 0x001Fu, 0x8410u, 0x1234u};

static void frame(FILE *o, const felucca_font_t *f, int32_t x, int32_t y, const char *s, uint16_t c)
{
    int32_t end;
    cv_begin(48, 40, 0);
    end = cv_text(x, y, f, s, c);
    fwrite(&end, sizeof end, 1, o);
    end = text_w(f, s);
    fwrite(&end, sizeof end, 1, o);
    fwrite(cv_px, sizeof cv_px[0], cv_w * cv_h, o);
}

int main(int argc, char **argv)
{
    static const int32_t POS[][2] = {{4, 4}, {0, 0}, {-3, 2}, {-9, -5}, {40, 30}, {44, 36}, {20, -14}, {1, 33}};
    const felucca_font_t *fonts[2] = {&FONT_S, &FONT_L};
    FILE *o = argc > 1 ? fopen(argv[1], "wb") : 0;
    uint32_t fi, ch, p, n = 0;
    char s[8];
    if (!o) {
        fprintf(stderr, "font_test OUT\n");
        return 2;
    }
    for (fi = 0; fi < 2u; fi++)
        for (ch = 1; ch < 256u; ch++)
            for (p = 0; p < sizeof POS / sizeof POS[0]; p++) {
                s[0] = (char)ch;
                s[1] = 0;
                frame(o, fonts[fi], POS[p][0], POS[p][1], s, COLS[(ch + p) % 6u]);
                n++;
            }
    for (fi = 0; fi < 2u; fi++) {
        frame(o, fonts[fi], -5, 3, "Hügelton 0.1 BETA", COLS[0]);
        frame(o, fonts[fi], 2, 1, "abc XYZ -12.5 dB", COLS[5]);
        palette_set(fi);
        frame(o, fonts[fi], 3, 20, "@{|}~\x7f\xa0\xff", C_HI);
        n += 3;
    }
    fclose(o);
    printf("font: %u frames written (FONT_BITS %d)\n", (unsigned)n, FONT_BITS);
    return 0;
}
