/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of our SDP server (firmware/src/bt/bt_sdps.c): the three requests a sink sends (Core spec Vol 3 Part B
 * §4.5-4.7) answered, the answers read back with the SDP client's parser (bt_l2cap.c bl_sdp_parse): the A2DP source
 * record (class 0x110A, L2CAP PSM 0x0019, AVDTP 1.3, A2DP 1.3), the AVRCP target (0x110C, AVRCP 1.6) and the Device ID
 * (0x1200), the AirPods' own questions as BlueZ answered them (a Raspberry Pi's capture, 2026-10-20). */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../firmware/src/bt/bt_l2cap.c"
#include "../firmware/src/bt/bt_sdps.c"

static int fails;
static void check(const char *what, int ok)
{
    printf("%-96s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

int main(void)
{
    uint8_t o[256];
    struct bl_sdp_info in;
    uint32_t n;
    /* search-attribute: pattern <0x110A>, 0x0400 bytes, all attributes (0x0000-0xFFFF), no continuation */
    static const uint8_t ssa[] = {0x06, 0x00, 0x05, 0x00, 0x0F, 0x35, 0x03, 0x19, 0x11, 0x0A, 0x04, 0x00,
                                  0x35, 0x05, 0x0A, 0x00, 0x00, 0xFF, 0xFF, 0x00};
    /* ... pattern <0x111F> (Handsfree Audio Gateway: not ours) */
    static const uint8_t ssa_other[] = {0x06, 0x00, 0x06, 0x00, 0x0F, 0x35, 0x03, 0x19, 0x11, 0x1F, 0x04, 0x00,
                                        0x35, 0x05, 0x0A, 0x00, 0x00, 0xFF, 0xFF, 0x00};
    /* ... pattern <L2CAP as a UUID128 on the base>, attributes 0x0001 and 0x0009 only */
    static const uint8_t ssa128[] = {0x06, 0x00, 0x07, 0x00, 0x1E, 0x35, 0x11, 0x1C, 0x00, 0x00, 0x01, 0x00, 0x00,
                                     0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB, 0x01, 0x00,
                                     0x35, 0x06, 0x09, 0x00, 0x01, 0x09, 0x00, 0x09, 0x00};
    /* service search: pattern <0x110A, 0x0019>, 5 records max */
    static const uint8_t ss[] = {0x02, 0x00, 0x08, 0x00, 0x0B, 0x35, 0x06, 0x19, 0x11, 0x0A, 0x19, 0x00, 0x19,
                                 0x00, 0x05, 0x00};
    /* service attribute: handle 0x00010000, 0x0400 bytes, all attributes */
    static const uint8_t sa[] = {0x04, 0x00, 0x09, 0x00, 0x0E, 0x00, 0x01, 0x00, 0x00, 0x04, 0x00,
                                 0x35, 0x05, 0x0A, 0x00, 0x00, 0xFF, 0xFF, 0x00};
    uint8_t sa_bad[sizeof sa];

    n = bsd_answer(ssa, sizeof ssa, o);
    check("search-attribute <0x110A>: a response, transaction 5 kept, lengths consistent",
          n > 9u && o[0] == 0x07 && o[2] == 5 && (uint32_t)(o[3] << 8 | o[4]) == n - 5u &&
              (uint32_t)(o[5] << 8 | o[6]) == n - 8u && o[n - 1] == 0);
    check("... the client's parser reads it: one record, class 0x110A",
          bl_sdp_parse(o + 7, n - 8u, 0x110A, &in) == 1 && in.records == 1 && in.has_class);
    check("... L2CAP PSM 0x0019, AVDTP 1.3, A2DP 1.3, features 0x0001, name OPTIMIST",
          in.psm == 0x19 && in.proto_uuid == 0x19 && in.proto_ver == 0x0103 && in.profile_uuid == 0x110D &&
              in.profile_ver == 0x0103 && in.features == 1 && !strcmp(in.name, "OPTIMIST"));
    n = bsd_answer(ssa_other, sizeof ssa_other, o);
    check("search-attribute <0x111F>: an empty list (no record)", n == 10 && o[0] == 0x07 && o[6] == 2 && o[7] == 0x35 && o[8] == 0);
    n = bsd_answer(ssa128, sizeof ssa128, o);
    check("search-attribute <L2CAP, UUID128>, attributes 0x0001, 0x0009: the source's and the target's, those two only",
          n == 8u + 2u + 2u * 23u && o[7] == 0x35 && o[8] == 46 && o[9] == 0x35 && o[10] == 21 && o[18] == 0x0A &&
              o[28] == 0x0D && o[32] == 0x35 && o[41] == 0x0C && o[51] == 0x0E && o[54] == 0x06);
    n = bsd_answer(ss, sizeof ss, o);
    check("service search <0x110A, 0x0019>: one handle, the source's (1 << 16)",
          n == 14 && o[0] == 0x03 && o[6] == 1 && o[8] == 1 && o[9] == 0 && o[10] == 1 && o[11] == 0 && o[12] == 0 && !o[13]);
    n = bsd_answer(sa, sizeof sa, o);
    {
        uint8_t w[256];                            /* (one attribute list: wrapped as the parser's list of lists) */
        w[0] = 0x35, w[1] = (uint8_t)(n - 8u);
        memcpy(w + 2, o + 7, n - 8u);
        check("service attribute, our handle: the attribute list, parsed: class 0x110A",
              n > 9u && o[0] == 0x05 && bl_sdp_parse(w, n - 6u, 0x110A, &in) == 1 && in.has_class && in.psm == 0x19);
    }
    memcpy(sa_bad, sa, sizeof sa);
    sa_bad[8] = 3;
    n = bsd_answer(sa_bad, sizeof sa_bad, o);
    check("service attribute, another handle: error 0x0002", n == 7 && o[0] == 0x01 && o[6] == 2);
    {   /* the AirPods' first question, as captured: Device ID, attributes 0x0201-0x0203, 0x0205, 0xA000, 0xA001, 0xAFFF */
        static const uint8_t did[] = {0x06, 0x00, 0x01, 0x00, 0x1F, 0x35, 0x03, 0x19, 0x12, 0x00, 0x00, 0x40, 0x35, 0x15,
                                      0x09, 0x02, 0x01, 0x09, 0x02, 0x02, 0x09, 0x02, 0x03, 0x09, 0x02, 0x05, 0x09, 0xA0,
                                      0x00, 0x09, 0xA0, 0x01, 0x09, 0xAF, 0xFF, 0x00};
        static const uint8_t want[] = {0x07, 0x00, 0x01, 0x00, 0x1F, 0x00, 0x1C, 0x35, 0x1A, 0x35, 0x18,
                                       0x09, 0x02, 0x01, 0x09, 0x12, 0x09, 0x09, 0x02, 0x02, 0x09, 0x00, 0x01,
                                       0x09, 0x02, 0x03, 0x09, 0x01, 0x00, 0x09, 0x02, 0x05, 0x09, 0x00, 0x02, 0x00};
        n = bsd_answer(did, sizeof did, o);
        check("the AirPods' Device ID question: vendor 0x1209 (source the USB-IF), product 1, version 1.0.0, source 2, nothing else",
              n == sizeof want && !memcmp(o, want, n));
    }
    {   /* ... their AVRCP question: the target's profile descriptor, as BlueZ: AVRCP 1.6 */
        static const uint8_t tg[] = {0x06, 0x00, 0x04, 0x00, 0x0D, 0x35, 0x03, 0x19, 0x11, 0x0C, 0x00, 0x40,
                                     0x35, 0x03, 0x09, 0x00, 0x09, 0x00};
        static const uint8_t want[] = {0x07, 0x00, 0x04, 0x00, 0x14, 0x00, 0x11, 0x35, 0x0F, 0x35, 0x0D, 0x09, 0x00,
                                       0x09, 0x35, 0x08, 0x35, 0x06, 0x19, 0x11, 0x0E, 0x09, 0x01, 0x06, 0x00};
        n = bsd_answer(tg, sizeof tg, o);
        check("the AirPods' AVRCP target question: AVRCP 1.6, byte for byte BlueZ's answer", n == sizeof want && !memcmp(o, want, n));
    }
    {   /* ... their A2DP source question: the protocol descriptors, as BlueZ */
        static const uint8_t src[] = {0x06, 0x00, 0x03, 0x00, 0x0D, 0x35, 0x03, 0x19, 0x11, 0x0A, 0x00, 0x40,
                                      0x35, 0x03, 0x09, 0x00, 0x04, 0x00};
        static const uint8_t want[] = {0x07, 0x00, 0x03, 0x00, 0x1C, 0x00, 0x19, 0x35, 0x17, 0x35, 0x15, 0x09, 0x00, 0x04,
                                       0x35, 0x10, 0x35, 0x06, 0x19, 0x01, 0x00, 0x09, 0x00, 0x19,
                                       0x35, 0x06, 0x19, 0x00, 0x19, 0x09, 0x01, 0x03, 0x00};
        n = bsd_answer(src, sizeof src, o);
        check("the AirPods' A2DP source question: L2CAP 0x0019, AVDTP 1.3, byte for byte BlueZ's answer",
              n == sizeof want && !memcmp(o, want, n));
    }
    n = bsd_answer(ssa, 12, o);
    check("a PDU shorter than its length: error 0x0004", n == 7 && o[0] == 0x01 && o[6] == 4);
    o[0] = 0x99;
    n = bsd_answer((const uint8_t *)"\x08\x00\x01\x00\x00", 5, o);
    check("an unknown PDU: error 0x0003", n == 7 && o[0] == 0x01 && o[6] == 3);
    printf(fails ? "bt_sdps_test: %d FAILED\n" : "bt_sdps_test: all ok\n", fails);
    return fails != 0;
}
