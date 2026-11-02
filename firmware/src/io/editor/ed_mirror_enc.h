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
 *              With b 7 (never a bitmap's: 2026-11-01, MIR_AA, only to a client that asked, opti_miraa_1) it is the AA token
 *              instead: the byte is k << 3 | 7, k the ground's slot (bit 6 0), then L (1..127), then L levels a (0..15) as a prefix
 *              code, bit-packed 7 bits a byte, the first bit in bit 6, the last byte's spare bits 0: a 15 '0', a 0 '10', a 1..14
 *              '11' and a's 4 bits. Pixel = blend(s's colour, k's colour, a) per channel (5, 6, 5 bits) as fm1-base's text
 *              (device/gfx.c cv_text): (fg * a + bg * (15 - a)) / 15, rounded down (a 15 fg, a 0 bg). Anti-aliased text, 3.2 bits a
 *              pixel where each shade was a new colour (3 bytes) before.
 *   The encoder takes a bitmap, an AA span or the pixels above only where they are the shorter.
 *   Checked frames (the client asks with 106 bit 3; 2026-11-01): ops 8, 9, 10 instead of 0, 1, 2, the same body, then seq (7 bit, +1 each
 *   checked frame sent) and a Fletcher sum of the bytes 7D .. seq before F7: A = sum of the bytes, B = sum of A after each, both & 127. Over Bluetooth a Mac's MIDI driver zeroes or
 *   loses bytes of long SysEx (measured 2026-10-31 / 11-01): the client drops a frame whose sum fails, and a MORE after a gap in seq
 *   (its BLIT lost: it would paint into the rectangle before), and asks for the whole panel again. A client that never asks gets
 *   ops 0..2 as before; an older firmware ignores bit 3, so the client reads both. */
#ifndef ED_MIRROR_ENC_H
#define ED_MIRROR_ENC_H
enum { ED_MIRROR = 106, ED_PANEL = 108, ED_NAV = 109, MIR_VERSION = 2, MIR_CHK_OP = 8, MIR_CHK_BIT = 8, MIR_CUT = 250, MIR_SLOTS = 7, MIR_LIT = 7, MIR_LONG = 13, MIR_BITS = 14, MIR_UP = 15,
       MIR_SHORT = 13,                  /* the pixels a one-byte run holds at most */
       MIR_LMAX = 8191,                 /* a length's largest */
       MIR_BMAX = 16 };                 /* a bitmap's 7-pixel bytes at most */
#ifndef MIR_AA
#define MIR_AA 1                        /* the AA token (opti_miraa_1): 0 builds the encoder without it */
#endif
static struct {
    uint16_t slot[MIR_SLOTS];
    uint8_t rr, ok;                     /* the next slot a colour takes; 0: the link failed (the rest of this write dropped) */
    uint16_t n;                         /* bytes in the frame so far */
    uint16_t w, sw;                     /* the rectangle's width; its rows' stride (pixels): wider when it is a part of a blit */
    uint8_t chk, chk_f, seq, sa, sb;    /* checked frames asked for; this frame is one; the next seq; the frame's sums so far */
    uint8_t aa;                         /* the client takes the AA token (106's second byte bit 0, opti_miraa_1; MIR_AA) */
    uint8_t rx, ry, rw, rh;             /* the rectangle the frame being sent draws in (a MORE: its BLIT's; w 0: none, a mark) */
    uint32_t pi, p0;                    /* BLIT: the next pixel of the rectangle to code; the frame's first (me_rr keeps its rows) */
    uint8_t blit;                       /* the rectangle is a BLIT's (its frames cover rows p0 / w .. (pi - 1) / w), 0 a FILL's */
    uint32_t done;                      /* BLIT: its pixels before this went out in whole frames (a write cut short: those rows held) */
} me;
/* the rectangles of the last MIR_RR checked frames by seq (entry seq & (MIR_RR - 1), seq | 0x80 when valid): a client that lost
 * frames a .. b asks for them (106 bit 4, ed_mirror.c mir_fix) and only those rectangles go again. 128: every 7-bit seq (was 64:
 * a burst of losses older than 64 frames fell back to a whole panel) */
enum { MIR_RR = 128, MIR_FIX_BIT = 16,
       MIR_ACK_BIT = 32,                /* 106 bit 5 (opti_miraimd_1): the client's losses (7 bit, free-running), its next seq */
       MIR_QUIET_BIT = 64 };            /* 106 bit 6 with bit 5: an ACK alone, no reply (the client sends it ~5 times a second) */
static struct { uint8_t seq, x, y, w, h; uint16_t ms; } me_rr[MIR_RR];   /* (ms: fm1_ms when it went out, the delay an ACK shows) */
/* the rectangle's pixel i (row by row, me.w a row), in a buffer of rows me.sw apart */
static uint32_t me_at(const uint16_t *px, uint32_t i) { return me.sw == me.w ? px[i] : px[i / me.w * me.sw + i % me.w]; }
static int mir_wire(uint32_t b);
static void mir_wire_end(void);
static void me_b(uint32_t b)
{
    if (me.ok && !mir_wire(b))
        me.ok = 0;
    me.n++;
    if (b < 0x80u)
        me.sa = (uint8_t)(me.sa + b), me.sb = (uint8_t)(me.sb + me.sa);
}
static void me_b2(uint32_t v)
{
    me_b(v & 127u);
    me_b(v >> 7 & 127u);
}
static void me_open(uint32_t op)
{
    me.n = 0;
    me.sa = me.sb = 0;
    me.chk_f = 0;
    me_b(0xF0), me_b(0x7D), me_b(0x46), me_b(0x4C), me_b(ED_MIRROR), me_b(op);
}
static void me_open_px(uint32_t op)     /* a panel write's frame: FILL, BLIT, MORE (checked when asked) */
{
    me.p0 = op == 2u ? me.pi : 0u;
    me_open(me.chk ? op + MIR_CHK_OP : op);
    me.chk_f = me.chk;
}
static void me_close(void)
{
    if (me.chk_f) {
        uint32_t a, b;
        me_b(me.seq & 127u);
        a = me.sa & 127u, b = me.sb & 127u;
        me_b(a);
        me_b(b);
    }
    me_b(0xF7);
    if (me.ok) {
        mir_wire_end();
        if (me.blit)
            me.done = me.pi;
        if (me.chk_f) {
            uint32_t k = me.seq & (MIR_RR - 1u);
            me_rr[k].seq = (uint8_t)(me.seq | 0x80u);
            me_rr[k].x = me.rx, me_rr[k].y = me.ry, me_rr[k].w = me.rw, me_rr[k].h = me.rh;
            me_rr[k].ms = (uint16_t)fm1_ms;
            if (me.blit && me.w && me.pi > me.p0) {   /* (a BLIT's frame: only the rows its pixels are in) */
                uint32_t r0 = me.p0 / me.w, r1 = (me.pi - 1u) / me.w;
                me_rr[k].y = (uint8_t)(me.ry + r0), me_rr[k].h = (uint8_t)(r1 - r0 + 1u);
            }
            me.seq = (uint8_t)((me.seq + 1u) & 127u);
        }
    }
    me.chk_f = 0;
}
static void me_rect(uint32_t op, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    me.ok = 1;
    me.rx = (uint8_t)x, me.ry = (uint8_t)y, me.rw = (uint8_t)(w > 255u ? 255u : w), me.rh = (uint8_t)(h > 255u ? 255u : h);
    me_open_px(op);
    me_b2(x), me_b2(y), me_b2(w), me_b2(h);
}
static void me_col(uint32_t c)
{
    me_b2(c);
    me_b(c >> 14);
}
static void mir_enc_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t c)
{
    me.blit = 0;
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
        me_open_px(2);
    }
}
#if MIR_AA
static void me_cut_n(uint32_t k)        /* before a token of k bytes (an AA span's, up to 115): the frame still under MIR_CUT + 24 */
{
    if (me.n > MIR_CUT || me.n + k > MIR_CUT + 21u) {
        me_close();
        me_open_px(2);
    }
}
#endif
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
        me.pi += k;                     /* (a long run cut between frames: each frame's rows, me_rr) */
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
#if MIR_AA
/* The AA token (2026-11-01; measured on 10 pages captured from the FM-1: a catch-up 8.39 KB -> 4.9 KB). fm1-base draws text with
 * 4-bit alpha over its ground, so each shade was a new colour (3 bytes, and the 6 slots overflowed). A span: the ground b in a
 * slot, the text's colour f (a slot or a new one), each pixel blend(f, b, a) exactly; it stops where the row above would take over
 * (MIR_AA_UP pixels the same as a row up) or at MIR_AA_BG pixels of bare ground (a run), and is taken only where it is shorter
 * than the runs it replaces and, per pixel, than a bitmap or the run here. Stateless across BLITs as the slots are */
enum { MIR_AA_MAX = 127, MIR_AA_UP = 16, MIR_AA_BG = 6, MIR_AA_LOOK = 16, MIR_AA_NC = 8 };
static uint32_t me_blend(uint32_t f, uint32_t b, uint32_t a)
{
    return ((f >> 11) * a + (b >> 11) * (15u - a)) / 15u << 11 | (((f >> 5) & 63u) * a + ((b >> 5) & 63u) * (15u - a)) / 15u << 5 |
           ((f & 31u) * a + (b & 31u) * (15u - a)) / 15u;
}
static void me_ramp(uint32_t f, uint32_t b, uint16_t *r)   /* the 16 shades of f over b (no cache: the pairs are tried rarely,
                                                             * after me_cover; 2026-11-01, a cache of 8 was no faster) */
{
    uint32_t a;
    for (a = 0; a < 16u; a++)
        r[a] = (uint16_t)me_blend(f, b, a);
}
/* colour c's level in ramp r: 15 or 0 first (the shortest codes), else the lowest a; 16: none */
static uint32_t me_lv(const uint16_t *r, uint32_t c)
{
    uint32_t a;
    if (r[15] == c)
        return 15;
    for (a = 0; a < 15u && r[a] != c; a++)
        ;
    return a < 15u ? a : 16u;
}
/* lo, hi: each channel's least and most over a few pixels; 1: every channel of them between f's and b's (a blend's are) */
static int me_cover(uint32_t lo, uint32_t hi, uint32_t f, uint32_t b)
{
    uint32_t m;
    for (m = 0x1Fu; m; m = m == 0x1Fu ? 0x7E0u : m == 0x7E0u ? 0xF800u : 0u)
        if ((lo & m) < ((f & m) < (b & m) ? f & m : b & m) || (hi & m) > ((f & m) > (b & m) ? f & m : b & m))
            return 0;
    return 1;
}
static int me_end(uint32_t lo, uint32_t hi, uint32_t b)   /* 1: b can be a blend's end over them (each channel not inside) */
{
    uint32_t m;
    for (m = 0x1Fu; m; m = m == 0x1Fu ? 0x7E0u : m == 0x7E0u ? 0xF800u : 0u)
        if ((b & m) > (lo & m) && (b & m) < (hi & m))
            return 0;
    return 1;
}
static uint32_t me_lv_bits(uint32_t a) { return a == 15u ? 1u : a == 0u ? 2u : 6u; }
/* at pixel i of n (w a row): an AA span's length (0: none), its colour *f and ground *b, its bytes *cost (the token's all) */
static uint32_t me_aa_at(const uint16_t *px, uint32_t i, uint32_t n, uint32_t w, uint32_t *f, uint32_t *b, uint32_t *cost)
{
    uint32_t lim = n < i + MIR_AA_MAX ? n : i + MIR_AA_MAX, c0 = me_px(px, i), cand[MIR_AA_NC], nc = 0, j, k, s, bl = 0, bc = 0, alt,
             lits = 0, lo, hi, ue = i + 1u > w ? i + 1u : w, uk = 0, ulim = 0xFFFFFFFFu;   /* (ue, uk, ulim: the row above's scan) */
    if (me_slot(c0) < MIR_SLOTS && i + 2u < n && me_at(px, i + 1u) == me_at(px, i) && me_at(px, i + 2u) == me_at(px, i))
        return 0;                       /* (a run starts here: the run is shorter) */
    for (j = i; j < lim && j < i + MIR_AA_LOOK && nc < MIR_AA_NC; j++) {   /* (the text's colour: one of the first pixels') */
        uint32_t c = me_px(px, j);
        for (k = 0; k < nc && cand[k] != c; k++)
            ;
        if (k == nc)
            cand[nc++] = c;
    }
    for (lo = hi = c0, j = i + 1u; j < lim && j < i + 3u; j++) {   /* (the first 3 pixels' range, each channel) */
        uint32_t c = me_px(px, j), m;
        for (m = 0x1Fu; m; m = m == 0x1Fu ? 0x7E0u : m == 0x7E0u ? 0xF800u : 0u) {
            if ((c & m) < (lo & m))
                lo = (lo & ~m) | (c & m);
            if ((c & m) > (hi & m))
                hi = (hi & ~m) | (c & m);
        }
    }
    for (s = 0; s < nc; s++)
        lits |= (me_slot(cand[s]) < MIR_SLOTS ? 0u : 1u) << s;
    for (s = 0; s < MIR_SLOTS; s++) {
        uint32_t bg = me.slot[s];
        if (me_slot(bg) != s || !me_end(lo, hi, bg))
            continue;                   /* (a colour in two slots: its first; a ground inside the first pixels' range: none) */
        for (k = 0; k < nc; k++) {
            uint32_t fg = cand[k], lit = (lits >> k & 1u) * 3u, L, bits = 0, c, a;
            uint16_t r[16];
            if (fg == bg || (lit && s == me.rr))
                continue;               /* (a new colour takes slot rr before the ground's slot is read) */
            if (!me_cover(lo, hi, fg, bg))
                continue;               /* (the first pixels not shades of the pair: no ramp needed to tell) */
            me_ramp(fg, bg, r);
            for (L = 0; i + L < lim; L++) {
                uint32_t q = i + L, z;
                c = me_px(px, q);
                if ((a = me_lv(r, c)) > 15u)
                    break;
                if (L) {
                    while (ue < ulim && ue <= q + MIR_AA_UP - 1u && ue < n) {   /* (where the row above takes over: found once) */
                        uk = me_at(px, ue) == me_at(px, ue - w) ? uk + 1u : 0u;
                        if (uk == MIR_AA_UP)
                            ulim = ue + 1u - MIR_AA_UP;
                        ue++;
                    }
                    if (q >= ulim)
                        break;
                    if (c == bg && q + MIR_AA_BG <= n) {   /* (bare ground: a run) */
                        for (z = 1; z < MIR_AA_BG && me_px(px, q + z) == bg; z++)
                            ;
                        if (z == MIR_AA_BG)
                            break;
                    }
                }
                bits += me_lv_bits(a);
            }
            if (L < 3u)
                continue;
            while (L > 3u && me_px(px, i + L - 1u) == bg && me_px(px, i + L - 2u) == bg)
                L--, bits -= 2u;        /* (no ground at its end: the run after takes it; the ground is level 0) */
            c = 3u + lit + (bits + 6u) / 7u;
            if (!bl || c * bl < bc * L)
                bl = L, bc = c, *f = fg, *b = bg;
        }
    }
    if (!bl)
        return 0;
    for (alt = 0, j = i; j < i + bl;) {   /* (the runs it replaces, a new colour's 3 bytes once) */
        uint32_t q = j + 1u, c = me_px(px, j);
        while (q < i + bl && me_at(px, q) == me_at(px, j))
            q++;
        alt += me_run_cost(q - j);
        if (me_slot(c) >= MIR_SLOTS) {
            for (k = i; k < j && me_px(px, k) != c; k++)
                ;
            alt += k < j ? 0u : 3u;
        }
        j = q;
    }
    *cost = bc;
    return bc < alt ? bl : 0u;
}
static void me_aa(const uint16_t *px, uint32_t i, uint32_t L, uint32_t f, uint32_t b, uint32_t cost)
{
    uint32_t s, j, acc = 0, nb = 0;
    uint16_t r[16];
    me_ramp(f, b, r);
    me_cut_n(cost);
    s = me_tok(MIR_BITS, f);
    me_lit(s, f);
    me_b(me_slot(b) << 3 | 7u);
    me_b(L);
    for (j = 0; j < L; j++) {
        uint32_t a = me_lv(r, me_px(px, i + j)), v = a == 15u ? 0u : a == 0u ? 2u : 48u | a, k = me_lv_bits(a);
        while (k--) {
            acc = acc << 1 | (v >> k & 1u);
            if (++nb == 7u)
                me_b(acc), acc = 0, nb = 0;
        }
    }
    if (nb)
        me_b(acc << (7u - nb));
}
#endif
/* a w x h rectangle at x, y whose rows are sw pixels apart in px (px as the panel takes it: each pixel byte-swapped,
 * display/lcd.c) */
static void mir_enc_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px, uint32_t sw)
{
    uint32_t i = 0, n = w * h, r, c;
    me.pi = 0;
    me.done = 0;
    me.blit = 1;
    me.w = (uint16_t)w, me.sw = (uint16_t)sw;
    me_rect(1, x, y, w, h);
    for (r = 0; r < MIR_SLOTS; r++)
        me.slot[r] = 0;
    me.rr = 1;
    while (i < n && me.ok) {
        uint32_t f, b, g, u;
        me.pi = i;
        u = me_up_at(px, i, n, w);
        if (u) {
            me_up(u);
            i += u;
            continue;
        }
        g = me_bits_at(px, i, n, &f, &b);
#if MIR_AA
        if (me.aa) {                    /* (the cheapest per pixel of a bitmap, an AA span and the run here) */
            uint32_t f2, b2, cost, L = me_aa_at(px, i, n, w, &f2, &b2, &cost), bp = 0, bn = 0, rc;
            if (g)
                bp = 7u * g, bn = 2u + g + (me_slot(f) < MIR_SLOTS ? 0u : 3u);
            if (L && (!bp || cost * bp < bn * L))
                bp = L, bn = cost, g = 0;
            for (r = i + 1u; r < n && me_at(px, r) == me_at(px, i); r++)
                ;
            c = me_px(px, i);
            rc = me_run_cost(r - i) + (me_slot(c) < MIR_SLOTS ? 0u : 3u);
            if (!bp || rc * bp <= bn * (r - i)) {
                me_run(c, r - i);
                i = r;
            } else if (g) {
                me_bits(px, i, g, f, b);
                i += 7u * g;
            } else {
                me_aa(px, i, L, f2, b2, cost);
                i += L;
            }
            continue;
        }
#endif
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
    me.pi = n;                          /* (the last frame's rows reach the end) */
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
#if defined(MIR_BLE) && MIR_BLE
#define MIR_SKIP 1                      /* with Bluetooth (2026-11-01): a client that lost frames gets only their rectangles again
                                         * (ed_mirror.c mir_fix: forgotten here, the panel repainted, the rest not sent) instead of
                                         * a whole 9 KB panel at 5 KB/s */
#else
#define MIR_SKIP 0
#endif
#endif
#if MIR_SKIP
#ifndef IN_POOL
#define IN_POOL                         /* (a host test: no pool section) */
#endif
enum { MIR_SEGW = 30, MIR_NSEG = 8 };
#define MIR_SEG_BYTES (240u * MIR_NSEG * 4u)
#if defined(MIR_LEND) && MIR_LEND
/* lent with the mirror's Bluetooth ring (ed_mirror.c mir_lend: after the ring in its block, MIR_LEND_BYTES), 0 while it is
 * not: nothing known, every segment sent (over USB the skip saves ~1 %; the whole-write cache me_rc stays) */
static uint32_t (*me_seg)[MIR_NSEG];
#define ME_SEG_OK() (me_seg != 0)
#else
static uint32_t me_seg[240][MIR_NSEG] IN_POOL;
#define ME_SEG_OK() 1
#endif
static uint8_t me_known[240];                       /* bit s: the client holds segment s of the row as me_seg says */
static uint32_t me_skipped;                          /* the segments not sent again (the host tests' count) */
static uint32_t me_hash(const uint16_t *p, uint32_t n)   /* FNV-1a over the pixels */
{
    uint32_t h = 2166136261u;
    while (n--)
        h = (h ^ *p++) * 16777619u;
    return h;
}
/* What the client holds, by whole write (2026-11-01): the UI draws the same rectangles again with the same pixels (a card, a
 * row, a band), so a write whose rectangle and pixels are those of one the client already holds whole goes nowhere. The segment
 * hashes alone resent every part segment (text and cards start between segments): a repair (mir_fix) sent 963 B on average, half
 * a panel, the user seeing "the whole screen again" after each loss. An entry is good until a write that overlaps it goes out
 * (or fails), or its rectangle is lost (mir_enc_forget); MIR_RC of them, the oldest replaced */
enum { MIR_RC = 64 };
static struct { uint8_t x, y, w, h, ok; uint32_t hash; } me_rc[MIR_RC];
static uint32_t me_rc_next, me_rc_hits;
static uint32_t me_rc_hash(const uint16_t *px, uint32_t n, uint32_t seed)
{
    uint32_t h = 2166136261u ^ seed;
    while (n--)
        h = (h ^ *px++) * 16777619u;
    return h;
}
static int me_rc_held(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t hash)
{
    uint32_t i;
    for (i = 0; i < MIR_RC; i++)
        if (me_rc[i].ok && me_rc[i].x == x && me_rc[i].y == y && me_rc[i].w == w && me_rc[i].h == h)
            return me_rc[i].hash == hash;
    return 0;
}
static void me_rc_drop(uint32_t x, uint32_t y, uint32_t w, uint32_t h)   /* every entry this rectangle overlaps */
{
    uint32_t i;
    for (i = 0; i < MIR_RC; i++)
        if (me_rc[i].ok && me_rc[i].x < x + w && x < (uint32_t)me_rc[i].x + me_rc[i].w && me_rc[i].y < y + h &&
            y < (uint32_t)me_rc[i].y + me_rc[i].h)
            me_rc[i].ok = 0;
}
static void me_rc_sent(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t hash, int whole)
{
    me_rc_drop(x, y, w, h);
    if (!whole || w > 255u || h > 255u)
        return;
    me_rc[me_rc_next].x = (uint8_t)x, me_rc[me_rc_next].y = (uint8_t)y, me_rc[me_rc_next].w = (uint8_t)w;
    me_rc[me_rc_next].h = (uint8_t)h, me_rc[me_rc_next].hash = hash, me_rc[me_rc_next].ok = 1;
    me_rc_next = (me_rc_next + 1u) % MIR_RC;
}
static void mir_enc_forget_all(void)
{
    memset(me_known, 0, sizeof me_known);
    memset(me_rc, 0, sizeof me_rc);
}
static void mir_enc_forget(uint32_t x, uint32_t y, uint32_t w, uint32_t h)   /* the client may not hold this rectangle */
{
    uint32_t j, s, s0 = x / MIR_SEGW, s1 = (x + w - 1u) / MIR_SEGW;
    if (!w || !h)
        return;
    for (j = y; j < y + h && j < 240u; j++)
        for (s = s0; s <= s1 && s < MIR_NSEG; s++)
            me_known[j] &= (uint8_t)~(1u << s);
    me_rc_drop(x, y, w, h);
}
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
            if (sent && ME_SEG_OK() && sx >= x && sx + MIR_SEGW <= x + w) {
                me_seg[y + j][s] = me_hash(px + j * w + (sx - x), MIR_SEGW);
                me_known[y + j] |= (uint8_t)(1u << s);
            } else
                me_known[y + j] &= (uint8_t)~(1u << s);
        }
}
static void mir_enc_blit_skip(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    uint32_t r = 0, nh[MIR_NSEG], hash = me_rc_hash(px, w * h, 1u), whole = 1;
    if (me_rc_held(x, y, w, h, hash)) {
        me_rc_hits++;
        return;                         /* (this very write the client holds: nothing) */
    }
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
        {   /* (cut short: the rows its whole frames carried are held, the rest not; 2026-11-01, a budget refusing the rest of a
             * tall rectangle each time would otherwise send its top again and again) */
            uint32_t held = me.ok ? k - r : me.done / (b - a);
            if (held)
                me_learn(px + r * w, x, y + r, w, held, 1);
            if (r + held < k)
                me_learn(px + (r + held) * w, x, y + r + held, w, k - r - held, 0);
        }
        whole &= me.ok;
        r = k;
    }
    me_rc_sent(x, y, w, h, hash, (int)whole);
}
static void mir_enc_fill_skip(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t c)
{
    uint32_t j, s, s0 = x / MIR_SEGW, s1 = (x + w - 1u) / MIR_SEGW, hs = 2166136261u, pc = (c >> 8 | c << 8) & 0xFFFFu;
    for (j = 0; j < MIR_SEGW; j++)
        hs = (hs ^ pc) * 16777619u;
    if (me_rc_held(x, y, w, h, c ^ 0x5A5A0000u)) {
        me_rc_hits++;
        return;                         /* (this very fill the client holds) */
    }
    if (w && h && x % MIR_SEGW == 0u && w % MIR_SEGW == 0u) {   /* (whole segments the client holds in this colour: not again) */
        uint32_t all = 1;
        for (j = y; j < y + h && all; j++)
            for (s = s0; s <= s1 && all; s++)
                all = (me_known[j] >> s & 1u) && me_seg[j][s] == hs;
        if (all) {
            me_skipped += (s1 - s0 + 1u) * h;
            return;
        }
    }
    me.ok = 1;
    mir_enc_fill(x, y, w, h, c);
    me_rc_sent(x, y, w, h, c ^ 0x5A5A0000u, me.ok);
    for (j = y; j < y + h; j++)
        for (s = s0; s <= s1; s++) {
            uint32_t sx = s * MIR_SEGW;
            if (me.ok && ME_SEG_OK() && sx >= x && sx + MIR_SEGW <= x + w)
                me_seg[j][s] = hs, me_known[j] |= (uint8_t)(1u << s);
            else
                me_known[j] &= (uint8_t)~(1u << s);
        }
}
#else
#define mir_enc_blit_skip mir_enc_blit
#define mir_enc_fill_skip mir_enc_fill
static void mir_enc_forget_all(void) {}
static void mir_enc_forget(uint32_t x, uint32_t y, uint32_t w, uint32_t h) { (void)x, (void)y, (void)w, (void)h; }
#endif
#endif
