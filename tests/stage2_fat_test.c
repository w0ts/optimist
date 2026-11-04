/* SPDX-License-Identifier: GPL-3.0-only */
/* MENU.BIN's list (firmware/stage2/s2_card.c's way) with the real Petit FatFs on a FAT32 image a Mac wrote
 * (tests/fixtures/sd/stage2_lfn.img.gz: an MBR, /FIRMWARES, long and 8.3 names, a ._ sidecar file). */
#include <stdio.h>
#include <string.h>
#include "diskio.h"
#include "pff.h"
#include "s2_flash.h"

static FILE *image;
static FATFS fs;
static struct s2_lfn lfn;
static int fails;

void s2_dir_hook(const BYTE *entry);
void s2_dir_hook(const BYTE *entry) { s2_lfn_feed(&lfn, entry); }

DSTATUS disk_initialize(void) { return 0; }
DRESULT disk_readp(BYTE *buff, DWORD sector, UINT offset, UINT count)
{
    if (fseek(image, (long)sector * 512 + (long)offset, SEEK_SET))
        return RES_ERROR;
    if (!buff)
        return RES_OK;
    return fread(buff, 1, count, image) == count ? RES_OK : RES_ERROR;
}
DRESULT disk_writep(const BYTE *buff, DWORD sc) { (void)buff; (void)sc; return RES_ERROR; }

static void expect(const char *label, int want, int got)
{
    printf("%-24s %s\n", label, got == want ? "ok" : "MISSING OR WRONG");
    fails += got != want;
}

int main(int argc, char **argv)
{
    static DIR dir;
    static FILINFO info;
    char folder[16] = "/", label[S2_LABEL_MAX + 1];
    int folder_found = 0, sd = 0, cut = 0, plain = 0, others = 0;
    if (argc != 2 || !(image = fopen(argv[1], "rb")))
        return 2;
    if (pf_mount(&fs) != FR_OK || pf_opendir(&dir, "/") != FR_OK)
        return 2;
    while (pf_readdir(&dir, &info) == FR_OK && info.fname[0])
        if ((info.fattrib & AM_DIR) && s2_is_folder(info.fname)) {
            strcat(folder, info.fname);
            folder_found = 1;
        }
    expect("folder", 1, folder_found);
    if (pf_opendir(&dir, folder) != FR_OK)
        return 2;
    while (pf_readdir(&dir, &info) == FR_OK && info.fname[0]) {
        if ((info.fattrib & AM_DIR) || !s2_is_firmware(info.fname))
            continue;
        s2_label(label, &lfn, info.fname);
        printf("listed %-13s %s\n", info.fname, label);
        if (!strcmp(label, "optimist-sd-a015e367"))
            ++sd;
        else if (!strcmp(label, "a-very-long-name-tha"))
            ++cut;
        else if (!strcmp(label, "PLAIN.FWS"))
            ++plain;
        else
            ++others;
    }
    expect("long name", 1, sd);
    expect("cut at 20, 3 entries", 1, cut);
    expect("8.3 name", 1, plain);
    expect("no sidecar, no strays", 0, others);
    printf("stage2_fat_test: %d failed\n", fails);
    return fails != 0;
}
