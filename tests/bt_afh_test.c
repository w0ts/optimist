/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of the audio link's adaptive frequency hopping decisions (firmware/src/bt/bt_afh.c): which channels leave
 * the map from a window's answers per channel, the spec's minimum of 20, and the engine's table and map words. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../firmware/src/bt/bt_afh.c"

static int fails;
static void check(const char *what, int ok)
{
    printf("%-72s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}
static uint32_t used_count(const uint8_t map[10])
{
    uint32_t n = 0, ch;
    for (ch = 0; ch < 79u; ch++)
        n += bt_afh_used(map, ch);
    return n;
}
static void full_map(uint8_t map[10])
{
    memset(map, 0xFF, 9);
    map[9] = 0x7F;
}

int main(void)
{
    /* the XM4's answers per RF channel 0..78 at 120 s (logs/xm4-2026-10-23.log): holes at 4, 10 and 17 */
    static const uint16_t xm4[79] = {81, 78, 71, 48, 10, 82, 85, 74, 66, 58, 27, 72, 88, 78, 62, 77, 65, 19, 68, 72, 86, 81, 70, 51, 62, 77, 67, 85, 69, 67,
                                     55, 73, 77, 81, 77, 72, 80, 50, 71, 74, 71, 76, 86, 94, 54, 72, 75, 84, 66, 64, 64, 54, 75, 71, 83, 87, 78, 66, 64, 72,
                                     66, 91, 74, 79, 66, 76, 66, 75, 81, 75, 78, 79, 67, 85, 77, 86, 82, 73, 70};
    uint16_t good[79];
    uint8_t map[10], table[80];
    uint32_t words[3], i, n;

    full_map(map);
    for (i = 0; i < 79u; i++)
        good[i] = (uint16_t)(70u + (i % 7u));
    check("a flat window: nothing leaves, 79 used", bt_afh_classify(good, map) == 0 && used_count(map) == 79u);

    full_map(map);
    n = bt_afh_classify(xm4, map);
    check("the XM4's window: channels 4, 10 and 17 leave (under half the mean), 3 (48) and 37 (50) stay",
          n == 3 && !bt_afh_used(map, 4) && !bt_afh_used(map, 10) && !bt_afh_used(map, 17) && bt_afh_used(map, 3) && bt_afh_used(map, 37)
          && used_count(map) == 76u);
    n = bt_afh_classify(xm4, map);
    check("the same window again: the map is as it was (the dropped ones count no more)", n == 0 && used_count(map) == 76u);

    full_map(map);
    for (i = 0; i < 79u; i++)
        good[i] = 0;
    for (i = 0; i < 10u; i++)
        good[i * 7u] = 100;
    n = bt_afh_classify(good, map);
    check("ten channels answer, 69 silent: 59 leave, 20 stay (the spec's minimum), the ten among them",
          n == 59u && used_count(map) == 20u && bt_afh_used(map, 0) && bt_afh_used(map, 63));

    full_map(map);
    for (i = 0; i < 79u; i++)
        good[i] = (uint16_t)(i == 4u ? 0u : 5u);
    check("too little heard (under 10 a channel): no decision", bt_afh_classify(good, map) == 0 && used_count(map) == 79u);

    full_map(map);
    n = bt_afh_table(map, table, words, 0);
    check("the full map's table: 79 used, the basic order (0 2 4 .. 78 1 3 .. 77), the rest 0",
          n == 79u && table[0] == 0 && table[1] == 2 && table[39] == 78 && table[40] == 1 && table[78] == 77 && table[79] == 0
          && words[0] == 0xFFFFFFFFu && words[1] == 0xFFFFFFFFu && words[2] == (79u << 24 | 0x7FFFu));

    full_map(map);
    map[0] &= (uint8_t)~0x10u;                     /* channel 4 out */
    map[2] &= (uint8_t)~0x02u;                     /* channel 17 out */
    n = bt_afh_table(map, table, words, 0);
    check("4 and 17 out, packed: 77 used, 0 2 6 8 .. 78 1 3 .. 15 19 .. 77, [77] = 0; the count in word 2 [31:24]",
          n == 77u && table[2] == 6 && table[38] == 78 && table[39] == 1 && table[46] == 15 && table[47] == 19 && table[76] == 77 && table[77] == 0
          && words[0] == 0xFFFDFFEFu && words[2] == (77u << 24 | 0x7FFFu));
    n = bt_afh_table(map, table, words, 1);
    check("the same, full: a used output stays itself, output 2 (channel 4) -> packed[2 mod 77] = 6, output 48 (17) -> packed[48] = 21",
          n == 77u && table[0] == 0 && table[1] == 2 && table[2] == 6 && table[39] == 78 && table[40] == 1 && table[48] == 21 && table[78] == 77);

    printf("%s\n", fails ? "FAILED" : "all ok");
    return fails != 0;
}
