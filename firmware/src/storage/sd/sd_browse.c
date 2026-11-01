/* SPDX-License-Identifier: GPL-3.0-only */
/* The card's files on the 16 keys (docs/SD-CARD-DESIGN.md 8, the user's ruling 2026-10-25): a list as pages of 16
 * slots, the page turned by the knob, the same list sorted / filtered by a view. Full FatFs builds (FELUCCA_SD_FATFS);
 * the data model only (the UI page draws b->slot[0 .. b->n - 1] and calls these), pure over FatFs, host-tested
 * (tests/sd_layout_test.c). Main loop only.
 * Views:
 *   SDB_FOLDER   one folder's entries: its folders first, then its files, each by name (the way to walk the card)
 *   SDB_BYNAME   every file under a folder, its sub-folders' too, by name (a flat list: "all the kicks")
 *   SDB_RECENT   the samples the FM-1 used, last used first (the index's use numbers: sd_index.c sdx_touch)
 * Hidden entries (sd_layout.c sdl_hidden: a computer's own) are never listed. A page is found without the list in
 * RAM: each turn is one pass over the view's source keeping the 16 entries that follow the page's last one (or come
 * before its first) in the view's order (keyset paging, 16 slots of RAM whatever the folder holds); the order is
 * total and the same in every pass: (folders first,) the name's first SDB_NAME - 1 bytes without case (ASCII), then
 * the entry's place in its source, so names equal that far keep their order on the card and no entry is shown twice
 * or skipped. A slot keeps its name cut there (on a UTF-8 boundary: cut set); sdb_path finds the whole path again.
 *   sdb_open(b, view, root)   the view of root ("OPTIMIST/SAMPLES"; ignored by SDB_RECENT): page 0. FR_OK, or
 *                             FatFs' error (b->n 0)
 *   sdb_turn(b, step)         the page step pages on (+1, -1, more: one pass a page), clamped to the first and last
 *   b->page, b->pages, b->total, b->n, b->slot[k]   what the page shows: slot k on key k (name, dir, cut)
 *   sdb_path(b, k, out, cap)  slot k's whole path from the card's root: its length, 0 when it is gone or does not fit
 *   sdb_into(b, k), sdb_up(b)   SDB_FOLDER: into the folder on slot k / up to the folder above (never above OPTIMIST),
 *                             page 0: FR_OK, FR_NO_PATH (slot k no folder, or already at the top) */
#define SDB_KEYS 16u
#define SDB_NAME 48u
enum { SDB_FOLDER = 0, SDB_BYNAME, SDB_RECENT };

typedef struct {
    char name[SDB_NAME];                             /* shown; the sort key (cut at SDB_NAME - 1 bytes) */
    uint32_t ord;                                    /* its place in the source: the folder's order, the walk's, the
                                                      * index's record number */
    uint32_t used;                                   /* SDB_RECENT's key */
    uint8_t dir, cut;
} sdb_item;

typedef struct {
    uint8_t view, n;
    uint32_t page, pages, total;
    char root[SDB_NAME * 2];
    sdb_item slot[SDB_KEYS];
} sdb_t;

static struct {                                      /* the source being read (one pass at a time) */
    uint32_t ord;
    DIR d;
    FILINFO fi;
    FIL f;
    sdw_t walk;
    sdx_rec rec;
} sdb_src IN_POOL;

static int sdb_ci(uint8_t c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

static int sdb_cmp(uint32_t view, const sdb_item *a, const sdb_item *b)
{
    const uint8_t *p = (const uint8_t *)a->name, *q = (const uint8_t *)b->name;
    if (view == SDB_RECENT) {
        if (a->used != b->used)
            return a->used > b->used ? -1 : 1;
    } else {
        if (view == SDB_FOLDER && a->dir != b->dir)
            return a->dir ? -1 : 1;
        for (; *p && sdb_ci(*p) == sdb_ci(*q); p++, q++)
            ;
        if (sdb_ci(*p) != sdb_ci(*q))
            return sdb_ci(*p) < sdb_ci(*q) ? -1 : 1;
    }
    return a->ord < b->ord ? -1 : a->ord > b->ord;
}

static void sdb_name(sdb_item *it, const char *name)  /* the name cut to SDB_NAME - 1 bytes, never inside a UTF-8 char */
{
    uint32_t n = 0;
    while (name[n] && n < SDB_NAME - 1u)
        n++;
    it->cut = name[n] != 0;
    if (it->cut)
        while (n && ((uint8_t)name[n] & 0xC0u) == 0x80u)
            n--;
    memcpy(it->name, name, n);
    it->name[n] = 0;
}

static const char *sdb_base(const char *path)       /* a path's last part */
{
    const char *s = path;
    for (; *path; path++)
        if (*path == '/')
            s = path + 1;
    return s;
}

static FRESULT sdb_src_open(const sdb_t *b)
{
    sdx_head h;
    FRESULT r;
    sdb_src.ord = 0;
    if (b->view == SDB_FOLDER)
        return f_opendir(&sdb_src.d, b->root);
    if (b->view == SDB_BYNAME)
        return sdw_open(&sdb_src.walk, b->root);
    r = sdx_open_file(&sdb_src.f, FA_READ);
    if (r == FR_OK && (r = sdx_head_read(&sdb_src.f, &h)) == FR_OK)
        sdx.count = h.count;
    if (r == FR_NO_FILE)
        sdx.count = 0;                               /* (no index: nothing used yet) */
    return r;
}

static void sdb_src_close(const sdb_t *b)
{
    if (b->view == SDB_FOLDER)
        (void)f_closedir(&sdb_src.d);
    else if (b->view == SDB_RECENT && sdb_src.f.obj.fs)
        (void)f_close(&sdb_src.f);
}

/* the source's next entry: 1, 0 the end, -1 an error (r) */
static int sdb_src_next(const sdb_t *b, sdb_item *it, FRESULT *r)
{
    int st;
    for (;;) {
        it->ord = sdb_src.ord++;
        it->used = 0;
        it->dir = 0;
        if (b->view == SDB_FOLDER) {
            if ((*r = f_readdir(&sdb_src.d, &sdb_src.fi)) != FR_OK)
                return -1;
            if (!sdb_src.fi.fname[0])
                return 0;
            if (sdl_hidden(&sdb_src.fi)) {
                sdb_src.ord--;
                continue;
            }
            it->dir = (sdb_src.fi.fattrib & AM_DIR) != 0;
            sdb_name(it, sdb_src.fi.fname);
            return 1;
        }
        if (b->view == SDB_BYNAME) {
            if ((st = sdw_next(&sdb_src.walk)) <= 0) {
                *r = sdb_src.walk.err;
                return st;
            }
            sdb_name(it, sdb_src.walk.fi.fname);
            return 1;
        }
        if (it->ord >= sdx.count || !sdb_src.f.obj.fs)
            return 0;
        if ((*r = sdx_read_in(&sdb_src.f, it->ord, &sdb_src.rec)) != FR_OK)
            return -1;
        if (!sdb_src.rec.path[0] || !sdb_src.rec.used)
            continue;                                /* (never used: not recent) */
        it->used = sdb_src.rec.used;
        sdb_name(it, sdb_base(sdb_src.rec.path));
        return 1;
    }
}

/* one pass: the page after `after` (the 16 first past it), or before `before` (the 16 last before it), or the first */
static FRESULT sdb_fill(sdb_t *b, const sdb_item *after, const sdb_item *before)
{
    sdb_item it;
    FRESULT r = sdb_src_open(b);
    uint32_t n = 0, i, total = 0;
    int st;
    if (r == FR_NO_FILE && b->view == SDB_RECENT)
        r = FR_OK;
    while (r == FR_OK && (st = sdb_src_next(b, &it, &r)) > 0) {
        total++;
        if ((after && sdb_cmp(b->view, &it, after) <= 0) || (before && sdb_cmp(b->view, &it, before) >= 0))
            continue;
        if (n == SDB_KEYS && (before ? sdb_cmp(b->view, &it, &b->slot[0]) <= 0
                                     : sdb_cmp(b->view, &it, &b->slot[SDB_KEYS - 1u]) >= 0))
            continue;                                /* (outside the 16 kept) */
        if (n == SDB_KEYS && before) {               /* the smallest drops out */
            memmove(&b->slot[0], &b->slot[1], (SDB_KEYS - 1u) * sizeof it);
            n--;
        } else if (n == SDB_KEYS) {
            n--;                                     /* the largest drops out */
        }
        for (i = n; i && sdb_cmp(b->view, &it, &b->slot[i - 1u]) < 0; i--)
            b->slot[i] = b->slot[i - 1u];
        b->slot[i] = it;
        n++;
    }
    sdb_src_close(b);
    b->n = (uint8_t)n;
    b->total = total;
    b->pages = total ? (total + SDB_KEYS - 1u) / SDB_KEYS : 1u;
    return r;
}

static FRESULT sdb_open(sdb_t *b, uint32_t view, const char *root)
{
    b->view = (uint8_t)view;
    b->page = 0;
    b->n = 0;
    if (sdl_cat(b->root, 0, sizeof b->root, root ? root : "") >= sizeof b->root)
        return FR_INVALID_NAME;
    return sdb_fill(b, 0, 0);
}

static FRESULT sdb_turn(sdb_t *b, int32_t step)
{
    sdb_item edge;
    FRESULT r = FR_OK;
    for (; step > 0 && r == FR_OK && b->page + 1u < b->pages && b->n == SDB_KEYS; step--) {
        edge = b->slot[b->n - 1u];
        if ((r = sdb_fill(b, &edge, 0)) == FR_OK && !b->n)
            r = sdb_fill(b, 0, 0), b->page = 0;      /* (the card changed under the page: back to the first) */
        else
            b->page++;
    }
    for (; step < 0 && r == FR_OK && b->page; step++) {
        edge = b->slot[0];
        if (b->page == 1u || !b->n) {
            r = sdb_fill(b, 0, 0);                   /* (the first page: from the start, whole) */
            b->page = 0;
        } else {
            r = sdb_fill(b, 0, &edge);
            b->page--;
        }
    }
    return r;
}

static uint32_t sdb_path(sdb_t *b, uint32_t k, char *out, uint32_t cap)
{
    sdb_item it;
    FRESULT r = FR_OK;
    uint32_t n = 0;
    int st;
    if (k >= b->n)
        return 0;
    if (b->view == SDB_FOLDER && !b->slot[k].cut) {
        n = sdl_cat(out, 0, cap, b->root);
        n = sdl_cat(out, n, cap, "/");
        n = sdl_cat(out, n, cap, b->slot[k].name);
        return n < cap ? n : 0;
    }
    if (sdb_src_open(b) != FR_OK)                    /* the source again, to the slot's place */
        return 0;
    while ((st = sdb_src_next(b, &it, &r)) > 0 && it.ord < b->slot[k].ord)
        ;
    if (st > 0 && it.ord == b->slot[k].ord && !sdl_cmp(it.name, b->slot[k].name)) {
        if (b->view == SDB_FOLDER) {
            n = sdl_cat(out, 0, cap, b->root);
            n = sdl_cat(out, n, cap, "/");
            n = sdl_cat(out, n, cap, sdb_src.fi.fname);
        } else if (b->view == SDB_BYNAME) {
            n = sdl_cat(out, 0, cap, sdb_src.walk.path);
        } else {
            n = sdl_cat(out, 0, cap, SDL_ROOT "/");
            n = sdl_cat(out, n, cap, sdb_src.rec.path);
        }
    }
    sdb_src_close(b);
    return n && n < cap ? n : 0;
}

static FRESULT sdb_into(sdb_t *b, uint32_t k)
{
    char p[sizeof b->root];
    if (b->view != SDB_FOLDER || k >= b->n || !b->slot[k].dir)
        return FR_NO_PATH;
    if (!sdb_path(b, k, p, sizeof p))
        return FR_INVALID_NAME;                      /* (too deep for b->root) */
    return sdb_open(b, SDB_FOLDER, p);
}

static FRESULT sdb_up(sdb_t *b)
{
    uint32_t n = 0, cut = 0;
    for (; b->root[n]; n++)
        if (b->root[n] == '/')
            cut = n;
    if (b->view != SDB_FOLDER || !cut)
        return FR_NO_PATH;                           /* ("OPTIMIST": the top) */
    b->root[cut] = 0;
    return sdb_open(b, SDB_FOLDER, b->root);
}
