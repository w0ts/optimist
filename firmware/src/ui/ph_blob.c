/* SPDX-License-Identifier: GPL-3.0-only */
/* The help lines' table (FELUCCA_PARAM_HELP; tools/gen_param_help.py -> build/gen/felucca_param_help.h): the lookup
 * and the unpacking both UIs share (SLOOP's ui/sloop/param_help.c, the Optimist UI's ui/optimist/op_help.c include
 * it). PH_BLOB holds the groups as (kind, context, label -> text); the labels and contexts plain, the texts packed
 * with a byte pair code: a byte 0x80..0xFF or 0x01..0x1F stands for a pair of PH_PAIR (each a character or an earlier
 * code), printable ASCII for itself. A line is unpacked into the caller's buffer of PH_LINE bytes when it is drawn:
 * main loop only, nothing in RAM. */
#include "felucca_param_help.h"

static const char *ph_next(const char *s)              /* past the string at s and its NUL */
{
    while (*s)
        s++;
    return s + 1;
}
/* label's line in context ctx of kind (PH_*), as its offset in PH_BLOB; 0 = none */
static __attribute__((noinline)) uint32_t ph_find(uint32_t kind, const char *ctx, const char *label)
{
    const char *p = PH_BLOB;
    while (*p) {
        int here = (uint8_t)*p == kind && str_eq(p + 1, ctx);
        for (p = ph_next(p + 1); *p; p = ph_next(ph_next(p)))
            if (here && str_eq(p, label))
                return (uint32_t)(ph_next(p) - PH_BLOB);
        p++;                                            /* (the group's empty label) */
    }
    return 0;
}
/* the characters of byte c of a packed text at o (a pair: its two halves, at most a few deep) -> past them */
static char *ph_put(char *o, uint32_t c)
{
    if (c >= 0x20u && c < 0x80u) {
        *o = (char)c;
        return o + 1;
    }
    c = c >= 0x80u ? c - 0x80u : c + 0x7Fu;             /* (the pair's index: 0x80.. 0, 0x01.. 128) */
    return ph_put(ph_put(o, PH_PAIR[c][0]), PH_PAIR[c][1]);
}
/* the text of the line at offset off of PH_BLOB (ph_find), unpacked into buf (PH_LINE bytes) -> buf */
static const char *ph_text(uint32_t off, char *buf)
{
    const uint8_t *p = (const uint8_t *)PH_BLOB + off;
    char *o = buf;
    while (*p)
        o = ph_put(o, *p++);
    *o = 0;
    return buf;
}
