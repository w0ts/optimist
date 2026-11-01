/* SPDX-License-Identifier: GPL-3.0-only */
/* system/bootid.h and INFO's tag 0x68 (io/editor/ed_opti.h) on fake boot areas: M-VAVE's bootloader's container head at
 * 0xA0, m-bootloader's (FM-1 and SMK-37) at 0xA0 with its identity at payload +0x40, an erased area, a read that fails.
 * The areas are built here from the plain bytes with JieLi's ENC (m-bootloader SPEC 5.1), so bootid.h's constants are
 * checked against the format, not against themselves (tests/anyflash_test.py checks them against V15's flash.bin and
 * m-bootloader 1.2's release). Run from the repo root:
 *   cc -Ifirmware/src -o build/bootid_test tests/bootid_test.c && build/bootid_test */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint8_t area[0x4000];
static int fail_reads, reads;

static int fake_read(uint32_t off, void *dst, uint32_t n)
{
    reads++;
    if (fail_reads || off + n > sizeof area)
        return -1;
    memcpy(dst, area + off, n);
    return 0;
}

static uint8_t out[700];
static uint32_t nout;
static void ed_b(uint32_t v) { if (nout < sizeof out) out[nout++] = (uint8_t)v; }

#define ED_BOOT_READ fake_read
#include "io/editor/ed_opti.h"

static void enc_ffff(uint8_t *p, uint32_t n)       /* ENC from 0xFFFF: one state step a byte */
{
    uint32_t s = 0xFFFFu, i;
    for (i = 0; i < n; i++) {
        p[i] ^= (uint8_t)s;
        s = ((s << 1) & 0xFFFFu) ^ ((s & 0x8000u) ? 0x1021u : 0u);
    }
}

/* a boot file at 0xA0: the 16-byte header and the payload each ciphered from their own start */
static void make_area(const uint8_t head[16], const char *ident, const char *board)
{
    uint8_t h[16], pay[0x100];
    uint32_t i;
    memset(area, 0xFF, sizeof area);
    memcpy(h, head, 16);
    for (i = 0; i < sizeof pay; i++)
        pay[i] = (uint8_t)(i * 37u + 11u);
    if (ident) {
        memcpy(pay + 0x40, ident, 8);
        memset(pay + 0x48, 0, 8);
        memcpy(pay + 0x48, board, strlen(board));
    }
    enc_ffff(h, 16);
    enc_ffff(pay, sizeof pay);
    memcpy(area + 0xA0, h, 16);
    memcpy(area + 0xB0, pay, sizeof pay);
}

static int bad;
static void check(int ok, const char *what)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    bad += !ok;
}

static int info_says(const char *word)              /* INFO's tags end with 0x68, its length, then the word */
{
    uint32_t n = (uint32_t)strlen(word);
    nout = 0;
    ed_opti_info();
    return nout >= n + 2u && out[nout - n - 2u] == 0x68u && out[nout - n - 1u] == n && !memcmp(out + nout - n, word, n);
}

int main(void)
{
    static const uint8_t STOCK[16] = {0x01, 0x00, 0x20, 0x38, 0x00, 0x20, 0xC0, 0x01,
                                      0x10, 0x00, 0x00, 0x00, 0xA2, 0x81, 0xF5, 0xBC};   /* SPEC 5.3 */
    static const uint8_t MBOOT[16] = {0x01, 0x00, 0x78, 0x32, 0x00, 0x20, 0xC0, 0x01,
                                      0x10, 0x00, 0x00, 0x00, 0x68, 0x35, 0xB0, 0x51};   /* m-bootloader 1.2's */
    make_area(STOCK, 0, 0);
    check(bootid_read(fake_read) == BOOTID_STOCK, "M-VAVE's bootloader: stock");
    make_area(MBOOT, "AC79BLID", "fm1");
    check(bootid_read(fake_read) == BOOTID_MBOOT, "m-bootloader for the FM-1: mboot");
    make_area(MBOOT, "AC79BLID", "smk37");
    check(bootid_read(fake_read) == BOOTID_UNKNOWN, "m-bootloader for the SMK-37: unknown");
    make_area(STOCK, "AC79BLID", "fm1");
    check(bootid_read(fake_read) == BOOTID_MBOOT, "the identity decides over the head");
    make_area(MBOOT, 0, 0);
    check(bootid_read(fake_read) == BOOTID_UNKNOWN, "another bootloader without the identity: unknown");
    memset(area, 0xFF, sizeof area);
    check(bootid_read(fake_read) == BOOTID_UNKNOWN, "an erased boot area: unknown");
    fail_reads = 1;
    reads = 0;
    check(bootid_read(fake_read) == BOOTID_UNKNOWN && info_says("boot=unknown"), "a read that fails: unknown");
    fail_reads = 0;
    make_area(MBOOT, "AC79BLID", "fm1");
    reads = 0;
    check(info_says("boot=mboot") && reads == 2, "INFO tag 0x68: boot=mboot (an unknown answer is read again)");
    make_area(STOCK, 0, 0);
    reads = 0;
    check(info_says("boot=mboot") && reads == 0, "... then kept: read once (stock is cached the same way)");
    printf(bad ? "bootid: %d FAILED\n" : "bootid: all passed\n", bad);
    return bad != 0;
}
