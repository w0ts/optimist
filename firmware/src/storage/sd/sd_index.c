/* SPDX-License-Identifier: GPL-3.0-only */
/* The sample index, Optimist/System/index.bin (docs/SD-CARD-DESIGN.md 8): every sample under Samples/ with its hash
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
 * writes a new index to index.tmp (its header zeroed first, written last), then deletes index.bin and renames the new
 * one: a cut leaves the old index, or the new one under index.tmp that sdx_open takes. A record's path is from
 * Optimist/ ("Samples/drums/kick.wav").
 *   sdx_open()                    the index's header read (sdx.count, sdx.cap, sdx.seq): FR_OK, FR_NO_FILE (none
 *                                 yet), FR_NO_FILESYSTEM (not an index: sdx_update replaces it)
 *   the pass, in slices across the main loop's passes (the user, 2026-11-04: one pass over the card in one frame froze the
 *   FM-1 for 10 s and starved the second core): the files counted (the new index's exact run), then walked again into it,
 *   each matched to its old record (the walk's order is the last pass's, so mostly the next record; else by reading the
 *   old index through, a record a unit), hashed when new or changed (a chunk of bufn bytes a unit). A unit is one
 *   directory entry, one record, one f_open or one chunk; sdx_pass_step runs units until its ms are spent. The card stays
 *   usable: lookups read the old index, and whoever needs the new one waits for sdx_pass_on to end (sd_mode.c)
 *   sdx_pass_begin(force)         a pass from the start, any running one dropped: 1 started, 0 nothing to do (not force, and
 *                                 the card unchanged since the last pass: the header's signature, below)
 *   sdx_pass_step(buf, bufn, ms)  the next units for ms, the bytes of buf (512 or more) free for the call; nothing while a
 *                                 recording holds the card: FR_OK, or the error that ended the pass
 *   sdx_pass_on()                 a pass under way (or to start again: a file written meanwhile)
 *   sdx_pass_pct()                how far, 0..99
 *   sdx_pass_cancel()             dropped: the files closed, index.bin as it was (a card pulled, a mount); index.tmp
 *                                 stays until the next pass removes it. index.bin is only replaced by a whole pass
 *   sdx_update(buf, bufn)         a whole pass at once, the walk forced (the tests'). sdx.files / sdx.hashed / sdx.kept:
 *                                 what the last pass did
 * The signature (sdx_sig, past the header in the header's record, outside its check: an older index has none): FSINFO's
 * free clusters and next-free hint as a pass left them. Whatever a computer adds, removes or rewrites by a new chain moves
 * one of them, so a mount that finds them equal skips the walk altogether. The FM-1's own later writes move them too (a
 * walk at the next mount, in slices, the hashes kept). A file overwritten in place by a computer does not: SYSTEM > SD CARD's
 * SAMPLES INDEX forces the walk. FAT16 has no FSINFO: always walked.
 *   sdx_find_hash(h, &rec)        the record of the sample with that hash: its number, or -1
 *   sdx_find_path(path, &rec)     the record of that path (from Optimist/, any case): its number, or -1
 *   sdx_read(no, &rec)            record no
 *   sdx_touch(no)                 record no used now (sdx.seq + 1): the recent view's order (during a pass, not kept)
 *   sdx_note_written(path, buf, bufn)   the FM-1 wrote that sample: hashed again now, its record updated or added
 *                                 (FR_DENIED: no room left in the index's run: the next pass makes some). During a
 *                                 pass: the pass dropped, done, and started again, or it would lose this
 * The presets and kits too (the user, 2026-11-04: "like samples, kits and presets should have a unique id so that if
 * they are moved a project finds them back"): every .ops under Presets/ and .opk under Kits/ is a record of the same
 * index, its hash the id in the file's header (sd_ids.h; sd_mode.c sdm_fh), flagged SDX_F_ID: the same pass, the same
 * kept-when-unchanged rule.
 *   sdx_find_id(id, &rec, &dups)  the first record (the walk's order) with that id, -1 none; dups: the others with it (a
 *                                 copied file: the first found wins, sd_mode.c says so)
 * sdx_find_hash finds samples only, sdx_find_id presets and kits only; the recent view lists used records (a preset's
 * never is). */
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
    char path[SDX_PATH];                             /* from Optimist/, 0 ended */
} sdx_rec;
_Static_assert(sizeof(sdx_rec) == SDX_REC, "sd_index.c: a record is SDX_REC bytes");

typedef struct {
    char magic[4];                                   /* "OPIX" */
    uint16_t version, rec;                           /* 1, SDX_REC */
    uint32_t count, cap, seq;
    uint64_t check;                                  /* XXH64 of the bytes before it */
} sdx_head;

#define SDX_FILE SDL_ROOT "/System/index.bin"
#define SDX_TMP SDL_ROOT "/System/index.tmp"
/* a pass cut short is not started from scratch (the user, 2026-11-08: 5,511 samples, "as long as it does not start from
 * scratch at every boot and saves state on a regular basis"): every SDX_CKPT_MS index.tmp's header says how many of its
 * records are written ("OPIP"); the next pass keeps that file as index.prt and takes from it each walked file's record
 * when the file is unchanged (path, size, date), as from the old index, so only the rest is hashed; gone when a pass ends */
#define SDX_PART SDL_ROOT "/System/index.prt"
/* a walked file not the old index's next record is looked for in the SDX_SCAN_MAX records from there, not the whole index: the
 * walk's order is the last pass's, so a file kept is near; a new one cost a read of every record (5,511 new WAVs over an
 * index of ~500: 170 ms a file, 2026-11-08, sd index on pink). One further away is hashed again: its hash is still right */
#ifndef SDX_SCAN_MAX
#define SDX_SCAN_MAX 16u
#endif
#ifndef SDX_CKPT_MS
#define SDX_CKPT_MS 30000u
#endif
#define SDX_ROOT SDL_ROOT "/Samples"
#define SDX_F_ID 1u                                  /* (flags: a preset's or kit's record, its hash the file's id) */
static const char *const SDX_ROOTS[3] = {SDX_ROOT, SDL_ROOT "/Presets", SDL_ROOT "/Kits"};
#define SDX_NROOT 3u
#define SDX_SKIP (sizeof SDL_ROOT)                   /* "Optimist/": a walked path's record path */

static struct {
    uint32_t count, cap, seq;                        /* the index on the card */
    uint32_t files, hashed, kept;                    /* the last pass */
    FIL f, old, smp;                                 /* the index, the old one in a pass, a sample hashed */
    sdx_rec rec;
    sdw_t walk;
} sdx IN_POOL;

enum { SDXP_OFF = 0, SDXP_START, SDXP_COUNT, SDXP_OLD, SDXP_TMP, SDXP_ROOM, SDXP_NEW, SDXP_WALK, SDXP_SCAN, SDXP_HASH, SDXP_PUT, SDXP_END,
       SDXP_UNLINK, SDXP_MOVE, SDXP_SIG };
static struct {
    uint8_t st, root, old_open, new_open, smp_open, redo, part_open;
    uint32_t n, k, cap, oldn, cursor, scan, used, left;
    uint32_t partn, pcursor, ck_at;                  /* index.prt's records, the next one the walk should meet; the last checkpoint */
    FIL pf;                                          /* index.prt */
    uint32_t room_at, room_left, room_cl, room_n;    /* SDXP_ROOM: the FAT's sector, those left, the free run's start, its clusters */
    sdh_t hs;
    sdx_rec rec;                                     /* (the pass's: sdx.rec and sdx.f are the lookups', between slices) */
    FIL nf;                                          /* index.tmp */
} sdxp IN_POOL;

#include "../../core/console_on.h"
#define SDX_STATS FELUCCA_CONSOLE
#if SDX_STATS
/* where a pass's time goes (console builds only: sd index, fm1-base optimist/ui/opt_console.h; the user, 2026-11-08: a first
 * index of 5,511 WAVs at ~1 % a minute). DIR the walk's units (a directory entry, the old index's record, and the file's open
 * and WAV header when it is to be hashed: open_us, inside DIR), READ the hashing's chunks (bytes: what they read), PUT a
 * record written (and a checkpoint), OTHER the rest (the count, the files made and renamed); us each; t0 the pass's start */
enum { SXS_DIR, SXS_READ, SXS_PUT, SXS_OTHER, SXS_N };
static struct { DWORD t0; uint32_t slices, us[SXS_N], open_us, bytes; } sxs;
static void sxs_add(uint32_t st, uint32_t ticks, uint32_t bytes)
{
    uint32_t b = st == SDXP_WALK || st == SDXP_SCAN ? SXS_DIR : st == SDXP_HASH ? SXS_READ : st == SDXP_PUT ? SXS_PUT : SXS_OTHER;
    sxs.us[b] += ticks * 1000u / SD_TICKS_MS;
    sxs.bytes += bytes;
}
#endif

typedef struct {
    char magic[4];                                   /* "OPSG" */
    uint32_t free_clst, last_clst;
} sdx_sig_t;
#define SDX_SIG_AT 32u                               /* (past sdx_head, in the header's record) */
_Static_assert(sizeof(sdx_head) <= SDX_SIG_AT && SDX_SIG_AT + sizeof(sdx_sig_t) <= SDX_REC, "sd_index.c: the signature's place");

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

/* a header of that kind: 'X' an index ("OPIX"), 'P' a pass's records so far ("OPIP", its checkpoint: SDX_CKPT_MS) */
static FRESULT sdx_head_kind(FIL *f, sdx_head *h, char kind)
{
    FRESULT r = sdx_rw(f, 0, h, sizeof *h, 0);
    if (r == FR_INT_ERR || (r == FR_OK && (memcmp(h->magic, "OPI", 3) || h->magic[3] != kind || h->version != 1u || h->rec != SDX_REC ||
                                           h->check != sdx_check(h) || h->count > h->cap ||
                                           (uint64_t)(h->cap + 1u) * SDX_REC > f_size(f))))
        r = FR_NO_FILESYSTEM;
    return r;
}
#define sdx_head_read(f, h) sdx_head_kind((f), (h), 'X')

static FRESULT sdx_head_put(FIL *f, char kind, uint32_t count, uint32_t cap, uint32_t seq)
{
    sdx_head h;
    memset(&h, 0, sizeof h);
    memcpy(h.magic, "OPIX", 4);
    h.magic[3] = kind;
    h.version = 1;
    h.rec = SDX_REC;
    h.count = count;
    h.cap = cap;
    h.seq = seq;
    h.check = sdx_check(&h);
    return sdx_rw(f, 0, &h, sizeof h, 1);
}
#define sdx_head_write(f, count, cap, seq) sdx_head_put((f), 'X', (count), (cap), (seq))

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

/* the first record from no on (wrapping round) whose path is path (without case): its number, or -1 */
static int32_t sdx_scan_path(FIL *f, uint32_t count, uint32_t from, const char *path, sdx_rec *rec)
{
    uint32_t i, no;
    for (i = 0; i < count; i++) {
        no = (from + i) % count;
        if (sdx_read_in(f, no, rec) != FR_OK)
            return -1;
        if (!sdl_cmp(rec->path, path))
            return (int32_t)no;
    }
    return -1;
}

static int32_t sdx_find(const char *path, uint64_t hash, int by_path, sdx_rec *rec)
{
    uint32_t i;
    int32_t at = -1;
    if (sdx_open_file(&sdx.f, FA_READ) != FR_OK)
        return -1;
    for (i = 0; i < sdx.count && at < 0; i++) {
        if (sdx_read_in(&sdx.f, i, rec) != FR_OK)
            break;
        if (by_path ? !sdl_cmp(rec->path, path) : rec->path[0] && rec->hash == hash && !(rec->flags & SDX_F_ID))
            at = (int32_t)i;
    }
    (void)f_close(&sdx.f);
    return at;
}
static int32_t sdx_find_hash(uint64_t hash, sdx_rec *rec) { return sdx_find(0, hash, 0, rec); }
static int32_t sdx_find_path(const char *path, sdx_rec *rec) { return sdx_find(path, 0, 1, rec); }
static int32_t sdx_find_id(uint64_t id, sdx_rec *rec, uint32_t *dups)
{
    static sdx_rec other;
    uint32_t i;
    int32_t at = -1;
    *dups = 0;
    if (!id || sdx_open_file(&sdx.f, FA_READ) != FR_OK)
        return -1;
    for (i = 0; i < sdx.count; i++) {
        sdx_rec *r = at < 0 ? rec : &other;
        if (sdx_read_in(&sdx.f, i, r) != FR_OK)
            break;
        if (!r->path[0] || !(r->flags & SDX_F_ID) || r->hash != id)
            continue;
        if (at < 0)
            at = (int32_t)i;
        else
            (*dups)++;
    }
    (void)f_close(&sdx.f);
    return at;
}

/* a walked file the index holds: a sample under Samples/, a .ops under Presets/, a .opk under Kits/ (path from
 * Optimist/) */
static int sdx_wanted(const char *rel)
{
    if (!sdl_ncmp(rel, "Samples/", 8))
        return 1;
    if (!sdl_ncmp(rel, "Presets/", 8))
        return sdl_ends(rel, ".ops") != 0;
    return !sdl_ncmp(rel, "Kits/", 5) && sdl_ends(rel, ".opk") != 0;
}

static void sdx_pass_cancel(void)
{
    if (sdxp.smp_open)
        (void)f_close(&sdx.smp);
    if (sdxp.old_open)
        (void)f_close(&sdx.old);
    if (sdxp.new_open)
        (void)f_close(&sdxp.nf);
    if (sdxp.part_open)
        (void)f_close(&sdxp.pf);                     /* (index.prt stays: the next pass takes it again) */
    sdxp.smp_open = sdxp.old_open = sdxp.new_open = sdxp.part_open = 0;
    sdxp.st = SDXP_OFF;
}

static int sdx_pass_on(void) { return sdxp.st != SDXP_OFF || sdxp.redo; }

static uint32_t sdx_pass_pct(void)
{
    uint32_t pct = sdxp.cap && sdxp.st >= SDXP_WALK ? sdxp.k * 100u / sdxp.cap : 0u;
    return pct > 99u ? 99u : pct;
}

static void sdx_pass_redo(void)                      /* the index written under a pass: dropped, to start again */
{
    if (sdxp.st != SDXP_OFF) {
        sdx_pass_cancel();
        sdxp.redo = 1;
    }
}

static FRESULT sdx_put(uint32_t no, sdx_rec *rec, uint32_t count, uint32_t seq)   /* a record and the header */
{
    FRESULT r;
    sdx_pass_redo();
    r = sdx_open_file(&sdx.f, FA_READ | FA_WRITE);
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
    FRESULT r;
    if (sdxp.st != SDXP_OFF)
        return FR_OK;                                /* (a use is not worth a pass again: the recent view misses it) */
    r = sdx_read(no, &sdx.rec);
    if (r != FR_OK)
        return r;
    sdx.rec.used = sdx.seq + 1u;
    return sdx_put(no, &sdx.rec, sdx.count, sdx.seq + 1u);
}

static void sdx_rec_end(sdx_rec *rec, const char *path, const FILINFO *fi, uint32_t used)   /* what every record says */
{
    rec->size = (uint32_t)fi->fsize;
    rec->stamp = (uint32_t)fi->fdate << 16 | fi->ftime;
    rec->used = used;
    sdl_cat(rec->path, 0, SDX_PATH, path + SDX_SKIP);
    rec->pkey = sdx_key(rec->path);
}

/* a preset's or kit's id, from its open file's header (sd_mode.c sdm_fh: magic, ver 2, len, hash, id) */
static FRESULT sdx_id_of(FIL *f, sdx_rec *rec, uint8_t *buf)
{
    UINT k = 0;
    FRESULT r = f_read(f, buf, 24, &k);
    rec->flags = SDX_F_ID;
    if (r == FR_OK && k == 24u && buf[4] == 2u && buf[5] == 0u)
        memcpy(&rec->hash, buf + 16, 8);
    return r;
}

/* a record for the file at path (from the card's root; its entry fi) hashed now; used carried over */
static FRESULT sdx_hash_into(sdx_rec *rec, const char *path, const FILINFO *fi, uint32_t used, uint8_t *buf,
                             uint32_t bufn)
{
    FRESULT r = f_open(&sdx.smp, path, FA_READ);
    memset(rec, 0, sizeof *rec);
    if (r == FR_OK) {
        r = sdl_ncmp(path + SDX_SKIP, "Samples/", 8) ? sdx_id_of(&sdx.smp, rec, buf)
                                                     : sdh_file(&sdx.smp, buf, bufn, &rec->hash, &rec->hlen);
        (void)f_close(&sdx.smp);
    }
    sdx_rec_end(rec, path, fi, used);
    return r;
}

static FRESULT sdx_note_written(const char *path, uint8_t *buf, uint32_t bufn)   /* path from the card's root */
{
    FILINFO fi;
    int32_t no;
    uint32_t used = 0;
    FRESULT r;
    sdx_pass_redo();
    if (sdl_ncmp(path, SDL_ROOT "/", SDX_SKIP) || !sdx_wanted(path + SDX_SKIP) || sdl_len(path + SDX_SKIP) >= SDX_PATH)
        return FR_INVALID_NAME;                      /* (a sample, a preset or a kit, its path short enough) */
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

/* ---- the pass (above: "the pass, in slices") */
static FRESULT sdx_root_open(void)                   /* this root's walk (Samples/ first; Presets/, Kits/: none is no error) */
{
    FRESULT r = sdw_open(&sdx.walk, SDX_ROOTS[sdxp.root]);
    if (sdxp.root && (r == FR_NO_PATH || r == FR_NO_FILE)) {
        sdx.walk.depth = 0;                          /* (none: its walk ends at once) */
        r = FR_OK;
    }
    return r;
}

static FRESULT sdx_fs_sig(sdx_sig_t *s, const FIL *f)  /* the volume's signature now: FR_NO_FILE none (FAT16, no FSINFO) */
{
    const FATFS *fs = f->obj.fs;
    memcpy(s->magic, "OPSG", 4);
    s->free_clst = fs->free_clst;
    s->last_clst = fs->last_clst;
    return fs->fs_type == FS_FAT32 && fs->free_clst != 0xFFFFFFFFu ? FR_OK : FR_NO_FILE;
}

static int sdx_unchanged(void)                       /* an index whose signature is the volume's now */
{
    sdx_head h;
    sdx_sig_t now, was;
    int same = 0;
    if (sdx_open_file(&sdx.f, FA_READ) != FR_OK)
        return 0;
    if (sdx_head_read(&sdx.f, &h) == FR_OK && sdx_rw(&sdx.f, SDX_SIG_AT, &was, sizeof was, 0) == FR_OK &&
        sdx_fs_sig(&now, &sdx.f) == FR_OK && !memcmp(&now, &was, sizeof now)) {
        same = 1;
        sdx.count = h.count;
        sdx.cap = h.cap;
        sdx.seq = h.seq;
    }
    (void)f_close(&sdx.f);
    return same;
}

static int sdx_pass_begin(int force)
{
    sdx_pass_cancel();
    sdxp.redo = 0;
    sdx.files = sdx.hashed = sdx.kept = 0;
    if (!force && sdx_unchanged())
        return 0;
    sdxp.n = sdxp.k = sdxp.cap = sdxp.oldn = sdxp.cursor = 0;
#if SDX_STATS
    memset(&sxs, 0, sizeof sxs);
    sxs.t0 = SD_NOW();
#endif
    sdxp.root = 0;
    sdxp.st = SDXP_START;
    return 1;
}

static FRESULT sdx_u_count(void)                     /* a file of the walk counted */
{
    int st = sdw_next(&sdx.walk);
    if (st > 0) {
        sdxp.n += sdl_len(sdx.walk.path + SDX_SKIP) < SDX_PATH && sdx_wanted(sdx.walk.path + SDX_SKIP);
        return FR_OK;
    }
    if (st < 0)
        return sdx.walk.err;
    if (++sdxp.root < SDX_NROOT)
        return sdx_root_open();
    sdxp.st = SDXP_OLD;
    return FR_OK;
}

static FRESULT sdx_u_old(void)                       /* the old index open */
{
    sdx_head h;
    FRESULT r = sdx_open_file(&sdx.old, FA_READ);
    if (r == FR_OK && sdx_head_read(&sdx.old, &h) == FR_OK) {
        sdxp.oldn = h.count;
        sdx.seq = h.seq;
        sdxp.old_open = 1;
    } else if (r == FR_OK) {
        (void)f_close(&sdx.old);
        r = FR_NO_FILE;
    }
    if (r != FR_OK && r != FR_NO_FILE)
        return r;
    sdxp.st = SDXP_TMP;
    return FR_OK;
}

static void sdx_u_tmp(void)                          /* the last pass's leftover: its records kept (index.prt), or gone */
{
    sdx_head h;
    int part = 0;
    if (sdl_chain(SDX_TMP) && sdl_chain(SDX_TMP) == sdl_chain(SDX_FILE)) {
        (void)f_unlink_entry(SDX_TMP);               /* (a cut rename's other half: the index's own clusters) */
    } else if (f_open(&sdxp.pf, SDX_TMP, FA_READ) == FR_OK) {
        part = sdx_head_kind(&sdxp.pf, &h, 'P') == FR_OK;   /* (a cut pass with a checkpoint: newer than index.prt) */
        (void)f_close(&sdxp.pf);
        if (part)
            (void)f_unlink(SDX_PART);
        if (!part || sdl_move(SDX_TMP, SDX_PART) != FR_OK)
            (void)f_unlink(SDX_TMP);                 /* (a cut pass's, before its first checkpoint) */
    }
    sdxp.partn = sdxp.pcursor = 0;
    if (f_open(&sdxp.pf, SDX_PART, FA_READ) == FR_OK) {
        if (sdx_head_kind(&sdxp.pf, &h, 'P') == FR_OK) {
            sdxp.partn = h.count;
            sdxp.part_open = 1;
        } else {
            (void)f_close(&sdxp.pf);
            (void)f_unlink(SDX_PART);
        }
    }
    sdxp.st = SDXP_ROOM;
    sdxp.room_left = 0;
}

/* the walked file in index.prt (the walk's order is the cut pass's: its next record),
 * unchanged: its record taken (sdxp.rec), 1 */
static int sdx_part_hit(sdx_rec *into)            /* (into: sdxp.rec, or a scratch record to follow the walk alone) */
{
    if (!sdxp.part_open || sdxp.pcursor >= sdxp.partn || sdx_read_in(&sdxp.pf, sdxp.pcursor, into) != FR_OK ||
        sdl_cmp(into->path, sdx.walk.path + SDX_SKIP))
        return 0;                                    /* (not the next one: a file gone since, the rest hashed again) */
    sdxp.pcursor++;
    return into->size == (uint32_t)sdx.walk.fi.fsize && into->stamp == ((uint32_t)sdx.walk.fi.fdate << 16 | sdx.walk.fi.ftime);
}

/* The room for index.tmp, a sector of the FAT a unit. FatFs looks for a file's run from the volume's next-free hint, in one call,
 * and a computer may leave that hint at the card's start (macOS's newfs_msdos and its writes: 3 on the test's 8 GB card, 3 GB
 * used in 4 KB clusters: f_expand read 6,026 FAT sectors, 6.1 s in one main loop pass, the audio's starvation check fired; the
 * user, 2026-11-05: "CPU1 is on OVERLOAD" at 23 %). So the first run long enough is found here, and the hint set to it: index.tmp
 * and everything allocated after it start there. FAT32 only: FAT16's table is small */
static FRESULT sdx_u_room(uint8_t *buf)
{
    static FATFS *fs;
    uint32_t i, cl, c0;
    if (!sdxp.room_left) {
        DIR d;
        FRESULT r = f_opendir(&d, "");
        if (r != FR_OK)
            return r;
        fs = d.obj.fs;
        (void)f_closedir(&d);
        if (fs->fs_type != FS_FAT32) {
            sdxp.st = SDXP_NEW;
            return FR_OK;
        }
        c0 = (uint32_t)fs->last_clst >= 2u && (uint32_t)fs->last_clst < (uint32_t)fs->n_fatent ? (uint32_t)fs->last_clst : 2u;
        sdxp.room_at = c0 / 128u;
        sdxp.room_left = (uint32_t)fs->fsize;
        sdxp.room_n = 0;
    }
    if (disk_read(fs->pdrv, buf, fs->fatbase + sdxp.room_at, 1) != RES_OK)
        return FR_DISK_ERR;
    for (i = 0; i < 128u; i++) {
        cl = sdxp.room_at * 128u + i;
        if (cl < 2u || cl >= (uint32_t)fs->n_fatent || (sdh_le32(buf + i * 4u) & 0x0FFFFFFFu)) {
            sdxp.room_n = 0;
            continue;
        }
        if (!sdxp.room_n++)
            sdxp.room_cl = cl;
        if (sdxp.room_n * fs->csize * 512u >= (sdxp.n + 1u) * SDX_REC) {
            fs->last_clst = sdxp.room_cl;
            sdxp.st = SDXP_NEW;
            return FR_OK;
        }
    }
    sdxp.room_at = sdxp.room_at + 1u < (uint32_t)fs->fsize ? sdxp.room_at + 1u : 0u;
    sdxp.room_n = sdxp.room_at ? sdxp.room_n : 0u;   /* (the table's end: a run does not wrap) */
    return --sdxp.room_left ? FR_OK : FR_DENIED;     /* (no run that long: as f_expand says it) */
}

static FRESULT sdx_u_new(void)                       /* the new index's run, the walk again */
{
    FRESULT r = sda_create(&sdxp.nf, SDX_TMP, SDA_EXACT, (sdxp.n + 1u) * SDX_REC, 0);
    if (r != FR_OK)
        return r;
    sdxp.new_open = 1;
    memset(&sdxp.rec, 0, sizeof sdxp.rec);             /* the header slot zeroed: the run's old bytes never read as one */
    sdxp.cap = (uint32_t)(f_size(&sdxp.nf) / SDX_REC) - 1u;
    if ((r = sdx_rw(&sdxp.nf, 0, &sdxp.rec, SDX_REC, 1)) != FR_OK)
        return r;
    sdxp.ck_at = SD_NOW();
    sdxp.root = 0;
    sdxp.st = SDXP_WALK;
    return sdx_root_open();
}

/* the walked file matched (at: its old record, in sdxp.rec; -1 none): kept, or its hashing begun */
static FRESULT sdx_matched(int32_t at, uint8_t *buf)
{
    FRESULT r;
    if (at >= 0)
        sdxp.cursor = (uint32_t)at + 1u;
    sdx.files++;
    if (at >= 0 && sdxp.rec.size == (uint32_t)sdx.walk.fi.fsize &&
        sdxp.rec.stamp == ((uint32_t)sdx.walk.fi.fdate << 16 | sdx.walk.fi.ftime)) {
        sdx.kept++;                                  /* unchanged: its hash kept */
        sdxp.st = SDXP_PUT;
        (void)sdx_part_hit((sdx_rec *)(void *)buf);  /* (index.prt followed along: buf is free here, 512 bytes, aligned) */
        return FR_OK;
    }
    sdxp.used = at >= 0 ? sdxp.rec.used : 0u;
    if (sdx_part_hit(&sdxp.rec)) {                   /* (hashed by a pass that was cut: kept, its use as that pass kept it) */
        sdx.kept++;
        sdxp.st = SDXP_PUT;
        return FR_OK;
    }
    memset(&sdxp.rec, 0, sizeof sdxp.rec);
#if SDX_STATS
    DWORD o0 = SD_NOW();
#endif
    if ((r = f_open(&sdx.smp, sdx.walk.path, FA_READ)) != FR_OK)
        return r;
    sdxp.smp_open = 1;
    sdxp.st = SDXP_HASH;
    if (sdl_ncmp(sdx.walk.path + SDX_SKIP, "Samples/", 8)) {
        sdxp.left = 0;
        return sdx_id_of(&sdx.smp, &sdxp.rec, buf);
    }
    r = sdh_begin(&sdx.smp, buf, &sdxp.hs, &sdxp.left, &sdxp.rec.hlen);
#if SDX_STATS
    sxs.open_us += (uint32_t)(SD_NOW() - o0) * 1000u / SD_TICKS_MS;
#endif
    return r;
}

static FRESULT sdx_u_walk(uint8_t *buf)              /* the next file: its old record the next one, or a scan begun */
{
    const char *rel;
    int st;
    if (sdxp.k >= sdxp.cap) {
        sdxp.st = SDXP_END;
        return FR_OK;
    }
    if ((st = sdw_next(&sdx.walk)) <= 0) {           /* (this root done: the next one) */
        if (st < 0)
            return sdx.walk.err;
        if (++sdxp.root >= SDX_NROOT) {
            sdxp.st = SDXP_END;
            return FR_OK;
        }
        return sdx_root_open();
    }
    rel = sdx.walk.path + SDX_SKIP;
    if (sdl_len(rel) >= SDX_PATH || !sdx_wanted(rel))
        return FR_OK;
    if (sdxp.cursor < sdxp.oldn && sdx_read_in(&sdx.old, sdxp.cursor, &sdxp.rec) == FR_OK && !sdl_cmp(sdxp.rec.path, rel))
        return sdx_matched((int32_t)sdxp.cursor, buf);  /* (the last pass's order: the next record) */
    if (sdx_part_hit(&sdxp.rec)) {                   /* (a cut pass hashed it, unchanged: before any search, one read; the
                                                      * user, 2026-11-08: "MUCH faster for already scanned samples") */
        sdx.files++;
        sdx.kept++;
        sdxp.st = SDXP_PUT;
        return FR_OK;
    }
    if (!sdxp.oldn)
        return sdx_matched(-1, buf);
    sdxp.scan = 0;
    sdxp.st = SDXP_SCAN;
    return FR_OK;
}

static FRESULT sdx_u_scan(uint8_t *buf)              /* a record of the old index, from the cursor round */
{
    uint32_t no = (sdxp.cursor + sdxp.scan) % sdxp.oldn;
    if (sdx_read_in(&sdx.old, no, &sdxp.rec) != FR_OK)
        return sdx_matched(-1, buf);
    if (!sdl_cmp(sdxp.rec.path, sdx.walk.path + SDX_SKIP))
        return sdx_matched((int32_t)no, buf);
    return ++sdxp.scan < sdxp.oldn && sdxp.scan < SDX_SCAN_MAX ? FR_OK : sdx_matched(-1, buf);   /* (beyond: new, hashed) */
}

static FRESULT sdx_u_hash(uint8_t *buf, uint32_t bufn)   /* a chunk; the file's end */
{
    if (sdxp.left)
        return sdh_more(&sdx.smp, buf, bufn, &sdxp.hs, &sdxp.left);
    if (!sdxp.rec.flags)
        sdxp.rec.hash = sdh_end(&sdxp.hs);
    (void)f_close(&sdx.smp);
    sdxp.smp_open = 0;
    sdx_rec_end(&sdxp.rec, sdx.walk.path, &sdx.walk.fi, sdxp.used);
    sdx.hashed++;
    sdxp.st = SDXP_PUT;
    return FR_OK;
}

static FRESULT sdx_u_end(void)                       /* the header, last */
{
    FRESULT r = sdx_head_write(&sdxp.nf, sdxp.k, sdxp.cap, sdx.seq), c;
    sdxp.new_open = 0;
    c = f_close(&sdxp.nf);
    if (r == FR_OK && c != FR_OK)
        r = FR_DISK_ERR;
    sdxp.st = SDXP_UNLINK;
    return r;
}

static FRESULT sdx_u_unlink(void)                    /* the new index in the old one's place: the old one gone, */
{
    FRESULT r;
    if (sdxp.old_open) {
        (void)f_close(&sdx.old);
        sdxp.old_open = 0;
    }
    if (sdxp.part_open) {                            /* (a cut pass's records: all in the new index now) */
        (void)f_close(&sdxp.pf);
        sdxp.part_open = 0;
    }
    (void)f_unlink(SDX_PART);
    r = f_unlink(SDX_FILE);
    sdxp.st = SDXP_MOVE;
    return r == FR_NO_FILE ? FR_OK : r;
}

static FRESULT sdx_u_move(void)                      /* the new one renamed */
{
    FRESULT r = sdl_move(SDX_TMP, SDX_FILE);
    if (r != FR_OK)
        return r;
    sdx.count = sdxp.k;
    sdx.cap = sdxp.cap;
    sdxp.st = SDXP_SIG;
    return FR_OK;
}

static FRESULT sdx_u_sig(void)                       /* the signature, no allocation after it */
{
    sdx_sig_t s;
    FRESULT r = FR_OK;
    sdxp.st = SDXP_OFF;
    if (f_open(&sdx.f, SDX_FILE, FA_READ | FA_WRITE) == FR_OK) {   /* (no signature: only a walk at the next mount) */
        if (sdx_fs_sig(&s, &sdx.f) == FR_OK)
            r = sdx_rw(&sdx.f, SDX_SIG_AT, &s, sizeof s, 1);
        if (f_close(&sdx.f) != FR_OK && r == FR_OK)
            r = FR_DISK_ERR;
    }
    return r;
}

static FRESULT sdx_unit(uint8_t *buf, uint32_t bufn)
{
    FRESULT r;
    switch (sdxp.st) {
    case SDXP_START:
        sdxp.st = SDXP_COUNT;
        return sdx_root_open();
    case SDXP_COUNT:
        return sdx_u_count();
    case SDXP_OLD:
        return sdx_u_old();
    case SDXP_TMP:
        sdx_u_tmp();
        return FR_OK;
    case SDXP_ROOM:
        return sdx_u_room(buf);
    case SDXP_NEW:
        return sdx_u_new();
    case SDXP_WALK:
        return sdx_u_walk(buf);
    case SDXP_SCAN:
        return sdx_u_scan(buf);
    case SDXP_HASH:
        return sdx_u_hash(buf, bufn);
    case SDXP_PUT:
        r = sdx_rw(&sdxp.nf, (sdxp.k + 1u) * SDX_REC, &sdxp.rec, SDX_REC, 1);
        sdxp.k++;
        sdxp.st = SDXP_WALK;
        if (r == FR_OK && (uint32_t)(SD_NOW() - sdxp.ck_at) >= SDX_CKPT_MS * SD_TICKS_MS) {
            sdxp.ck_at = SD_NOW();                   /* the checkpoint: the records so far, kept should the pass be cut */
            if ((r = sdx_head_put(&sdxp.nf, 'P', sdxp.k, sdxp.cap, sdx.seq)) == FR_OK)
                r = f_sync(&sdxp.nf);
        }
        return r;
    case SDXP_END:
        return sdx_u_end();
    case SDXP_UNLINK:
        return sdx_u_unlink();
    case SDXP_MOVE:
        return sdx_u_move();
    default:
        return sdx_u_sig();
    }
}

static FRESULT sdx_pass_step(uint8_t *buf, uint32_t bufn, uint32_t ms)
{
    DWORD t0 = SD_NOW();
    FRESULT r = FR_OK;
    if (sdxp.st == SDXP_OFF && sdxp.redo)
        (void)sdx_pass_begin(1);
    if (sd_wr.state != SDW_IDLE)
        return FR_OK;                                /* (a recording's CMD25 open: no other command) */
#if SDX_STATS
    if (sdxp.st != SDXP_OFF)
        sxs.slices++;
#endif
    while (sdxp.st != SDXP_OFF && r == FR_OK && sd_ms_since(t0) < ms) {
#if SDX_STATS
        uint32_t st = sdxp.st, left = sdxp.left;
        DWORD u0 = SD_NOW();
#endif
        r = sdx_unit(buf, bufn);
#if SDX_STATS
        sxs_add(st, (uint32_t)(SD_NOW() - u0), st == SDXP_HASH && left ? left - sdxp.left : 0u);
#endif
    }
    if (r != FR_OK)
        sdx_pass_cancel();
    return r;
}

static FRESULT __attribute__((unused)) sdx_update(uint8_t *buf, uint32_t bufn)
{
    FRESULT r = FR_OK;
    (void)sdx_pass_begin(1);
    while (r == FR_OK && sdxp.st != SDXP_OFF)
        r = sdx_pass_step(buf, bufn, 0xFFFFFFFFu);
    return r;
}
