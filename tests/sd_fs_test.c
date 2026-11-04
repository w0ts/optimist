/* SPDX-License-Identifier: GPL-3.0-only */
/* The SD card under full FatFs (builder item SDCARD in the big layout: FELUCCA_SD_FATFS; firmware/src/storage/sd/fatfs/,
 * the disk layer sd_diskio.c) on the byte-level card model (tests/sd_card_model.h), FAT16 and FAT32 images from a real
 * formatter (tests/fixtures/sd/make_images.sh): the start-ups, long names, files created, grown, renamed, deleted,
 * directories made, a contiguous run (f_expand) and the fast-seek map's runs, the DMA, the card gone in the middle.
 *   sd_fs_test FAT16.img FAT32.img */
#define FELUCCA_SD_FATFS 1
#include "sd_card_model.h"
#include "storage/sd/sd_mmc.c"
#include "storage/sd/sd_diskio.c"

static uint8_t kick(uint32_t i) { return (uint8_t)((i * 7 + (i >> 8)) & 255); }
static uint8_t pat(uint32_t i, uint32_t seed) { return (uint8_t)(i * 13u + (i >> 9) + seed); }

static FATFS fs __attribute__((aligned(4)));

static FRESULT mount(void) { return f_mount(&fs, "", 1); }

static uint32_t runs_of(const char *path)          /* a file's runs (fragments) by its link map: 1 = contiguous */
{
    FIL f;
    DWORD map[4];
    uint32_t n = 0;
    if (f_open(&f, path, FA_READ) != FR_OK)
        return 0;
    map[0] = 4;
    f.cltbl = map;
    if (f_lseek(&f, CREATE_LINKMAP) == FR_OK || map[0] > 4)
        n = (map[0] - 2u) / 2u;                      /* (too small a map still says how long it must be) */
    f.cltbl = 0;
    f_close(&f);
    return n;
}

static int file_is(const char *path, uint32_t size, uint32_t seed)   /* size bytes of pat(i, seed) */
{
    static uint8_t buf[1536];
    FIL f;
    UINT got;
    uint32_t off = 0, i;
    int ok;
    if (f_open(&f, path, FA_READ) != FR_OK)
        return 0;
    ok = f_size(&f) == size;
    while (ok && off < size) {
        ok = f_read(&f, buf, sizeof buf, &got) == FR_OK && got;
        for (i = 0; ok && i < got; i++)
            ok = buf[i] == pat(off + i, seed);
        off += got;
    }
    f_close(&f);
    return ok && off == size;
}

static FRESULT write_file(const char *path, uint32_t size, uint32_t seed, BYTE mode)
{
    static uint8_t buf[1000];
    FIL f;
    UINT put;
    uint32_t off = 0, i, n;
    FRESULT r = f_open(&f, path, mode);
    if (r != FR_OK)
        return r;
    if (mode & FA_OPEN_APPEND)
        off = (uint32_t)f_size(&f);
    size += off;
    while (r == FR_OK && off < size) {
        n = size - off < sizeof buf ? size - off : (uint32_t)sizeof buf;   /* (odd sizes: partial sectors) */
        for (i = 0; i < n; i++)
            buf[i] = pat(off + i, seed);
        r = f_write(&f, buf, n, &put);
        if (r == FR_OK && put != n)
            r = FR_DENIED;
        off += n;
    }
    if (r == FR_OK)
        r = f_close(&f);
    return r;
}

static void files(const char *what, BYTE fat)
{
    FIL f;
    DIR d;
    FILINFO fi;
    UINT got;
    static uint8_t buf[4096] __attribute__((aligned(4)));
    LBA_t sec;
    UINT n, off;
    DWORD map[16];
    int seen_hello = 0, seen_long = 0, seen_alias = 0;
    uint32_t i;

    CHECK(mount() == FR_OK, "%s: mount", what);
    CHECK(fs.fs_type == fat, "%s: FAT type %u, want %u", what, fs.fs_type, fat);
    CHECK(sd_sectors == card_sectors(), "%s: capacity %lu sectors", what, (unsigned long)sd_sectors);

    CHECK(f_open(&f, "HELLO.TXT", FA_READ) == FR_OK, "%s: open HELLO.TXT", what);
    memset(buf, 0, sizeof buf);
    CHECK(f_read(&f, buf, 100, &got) == FR_OK && got == 28 && !memcmp(buf, "Hello from the FM-1 SD card\n", 28),
          "%s: HELLO.TXT (%u B)", what, got);
    f_close(&f);
    CHECK(f_open(&f, "Long File Name.txt", FA_READ) == FR_OK && f_read(&f, buf, 100, &got) == FR_OK && got == 10 &&
          !memcmp(buf, "long name\n", 10), "%s: a long name opened by its name", what);
    f_close(&f);
    CHECK(f_open(&f, "long file NAME.TXT", FA_READ) == FR_OK, "%s: ... in any case", what);
    f_close(&f);

    CHECK(f_open(&f, "SAMPLES/KICK.RAW", FA_READ) == FR_OK && f_read(&f, buf, sizeof buf, &got) == FR_OK &&
          got == 3000, "%s: KICK.RAW %u B", what, got);
    for (i = 0; i < 3000 && buf[i] == kick(i); i++)
        ;
    CHECK(i == 3000, "%s: KICK.RAW differs at %u", what, (unsigned)i);
    CHECK(f_lseek(&f, 2500) == FR_OK && f_read(&f, buf, 16, &got) == FR_OK && got == 16 && buf[0] == kick(2500) &&
          buf[15] == kick(2515), "%s: seek 2500 then read", what);

    map[0] = 16;                                     /* its runs from the link map, one CMD18 each */
    f.cltbl = map;
    CHECK(f_lseek(&f, CREATE_LINKMAP) == FR_OK && sd_map_runs(&f) >= 1, "%s: KICK.RAW's map", what);
    memset(buf, 0, sizeof buf);
    CHECK(f_lseek(&f, 0) == FR_OK, "%s: fast seek to 0", what);
    for (off = 0; off < sizeof buf && (n = sd_map_run(&f, &sec)) != 0; off += n * 512u) {
        CHECK(n >= 1 && off + n * 512u <= sizeof buf, "%s: run of %u at %u", what, n, off);
        CHECK(disk_readblocks(buf + off, sec, n) == RES_OK, "%s: the run at %u read", what, off);
        CHECK(f_lseek(&f, off + n * 512u) == FR_OK, "%s: seek past the run", what);
    }
    for (i = 0; i < 3000 && buf[i] == kick(i); i++)
        ;
    CHECK(off == 3072 && i == 3000, "%s: KICK.RAW by its runs: %u B, differs at %u", what, off, (unsigned)i);
    CHECK(f_lseek(&f, 100) == FR_OK && sd_map_run(&f, &sec) == 0, "%s: no run off a sector boundary", what);
    f_close(&f);

    CHECK(f_opendir(&d, "") == FR_OK, "%s: open the root", what);
    while (f_readdir(&d, &fi) == FR_OK && fi.fname[0]) {
        seen_hello |= !strcmp(fi.fname, "HELLO.TXT") && fi.fsize == 28;
        seen_long |= !strcmp(fi.fname, "Long File Name.txt") && fi.fsize == 10;
        seen_alias |= !strncmp(fi.altname, "LONGFI", 6);
    }
    f_closedir(&d);
    CHECK(seen_hello && seen_long && seen_alias, "%s: the root by long names (hello %d long %d alias %d)", what,
          seen_hello, seen_long, seen_alias);

    CHECK(f_open(&f, "NOPE.TXT", FA_READ) == FR_NO_FILE, "%s: a missing file", what);
    CHECK(C.lcd_bad == 0, "%s: %ld bytes not 0xFF reached the LCD", what, C.lcd_bad);
}

/* files the FM-1 makes: a folder tree, long names (UTF-8), created, grown, renamed, moved, deleted; read back after a
 * mount again (nothing only in FatFs' window) */
static void writes(const char *what)
{
    FILINFO fi;
    FIL f;
    DWORD nfree0, nfree;
    FATFS *pfs;
    static const char *const NAME = "OPTIMIST/PROJECTS/Night Drive \xc3\xa9t\xc3\xa9/Night Drive \xc3\xa9t\xc3\xa9.OPP";

    CHECK(mount() == FR_OK, "%s: mount", what);
    CHECK(f_getfree("", &nfree0, &pfs) == FR_OK && nfree0 > 100, "%s: free clusters %lu", what, (unsigned long)nfree0);
    CHECK(f_mkdir("OPTIMIST") == FR_OK && f_mkdir("OPTIMIST/PROJECTS") == FR_OK &&
          f_mkdir("OPTIMIST/PROJECTS/Night Drive \xc3\xa9t\xc3\xa9") == FR_OK, "%s: folders made", what);
    CHECK(f_mkdir("OPTIMIST") == FR_EXIST, "%s: a folder made twice: FR_EXIST", what);
    CHECK(write_file(NAME, 5000, 1, FA_CREATE_NEW | FA_WRITE) == FR_OK, "%s: a file created", what);
    CHECK(write_file(NAME, 10, 1, FA_CREATE_NEW | FA_WRITE) == FR_EXIST, "%s: created twice: FR_EXIST", what);
    CHECK(write_file(NAME, 7123, 1, FA_OPEN_APPEND | FA_WRITE) == FR_OK, "%s: grown", what);
    CHECK(write_file("OPTIMIST/a small one.txt", 1, 9, FA_CREATE_ALWAYS | FA_WRITE) == FR_OK, "%s: 1 byte", what);

    CHECK(mount() == FR_OK, "%s: mount again", what);
    CHECK(file_is(NAME, 12123, 1), "%s: the long-named file read back whole", what);
    CHECK(file_is("OPTIMIST/a small one.txt", 1, 9), "%s: the 1-byte one", what);
    CHECK(f_stat(NAME, &fi) == FR_OK && fi.fsize == 12123 && fi.fdate == (WORD)(sd_fattime >> 16),
          "%s: its size and the placeholder date (%04x)", what, fi.fdate);
    CHECK(f_rename(NAME, "OPTIMIST/PROJECTS/moved.opp") == FR_OK && file_is("OPTIMIST/PROJECTS/moved.opp", 12123, 1) &&
          f_stat(NAME, &fi) == FR_NO_FILE, "%s: renamed into another folder", what);
    CHECK(f_rename("OPTIMIST/a small one.txt", "OPTIMIST/PROJECTS/moved.opp") == FR_EXIST,
          "%s: a rename onto a file: FR_EXIST (FatFs never overwrites)", what);
    CHECK(f_unlink("OPTIMIST/PROJECTS/Night Drive \xc3\xa9t\xc3\xa9") == FR_OK, "%s: an empty folder deleted", what);
    CHECK(f_unlink("OPTIMIST/PROJECTS") == FR_DENIED, "%s: a folder with files in it: FR_DENIED", what);
    CHECK(f_unlink("OPTIMIST/PROJECTS/moved.opp") == FR_OK && f_unlink("OPTIMIST/a small one.txt") == FR_OK &&
          f_unlink("OPTIMIST/PROJECTS") == FR_OK && f_unlink("OPTIMIST") == FR_OK, "%s: all deleted", what);
    CHECK(mount() == FR_OK && f_getfree("", &nfree, &pfs) == FR_OK && nfree == nfree0,
          "%s: every cluster free again (%lu of %lu)", what, (unsigned long)nfree, (unsigned long)nfree0);

    CHECK(f_open(&f, "RUN.BIN", FA_CREATE_NEW | FA_WRITE) == FR_OK && f_expand(&f, 40000, 1) == FR_OK &&
          f_close(&f) == FR_OK, "%s: a contiguous run allocated (f_expand)", what);
    CHECK(runs_of("RUN.BIN") == 1, "%s: ... one run (%u)", what, (unsigned)runs_of("RUN.BIN"));
    CHECK(f_stat("RUN.BIN", &fi) == FR_OK && fi.fsize == 40000, "%s: ... its size the run's", what);
    CHECK(write_file("RUN.BIN", 40000, 3, FA_OPEN_EXISTING | FA_WRITE) == FR_OK && file_is("RUN.BIN", 40000, 3) &&
          runs_of("RUN.BIN") == 1, "%s: written inside the run: still one", what);
    CHECK(f_unlink("RUN.BIN") == FR_OK, "%s: the run deleted", what);
    CHECK(C.lcd_bad == 0 && C.crc_refused == 0, "%s: LCD bytes %ld, CRC refusals %ld", what, C.lcd_bad,
          C.crc_refused);
}

/* fragmented free room: f_expand finds a run where FatFs' own growth would scatter the file */
static void fragments(const char *what)
{
    char name[16];
    FIL f;
    int i;
    uint32_t cl = (uint32_t)fs.csize * 512u;

    CHECK(mount() == FR_OK, "%s: mount", what);
    fs.last_clst = 2;                                /* (the files in the lowest free clusters) */
    for (i = 0; i < 24; i++) {                       /* 24 one-cluster files, every other one deleted */
        snprintf(name, sizeof name, "F%02d.BIN", i);
        CHECK(write_file(name, cl, (uint32_t)i, FA_CREATE_ALWAYS | FA_WRITE) == FR_OK, "%s: %s", what, name);
    }
    for (i = 0; i < 24; i += 2) {
        snprintf(name, sizeof name, "F%02d.BIN", i);
        CHECK(f_unlink(name) == FR_OK, "%s: %s deleted", what, name);
    }
    CHECK(mount() == FR_OK, "%s: mount", what);
    fs.last_clst = 2;                                /* the allocator from the volume's start (FAT32's FSINFO hint would
                                                      * start it past the holes) */
    CHECK(write_file("GROWN.BIN", 6u * cl, 5, FA_CREATE_NEW | FA_WRITE) == FR_OK && file_is("GROWN.BIN", 6u * cl, 5),
          "%s: a file grown cluster by cluster", what);
    CHECK(runs_of("GROWN.BIN") > 1, "%s: ... is scattered over the holes (%u runs)", what,
          (unsigned)runs_of("GROWN.BIN"));
    CHECK(f_open(&f, "RUN.BIN", FA_CREATE_NEW | FA_WRITE) == FR_OK && f_expand(&f, 6u * cl, 1) == FR_OK &&
          f_close(&f) == FR_OK && write_file("RUN.BIN", 6u * cl, 6, FA_OPEN_EXISTING | FA_WRITE) == FR_OK,
          "%s: the same size by f_expand", what);
    CHECK(runs_of("RUN.BIN") == 1 && file_is("RUN.BIN", 6u * cl, 6), "%s: ... one run (%u)", what,
          (unsigned)runs_of("RUN.BIN"));
    for (i = 1; i < 24; i += 2) {
        snprintf(name, sizeof name, "F%02d.BIN", i);
        CHECK(file_is(name, cl, (uint32_t)i) && f_unlink(name) == FR_OK, "%s: %s whole", what, name);
    }
    CHECK(f_unlink("GROWN.BIN") == FR_OK && f_unlink("RUN.BIN") == FR_OK, "%s: cleaned up", what);
}

/* f_expand's search never takes a run across the volume's end (free clusters at the end, then cluster 2 free: not one
 * run; R0.16 counted on past the wrap: FR_INT_ERR and a chain written past the FAT's end, fixed in fatfs/ff.c). FAT16
 * only: FAT32's root folder is cluster 2 */
static FRESULT empty_dir(char *path, UINT len)      /* everything under path deleted (path: a buffer of 256) */
{
    DIR d;
    FILINFO fi;
    FRESULT r = f_opendir(&d, path);
    while (r == FR_OK && (r = f_readdir(&d, &fi)) == FR_OK && fi.fname[0]) {
        snprintf(path + len, 256 - len, "%s%s", len ? "/" : "", fi.fname);
        if (fi.fattrib & AM_DIR)
            r = empty_dir(path, (UINT)strlen(path));
        if (r == FR_OK)
            r = f_unlink(path);
        path[len] = 0;
    }
    f_closedir(&d);
    return r;
}

static void expand_wrap(const char *what)
{
    char path[256] = "";
    FRESULT r;
    FIL f;
    DWORD nfree;
    FATFS *pfs;
    uint32_t cl = (uint32_t)fs.csize * 512u, n;
    CHECK(mount() == FR_OK, "%s: mount", what);
    CHECK(empty_dir(path, 0) == FR_OK, "%s: the card emptied", what);
    CHECK(f_getfree("", &nfree, &pfs) == FR_OK && nfree == fs.n_fatent - 2u, "%s: every cluster free (%lu)", what,
          (unsigned long)nfree);
    n = fs.n_fatent - 2u;
    fs.last_clst = 2;
    CHECK(f_open(&f, "X.BIN", FA_CREATE_NEW | FA_WRITE) == FR_OK && f_expand(&f, 3u * cl, 1) == FR_OK &&
          f_close(&f) == FR_OK && f.obj.sclust == 2u, "%s: X at cluster 2 (%lu)", what, (unsigned long)f.obj.sclust);
    CHECK(f_open(&f, "A.BIN", FA_CREATE_NEW | FA_WRITE) == FR_OK && f_expand(&f, (n - 9u) * cl, 1) == FR_OK &&
          f_close(&f) == FR_OK, "%s: A to 6 clusters before the end", what);
    CHECK(f_unlink("X.BIN") == FR_OK, "%s: X deleted: 3 free at the start, 6 at the end", what);
    fs.last_clst = fs.n_fatent - 8u;                 /* the search from A's last cluster */
    CHECK(f_open(&f, "C.BIN", FA_CREATE_NEW | FA_WRITE) == FR_OK, "%s: C opened", what);
    r = f_expand(&f, 8u * cl, 1);
    CHECK(r == FR_DENIED, "%s: no run of 8: FR_DENIED (%d)", what, r);
    CHECK(f_expand(&f, 6u * cl, 1) == FR_OK && f.obj.sclust == fs.n_fatent - 6u, "%s: the 6 at the end: a run", what);
    CHECK(f_close(&f) == FR_OK && runs_of("C.BIN") == 1, "%s: ... one", what);
    CHECK(f_open(&f, "D.BIN", FA_CREATE_NEW | FA_WRITE) == FR_OK && f_expand(&f, 3u * cl, 1) == FR_OK &&
          f.obj.sclust == 2u && f_close(&f) == FR_OK, "%s: the 3 at the start: a run after the wrap", what);
    CHECK(f_getfree("", &nfree, &pfs) == FR_OK && nfree == 0, "%s: the card full (%lu free)", what,
          (unsigned long)nfree);
}

/* the DMA: FatFs' window and whole sectors into an aligned buffer by DMA, an unaligned one by bytes, no clash */
static void dma(const char *what)
{
    static uint8_t buf[2048 + 4] __attribute__((aligned(4)));
    FIL f;
    UINT got;
    dma_on = 1;
    memset(&D, 0, sizeof D);
    CHECK(mount() == FR_OK && write_file("DMA.BIN", 2048, 7, FA_CREATE_ALWAYS | FA_WRITE) == FR_OK,
          "%s: written with the DMA on", what);
    CHECK(file_is("DMA.BIN", 2048, 7), "%s: read back", what);
    CHECK(f_open(&f, "DMA.BIN", FA_READ) == FR_OK && f_read(&f, buf + 1, 2048, &got) == FR_OK && got == 2048,
          "%s: whole sectors into an unaligned buffer", what);
    CHECK(buf[1] == pat(0, 7) && buf[2048] == pat(2047, 7), "%s: ... right", what);
    f_close(&f);
    CHECK(D.ops > 0 && D.clash == 0 && D.misaligned == 0, "%s: %ld DMAs, %ld clashes, %ld misaligned", what, D.ops,
          D.clash, D.misaligned);
    CHECK(f_unlink("DMA.BIN") == FR_OK, "%s: deleted", what);
    dma_on = 0;
}

/* the card gone in the middle: an error, then every call fails at once (no timeouts each), a mount finds it again */
static void gone(const char *what)
{
    FIL f;
    UINT got;
    uint8_t b[600];
    uint32_t t0;
    CHECK(mount() == FR_OK && f_open(&f, "HELLO.TXT", FA_READ) == FR_OK, "%s: open", what);
    C.present = 0;
    CHECK(f_read(&f, b, 600, &got) == FR_DISK_ERR, "%s: the card pulled: FR_DISK_ERR", what);
    t0 = clk;
    CHECK(f_open(&f, "SAMPLES/KICK.RAW", FA_READ) == FR_DISK_ERR && f_stat("HELLO.TXT", 0) == FR_DISK_ERR,
          "%s: later calls fail", what);
    CHECK(clk - t0 < 10u, "%s: ... at once (%u ticks)", what, (unsigned)(clk - t0));
    C.present = 1;
    card_power_on();
    CHECK(mount() == FR_OK && f_open(&f, "HELLO.TXT", FA_READ) == FR_OK && f_read(&f, b, 600, &got) == FR_OK &&
          got == 28, "%s: back after a mount", what);
    f_close(&f);
}

int main(int argc, char **argv)
{
    uint32_t n16, n32;
    uint8_t *i16, *i32, *copy;
    uint32_t t0;

    if (argc < 3) { printf("usage: sd_fs_test FAT16.img FAT32.img\n"); return 2; }
    i16 = load(argv[1], &n16);
    i32 = load(argv[2], &n32);
    copy = malloc((size_t)n32 * 512u);

    memcpy(copy, i32, (size_t)n32 * 512u);
    card(copy, n32, 2048, 1, 1, 0);                  /* SDHC, FAT32 in a partition at 2048 */
    files("SDHC FAT32 + MBR", FS_FAT32);
    writes("SDHC FAT32 + MBR");
    fragments("SDHC FAT32 + MBR");
    dma("SDHC FAT32 + MBR");
    gone("SDHC FAT32 + MBR");

    memcpy(copy, i32, (size_t)n32 * 512u);
    card(copy, n32, 0, 1, 1, 0);                     /* SDHC, FAT32 on the whole card */
    files("SDHC FAT32", FS_FAT32);

    memcpy(copy, i16, (size_t)n16 * 512u);
    card(copy, n16, 2048, 1, 0, 0);                  /* SDSC v2 byte addressed, FAT16 in a partition */
    files("SDSC v2 FAT16 + MBR", FS_FAT16);
    writes("SDSC v2 FAT16 + MBR");
    fragments("SDSC v2 FAT16 + MBR");
    dma("SDSC v2 FAT16 + MBR");

    memcpy(copy, i16, (size_t)n16 * 512u);
    card(copy, n16, 0, 0, 0, 0);                     /* SDv1 */
    files("SDv1 FAT16", FS_FAT16);
    expand_wrap("SDv1 FAT16");
    CHECK(CardType == CT_SD1, "SDv1: card type %u", CardType);

    memcpy(copy, i16, (size_t)n16 * 512u);
    card(copy, n16, 0, 0, 0, 1);                     /* MMCv3 */
    files("MMC FAT16", FS_FAT16);
    writes("MMC FAT16");
    CHECK(CardType == CT_MMC, "MMC: card type %u", CardType);

    card(copy, n16, 0, 1, 1, 0);
    C.present = 0;                                   /* no card */
    t0 = clk;
    CHECK(mount() == FR_NOT_READY && clk - t0 < 1000u * 50u, "no card: not ready in %u ms",
          (unsigned)((clk - t0) / 1000u));
    card(copy, n16, 0, 1, 1, 0);
    C.acmd41_busy = -1;                              /* never leaves idle: the 1 s start-up limit */
    t0 = clk;
    CHECK(mount() == FR_NOT_READY && clk - t0 >= 1000u * 1000u && clk - t0 < 1000u * 1100u,
          "stuck card: gave up after %u ms", (unsigned)((clk - t0) / 1000u));
    memset(copy, 0, (size_t)n16 * 512u);
    card(copy, n16, 0, 1, 1, 0);
    CHECK(mount() == FR_NO_FILESYSTEM, "blank card: no filesystem");

    printf("sd_fs_test: %d checks, %d failed\n", checks, fails);
    free(copy); free(i16); free(i32);
    return fails ? 1 : 0;
}
