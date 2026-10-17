/* SPDX-License-Identifier: GPL-3.0-only */
/* Editor protocol: a reply's bytes several at a time, on ed_b (editor.c's, or a host test's own): a call is 4 bytes of
 * flash and its argument 2..6 more, so two to four bytes of a reply in one call save 4 a byte at every reply that
 * sends them (2026-10, the slim pass). C leaves the order of the arguments open: none of them may change what another
 * reads. Also n bytes from memory (ed_bs) and a value as k 7-bit groups both ways (ed_b32 out, ed_r32 in: the backup's,
 * the snapshots', SLOOP 2.4's, an offset's). Included by editor.c and each file of its commands that uses them; host
 * tests include those. */
#ifndef ED_REPLY_H
#define ED_REPLY_H
static void ed_b2(uint32_t x, uint32_t y)
{
    ed_b(x);
    ed_b(y);
}
static void ed_b3(uint32_t x, uint32_t y, uint32_t z)
{
    ed_b2(x, y);
    ed_b(z);
}
static void ed_b4(uint32_t w, uint32_t x, uint32_t y, uint32_t z)
{
    ed_b2(w, x);
    ed_b2(y, z);
}
static void ed_bs(const uint8_t *p, uint32_t n) { while (n--) ed_b(*p++); }          /* n bytes from p */
static void ed_b32(uint32_t v, uint32_t k) { while (k--) { ed_b(v); v >>= 7; } }   /* v as k 7-bit groups, LSB first */
static uint32_t ed_r32(const uint8_t *a, uint32_t k)                                 /* k 7-bit groups at a, LSB first */
{
    uint32_t v = 0;
    while (k--)
        v |= (uint32_t)a[k] << (7u * k);
    return v;
}
#endif
