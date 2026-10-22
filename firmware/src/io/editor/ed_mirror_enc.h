/* SPDX-License-Identifier: GPL-3.0-only */
/* Editor protocol: the screen mirrored, its encoder (ed_mirror.c; web/EDITOR_PROTOCOL.md "The screen mirrored"; builder item
 * ED_MIRROR). Every write to the panel (display/lcd.c lcd_fill, lcd_blit) goes out as it is drawn, so a client that applies them in
 * order holds the panel's pixels: no frame buffer on the device, nothing drawn twice. Included by ed_mirror.c and tests/mirror_test.c,
 * which supply the wire: mir_wire(b) one byte of the frame (F0 .. F7; 0: the link is gone, stop) and mir_wire_end() after F7.
 *   106 MIRROR push   op 0 FILL  x, y, w, h (2 x 7 bit each), colour (RGB565, 3 x 7 bit)
 *                     op 1 BLIT  x, y, w, h, then runs: the rectangle's pixels row by row
 *                     op 2 MORE  runs: the same rectangle goes on (a BLIT is cut every MIR_CUT bytes)
 *   a run: one token, t = n << 3 | s; n 0..14: n + 1 pixels; n 15: 16 + (2 x 7 bit) pixels after it; s 0..6 the colour in that
 *   slot, s 7 a colour after it (3 x 7 bit), which then takes slot k (1, 2 .. 6, round). The slots are reset at each BLIT: slot 0
 *   is black (always), the others black until written. */
#ifndef ED_MIRROR_ENC_H
#define ED_MIRROR_ENC_H
enum { ED_MIRROR = 106, MIR_CUT = 250, MIR_SLOTS = 7, MIR_LIT = 7, MIR_LONG = 15 };
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
/* n pixels of colour c (n >= 1) */
static void me_run(uint32_t c, uint32_t n)
{
    uint32_t s, k;
    for (s = 0; s < MIR_SLOTS && me.slot[s] != c; s++)
        ;
    while (n && me.ok) {
        if (me.n > MIR_CUT) {           /* (a token is 6 bytes at most: the frame stays under MIR_CUT + 8) */
            me_close();
            me_open(2);
        }
        k = n < 16u + 16383u ? n : 16u + 16383u;
        me_b((k <= 15u ? k - 1u : MIR_LONG) << 3 | (s < MIR_SLOTS ? s : MIR_LIT));
        if (k > 15u)
            me_b2(k - 16u);
        if (s >= MIR_SLOTS) {
            me_col(c);
            s = me.rr;
            me.slot[s] = (uint16_t)c;
            me.rr = (uint8_t)(me.rr < MIR_SLOTS - 1u ? me.rr + 1u : 1u);
        }
        n -= k;
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
        for (r = i + 1u; r < n && px[r] == px[i]; r++)
            ;
        c = px[i];
        me_run((c >> 8 | c << 8) & 0xFFFFu, r - i);
        i = r;
    }
    me_close();
}
#endif
