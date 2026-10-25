/* SPDX-License-Identifier: GPL-3.0-only */
/* The SD card (builder item SDCARD, off by default; the hardware mod: docs/SD-CARD-DESIGN.md). Petit FatFs (pff.c, its
 * own unit: tools/build.py) over ChaN's SPI-mode card driver (sd_mmc.c) on the LCD's SPI1 (hal/fm1_sd_bus.h). FAT16 and
 * FAT32, 8.3 names, no files created or grown: a computer makes them, the FM-1 reads them and writes inside them.
 * Main loop only: each call takes the bus from the LCD (a NOP to the panel first, so the bytes it sees while the card is
 * deselected are ignored) and gives it back at the LCD's clock.
 *
 *   sd_mount()                  the card and its filesystem: SD_READY, SD_NOCARD or SD_NOFS (sd_state)
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

static uint32_t sd_mount(void)
{
    FRESULT r;
    sd_begin(FM1_SD_BAUD_SLOW);
    r = pf_mount(&sd_fs);                            /* (disk_initialize, then the boot record at SD_FAST) */
    sd_end();
    sd_state = r == FR_OK ? SD_READY : r == FR_NOT_READY ? SD_NOCARD : SD_NOFS;
    return sd_state;
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
