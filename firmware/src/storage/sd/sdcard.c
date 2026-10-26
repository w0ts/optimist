/* SPDX-License-Identifier: GPL-3.0-only */
/* The SD card (builder item SDCARD, off by default; the hardware mod: docs/SD-CARD-DESIGN.md). ChaN's SPI-mode card
 * driver (sd_mmc.c) on the LCD's SPI1 (hal/fm1_sd_bus.h) under one of two filesystems:
 *   - full FatFs R0.16 (FELUCCA_SD_FATFS: LAYOUT 1 + FAR_OK, the room for its ~15 KB; fatfs/, its own units:
 *     tools/build.py; the disk layer sd_diskio.c): long names, files created, grown, renamed, several open, fast seek,
 *     contiguous runs (f_expand); the card's own layout on top (sd_layout.c, sd_index.c, sd_browse.c, sd_save.c);
 *   - Petit FatFs R0.03a (every other build: the bring-up, pff.c its own unit): FAT16 and FAT32, 8.3 names, no files
 *     created or grown: a computer makes them, the FM-1 reads them and writes inside them.
 * Main loop only. Each bus session takes the bus from the LCD (a NOP to the panel first, so the bytes it sees while the
 * card is deselected are ignored) and gives it back at the LCD's clock: under FatFs a session a disk read or write
 * (sd_diskio.c), so FatFs' own calls (f_open, f_read, ...) are made directly; under Petit a session a call (SD_CALL).
 *
 *   sd_mount()                  the card and its filesystem: SD_READY, SD_NOCARD or SD_NOFS (sd_state). A mode
 *                               (fm1_sd_mode) that finds no card makes it try the other two, so the mode that reads is
 *                               the one left set; then the DMA probe (sd_dma_probe) sets fm1_sd_dma
 *   sd_mount_boot()             the same with the set mode only (boot, before the screen's first command)
 *   the clock: after the mount, sd_clock_probe (sd_mmc.c) keeps 30 MHz when the card proves it (64 blocks at 20 and at
 *               30 MHz, CRC16 checked and compared), else 20; sd_clock_pick (the console's sd clock: 0 auto, 20, 30)
 *               overrides; a CRC failure at 30 later drops the session to 20 (sd_crc_retry); SD_BAUD() the clock of
 *               every session; sd_clock_bad: the blocks the probe's 30 MHz pass failed
 *   a card error (FR_DISK_ERR, FR_NOT_READY) in any call below: sd_state SD_NOCARD, later calls FR_NOT_ENABLED at once
 *   sd_dma_probe()              sector 0 read by bytes and by DMA, each block's CRC16 checked: 1 = DMA reads right
 *                               (fm1_sd_dma = 1), 0 = the byte path only; sd_probe_seen: b0 bytes CRC ok, b1 DMA CRC
 *                               ok, b2 the two equal
 *   sd_blk                      a 512-byte block buffer, 4-byte aligned (the probe's, the console's)
 * One file at a time for the console (Petit FatFs' rule; under FatFs sd_fil, its cluster-link map sd_fil_map):
 *   sd_open(path)               FR_OK when the file opened (for reading and writing inside it)
 *   sd_read(p, n, &got)         from the open file, at its pointer
 *   sd_write(p, n, &put)        into the open file, at its pointer, within its size (Petit: sd_write(0, 0, &put) ends
 *                               the sector, the pointer sector aligned)
 *   sd_seek(ofs)                the pointer
 *   sd_file_run(&sector)        the open file's sectors from its pointer (on a sector boundary) to the end of the run
 *                               they are in (Petit: the cluster's end; FatFs: the contiguous run's, from the link map,
 *                               to the file's end), for sd_rd_* (one CMD18); then sd_seek(pointer + n * 512). 0: none
 *   sd_file_size()              the open file's size
 *   sd_opendir / sd_readdir     a directory's entries (fname: the long name under FatFs, the 8.3 one under Petit)
 *   sd_card_mb(), sd_card_type()  the card's capacity, MB (its CSD); its type (CT_*)
 *   sd_fat_bits(), sd_cluster_bytes(), sd_data_sector()   the volume: FAT 12 / 16 / 32, a cluster, its first data sector */
#if FELUCCA_SD_FATFS
#include "storage/sd/sd_mmc.c"
#else
#include "storage/sd/pff.h"
#include "storage/sd/sd_mmc.c"
#endif

enum { SD_UNMOUNTED = 0, SD_NOCARD, SD_NOFS, SD_READY };
static FATFS sd_fs IN_POOL __attribute__((aligned(4)));   /* (FatFs' window is read into by DMA) */
static uint8_t sd_state;
static uint8_t sd_clock_pick;                        /* 0 auto (the probe), 20 or 30 MHz (the console's sd clock) */
static uint8_t sd_clock_bad;                         /* the probe's 30 MHz pass: blocks failed (64: not tried) */
#define SD_CLOCK_BLOCKS 64u                          /* the probe's run at each clock: 128 block reads, under 100 ms */
#define SD_BAUD() (sd_over ? FM1_SD_BAUD_OVER : FM1_SD_BAUD_FAST)

static uint8_t sd_lcd_live;                          /* the LCD started (main.c, after lcd_init) */

static void sd_begin(uint32_t baud)
{
    if (sd_lcd_live)
        lcd_cmd(0x00);                               /* NOP: ends a memory write (none before the LCD's start-up: the
                                                      * boot mount sends none; the emulator refuses a bare NOP there) */
    fm1_sd_bus_begin(baud);
}

static void sd_end(void)
{
    fm1_sd_bus_end();
    fm1_lcd_baud(LCD_BAUD);
}

#if FELUCCA_SD_FATFS
#define SD_SESSION_SLOW() sd_begin(FM1_SD_BAUD_SLOW)
#define SD_SESSION() sd_begin(SD_BAUD())
#define SD_SESSION_END() sd_end()
#include "storage/sd/sd_diskio.c"
#include "storage/sd/sd_layout.c"                 /* the card's own folder, the tree walker */
#include "storage/sd/sd_hash.c"                   /* XXH64: a sample's identity */
#include "storage/sd/sd_alloc.c"                  /* every file the FM-1 writes one contiguous run */
#include "storage/sd/sd_index.c"                  /* SYSTEM/INDEX.BIN: the samples by hash */
#include "storage/sd/sd_browse.c"                 /* any list as pages of 16 on the keys */
#include "storage/sd/sd_save.c"                   /* a project saved by name, staged in flash, with versions */
_Static_assert(__builtin_offsetof(FATFS, win) % 4u == 0, "sd_diskio.c: FatFs' window 4-byte aligned for the DMA");
#endif

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

static FRESULT sd_mount_fs(void)                     /* the card's start-up at 400 kHz, then the boot record at 20 */
{
    FRESULT r;
#if FELUCCA_SD_FATFS
    r = f_mount(&sd_fs, "", 1);                      /* (sessions of its own: sd_diskio.c) */
    if (r != FR_OK)
        (void)f_mount(0, "", 0);                     /* unregistered: a later f_* call is FR_NOT_ENABLED at once, never
                                                      * a card start-up of its own (a stuck card's 1 s) */
#else
    sd_begin(FM1_SD_BAUD_SLOW);
    r = pf_mount(&sd_fs);                            /* (disk_initialize, then the boot record at SD_FAST) */
    sd_end();
#endif
    return r;
}

static uint32_t sd_mount_modes(uint32_t tries)
{
    FRESULT r = FR_NOT_READY;
    uint32_t i, m0 = fm1_sd_mode;
    sd_over = 0;
    for (i = 0; i < tries && r == FR_NOT_READY; i++) {   /* the set mode first, then the others */
        fm1_sd_mode = (uint8_t)((m0 + i) % 3u);
        fm1_sd_dma = 0;
        r = sd_mount_fs();
    }
    if (r == FR_NOT_READY)
        fm1_sd_mode = (uint8_t)m0;
    else
        sd_dma_probe();
    if (r == FR_OK) {                                /* the clock the card proves */
        sd_begin(FM1_SD_BAUD_FAST);
        sd_clock_bad = (uint8_t)sd_clock_probe(sd_blk, sd_sectors < SD_CLOCK_BLOCKS ? sd_sectors : SD_CLOCK_BLOCKS,
                                               sd_clock_pick);
        sd_end();
    } else {
        sd_over = 0;
    }
    sd_state = r == FR_OK ? SD_READY : r == FR_NOT_READY ? SD_NOCARD : SD_NOFS;
    return sd_state;
}
static uint32_t sd_mount(void) { return sd_mount_modes(3u); }     /* (the console: every read mode tried) */
static uint32_t sd_mount_boot(void) { return sd_mount_modes(1u); }   /* (boot: the set mode once; a slow or bad card
                                                                       * costs one ACMD41 timeout, 1 s, not three) */

static uint32_t sd_card_mb(void) { return sd_sectors / 2048u; }
static uint32_t sd_card_type(void) { return CardType; }

#if FELUCCA_SD_FATFS
static uint32_t sd_fat_bits(void) { return sd_fs.fs_type == FS_FAT32 ? 32u : sd_fs.fs_type == FS_FAT16 ? 16u : 12u; }
static uint32_t sd_cluster_bytes(void) { return (uint32_t)sd_fs.csize * 512u; }
static uint32_t sd_data_sector(void) { return (uint32_t)sd_fs.database; }

#define SD_MAP_N 64u                                 /* the console file's link map: 31 runs (a computer's file on a
                                                      * card formatted with large clusters has few) */
static FIL sd_fil IN_POOL;
static DWORD sd_fil_map[SD_MAP_N] IN_POOL;

static FRESULT sd_was(FRESULT r)                     /* (a card error: taken as gone) */
{
    if (r == FR_DISK_ERR || r == FR_NOT_READY)
        sd_state = SD_NOCARD;
    return r;
}
#define SD_FF(expr) do { if (sd_state != SD_READY) return FR_NOT_ENABLED; return sd_was(expr); } while (0)

static FRESULT sd_open(const char *path)
{
    FRESULT r;
    if (sd_state != SD_READY)
        return FR_NOT_ENABLED;
    if (!sd_fs.wflag)
        sd_fs.winsect = (LBA_t)0 - 1u;               /* a file opened anew reads the card, not a window that raw block
                                                      * writes (the console's sd write) went around */
    (void)f_close(&sd_fil);
    r = f_open(&sd_fil, path, FA_READ | FA_WRITE);
    if (r == FR_OK) {
        sd_fil_map[0] = SD_MAP_N;
        sd_fil.cltbl = sd_fil_map;
        if (f_lseek(&sd_fil, CREATE_LINKMAP) != FR_OK)
            sd_fil.cltbl = 0;                        /* (more runs than the map holds: sd_file_run has none) */
    }
    return sd_was(r);
}

static FRESULT sd_read(void *p, uint32_t n, uint32_t *got)
{
    UINT g = 0;
    FRESULT r = sd_state == SD_READY ? sd_was(f_read(&sd_fil, p, (UINT)n, &g)) : FR_NOT_ENABLED;
    *got = g;
    return r;
}

static FRESULT sd_write(const void *p, uint32_t n, uint32_t *put)
{
    UINT w = 0;
    FRESULT r = sd_state == SD_READY ? sd_was(n ? f_write(&sd_fil, p, (UINT)n, &w) : f_sync(&sd_fil)) : FR_NOT_ENABLED;
    *put = w;
    return r;
}

static FRESULT sd_seek(uint32_t ofs) { SD_FF(f_lseek(&sd_fil, ofs)); }
static FRESULT sd_opendir(DIR *d, const char *path) { SD_FF(f_opendir(d, path)); }
static FRESULT sd_readdir(DIR *d, FILINFO *fi) { SD_FF(f_readdir(d, fi)); }
static uint32_t sd_file_size(void) { return (uint32_t)f_size(&sd_fil); }

static uint32_t sd_file_run(uint32_t *sector)
{
    LBA_t s = 0;
    UINT n = sd_state == SD_READY && sd_fil.obj.fs ? sd_map_run(&sd_fil, &s) : 0;
    *sector = (uint32_t)s;
    return n;
}
#else
static uint32_t sd_fat_bits(void) { return sd_fs.fs_type == FS_FAT32 ? 32u : 16u; }
static uint32_t sd_cluster_bytes(void) { return (uint32_t)sd_fs.csize * 512u; }
static uint32_t sd_data_sector(void) { return (uint32_t)sd_fs.database; }
static uint32_t sd_file_size(void) { return sd_fs.fsize; }

static uint32_t sd_file_run(uint32_t *sector)
{
    DWORD s = 0;
    UINT n;
    if (sd_state != SD_READY)
        return 0;
    sd_begin(SD_BAUD());                             /* (a FAT read when the pointer is on a cluster boundary) */
    n = pf_run(&s);
    sd_end();
    *sector = s;
    return n;
}

/* a filesystem call in a bus session; a card that fails (gone, or answering wrong) is taken as gone: the next calls
 * return at once (FR_NOT_ENABLED) until a mount finds it again, instead of each waiting out its timeouts */
#define SD_CALL(expr) do { \
    if (sd_state != SD_READY) return FR_NOT_ENABLED; \
    sd_begin(SD_BAUD()); r = (expr); sd_end(); \
    if (r == FR_DISK_ERR || r == FR_NOT_READY) sd_state = SD_NOCARD; \
} while (0)

static FRESULT sd_open(const char *path)
{
    FRESULT r;
    SD_CALL(pf_open(path));
    return r;
}

static FRESULT sd_read(void *p, uint32_t n, uint32_t *got)
{
    FRESULT r;
    UINT g = 0;
    *got = 0;
    SD_CALL(pf_read(p, (UINT)n, &g));
    *got = g;
    return r;
}

static FRESULT sd_write(const void *p, uint32_t n, uint32_t *put)
{
    FRESULT r;
    UINT w = 0;
    *put = 0;
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
    SD_CALL(pf_opendir(d, path));
    return r;
}

static FRESULT sd_readdir(DIR *d, FILINFO *fi)
{
    FRESULT r;
    SD_CALL(pf_readdir(d, fi));
    return r;
}
#endif
