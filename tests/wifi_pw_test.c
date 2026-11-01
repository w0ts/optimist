/* SPDX-License-Identifier: GPL-3.0-only */
/* The Wi-Fi password buffer (firmware/src/ui/optimist/op_pwbuf.h): the four families, the character and family knobs (round),
 * the cursor (stops at the ends), add at the cursor, erase before it, the 63 limit, the 8 minimum, the window, the wipe. */
#include <stdio.h>
#include <string.h>
#include "../firmware/src/ui/optimist/op_pwbuf.h"
static int fails;
static void check(int ok, const char *what)
{
    printf("wifi pw: %-90s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}
int main(void)
{
    struct pw_buf b;
    uint32_t i, f, printable = 1, first;
    memset(&b, 0, sizeof b);
    for (f = 0; f < PW_NFAM; f++)
        for (i = 0; i < pw_fam_n(f); i++)
            printable &= (uint8_t)PW_FAM[f][i] >= 32u && (uint8_t)PW_FAM[f][i] <= 126u;
    check(printable, "every character of every family is in the display font's range (32..126): nothing typed blind");
    check(pw_fam_n(0) == 26 && pw_fam_n(1) == 26 && pw_fam_n(2) == 10 && pw_fam_n(3) == 33, "families: 26 lower, 26 upper, 10 digits, 33 signs (with the space)");
    {   /* together they are all of 32..126, once each */
        uint8_t seen[128] = {0};
        uint32_t all = 1;
        for (f = 0; f < PW_NFAM; f++)
            for (i = 0; i < pw_fam_n(f); i++)
                seen[(uint8_t)PW_FAM[f][i]]++;
        for (i = 32; i < 127u; i++)
            all &= seen[i] == 1u;
        check(all, "... together every printable ASCII character once (a WPA2 passphrase can be any of them)");
    }
    check(pw_sel(&b) == 'a', "empty: family abc, the character a");
    pw_pick(&b, -1);
    check(pw_sel(&b) == 'z', "KNOB 1 left from a: round to z");
    pw_pick(&b, 30);
    check(pw_sel(&b) == 'd', "... a fast turn of 30 wraps (z + 30 = d)");
    pw_family(&b, 1);
    check(b.fam == 1 && pw_sel(&b) == 'A', "KNOB 2: UPPER CASE, its own character (A)");
    pw_family(&b, -2);
    check(b.fam == 3 && pw_sel(&b) == '!', "... left twice: round to the signs");
    pw_family(&b, 1);
    check(b.fam == 0 && pw_sel(&b) == 'd', "... and round to abc: the character was kept (d)");
    check(pw_add(&b) && pw_add(&b) && b.len == 2 && !strcmp(b.s, "dd") && b.cur == 2, "SAVE tapped: the character added at the cursor");
    pw_move(&b, -1);
    pw_pick(&b, 1);
    pw_add(&b);
    check(!strcmp(b.s, "ded") && b.cur == 2, "KNOB 3 left, an add goes in before the cursor");
    pw_move(&b, -9);
    check(b.cur == 0, "the cursor stops at the start");
    check(!pw_erase(&b) && !strcmp(b.s, "ded"), "... erase there: nothing");
    pw_move(&b, 9);
    check(b.cur == 3, "... and at the end");
    check(pw_erase(&b) && !strcmp(b.s, "de") && b.cur == 2, "erase: the character before the cursor");
    pw_move(&b, -1);
    check(pw_erase(&b) && !strcmp(b.s, "e") && b.cur == 0 && b.len == 1, "... inside the text: the text closes up");
    check(!pw_valid(&b), "1 character: too short");
    for (i = 0; i < 7u; i++)
        pw_add(&b);
    check(b.len == 8 && pw_valid(&b), "8 characters: valid");
    for (i = 0; i < 100u; i++)
        pw_add(&b);
    check(b.len == 63 && pw_valid(&b) && b.s[63] == 0, "63 characters at most; add past that does nothing; the text stays terminated");
    check(!pw_add(&b), "... pw_add says so");
    b.cur = 63;
    first = pw_window(&b, 0, 14);
    check(first == 50, "the window follows the cursor: at the end the last 14 characters (50..63)");
    b.cur = 5;
    check(pw_window(&b, 50, 14) == 5 && pw_window(&b, 0, 14) == 0, "... moved left it follows; where the cursor is in view it stays");
    pw_wipe(&b);
    {
        const uint8_t *p = (const uint8_t *)&b;
        uint32_t z = 1;
        for (i = 0; i < sizeof b; i++)
            z &= p[i] == 0;
        check(z, "wipe: every byte of the buffer is 0");
    }
    printf(fails ? "wifi pw test FAILED (%d)\n" : "wifi pw test passed\n", fails);
    return fails != 0;
}
