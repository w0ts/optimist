/* SPDX-License-Identifier: GPL-3.0-only */
/* A project saved to the card, safe over a power cut, with versions (docs/SD-CARD-DESIGN.md 8, the user 2026-10-25:
 * "we have flash for temp / buffer"). Full FatFs builds (FELUCCA_SD_FATFS); pure over FatFs and the hooks below,
 * host-tested with a power cut at every block write (tests/sd_layout_test.c). Main loop only.
 * The project is whole in the FM-1's own flash first (the autosave's copy: the caller's), so the card never holds the
 * only copy of the work:
 *   1. a pending record ("card save: <name>, save number, length, hash") written in flash (sdp_hooks.pend_set);
 *   2. the card file written from the flash copy, streamed a sector at a time, to <name>.TMP: its header sector zeroed,
 *      the payload, then the header; read back and its hash checked; the previous <name>.OPP moved into
 *      VERSIONS/<name>-NNNN.OPP (NNNN its save number); <name>.TMP renamed <name>.OPP; the versions past the newest
 *      sdp_keep dropped;
 *   3. the pending record cleared.
 * A cut anywhere: the card holds the old project whole or the new one (sdp_pick takes the newest whole copy among the
 * project's files), and the next boot finishes the save from the flash copy (sdp_boot). Every step checks what is on
 * the card first, so finishing is the same code as saving.
 * No fragmentation (sd_alloc.c): <name>.TMP is one run of the project's size x 1.5; the run a save drops (the oldest
 * version past sdp_keep, or with sdp_keep 0 the replaced project itself) is kept as <name>.SPR, the next save's run
 * when it is big enough (the user's "the next saves reuse the run").
 *   PROJECTS/<name>/<name>.OPP      the project: a header sector (sdp_head: "OPPJ", the save number, the length, the
 *                                   payload's XXH64, the header's own), the payload from byte 512, slack to the run's end
 *   PROJECTS/<name>/VERSIONS/<name>-NNNN.OPP   earlier saves (a number already there: -NNNN-2 .. -9)
 *   sdp_keep                        the versions kept (the setting; 0 none)
 *   sdp_save(name, number, len, buf)   the save: the staged copy (len bytes, read by hooks.read) to the card; buf 512
 *                                   B. FR_OK; else the error, the pending record left for sdp_boot
 *   sdp_boot(buf)                   at boot after the mount: a pending record finished. FR_OK (nothing pending too)
 *   sdp_pick(name, out, cap, &head, buf)   the project's whole copy (the header right, the payload's hash right): the
 *                                   .OPP, else (a cut between the moves) the newest of the .TMP, the spare and the
 *                                   versions by save number: its path; FR_NO_FILE when none */
#define SDP_SECTOR 512u
#define SDP_VERSIONS "VERSIONS"

typedef struct {
    char magic[4];                                   /* "OPPJ" */
    uint16_t version, head;                          /* 1, SDP_SECTOR (the payload's offset) */
    uint32_t number, len;                            /* the save's number, the payload's bytes */
    uint64_t hash;                                   /* XXH64 of the payload */
    uint64_t check;                                  /* XXH64 of the 24 bytes before */
} sdp_head;

typedef struct {
    char name[SDL_NAME_MAX + 1];
    uint32_t number, len;
    uint64_t hash;
} sdp_pending;

typedef struct {                                     /* the flash side (the caller's) */
    int (*read)(uint32_t at, void *p, uint32_t n);   /* the staged project's bytes: 0 ok */
    int (*pend_set)(const sdp_pending *p);           /* the pending record written: 0 ok */
    int (*pend_get)(sdp_pending *p);                 /* 1: one is there (into p) */
    int (*pend_clear)(void);                         /* 0 ok */
} sdp_hooks_t;

static sdp_hooks_t sdp_hooks;
static uint8_t sdp_keep = 8;                         /* (the setting) */
static struct {
    FIL f;
    char path[FF_LFN_BUF + 1], to[FF_LFN_BUF + 1];
    DIR d;
    FILINFO fi;
} sdp IN_POOL;

enum { SDP_DIR = 0, SDP_VDIR, SDP_OPP, SDP_TMP, SDP_SPR, SDP_VER };

static uint32_t sdp_num(char *out, uint32_t at, uint32_t cap, uint32_t v, uint32_t digits)
{
    char t[12];
    uint32_t n = 0;
    do {
        t[n++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v || n < digits);
    while (n && at < cap)
        out[at++] = t[--n];
    if (at >= cap)
        return cap;
    out[at] = 0;
    return at;
}

/* a project's paths: its folder, VERSIONS, the .OPP, .TMP, .SPR, a version (number, dup 0 or 2..9); 0: too long */
static uint32_t sdp_path(char *out, const char *name, uint32_t what, uint32_t number, uint32_t dup)
{
    static const char *const EXT[3] = {".OPP", ".TMP", ".SPR"};
    uint32_t cap = FF_LFN_BUF + 1u, n = sdl_path(out, cap, SDL_PROJECTS, name);
    if (!n)
        return 0;
    if (what == SDP_VDIR || what == SDP_VER) {
        n = sdl_cat(out, n, cap, "/" SDP_VERSIONS);
    }
    if (what != SDP_DIR && what != SDP_VDIR) {
        n = sdl_cat(out, n, cap, "/");
        n = sdl_cat(out, n, cap, name);
        if (what == SDP_VER) {
            n = sdl_cat(out, n, cap, "-");
            n = sdp_num(out, n, cap, number, 4);
            if (dup) {
                n = sdl_cat(out, n, cap, "-");
                n = sdp_num(out, n, cap, dup, 1);
            }
            n = sdl_cat(out, n, cap, ".OPP");
        } else {
            n = sdl_cat(out, n, cap, EXT[what - SDP_OPP]);
        }
    }
    return n < cap ? n : 0;
}

static uint64_t sdp_check(const sdp_head *h) { sdh_t s; sdh_init(&s); sdh_add(&s, h, 24); return sdh_end(&s); }

/* the open file's header; with buf, its payload's hash checked too (the file read through): FR_OK when whole */
static FRESULT sdp_verify(FIL *f, sdp_head *h, uint8_t *buf)
{
    sdh_t s;
    UINT got;
    uint32_t left;
    FRESULT r = f_lseek(f, 0);
    if (r == FR_OK)
        r = f_read(f, h, sizeof *h, &got);
    if (r != FR_OK)
        return r;
    if (got != sizeof *h || memcmp(h->magic, "OPPJ", 4) || h->version != 1u || h->head != SDP_SECTOR ||
        h->check != sdp_check(h) || f_size(f) < (FSIZE_t)SDP_SECTOR + h->len)
        return FR_NO_FILESYSTEM;
    if (!buf)
        return FR_OK;
    sdh_init(&s);
    left = h->len;
    if ((r = f_lseek(f, SDP_SECTOR)) != FR_OK)
        return r;
    while (left && (r = f_read(f, buf, left < SDP_SECTOR ? left : SDP_SECTOR, &got)) == FR_OK && got) {
        sdh_add(&s, buf, got);
        left -= got;
    }
    if (r != FR_OK)
        return r;
    return !left && sdh_end(&s) == h->hash ? FR_OK : FR_NO_FILESYSTEM;
}

static FRESULT sdp_verify_path(const char *path, sdp_head *h, uint8_t *buf)   /* FR_NO_FILE: none there */
{
    FRESULT r = f_open(&sdp.f, path, FA_READ);
    if (r != FR_OK)
        return r;
    r = sdp_verify(&sdp.f, h, buf);
    (void)f_close(&sdp.f);
    return r;
}

static FRESULT sdp_drop(const char *path, const char *name)   /* a run no longer wanted: the spare if there is none */
{
    FRESULT r;
    char *spr = sdp.to;
    if (!sdp_path(spr, name, SDP_SPR, 0, 0))
        return FR_INVALID_NAME;
    r = sdl_move(path, spr);
    if (r == FR_EXIST)
        r = f_unlink(path);                          /* (a spare there already, its own chain) */
    return r;
}

/* the temporary file, opened for writing: a valid one of this save (the payload checked) is kept as it is (1 in
 * *done), else one big enough is reused (the spare, or a cut save's), else a new run */
static FRESULT sdp_temp(const sdp_pending *p, uint8_t *buf, int *done)
{
    sdp_head h;
    char *tmp = sdp.path, *spr = sdp.to;
    uint32_t need = SDP_SECTOR + p->len;
    FRESULT r;
    *done = 0;
    if (!sdp_path(tmp, p->name, SDP_TMP, 0, 0) || !sdp_path(spr, p->name, SDP_SPR, 0, 0))
        return FR_INVALID_NAME;
    r = f_open(&sdp.f, tmp, FA_READ | FA_WRITE);
    if (r == FR_NO_FILE && (r = sdl_move(spr, tmp)) == FR_OK)   /* (the spare's run: the next save's) */
        r = f_open(&sdp.f, tmp, FA_READ | FA_WRITE);
    if (r == FR_OK) {
        if (sdp_verify(&sdp.f, &h, buf) == FR_OK && h.number == p->number && h.hash == p->hash && h.len == p->len) {
            *done = 1;
            return FR_OK;
        }
        if (f_size(&sdp.f) >= need)
            return FR_OK;                            /* reused: written over */
        (void)f_close(&sdp.f);
        if ((r = f_unlink(tmp)) != FR_OK)
            return r;
    } else if (r != FR_NO_FILE) {
        return r;
    }
    return sda_create(&sdp.f, tmp, SDA_PROJECT, need, 0);
}

static FRESULT sdp_write(const sdp_pending *p, uint8_t *buf)   /* the staged copy into sdp.f, then checked */
{
    sdp_head h;
    UINT put;
    uint32_t at = 0, n;
    FRESULT r;
    memset(buf, 0, SDP_SECTOR);                      /* the header first gone: a run's old bytes never read as one */
    r = f_lseek(&sdp.f, 0);
    if (r == FR_OK)
        r = f_write(&sdp.f, buf, SDP_SECTOR, &put);
    while (r == FR_OK && at < p->len) {
        n = p->len - at < SDP_SECTOR ? p->len - at : SDP_SECTOR;
        if (sdp_hooks.read(at, buf, n))
            r = FR_INT_ERR;
        else
            r = f_write(&sdp.f, buf, n, &put);
        at += n;
    }
    if (r == FR_OK)
        r = f_sync(&sdp.f);                          /* (the payload on the card before the header says it is whole) */
    memset(&h, 0, sizeof h);
    memcpy(h.magic, "OPPJ", 4);
    h.version = 1;
    h.head = SDP_SECTOR;
    h.number = p->number;
    h.len = p->len;
    h.hash = p->hash;
    h.check = sdp_check(&h);
    if (r == FR_OK && (r = f_lseek(&sdp.f, 0)) == FR_OK)
        r = f_write(&sdp.f, &h, sizeof h, &put);
    if (r == FR_OK)
        r = f_sync(&sdp.f);
    if (r == FR_OK && ((r = sdp_verify(&sdp.f, &h, buf)) != FR_OK || h.number != p->number || h.hash != p->hash))
        r = r != FR_OK ? r : FR_INT_ERR;             /* (read back: what the card holds is what was staged) */
    return r;
}

/* the old project (a header that does not read: number 0) into VERSIONS, or the spare with no versions kept */
static FRESULT sdp_retire(const sdp_pending *p, char *opp, uint8_t *buf)
{
    sdp_head h;
    uint32_t number, dup;
    FRESULT r = sdp_verify_path(opp, &h, 0);
    if (r == FR_NO_FILE)
        return FR_OK;                                /* (none: a first save, or a cut after the move) */
    if (r != FR_OK && r != FR_NO_FILESYSTEM)
        return r;
    if (!sdp_keep)
        return sdp_drop(opp, p->name);
    number = r == FR_OK ? h.number : 0;
    for (dup = 0; dup < 10u; dup = dup ? dup + 1u : 2u) {
        if (!sdp_path(sdp.to, p->name, SDP_VER, number, dup))
            return FR_INVALID_NAME;
        if ((r = sdl_move(opp, sdp.to)) != FR_EXIST)
            return r;
    }
    return FR_EXIST;
}

static FRESULT sdp_prune(const sdp_pending *p)       /* the versions past the newest sdp_keep dropped, oldest first */
{
    uint32_t n, oldest, num, at = (uint32_t)sdl_len(p->name), i, len;
    FRESULT r;
    char *v = sdp.path, *old = sdp.to;               /* the folder; the oldest's name */
    for (;;) {
        if (!(len = sdp_path(v, p->name, SDP_VDIR, 0, 0)))
            return FR_INVALID_NAME;
        if ((r = f_opendir(&sdp.d, v)) != FR_OK)
            return r == FR_NO_PATH ? FR_OK : r;
        n = 0;
        oldest = 0xFFFFFFFFu;
        old[0] = 0;
        while ((r = f_readdir(&sdp.d, &sdp.fi)) == FR_OK && sdp.fi.fname[0]) {
            if (sdl_hidden(&sdp.fi) || sdl_ncmp(sdp.fi.fname, p->name, at) || sdp.fi.fname[at] != '-')
                continue;
            for (num = 0, i = at + 1u; sdp.fi.fname[i] >= '0' && sdp.fi.fname[i] <= '9'; i++)
                num = num * 10u + (uint32_t)(sdp.fi.fname[i] - '0');
            n++;
            if (num < oldest || (num == oldest && sdl_cmp(sdp.fi.fname, old) < 0)) {
                oldest = num;
                sdl_cat(old, 0, sizeof sdp.to, sdp.fi.fname);
            }
        }
        (void)f_closedir(&sdp.d);
        if (r != FR_OK || n <= sdp_keep)
            return r;
        if (sdl_cat(v, sdl_cat(v, len, sizeof sdp.path, "/"), sizeof sdp.path, old) >= sizeof sdp.path)
            return FR_INVALID_NAME;
        if ((r = sdp_drop(v, p->name)) != FR_OK)     /* (sdp_drop's spare path goes to sdp.to: old is read already) */
            return r;
    }
}

/* the halves of renames a cut left: a pair of the project's files that is one chain loses its rename source's name
 * (the moves: .OPP -> a version or the spare, .TMP -> .OPP, the spare -> .TMP, a version -> the spare) */
static FRESULT sdp_dedupe(const char *name)
{
    char v[FF_LFN_BUF + 1];
    DWORD c[3], ch;
    uint32_t i, n;
    FRESULT r = FR_OK;
    for (i = 0; i < 3u; i++)
        c[i] = sdp_path(sdp.path, name, SDP_OPP + i, 0, 0) ? sdl_chain(sdp.path) : 0;   /* .OPP, .TMP, .SPR */
    for (i = 0; i < 3u && r == FR_OK; i++) {          /* .TMP = .OPP, .SPR = .TMP, .OPP = .SPR: drop the first */
        uint32_t src = (i + 1u) % 3u, dst = i;
        if (c[src] && c[src] == c[dst]) {
            sdp_path(sdp.path, name, SDP_OPP + src, 0, 0);
            r = f_unlink_entry(sdp.path);
            c[src] = 0;
        }
    }
    if (r != FR_OK || !(n = sdp_path(v, name, SDP_VDIR, 0, 0)) || f_opendir(&sdp.d, v) != FR_OK)
        return r;
    while (r == FR_OK && f_readdir(&sdp.d, &sdp.fi) == FR_OK && sdp.fi.fname[0]) {
        if (sdl_cat(v, sdl_cat(v, n, sizeof v, "/"), sizeof v, sdp.fi.fname) >= sizeof v)
            continue;
        ch = sdl_chain(v);
        if (ch && ch == c[0]) {                      /* .OPP -> this version */
            sdp_path(sdp.path, name, SDP_OPP, 0, 0);
            r = f_unlink_entry(sdp.path);
            c[0] = 0;
        } else if (ch && ch == c[2]) {               /* this version -> the spare */
            r = f_unlink_entry(v);
        }
        v[n] = 0;
    }
    (void)f_closedir(&sdp.d);
    return r;
}

static FRESULT sdp_finish(const sdp_pending *p, uint8_t *buf)
{
    sdp_head h;
    char opp[FF_LFN_BUF + 1];
    int done;
    FRESULT r;
    if (!sdl_name_ok(p->name) || !sdp_path(opp, p->name, SDP_DIR, 0, 0))
        return FR_INVALID_NAME;
    if ((r = sdl_mkdir(opp)) != FR_OK)
        return r;
    if (sdp_keep && (!sdp_path(opp, p->name, SDP_VDIR, 0, 0) || (r = sdl_mkdir(opp)) != FR_OK))
        return r != FR_OK ? r : FR_INVALID_NAME;
    if ((r = sdp_dedupe(p->name)) != FR_OK)
        return r;
    sdp_path(opp, p->name, SDP_OPP, 0, 0);
    r = sdp_verify_path(opp, &h, buf);
    if (r != FR_OK || h.number != p->number || h.hash != p->hash) {   /* (not already done: past the rename) */
        if ((r = sdp_temp(p, buf, &done)) != FR_OK)
            return r;
        r = done ? FR_OK : sdp_write(p, buf);
        if (f_close(&sdp.f) != FR_OK && r == FR_OK)
            r = FR_DISK_ERR;
        if (r == FR_OK)
            r = sdp_retire(p, opp, buf);
        if (r == FR_OK && sdp_path(sdp.path, p->name, SDP_TMP, 0, 0))
            r = sdl_move(sdp.path, opp);
        if (r != FR_OK)
            return r;
    }
    if (sdp_keep)
        r = sdp_prune(p);
    if (r == FR_OK && sdp_hooks.pend_clear())
        r = FR_INT_ERR;
    return r;
}

/* the staged copy's hash (read through the hook) */
static int sdp_stage_hash(uint32_t len, uint8_t *buf, uint64_t *hash)
{
    sdh_t s;
    uint32_t at, n;
    sdh_init(&s);
    for (at = 0; at < len; at += n) {
        n = len - at < SDP_SECTOR ? len - at : SDP_SECTOR;
        if (sdp_hooks.read(at, buf, n))
            return -1;
        sdh_add(&s, buf, n);
    }
    *hash = sdh_end(&s);
    return 0;
}

static FRESULT sdp_save(const char *name, uint32_t number, uint32_t len, uint8_t *buf)
{
    sdp_pending p;
    if (!sdl_name_ok(name))
        return FR_INVALID_NAME;
    memset(&p, 0, sizeof p);
    sdl_cat(p.name, 0, sizeof p.name, name);
    p.number = number;
    p.len = len;
    if (sdp_stage_hash(len, buf, &p.hash) || sdp_hooks.pend_set(&p))
        return FR_INT_ERR;
    return sdp_finish(&p, buf);
}

static FRESULT sdp_boot(uint8_t *buf)
{
    sdp_pending p;
    return sdp_hooks.pend_get(&p) ? sdp_finish(&p, buf) : FR_OK;
}

/* the project's whole copy: its .OPP; with none whole there (a cut between the moves), the newest whole one of the
 * .TMP, the spare and the versions, by save number */
static FRESULT sdp_pick(const char *name, char *out, uint32_t cap, sdp_head *best, uint8_t *buf)
{
    sdp_head h;
    uint32_t what, found = 0;
    char *v = sdp.to;
    for (what = SDP_OPP; what <= SDP_VER; what++) {
        if (what < SDP_VER) {
            if (!sdp_path(sdp.path, name, what, 0, 0))
                return FR_INVALID_NAME;
            if (sdp_verify_path(sdp.path, &h, buf) == FR_OK && (!found || h.number > best->number)) {
                *best = h;
                found = sdl_cat(out, 0, cap, sdp.path) < cap;
            }
            if (found && what == SDP_OPP)
                return FR_OK;
            continue;
        }
        if (!sdp_path(v, name, SDP_VDIR, 0, 0) || f_opendir(&sdp.d, v) != FR_OK)
            break;
        while (f_readdir(&sdp.d, &sdp.fi) == FR_OK && sdp.fi.fname[0]) {
            uint32_t n = sdl_cat(sdp.path, 0, sizeof sdp.path, v);
            n = sdl_cat(sdp.path, n, sizeof sdp.path, "/");
            n = sdl_cat(sdp.path, n, sizeof sdp.path, sdp.fi.fname);
            if (sdl_hidden(&sdp.fi) || n >= sizeof sdp.path)
                continue;
            if (sdp_verify_path(sdp.path, &h, buf) == FR_OK && (!found || h.number > best->number)) {
                *best = h;
                found = sdl_cat(out, 0, cap, sdp.path) < cap;
            }
        }
        (void)f_closedir(&sdp.d);
    }
    return found ? FR_OK : FR_NO_FILE;
}
