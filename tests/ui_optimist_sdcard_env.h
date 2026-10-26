/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/ui_optimist_test.c with UI_SDCARD=1 (run_tests.sh: SD card mode, FELUCCA_SD_UI): the card's modules
 * (storage/sd/sd_layout.c .. sd_save.c) on the byte-level card model (tests/sd_card_model.h), the part of sdcard.c that
 * sd_mode.c calls (a mount, the state, the scratch block) over the model, the user presets' card side as doubles (upreset.c
 * is not in this test: its slots are the doubles above), then storage/sd/sd_mode.c itself. Included after the stores. */
#define fails card_fails                                /* (the model's counters: this test counts its own) */
#define checks card_checks
#include "sd_card_model.h"
#undef fails
#undef checks
#undef CHECK
#include "storage/sd/sd_mmc.c"
#include "storage/sd/sd_diskio.c"
#include "storage/sd/sd_layout.c"
#include "storage/sd/sd_hash.c"
#include "storage/sd/sd_alloc.c"
#include "storage/sd/sd_index.c"
#include "storage/sd/sd_browse.c"
#include "storage/sd/sd_save.c"
#if FELUCCA_SD_STREAM                                   /* (UI_SDCARD with SAMPLE, ARENA and SD_RING: the streams' plan) */
#include "storage/sd/sd_stream.c"
#include "storage/sd/sd_stream_plan.c"
#endif

enum { SD_UNMOUNTED = 0, SD_NOCARD, SD_NOFS, SD_READY };   /* (sdcard.c's) */
static FATFS sd_fs;
static uint8_t sd_state;
static uint8_t sd_blk[512];
static uint32_t sd_mounts;
static uint32_t sd_mount(void)
{
    FRESULT r;
    sd_mounts++;
    memset(&sd_fs, 0, sizeof sd_fs);
    r = f_mount(&sd_fs, "", 1);
    if (r != FR_OK)
        (void)f_mount(0, "", 0);
    sd_state = r == FR_OK ? SD_READY : r == FR_NOT_READY || r == FR_DISK_ERR ? SD_NOCARD : SD_NOFS;
    return sd_state;
}
static uint32_t sd_card_mb(void) { return sd_sectors / 2048u; }
static FRESULT sd_was(FRESULT r)
{
    if (r == FR_DISK_ERR || r == FR_NOT_READY)
        sd_state = SD_NOCARD;
    return r;
}

#define UPC_N 216u                                      /* (upreset.c: sizeof(up_rec_t)) */
static uint8_t upc_rec[UPC_N];                          /* the last record given (a LOAD from the card) */
static uint32_t upc_takes, upc_gives;
static uint32_t up_card_take(uint8_t *out, const char *name)   /* the sound as a record: a pattern and the name */
{
    uint32_t i;
    for (i = 0; i < UPC_N; i++)
        out[i] = (uint8_t)(i * 7u + (uint32_t)trk[song.sel].p[P_DEC]);
    str_cpy((char *)out + 5, name, 13);
    upc_takes++;
    return UPC_N;
}
static int up_card_give(const uint8_t *in, uint32_t n)
{
    if (n != UPC_N)
        return -1;
    memcpy(upc_rec, in, n);
    upc_gives++;
    return 0;
}
#include "storage/sd/sd_mode.c"
