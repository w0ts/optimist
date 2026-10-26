/* SPDX-License-Identifier: GPL-3.0-only */
/* The SD card (builder item SDCARD): ChaN's SPI-mode driver (firmware/src/storage/sd/sd_mmc.c) and Petit FatFs (pff.c,
 * the bring-up's filesystem) against the byte-level card model (tests/sd_card_model.h), on FAT16 and FAT32 images made
 * by a real formatter (tests/fixtures/sd/make_images.sh). Full FatFs on the same model: tests/sd_fs_test.c.
 *   sd_test FAT16.img FAT32.img */
#include "sd_card_model.h"
#include "storage/sd/sd_mmc.c"
#include "storage/sd/pff.h"

static uint8_t kick(uint32_t i) { return (uint8_t)((i * 7 + (i >> 8)) & 255); }

static void files(const char *what, BYTE fat)
{
    FATFS fs;
    DIR d;
    FILINFO fi;
    UINT got, put;
    static uint8_t buf[4096] __attribute__((aligned(4))), pat[4096];
    DWORD sec;
    UINT n, off;
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

    CHECK(pf_open("SAMPLES/KICK.RAW") == FR_OK, "%s: open KICK.RAW for its runs", what);
    memset(buf, 0, sizeof buf);
    for (off = 0; off < sizeof buf && (n = pf_run(&sec)) != 0; off += n * 512u) {
        CHECK(n >= 1 && n <= fs.csize && off + n * 512u <= sizeof buf, "%s: run of %u at %u", what, n, off);
        CHECK(disk_readblocks(buf + off, sec, n) == RES_OK, "%s: the run at %u read", what, off);
        CHECK(pf_lseek(off + n * 512u) == FR_OK, "%s: seek past the run", what);
    }
    for (i = 0; i < 3000 && buf[i] == kick(i); i++)
        ;
    CHECK(off == 3072 && i == 3000, "%s: KICK.RAW by its runs: %u B, differs at %u", what, off, (unsigned)i);
    CHECK(pf_lseek(100) == FR_OK && pf_run(&sec) == 0, "%s: no run off a sector boundary", what);

    CHECK(pf_open("NOPE.TXT") == FR_NO_FILE, "%s: a missing file", what);
    lcd0 = C.lcd_bytes;
    CHECK(C.lcd_bad == 0, "%s: %ld bytes not 0xFF reached the LCD", what, C.lcd_bad);
    CHECK(lcd0 > 0, "%s: the deselect clocks went to the LCD side", what);
}


/* whole blocks: CMD18 + CMD12 and CMD17 through the polled reader, by bytes and by DMA; CRC errors, the card's end, no
 * token, block writes */
static void blocks(const char *what)
{
    static uint8_t buf[48 * 512] __attribute__((aligned(4))), pat[512] __attribute__((aligned(4)));
    uint8_t scratch[512], one[4];
    uint32_t s0 = C.base + 3, n = 40, i, t0;          /* (inside the image: writes land there) */
    long c18, c12, c17, w0;
    int d, busy, tok_busy, st;

    for (d = 0; d < 2; d++) {
        dma_on = d;
        memset(&D, 0, sizeof D);
        c18 = C.cmd18s; c12 = C.cmd12s; c17 = C.cmd17s;
        memset(buf, 0xA5, sizeof buf);
        CHECK(disk_readblocks(buf, s0, n) == RES_OK, "%s dma %d: %u blocks by CMD18", what, d, n);
        CHECK(same_as_card(buf, s0, n), "%s dma %d: the blocks' data", what, d);
        CHECK(C.cmd18s == c18 + 1 && C.cmd12s == c12 + 1 && C.cmd17s == c17, "%s dma %d: one CMD18, one CMD12", what, d);
        CHECK(!C.rs, "%s dma %d: the stream stopped", what, d);
        CHECK(D.ops == (d ? (long)n : 0) && D.clash == 0 && D.misaligned == 0,
              "%s dma %d: %ld DMAs, %ld CPU bytes during one, %ld misaligned", what, d, D.ops, D.clash, D.misaligned);
        CHECK(sd_readp(one, 0, 510, 2) == RES_OK && !memcmp(one, sector_ptr(0, scratch) + 510, 2),
              "%s dma %d: the card answers after CMD12", what, d);
        CHECK(disk_readblocks(buf, s0 + 5, 1) == RES_OK && same_as_card(buf, s0 + 5, 1) && C.cmd17s == c17 + 2,
              "%s dma %d: one block by CMD17", what, d);
        CHECK(sd_readp(buf, s0 + 7, 0, 512) == RES_OK && same_as_card(buf, s0 + 7, 1),
              "%s dma %d: a whole sector by sd_readp", what, d);
        CHECK(C.lcd_bad == 0, "%s dma %d: %ld bytes not 0xFF reached the LCD", what, d, C.lcd_bad);
    }

    dma_on = 1;                                /* polled: the reader hands back while the token or the DMA is due */
    memset(&D, 0, sizeof D);
    C.nac_long = 1;
    busy = tok_busy = 0;
    CHECK(sd_rd_open(s0, 12) == RES_OK, "%s: open 12", what);
    for (i = 0; i < 12; i++) {
        sd_rd_next(buf + i * 512);
        while ((st = sd_rd_poll()) == SDR_BUSY) {
            busy++;
            tok_busy += sd_rd.state == SDR_TOKEN;
        }
        CHECK(st == SDR_READY && sd_rd.crc_ok, "%s: polled block %u", what, (unsigned)i);
    }
    CHECK(sd_rd_close() == RES_OK && same_as_card(buf, s0, 12), "%s: polled blocks' data", what);
    CHECK(tok_busy > 0 && busy - tok_busy >= 12 && D.busy_polls >= 12 && D.clash == 0,
          "%s: busy polls %d (token %d, DMA %ld), clashes %ld", what, busy, tok_busy, D.busy_polls, D.clash);
    C.nac_long = 0;

    for (d = 0; d < 2; d++) {
        dma_on = d;
        C.corrupt = s0 + 2;                    /* a block whose CRC does not match: an error, the stream stopped */
        CHECK(disk_readblocks(buf, s0, 5) == RES_ERROR && !C.rs, "%s dma %d: a CRC error", what, d);
        CHECK(sd_readp(one, s0 + 2, 100, 4) == RES_ERROR && sd_readp(buf, s0 + 2, 0, 512) == RES_ERROR,
              "%s dma %d: sd_readp checks the block's CRC too (a part, a whole sector)", what, d);
        C.corrupt = 0xFFFFFFFFu;
        CHECK(sd_readp(one, s0 + 2, 100, 4) == RES_OK && !memcmp(one, sector_ptr(s0 + 2, scratch) + 100, 4),
              "%s dma %d: sd_readp of a part, CRC right", what, d);
        CHECK(disk_readblocks(buf, s0, 5) == RES_OK && same_as_card(buf, s0, 5), "%s dma %d: read again", what, d);
        CHECK(disk_readblocks(buf, card_sectors() - 2, 4) == RES_ERROR, "%s dma %d: past the card's end", what, d);
        CHECK(disk_readblocks(buf, card_sectors() - 2, 2) == RES_OK && same_as_card(buf, card_sectors() - 2, 2),
              "%s dma %d: the card's last two blocks", what, d);
        CHECK(disk_readblocks(buf, card_sectors(), 2) == RES_ERROR, "%s dma %d: CMD18 at the end refused", what, d);
        C.no_token = 1;
        t0 = clk;
        CHECK(disk_readblocks(buf, s0, 3) == RES_ERROR, "%s dma %d: no data token", what, d);
        CHECK(clk - t0 >= 1000u * 100u && clk - t0 < 1000u * 120u, "%s dma %d: gave up after %u ms", what, d,
              (unsigned)((clk - t0) / 1000u));
        C.no_token = 0;
        CHECK(disk_readblocks(buf, s0, 2) == RES_OK, "%s dma %d: reads after the timeout", what, d);

        memset(&D, 0, sizeof D);
        for (i = 0; i < 512; i++) pat[i] = (uint8_t)(i * 3 + d);
        w0 = C.writes;
        CHECK(disk_writeblock(pat, s0 + 9) == RES_OK && C.writes == w0 + 1, "%s dma %d: a block written", what, d);
        CHECK(!memcmp(sector_ptr(s0 + 9, scratch), pat, 512), "%s dma %d: the block's data on the card", what, d);
        CHECK(D.ops == d && D.clash == 0, "%s dma %d: write DMAs %ld, clashes %ld", what, d, D.ops, D.clash);
        CHECK(disk_readblocks(buf, s0 + 9, 1) == RES_OK && !memcmp(buf, pat, 512), "%s dma %d: read back", what, d);
    }
    CHECK(C.lcd_bad == 0, "%s: %ld bytes not 0xFF reached the LCD", what, C.lcd_bad);
    CHECK(C.crc_on && sd_crc_on, "%s: CMD59: the card checks the CRCs", what);
    CHECK(C.crc_refused == 0, "%s: %ld commands or blocks refused for their CRC (each CRC7 and CRC16 sent right)", what,
          C.crc_refused);
    {   /* a command with a wrong CRC7 (a garbled one, or a stray CMD12 on DI during a CMD18 run): refused, not executed */
        static const uint8_t bad[6] = {0x40 | 12, 0, 0, 0, 0, 0x01};
        long r0 = C.crc_refused;
        C.rs = 1;
        memcpy(C.cmd, bad, 6);
        C.outlen = C.outpos = 0;
        command();
        CHECK(C.crc_refused == r0 + 1 && C.rs == 1, "%s: a CMD12 with a wrong CRC7: refused, the stream goes on", what);
        C.rs = 0;
        C.outlen = C.outpos = 0;
    }
    dma_on = 0;
}

/* the clock the card proves (sd_clock_probe) and the drop to the floor on CRC failures (sd_crc_retry) */
static void clocks(const char *what)
{
    static uint8_t buf[8 * 512] __attribute__((aligned(4)));
    uint8_t one[4], scratch[512];
    uint32_t s0 = C.base + 3, n = card_sectors() < 64u ? card_sectors() : 64u;
    UINT bad;
    long d0, r0 = C.crc_refused;
    int sd2 = C.v2 && !C.mmc;

    C.bad30 = 0;
    bad = sd_clock_probe(buf, n, 0);
    if (sd2) {
        CHECK(bad == 0 && sd_over && t_over && C.hs, "%s: a card that reads right at 30 MHz: kept there (CMD6 switched)",
              what);
    } else {
        CHECK(sd_over == 0 && !t_over && bad == n, "%s: SDv1 / MMC: the floor, 20 MHz, nothing tried", what);
        return;
    }
    C.bad30 = 1;
    bad = sd_clock_probe(buf, n, 0);
    CHECK(bad == n && !sd_over && !t_over, "%s: a card that fails at 30 MHz: %u of %u blocks bad, back to 20", what,
          (unsigned)bad, (unsigned)n);
    CHECK(sd_clock_probe(buf, n, 20) == n && !sd_over && !t_over, "%s: sd clock 20: the floor, nothing tried", what);
    CHECK(sd_clock_probe(buf, n, 30) == n && sd_over && t_over, "%s: sd clock 30: forced", what);

    d0 = sd_demotions;                         /* at 30, the card starts failing: the read goes on at 20 */
    CHECK(disk_readblocks(buf, s0, 8) == RES_OK && same_as_card(buf, s0, 8), "%s: CRC failures at 30: read right", what);
    CHECK(sd_demotions == d0 + 1 && !sd_over && !t_over, "%s: ... the session dropped to 20 MHz once", what);
    SD_SET_OVER(1), sd_over = 1;
    CHECK(sd_readp(one, s0 + 1, 40, 4) == RES_OK && !memcmp(one, sector_ptr(s0 + 1, scratch) + 40, 4) &&
          sd_demotions == d0 + 2 && !sd_over, "%s: sd_readp at 30 failing: read at 20, dropped", what);
    C.bad30 = 0;

    d0 = sd_demotions;                         /* at 20, a glitch: read again, no drop, no error */
    C.corrupt_once = s0 + 4;
    CHECK(disk_readblocks(buf, s0, 8) == RES_OK && same_as_card(buf, s0, 8) && sd_demotions == d0,
          "%s: one CRC glitch at 20: the block read again, right", what);
    C.corrupt_once = s0 + 2;
    CHECK(sd_readp(buf, s0 + 2, 0, 512) == RES_OK && same_as_card(buf, s0 + 2, 1), "%s: ... sd_readp too", what);
    C.corrupt = s0 + 4;                        /* at 20, a block that keeps failing: an error */
    CHECK(disk_readblocks(buf, s0, 8) == RES_ERROR, "%s: a block failing twice at 20: an error", what);
    C.corrupt = 0xFFFFFFFFu;
    CHECK(C.crc_refused == r0, "%s: no command refused for its CRC through the probes", what);
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

    memcpy(copy, i32, (size_t)n32 * 512u);     /* the same with the sectors by DMA (pf_read's whole sectors) */
    card(copy, n32, 2048, 1, 1, 0);
    dma_on = 1;
    memset(&D, 0, sizeof D);
    files("SDHC FAT32 + MBR, DMA", FS_FAT32);
    CHECK(D.ops > 0 && D.clash == 0 && D.misaligned == 0, "DMA files: %ld DMAs, %ld clashes, %ld misaligned", D.ops,
          D.clash, D.misaligned);
    dma_on = 0;
    blocks("SDHC + MBR");
    clocks("SDHC + MBR");

    memcpy(copy, i16, (size_t)n16 * 512u);
    card(copy, n16, 2048, 1, 0, 0);            /* SDv2 byte-addressed (SDSC), FAT16 in a partition */
    files("SDSC v2 FAT16 + MBR", FS_FAT16);
    CHECK(CardType == CT_SD2, "SDSC v2: card type %u", CardType);
    blocks("SDSC v2 (byte addresses)");
    clocks("SDSC v2 (byte addresses)");

    memcpy(copy, i16, (size_t)n16 * 512u);
    card(copy, n16, 0, 0, 0, 0);               /* SDv1 */
    files("SDv1 FAT16", FS_FAT16);
    CHECK(CardType == CT_SD1, "SDv1: card type %u", CardType);

    memcpy(copy, i16, (size_t)n16 * 512u);
    card(copy, n16, 0, 0, 0, 1);               /* MMCv3 */
    files("MMC FAT16", FS_FAT16);
    CHECK(CardType == CT_MMC, "MMC: card type %u", CardType);
    blocks("MMC");
    clocks("MMC");

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
    CHECK(sd_readp(copy, 0, 0, 16) == RES_ERROR, "no data token: error");
    CHECK(clk - t0 >= 1000u * 100u && clk - t0 < 1000u * 110u, "no data token: gave up after %u ms",
          (unsigned)((clk - t0) / 1000u));

    printf("sd_test: %d checks, %d failed\n", checks, fails);
    free(copy); free(i16); free(i32);
    return fails ? 1 : 0;
}
