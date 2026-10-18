/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of the Classic probe's AVDTP initiator (firmware/src/bt/bt_avdtp.c): commands byte for byte (AVDTP 1.3
 * §8), a DISCOVER answer with a source, a sink in use and a free sink, a GET_CAPABILITIES answer with SBC, the SBC
 * choice (A2DP 1.3 §4.3) and SET_CONFIGURATION's parameters. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../firmware/src/bt/bt_avdtp.c"

static int fails;
static void check(const char *what, int ok)
{
    printf("%-72s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

int main(void)
{
    uint8_t o[32], sbc[4], conf[4];
    uint32_t l, m, s;
    static const uint8_t disc[8] = {0x12, 0x01, 1 << 2, 0x00, 2 << 2 | 2, 0x08, 3 << 2, 0x08};   /* src, sink in use, sink */
    static const uint8_t caps[14] = {0x32, 0x02, 1, 0, 7, 6, 0x00, 0x00, 0xFF, 0xFF, 2, 53, 8, 0};
    static const uint8_t setp[12] = {3 << 2, 1 << 2, 1, 0, 7, 6, 0, 0, 0x21, 0x15, 2, 53};
    uint8_t par = 3 << 2;

    check("DISCOVER, label 1: 10 01", bv_cmd(o, 1, BV_DISCOVER, 0, 0) == 2 && o[0] == 0x10 && o[1] == 1);
    check("OPEN SEID 3, label 4: 40 06 0C", bv_cmd(o, 4, BV_OPEN, &par, 1) == 3 && o[0] == 0x40 && o[1] == 6 && o[2] == 12);
    check("header of an accept: label 1, accept, DISCOVER", bv_hdr(disc, 8, &l, &m, &s) && l == 1 && m == BV_ACCEPT && s == 1);
    check("a fragmented packet is not read", !bv_hdr((const uint8_t *)"\x14\x01", 2, &l, &m, &s));
    check("DISCOVER answer: the free sink is SEID 3", bv_pick_sink(disc, 8) == 3);
    check("... none when only a source", bv_pick_sink(disc, 4) == 0);
    check("GET_CAPABILITIES answer: SBC FF FF 02 35", bv_caps_sbc(caps, 14, sbc) && sbc[0] == 0xFF && sbc[3] == 53);
    check("... no codec, no SBC", !bv_caps_sbc(caps, 6, sbc));
    check("SBC choice: 44.1 kHz joint stereo, 16 blocks 8 subbands loudness, bitpool 2..53",
          bv_sbc_choose(sbc, conf) && conf[0] == 0x21 && conf[1] == 0x15 && conf[2] == 2 && conf[3] == 53);
    sbc[0] = 0x12, sbc[3] = 250;
    check("... 48 kHz stereo when that is all, bitpool capped at 53",
          bv_sbc_choose(sbc, conf) && conf[0] == 0x12 && conf[3] == 53);
    sbc[1] = 0x20;
    check("... no choice without 16 blocks", !bv_sbc_choose(sbc, conf));
    conf[0] = 0x21, conf[1] = 0x15, conf[2] = 2, conf[3] = 53;
    check("SET_CONFIGURATION parameters: ACP 3, INT 1, transport, SBC", bv_setconf_par(o, 3, 1, conf) == 12 &&
                                                                             !memcmp(o, setp, 12));
    printf(fails ? "bt_avdtp_test: %d FAILED\n" : "bt_avdtp_test: all ok\n", fails);
    return fails != 0;
}
