/* SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0 */
/* Adaptive frequency hopping as the master of the audio link (builder item BT_AFH; Core Vol 2 Part B 2.6.4, Part C
 * 4.1.4): the channels the device fails to answer on leave the hop set, and the device is told with LMP_set_AFH
 * (bt_lmp.c btl_set_afh). bt_page.c measures (bp.ch_good, its answers per RF channel over BP_CH_SEC) and applies (the
 * engine's AFH table and map, the switch at the instant); the decisions are here, pure, host-tested by
 * tests/bt_afh_test.c. Why: the WH-1000XM4 streamed cleanly, then answered less and less on a few fixed channels
 * (2405, 2406, 2412 and 2419 MHz down to 9 of ~80 answers in 30 s while the other 75 stayed flat,
 * worktrees/notes/bt/logs/xm4-2026-10-23.log), the stream stalled on them, the queue filled, it crackled; every
 * phone's controller skips such channels, our stack hopped over all 79. Included by bt_page.c. */
#define BT_AFH_CH 79u
#define BT_AFH_MIN 20u                             /* the fewest used channels the spec allows (Part B 2.6.4.2) */
#define BT_AFH_BAD_DIV 2u                          /* a channel leaves under 1/2 of the used channels' mean */
#define BT_AFH_MIN_MEAN 10u                        /* fewer answers a channel on average: no decision (too little heard) */

static int bt_afh_used(const uint8_t map[10], uint32_t ch) { return map[ch >> 3] >> (ch & 7u) & 1u; }

/* a window's answers per RF channel: the channels that fell well under the others leave the map, the worst first,
 * never back in this link (the holes we saw only deepened as the radio warmed); at least BT_AFH_MIN stay
 * -> the channels dropped (0: the map is as it was) */
static uint32_t bt_afh_classify(const uint16_t good[BT_AFH_CH], uint8_t map[10])
{
    uint32_t used = 0, sum = 0, ch, dropped = 0;
    for (ch = 0; ch < BT_AFH_CH; ch++)
        if (bt_afh_used(map, ch)) {
            used++;
            sum += good[ch];
        }
    if (sum < used * BT_AFH_MIN_MEAN)
        return 0;
    while (used > BT_AFH_MIN) {
        uint32_t worst = BT_AFH_CH, wv = 0xFFFFu;
        for (ch = 0; ch < BT_AFH_CH; ch++)
            if (bt_afh_used(map, ch) && good[ch] < wv)
                worst = ch, wv = good[ch];
        if (wv * BT_AFH_BAD_DIV * used >= sum)      /* (the worst still at half the mean or more: all stay) */
            break;
        map[worst >> 3] &= (uint8_t)~(1u << (worst & 7u));
        used--;
        sum -= wv;
        dropped++;
    }
    return dropped;
}

/* the engine's AFH hop table and map words from the map (BR §3, §7), as the vendor controller programs them
 * (btctrler.a bdhw_set_frq_table / bdhw_set_afh, read from its bitcode 2026-10-23 after our first guess, the RF
 * channel numbers, killed three links): the table holds the used channels' INDEXES in the basic table (0-39 the even
 * channels, 40-78 the odd, frq_table[0]'s order), packed, the rest 0 (the spec's AFH mapping k mod N onto them,
 * Part B 2.6.4.2); the three map words are the LMP map's bytes 0-3, 4-7, 8-9 with the used count in word 2 [31:24]
 * -> the used channels' count */
static uint32_t bt_afh_table(const uint8_t map[10], uint8_t table[80], uint32_t words[3])
{
    uint32_t n = 0, i, ch;
    for (i = 0; i < BT_AFH_CH; i++) {
        ch = i < 40u ? 2u * i : 2u * (i - 40u) + 1u;
        if (bt_afh_used(map, ch))
            table[n++] = (uint8_t)i;
    }
    for (i = n; i < 80u; i++)
        table[i] = 0;
    words[0] = (uint32_t)map[0] | (uint32_t)map[1] << 8 | (uint32_t)map[2] << 16 | (uint32_t)map[3] << 24;
    words[1] = (uint32_t)map[4] | (uint32_t)map[5] << 8 | (uint32_t)map[6] << 16 | (uint32_t)map[7] << 24;
    words[2] = (uint32_t)map[8] | (uint32_t)(map[9] & 0x7Fu) << 8 | n << 24;
    return n;
}
