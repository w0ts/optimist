/* SPDX-License-Identifier: GPL-3.0-only */
/* Which bootloader the FM-1's boot area holds, for the installers (INFO tag 0x68 "boot=mboot|stock|unknown",
 * web/EDITOR_PROTOCOL.md; docs/INSTALL-PATHS.md "Asking the firmware"). Read only, 32 bytes, as stored (no XIP):
 *   0xA0  the boot file's 16-byte container header, ENC 0xFFFF from its own start: M-VAVE's bootloader (byte-identical
 *         in the stock FM-1 and SMK-37 images, Felucca's, SLOOP's and Baud Girl's: m-bootloader SPEC 5.3) has the
 *         plain header 01 00 20 38 00 20 C0 01 10 00 00 00 A2 81 F5 BC (its payload's length and CRC 0x81A2 in it)
 *   0xF0  the container's payload +0x40, ENC 0xFFFF from the payload's start (0xB0): m-bootloader's identity
 *         "AC79BLID" and its board "fm1" + NULs (SPEC 8.1; what M-Flasher's bootloader_identity reads)
 * Both compared as stored: ENC is a fixed keystream for a fixed start, so the constants below are the plain bytes
 * ciphered (tests/bootid_test.c rebuilds them from the plain bytes; tests/install_paths... checks them against V15's
 * flash.bin and m-bootloader 1.2's container). Another board's m-bootloader, a damaged area or a read that fails:
 * unknown (the installers then choose as without the token) */
#ifndef BOOTID_H
#define BOOTID_H
#include <stdint.h>

#define BOOTID_HEAD_AT 0xA0u
#define BOOTID_TAG_AT 0xF0u
enum { BOOTID_UNKNOWN, BOOTID_STOCK, BOOTID_MBOOT };

static const uint8_t BOOTID_STOCK_HEAD[16] = {0xfe, 0xdf, 0xbf, 0x27, 0x1f, 0x1e, 0xbc, 0xf9,
                                              0xe0, 0xc1, 0xa3, 0x67, 0x6c, 0x3c, 0xae, 0x2b};
static const uint8_t BOOTID_MBOOT_TAG[16] = {0x7f, 0x3f, 0xcf, 0xe8, 0xc1, 0x4a, 0x45, 0x7d,
                                             0x14, 0x89, 0xf9, 0x90, 0x01, 0x23, 0x46, 0x8c};

static int bootid_same(const uint8_t *a, const uint8_t *b)
{
    uint32_t i, d = 0;
    for (i = 0; i < 16u; i++)
        d |= (uint32_t)(a[i] ^ b[i]);
    return !d;
}

/* rd: a flash read that returns 0 when done (felucca.c st_read; fm1-base: plat_boot_read) -> BOOTID_* */
static uint32_t bootid_read(int (*rd)(uint32_t off, void *dst, uint32_t n))
{
    uint8_t h[16], t[16];
    if (rd(BOOTID_HEAD_AT, h, 16) || rd(BOOTID_TAG_AT, t, 16))
        return BOOTID_UNKNOWN;
    if (bootid_same(t, BOOTID_MBOOT_TAG))
        return BOOTID_MBOOT;
    return bootid_same(h, BOOTID_STOCK_HEAD) ? BOOTID_STOCK : BOOTID_UNKNOWN;
}

static const char *bootid_word(uint32_t id)    /* INFO tag 0x68's text */
{
    return id == BOOTID_MBOOT ? "boot=mboot" : id == BOOTID_STOCK ? "boot=stock" : "boot=unknown";
}
#endif
