/* SPDX-License-Identifier: GPL-3.0-only */
/* The card's own layout on full FatFs (docs/SD-CARD-DESIGN.md 8; firmware/src/storage/sd/sd_layout.c, sd_hash.c,
 * sd_alloc.c, sd_index.c, sd_browse.c, sd_save.c) on the byte-level card model (tests/sd_card_model.h), the FAT32 and
 * FAT16 images (512-byte clusters: every file of more than one is a chain, so a run that is not one shows):
 *   - XXH64 against its published vectors, any split; a WAV's identity its data chunk only;
 *   - the folders made on a fresh card, names, the tree walk (a computer's hidden files skipped);
 *   - the index: a pass hashes new and changed files only, a moved sample found by its hash, a file the FM-1 wrote
 *     hashed at once, the same index with or without the work table, a power cut at every write of a pass;
 *   - the 16-key pages: every view's entries each shown once, in order, forward and back, names cut on a UTF-8
 *     boundary and sorted by the cut, a slot's whole path found again;
 *   - the staged save: versions (N kept), the spare run reused, a power cut at every block write: the card holds the
 *     old project or the new one whole, never a mix, the boot finishes the new one, no cluster in two files;
 *   - every file the FM-1 wrote one contiguous run, on a card whose free room is in holes.
 *   sd_layout_test FAT16.img FAT32.img */
#define FELUCCA_SD_FATFS 1
#include "sd_card_model.h"
#include "storage/sd/sd_mmc.c"
#include "storage/sd/sd_diskio.c"
#include "storage/sd/sd_layout.c"
#include "storage/sd/sd_hash.c"
#include "storage/sd/sd_alloc.c"
#include "storage/sd/sd_index.c"
#include "storage/sd/sd_browse.c"
#include "storage/sd/sd_save.c"

static FATFS fs __attribute__((aligned(4)));
static uint8_t buf[512] __attribute__((aligned(4)));
static FRESULT mount(void) { memset(&fs, 0, sizeof fs); return f_mount(&fs, "", 1); }

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

static uint32_t runs_of(const char *path)          /* a file's runs: 1 = contiguous, 0 = empty or none */
{
    FIL f;
    DWORD map[4];
    uint32_t n = 0;
    if (f_open(&f, path, FA_READ) != FR_OK)
        return 0;
    map[0] = 4;
    f.cltbl = map;
    if (f_lseek(&f, CREATE_LINKMAP) == FR_OK || map[0] > 4)
        n = (map[0] - 2u) / 2u;
    f.cltbl = 0;
    f_close(&f);
    return n;
}

/* every file under path: one run each (when want_one), and no cluster in two files (a cross-link) */
static uint16_t *owner;
static char names[4096][96];
static int nnames;
static int cross, many;
static void walk_check(char *path, uint32_t len, int want_one, const char *what)
{
    static DWORD map[1024];
    DIR d;
    FILINFO fi;
    FIL f;
    uint32_t i, c;
    if (f_opendir(&d, path) != FR_OK)
        return;
    while (f_readdir(&d, &fi) == FR_OK && fi.fname[0]) {
        snprintf(path + len, 256 - len, "%s%s", len ? "/" : "", fi.fname);
        if (fi.fattrib & AM_DIR) {
            walk_check(path, (uint32_t)strlen(path), want_one, what);
        } else if (f_open(&f, path, FA_READ) == FR_OK) {
            map[0] = 1024;
            f.cltbl = map;
            if (fi.fsize && f_lseek(&f, CREATE_LINKMAP) == FR_OK) {
                if (want_one && map[0] != 4u) {
                    many++;
                    printf("  %s: %s in %u runs\n", what, path, (unsigned)(map[0] - 2u) / 2u);
                }
                if (nnames < 4095)
                    snprintf(names[++nnames], 96, "%s", path);
                for (i = 1; map[i]; i += 2)
                    for (c = 0; c < map[i]; c++) {
                        if (owner[map[i + 1] + c] && cross < 3)
                            printf("  %s: cluster %u in %s and %s\n", what, (unsigned)(map[i + 1] + c),
                                   names[owner[map[i + 1] + c]], path);
                        cross += owner[map[i + 1] + c] != 0;
                        owner[map[i + 1] + c] = (uint16_t)nnames;
                    }
            }
            f.cltbl = 0;
            f_close(&f);
        }
        path[len] = 0;
    }
    f_closedir(&d);
}

/* the folders only the FM-1 writes in (SAMPLES: the test's own files stand for a computer's) */
static const char *const FM1_DIRS[] = {"OPTIMIST/PROJECTS", "OPTIMIST/SYSTEM", "OPTIMIST/RECORD", "OPTIMIST/PRESETS"};

static void fat_check(int want_one, const char *what)
{
    char path[256];
    uint32_t d;
    owner = calloc(fs.n_fatent, 2);
    cross = many = nnames = 0;
    snprintf(path, sizeof path, "%s", "");
    walk_check(path, 0, 0, what);                    /* (the whole card for the cross-links) */
    free(owner);
    CHECK(cross == 0, "%s: %d clusters in two files", what, cross);
    for (d = 0; want_one && d < sizeof FM1_DIRS / sizeof FM1_DIRS[0]; d++) {
        owner = calloc(fs.n_fatent, 2);
        many = nnames = 0;
        snprintf(path, sizeof path, "%s", FM1_DIRS[d]);
        walk_check(path, (uint32_t)strlen(path), 1, what);
        free(owner);
        CHECK(many == 0, "%s: %d files the FM-1 wrote in %s not one run", what, many, FM1_DIRS[d]);
    }
}

static FRESULT empty_dir(char *path, UINT len)
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

/* the card's free room in holes of one cluster: n one-cluster files from the start, every other one deleted */
static void holes(uint32_t n)
{
    char name[24];
    uint8_t one[512];
    uint32_t i;
    memset(one, 0x5A, sizeof one);
    f_mkdir("HOLES");
    fs.last_clst = 2;
    for (i = 0; i < n; i++) {
        snprintf(name, sizeof name, "HOLES/H%04u", (unsigned)i);
        put_file(name, one, (uint32_t)fs.csize * 512u);
    }
    for (i = 0; i < n; i += 2) {
        snprintf(name, sizeof name, "HOLES/H%04u", (unsigned)i);
        f_unlink(name);
    }
    fs.last_clst = 2;                                /* (FatFs' own growth would now take the holes) */
}

/* ---- the hash ---------------------------------------------------------------------------------------------------- */

static uint64_t xxh(const void *p, uint32_t n, uint32_t step)
{
    sdh_t s;
    uint32_t i;
    sdh_init(&s);
    for (i = 0; i < n; i += step)
        sdh_add(&s, (const uint8_t *)p + i, n - i < step ? n - i : step);
    return sdh_end(&s);
}

static uint32_t wav(uint8_t *out, const uint8_t *pcm, uint32_t n, int extra)   /* a WAV around n bytes of data */
{
    uint32_t at = 0, fmt = 16;
    memcpy(out, "RIFF\0\0\0\0WAVEfmt ", 16);
    at = 16;
    out[at++] = (uint8_t)fmt; out[at++] = 0; out[at++] = 0; out[at++] = 0;
    memset(out + at, 0, fmt);
    out[at] = 1; out[at + 2] = 1; out[at + 4] = 0x44; out[at + 5] = 0xAC; out[at + 14] = 16;
    at += fmt;
    if (extra) {                                     /* a LIST chunk of odd length (padded), as an editor adds */
        memcpy(out + at, "LIST\x07\0\0\0INFOabc\0", 16);
        at += 16;
    }
    memcpy(out + at, "data", 4);
    out[at + 4] = (uint8_t)n; out[at + 5] = (uint8_t)(n >> 8); out[at + 6] = (uint8_t)(n >> 16); out[at + 7] = 0;
    at += 8;
    memcpy(out + at, pcm, n);
    at += n;
    out[4] = (uint8_t)(at - 8); out[5] = (uint8_t)((at - 8) >> 8); out[6] = (uint8_t)((at - 8) >> 16);
    return at;
}

static void hashes(void)
{
    static const struct { const char *s; uint64_t h; } V[] = {   /* XXH64 seed 0 (xxHash's and python-xxhash's) */
        {"", 0xEF46DB3751D8E999ull}, {"a", 0xD24EC4F1A98C6E5Bull}, {"abc", 0x44BC2CF5AD770999ull},
        {"xxhash", 0x32DD38952C4BC720ull}, {"Nobody inspects the spammish repetition", 0xFBCEA83C8A378BF1ull}};
    static uint8_t pcm[5000], w1[6000], w2[6000];
    FIL f;
    uint64_t h1 = 0, h2 = 1, h3 = 2;
    uint32_t i, n1, n2, l1 = 0, l2 = 0, l3 = 0;
    for (i = 0; i < sizeof V / sizeof V[0]; i++) {
        uint32_t n = (uint32_t)strlen(V[i].s), st;
        for (st = 1; st <= 40; st += 3)
            CHECK(xxh(V[i].s, n, st) == V[i].h, "XXH64(\"%s\") in pieces of %u", V[i].s, (unsigned)st);
    }
    for (i = 0; i < sizeof pcm; i++)
        pcm[i] = (uint8_t)(i * 31u + (i >> 7));
    CHECK(xxh(pcm, sizeof pcm, 5000) == xxh(pcm, sizeof pcm, 7) && xxh(pcm, sizeof pcm, 33) == xxh(pcm, 5000, 512),
          "XXH64 of 5000 bytes: the same in any pieces");
    n1 = wav(w1, pcm, 4999, 0);
    n2 = wav(w2, pcm, 4999, 1);
    CHECK(put_file("W1.WAV", w1, n1) == FR_OK && put_file("W2.WAV", w2, n2) == FR_OK && put_file("RAW.BIN", pcm, 4999) ==
          FR_OK, "hash: three files written");
    CHECK(f_open(&f, "W1.WAV", FA_READ) == FR_OK && sdh_file(&f, buf, sizeof buf, &h1, &l1) == FR_OK, "hash: W1");
    f_close(&f);
    CHECK(f_open(&f, "W2.WAV", FA_READ) == FR_OK && sdh_file(&f, buf, sizeof buf, &h2, &l2) == FR_OK, "hash: W2");
    f_close(&f);
    CHECK(f_open(&f, "RAW.BIN", FA_READ) == FR_OK && sdh_file(&f, buf, sizeof buf, &h3, &l3) == FR_OK, "hash: RAW");
    f_close(&f);
    CHECK(h1 == h2 && l1 == 4999 && l2 == 4999, "a WAV's hash: its data chunk's (another header, the same sound)");
    CHECK(h1 == xxh(pcm, 4999, 4999) && h3 == h1 && l3 == 4999, "... = the data's XXH64; a non-WAV whole");
    pcm[4000] ^= 1;
    n1 = wav(w1, pcm, 4999, 0);
    CHECK(put_file("W1.WAV", w1, n1) == FR_OK && f_open(&f, "W1.WAV", FA_READ) == FR_OK &&
          sdh_file(&f, buf, sizeof buf, &h1, &l1) == FR_OK && h1 != h2, "hash: one sample changed, another hash");
    f_close(&f);
    f_unlink("W1.WAV");
    f_unlink("W2.WAV");
    f_unlink("RAW.BIN");
}

/* ---- the layout ---------------------------------------------------------------------------------------------------- */

static void layout(void)
{
    char p[40];
    FILINFO fi;
    uint32_t i;
    CHECK(sdl_prepare() == FR_OK, "layout: the folders made");
    for (i = 0; i < SDL_N; i++) {
        sdl_path(p, sizeof p, i, 0);
        CHECK(f_stat(p, &fi) == FR_OK && (fi.fattrib & AM_DIR), "layout: %s", p);
    }
    CHECK(sdl_prepare() == FR_OK, "layout: made again: found");
    CHECK(sdl_path(p, sizeof p, SDL_KITS, "909 house") == 23 && !strcmp(p, "OPTIMIST/KITS/909 house"), "a path: %s", p);
    CHECK(sdl_path(p, 12, SDL_KITS, "x") == 0 && !p[0], "a path that does not fit: 0, empty");
    CHECK(sdl_name_ok("Night Drive") && sdl_name_ok("\xc3\xa9t\xc3\xa9 2") && sdl_name_ok(".hidden"),
          "names: spaces, UTF-8, a leading dot");
    CHECK(!sdl_name_ok("") && !sdl_name_ok(".") && !sdl_name_ok("..") && !sdl_name_ok("a/b") && !sdl_name_ok("a:b") &&
          !sdl_name_ok("a?") && !sdl_name_ok("end.") && !sdl_name_ok("end ") && !sdl_name_ok("tab\t"),
          "names: refused (separators, wildcards, trailing dot / space, control)");
    CHECK(sdl_name_ok("0123456789012345678901234567890123456789012345678901234567890AB") &&
          !sdl_name_ok("0123456789012345678901234567890123456789012345678901234567890ABC"), "names: 63 bytes at most");
    CHECK(f_unlink("OPTIMIST/KITS") == FR_OK && put_file("OPTIMIST/KITS", "x", 1) == FR_OK &&
          sdl_prepare() == FR_DENIED, "layout: a file where a folder must be: FR_DENIED");
    CHECK(f_unlink("OPTIMIST/KITS") == FR_OK && sdl_prepare() == FR_OK, "layout: the folder made again");
}

/* ---- the index ------------------------------------------------------------------------------------------------------ */

static uint8_t smp(uint32_t i, uint32_t k) { return (uint8_t)(i * (7u + k) + k * 13u + (i >> 9)); }

static void sample(const char *path, uint32_t k, uint32_t n)
{
    static uint8_t pcm[3000], w[3100];
    uint32_t i;
    for (i = 0; i < n; i++)
        pcm[i] = smp(i, k);
    CHECK(put_file(path, w, wav(w, pcm, n, (int)(k & 1u))) == FR_OK, "sample %s", path);
}

static uint64_t sample_hash(uint32_t k, uint32_t n)
{
    static uint8_t pcm[3000];
    uint32_t i;
    for (i = 0; i < n; i++)
        pcm[i] = smp(i, k);
    return xxh(pcm, n, n ? n : 1);
}

static uint32_t work[64];

static void samples(void)
{
    CHECK(f_mkdir("OPTIMIST/SAMPLES/drums") == FR_OK && f_mkdir("OPTIMIST/SAMPLES/drums/909") == FR_OK &&
          f_mkdir("OPTIMIST/SAMPLES/keys") == FR_OK, "samples: folders");
    sample("OPTIMIST/SAMPLES/drums/909/kick.wav", 1, 2000);
    sample("OPTIMIST/SAMPLES/drums/909/snare.wav", 2, 1500);
    sample("OPTIMIST/SAMPLES/drums/clap long name.wav", 3, 900);
    sample("OPTIMIST/SAMPLES/keys/rhodes C3.wav", 4, 2999);
    sample("OPTIMIST/SAMPLES/top.wav", 5, 100);
    CHECK(put_file("OPTIMIST/SAMPLES/drums/._kick.wav", "junk", 4) == FR_OK, "a computer's ._ file");
}

static void index_tests(const char *what)
{
    sdx_rec rec;
    int32_t no;
    FILINFO fi;
    uint32_t cnt;

    samples();
    CHECK(sdx_open() == FR_NO_FILE, "%s: no index yet", what);
    CHECK(sdx_update(buf, sizeof buf, work, 64) == FR_OK, "%s: the first pass", what);
    CHECK(sdx.files == 5 && sdx.hashed == 5 && sdx.kept == 0 && sdx.count == 5, "%s: 5 files hashed (%u %u %u)", what,
          (unsigned)sdx.files, (unsigned)sdx.hashed, (unsigned)sdx.kept);
    CHECK(sdx_open() == FR_OK && sdx.count == 5 && sdx.cap >= 5, "%s: the index read back", what);
    CHECK(runs_of(SDX_FILE) == 1, "%s: the index one run", what);
    CHECK(f_stat(SDX_TMP, &fi) == FR_NO_FILE, "%s: no INDEX.TMP left", what);
    no = sdx_find_path("SAMPLES/keys/rhodes C3.wav", &rec);
    CHECK(no >= 0 && rec.hash == sample_hash(4, 2999) && rec.hlen == 2999, "%s: a record by its path, its hash", what);
    CHECK(sdx_find_hash(sample_hash(1, 2000), &rec) >= 0 && !strcmp(rec.path, "SAMPLES/drums/909/kick.wav"),
          "%s: a sample by its hash: %s", what, rec.path);
    CHECK(sdx_find_path("SAMPLES/drums/._kick.wav", &rec) < 0, "%s: the ._ file not indexed", what);

    CHECK(sdx_update(buf, sizeof buf, work, 64) == FR_OK && sdx.hashed == 0 && sdx.kept == 5,
          "%s: a pass again: nothing hashed (%u)", what, (unsigned)sdx.hashed);
    sample("OPTIMIST/SAMPLES/drums/909/snare.wav", 6, 1400);   /* changed "elsewhere": another size */
    CHECK(f_rename("OPTIMIST/SAMPLES/top.wav", "OPTIMIST/SAMPLES/keys/moved top.wav") == FR_OK, "%s: moved", what);
    sample("OPTIMIST/SAMPLES/new.wav", 7, 640);
    CHECK(sdx_update(buf, sizeof buf, 0, 0) == FR_OK && sdx.files == 6 && sdx.hashed == 3 && sdx.kept == 3,
          "%s: a pass without the work table: the changed, the moved and the new hashed (%u %u %u)", what,
          (unsigned)sdx.files, (unsigned)sdx.hashed, (unsigned)sdx.kept);
    CHECK(sdx_find_hash(sample_hash(5, 100), &rec) >= 0 && !strcmp(rec.path, "SAMPLES/keys/moved top.wav"),
          "%s: the moved sample found by its hash where it is now", what);
    CHECK(sdx_find_hash(sample_hash(2, 1500), &rec) < 0 && sdx_find_hash(sample_hash(6, 1400), &rec) >= 0,
          "%s: the changed one by its new hash only", what);

    no = sdx_find_path("SAMPLES/drums/909/kick.wav", &rec);
    CHECK(no >= 0 && sdx_touch((uint32_t)no) == FR_OK && sdx_read((uint32_t)no, &rec) == FR_OK && rec.used == sdx.seq,
          "%s: used: its number %u", what, (unsigned)rec.used);
    sample("OPTIMIST/SAMPLES/drums/909/kick.wav", 8, 2000);    /* the FM-1 rewrote it (the same size, the same date) */
    CHECK(sdx_note_written("OPTIMIST/SAMPLES/drums/909/kick.wav", buf, sizeof buf) == FR_OK, "%s: noted", what);
    CHECK(sdx_find_hash(sample_hash(8, 2000), &rec) == no && rec.used == sdx.seq,
          "%s: the FM-1's write hashed at once, its use kept", what);
    CHECK(sdx_update(buf, sizeof buf, work, 64) == FR_OK && sdx.hashed == 0,
          "%s: ... and a pass keeps it (its date and size as recorded): %u hashed", what, (unsigned)sdx.hashed);
    cnt = sdx.count;
    sample("OPTIMIST/SAMPLES/rec 1.wav", 9, 300);
    CHECK(sdx_note_written("OPTIMIST/SAMPLES/rec 1.wav", buf, sizeof buf) == FR_OK && sdx.count == cnt + 1u &&
          sdx_find_hash(sample_hash(9, 300), &rec) >= 0, "%s: a new file the FM-1 wrote: a record added", what);
    CHECK(sdx_note_written("OPTIMIST/KITS/x", buf, sizeof buf) == FR_INVALID_NAME, "%s: no sample: refused", what);
    CHECK(runs_of(SDX_FILE) == 1, "%s: the index still one run", what);
    while (sdx.count < sdx.cap) {                    /* the run full: a note is refused until a pass makes room */
        char name[48];
        snprintf(name, sizeof name, "OPTIMIST/SAMPLES/fill %u.wav", (unsigned)sdx.count);
        sample(name, 10 + sdx.count, 50);
        CHECK(sdx_note_written(name, buf, sizeof buf) == FR_OK, "%s: %s noted", what, name);
    }
    sample("OPTIMIST/SAMPLES/one more.wav", 99, 50);
    CHECK(sdx_note_written("OPTIMIST/SAMPLES/one more.wav", buf, sizeof buf) == FR_DENIED, "%s: no room: FR_DENIED",
          what);
    cnt = sdx.count + 1u;
    CHECK(sdx_update(buf, sizeof buf, work, 64) == FR_OK && sdx.count == cnt && sdx.hashed == 1,
          "%s: the pass takes it (%u records, %u hashed)", what, (unsigned)sdx.count, (unsigned)sdx.hashed);
}

/* a power cut at every block write of a pass: after it the index reads (the old or the new), a pass again is right */
static void index_cuts(const char *what, uint8_t *img, uint32_t sectors, uint32_t base)
{
    uint8_t *snap = malloc((size_t)sectors * 512u);
    long k, w0, total;
    uint32_t want;
    int bad = 0;
    sample("OPTIMIST/SAMPLES/cut test.wav", 77, 2500);
    memcpy(snap, img, (size_t)sectors * 512u);
    w0 = C.writes;
    CHECK(sdx_update(buf, sizeof buf, work, 64) == FR_OK, "%s: the pass (uncut)", what);
    want = sdx.count;
    total = C.writes - w0;
    for (k = 0; k < total; k++) {
        memcpy(img, snap, (size_t)sectors * 512u);
        card_power_on();
        if (mount() != FR_OK) { bad++; continue; }
        C.cut_at = C.writes + k;
        (void)sdx_update(buf, sizeof buf, work, 64);
        card_power_on();
        if (mount() != FR_OK || sdx_open() != FR_OK) { bad++; continue; }
        if (sdx.count != want && sdx.count != want - 1u) { bad++; continue; }
        if (sdx_update(buf, sizeof buf, work, 64) != FR_OK || sdx.count != want ||
            sdx_find_hash(sample_hash(77, 2500), &sdx.rec) < 0)
            bad++;
        fat_check(1, what);                          /* (no cluster in two files: a cut rename's halves) */
    }
    CHECK(total > 4 && bad == 0, "%s: a cut at each of the pass's %ld writes: %d left the index wrong", what, total, bad);
    memcpy(img, snap, (size_t)sectors * 512u);
    card_power_on();
    (void)base;
    free(snap);
    CHECK(mount() == FR_OK, "%s: mount after the cuts", what);
}

/* ---- the 16-key pages ----------------------------------------------------------------------------------------------- */

static int seen[200];

/* walks the view's pages to the end and back: each entry once each way, in order; the slots' count */
static uint32_t pages_check(sdb_t *b, const char *what)
{
    sdb_item last;
    uint32_t n = 0, p, i, back = 0;
    int ordered = 1, have = 0;
    memset(seen, 0, sizeof seen);
    for (p = 0;; p++) {
        CHECK(b->page == p, "%s: page %u (%u)", what, (unsigned)p, (unsigned)b->page);
        for (i = 0; i < b->n; i++) {
            if (have && sdb_cmp(b->view, &last, &b->slot[i]) >= 0)
                ordered = 0;
            last = b->slot[i];
            have = 1;
            if (b->slot[i].ord < 200)
                seen[b->slot[i].ord]++;
            n++;
        }
        if (b->page + 1u >= b->pages)
            break;
        CHECK(sdb_turn(b, 1) == FR_OK, "%s: turned on", what);
    }
    CHECK(ordered, "%s: the entries in order across the pages", what);
    CHECK(n == b->total && b->pages == (b->total + 15u) / 16u + !b->total, "%s: %u shown of %u, %u pages", what,
          (unsigned)n, (unsigned)b->total, (unsigned)b->pages);
    for (i = 0; i < 200; i++)
        CHECK(seen[i] <= 1, "%s: entry %u shown %d times", what, (unsigned)i, seen[i]);
    while (b->page) {                                /* and back */
        uint32_t page = b->page;
        sdb_item first = b->slot[0];
        CHECK(sdb_turn(b, -1) == FR_OK && b->page == page - 1u && b->n == 16 &&
              sdb_cmp(b->view, &b->slot[15], &first) < 0, "%s: back to page %u", what, (unsigned)page - 1u);
        back += b->n;
    }
    CHECK(back + (n - back) == n && b->page == 0, "%s: back at the first page", what);
    return n;
}

static void browse_tests(const char *what)
{
    static sdb_t b;
    char name[96], path[256];
    uint32_t i, k, n;
    FILINFO fi;
    static const char LONG[] = "an extremely long sample name that goes on and on past the slot ";   /* 64 B */
    CHECK(f_mkdir("OPTIMIST/SAMPLES/many") == FR_OK, "%s: a folder of many", what);
    for (i = 0; i < 37; i++) {                       /* names out of order, mixed case */
        snprintf(name, sizeof name, "OPTIMIST/SAMPLES/many/%c%02u.wav", (i * 7u % 37u) & 1 ? 'S' : 's',
                 (unsigned)(i * 7u % 37u));
        sample(name, i, 10);
    }
    for (i = 0; i < 3; i++) {                        /* names equal in their first 47 bytes, then the UTF-8 cut */
        snprintf(name, sizeof name, "OPTIMIST/SAMPLES/many/%s%u.wav", LONG, (unsigned)(2 - i));
        sample(name, 50 + i, 10);
    }
    snprintf(name, sizeof name, "OPTIMIST/SAMPLES/many/%.45s\xc3\xa9\xc3\xa9.wav", LONG);   /* é across the cut */
    sample(name, 60, 10);
    f_mkdir("OPTIMIST/SAMPLES/many/zz sub");
    f_mkdir("OPTIMIST/SAMPLES/many/AA sub");
    put_file("OPTIMIST/SAMPLES/many/._s01.wav", "x", 1);

    CHECK(sdb_open(&b, SDB_FOLDER, "OPTIMIST/SAMPLES/many") == FR_OK && b.total == 43 && b.pages == 3,
          "%s: a folder of 43 entries: 3 pages (%u, %u)", what, (unsigned)b.total, (unsigned)b.pages);
    CHECK(b.slot[0].dir && b.slot[1].dir && !strcmp(b.slot[0].name, "AA sub") && !strcmp(b.slot[1].name, "zz sub") &&
          !b.slot[2].dir, "%s: its folders first, by name", what);
    n = pages_check(&b, "folder view");
    CHECK(n == 43, "%s: every entry once (%u)", what, (unsigned)n);
    CHECK(sdb_open(&b, SDB_FOLDER, "OPTIMIST/SAMPLES/many") == FR_OK, "%s: open again", what);
    for (k = 0, i = 0; i < b.n; i++) {
        if (!b.slot[i].cut)
            continue;
        k++;
        CHECK(strlen(b.slot[i].name) <= 47 && ((uint8_t)b.slot[i].name[strlen(b.slot[i].name) - 1] & 0xC0) != 0xC0,
              "%s: a cut name ends on a UTF-8 boundary: %s", what, b.slot[i].name);
        CHECK(sdb_path(&b, i, path, sizeof path) && f_stat(path, &fi) == FR_OK && !(fi.fattrib & AM_DIR),
              "%s: a cut slot's whole path: %s", what, path);
    }
    CHECK(k == 4, "%s: 4 names cut (%u)", what, (unsigned)k);
    CHECK(sdb_path(&b, 5, path, sizeof path) && f_stat(path, &fi) == FR_OK, "%s: a slot's path: %s", what, path);
    CHECK(sdb_into(&b, 0) == FR_OK && !strcmp(b.root, "OPTIMIST/SAMPLES/many/AA sub") && b.total == 0 && b.n == 0 &&
          b.pages == 1, "%s: into an empty folder", what);
    CHECK(sdb_up(&b) == FR_OK && b.total == 43 && sdb_into(&b, 5) == FR_NO_PATH, "%s: up; a file is no folder", what);
    CHECK(sdb_open(&b, SDB_FOLDER, "OPTIMIST") == FR_OK && sdb_up(&b) == FR_NO_PATH, "%s: never above OPTIMIST", what);
    CHECK(sdb_turn(&b, 5) == FR_OK && b.page == 0, "%s: one page: turning stays", what);

    CHECK(sdb_open(&b, SDB_BYNAME, "OPTIMIST/SAMPLES") == FR_OK && !b.slot[0].dir, "%s: by name, every folder", what);
    n = pages_check(&b, "name view");
    CHECK(n == b.total && n >= 41 + 6, "%s: every sample once (%u)", what, (unsigned)n);
    CHECK(sdb_open(&b, SDB_BYNAME, "OPTIMIST/SAMPLES") == FR_OK && sdb_turn(&b, 2) == FR_OK && b.page == 2 &&
          sdb_path(&b, 3, path, sizeof path) && f_stat(path, &fi) == FR_OK, "%s: page 2, a slot's path %s", what, path);
    CHECK(sdb_turn(&b, 99) == FR_OK && b.page == b.pages - 1u && sdb_turn(&b, -99) == FR_OK && b.page == 0,
          "%s: turned past the ends: clamped", what);

    CHECK(sdx_update(buf, sizeof buf, work, 64) == FR_OK, "%s: the index for the recent view", what);
    for (i = 0; i < 20; i++) {                       /* 20 uses, the last one first */
        snprintf(name, sizeof name, "SAMPLES/many/%c%02u.wav", i & 1 ? 'S' : 's', (unsigned)i);
        int32_t no = sdx_find_path(name, &sdx.rec);
        CHECK(no >= 0 && sdx_touch((uint32_t)no) == FR_OK, "%s: used %s", what, name);
    }
    CHECK(sdb_open(&b, SDB_RECENT, 0) == FR_OK && b.total == 21 && b.n == 16 && !strcmp(b.slot[0].name, "S19.wav") &&
          !strcmp(b.slot[15].name, "s04.wav"), "%s: recent: the last used first (%s, %s)", what, b.slot[0].name,
          b.slot[15].name);
    n = pages_check(&b, "recent view");
    CHECK(n == 21, "%s: recent: 21 (the 20, the kick used before)", what);
    CHECK(sdb_open(&b, SDB_RECENT, 0) == FR_OK && sdb_path(&b, 0, path, sizeof path) &&
          !strcmp(path, "OPTIMIST/SAMPLES/many/S19.wav"), "%s: a recent slot's path %s", what, path);
}

/* ---- the staged save ------------------------------------------------------------------------------------------------ */

static uint8_t stage[60000];                         /* the FM-1's flash copy */
static sdp_pending pend;
static int pend_on;
static int st_read(uint32_t at, void *p, uint32_t n) { memcpy(p, stage + at, n); return 0; }
static int st_set(const sdp_pending *p) { pend = *p; pend_on = 1; return 0; }
static int st_get(sdp_pending *p) { *p = pend; return pend_on; }
static int st_clear(void) { pend_on = 0; return 0; }

static void project(uint32_t seed, uint32_t len)     /* a project of len bytes staged */
{
    uint32_t i;
    for (i = 0; i < len; i++)
        stage[i] = (uint8_t)(i * 17u + seed * 101u + (i >> 8) * seed);
}

static int picked(const char *name, uint32_t number, uint32_t seed, uint32_t len)   /* the pick: that save, whole */
{
    sdp_head h;
    char path[256];
    FIL f;
    UINT got;
    uint32_t i, at;
    int ok;
    if (sdp_pick(name, path, sizeof path, &h, buf) != FR_OK || h.number != number || h.len != len)
        return 0;
    if (f_open(&f, path, FA_READ) != FR_OK)
        return 0;
    ok = f_lseek(&f, 512) == FR_OK;
    for (at = 0; ok && at < len; at += got) {
        ok = f_read(&f, buf, 512, &got) == FR_OK && got;
        for (i = 0; ok && i < got && at + i < len; i++)
            ok = buf[i] == (uint8_t)((at + i) * 17u + seed * 101u + ((at + i) >> 8) * seed);
    }
    f_close(&f);
    return ok;
}

static int versions(const char *name)
{
    DIR d;
    FILINFO fi;
    char p[128];
    int n = 0;
    snprintf(p, sizeof p, "OPTIMIST/PROJECTS/%s/VERSIONS", name);
    if (f_opendir(&d, p) != FR_OK)
        return -1;
    while (f_readdir(&d, &fi) == FR_OK && fi.fname[0])
        n++;
    f_closedir(&d);
    return n;
}

static void save_tests(const char *what)
{
    static const char *const P = "Night Drive";
    FILINFO fi;
    uint32_t len;
    sdp_hooks.read = st_read;
    sdp_hooks.pend_set = st_set;
    sdp_hooks.pend_get = st_get;
    sdp_hooks.pend_clear = st_clear;
    sdp_keep = 2;

    project(1, 20000);
    CHECK(sdp_save(P, 1, 20000, buf) == FR_OK && !pend_on, "%s: the first save, the pending record cleared", what);
    CHECK(picked(P, 1, 1, 20000), "%s: save 1 on the card, whole", what);
    CHECK(runs_of("OPTIMIST/PROJECTS/Night Drive/Night Drive.OPP") == 1, "%s: one run", what);
    CHECK(f_stat("OPTIMIST/PROJECTS/Night Drive/Night Drive.OPP", &fi) == FR_OK &&
          fi.fsize == (512u + 20000u) * 3u / 2u / ((uint32_t)fs.csize * 512u) * ((uint32_t)fs.csize * 512u) +
                      ((((512u + 20000u) * 3u / 2u) % ((uint32_t)fs.csize * 512u)) ? (uint32_t)fs.csize * 512u : 0u),
          "%s: its run the project x 1.5, whole clusters (%u)", what, (unsigned)fi.fsize);
    for (len = 2; len <= 5; len++) {
        project(len, 20000 + len * 3000u);
        CHECK(sdp_save(P, len, 20000 + len * 3000u, buf) == FR_OK, "%s: save %u", what, (unsigned)len);
        CHECK(picked(P, len, len, 20000 + len * 3000u), "%s: save %u picked", what, (unsigned)len);
    }
    CHECK(versions(P) == 2, "%s: 2 versions kept (%d)", what, versions(P));
    CHECK(f_stat("OPTIMIST/PROJECTS/Night Drive/VERSIONS/Night Drive-0004.OPP", &fi) == FR_OK &&
          f_stat("OPTIMIST/PROJECTS/Night Drive/VERSIONS/Night Drive-0003.OPP", &fi) == FR_OK,
          "%s: ... the newest two", what);
    CHECK(f_stat("OPTIMIST/PROJECTS/Night Drive/Night Drive.SPR", &fi) == FR_OK, "%s: a spare run kept", what);
    CHECK(f_stat("OPTIMIST/PROJECTS/Night Drive/Night Drive.TMP", &fi) == FR_NO_FILE, "%s: no .TMP", what);
    sdp_keep = 0;
    project(6, 21000);
    CHECK(sdp_save(P, 6, 21000, buf) == FR_OK && picked(P, 6, 6, 21000), "%s: N 0: saved", what);
    CHECK(versions(P) == 2, "%s: N 0: the versions there left as they are, no new one (%d)", what, versions(P));
    sdp_keep = 1;
    project(7, 22000);
    CHECK(sdp_save(P, 7, 22000, buf) == FR_OK && picked(P, 7, 7, 22000) && versions(P) == 1,
          "%s: N 1: one version (%d)", what, versions(P));
    project(8, 22000);
    CHECK(sdp_save(P, 6, 22000, buf) == FR_OK && picked(P, 6, 8, 22000) && versions(P) == 1,
          "%s: a save numbered as an older one: the .OPP is the project whatever its number", what);
    CHECK(sdp_save("bad/name", 9, 10, buf) == FR_INVALID_NAME && !pend_on, "%s: a bad name refused first", what);
}

/* a power cut at every block write of a save, from a card holding save A: the pick is A or B whole at the cut, and
 * the boot's finish makes it B */
static void save_cuts(const char *what, uint8_t *img, uint32_t sectors)
{
    static const char *const P = "Cut Me";
    uint8_t *snap = malloc((size_t)sectors * 512u);
    long k, w0, total;
    int mixed = 0, unfinished = 0, wrong = 0, leftover = 0;
    FILINFO fi;
    char name[96];
    int i;
    sdp_keep = 2;
    project(11, 30000);
    CHECK(sdp_save(P, 10, 30000, buf) == FR_OK, "%s: save A", what);
    for (i = 0; i < 9; i++) {                        /* the folder over several sectors: a rename's new entry lands in
                                                      * another sector than the old one (two writes: a cut between) */
        snprintf(name, sizeof name, "OPTIMIST/PROJECTS/Cut Me/a note the user left here %d.txt", i);
        put_file(name, "x", 1);
    }
    project(12, 30000);
    CHECK(sdp_save(P, 11, 30000, buf) == FR_OK, "%s: save A2 (a version, then the spare's turn)", what);
    project(13, 41000);
    CHECK(sdp_save(P, 12, 41000, buf) == FR_OK, "%s: save A3", what);
    memcpy(snap, img, (size_t)sectors * 512u);
    project(14, 47000);                              /* B: larger than the spare (a new run) */
    w0 = C.writes;
    CHECK(sdp_save(P, 13, 47000, buf) == FR_OK && picked(P, 13, 14, 47000), "%s: save B (uncut)", what);
    total = C.writes - w0;
    for (k = 0; k <= total; k++) {
        memcpy(img, snap, (size_t)sectors * 512u);
        card_power_on();
        pend_on = 0;
        if (mount() != FR_OK) { wrong++; continue; }
        project(14, 47000);
        C.cut_at = C.writes + k;
        (void)sdp_save(P, 13, 47000, buf);
        card_power_on();
        if (mount() != FR_OK) { wrong++; continue; }
        if (!picked(P, 12, 13, 41000) && !picked(P, 13, 14, 47000))
            mixed++;                                 /* (neither A3 nor B whole: a mix) */
        if (!pend_on && k < total)
            unfinished++;                            /* (cut, yet no pending record: the boot would not finish it) */
        if (sdp_boot(buf) != FR_OK || pend_on || !picked(P, 13, 14, 47000) || versions(P) != 2)
            wrong++;
        if (f_stat("OPTIMIST/PROJECTS/Cut Me/Cut Me.TMP", &fi) == FR_OK)
            leftover++;
        fat_check(1, what);
    }
    CHECK(total > 20, "%s: the save wrote %ld blocks", what, total);
    CHECK(mixed == 0, "%s: a cut at each of %ld writes: %d left neither the old nor the new whole", what, total + 1,
          mixed);
    CHECK(unfinished == 0, "%s: ... %d cut with the pending record already cleared", what, unfinished);
    CHECK(wrong == 0, "%s: ... %d not finished right at the boot (B picked, the record cleared, 2 versions)", what,
          wrong);
    CHECK(leftover == 0, "%s: ... %d left a .TMP after the boot", what, leftover);
    memcpy(img, snap, (size_t)sectors * 512u);
    card_power_on();
    CHECK(mount() == FR_OK, "%s: mount after the cuts", what);
    free(snap);
}

/* a rename cut between its two directory writes (the new entry written, the old one not yet removed): the project's
 * .TMP named .OPP too; the boot's finish removes the .TMP's name, never its clusters (sdp_dedupe, ff.c f_unlink_entry) */
static void half_rename(const char *what)
{
    static const char *const P = "Half";
    char name[96];
    FILINFO fi;
    int i, half = 0;
    long w;
    sdp_keep = 1;
    project(21, 9000);
    CHECK(sdp_save(P, 1, 9000, buf) == FR_OK, "%s: half: save 1", what);
    for (i = 0; i < 40; i++) {                       /* the folder's first sectors full: new entries go past them */
        snprintf(name, sizeof name, "OPTIMIST/PROJECTS/Half/pad %02d.txt", i);
        put_file(name, "x", 1);
    }
    project(22, 9500);
    pend_on = 0;
    {                                                /* save 2 by hand up to the rename: .TMP whole, .OPP retired */
        sdp_pending p;
        int done;
        memset(&p, 0, sizeof p);
        strcpy(p.name, P);
        p.number = 2;
        p.len = 9500;
        CHECK(sdp_stage_hash(9500, buf, &p.hash) == 0 && st_set(&p) == 0, "%s: half: staged", what);
        CHECK(sdp_temp(&p, buf, &done) == FR_OK && sdp_write(&p, buf) == FR_OK && f_close(&sdp.f) == FR_OK,
              "%s: half: .TMP written", what);
        CHECK(sdp_path(name, P, SDP_OPP, 0, 0) && sdp_retire(&p, name, buf) == FR_OK, "%s: half: .OPP retired", what);
    }
    w = C.writes;
    C.cut_at = w + 1;                                /* one directory write lands, then the power goes */
    (void)f_rename("OPTIMIST/PROJECTS/Half/Half.TMP", "OPTIMIST/PROJECTS/Half/Half.OPP");
    card_power_on();
    CHECK(mount() == FR_OK, "%s: half: mount", what);
    half = sdl_chain("OPTIMIST/PROJECTS/Half/Half.TMP") &&
           sdl_chain("OPTIMIST/PROJECTS/Half/Half.TMP") == sdl_chain("OPTIMIST/PROJECTS/Half/Half.OPP");
    CHECK(half, "%s: half: the cut left .TMP and .OPP one chain (the case under test)", what);
    CHECK(sdp_boot(buf) == FR_OK && !pend_on && picked(P, 2, 22, 9500), "%s: half: the boot finished save 2", what);
    CHECK(f_stat("OPTIMIST/PROJECTS/Half/Half.TMP", &fi) == FR_NO_FILE, "%s: half: the .TMP's name gone", what);
    fat_check(1, what);
    project(23, 9500);
    CHECK(sdp_save(P, 3, 9500, buf) == FR_OK && picked(P, 3, 23, 9500), "%s: half: save 3 after it", what);
    fat_check(1, what);
}

/* ---- the runs ------------------------------------------------------------------------------------------------------- */

static void alloc_tests(const char *what)
{
    FIL f;
    FILINFO fi;
    uint32_t cl = (uint32_t)fs.csize * 512u;
    sda_scale = 100;
    CHECK(sda_guess(SDA_EXACT, 1000, 0) == 1000 && sda_guess(SDA_PROJECT, 1000, 0) == 1500 &&
          sda_guess(SDA_RECORD, 1000, 9000) == 9000 && sda_guess(SDA_RECORD, 1000, 10) == 1000 &&
          sda_guess(SDA_RENDER, 5, 7) == 7, "guesses by kind");
    sda_scale = 0;
    CHECK(sda_guess(SDA_PROJECT, 1000, 0) == 1000 && sda_guess(SDA_RECORD, 1000, 9000) == 1000, "the setting 0: none");
    sda_scale = 200;
    CHECK(sda_guess(SDA_PROJECT, 1000, 0) == 2000 && sda_guess(SDA_RECORD, 1000, 9000) == 17000, "the setting 200 %%");
    sda_scale = 100;
    CHECK(sda_rec_bytes(2000, 0, 48000, 2) == 192000 && sda_rec_bytes(0, 1000, 48000, 2) == 192000 &&
          sda_rec_bytes(0, 0, 44100, 2) == 5292000, "a recording's guess: asked, the loop x 2, a minute");
    CHECK(sda_create(&f, "OPTIMIST/RECORD/take 1.wav", SDA_RECORD, 0, 70000) == FR_OK && f_close(&f) == FR_OK &&
          f_stat("OPTIMIST/RECORD/take 1.wav", &fi) == FR_OK && fi.fsize == (70000u + cl - 1u) / cl * cl &&
          runs_of("OPTIMIST/RECORD/take 1.wav") == 1, "%s: a recording's run: the guess, whole clusters, one", what);
    CHECK(sda_create(&f, "OPTIMIST/RECORD/take 1.wav", SDA_RECORD, 0, 100) == FR_EXIST, "%s: never over a file", what);
    CHECK(sda_create(&f, "OPTIMIST/RECORD/huge.wav", SDA_RENDER, 1000, 0x7FFFFFFFu) == FR_OK && f_close(&f) == FR_OK &&
          f_stat("OPTIMIST/RECORD/huge.wav", &fi) == FR_OK && fi.fsize == (1000u + cl - 1u) / cl * cl,
          "%s: no run that long free: the bytes it needs (%u)", what, (unsigned)fi.fsize);
    CHECK(sda_create(&f, "OPTIMIST/RECORD/too big.wav", SDA_EXACT, 0x7FFFFFFFu, 0) == FR_DENIED &&
          f_stat("OPTIMIST/RECORD/too big.wav", &fi) == FR_NO_FILE, "%s: no room at all: FR_DENIED, no file", what);
    sda_scale = 100;
    CHECK(put_file("OPTIMIST/PRESETS/pad.opr", stage, 1800) == FR_OK, "%s: a preset", what);
    f_unlink("OPTIMIST/PRESETS/pad.opr");
    CHECK(sda_create(&f, "OPTIMIST/PRESETS/pad.opr", SDA_EXACT, 1800, 0) == FR_OK, "%s: a preset by its run", what);
    {
        UINT k;
        CHECK(f_write(&f, stage, 1800, &k) == FR_OK && k == 1800 && f_close(&f) == FR_OK &&
              runs_of("OPTIMIST/PRESETS/pad.opr") == 1, "%s: ... one run on a card in holes", what);
    }
}

static void card_run(const char *what, uint8_t *img, uint32_t sectors, uint32_t base, int hc)
{
    char path[256] = "";
    card(img, sectors, base, 1, hc, 0);
    CHECK(mount() == FR_OK, "%s: mount", what);
    CHECK(empty_dir(path, 0) == FR_OK, "%s: the card emptied (a fresh one)", what);
    hashes();
    layout();
    holes(400);                                      /* from here the free room is in holes of one cluster */
    index_tests(what);
    index_cuts(what, img, sectors, base);
    browse_tests(what);
    save_tests(what);
    alloc_tests(what);
    fat_check(1, what);
    save_cuts(what, img, sectors);
    half_rename(what);
    CHECK(C.lcd_bad == 0 && C.crc_refused == 0, "%s: LCD bytes %ld, CRC refusals %ld", what, C.lcd_bad,
          C.crc_refused);
}

int main(int argc, char **argv)
{
    uint32_t n16, n32;
    uint8_t *i16, *i32;
    if (argc < 3) { printf("usage: sd_layout_test FAT16.img FAT32.img\n"); return 2; }
    i16 = load(argv[1], &n16);
    i32 = load(argv[2], &n32);
    card_run("SDHC FAT32 + MBR", i32, n32, 2048, 1);
    card_run("SDSC FAT16", i16, n16, 0, 0);
    printf("sd_layout_test: %d checks, %d failed\n", checks, fails);
    free(i16); free(i32);
    return fails ? 1 : 0;
}
