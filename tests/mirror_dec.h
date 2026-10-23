/* SPDX-License-Identifier: GPL-3.0-only */
/* The screen mirrored: the client's side (io/editor/ed_mirror_enc.h; web/EDITOR_PROTOCOL.md "The screen mirrored"), the model of
 * ios/OptimistMirror's MirrorDecoder.swift. md_frame takes one SysEx frame (F0 .. F7) and applies it to md_fb (RGB565, 240 x 240).
 * Included by tests/mirror_test.c and tests/ui_optimist_test.c (MIRROR_CHECK). */
#ifndef MIRROR_DEC_H
#define MIRROR_DEC_H
static uint16_t md_fb[240 * 240];
static struct {
    uint32_t x, y, w, h, at;            /* the BLIT's rectangle, the next pixel in it */
    uint16_t slot[7];
    uint32_t rr;
} md;
static uint32_t md_2(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 7; }
static void md_px(uint16_t c, uint32_t n)
{
    for (; n && md.at < md.w * md.h; n--, md.at++) {
        uint32_t x = md.x + md.at % md.w, y = md.y + md.at / md.w;
        if (x < 240u && y < 240u)
            md_fb[y * 240u + x] = c;
    }
}
/* 1: applied; 0: not a mirror frame or a bad one */
static int md_frame(const uint8_t *f, uint32_t n)
{
    uint32_t i, op;
    if (n < 7u || f[0] != 0xF0u || f[1] != 0x7Du || f[2] != 0x46u || f[3] != 0x4Cu || f[4] != 106u || f[n - 1u] != 0xF7u)
        return 0;
    op = f[5];
    i = 6;
    n--;                                /* (F7) */
    if (op <= 1u) {
        if (i + 8u > n)
            return 0;
        md.x = md_2(f + i), md.y = md_2(f + i + 2u), md.w = md_2(f + i + 4u), md.h = md_2(f + i + 6u), md.at = 0;
        i += 8u;
    }
    if (op == 0u) {
        if (i + 3u > n)
            return 0;
        md_px((uint16_t)(md_2(f + i) | (uint32_t)f[i + 2u] << 14), md.w * md.h);
        return 1;
    }
    if (op == 1u) {
        for (i = 0; i < 7u; i++)
            md.slot[i] = 0;
        md.rr = 1;
        i = 14;
    } else if (op != 2u) {
        return 0;
    }
    while (i < n) {
        uint32_t t = f[i++], c = t >> 3, len = c + 1u, s = t & 7u, g, k, j, bg;
        if (c == 13u || (c == 15u && s == 7u)) {   /* a length: 1 byte below 64, else (L & 63) | 64, L >> 6 */
            if (i + 1u > n || ((f[i] & 64u) && i + 2u > n))
                return 0;
            len = (c == 13u ? 14u : 8u) + ((f[i] & 64u) ? (f[i] & 63u) | (uint32_t)f[i + 1u] << 6 : f[i]);
            i += (f[i] & 64u) ? 2u : 1u;
        }
        if (c == 15u) {                 /* the pixels one row up */
            if (s < 7u)
                len = s + 1u;
            if (md.at < md.w)
                return 0;
            for (k = 0; k < len && md.at < md.w * md.h; k++) {
                uint32_t x = md.x + md.at % md.w, y = md.y + md.at / md.w;
                if (x < 240u && y < 240u && y >= 1u)
                    md_fb[y * 240u + x] = md_fb[(y - 1u) * 240u + x];
                md.at++;
            }
            continue;
        }
        if (s == 7u) {
            if (i + 3u > n)
                return 0;
            md.slot[md.rr] = (uint16_t)(md_2(f + i) | (uint32_t)f[i + 2u] << 14);
            s = md.rr;
            md.rr = md.rr < 6u ? md.rr + 1u : 1u;
            i += 3u;
        }
        if (c == 14u) {                 /* a bitmap: g << 3 | the 0s' slot, then g + 1 bytes of 7 pixels, bit 6 first */
            if (i + 1u > n || (f[i] & 7u) == 7u)
                return 0;
            g = (f[i] >> 3) + 1u, bg = md.slot[f[i] & 7u];
            if (i + 1u + g > n)
                return 0;
            for (k = 0; k < g; k++)
                for (j = 0; j < 7u; j++)
                    md_px((f[i + 1u + k] >> (6u - j)) & 1u ? md.slot[s] : bg, 1);
            i += 1u + g;
            continue;
        }
        md_px(md.slot[s], len);
    }
    return 1;
}
#endif
