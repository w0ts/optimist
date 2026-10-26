/* SPDX-License-Identifier: GPL-3.0-only */
/* FatFs' disk layer (fatfs/diskio.h) over the card driver (sd_mmc.c): full FatFs builds (FELUCCA_SD_FATFS). Included
 * after sd_mmc.c by sdcard.c (the FM-1) and by tests/sd_test.c (the card model). One drive, pdrv 0:
 *   disk_initialize   the card's start-up (sd_card_init) in a session at the start-up clock (SD_SESSION_SLOW)
 *   disk_read         n sectors by one CMD18 run (CMD17 for one: disk_readblocks), each block's CRC16 checked (a
 *                     failure read again, a second one at 30 MHz dropping the session to 20: sd_crc_retry); by DMA
 *                     into an aligned buffer (FatFs' window; a caller's unaligned buffer by bytes)
 *   disk_write        n sectors, a CMD24 each (disk_writeblock: the CRC16 sent, the card's busy end waited for, so
 *                     nothing is pending after it: CTRL_SYNC has nothing to do)
 *   disk_ioctl        CTRL_SYNC, GET_SECTOR_COUNT (the CSD's), GET_BLOCK_SIZE (1: unknown; FatFs asks for f_mkfs only)
 *   get_fattime       the date FatFs stamps on what it writes: sd_fattime, a placeholder (the FM-1 has no clock; the
 *                     web / iOS app could set it when it connects: docs/SD-CARD-DESIGN.md 8)
 * Each read and each write is a bus session of its own (SD_SESSION / SD_SESSION_END: sdcard.c takes the bus from the
 * LCD and gives it back), so any FatFs call runs from the main loop and the screen draws between its transfers. A card
 * that fails a read or a write (an error, a timeout, a CRC failing twice at 20 MHz) is taken as gone: sd_gone, every
 * later read and write RES_NOTRDY at once (FatFs: FR_DISK_ERR) until a mount starts it again, instead of each waiting
 * out its timeouts. disk_status stays 0 meanwhile, so FatFs never starts the card on its own in some later call.
 *   sd_map_run(fp, &sector)  an open file's sectors from its pointer (on a sector boundary) to the end of the
 *                     contiguous run they are in, or the file's end, from its cluster-link map (fp->cltbl, made by
 *                     f_lseek(fp, CREATE_LINKMAP)): one CMD18 for sd_rd_*; 0 when there is no map or no data there
 *   sd_map_runs(fp)   the runs the map holds (1: the file is contiguous) */
#ifndef SD_SESSION
#define SD_SESSION_SLOW() ((void)0)                  /* (a host test: no bus to share) */
#define SD_SESSION() ((void)0)
#define SD_SESSION_END() ((void)0)
#endif

static DSTATUS sd_dstat = STA_NOINIT;
static BYTE sd_gone;
static DWORD sd_fattime = (DWORD)(2026 - 1980) << 25 | (DWORD)1 << 21 | (DWORD)1 << 16;   /* 2026-01-01 00:00 */

DSTATUS disk_status(BYTE pdrv) { return pdrv ? STA_NOINIT : sd_dstat; }

DSTATUS disk_initialize(BYTE pdrv)
{
    if (pdrv)
        return STA_NOINIT;
    SD_SESSION_SLOW();
    sd_dstat = sd_card_init();
    SD_SESSION_END();
    sd_gone = sd_dstat != 0;
    return sd_dstat;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    DRESULT r;
    if (pdrv || !count)
        return RES_PARERR;
    if (sd_gone)
        return RES_NOTRDY;
    SD_SESSION();
    r = disk_readblocks(buff, sector, count);
    SD_SESSION_END();
    sd_gone = r != RES_OK;
    return r;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    DRESULT r = RES_OK;
    UINT i;
    if (pdrv || !count)
        return RES_PARERR;
    if (sd_gone)
        return RES_NOTRDY;
    SD_SESSION();
    for (i = 0; i < count && r == RES_OK; i++)
        r = disk_writeblock(buff + i * 512u, sector + i);
    SD_SESSION_END();
    sd_gone = r != RES_OK;
    return r;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    if (pdrv)
        return RES_PARERR;
    if (cmd == CTRL_SYNC)
        return sd_gone ? RES_NOTRDY : RES_OK;
    if (cmd == GET_SECTOR_COUNT && sd_sectors) {
        *(LBA_t *)buff = sd_sectors;
        return RES_OK;
    }
    if (cmd == GET_BLOCK_SIZE) {
        *(DWORD *)buff = 1;
        return RES_OK;
    }
    return RES_PARERR;
}

DWORD get_fattime(void) { return sd_fattime; }

static UINT sd_map_run(const FIL *fp, LBA_t *sector)
{
    const DWORD *m = fp->cltbl;
    const FATFS *fs = fp->obj.fs;
    DWORD cs, ptr = (DWORD)fp->fptr, size = (DWORD)fp->obj.objsize, sec, cl, n;
    *sector = 0;
    if (!m || !fs || (ptr & 511u) || ptr >= size)
        return 0;
    cs = fs->csize;
    sec = ptr / 512u;
    cl = sec / cs;
    for (m++; m[0]; m += 2) {                        /* (pairs: a run's clusters, its first cluster; 0 ends them) */
        if (cl < m[0]) {
            *sector = fs->database + (m[1] + cl - 2u) * cs + sec % cs;
            n = (m[0] - cl) * cs - sec % cs;         /* to the run's end */
            sec = (size - ptr + 511u) / 512u;        /* ... or the file's */
            return (UINT)(n < sec ? n : sec);
        }
        cl -= m[0];
    }
    return 0;
}

static UINT sd_map_runs(const FIL *fp)              /* (the map's length: 2 + 2 a run) */
{
    return fp->cltbl ? (UINT)(fp->cltbl[0] - 2u) / 2u : 0;
}
