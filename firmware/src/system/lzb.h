/* SPDX-License-Identifier: GPL-3.0-only */
/* LZB: what the boot copies into RAM (.data's initial values, the pool's tables, the RAM code) is one stream in flash,
 * coded by tools/lzb.py (LZ4's block shape with a 3-byte minimum match and 1-byte offsets up to 256: 52 KB of RAM
 * code and tables take 44 KB of flash, 2026-10-23), a block for each in the order main.c fm1_cstart decodes them.
 * One block: d..e from s. A sequence is a token (bits 7..4 the literal count, 15 = more in the bytes after, each
 * added, 255 = another byte; bit 3 = a 2-byte offset; bits 2..0 the match length - 3, 7 = more, the same way), the
 * literals, the offset (1 byte: distance - 1; or 2 bytes little-endian), the length bytes; the match is copied byte
 * by byte from distance back (it may overlap). The block's last sequence is literals only: the loop ends when d
 * reaches e, which is where the next block starts. Never past e whatever the stream says. Runs from flash before any
 * RAM code or global exists: no call, no global (tests/lzb_test.c runs it on tools/lzb.py's vectors). */
static const uint8_t *lzb_expand(const uint8_t *s, uint8_t *d, uint8_t *e)
{
    while (d < e) {
        uint32_t t = *s++, n = t >> 4, b;
        if (n == 15u)
            do { b = *s++; n += b; } while (b == 255u);
        for (; n && d < e; n--)
            *d++ = *s++;
        if (d >= e)
            break;                                  /* (the block's last literals) */
        if (t & 8u) {
            b = s[0] | ((uint32_t)s[1] << 8);
            s += 2;
        } else {
            b = (uint32_t)*s++ + 1u;
        }
        n = (t & 7u) + 3u;
        if ((t & 7u) == 7u) {
            uint32_t m;
            do { m = *s++; n += m; } while (m == 255u);
        }
        for (; n && d < e; n--, d++)
            *d = d[-(int32_t)b];
    }
    return s;
}
