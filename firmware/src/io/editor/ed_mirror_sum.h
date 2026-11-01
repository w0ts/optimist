/* SPDX-License-Identifier: GPL-3.0-only */
/* Editor protocol: the screen mirrored, its self-check (2026-11-01; ed_mirror.c, web/EDITOR_PROTOCOL.md "The screen mirrored",
 * INFO "opti_mirsum_1"). Included by ed_mirror.c and tests/ui_optimist_test.c, after ed_mirror_enc.h.
 * The user saw lines missing after a full screen refresh over Bluetooth: a lost frame's rows that the repaint never drew again
 * stayed wrong on the client for good. So the FM-1 now tells the client, about once a second, what its picture must be, band by
 * band, and the client asks for the bands that differ:
 *   106 op 7 SUMS   ref, valid (5 x 7 bit: bit b band b), then MSUM_BANDS hashes (3 x 7 bit each), then a Fletcher sum A, B of
 *                   7D .. the last hash byte (as a checked frame's), F7. Band b: rows 8b .. 8b + 7. ref: the seq of the next
 *                   checked frame, so the client compares only when it has every frame before (its next seq is ref).
 *                   A band's hash: the sum (mod 2^32, its low 21 bits sent) over its 1920 pixels of msum_mix(y * 240 + x, c),
 *                   c the pixel as RGB565 (the client's picture: the colour a FILL carries, a BLIT's pixel as decoded).
 *   106 bits 112    (bits 4, 5 and 6 together: never a repair, an ACK or a keep-alive) then 5 x 7 bit, bit b band b: the client's
 *                   bands that differ; the FM-1 forgets those rows and repaints in place (only they go out). All 0: no band, the
 *                   client takes SUMS (opti_mirsum_1): sent again each time it has had none for a while. An old firmware reads it
 *                   as a keep-alive and never sends SUMS; an old client never asks and gets none.
 * The FM-1 has no frame buffer: it knows its picture only as it draws it. So a check repaints the panel in place (op_repaint, the
 * pixels the same: nothing to see) and hashes that frame's writes as they go out to the client (a muted one, never sent, not).
 * Writes overlap (the mixer's cells over its rows, STEP's grid): a pixel counts for the last write over it only, which the
 * check knows from the rectangles of the last check's frame (msum_r[prev]: the same screen draws the same rectangles); a frame
 * whose rectangles differ from the last one's proves nothing and the next frame is checked again. A band not covered whole (a
 * part drawn on a later frame) is left out of the valid bits. Cost: one frame drawn again a second, 101 bytes */
#ifndef ED_MIRROR_SUM_H
#define ED_MIRROR_SUM_H
enum { MSUM_OP = 7, MSUM_BANDS = 30, MSUM_ROWS = 8, MSUM_PX = 240 * MSUM_ROWS, MSUM_RECTS = 192, MSUM_SPANS = 8,
       MSUM_ASK = 112,                  /* 106's bits for the bands request: MIR_FIX_BIT | MIR_ACK_BIT | MIR_QUIET_BIT */
       MSUM_MASKN = 5,                  /* the band mask's 7-bit bytes */
       MSUM_LEN = 6 + 1 + MSUM_MASKN + 3 * MSUM_BANDS + 2 + 1 };   /* the frame's bytes, F0 .. F7 */
#ifndef IN_POOL
#define IN_POOL
#endif
static struct { uint8_t x, y, w, h; } msum_r[2][MSUM_RECTS] IN_POOL;   /* the writes' rectangles: this frame's, the last check's */
static struct {
    uint8_t cap, cur;                   /* a frame being checked; msum_r[cur] its rectangles (cur ^ 1 the last check's) */
    uint8_t over, same;                 /* more writes than MSUM_RECTS; every rectangle so far the last check's */
    uint16_t n, pn;                     /* the writes this frame; the last check's */
    uint32_t bad;                       /* bands whose visible spans did not fit MSUM_SPANS */
    uint32_t h[MSUM_BANDS];             /* each band's hash so far */
    uint16_t c[MSUM_BANDS];             /* ... and its pixels counted */
} msum;
static uint32_t msum_mix(uint32_t p, uint32_t c)   /* a pixel's share of its band's hash: murmur3's finaliser of p << 16 | c */
{
    uint32_t h = p << 16 | (c & 0xFFFFu);
    h ^= h >> 16;
    h *= 0x85EBCA6Bu;
    h ^= h >> 13;
    h *= 0xC2B2AE35u;
    h ^= h >> 16;
    return h;
}
static void msum_begin(void)
{
    uint32_t b;
    msum.cap = 1, msum.cur ^= 1u, msum.n = 0, msum.over = 0, msum.same = 1, msum.bad = 0;
    for (b = 0; b < MSUM_BANDS; b++)
        msum.h[b] = 0, msum.c[b] = 0;
}
/* row y of write k at x, w: its spans no later write of the last check covers (lo[i], hi[i]); the count, or -1: too many */
static int msum_spans(uint32_t k, uint32_t x, uint32_t w, uint32_t y, uint16_t *lo, uint16_t *hi)
{
    uint32_t j, i, n = 1, p = msum.cur ^ 1u;
    lo[0] = (uint16_t)x, hi[0] = (uint16_t)(x + w);
    for (j = k + 1u; j < msum.pn && n; j++) {
        uint32_t a = msum_r[p][j].x, e = a + msum_r[p][j].w;
        if (y < msum_r[p][j].y || y >= (uint32_t)msum_r[p][j].y + msum_r[p][j].h)
            continue;
        for (i = 0; i < n; i++) {
            if (e <= lo[i] || a >= hi[i])
                continue;
            if (a > lo[i] && e < hi[i]) {   /* (inside: split in two) */
                if (n == MSUM_SPANS)
                    return -1;
                lo[n] = (uint16_t)e, hi[n] = hi[i], n++;
                hi[i] = (uint16_t)a;
            } else if (a > lo[i]) {
                hi[i] = (uint16_t)a;
            } else if (e < hi[i]) {
                lo[i] = (uint16_t)e;
            } else {                    /* (covered whole: gone) */
                lo[i] = lo[n - 1u], hi[i] = hi[n - 1u], n--, i--;
            }
        }
    }
    return (int)n;
}
/* one write that goes to the client: c a FILL's colour (px 0), else px its pixels as the panel takes them (byte-swapped) */
static void msum_see(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px, uint32_t c)
{
    uint32_t k = msum.n, r;
    if (!msum.cap || !w || !h)
        return;
    if (k >= MSUM_RECTS) {
        msum.over = 1;
        return;
    }
    msum.n++;
    msum_r[msum.cur][k].x = (uint8_t)x, msum_r[msum.cur][k].y = (uint8_t)y;
    msum_r[msum.cur][k].w = (uint8_t)(w > 240u ? 240u : w), msum_r[msum.cur][k].h = (uint8_t)(h > 240u ? 240u : h);
    if (!msum.same || k >= msum.pn || msum_r[msum.cur ^ 1u][k].x != x || msum_r[msum.cur ^ 1u][k].y != y ||
        msum_r[msum.cur ^ 1u][k].w != msum_r[msum.cur][k].w || msum_r[msum.cur ^ 1u][k].h != msum_r[msum.cur][k].h) {
        msum.same = 0;                  /* (another frame than the last check's: nothing proved, the next one checked) */
        return;
    }
    for (r = 0; r < h && y + r < 240u; r++) {
        uint16_t lo[MSUM_SPANS], hi[MSUM_SPANS];
        uint32_t yy = y + r, b = yy / MSUM_ROWS, s, i, hs = 0;
        int n = msum_spans(k, x, w, yy, lo, hi);
        if (n < 0) {
            msum.bad |= 1u << b;
            continue;
        }
        for (s = 0; s < (uint32_t)n; s++)
            for (i = lo[s]; i < hi[s] && i < 240u; i++) {
                uint32_t v = px ? px[r * w + (i - x)] : c;
                hs += msum_mix(yy * 240u + i, px ? (v >> 8 | v << 8) & 0xFFFFu : v);
                msum.c[b]++;
            }
        msum.h[b] += hs;
    }
}
/* the frame drawn: 1 and the valid bands in *valid when it proves something (the last check's rectangles, none left out) */
static int msum_end(uint32_t *valid)
{
    uint32_t b, v = 0, ok = msum.cap && !msum.over && msum.same && msum.n == msum.pn;
    msum.cap = 0;
    msum.pn = msum.over ? 0u : msum.n;  /* (the next check's: these rectangles) */
    for (b = 0; b < MSUM_BANDS; b++)
        v |= (uint32_t)(msum.c[b] == MSUM_PX && !(msum.bad >> b & 1u)) << b;
    *valid = v;
    return (int)ok;
}
/* the SUMS frame into f (MSUM_LEN bytes): ref the next checked frame's seq */
static void msum_frame(uint8_t *f, uint32_t ref, uint32_t valid)
{
    uint32_t i = 0, b, k, a = 0, s = 0;
    f[i++] = 0xF0, f[i++] = 0x7D, f[i++] = 0x46, f[i++] = 0x4C, f[i++] = 106, f[i++] = MSUM_OP;
    f[i++] = (uint8_t)(ref & 127u);
    for (k = 0; k < MSUM_MASKN; k++)
        f[i++] = (uint8_t)(valid >> (7u * k) & 127u);
    for (b = 0; b < MSUM_BANDS; b++) {
        uint32_t h = valid >> b & 1u ? msum.h[b] & 0x1FFFFFu : 0u;
        f[i++] = (uint8_t)(h & 127u), f[i++] = (uint8_t)(h >> 7 & 127u), f[i++] = (uint8_t)(h >> 14 & 127u);
    }
    for (k = 1; k < i; k++)
        a += f[k], s += a;
    f[i++] = (uint8_t)(a & 127u), f[i++] = (uint8_t)(s & 127u);
    f[i++] = 0xF7;
}
/* the client's side (the tests' model of MirrorLink.swift and the web Screen tab): fb's band hashes against a SUMS frame f (n
 * bytes); the bands that differ (bit b), or -1: not a good SUMS frame. *ref: its ref */
static int32_t msum_client(const uint16_t *fb, const uint8_t *f, uint32_t n, uint32_t *ref)
{
    uint32_t i, a = 0, s = 0, valid = 0, b, diff = 0;
    if (n != MSUM_LEN || f[5] != MSUM_OP || f[n - 1u] != 0xF7u)
        return -1;
    for (i = 1; i < n - 3u; i++)
        a += f[i], s += a;
    if ((a & 127u) != f[n - 3u] || (s & 127u) != f[n - 2u])
        return -1;
    *ref = f[6];
    for (i = 0; i < MSUM_MASKN; i++)
        valid |= (uint32_t)f[7u + i] << (7u * i);
    for (b = 0; b < MSUM_BANDS; b++) {
        uint32_t h = 0, p, want;
        const uint8_t *q = f + 7u + MSUM_MASKN + 3u * b;
        if (!(valid >> b & 1u))
            continue;
        for (p = b * MSUM_PX; p < (b + 1u) * MSUM_PX; p++)
            h += msum_mix(p, fb[p]);
        want = q[0] | (uint32_t)q[1] << 7 | (uint32_t)q[2] << 14;
        diff |= (uint32_t)((h & 0x1FFFFFu) != want) << b;
    }
    return (int32_t)diff;
}
#endif
