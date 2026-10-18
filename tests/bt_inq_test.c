/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of the Classic inquiry probe's portable part (firmware/src/bt/bt_inq_core.c):
 *   the sync word's 34 parity bits for the GIAC (the value docs/BT-CLASSIC-HW-FACTS.md §5.3 lists) and for stock
 *   V15's own LAP (its page-scan frame in the emulator's RAM, §11), and for LAPs with bit 23 set and clear (the two
 *   Barker words) against an independent bit-list computation of the spec's construction;
 *   stock V15's FHS and EIR as the emulator's RAM held them (§8.2, §8.4): address, class, SR, EIR bit, the name. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../firmware/src/bt/bt_inq_core.c"

static int fails;
static void check(const char *what, int ok)
{
    printf("%-72s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

/* the spec's construction, bit by bit with 64-bit host arithmetic (independent of the firmware's word pair) */
static uint64_t ref_parity(uint32_t lap)
{
    const uint64_t pn = 0x83848D96BBCC54FCull, g = 0x585713DA9ull;
    uint64_t v = 0;
    int a[30], i;
    static const int b0[6] = {0, 0, 1, 1, 0, 1}, b1[6] = {1, 1, 0, 0, 1, 0};
    for (i = 0; i < 24; i++)
        a[i] = (lap >> i) & 1;
    for (i = 0; i < 6; i++)
        a[24 + i] = a[23] ? b1[i] : b0[i];
    for (i = 0; i < 30; i++)
        v |= (uint64_t)(a[i] ^ (int)((pn >> (34 + i)) & 1)) << (34 + i);
    for (i = 63; i >= 34; i--)
        if ((v >> i) & 1)
            v ^= g << (i - 34);
    return (v ^ pn) & 0x3FFFFFFFFull;
}

static uint64_t got_parity(uint32_t lap)
{
    uint8_t b[5];
    bti_parity(lap, b);
    return b[0] | (uint64_t)b[1] << 8 | (uint64_t)b[2] << 16 | (uint64_t)b[3] << 24 | (uint64_t)b[4] << 32;
}

int main(void)
{
    static const uint8_t giac[5] = {0xE2, 0x3A, 0x1A, 0x33, 0x02}, v15[5] = {0x08, 0xD9, 0x39, 0x52, 0x02};
    /* stock V15 in the emulator (BT-CLASSIC-HW-FACTS §8.4, §8.2): its FHS and the start of its EIR */
    static const uint8_t fhs[18] = {0x08, 0xD9, 0x39, 0x52, 0x32, 0x65, 0x76, 0x94, 0xF4, 0x06, 0xD9,
                                    0x04, 0x04, 0x24, 0x00, 0x00, 0x00, 0x00};
    static const uint8_t eir[25] = {0x06, 0x09, 'F', 'M', '-', '1', 0x00, 0x11, 0xFF, 'b', 'r', '2', '2',
                                    'x', 'x', 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0xAA, 0xBB, 0x00, 0x00};
    static const uint8_t two[] = {0x05, 0x08, 'S', 'H', 'R', 'T', 0x06, 0x09, 'L', 'O', 'N', 'G', 'X'};
    uint8_t b[5];
    struct bti_fhs f;
    char name[32];
    uint32_t lap, ok = 1;

    bti_parity(BTI_GIAC, b);
    check("sync word parity of the GIAC = E2 3A 1A 33 02 (the frame's bch, HW §5.3)", !memcmp(b, giac, 5));
    bti_parity(0x1D994Cu, b);
    check("sync word parity of stock V15's LAP 1D994C = 08 D9 39 52 02 (its page-scan frame)", !memcmp(b, v15, 5));
    for (lap = 0; lap < 0x1000000u; lap += 0x10001u)
        ok &= got_parity(lap) == ref_parity(lap);
    ok &= got_parity(0xFFFFFFu) == ref_parity(0xFFFFFFu) && got_parity(0x800000u) == ref_parity(0x800000u);
    check("parity = the spec's bit-list construction for 257 LAPs (both Barker words)", (int)ok);

    bti_fhs_parse(fhs, &f);
    check("FHS: V15's address D9:06:F4:1D:99:4C (LAP 1D994C, UAP F4, NAP D906)",
          f.addr[0] == 0x4C && f.addr[1] == 0x99 && f.addr[2] == 0x1D && f.addr[3] == 0xF4 && f.addr[4] == 0x06 &&
              f.addr[5] == 0xD9);
    check("FHS: its parity = the parity of its LAP", f.parity_lo == 0x5239D908u && f.parity_hi == 2);
    check("FHS: class 0x240404, EIR 1, SR R1, reserved 10b, LT_ADDR 0, clock field 0, page scan mode 0",
          f.cod == 0x240404u && f.eir == 1 && f.sr == 1 && f.reserved == 2 && f.lt_addr == 0 && f.clk == 0 &&
              f.psm == 0);
    {
        uint8_t again[18];
        struct bti_fhs g = f;
        g.parity_lo = 0, g.parity_hi = 0, g.reserved = 0;   /* (computed by the builder) */
        memset(again, 0xA5, sizeof again);
        bti_fhs_build(&g, again);
        check("FHS built from V15's parsed fields = V15's 18 bytes (parity computed, reserved 10b)",
              !memcmp(again, fhs, 18));
        g.clk = 0x3FFFFFF, g.lt_addr = 7, g.psm = 5, g.cod = 0xABCDEF, g.sr = 2, g.eir = 0;
        bti_fhs_build(&g, again);
        bti_fhs_parse(again, &f);
        check("FHS: build then parse keeps every field (all-ones clock, LT_ADDR 7, PSM 5)",
              f.clk == 0x3FFFFFF && f.lt_addr == 7 && f.psm == 5 && f.cod == 0xABCDEF && f.sr == 2 && f.eir == 0 &&
                  f.reserved == 2 && !memcmp(f.addr, g.addr, 6));
    }
    check("EIR: V15's name \"FM-1\" (complete local name, its trailing 0 dropped)",
          bti_eir_name(eir, sizeof eir, name, sizeof name) == 4 && !strcmp(name, "FM-1"));
    check("EIR: the complete name wins over the shortened one",
          bti_eir_name(two, sizeof two, name, sizeof name) == 5 && !strcmp(name, "LONGX"));
    check("EIR: none in an empty response", bti_eir_name(eir + 23, 2, name, sizeof name) == 0);
    check("EIR: a field running past the data ends the walk", bti_eir_name(two, 4, name, sizeof name) == 0);
    check("EIR: a short buffer truncates the name", bti_eir_name(eir, sizeof eir, name, 3) == 4 && !strcmp(name, "FM"));
    printf(fails ? "bt_inq_test: %d FAILED\n" : "bt_inq_test: all ok\n", fails);
    return fails != 0;
}
