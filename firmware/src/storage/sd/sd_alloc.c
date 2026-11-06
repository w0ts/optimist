/* SPDX-License-Identifier: GPL-3.0-only */
/* No fragmentation by design (docs/SD-CARD-DESIGN.md 8, the user 2026-10-25: "cards are cheap ... this can be a
 * setting"): every file the FM-1 writes through here is ONE contiguous run (a project does not come here: sd_save.c
 * lets FatFs allocate it, read once it may be in pieces, the user 2026-11-06), allocated at once (FatFs f_expand) and
 * sized by a best guess of its final size, never grown cluster by cluster. The file's size on the card is the run's;
 * the real length is kept inside the file (a WAV's chunks), and the file is never truncated: the slack is the point.
 * Full FatFs builds (FELUCCA_SD_FATFS); pure over FatFs, host-tested (tests/sd_layout_test.c: every such file,
 * on a card whose free room is fragmented, is one run).
 *   sda_guess(kind, need, hint)   the run's bytes before rounding: need (the bytes the file holds now) plus the kind's
 *                                 slack:
 *       SDA_EXACT     presets, kits, the index, an edited or resampled sample: none (its length is known; a file of
 *                     a cluster or less cannot fragment anyway)
 *       SDA_RECORD    hint: the take's length in bytes if one was asked for, else the loop's at the tempo x 2 (a
 *                     second take), else a minute (sda_rec_bytes); never less than need
 *       SDA_RENDER    hint: the song's length in bytes
 *   sda_create(fp, path, kind, need, hint)   a new file (FA_CREATE_NEW: never over one that exists) as one run of the
 *                                 guess rounded up to whole clusters, opened for writing at 0. A card with no run that
 *                                 long free: the run of need alone (no slack) is tried, then FR_DENIED (the card full or
 *                                 too fragmented: say so, never write it in pieces); the file is gone again then
 *   sda_rec_bytes(asked_ms, loop_ms, rate, bytes_frame)   SDA_RECORD's hint */
enum { SDA_EXACT = 0, SDA_RECORD, SDA_RENDER };

static uint32_t sda_rec_bytes(uint32_t asked_ms, uint32_t loop_ms, uint32_t rate, uint32_t bytes_frame)
{
    uint32_t ms = asked_ms ? asked_ms : loop_ms ? loop_ms * 2u : 60000u;
    return (uint32_t)((uint64_t)ms * rate / 1000u * bytes_frame);
}

static uint32_t sda_guess(uint32_t kind, uint32_t need, uint32_t hint)
{
    uint64_t want = kind == SDA_EXACT || hint < need ? need : hint;
    return want > 0xFFFF0000u ? 0xFFFF0000u : want ? (uint32_t)want : 1u;   /* (FAT: under 4 GB; f_expand: not 0) */
}

static uint32_t sda_round(const FATFS *fs, uint32_t n)   /* up to whole clusters */
{
    uint32_t cl = (uint32_t)fs->csize * 512u;
    return n > 0xFFFF0000u - cl ? n : (n + cl - 1u) / cl * cl;
}

static FRESULT sda_create(FIL *fp, const char *path, uint32_t kind, uint32_t need, uint32_t hint)
{
    FRESULT r = f_open(fp, path, FA_CREATE_NEW | FA_WRITE | FA_READ);
    uint32_t run;
    if (r != FR_OK)
        return r;
    run = sda_round(fp->obj.fs, sda_guess(kind, need, hint));
    r = f_expand(fp, run, 1);
    if (r == FR_DENIED && run > need)                /* no run that long: the bytes it needs, no slack */
        r = f_expand(fp, sda_round(fp->obj.fs, need ? need : 1u), 1);
    if (r != FR_OK) {
        (void)f_close(fp);
        (void)f_unlink(path);
    }
    return r;
}
