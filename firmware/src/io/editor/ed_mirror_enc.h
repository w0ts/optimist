/* SPDX-License-Identifier: GPL-3.0-only */
/* Editor protocol: the screen mirrored, its encoder (ed_mirror.c; web/EDITOR_PROTOCOL.md "The screen mirrored"; builder item
 * ED_MIRROR). Every write to the panel (display/lcd.c lcd_fill, lcd_blit) goes out as it is drawn, so a client that applies them in
 * order holds the panel's pixels: no frame buffer on the device, nothing drawn twice. Included by ed_mirror.c and tests/mirror_test.c,
 * which supply the wire: mir_wire(b) one byte of the frame (F0 .. F7; 0: the link is gone, stop) and mir_wire_end() after F7.
 *   106 MIRROR push   op 0 FILL  x, y, w, h (2 x 7 bit each), colour (RGB565, 3 x 7 bit)
 *                     op 1 BLIT  x, y, w, h, then runs: the rectangle's pixels row by row
 *                     op 2 MORE  runs: the same rectangle goes on (a BLIT is cut every MIR_CUT bytes)
 *   a token, t = n << 3 | s; s 0..6 the colour in that slot, s 7 a colour after it (3 x 7 bit), which then takes slot k (1, 2 .. 6,
 *   round). The slots are reset at each BLIT: slot 0 is black (always), the others black until written. A length L is 1 byte
 *   below 64, else 2: (L & 63) | 64, then L >> 6. (Version 2; version 1 had runs of 1 .. 15 and 16 + 2 x 7 bit, nothing else.)
 *     n 0..12  a run of n + 1 pixels of s
 *     n 13     a run of 14 + L pixels of s (L before s's colour)
 *     n 14     a bitmap: s the colour of the 1s; then a byte g << 3 | b: b the slot of the 0s, then g + 1 bytes of 7 pixels each, the
 *              first pixel in bit 6 (two colours: text on its ground, 7 pixels a byte)
 *     n 15     the pixels above (one row up in the rectangle, from its second row): s 0..6: s + 1 pixels, s 7: 8 + L pixels
 *   The encoder takes a bitmap or the pixels above only where they are the shorter. */
#ifndef ED_MIRROR_ENC_H
#define ED_MIRROR_ENC_H
enum { ED_MIRROR = 106, ED_PANEL = 108, MIR_VERSION = 2, MIR_CUT = 250, MIR_SLOTS = 7, MIR_LIT = 7, MIR_LONG = 13, MIR_BITS = 14, MIR_UP = 15,
       MIR_SHORT = 13,                  /* the pixels a one-byte run holds at most */
       MIR_LMAX = 8191,                 /* a length's largest */
       MIR_BMAX = 16 };                 /* a bitmap's 7-pixel bytes at most */
static struct {
    uint16_t slot[MIR_SLOTS];
    uint8_t rr, ok;                     /* the next slot a colour takes; 0: the link failed (the rest of this write dropped) */
    uint16_t n;                         /* bytes in the frame so far */
} me;
static int mir_wire(uint32_t b);
static void mir_wire_end(void);
static void me_b(uint32_t b)
{
    if (me.ok && !mir_wire(b))
        me.ok = 0;
    me.n++;
}
static void me_b2(uint32_t v)
{
    me_b(v & 127u);
    me_b(v >> 7 & 127u);
}
static void me_open(uint32_t op)
{
    me.n = 0;
    me_b(0xF0), me_b(0x7D), me_b(0x46), me_b(0x4C), me_b(ED_MIRROR), me_b(op);
}
static void me_close(void)
{
    me_b(0xF7);
    if (me.ok)
        mir_wire_end();
}
static void me_rect(uint32_t op, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    me.ok = 1;
    me_open(op);
    me_b2(x), me_b2(y), me_b2(w), me_b2(h);
}
static void me_col(uint32_t c)
{
    me_b2(c);
    me_b(c >> 14);
}
static void mir_enc_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t c)
{
    me_rect(0, x, y, w, h);
    me_col(c);
    me_close();
}
static uint32_t me_slot(uint32_t c)     /* c's slot, MIR_SLOTS: none */
{
    uint32_t s;
    for (s = 0; s < MIR_SLOTS && me.slot[s] != c; s++)
        ;
    return s;
}
static void me_cut(void)
{
    if (me.n > MIR_CUT) {               /* (a token is 21 bytes at most: the frame stays under MIR_CUT + 24) */
        me_close();
        me_open(2);
    }
}
/* token n with colour c's slot (MIR_SLOTS: none, s 7: c follows the token's own bytes, me_lit) */
static uint32_t me_tok(uint32_t n, uint32_t c)
{
    uint32_t s = me_slot(c);
    me_b(n << 3 | (s < MIR_SLOTS ? s : MIR_LIT));
    return s;
}
static void me_lit(uint32_t s, uint32_t c)
{
    if (s < MIR_SLOTS)
        return;
    me_col(c);
    me.slot[me.rr] = (uint16_t)c;
    me.rr = (uint8_t)(me.rr < MIR_SLOTS - 1u ? me.rr + 1u : 1u);
}
static void me_len(uint32_t l)          /* a length: 1 byte below 64, else 2 */
{
    if (l < 64u) {
        me_b(l);
    } else {
        me_b((l & 63u) | 64u);
        me_b(l >> 6);
    }
}
static uint32_t me_run_cost(uint32_t k) { return k <= MIR_SHORT ? 1u : k - 14u < 64u ? 2u : 3u; }
/* n pixels of colour c (n >= 1) */
static void me_run(uint32_t c, uint32_t n)
{
    uint32_t k, s;
    while (n && me.ok) {
        me_cut();
        k = n < 14u + MIR_LMAX ? n : 14u + MIR_LMAX;
        s = me_tok(k <= MIR_SHORT ? k - 1u : MIR_LONG, c);
        if (k > MIR_SHORT)
            me_len(k - 14u);
        me_lit(s, c);
        n -= k;
    }
}
/* the pixels above from i (n in all, w a row): how many, and whether they are shorter than the runs they replace */
static uint32_t me_up_at(const uint16_t *px, uint32_t i, uint32_t n, uint32_t w)
{
    uint32_t j, u, runs = 0, cost;
    if (i < w)
        return 0;
    for (u = 0; i + u < n && u < 8u + MIR_LMAX && px[i + u] == px[i + u - w]; u++)
        ;
    if (u < 2u)
        return 0;
    for (j = i; j < i + u;) {                       /* (the runs it replaces, cut at u: a byte or more each) */
        uint32_t r = j + 1u;
        while (r < i + u && px[r] == px[j])
            r++;
        runs += me_run_cost(r - j);
        j = r;
    }
    cost = u <= 7u ? 1u : u - 8u < 64u ? 2u : 3u;
    return cost < runs ? u : 0u;
}
static void me_up(uint32_t u)
{
    me_cut();
    if (u <= 7u) {
        me_b(MIR_UP << 3 | (u - 1u));
    } else {
        me_b(MIR_UP << 3 | 7u);
        me_len(u - 8u);
    }
}
static uint32_t me_px(const uint16_t *px, uint32_t i) { return (px[i] >> 8 | px[i] << 8) & 0xFFFFu; }
/* at pixel i of n: g 7-pixel bytes of two colours, f the 1s, b the 0s (b in a slot), cheaper than their runs; 0: none. A first
 * run longer than a short token's is left to the runs at once (black and fills: no scan) */
static uint32_t me_bits_at(const uint16_t *px, uint32_t i, uint32_t n, uint32_t *f, uint32_t *b)
{
    uint32_t c0 = me_px(px, i), c1 = c0, j, g, runs = 1, lit;
    for (j = i + 1u; j < n && j <= i + MIR_SHORT && px[j] == px[i]; j++)
        ;
    if (j > i + MIR_SHORT)
        return 0;
    for (j = i + 1u; j < n && j < i + 7u * MIR_BMAX; j++) {
        uint32_t c = me_px(px, j);
        if (c != c0 && c1 == c0)
            c1 = c;
        else if (c != c0 && c != c1)
            break;
    }
    g = (j - i) / 7u;
    if (g < 2u || c1 == c0)
        return 0;
    for (j = i + 1u; j < i + 7u * g; j++)              /* (the runs inside the g bytes: a byte each at least) */
        runs += px[j] != px[j - 1u];
    *b = me_slot(c0) < MIR_SLOTS ? c0 : c1;
    *f = *b == c0 ? c1 : c0;
    lit = me_slot(*f) < MIR_SLOTS ? 0u : 3u;
    if (me_slot(*b) >= MIR_SLOTS || (lit && me_slot(*b) == me.rr))
        return 0;                       /* (the 0s need a slot, one the 1s' new colour does not take) */
    return 2u + g + lit < runs + lit ? g : 0u;
}
static void me_bits(const uint16_t *px, uint32_t i, uint32_t g, uint32_t f, uint32_t b)
{
    uint32_t s, k, j, v;
    me_cut();
    s = me_tok(MIR_BITS, f);
    me_lit(s, f);
    me_b((g - 1u) << 3 | me_slot(b));
    for (k = 0; k < g; k++) {
        for (v = 0, j = 0; j < 7u; j++)
            v = v << 1 | (me_px(px, i + 7u * k + j) == f);
        me_b(v);
    }
}
/* px as the panel takes it (each pixel byte-swapped: display/lcd.c) */
static void mir_enc_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    uint32_t i = 0, n = w * h, r, c;
    me_rect(1, x, y, w, h);
    for (r = 0; r < MIR_SLOTS; r++)
        me.slot[r] = 0;
    me.rr = 1;
    while (i < n && me.ok) {
        uint32_t f, b, g, u = me_up_at(px, i, n, w);
        if (u) {
            me_up(u);
            i += u;
            continue;
        }
        g = me_bits_at(px, i, n, &f, &b);
        if (g) {
            me_bits(px, i, g, f, b);
            i += 7u * g;
            continue;
        }
        for (r = i + 1u; r < n && px[r] == px[i]; r++)
            ;
        c = px[i];
        me_run((c >> 8 | c << 8) & 0xFFFFu, r - i);
        i = r;
    }
    me_close();
}
#endif
