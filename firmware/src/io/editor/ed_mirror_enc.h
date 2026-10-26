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
enum { ED_MIRROR = 106, ED_PANEL = 108, ED_NAV = 109, MIR_VERSION = 2, MIR_CUT = 250, MIR_SLOTS = 7, MIR_LIT = 7, MIR_LONG = 13, MIR_BITS = 14, MIR_UP = 15,
       MIR_SHORT = 13,                  /* the pixels a one-byte run holds at most */
       MIR_LMAX = 8191,                 /* a length's largest */
       MIR_BMAX = 16 };                 /* a bitmap's 7-pixel bytes at most */
static struct {
    uint16_t slot[MIR_SLOTS];
    uint8_t rr, ok;                     /* the next slot a colour takes; 0: the link failed (the rest of this write dropped) */
    uint16_t n;                         /* bytes in the frame so far */
    uint16_t w, sw;                     /* the rectangle's width; its rows' stride (pixels): wider when it is a part of a blit */
} me;
/* the rectangle's pixel i (row by row, me.w a row), in a buffer of rows me.sw apart */
static uint32_t me_at(const uint16_t *px, uint32_t i) { return me.sw == me.w ? px[i] : px[i / me.w * me.sw + i % me.w]; }
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
    for (u = 0; i + u < n && u < 8u + MIR_LMAX && me_at(px, i + u) == me_at(px, i + u - w); u++)
        ;
    if (u < 2u)
        return 0;
    for (j = i; j < i + u;) {                       /* (the runs it replaces, cut at u: a byte or more each) */
        uint32_t r = j + 1u;
        while (r < i + u && me_at(px, r) == me_at(px, j))
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
static uint32_t me_px(const uint16_t *px, uint32_t i) { uint32_t v = me_at(px, i); return (v >> 8 | v << 8) & 0xFFFFu; }
/* at pixel i of n: g 7-pixel bytes of two colours, f the 1s, b the 0s (b in a slot), cheaper than their runs; 0: none. A first
 * run longer than a short token's is left to the runs at once (black and fills: no scan) */
static uint32_t me_bits_at(const uint16_t *px, uint32_t i, uint32_t n, uint32_t *f, uint32_t *b)
{
    uint32_t c0 = me_px(px, i), c1 = c0, j, g, runs = 1, lit;
    for (j = i + 1u; j < n && j <= i + MIR_SHORT && me_at(px, j) == me_at(px, i); j++)
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
        runs += me_at(px, j) != me_at(px, j - 1u);
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
/* a w x h rectangle at x, y whose rows are sw pixels apart in px (px as the panel takes it: each pixel byte-swapped,
 * display/lcd.c) */
static void mir_enc_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px, uint32_t sw)
{
    uint32_t i = 0, n = w * h, r, c;
    me_rect(1, x, y, w, h);
    me.w = (uint16_t)w, me.sw = (uint16_t)sw;
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
        for (r = i + 1u; r < n && me_at(px, r) == me_at(px, i); r++)
            ;
        c = me_at(px, i);
        me_run((c >> 8 | c << 8) & 0xFFFFu, r - i);
        i = r;
    }
    me_close();
}
static void mir_enc_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px) { mir_enc_rect(x, y, w, h, px, w); }

/* What the client already has is not sent again (after fm1-base's device/mirror.c): each row of the panel in MIR_NSEG segments of
 * MIR_SEGW pixels, the hash of each segment the client holds (me_seg, valid where me_known has its bit). A blit sends, row by row,
 * only the span of its segments that changed (a segment it covers in part always counts as changed, and is not known after), rows
 * with the same span or one inside it as one rectangle; a fill's whole segments become known. The wire is the same: a client needs
 * nothing new. ed_mirror.c forgets it all when the client may have lost a frame (behind, a first on). Off by default (the user,
 * 2026-10-26): on the UI's whole host run it saves 1.1 % of the bytes for 7.7 KB of pool; the host tests build it on */
#ifndef MIR_SKIP
#define MIR_SKIP 0
#endif
#if MIR_SKIP
#ifndef IN_POOL
#define IN_POOL                         /* (a host test: no pool section) */
#endif
enum { MIR_SEGW = 30, MIR_NSEG = 8 };
static uint32_t me_seg[240][MIR_NSEG] IN_POOL;
static uint8_t me_known[240];                       /* bit s: the client holds segment s of the row as me_seg says */
static uint32_t me_skipped;                          /* the segments not sent again (the host tests' count) */
static uint32_t me_hash(const uint16_t *p, uint32_t n)   /* FNV-1a over the pixels */
{
    uint32_t h = 2166136261u;
    while (n--)
        h = (h ^ *p++) * 16777619u;
    return h;
}
static void mir_enc_forget_all(void) { memset(me_known, 0, sizeof me_known); }
/* row ry, pixels x .. x + w of p: the changed span [*a, *b) in pixels from x; 0: nothing changed. new: the whole segments' hashes */
static int me_span(const uint16_t *p, uint32_t x, uint32_t w, uint32_t ry, uint32_t *a, uint32_t *b, uint32_t *nh)
{
    uint32_t s, s0 = x / MIR_SEGW, s1 = (x + w - 1u) / MIR_SEGW, lo = w, hi = 0;
    for (s = s0; s <= s1; s++) {
        uint32_t sx = s * MIR_SEGW, whole = sx >= x && sx + MIR_SEGW <= x + w, from = sx > x ? sx - x : 0u,
                 to = sx + MIR_SEGW < x + w ? sx + MIR_SEGW - x : w;
        if (whole) {
            nh[s] = me_hash(p + from, MIR_SEGW);
            if ((me_known[ry] >> s & 1u) && me_seg[ry][s] == nh[s]) {
                me_skipped++;
                continue;
            }
        }
        lo = from < lo ? from : lo;
        hi = to > hi ? to : hi;
    }
    *a = lo, *b = hi;
    return lo < hi;
}
/* after rows y .. y + h of a blit at x, w went out whole (sent 1) or did not (0): what the client now holds */
static void me_learn(const uint16_t *px, uint32_t x, uint32_t y, uint32_t w, uint32_t h, int sent)
{
    uint32_t j, s, s0 = x / MIR_SEGW, s1 = (x + w - 1u) / MIR_SEGW;
    for (j = 0; j < h; j++)
        for (s = s0; s <= s1; s++) {
            uint32_t sx = s * MIR_SEGW;
            if (sent && sx >= x && sx + MIR_SEGW <= x + w) {
                me_seg[y + j][s] = me_hash(px + j * w + (sx - x), MIR_SEGW);
                me_known[y + j] |= (uint8_t)(1u << s);
            } else
                me_known[y + j] &= (uint8_t)~(1u << s);
        }
}
static void mir_enc_blit_skip(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    uint32_t r = 0, nh[MIR_NSEG];
    while (r < h) {
        uint32_t a, b, a2, b2, k;
        if (!me_span(px + r * w, x, w, y + r, &a, &b, nh)) {
            r++;
            continue;
        }
        for (k = r + 1u; k < h && me_span(px + k * w, x, w, y + k, &a2, &b2, nh) && a2 >= a && b2 <= b; k++)
            ;                           /* (rows whose change lies inside this one's span: the same rectangle) */
        me.ok = 1;
        mir_enc_rect(x + a, y + r, b - a, k - r, px + r * w + a, w);
        me_learn(px + r * w, x, y + r, w, k - r, me.ok);
        r = k;
    }
}
static void mir_enc_fill_skip(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t c)
{
    uint32_t j, s, s0 = x / MIR_SEGW, s1 = (x + w - 1u) / MIR_SEGW, hs = 2166136261u, pc = (c >> 8 | c << 8) & 0xFFFFu;
    for (j = 0; j < MIR_SEGW; j++)
        hs = (hs ^ pc) * 16777619u;
    me.ok = 1;
    mir_enc_fill(x, y, w, h, c);
    for (j = y; j < y + h; j++)
        for (s = s0; s <= s1; s++) {
            uint32_t sx = s * MIR_SEGW;
            if (me.ok && sx >= x && sx + MIR_SEGW <= x + w)
                me_seg[j][s] = hs, me_known[j] |= (uint8_t)(1u << s);
            else
                me_known[j] &= (uint8_t)~(1u << s);
        }
}
#else
#define mir_enc_blit_skip mir_enc_blit
#define mir_enc_fill_skip mir_enc_fill
static void mir_enc_forget_all(void) {}
#endif
#endif
