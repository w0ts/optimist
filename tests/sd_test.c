/* SPDX-License-Identifier: GPL-3.0-only */
/* The SD card (builder item SDCARD): ChaN's SPI-mode driver (firmware/src/storage/sd/sd_mmc.c) and Petit FatFs (pff.c)
 * against a byte-level card model, on FAT16 and FAT32 images made by a real formatter (tests/fixtures/sd/make_images.sh).
 *   sd_test FAT16.img FAT32.img
 * The model answers like an SPI-mode card: R1 a byte after the command, CMD8 / ACMD41 / CMD58 for SDv2 (block or byte
 * addressed), SDv1 and MMC start-ups, CSD v1 / v2, data tokens, a write's response and busy bytes. It also checks the
 * FM-1's bus rule: every byte clocked while the card is deselected reaches the LCD, so each must be 0xFF. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t clk;                           /* the fake clock: one tick a byte, 1000 a millisecond */

typedef struct {
    uint8_t *img;                              /* the filesystem's sectors */
    uint32_t sectors;                          /* of the image */
    uint32_t base;                             /* where the image starts on the card (>0: an MBR before it) */
    int present, v2, hc, mmc;
    int acmd41_busy;                           /* idle answers before ready (-1: never ready) */
    int no_token;                              /* CMD17 never sends its data token */
    int selected, app, idle;
    uint8_t cmd[6];
    int cmdlen;
    uint8_t out[600];
    int outlen, outpos;
    int wstate;                                /* 0, 1 waiting for 0xFE, 2 data */
    uint32_t waddr;
    uint8_t wbuf[514];
    int wpos;
    long lcd_bytes, lcd_bad;                   /* bytes clocked while deselected; those not 0xFF */
    long writes;
} card_t;
static card_t C;

static void q(uint8_t b) { if (C.outlen < (int)sizeof C.out) C.out[C.outlen++] = b; }

static uint8_t *sector_ptr(uint32_t s, uint8_t *scratch)
{
    if (s >= C.base && s - C.base < C.sectors)
        return C.img + (size_t)(s - C.base) * 512u;
    memset(scratch, 0, 512);
    if (s == 0 && C.base) {                    /* the MBR: one partition at base */
        uint8_t *e = scratch + 446;
        e[4] = C.sectors > 65536u ? 0x0C : 0x06;
        e[8] = (uint8_t)C.base; e[9] = (uint8_t)(C.base >> 8); e[10] = (uint8_t)(C.base >> 16);
        e[12] = (uint8_t)C.sectors; e[13] = (uint8_t)(C.sectors >> 8); e[14] = (uint8_t)(C.sectors >> 16);
        scratch[510] = 0x55; scratch[511] = 0xAA;
    }
    return scratch;
}

static uint32_t card_sectors(void) { return C.base + C.sectors; }

static void csd(uint8_t *c)
{
    memset(c, 0, 16);
    if (C.hc) {                                /* CSD v2: (C_SIZE + 1) x 512 KB */
        uint32_t cs = card_sectors() / 1024u - 1u;
        c[0] = 0x40; c[7] = (uint8_t)(cs >> 16 & 0x3F); c[8] = (uint8_t)(cs >> 8); c[9] = (uint8_t)cs;
    } else {                                   /* CSD v1: READ_BL_LEN 9, C_SIZE_MULT 7: (C_SIZE + 1) x 512 blocks */
        uint32_t cs = card_sectors() / 512u - 1u;
        c[5] = 9; c[6] = (uint8_t)(cs >> 10 & 3); c[7] = (uint8_t)(cs >> 2); c[8] = (uint8_t)(cs << 6);
        c[9] = 3; c[10] = 0x80;
    }
}

static void command(void)
{
    uint32_t idx = C.cmd[0] & 0x3Fu, arg = (uint32_t)C.cmd[1] << 24 | (uint32_t)C.cmd[2] << 16 | (uint32_t)C.cmd[3] << 8 | C.cmd[4];
    uint8_t scratch[512], c16[16];
    int app = C.app, i;
    C.app = 0;
    q(0xFF);                                   /* NCR: one byte before the answer */
    switch (idx) {
    case 0:
        C.idle = 1;
        q(C.cmd[5] == 0x95 ? 0x01 : 0x09);     /* (a bad CRC: the CRC error bit) */
        return;
    case 8:
        if (!C.v2) { q(0x05); return; }        /* illegal command: SDv1 or MMC */
        q(C.idle); q(0); q(0); q((uint8_t)(arg >> 8 & 15)); q((uint8_t)arg);
        return;
    case 55:
        C.app = 1;
        q(C.idle);
        return;
    case 41:
    case 1:
        if ((idx == 41 && (!app || C.mmc)) || (idx == 1 && !C.mmc)) { q(0x05); return; }
        if (C.acmd41_busy < 0) { q(0x01); return; }
        if (C.acmd41_busy > 0) { C.acmd41_busy--; q(0x01); return; }
        C.idle = 0;
        q(0x00);
        return;
    case 58:
        q(C.idle); q((uint8_t)(0x80 | (C.hc ? 0x40 : 0))); q(0xFF); q(0x80); q(0);
        return;
    case 16:
        q(arg == 512 ? 0 : 0x40);
        return;
    case 9:
        q(0); q(0xFF); q(0xFE); csd(c16);
        for (i = 0; i < 16; i++) q(c16[i]);
        q(0); q(0);
        return;
    case 17:
    case 24: {
        uint32_t s;
        if (C.idle) { q(0x05); return; }
        if (!C.hc && (arg & 511u)) { q(0x40); return; }   /* a byte address off a block */
        s = C.hc ? arg : arg / 512u;
        if (s >= card_sectors()) { q(0x40); return; }
        q(0);
        if (idx == 24) { C.wstate = 1; C.waddr = s; return; }
        if (C.no_token) return;
        q(0xFF); q(0xFF); q(0xFE);
        {
            uint8_t *p = sector_ptr(s, scratch);
            for (i = 0; i < 512; i++) q(p[i]);
        }
        q(0x12); q(0x34);
        return;
    }
    default:
        q(0x04);
    }
}

static uint8_t xchg(uint8_t in)
{
    clk++;
    if (!C.present)
        return 0xFF;
    if (!C.selected) {                         /* the LCD gets this byte */
        C.lcd_bytes++;
        if (in != 0xFF) C.lcd_bad++;
        return 0xFF;
    }
    if (C.outpos < C.outlen) {
        uint8_t b = C.out[C.outpos++];
        if (C.outpos == C.outlen) C.outlen = C.outpos = 0;
        return b;
    }
    if (C.wstate == 1) {
        if (in == 0xFE) { C.wstate = 2; C.wpos = 0; }
        return 0xFF;
    }
    if (C.wstate == 2) {
        C.wbuf[C.wpos++] = in;
        if (C.wpos == 514) {
            uint8_t scratch[512];
            uint8_t *p = sector_ptr(C.waddr, scratch);
            memcpy(p, C.wbuf, 512);
            C.writes++;
            C.wstate = 0; C.outlen = C.outpos = 0;
            q(0xE5); q(0); q(0); q(0);         /* data accepted, then busy */
        }
        return 0xFF;
    }
    if (C.cmdlen == 0 && (in & 0xC0) != 0x40)
        return 0xFF;
    C.cmd[C.cmdlen++] = in;
    if (C.cmdlen == 6) { C.cmdlen = 0; command(); }
    return 0xFF;
}

#define SD_TX(b) ((void)xchg((uint8_t)(b)))
#define SD_RX() xchg(0xFF)
#define SD_SEL() (C.selected = 1)
#define SD_DESEL() (C.selected = 0, C.cmdlen = 0, C.outlen = C.outpos = 0)
#define SD_FAST() ((void)0)
#define SD_NOW() clk
#define SD_TICKS_MS 1000u
#include "storage/sd/sd_mmc.c"
#include "storage/sd/pff.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint8_t *load(const char *path, uint32_t *sectors)
{
    FILE *f = fopen(path, "rb");
    long n;
    uint8_t *p;
    if (!f) { printf("cannot open %s\n", path); exit(2); }
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    p = malloc((size_t)n);
    if (!p || fread(p, 1, (size_t)n, f) != (size_t)n) { printf("cannot read %s\n", path); exit(2); }
    fclose(f);
    *sectors = (uint32_t)(n / 512);
    return p;
}

static void card(uint8_t *img, uint32_t sectors, uint32_t base, int v2, int hc, int mmc)
{
    memset(&C, 0, sizeof C);
    C.img = img; C.sectors = sectors; C.base = base;
    C.present = 1; C.v2 = v2; C.hc = hc; C.mmc = mmc;
    C.acmd41_busy = 20;
}

static uint8_t kick(uint32_t i) { return (uint8_t)((i * 7 + (i >> 8)) & 255); }

static void files(const char *what, BYTE fat)
{
    FATFS fs;
    DIR d;
    FILINFO fi;
    UINT got, put;
    uint8_t buf[4096], pat[4096];
    int seen_hello = 0, seen_data = 0, seen_samples = 0, seen_long = 0;
    uint32_t i;
    long lcd0;

    CHECK(pf_mount(&fs) == FR_OK, "%s: mount", what);
    CHECK(fs.fs_type == fat, "%s: FAT type %u, want %u", what, fs.fs_type, fat);
    CHECK(sd_sectors == card_sectors(), "%s: capacity %lu sectors, want %lu", what, (unsigned long)sd_sectors,
          (unsigned long)card_sectors());

    CHECK(pf_open("HELLO.TXT") == FR_OK, "%s: open HELLO.TXT", what);
    memset(buf, 0, sizeof buf);
    CHECK(pf_read(buf, 100, &got) == FR_OK && got == 28, "%s: HELLO.TXT %u bytes", what, got);
    CHECK(!memcmp(buf, "Hello from the FM-1 SD card\n", 28), "%s: HELLO.TXT contents", what);

    CHECK(pf_open("SAMPLES/KICK.RAW") == FR_OK, "%s: open SAMPLES/KICK.RAW", what);
    CHECK(pf_read(buf, sizeof buf, &got) == FR_OK && got == 3000, "%s: KICK.RAW %u bytes", what, got);
    for (i = 0; i < 3000 && buf[i] == kick(i); i++)
        ;
    CHECK(i == 3000, "%s: KICK.RAW differs at %u", what, (unsigned)i);
    CHECK(pf_lseek(2500) == FR_OK && pf_read(buf, 16, &got) == FR_OK && got == 16 && buf[0] == kick(2500)
          && buf[15] == kick(2515), "%s: seek 2500 then read", what);

    CHECK(pf_opendir(&d, "") == FR_OK, "%s: open the root", what);
    while (pf_readdir(&d, &fi) == FR_OK && fi.fname[0]) {
        seen_hello |= !strcmp(fi.fname, "HELLO.TXT") && fi.fsize == 28;
        seen_data |= !strcmp(fi.fname, "DATA.BIN") && fi.fsize == 4096;
        seen_samples |= !strcmp(fi.fname, "SAMPLES") && (fi.fattrib & AM_DIR);
        seen_long |= !strncmp(fi.fname, "LONGFI", 6);
    }
    CHECK(seen_hello && seen_data && seen_samples && seen_long, "%s: root listing (hello %d data %d samples %d long %d)",
          what, seen_hello, seen_data, seen_samples, seen_long);

    for (i = 0; i < sizeof pat; i++) pat[i] = (uint8_t)(i * 13 + 5);
    CHECK(pf_open("DATA.BIN") == FR_OK, "%s: open DATA.BIN", what);
    CHECK(pf_write(pat, sizeof pat, &put) == FR_OK && put == sizeof pat, "%s: write %u", what, put);
    CHECK(pf_write(0, 0, &put) == FR_OK, "%s: write finalized", what);
    CHECK(C.writes == 8, "%s: %ld sectors written, want 8", what, C.writes);
    CHECK(pf_open("DATA.BIN") == FR_OK && pf_read(buf, sizeof buf, &got) == FR_OK && got == sizeof pat
          && !memcmp(buf, pat, sizeof pat), "%s: DATA.BIN read back", what);
    CHECK(pf_write(pat, 16, &put) == FR_OK && put == 0, "%s: no write past the file's end (%u)", what, put);

    CHECK(pf_open("NOPE.TXT") == FR_NO_FILE, "%s: a missing file", what);
    lcd0 = C.lcd_bytes;
    CHECK(C.lcd_bad == 0, "%s: %ld bytes not 0xFF reached the LCD", what, C.lcd_bad);
    CHECK(lcd0 > 0, "%s: the deselect clocks went to the LCD side", what);
}

int main(int argc, char **argv)
{
    uint32_t n16, n32;
    uint8_t *i16, *i32, *copy;
    FATFS fs;
    uint32_t t0;

    if (argc < 3) { printf("usage: sd_test FAT16.img FAT32.img\n"); return 2; }
    i16 = load(argv[1], &n16);
    i32 = load(argv[2], &n32);
    copy = malloc((size_t)n32 * 512u);

    memcpy(copy, i32, (size_t)n32 * 512u);
    card(copy, n32, 0, 1, 1, 0);               /* SDHC, FAT32 on the whole card (no MBR) */
    files("SDHC FAT32", FS_FAT32);
    CHECK(CardType == (CT_SD2 | CT_BLOCK), "SDHC: card type %u", CardType);

    memcpy(copy, i32, (size_t)n32 * 512u);
    card(copy, n32, 2048, 1, 1, 0);            /* SDHC, FAT32 in a partition at 2048 */
    files("SDHC FAT32 + MBR", FS_FAT32);

    memcpy(copy, i16, (size_t)n16 * 512u);
    card(copy, n16, 2048, 1, 0, 0);            /* SDv2 byte-addressed (SDSC), FAT16 in a partition */
    files("SDSC v2 FAT16 + MBR", FS_FAT16);
    CHECK(CardType == CT_SD2, "SDSC v2: card type %u", CardType);

    memcpy(copy, i16, (size_t)n16 * 512u);
    card(copy, n16, 0, 0, 0, 0);               /* SDv1 */
    files("SDv1 FAT16", FS_FAT16);
    CHECK(CardType == CT_SD1, "SDv1: card type %u", CardType);

    memcpy(copy, i16, (size_t)n16 * 512u);
    card(copy, n16, 0, 0, 0, 1);               /* MMCv3 */
    files("MMC FAT16", FS_FAT16);
    CHECK(CardType == CT_MMC, "MMC: card type %u", CardType);

    card(copy, n16, 0, 1, 1, 0);
    C.present = 0;                             /* no card: every byte 0xFF */
    t0 = clk;
    CHECK(pf_mount(&fs) == FR_NOT_READY, "no card: not ready");
    CHECK(clk - t0 < 1000u * 50u, "no card: gave up in %u ms", (unsigned)((clk - t0) / 1000u));

    card(copy, n16, 0, 1, 1, 0);
    C.acmd41_busy = -1;                        /* never leaves idle: the 1 s start-up limit */
    t0 = clk;
    CHECK(pf_mount(&fs) == FR_NOT_READY, "stuck card: not ready");
    CHECK(clk - t0 >= 1000u * 1000u && clk - t0 < 1000u * 1100u, "stuck card: gave up after %u ms",
          (unsigned)((clk - t0) / 1000u));

    memset(copy, 0, (size_t)n16 * 512u);
    card(copy, n16, 0, 1, 1, 0);               /* a card without a filesystem */
    CHECK(pf_mount(&fs) == FR_NO_FILESYSTEM, "blank card: no filesystem");

    memcpy(copy, i16, (size_t)n16 * 512u);
    card(copy, n16, 0, 1, 1, 0);
    CHECK(pf_mount(&fs) == FR_OK, "token test: mount");
    C.no_token = 1;                            /* a read whose data never comes: the 100 ms limit */
    t0 = clk;
    CHECK(disk_readp(copy, 0, 0, 16) == RES_ERROR, "no data token: error");
    CHECK(clk - t0 >= 1000u * 100u && clk - t0 < 1000u * 110u, "no data token: gave up after %u ms",
          (unsigned)((clk - t0) / 1000u));

    printf("sd_test: %d checks, %d failed\n", checks, fails);
    free(copy); free(i16); free(i32);
    return fails ? 1 : 0;
}
