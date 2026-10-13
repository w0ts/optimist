/* SPDX-License-Identifier: GPL-3.0-only */
/* The help lines' packing (tools/gen_param_help.py, firmware/src/ui/ph_blob.c): every line of tools/param_help.json,
 * unpacked by the firmware's ph_text, reads exactly as the table has it (PH_PLAIN), fits PH_LINE, and is found by its
 * context and label. Built against `gen_param_help.py --all DIR/felucca_param_help.h` (every group, no condition):
 *   cc -IDIR tests/param_help_pack_test.c && ./a.out */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int str_eq(const char *a, const char *b) { return !strcmp(a, b); }
#include "../firmware/src/ui/ph_blob.c"

int main(void)
{
    const uint32_t npl = sizeof PH_PLAIN / sizeof PH_PLAIN[0], npair = sizeof PH_PAIR / sizeof PH_PAIR[0];
    uint32_t n = 0, bad = 0, plain = 0, packed = 0;
    const char *p = PH_BLOB;
    char buf[PH_LINE + 8];
    while (*p) {
        const char *ctx = p + 1;
        uint32_t kind = (uint8_t)*p;
        for (p = ph_next(ctx); *p; ) {
            const char *label = p;
            const uint8_t *t = (const uint8_t *)ph_next(label), *q;
            uint32_t off = (uint32_t)((const char *)t - PH_BLOB);
            for (q = t; *q; q++)
                if ((*q < 0x20u || *q >= 0x80u) && (*q >= 0x80u ? *q - 0x80u : *q + 0x7Fu) >= npair)
                    bad++, printf("FAIL: %s / %s: code %u past the %u pairs\n", ctx, label, *q, npair);
            memset(buf, 0x55, sizeof buf);
            ph_text(off, buf);
            if (n >= npl || strcmp(PH_PLAIN[n][0], ctx) || strcmp(PH_PLAIN[n][1], label) ||
                strcmp(PH_PLAIN[n][2], buf))
                bad++, printf("FAIL: line %u %s / %s: \"%s\", the table: \"%s\"\n", n, ctx, label, buf,
                              n < npl ? PH_PLAIN[n][2] : "(none)");
            if (strlen(buf) + 1 > PH_LINE || (uint8_t)buf[PH_LINE] != 0x55)
                bad++, printf("FAIL: %s / %s: longer than PH_LINE %u\n", ctx, label, (unsigned)PH_LINE);
            if (!ph_find(kind, ctx, label))
                bad++, printf("FAIL: %s / %s: not found\n", ctx, label);
            plain += (uint32_t)strlen(buf) + 1;
            packed += (uint32_t)(q - t) + 1;
            n++;
            p = ph_next((const char *)t);
        }
        p++;
    }
    if (n != npl)
        bad++, printf("FAIL: %u lines in PH_BLOB, %u in the table\n", n, npl);
    printf("help packing: %u lines, texts %u B -> %u B + %u B of pairs\n", n, plain, packed, 2u * npair);
    printf(bad ? "help packing: %u FAILED\n" : "help packing: test passed\n", bad);
    return bad != 0;
}
