/* SPDX-License-Identifier: GPL-3.0-only */
/* The sample index, OPTIMIST/SYSTEM/INDEX.BIN (docs/SD-CARD-DESIGN.md 8): every sample under SAMPLES/ with its hash
 * (sd_hash.c sdh_file: the audio data's XXH64), the file's size and FAT date when hashed, and its last use (the recent
 * view). A project names its samples by hash, so a sample renamed or moved on the card is still found; the index is
 * what makes that a lookup and not a hash of every file at each load. Full FatFs builds (FELUCCA_SD_FATFS); pure over
 * FatFs, host-tested (tests/sd_layout_test.c). Main loop only.
 * When a file is hashed again (the user, 2026-10-25: the FM-1 has no clock, other systems do):
 *   - a file the FM-1 changes (records, crops, normalises, writes) right away, by the FM-1 itself (sdx_note_written),
 *     whatever date it stamps (a placeholder: sd_diskio.c get_fattime);
 *   - a file whose size or date differs from its record: changed elsewhere (a computer, a phone), at the next pass
 *     (sdx_update); one whose record matches keeps its hash.
 * The file: a header record, then records of SDX_REC bytes (sdx_rec), count in use, cap in the file's run (one run,
 * sized exactly: sd_alloc.c SDA_EXACT, the cluster's rounding the room for sdx_note_written's new records). A pass
 * writes a new index to INDEX.TMP (its header zeroed first, written last), then deletes INDEX.BIN and renames the new
 * one: a cut leaves the old index, or the new one under INDEX.TMP that sdx_open takes. A record's path is from
 * OPTIMIST/ ("SAMPLES/drums/kick.wav").
 *   sdx_open()                    the index's header read (sdx.count, sdx.cap, sdx.seq): FR_OK, FR_NO_FILE (none
 *                                 yet), FR_NO_FILESYSTEM (not an index: sdx_update replaces it)
 *   sdx_update(buf, bufn, work, work_n)   the pass: SAMPLES/ walked, each file matched to its old record (the walk's
 *                                 order is the last pass's, so mostly the next record; else by the path's key in work,
 *                                 work_n words for that many old records; else by reading the old index through),
 *                                 hashed when new or changed; buf: bufn bytes (512 or more) for the hashing.
 *                                 sdx.files / sdx.hashed / sdx.kept: what it did
 *   sdx_find_hash(h, &rec)        the record of the sample with that hash: its number, or -1
 *   sdx_find_path(path, &rec)     the record of that path (from OPTIMIST/): its number, or -1
 *   sdx_read(no, &rec)            record no
 *   sdx_touch(no)                 record no used now (sdx.seq + 1): the recent view's order
 *   sdx_note_written(path, buf, bufn)   the FM-1 wrote that sample: hashed again now, its record updated or added
 *                                 (FR_DENIED: no room left in the index's run: the next sdx_update makes some) */
#define SDX_REC 256u
#define SDX_PATH 224u
typedef struct {
    uint64_t hash;
    uint32_t size;                                   /* the file's size when hashed */
    uint32_t stamp;                                  /* its FAT date << 16 | time then */
    uint32_t hlen;                                   /* the bytes hashed (a WAV's data chunk) */
    uint32_t used;                                   /* its last use (sdx.seq then), 0 never */
    uint32_t pkey;                                   /* sdx_key(path): a quick compare */
    uint32_t flags;                                  /* (none yet) */
    char path[SDX_PATH];                             /* from OPTIMIST/, 0 ended */
} sdx_rec;
_Static_assert(sizeof(sdx_rec) == SDX_REC, "sd_index.c: a record is SDX_REC bytes");

typedef struct {
    char magic[4];                                   /* "OPIX" */
    uint16_t version, rec;                           /* 1, SDX_REC */
    uint32_t count, cap, seq;
    uint64_t check;                                  /* XXH64 of the bytes before it */
} sdx_head;

#define SDX_FILE "OPTIMIST/SYSTEM/INDEX.BIN"
#define SDX_TMP "OPTIMIST/SYSTEM/INDEX.TMP"
#define SDX_ROOT "OPTIMIST/SAMPLES"
#define SDX_SKIP 9u                                  /* "OPTIMIST/": a walked path's record path */

static struct {
    uint32_t count, cap, seq;                        /* the index on the card */
    uint32_t files, hashed, kept;                    /* the last pass */
    FIL f, old, smp;                                 /* the index, the old one in a pass, a sample hashed */
    sdx_rec rec;
    sdw_t walk;
} sdx IN_POOL;

static uint32_t sdx_key(const char *p)               /* FNV-1a, 32 bits */
{
    uint32_t h = 2166136261u;
    while (*p)
        h = (h ^ (uint8_t)*p++) * 16777619u;
    return h;
}

static uint64_t sdx_check(const sdx_head *h) { sdh_t s; sdh_init(&s); sdh_add(&s, h, 20); return sdh_end(&s); }

static FRESULT sdx_rw(FIL *f, uint32_t at, void *p, uint32_t n, int write)   /* n bytes at at, whole */
{
    UINT k = 0;
    FRESULT r = f_lseek(f, at);
    if (r == FR_OK)
        r = write ? f_write(f, p, n, &k) : f_read(f, p, n, &k);
    return r != FR_OK ? r : k == n ? FR_OK : FR_INT_ERR;
}

static FRESULT sdx_head_read(FIL *f, sdx_head *h)
{
    FRESULT r = sdx_rw(f, 0, h, sizeof *h, 0);
    if (r == FR_INT_ERR || (r == FR_OK && (memcmp(h->magic, "OPIX", 4) || h->version != 1u || h->rec != SDX_REC ||
                                           h->check != sdx_check(h) || h->count > h->cap ||
                                           (uint64_t)(h->cap + 1u) * SDX_REC > f_size(f))))
        r = FR_NO_FILESYSTEM;
    return r;
}

static FRESULT sdx_head_write(FIL *f, uint32_t count, uint32_t cap, uint32_t seq)
{
    sdx_head h;
    memset(&h, 0, sizeof h);
    memcpy(h.magic, "OPIX", 4);
    h.version = 1;
    h.rec = SDX_REC;
    h.count = count;
    h.cap = cap;
    h.seq = seq;
    h.check = sdx_check(&h);
    return sdx_rw(f, 0, &h, sizeof h, 1);
}

static FRESULT sdx_open_file(FIL *f, BYTE mode)      /* INDEX.BIN, or a pass's INDEX.TMP a cut left whole */
{
    sdx_head h;
    FRESULT r = f_open(f, SDX_FILE, mode);
    if (r == FR_NO_FILE && f_open(f, SDX_TMP, FA_READ) == FR_OK) {
        r = sdx_head_read(f, &h);
        (void)f_close(f);
        if (r == FR_OK && (r = sdl_move(SDX_TMP, SDX_FILE)) == FR_OK)
            r = f_open(f, SDX_FILE, mode);
        else if (r == FR_NO_FILESYSTEM)
            r = FR_NO_FILE;                          /* (a pass cut before its header: nothing) */
    }
    return r;
}

static FRESULT sdx_open(void)
{
    sdx_head h;
    FRESULT r = sdx_open_file(&sdx.f, FA_READ);
    sdx.count = sdx.cap = 0;
    if (r != FR_OK)
        return r;
    if ((r = sdx_head_read(&sdx.f, &h)) == FR_OK) {
        sdx.count = h.count;
        sdx.cap = h.cap;
        sdx.seq = h.seq;
    }
    (void)f_close(&sdx.f);
    return r;
}

static FRESULT sdx_read_in(FIL *f, uint32_t no, sdx_rec *rec) { return sdx_rw(f, (no + 1u) * SDX_REC, rec, SDX_REC, 0); }

static FRESULT sdx_read(uint32_t no, sdx_rec *rec)
{
    FRESULT r = no < sdx.count ? sdx_open_file(&sdx.f, FA_READ) : FR_INVALID_PARAMETER;
    if (r == FR_OK) {
        r = sdx_read_in(&sdx.f, no, rec);
        (void)f_close(&sdx.f);
    }
    return r;
}

/* the first record from no on (wrapping round) whose path is path (key its sdx_key): its number, or -1 */
static int32_t sdx_scan_path(FIL *f, uint32_t count, uint32_t from, const char *path, uint32_t key, sdx_rec *rec)
{
    uint32_t i, no;
    for (i = 0; i < count; i++) {
        no = (from + i) % count;
        if (sdx_read_in(f, no, rec) != FR_OK)
            return -1;
        if (rec->pkey == key && !sdl_cmp(rec->path, path))
            return (int32_t)no;
    }
    return -1;
}

static int32_t sdx_find(const char *path, uint64_t hash, int by_path, sdx_rec *rec)
{
    uint32_t i, key = by_path ? sdx_key(path) : 0;
    int32_t at = -1;
    if (sdx_open_file(&sdx.f, FA_READ) != FR_OK)
        return -1;
    for (i = 0; i < sdx.count && at < 0; i++) {
        if (sdx_read_in(&sdx.f, i, rec) != FR_OK)
            break;
        if (by_path ? rec->pkey == key && !sdl_cmp(rec->path, path) : rec->path[0] && rec->hash == hash)
            at = (int32_t)i;
    }
    (void)f_close(&sdx.f);
    return at;
}
static int32_t sdx_find_hash(uint64_t hash, sdx_rec *rec) { return sdx_find(0, hash, 0, rec); }
static int32_t sdx_find_path(const char *path, sdx_rec *rec) { return sdx_find(path, 0, 1, rec); }

static FRESULT sdx_put(uint32_t no, sdx_rec *rec, uint32_t count, uint32_t seq)   /* a record and the header */
{
    FRESULT r = sdx_open_file(&sdx.f, FA_READ | FA_WRITE);
    if (r != FR_OK)
        return r;
    r = sdx_rw(&sdx.f, (no + 1u) * SDX_REC, rec, SDX_REC, 1);
    if (r == FR_OK)
        r = sdx_head_write(&sdx.f, count, sdx.cap, seq);
    if (f_close(&sdx.f) != FR_OK && r == FR_OK)
        r = FR_DISK_ERR;
    if (r == FR_OK) {
        sdx.count = count;
        sdx.seq = seq;
    }
    return r;
}

static FRESULT sdx_touch(uint32_t no)
{
    FRESULT r = sdx_read(no, &sdx.rec);
    if (r != FR_OK)
        return r;
    sdx.rec.used = sdx.seq + 1u;
    return sdx_put(no, &sdx.rec, sdx.count, sdx.seq + 1u);
}

/* a record for the file at path (from the card's root; its entry fi) hashed now; used carried over */
static FRESULT sdx_hash_into(sdx_rec *rec, const char *path, const FILINFO *fi, uint32_t used, uint8_t *buf,
                             uint32_t bufn)
{
    FRESULT r = f_open(&sdx.smp, path, FA_READ);
    memset(rec, 0, sizeof *rec);
    if (r == FR_OK) {
        r = sdh_file(&sdx.smp, buf, bufn, &rec->hash, &rec->hlen);
        (void)f_close(&sdx.smp);
    }
    rec->size = (uint32_t)fi->fsize;
    rec->stamp = (uint32_t)fi->fdate << 16 | fi->ftime;
    rec->used = used;
    sdl_cat(rec->path, 0, SDX_PATH, path + SDX_SKIP);
    rec->pkey = sdx_key(rec->path);
    return r;
}

static FRESULT sdx_note_written(const char *path, uint8_t *buf, uint32_t bufn)   /* path from the card's root */
{
    FILINFO fi;
    int32_t no;
    uint32_t used = 0;
    FRESULT r;
    if (sdl_ncmp(path, SDX_ROOT "/", sizeof SDX_ROOT) || sdl_len(path + SDX_SKIP) >= SDX_PATH)
        return FR_INVALID_NAME;                      /* (a sample is under SAMPLES/, its path short enough) */
    if ((r = f_stat(path, &fi)) != FR_OK || ((r = sdx_open()) != FR_OK))
        return r;
    no = sdx_find_path(path + SDX_SKIP, &sdx.rec);
    if (no >= 0)
        used = sdx.rec.used;
    else if (sdx.count >= sdx.cap)
        return FR_DENIED;
    if ((r = sdx_hash_into(&sdx.rec, path, &fi, used, buf, bufn)) != FR_OK)
        return r;
    return sdx_put(no >= 0 ? (uint32_t)no : sdx.count, &sdx.rec, no >= 0 ? sdx.count : sdx.count + 1u, sdx.seq);
}

/* the pass: the files counted (the new index's exact run), then walked again into it */
static FRESULT sdx_update(uint8_t *buf, uint32_t bufn, uint32_t *work, uint32_t work_n)
{
    sdx_head h;
    uint32_t n = 0, k = 0, cursor = 0, oldn = 0, key, i, used;
    int32_t at;
    int st, have_old = 0;
    FRESULT r;
    sdx.files = sdx.hashed = sdx.kept = 0;
    if ((r = sdw_open(&sdx.walk, SDX_ROOT)) != FR_OK)
        return r;
    while ((st = sdw_next(&sdx.walk)) > 0)
        n += sdl_len(sdx.walk.path + SDX_SKIP) < SDX_PATH;
    if (st < 0)
        return sdx.walk.err;

    r = sdx_open_file(&sdx.old, FA_READ);            /* the old index, if any, and its keys into work */
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
    for (i = 0; i < oldn && oldn <= work_n; i++) {
        if ((r = sdx_read_in(&sdx.old, i, &sdx.rec)) != FR_OK)
            goto out;
        work[i] = sdx.rec.pkey;
    }

    if (sdl_chain(SDX_TMP) && sdl_chain(SDX_TMP) == sdl_chain(SDX_FILE))
        (void)f_unlink_entry(SDX_TMP);               /* (a cut rename's other half: the index's own clusters) */
    else
        (void)f_unlink(SDX_TMP);                     /* (a cut pass's) */
    if ((r = sda_create(&sdx.f, SDX_TMP, SDA_EXACT, (n + 1u) * SDX_REC, 0)) != FR_OK)
        goto out;
    memset(&sdx.rec, 0, sizeof sdx.rec);             /* the header slot zeroed: the run's old bytes never read as one */
    sdx.cap = (uint32_t)(f_size(&sdx.f) / SDX_REC) - 1u;
    r = sdx_rw(&sdx.f, 0, &sdx.rec, SDX_REC, 1);
    if (r == FR_OK)
        r = sdw_open(&sdx.walk, SDX_ROOT);
    while (r == FR_OK && k < sdx.cap && (st = sdw_next(&sdx.walk)) > 0) {
        const char *rel = sdx.walk.path + SDX_SKIP;
        if (sdl_len(rel) >= SDX_PATH)
            continue;
        key = sdx_key(rel);
        at = -1;
        if (cursor < oldn && sdx_read_in(&sdx.old, cursor, &sdx.rec) == FR_OK && sdx.rec.pkey == key &&
            !sdl_cmp(sdx.rec.path, rel))
            at = (int32_t)cursor;                    /* (the last pass's order: the next record) */
        else if (oldn && oldn <= work_n) {
            for (i = 0; i < oldn && at < 0; i++)
                if (work[i] == key && sdx_read_in(&sdx.old, i, &sdx.rec) == FR_OK && !sdl_cmp(sdx.rec.path, rel))
                    at = (int32_t)i;
        } else if (oldn) {
            at = sdx_scan_path(&sdx.old, oldn, cursor, rel, key, &sdx.rec);
        }
        if (at >= 0)
            cursor = (uint32_t)at + 1u;
        sdx.files++;
        if (at >= 0 && sdx.rec.size == (uint32_t)sdx.walk.fi.fsize &&
            sdx.rec.stamp == ((uint32_t)sdx.walk.fi.fdate << 16 | sdx.walk.fi.ftime)) {
            sdx.kept++;                              /* unchanged: its hash kept */
        } else {
            used = at >= 0 ? sdx.rec.used : 0;
            if ((r = sdx_hash_into(&sdx.rec, sdx.walk.path, &sdx.walk.fi, used, buf, bufn)) != FR_OK)
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
