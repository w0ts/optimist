/* SPDX-License-Identifier: GPL-3.0-only */
/* The sample index's pass in slices (firmware/src/storage/sd/sd_index.c sdx_pass_*; docs/SD-CARD-DESIGN.md 8) on the
 * byte-level card model (tests/sd_card_model.h: a byte a microsecond, ~1 MB/s) and a card like the 0.3 zip: 43 samples of
 * 1.66 MB, 349 presets, 56 kits, 449 files with the rest:
 *   - the pass in slices gives the one-shot pass's index (old_update below, the code as it was), byte for byte, the
 *     signature's bytes apart: a first pass, and a pass over an old index with files changed, moved, removed, added;
 *   - no slice over ~20 ms at 840 KB/s (8 ms and the unit that ends it: 22 the limit, 20.8 measured); index.tmp's and index.bin's own removal and creation, single
 *     FatFs calls, under 35 ms on the images' 512-byte clusters; the full pass's time at 840 KB/s, the screen and the audio
 *     sharing the bus;
 *   - lookups meanwhile read the old index; a use is not kept; a recording holds the pass;
 *   - a card pulled mid-pass: cancelled, index.bin as it was, the next pass whole;
 *   - a file the FM-1 writes mid-pass: the pass again, the record right;
 *   - a mount of an unchanged card walks nothing; a file added walks; forced walks; FAT16 (no FSINFO) always walks.
 *   - a card a computer filled, its next-free hint left at the volume's start (macOS): the FAT read a sector a unit, index.tmp's
 *     creation a single call of its own again, and the hint past what is used.
 *   sd_index_pass_test FAT16.img FAT32.img */
#define FELUCCA_SD_FATFS 1
#include "sd_card_model.h"
#include "storage/sd/sd_mmc.c"
#include "storage/sd/sd_diskio.c"
#include "storage/sd/sd_layout.c"
#include "storage/sd/sd_hash.c"
#include "storage/sd/sd_alloc.c"
#include "storage/sd/sd_index.c"

static FATFS fs __attribute__((aligned(4)));
static uint8_t buf[512] __attribute__((aligned(4)));
#define IDX_MAX (256u * 1024u)
static uint8_t idx_a[IDX_MAX], idx_b[IDX_MAX];
#define SLICE_MS 8u                                  /* sd_mode.c SDM_IX_SLICE_MS */
#define FRAME_MS 16.7                                /* the main loop's pass: the rest of it the screen's and the audio's */
#define WBUSY_US 3000u                               /* a block write's busy */

static FRESULT mount(void)
{
    sd_gone = 0;
    memset(&fs, 0, sizeof fs);
    return f_mount(&fs, "", 1);
}

static FRESULT put_file(const char *path, const void *p, uint32_t n)
{
    FIL f;
    UINT k;
    FRESULT r = f_open(&f, path, FA_CREATE_ALWAYS | FA_WRITE);
    if (r == FR_OK)
        r = f_write(&f, p, n, &k);
    if (r == FR_OK)
        r = f_close(&f);
    return r;
}

static FRESULT empty_dir(char *path, uint32_t n)     /* every file and folder under path */
{
    DIR d;
    FILINFO fi;
    FRESULT r = f_opendir(&d, path);
    while (r == FR_OK && (r = f_readdir(&d, &fi)) == FR_OK && fi.fname[0]) {
        uint32_t m = n + (uint32_t)strlen(fi.fname) + 1u;
        path[n] = '/';
        strcpy(path + n + 1, fi.fname);
        if (fi.fattrib & AM_DIR) {
            r = empty_dir(path, m);
            path[m] = 0;
        }
        if (r == FR_OK)
            r = f_unlink(path);
        path[n] = 0;
    }
    f_closedir(&d);
    return r;
}

static void wav(const char *path, uint32_t bytes, uint32_t seed)
{
    static uint8_t pcm[4096];
    uint8_t h[44] = {'R', 'I', 'F', 'F'};
    FIL f;
    UINT k;
    uint32_t i, left = bytes;
    memcpy(h + 8, "WAVEfmt ", 8);
    h[16] = 16;
    memcpy(h + 36, "data", 4);
    h[40] = (uint8_t)bytes, h[41] = (uint8_t)(bytes >> 8), h[42] = (uint8_t)(bytes >> 16), h[43] = (uint8_t)(bytes >> 24);
    CHECK(f_open(&f, path, FA_CREATE_ALWAYS | FA_WRITE) == FR_OK && f_write(&f, h, 44, &k) == FR_OK, "%s: made", path);
    for (i = 0; i < sizeof pcm; i++)
        pcm[i] = (uint8_t)(i * 31u + seed * 17u + (i >> 5));
    while (left) {
        UINT n = left < sizeof pcm ? left : sizeof pcm;
        CHECK(f_write(&f, pcm, n, &k) == FR_OK && k == n, "%s: written", path);
        left -= n;
    }
    CHECK(f_close(&f) == FR_OK, "%s: closed", path);
}

static void small(const char *path, uint32_t id)     /* a preset or a kit: sd_mode.c sdm_fh's header, version 2, an id */
{
    uint8_t b[160];
    memset(b, 0x11, sizeof b);
    memcpy(b, "OPPR", 4);
    b[4] = 2, b[5] = 0;
    memset(b + 16, 0, 8);
    memcpy(b + 16, &id, 4);
    CHECK(put_file(path, b, sizeof b) == FR_OK, "%s: made", path);
}

static void the_card(void)                           /* the 0.3 zip's shape */
{
    char p[96];
    uint32_t i, k, id = 1;
    CHECK(sdl_prepare() == FR_OK, "the folders");
    CHECK(f_mkdir("Optimist/Samples/Kits") == FR_OK, "a sample folder");
    for (i = 0; i < 43u; i++) {
        snprintf(p, sizeof p, "Optimist/Samples/%ssample %02u.wav", i & 1u ? "Kits/" : "", (unsigned)i);
        wav(p, 1660000u / 43u, i);
    }
    for (k = 0; k < 20u; k++) {
        snprintf(p, sizeof p, "Optimist/Presets/Factory/E%02u", (unsigned)k);
        CHECK(f_mkdir(p) == FR_OK, "%s", p);
        for (i = 0; i < 18u && id <= 349u; i++, id++) {
            snprintf(p, sizeof p, "Optimist/Presets/Factory/E%02u/%02u - preset.ops", (unsigned)k, (unsigned)i);
            small(p, id);
        }
    }
    for (i = 0; i < 57u; i++, id++) {
        snprintf(p, sizeof p, "Optimist/Kits/Factory/%02u - kit.opk", (unsigned)i);
        small(p, id);
    }
}

/* the pass as it was, in one call (the reference: every result of the slices compared with it) */
static FRESULT old_update(uint8_t *b, uint32_t bufn)
{
    sdx_head h;
    uint32_t n = 0, k = 0, cursor = 0, oldn = 0, used;
    int32_t at;
    int st, have_old = 0;
    FRESULT r;
    uint32_t root;
    sdx.files = sdx.hashed = sdx.kept = 0;
    for (root = 0; root < SDX_NROOT; root++) {
        if ((r = sdw_open(&sdx.walk, SDX_ROOTS[root])) != FR_OK) {
            if (root && (r == FR_NO_PATH || r == FR_NO_FILE))
                continue;
            return r;
        }
        while ((st = sdw_next(&sdx.walk)) > 0)
            n += sdl_len(sdx.walk.path + SDX_SKIP) < SDX_PATH && sdx_wanted(sdx.walk.path + SDX_SKIP);
        if (st < 0)
            return sdx.walk.err;
    }
    r = sdx_open_file(&sdx.old, FA_READ);
    if (r == FR_OK && sdx_head_read(&sdx.old, &h) == FR_OK) {
        oldn = h.count;
        sdx.seq = h.seq;
        have_old = 1;
    } else if (r == FR_OK) {
        (void)f_close(&sdx.old);
        r = FR_NO_FILE;
    }
    if (r != FR_OK && r != FR_NO_FILE)
        return r;
    if (sdl_chain(SDX_TMP) && sdl_chain(SDX_TMP) == sdl_chain(SDX_FILE))
        (void)f_unlink_entry(SDX_TMP);
    else
        (void)f_unlink(SDX_TMP);
    if ((r = sda_create(&sdx.f, SDX_TMP, SDA_EXACT, (n + 1u) * SDX_REC, 0)) != FR_OK)
        goto out;
    memset(&sdx.rec, 0, sizeof sdx.rec);
    sdx.cap = (uint32_t)(f_size(&sdx.f) / SDX_REC) - 1u;
    r = sdx_rw(&sdx.f, 0, &sdx.rec, SDX_REC, 1);
    root = 0;
    if (r == FR_OK)
        r = sdw_open(&sdx.walk, SDX_ROOTS[0]);
    st = 1;
    while (r == FR_OK && k < sdx.cap) {
        const char *rel;
        if ((st = sdw_next(&sdx.walk)) <= 0) {
            if (st < 0 || ++root >= SDX_NROOT)
                break;
            r = sdw_open(&sdx.walk, SDX_ROOTS[root]);
            if (r == FR_NO_PATH || r == FR_NO_FILE) {
                r = FR_OK;
                sdx.walk.depth = 0;
            }
            continue;
        }
        rel = sdx.walk.path + SDX_SKIP;
        if (sdl_len(rel) >= SDX_PATH || !sdx_wanted(rel))
            continue;
        at = -1;
        if (cursor < oldn && sdx_read_in(&sdx.old, cursor, &sdx.rec) == FR_OK && !sdl_cmp(sdx.rec.path, rel))
            at = (int32_t)cursor;
        else if (oldn)
            at = sdx_scan_path(&sdx.old, oldn, cursor, rel, &sdx.rec);
        if (at >= 0)
            cursor = (uint32_t)at + 1u;
        sdx.files++;
        if (at >= 0 && sdx.rec.size == (uint32_t)sdx.walk.fi.fsize &&
            sdx.rec.stamp == ((uint32_t)sdx.walk.fi.fdate << 16 | sdx.walk.fi.ftime)) {
            sdx.kept++;
        } else {
            used = at >= 0 ? sdx.rec.used : 0;
            if ((r = sdx_hash_into(&sdx.rec, sdx.walk.path, &sdx.walk.fi, used, b, bufn)) != FR_OK)
                break;
            sdx.hashed++;
        }
        r = sdx_rw(&sdx.f, (k + 1u) * SDX_REC, &sdx.rec, SDX_REC, 1);
        k++;
    }
    if (r == FR_OK && st < 0)
        r = sdx.walk.err;
    if (r == FR_OK)
        r = sdx_head_write(&sdx.f, k, sdx.cap, sdx.seq);
    if (f_close(&sdx.f) != FR_OK && r == FR_OK)
        r = FR_DISK_ERR;
out:
    if (have_old)
        (void)f_close(&sdx.old);
    if (r == FR_OK && ((r = f_unlink(SDX_FILE)) == FR_OK || r == FR_NO_FILE))
        r = sdl_move(SDX_TMP, SDX_FILE);
    if (r == FR_OK)
        sdx.count = k;
    return r;
}

static uint32_t index_read(uint8_t *to)              /* index.bin whole, the signature's bytes zeroed: its length */
{
    FIL f;
    UINT k = 0;
    if (f_open(&f, SDX_FILE, FA_READ) != FR_OK)
        return 0;
    CHECK(f_size(&f) <= IDX_MAX && f_read(&f, to, IDX_MAX, &k) == FR_OK, "the index read");
    f_close(&f);
    memset(to + SDX_SIG_AT, 0, sizeof(sdx_sig_t));
    return k;
}

typedef struct {
    uint32_t slices, max_us, file_us;
    double busy_us;
} slices_t;

/* the pass, a slice a frame, each slice's model time kept */
static FRESULT by_slices(slices_t *s)
{
    FRESULT r = FR_OK;
    memset(s, 0, sizeof *s);
    while (r == FR_OK && sdx_pass_on()) {
        uint32_t t0 = clk, us;
        uint32_t st0 = sdxp.st;
        r = sdx_pass_step(buf, sizeof buf, SLICE_MS);
        us = clk - t0;
        if (st0 == SDXP_TMP || st0 == SDXP_NEW || st0 == SDXP_UNLINK || st0 == SDXP_MOVE) {
            if (us > s->file_us)
                s->file_us = us;                     /* (a chain freed or made: below) */
            s->busy_us += us;
            s->slices++;
            continue;
        }
        s->slices++;
        s->busy_us += us;
        if (us > s->max_us)
            s->max_us = us;
    }
    return r;
}

/* both passes from the same card state, the indexes compared */
static void same_as_old(const char *what, uint8_t *img, uint8_t *snap, uint32_t bytes)
{
    uint32_t na, nb;
    slices_t s;
    uint32_t files, hashed, kept;
    memcpy(snap, img, bytes);
    CHECK(old_update(buf, sizeof buf) == FR_OK, "%s: the one-shot pass", what);
    files = sdx.files, hashed = sdx.hashed, kept = sdx.kept;
    na = index_read(idx_a);
    memcpy(img, snap, bytes);
    CHECK(mount() == FR_OK, "%s: mounted again", what);
    CHECK(sdx_pass_begin(1) == 1 && by_slices(&s) == FR_OK, "%s: the pass in slices", what);
    nb = index_read(idx_b);
    CHECK(na && na == nb && !memcmp(idx_a, idx_b, na), "%s: the same index, byte for byte (%u, %u bytes)", what, (unsigned)na,
          (unsigned)nb);
    CHECK(sdx.files == files && sdx.hashed == hashed && sdx.kept == kept, "%s: the same work: files %u hashed %u kept %u", what,
          (unsigned)sdx.files, (unsigned)sdx.hashed, (unsigned)sdx.kept);
    printf("sd_index_pass_test: %s: %u files, %u hashed, %u kept: %u slices, the longest %u us, its file operations %u us\n", what,
           (unsigned)sdx.files, (unsigned)sdx.hashed, (unsigned)sdx.kept, (unsigned)s.slices, (unsigned)s.max_us, (unsigned)s.file_us);
}

static void change_some(void)                        /* a computer's: changed, moved, removed, added */
{
    wav("Optimist/Samples/sample 02.wav", 20000, 99);
    CHECK(f_rename("Optimist/Samples/sample 04.wav", "Optimist/Samples/Kits/moved.wav") == FR_OK, "a sample moved");
    CHECK(f_unlink("Optimist/Samples/sample 06.wav") == FR_OK, "a sample removed");
    wav("Optimist/Samples/new.wav", 3000, 7);
    small("Optimist/Kits/Factory/new kit.opk", 900);
}

static void pulled(const char *what)
{
    uint32_t n0 = index_read(idx_a), i;
    slices_t s;
    (void)sdx_pass_begin(1);
    for (i = 0; i < 60u; i++)
        CHECK(sdx_pass_step(buf, sizeof buf, SLICE_MS) == FR_OK, "%s: a slice", what);
    CHECK(sdx_pass_on(), "%s: a pass under way", what);
    C.present = 0;
    {
        FRESULT r = FR_OK;
        for (i = 0; i < 40u && r == FR_OK && sdx_pass_on(); i++)
            r = sdx_pass_step(buf, sizeof buf, SLICE_MS);
        CHECK(r != FR_OK, "%s: the pass fails with the card gone", what);
    }
    CHECK(!sdx_pass_on() && !sdxp.smp_open && !sdxp.old_open && !sdxp.new_open, "%s: cancelled, nothing left open", what);
    C.present = 1;
    card_power_on();
    CHECK(mount() == FR_OK, "%s: the card back", what);
    CHECK(index_read(idx_b) == n0 && !memcmp(idx_a, idx_b, n0), "%s: index.bin as it was", what);
    CHECK(sdx_pass_begin(1) == 1 && by_slices(&s) == FR_OK && sdx_open() == FR_OK && sdx.count == sdx.files,
          "%s: the next pass whole (%u)", what, (unsigned)sdx.count);
}

static void meanwhile(const char *what)
{
    sdx_rec rec;
    slices_t s;
    uint32_t i, k0, seq0;
    FILINFO fi;
    uint64_t was;
    int32_t no;
    CHECK(sdx_open() == FR_OK && sdx_find_path("Samples/sample 02.wav", &rec) >= 0, "%s: indexed", what);
    (void)sdx_pass_begin(1);
    for (i = 0; i < 60u; i++)
        CHECK(sdx_pass_step(buf, sizeof buf, SLICE_MS) == FR_OK, "%s: a slice", what);
    CHECK(sdx_pass_on() && sdx_open() == FR_OK && sdx_find_path("Samples/sample 02.wav", &rec) >= 0,
          "%s: a lookup mid-pass reads the old index", what);
    seq0 = sdx.seq;
    CHECK(sdx_touch(0) == FR_OK && sdx.seq == seq0 && sdx_pass_on(), "%s: a use mid-pass: not kept, the pass goes on", what);
    k0 = sdxp.k;
    sd_wr.state = SDW_READY;
    CHECK(sdx_pass_step(buf, sizeof buf, SLICE_MS) == FR_OK && sdxp.k == k0, "%s: a recording holds the pass", what);
    sd_wr.state = SDW_IDLE;
    CHECK(by_slices(&s) == FR_OK && !sdx_pass_on(), "%s: the pass ends", what);

    /* a file the FM-1 writes mid-pass: same size, same date (the FM-1 has no clock): only sdx_note_written can tell */
    no = sdx_find_path("Samples/sample 02.wav", &rec);
    was = rec.hash;
    (void)sdx_pass_begin(1);
    for (i = 0; i < 60u; i++)
        CHECK(sdx_pass_step(buf, sizeof buf, SLICE_MS) == FR_OK, "%s: a slice", what);
    CHECK(f_stat("Optimist/Samples/sample 02.wav", &fi) == FR_OK, "%s: stat", what);    /* (the model's date a constant) */
    wav("Optimist/Samples/sample 02.wav", (uint32_t)fi.fsize - 44u, 123);
    CHECK(sdx_note_written("Optimist/Samples/sample 02.wav", buf, sizeof buf) == FR_OK, "%s: noted", what);
    CHECK(sdx_pass_on(), "%s: the pass starts again", what);
    CHECK(by_slices(&s) == FR_OK && sdx_open() == FR_OK && (no = sdx_find_path("Samples/sample 02.wav", &rec)) >= 0 &&
          rec.hash != was, "%s: the record right, the new hash", what);
    CHECK(sdx.hashed == 0, "%s: nothing hashed twice (%u)", what, (unsigned)sdx.hashed);
}

static void remount(const char *what, int fsinfo)
{
    uint32_t t0;
    slices_t s;
    CHECK(sdx_pass_begin(1) == 1 && by_slices(&s) == FR_OK, "%s: a pass", what);
    CHECK(mount() == FR_OK, "%s: mounted again", what);
    t0 = clk;
    if (!fsinfo) {
        CHECK(sdx_pass_begin(0) == 1, "%s: no FSINFO: walked", what);
        sdx_pass_cancel();
        return;
    }
    CHECK(sdx_pass_begin(0) == 0 && !sdx_pass_on() && sdx.files == 0, "%s: unchanged: no walk", what);
    printf("sd_index_pass_test: %s: a mount of an unchanged card: %u us in the index\n", what, (unsigned)(clk - t0));
    CHECK(clk - t0 < 20000u, "%s: under 20 ms (%u us)", what, (unsigned)(clk - t0));
    CHECK(sdx_open() == FR_OK && sdx.count > 400u, "%s: the index there (%u)", what, (unsigned)sdx.count);
    CHECK(sdx_pass_begin(1) == 1, "%s: forced: walked", what);
    sdx_pass_cancel();
    small("Optimist/Kits/Factory/added.opk", 901);
    CHECK(mount() == FR_OK, "%s: mounted again", what);
    CHECK(sdx_pass_begin(0) == 1, "%s: a file added: walked", what);
    CHECK(by_slices(&s) == FR_OK && sdx_open() == FR_OK && sdx.hashed == 1, "%s: that file alone hashed (%u)", what,
          (unsigned)sdx.hashed);
}

/* a card used from its start, the hint at its start: FatFs's f_expand alone would read the whole used FAT in one call */
static void low_hint(const char *what, uint8_t *img, uint8_t *snap, uint32_t bytes)
{
    static uint8_t mb[4096];
    slices_t s;
    FIL f;
    UINT k;
    uint32_t i;
    double scale = 1000.0 / 840.0;
    memcpy(snap, img, bytes);
    CHECK(f_open(&f, "fill.bin", FA_CREATE_ALWAYS | FA_WRITE) == FR_OK, "%s: a big file", what);
    for (i = 0; i < 20u * 256u; i++)
        CHECK(f_write(&f, mb, sizeof mb, &k) == FR_OK && k == sizeof mb, "%s: its bytes", what);
    CHECK(f_close(&f) == FR_OK, "%s: closed", what);
    fs.last_clst = 2u;                               /* (what the computer's FSINFO says) */
    CHECK(sdx_pass_begin(1) == 1 && by_slices(&s) == FR_OK, "%s: a pass, the hint low", what);
    printf("sd_index_pass_test: %s: a low hint, 20 MB used: the longest slice %.1f ms, the file operations' %.1f ms at 840 KB/s\n",
           what, s.max_us * scale / 1000.0, s.file_us * scale / 1000.0);
    CHECK(s.max_us * scale <= 22000.0, "%s: no slice over ~20 ms with the hint low (%.1f ms)", what, s.max_us * scale / 1000.0);
    CHECK(s.file_us * scale <= 20000.0, "%s: index.tmp's creation under 20 ms with the hint low (%.1f ms; the FAT in one call: 30)", what,
          s.file_us * scale / 1000.0);
    memcpy(img, snap, bytes);
    CHECK(mount() == FR_OK, "%s: mounted again", what);
}

static void measure(const char *what, uint8_t *img, uint8_t *snap, uint32_t bytes)
{
    slices_t s;
    double scale = 1000.0 / 840.0;                   /* (the model's 1 MB/s to the card's 840 KB/s) */
    double slices;
    memcpy(snap, img, bytes);
    CHECK(f_unlink(SDX_FILE) == FR_OK || 1, "%s: no index", what);
    CHECK(sdx_pass_begin(1) == 1 && by_slices(&s) == FR_OK, "%s: a first pass", what);
    slices = s.busy_us * scale / (SLICE_MS * 1000.0);
    printf("sd_index_pass_test: %s: a first pass: %.0f ms on the card at 840 KB/s (write busy %u us), the longest slice %.1f ms; "
           "%u files; %.0f frames of %.1f ms with %u ms of bus each: %.1f s\n", what, s.busy_us * scale / 1000.0,
           (unsigned)WBUSY_US, s.max_us * scale / 1000.0, (unsigned)sdx.files, slices, FRAME_MS, (unsigned)SLICE_MS,
           slices * FRAME_MS / 1000.0);
    CHECK(s.max_us * scale <= 22000.0, "%s: no slice over ~20 ms at 840 KB/s (%.1f ms)", what, s.max_us * scale / 1000.0);
    /* (index.tmp removed or made, index.bin removed, one call each: the model's clusters are 512 bytes, a real card's 32 KB:
     * its index a run of 2-8 clusters, these a FAT sector or two) */
    CHECK(s.file_us * scale <= 35000.0, "%s: no file operation over 35 ms on 512-byte clusters (%.1f ms)", what,
          s.file_us * scale / 1000.0);
    memcpy(img, snap, bytes);
    CHECK(mount() == FR_OK, "%s: back", what);
}

static void card_run(const char *what, uint8_t *img, uint32_t sectors, uint32_t base, int hc, int fsinfo)
{
    char path[256] = "";
    uint32_t bytes = sectors * 512u;
    uint8_t *snap = malloc(bytes);
    CHECK(snap != 0, "%s: room", what);
    card(img, sectors, base, 1, hc, 0);
    C.wbusy = WBUSY_US;
    CHECK(mount() == FR_OK, "%s: mount", what);
    CHECK(empty_dir(path, 0) == FR_OK, "%s: the card emptied", what);
    the_card();
    same_as_old("a first pass", img, snap, bytes);
    change_some();
    same_as_old("over an old index", img, snap, bytes);
    meanwhile(what);
    pulled(what);
    remount(what, fsinfo);
    if (fs.fs_type == FS_FAT32)
        low_hint(what, img, snap, bytes);
    measure(what, img, snap, bytes);
    free(snap);
}

int main(int argc, char **argv)
{
    uint32_t n16, n32;
    uint8_t *i16, *i32;
    if (argc < 3) { printf("usage: sd_index_pass_test FAT16.img FAT32.img\n"); return 2; }
    i16 = load(argv[1], &n16);
    i32 = load(argv[2], &n32);
    card_run("SDHC FAT32 + MBR", i32, n32, 2048, 1, 1);
    card_run("SDSC FAT16", i16, n16, 0, 0, 0);
    printf("sd_index_pass_test: %d checks, %d failed\n", checks, fails);
    free(i16); free(i32);
    return fails ? 1 : 0;
}
