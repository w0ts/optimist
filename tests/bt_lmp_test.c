/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of the Classic probe's minimal LMP (firmware/src/bt/bt_lmp.c): the PDUs we send to set a link up and the
 * answers to a slave's requests, byte for byte against the Core spec's layouts (Vol 2 Part C §5). */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../firmware/src/bt/bt_lmp.c"

static int fails;
static void check(const char *what, int ok)
{
    printf("%-72s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

int main(void)
{
    uint8_t o[BTL_MAX + 8], in[8];
    uint32_t n, tid = 9;
    static const uint8_t ver[6] = {37 << 1, 0x06, 0xFF, 0xFF, 0x01, 0x00};

    n = btl_features_req(o);
    check("features_req: opcode 39, TID 0, 8 feature bytes of 0", n == 9 && o[0] == 78 && !o[1] && !o[8]);
    n = btl_version_req(o);
    check("version_req: VersNr 6, CompId FFFF, SubVersNr 1", n == 6 && !memcmp(o, ver, 6));
    check("host_connection_req: one byte, opcode 51", btl_host_conn_req(o) == 1 && o[0] == 102);
    check("setup_complete: one byte, opcode 49", btl_setup_complete(o) == 1 && o[0] == 98);
    check("detach 0x13: opcode 7 + reason", btl_detach(o, 0x13) == 2 && o[0] == 14 && o[1] == 0x13);

    in[0] = 39 << 1 | 1;                           /* the slave's features_req (TID 1) */
    n = btl_answer(in, 9, "X", o);
    check("features_req (TID 1) -> features_res with TID 1", n == 9 && o[0] == (40 << 1 | 1));
    in[0] = 37 << 1 | 1;
    n = btl_answer(in, 6, "X", o);
    check("version_req -> version_res, TID kept", n == 6 && o[0] == (38 << 1 | 1) && o[1] == 6);
    in[0] = 1 << 1 | 1, in[1] = 0;
    n = btl_answer(in, 2, "OPTIMIST BT", o);
    check("name_req offset 0 -> name_res: offset 0, length 11, the name, zero padded to 14",
          n == 17 && o[0] == (2 << 1 | 1) && o[1] == 0 && o[2] == 11 && !memcmp(o + 3, "OPTIMIST BT", 11) &&
              o[14] == 0 && o[16] == 0);
    in[1] = 14;
    n = btl_answer(in, 2, "A NAME LONGER THAN FOURTEEN", o);
    check("name_req offset 14 -> the second fragment", n == 17 && o[1] == 14 && o[2] == 27 && o[3] == 'T');
    in[0] = 19 << 1 | 1;
    n = btl_answer(in, 5, "X", o);
    check("switch_req -> not_accepted(19, role switch not allowed)",
          n == 3 && o[0] == (4 << 1 | 1) && o[1] == 19 && o[2] == 0x21);
    in[0] = 46 << 1 | 1;
    n = btl_answer(in, 2, "X", o);
    check("max_slot_req -> accepted(46)", n == 2 && o[0] == (3 << 1 | 1) && o[1] == 46);
    in[0] = 47 << 1 | 1;
    n = btl_answer(in, 1, "X", o);
    check("timing_accuracy_req -> res 250 ppm, 10 us", n == 3 && o[0] == (48 << 1 | 1) && o[1] == 250);
    in[0] = 127 << 1 | 1, in[1] = 3, in[2] = 1;
    n = btl_answer(in, 3, "X", o);
    check("features_req_ext page 1 -> features_res_ext page 1, max page 0, no features",
          n == 12 && o[0] == (127 << 1 | 1) && o[1] == 4 && o[2] == 1 && o[3] == 0 && !o[11]);
    in[1] = 23;
    n = btl_answer(in, 2, "X", o);
    check("an unknown extended PDU -> not_accepted_ext(127, ext, 0x19)",
          n == 5 && o[1] == 2 && o[2] == 127 && o[3] == 23 && o[4] == 0x19);
    in[0] = 60 << 1 | 1;
    n = btl_answer(in, 1, "X", o);
    check("an unknown PDU -> not_accepted(op, 0x19)", n == 3 && o[0] == (4 << 1 | 1) && o[1] == 60 && o[2] == 0x19);
    in[0] = 3 << 1, in[1] = 51;
    check("accepted / detach / setup_complete need no answer",
          btl_answer(in, 2, "X", o) == 0 && (in[0] = 7 << 1, btl_answer(in, 2, "X", o) == 0) &&
              (in[0] = 49 << 1 | 1, btl_answer(in, 1, "X", o) == 0));
    check("a short PDU: no opcode", btl_opcode(in, 0, &tid) == 0 && (in[0] = 127 << 1, btl_opcode(in, 1, &tid) == 0));
    check("names: host_connection_req, features_res_ext, ?",
          !strcmp(btl_name(51), "host_connection_req") && !strcmp(btl_name(127 << 8 | 4), "features_res_ext") &&
              !strcmp(btl_name(99), "?"));
    printf(fails ? "bt_lmp_test: %d FAILED\n" : "bt_lmp_test: all ok\n", fails);
    return fails != 0;
}
