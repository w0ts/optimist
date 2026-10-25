/* SPDX-License-Identifier: GPL-3.0-only */
/* The SD card (builder item SDCARD, off by default; the hardware mod: docs/SD-CARD-DESIGN.md). Petit FatFs (pff.c, its
 * own unit: tools/build.py) over ChaN's SPI-mode card driver (sd_mmc.c) on the LCD's SPI1 (hal/fm1_sd_bus.h). FAT16 and
 * FAT32, 8.3 names, no files created or grown: a computer makes them, the FM-1 reads them and writes inside them.
 * Main loop only: each call takes the bus from the LCD (a NOP to the panel first, so the bytes it sees while the card is
 * deselected are ignored) and gives it back at the LCD's clock.
 *
 *   sd_mount()                  the card and its filesystem: SD_READY, SD_NOCARD or SD_NOFS (sd_state). A mode
 *                               (fm1_sd_mode) that finds no card makes it try the other two, so the mode that reads is
 *                               the one left set; then the DMA probe (sd_dma_probe) sets fm1_sd_dma
 *   sd_dma_probe()              sector 0 read by bytes and by DMA, each block's CRC16 checked: 1 = DMA reads right
 *                               (fm1_sd_dma = 1), 0 = the byte path only; sd_probe_seen: b0 bytes CRC ok, b1 DMA CRC
 *                               ok, b2 the two equal
 *   sd_file_run(&sector)        the open file's sectors under its pointer to the cluster's end, for sd_rd_* (one
 *                               CMD18); then sd_seek(pointer + n * 512)
 *   sd_blk                      a 512-byte block buffer, 4-byte aligned (the probe's, the console's)
 *   sd_open(path)               FR_OK when the file opened (one file at a time, Petit FatFs' rule)
 *   sd_read(p, n, &got)         from the open file, at its pointer
 *   sd_write(p, n, &put)        into the open file, at its pointer, within its size; sd_write(0, 0, &put) ends the sector
 *   sd_seek(ofs)                the pointer (sector aligned for writes)
 *   sd_opendir / sd_readdir     a directory's entries
 *   sd_card_mb()                the card's capacity, MB (its CSD) */
#include "storage/sd/pff.h"
#include "storage/sd/sd_mmc.c"

enum { SD_UNMOUNTED = 0, SD_NOCARD, SD_NOFS, SD_READY };
static FATFS sd_fs IN_POOL;
static uint8_t sd_state;

static void sd_begin(uint32_t baud)
{
    lcd_cmd(0x00);                                   /* NOP: ends a memory write; leaves the LCD deselected */
    fm1_sd_bus_begin(baud);
}

static void sd_end(void)
{
    fm1_sd_bus_end();
    fm1_lcd_baud(LCD_BAUD);
}

static uint8_t sd_blk[512] IN_POOL __attribute__((aligned(4)));
static uint8_t sd_probe_seen;

static uint32_t sd_dma_probe(void)
{
    uint16_t want = 0;
    uint32_t seen = 0, ok;
    fm1_sd_dma = 0;
    sd_begin(FM1_SD_BAUD_FAST);
    if (disk_readblocks(sd_blk, 0, 1) == RES_OK && sd_crc16(sd_blk, 512) == sd_rd.crc) {
        seen = 1;
        want = sd_rd.crc;
    }
    if (fm1_sd_mode != FM1_SD_BITBANG) {
        fm1_sd_dma = 1;
        if (disk_readblocks(sd_blk, 0, 1) == RES_OK && sd_crc16(sd_blk, 512) == sd_rd.crc)
            seen |= 2u | (seen & 1u && sd_rd.crc == want ? 4u : 0u);
    }
    sd_end();
    ok = (seen & 2u) && (!(seen & 1u) || (seen & 4u));      /* DMA right (and agreeing with the bytes when they read) */
    fm1_sd_dma = (uint8_t)ok;
    sd_probe_seen = (uint8_t)seen;
    return ok;
}

static uint32_t sd_mount(void)
{
    FRESULT r = FR_NOT_READY;
    uint32_t i, m0 = fm1_sd_mode;
    for (i = 0; i < 3u && r == FR_NOT_READY; i++) {   /* the set mode first, then the others */
        fm1_sd_mode = (uint8_t)((m0 + i) % 3u);
        fm1_sd_dma = 0;
        sd_begin(FM1_SD_BAUD_SLOW);
        r = pf_mount(&sd_fs);                        /* (disk_initialize, then the boot record at SD_FAST) */
        sd_end();
    }
    if (r == FR_NOT_READY)
        fm1_sd_mode = (uint8_t)m0;
    else
        sd_dma_probe();
    sd_state = r == FR_OK ? SD_READY : r == FR_NOT_READY ? SD_NOCARD : SD_NOFS;
    return sd_state;
}

static uint32_t sd_file_run(uint32_t *sector)
{
    DWORD s = 0;
    UINT n;
    if (sd_state != SD_READY)
        return 0;
    sd_begin(FM1_SD_BAUD_FAST);                      /* (a FAT read when the pointer is on a cluster boundary) */
    n = pf_run(&s);
    sd_end();
    *sector = s;
    return n;
}

static uint32_t sd_card_mb(void) { return sd_sectors / 2048u; }
static uint32_t sd_card_type(void) { return CardType; }

#define SD_CALL(expr) do { sd_begin(FM1_SD_BAUD_FAST); r = (expr); sd_end(); } while (0)

static FRESULT sd_open(const char *path)
{
    FRESULT r;
    if (sd_state != SD_READY)
        return FR_NOT_ENABLED;
    SD_CALL(pf_open(path));
    return r;
}

static FRESULT sd_read(void *p, uint32_t n, uint32_t *got)
{
    FRESULT r;
    UINT g = 0;
    SD_CALL(pf_read(p, (UINT)n, &g));
    *got = g;
    return r;
}

static FRESULT sd_write(const void *p, uint32_t n, uint32_t *put)
{
    FRESULT r;
    UINT w = 0;
    SD_CALL(pf_write(p, (UINT)n, &w));
    *put = w;
    return r;
}

static FRESULT sd_seek(uint32_t ofs)
{
    FRESULT r;
    SD_CALL(pf_lseek(ofs));
    return r;
}

static FRESULT sd_opendir(DIR *d, const char *path)
{
    FRESULT r;
    if (sd_state != SD_READY)
        return FR_NOT_ENABLED;
    SD_CALL(pf_opendir(d, path));
    return r;
}

static FRESULT sd_readdir(DIR *d, FILINFO *fi)
{
    FRESULT r;
    SD_CALL(pf_readdir(d, fi));
    return r;
}
