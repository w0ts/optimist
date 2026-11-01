/* SPDX-License-Identifier: GPL-3.0-only */
/* The Wi-Fi password being typed (SYSTEM > WI-FI, op_wifi.c; docs/UI-OPTIMIST-DESIGN.md 4.13): the buffer, the cursor, the
 * four character families and the pure operations on them. No drawing, no buttons, no radio: the same code runs in
 * tests/wifi_pw_test.c. Case-sensitive, 8..63 printable ASCII characters (WPA2-PSK; the display font draws 32..126 only,
 * so there is no accent family: a character the screen cannot draw would be typed blind). The buffer is wiped by pw_wipe
 * and never copied anywhere (op_wifi.c hands the manager its text once). */
#ifndef OP_PWBUF_H
#define OP_PWBUF_H
#include <stdint.h>

#define PW_MIN 8u
#define PW_MAX 63u
#define PW_NFAM 4u
/* the families: KNOB 2 picks one, KNOB 1 a character in it (the user, 2026-10-27). The signs end in a space, a WPA2
 * password may hold one */
static const char *const PW_FAM[PW_NFAM] = {
    "abcdefghijklmnopqrstuvwxyz",
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ",
    "0123456789",
    "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~ ",
};
static const char *const PW_FAM_TAG[PW_NFAM] = {"abc", "ABC", "123", "#+="};   /* (the tabs, as drawn) */

struct pw_buf {
    uint8_t len, cur, fam;                              /* the length; the cursor 0..len (insert before it); the family */
    uint8_t idx[PW_NFAM];                               /* each family's selected character (kept when KNOB 2 leaves it) */
    char s[PW_MAX + 1u];
};

static uint32_t pw_fam_n(uint32_t f)
{
    uint32_t n = 0;
    while (PW_FAM[f % PW_NFAM][n])
        n++;
    return n;
}
static char pw_sel(const struct pw_buf *b)
{
    uint32_t f = b->fam < PW_NFAM ? b->fam : 0u, i = b->idx[f];
    return PW_FAM[f][i < pw_fam_n(f) ? i : 0u];         /* (no divide: the index is kept inside its family) */
}
static void pw_wipe(struct pw_buf *b)
{
    uint8_t *p = (uint8_t *)b;
    uint32_t i;
    for (i = 0; i < sizeof *b; i++)
        p[i] = 0;
}
static void pw_pick(struct pw_buf *b, int32_t s)        /* KNOB 1: the character, round the family */
{
    uint32_t f = b->fam < PW_NFAM ? b->fam : 0u;
    int32_t n = (int32_t)pw_fam_n(f), i = (int32_t)b->idx[f] + s;
    while (i < 0)                                       /* (a fast turn is a few dozen at most: no divide) */
        i += n;
    while (i >= n)
        i -= n;
    b->idx[f] = (uint8_t)i;
}
static void pw_family(struct pw_buf *b, int32_t s)      /* KNOB 2: the family, round */
{
    int32_t f = ((int32_t)b->fam + s) % (int32_t)PW_NFAM;
    b->fam = (uint8_t)(f < 0 ? f + (int32_t)PW_NFAM : f);
}
static void pw_move(struct pw_buf *b, int32_t s)        /* KNOB 3: the cursor, stopping at the ends */
{
    int32_t c = (int32_t)b->cur + s;
    b->cur = (uint8_t)(c < 0 ? 0 : c > (int32_t)b->len ? (int32_t)b->len : c);
}
static int pw_add(struct pw_buf *b)                     /* SAVE tapped: the selected character at the cursor -> 0 when full */
{
    uint32_t i;
    if (b->len >= PW_MAX)
        return 0;
    for (i = b->len; i > b->cur; i--)
        b->s[i] = b->s[i - 1u];
    b->s[b->cur++] = pw_sel(b);
    b->s[++b->len] = 0;
    return 1;
}
static int pw_erase(struct pw_buf *b)                   /* HOME tapped: the character before the cursor -> 0 at the start */
{
    uint32_t i;
    if (!b->cur)
        return 0;
    for (i = --b->cur; i < b->len; i++)
        b->s[i] = b->s[i + 1u];
    b->len--;
    return 1;
}
static int pw_valid(const struct pw_buf *b) { return b->len >= PW_MIN && b->len <= PW_MAX; }
/* the first character shown of a window of w, so that the cursor is in it (kept where it was while it is) */
static uint32_t pw_window(const struct pw_buf *b, uint32_t first, uint32_t w)
{
    if (b->cur < first)
        first = b->cur;
    else if (b->cur >= first + w)
        first = b->cur - w + 1u;
    return first;
}
#endif
