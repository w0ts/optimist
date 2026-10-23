/* SPDX-License-Identifier: GPL-3.0-only */
/* The boot stream's decoder (firmware/src/system/lzb.h lzb_expand, what fm1_cstart runs on the LZB stream of the RAM
 * code, the tables and .data) against tools/lzb.py's vectors (lzb_vectors.h, generated at every build: blobs with a
 * 1-byte offset of 256, 2-byte offsets, overlapping copies, long literal and match runs, empty and tiny ones, all in
 * one stream): every blob decodes byte for byte, each block ends exactly where the next starts, and the stream is
 * read to its end. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "lzb_vectors.h"
#include "../firmware/src/system/lzb.h"

static uint8_t out[1 << 17];

int main(void)
{
    const uint8_t *s = LZB_CODED, *plain = LZB_PLAIN;
    uint32_t i, fails = 0;
    for (i = 0; i < LZB_NCASES; i++) {
        uint32_t n = LZB_SIZES[i];
        const uint8_t *next;
        memset(out, 0xA5, sizeof out);
        next = lzb_expand(s, out, out + n);
        if (memcmp(out, plain, n) != 0 || out[n] != 0xA5u) {
            printf("FAIL blob %u (%u B): decoded bytes differ, or written past its end\n", (unsigned)i, (unsigned)n);
            fails++;
        }
        if (next < s || next > LZB_CODED + LZB_CODED_N) {
            printf("FAIL blob %u: the cursor left the stream\n", (unsigned)i);
            fails++;
        }
        s = next;
        plain += n;
    }
    if (s != LZB_CODED + LZB_CODED_N) {
        printf("FAIL: %ld B of the stream left after the last block\n", (long)(LZB_CODED + LZB_CODED_N - s));
        fails++;
    }
    printf("%s lzb: %u blobs decoded as tools/lzb.py coded them, the stream read to its end\n", fails ? "FAIL" : "ok",
           (unsigned)LZB_NCASES);
    return fails ? 1 : 0;
}
