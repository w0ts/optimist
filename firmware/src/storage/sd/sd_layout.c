/* SPDX-License-Identifier: GPL-3.0-only */
/* The card's own folder (docs/SD-CARD-DESIGN.md 8, the user's rulings 2026-10-25: "fully for a fixed folder
 * structure", names free inside it). Full FatFs builds (FELUCCA_SD_FATFS); pure over FatFs, host-tested
 * (tests/sd_layout_test.c):
 *   /OPTIMIST/                     the FM-1's; anything else on the card is left alone
 *     PROJECTS/<name>/<name>.OPP   a project (sd_save.c), VERSIONS/ the earlier saves
 *     SAMPLES/                     samples, in any sub-folders the user makes
 *     KITS/, PRESETS/              by name
 *     RECORD/, RENDERS/            recordings, renders
 *     BACKUP/                      whole-device backups
 *     SYSTEM/INDEX.BIN             the sample index (sd_index.c)
 *   sdl_prepare()            the folders made where missing (a fresh card, or one a computer emptied): FR_OK, or the
 *                            first error (FR_DENIED: a file where a folder must be)
 *   sdl_path(out, cap, folder, name)   "OPTIMIST/<FOLDER>/<name>" (name 0: the folder): its length, 0 when it does not
 *                            fit cap
 *   sdl_name_ok(name)        a name the FM-1 may give a file or folder: 1..SDL_NAME_MAX bytes of UTF-8, none of
 *                            / \ : * ? " < > | or control characters, not "." or "..", no trailing dot or space
 *   sdl_hidden(fi)           an entry the FM-1 never shows or indexes: hidden or system (FAT attributes), or a name
 *                            starting with a dot (a computer's own: macOS ._ files, .fseventsd, .Trashes)
 *   sdl_move(src, dst)       f_rename, safe over a cut: FatFs writes the new entry, then removes the old one, so a cut
 *                            between leaves one cluster chain under both names (and an f_unlink of either would
 *                            free the other's clusters); a dst that is src's chain already completes that rename
 *                            (src's entry removed, the chain kept: ff.c f_unlink_entry). sdl_chain(path): a file's
 *                            first cluster (0: none, or empty), what tells the two halves apart
 * A folder tree's files one by one (the sample index's pass, the browser's views across folders), depth first in
 * the folders' own order (stable while the card is unchanged), hidden entries skipped, SDW_DEPTH folders deep:
 *   sdw_open(w, root)        from the folder root ("OPTIMIST/SAMPLES"): FR_OK or FatFs' error
 *   sdw_next(w)              1: the next file, w->path its path, w->fi its entry; 0: no more; -1: an error (w->err);
 *                            a path that would not fit w->path is skipped (w->skipped counts them) */
#ifndef IN_POOL
#define IN_POOL                                      /* (a host test: no pool section) */
#endif
#define SDL_ROOT "OPTIMIST"
#define SDL_NAME_MAX 63u                             /* a name's bytes: PROJECTS/<name>/VERSIONS/<name>-NNNN.OPP stays
                                                      * far inside FatFs' 255 */
enum { SDL_PROJECTS = 0, SDL_SAMPLES, SDL_KITS, SDL_PRESETS, SDL_RECORD, SDL_RENDERS, SDL_BACKUP, SDL_SYSTEM, SDL_N };
static const char *const SDL_DIR[SDL_N] = {"PROJECTS", "SAMPLES", "KITS", "PRESETS", "RECORD", "RENDERS", "BACKUP",
                                           "SYSTEM"};

static uint32_t sdl_len(const char *s)               /* (the firmware has no libc: strlen, strcmp, strncmp) */
{
    uint32_t n = 0;
    while (s[n])
        n++;
    return n;
}

static int sdl_ncmp(const char *a, const char *b, uint32_t n)
{
    for (; n && *a && *a == *b; n--, a++, b++)
        ;
    return n ? (uint8_t)*a - (uint8_t)*b : 0;
}

static int sdl_cmp(const char *a, const char *b) { return sdl_ncmp(a, b, 0xFFFFFFFFu); }

static uint32_t sdl_cat(char *out, uint32_t at, uint32_t cap, const char *s)   /* s at out + at: the new length, or
                                                                                * cap when it does not fit */
{
    while (*s && at < cap)
        out[at++] = *s++;
    if (*s || at >= cap)
        return cap;
    out[at] = 0;
    return at;
}

static uint32_t sdl_path(char *out, uint32_t cap, uint32_t folder, const char *name)
{
    uint32_t n = sdl_cat(out, 0, cap, SDL_ROOT);
    if (folder < SDL_N) {
        n = sdl_cat(out, n, cap, "/");
        n = sdl_cat(out, n, cap, SDL_DIR[folder]);
    }
    if (name && *name) {
        n = sdl_cat(out, n, cap, "/");
        n = sdl_cat(out, n, cap, name);
    }
    if (n >= cap) {
        if (cap)
            out[0] = 0;
        return 0;
    }
    return n;
}

static int sdl_name_ok(const char *name)
{
    uint32_t n = 0;
    uint8_t c = 0;
    if (!name || !name[0] || (name[0] == '.' && (!name[1] || (name[1] == '.' && !name[2]))))
        return 0;
    for (; name[n]; n++) {
        c = (uint8_t)name[n];
        if (c < 0x20u || c == 0x7Fu || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' ||
            c == '<' || c == '>' || c == '|')
            return 0;
    }
    return n <= SDL_NAME_MAX && c != '.' && c != ' ';
}

static int sdl_hidden(const FILINFO *fi) { return (fi->fattrib & (AM_HID | AM_SYS)) || fi->fname[0] == '.'; }

static FRESULT sdl_mkdir(const char *path)           /* a folder there, made or found */
{
    FILINFO fi;
    FRESULT r = f_mkdir(path);
    if (r == FR_EXIST)
        r = f_stat(path, &fi) != FR_OK ? FR_DISK_ERR : (fi.fattrib & AM_DIR) ? FR_OK : FR_DENIED;
    return r;
}

static DWORD sdl_chain(const char *path)
{
    FIL f;
    DWORD c;
    if (f_open(&f, path, FA_READ) != FR_OK)
        return 0;
    c = f.obj.sclust;
    (void)f_close(&f);
    return c;
}

static FRESULT sdl_move(const char *src, const char *dst)
{
    DWORD c = sdl_chain(src);
    FRESULT r = f_rename(src, dst);
    if (r == FR_EXIST && c && sdl_chain(dst) == c)
        r = f_unlink_entry(src);                     /* (a cut rename: done now) */
    return r;
}

static FRESULT sdl_prepare(void)
{
    char p[24];
    uint32_t i;
    FRESULT r = sdl_mkdir(SDL_ROOT);
    for (i = 0; i < SDL_N && r == FR_OK; i++) {
        sdl_path(p, sizeof p, i, 0);
        r = sdl_mkdir(p);
    }
    return r;
}

#define SDW_DEPTH 8u
typedef struct {
    DIR d[SDW_DEPTH];
    uint16_t at[SDW_DEPTH];                          /* the path's length at each level */
    uint8_t depth;
    FRESULT err;
    uint32_t skipped;
    FILINFO fi;
    char path[FF_LFN_BUF + 1];
} sdw_t;

static FRESULT sdw_open(sdw_t *w, const char *root)
{
    uint32_t n = sdl_cat(w->path, 0, sizeof w->path, root);
    w->depth = 0;
    w->skipped = 0;
    w->err = n >= sizeof w->path ? FR_INVALID_NAME : f_opendir(&w->d[0], w->path);
    w->at[0] = (uint16_t)n;
    w->depth = w->err == FR_OK;
    return w->err;
}

static int sdw_next(sdw_t *w)
{
    uint32_t n, lv;
    while (w->depth) {
        lv = w->depth - 1u;
        w->path[w->at[lv]] = 0;
        if ((w->err = f_readdir(&w->d[lv], &w->fi)) != FR_OK)
            return -1;
        if (!w->fi.fname[0]) {                       /* this folder done: back up one */
            (void)f_closedir(&w->d[lv]);
            w->depth--;
            continue;
        }
        if (sdl_hidden(&w->fi))
            continue;
        n = sdl_cat(w->path, w->at[lv], sizeof w->path, "/");
        n = sdl_cat(w->path, n, sizeof w->path, w->fi.fname);
        if (n >= sizeof w->path) {
            w->skipped++;
            continue;
        }
        if (!(w->fi.fattrib & AM_DIR))
            return 1;
        if (w->depth < SDW_DEPTH) {
            if ((w->err = f_opendir(&w->d[w->depth], w->path)) != FR_OK)
                return -1;
            w->at[w->depth++] = (uint16_t)n;
        } else {
            w->skipped++;                            /* (deeper than SDW_DEPTH: not walked) */
        }
    }
    w->path[w->at[0]] = 0;
    return 0;
}
