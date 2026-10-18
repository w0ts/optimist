/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of the Classic probe's L2CAP signalling and SDP client (firmware/src/bt/bt_l2cap.c): the frames byte for
 * byte (Core spec Vol 3 Part A §4) and the reading of an A2DP sink record built here from the spec's data elements
 * (Part B §3, the A2DP spec's record layout): class 0x110B, L2CAP PSM 0x0019, AVDTP 1.3, A2DP 1.3, features, name. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../firmware/src/bt/bt_l2cap.c"

static int fails;
static void check(const char *what, int ok)
{
    printf("%-72s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

int main(void)
{
    uint8_t o[64];
    static const uint8_t creq[12] = {8, 0, 1, 0, 2, 7, 4, 0, 1, 0, 0x40, 0};
    static const uint8_t cfg[16] = {12, 0, 1, 0, 4, 3, 8, 0, 0x41, 0, 0, 0, 1, 2, 0xA0, 2};
    /* AttributeLists: one record { 0x0000: 0x00010001, 0x0001: <0x110B>, 0x0004: <<0x0100, 0x0019>, <0x0019, 0x0103>>,
     * 0x0009: <<0x110D, 0x0103>>, 0x0100: "Audio Sink", 0x0311: 0x000B } */
    static const uint8_t rec[] = {
        0x35, 0x49, 0x35, 0x47,
        0x09, 0x00, 0x00, 0x0A, 0x00, 0x01, 0x00, 0x01,
        0x09, 0x00, 0x01, 0x35, 0x03, 0x19, 0x11, 0x0B,
        0x09, 0x00, 0x04, 0x35, 0x10, 0x35, 0x06, 0x19, 0x01, 0x00, 0x09, 0x00, 0x19,
        0x35, 0x06, 0x19, 0x00, 0x19, 0x09, 0x01, 0x03,
        0x09, 0x00, 0x09, 0x35, 0x08, 0x35, 0x06, 0x19, 0x11, 0x0D, 0x09, 0x01, 0x03,
        0x09, 0x01, 0x00, 0x25, 0x0A, 'A', 'u', 'd', 'i', 'o', ' ', 'S', 'i', 'n', 'k',
        0x09, 0x03, 0x11, 0x09, 0x00, 0x0B};
    struct bl_sdp_info in;
    uint32_t n;

    check("connection request: PSM 1, SCID 0x0040, identifier 7", bl_conn_req(o, 7, 1, 0x40) == 12 && !memcmp(o, creq, 12));
    check("configuration request: DCID 0x0041, MTU 672", bl_conf_req(o, 3, 0x41, 672) == 16 && !memcmp(o, cfg, 16));
    n = bl_conf_rsp(o, 9, 0x55, 0);
    check("configuration response: SCID 0x0055, success", n == 14 && o[4] == 5 && o[5] == 9 && o[8] == 0x55 && !o[12]);
    n = bl_info_rsp(o, 2, 3);
    check("information response, fixed channels: signalling only", n == 20 && o[4] == 11 && o[8] == 3 && o[12] == 2);
    n = bl_info_rsp(o, 2, 2);
    check("information response, extended features: none", n == 16 && o[10] == 0 && !o[12] && !o[15]);
    n = bl_info_rsp(o, 2, 9);
    check("information response, another type: not supported", n == 12 && o[10] == 1);
    check("command reject: not understood", bl_cmd_reject(o, 5) == 10 && o[4] == 1 && o[5] == 5 && o[8] == 0);
    n = bl_conn_rsp(o, 4, 0x40, 0x41, 2);
    check("connection response: refused, PSM not supported", n == 16 && o[4] == 3 && o[12] == 2);
    n = bl_sdp_ssa_req(o, 0x41, 1, 0x110B, 0, 0);
    check("SDP search-attribute request: UUID 0x110B, 512 bytes, all attributes, no continuation",
          n == 4u + 20u && o[0] == 20 && o[2] == 0x41 && o[4] == 6 && o[6] == 1 && o[8] == 15 && o[12] == 0x11 &&
              o[13] == 0x0B && o[14] == 2 && o[21] == 0xFF && o[23] == 0);
    n = bl_sdp_ssa_req(o, 0x41, 2, 0x110B, (const uint8_t *)"\x01\x02", 2);
    check("... with a 2-byte continuation state", n == 26 && o[23] == 2 && o[24] == 1 && o[25] == 2 && o[8] == 17);
    check("the A2DP sink record parses", bl_sdp_parse(rec, sizeof rec, 0x110B, &in) == 1);
    check("... class 0x110B, PSM 0x0019, AVDTP 1.3", in.has_class && in.psm == 0x19 && in.proto_uuid == 0x19 &&
                                                           in.proto_ver == 0x0103);
    check("... A2DP 1.3, features 0x000B, name \"Audio Sink\", one record",
          in.profile_uuid == 0x110D && in.profile_ver == 0x0103 && in.features == 0x0B && !strcmp(in.name, "Audio Sink") &&
              in.records == 1);
    check("a truncated list does not parse", bl_sdp_parse(rec, sizeof rec - 3, 0x110B, &in) == 0);
    check("an empty list parses (no record)", bl_sdp_parse((const uint8_t *)"\x35\x00", 2, 0x110B, &in) == 1 && !in.records);
    printf(fails ? "bt_l2cap_test: %d FAILED\n" : "bt_l2cap_test: all ok\n", fails);
    return fails != 0;
}
