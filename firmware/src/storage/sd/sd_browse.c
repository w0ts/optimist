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
 *   sdb_open(b, view, root)   the view of root ("Optimist/Samples"; ignored by SDB_RECENT): page 0. FR_OK, or
 *                             FatFs' error (b->n 0)
 *   sdb_turn(b, step)         the page step pages on (+1, -1, more: one pass a page), clamped to the first and last
 *   b->page, b->pages, b->total, b->n, b->slot[k]   what the page shows: slot k on key k (name, dir, cut)
 *   sdb_go(b, view, root), sdb_go_turn(b, step), sdb_pump(units)   the same in slices of the main loop's (below)
 *   sdb_path(b, k, out, cap)  slot k's whole path from the card's root: its length, 0 when it is gone or does not fit
 *   sdb_into(b, k), sdb_up(b)   SDB_FOLDER: into the folder on slot k / up to the folder above (never above Optimist),
 *                             page 0: FR_OK, FR_NO_PATH (slot k no folder, or already at the top) */
#define SDB_KEYS 16u
#define SDB_NAME 48u
enum { SDB_FOLDER = 0, SDB_BYNAME, SDB_RECENT, SDB_BANK };
#define SDB_DIRLIKE(v) ((v) == SDB_FOLDER || (v) == SDB_BANK)   /* (read as one folder: a bank is one) */

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
static struct {                                      /* ... and that pass's page so far (sdb_ps_begin .. sdb_ps_step) */
    sdb_item after, before;                          /* the edges (has_*: asked) */
    uint32_t n, total;
    FRESULT r;
    uint8_t has_after, has_before, none;
} sdb_ps IN_POOL;

static int sdb_ci(uint8_t c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

static uint32_t sdb_bank_no(const char *s);
static int sdb_cmp(uint32_t view, const sdb_item *a, const sdb_item *b)
{
    const uint8_t *p = (const uint8_t *)a->name, *q = (const uint8_t *)b->name;
    if (view == SDB_BYNAME) {                        /* (by name: a bank's "NN - " is the slot, not the name) */
        p += sdb_bank_no(a->name) ? 5 : 0;
        q += sdb_bank_no(b->name) ? 5 : 0;
    }
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

static char sdb_flt[32];                            /* BYNAME: "/<folder>/" a file's path must hold ("" none; sd_mode.c sdm_name_filter) */

static FRESULT sdb_src_open(const sdb_t *b)
{
    sdx_head h;
    FRESULT r;
    sdb_src.ord = 0;
    if (SDB_DIRLIKE(b->view))
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
    if (SDB_DIRLIKE(b->view))
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
        if (SDB_DIRLIKE(b->view)) {
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
            for (;;) {                                  /* (a filter: only the files under a folder of that name, "/Digital/") */
                if ((st = sdw_next(&sdb_src.walk)) <= 0) {
                    *r = sdb_src.walk.err;
                    return st;
                }
                if (!sdb_flt[0] || sdl_has(sdb_src.walk.path, sdb_flt))
                    break;
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

/* a bank's slot number of a name: "NN - name" with NN 01 .. 16 -> NN, else 0 (the user, 2026-11-04: presets and
 * projects in banks of 16, the number in the file's name the slot the FM-1 shows it on) */
static uint32_t sdb_bank_no(const char *s)
{
    uint32_t nn;
    if (s[0] < '0' || s[0] > '9' || s[1] < '0' || s[1] > '9' || s[2] != ' ' || s[3] != '-' || s[4] != ' ' || !s[5])
        return 0;
    nn = (uint32_t)(s[0] - '0') * 10u + (uint32_t)(s[1] - '0');
    return nn >= 1u && nn <= SDB_KEYS ? nn : 0u;
}

/* one pass over the source, a few entries at a time (sdb_fill: all of them; sdb_pump: a slice of the main loop's):
 * SDB_BANK: one folder of 16 numbered slots, slot NN - 1 the entry "NN - name" (the first of a number, in the folder's
 * order); a free number an empty slot (name ""); anything without a number not shown (b->total counts what is). A bank
 * not made yet: 16 free slots. The other views: the page after the edge, or before it (the 16 last), or the first */
static void sdb_ps_begin(sdb_t *b, const sdb_item *after, const sdb_item *before)
{
    FRESULT r;
    sdb_ps.has_after = after != 0;
    sdb_ps.has_before = before != 0;
    if (after)
        sdb_ps.after = *after;
    if (before)
        sdb_ps.before = *before;
    sdb_ps.n = sdb_ps.total = 0;
    sdb_ps.none = 0;
    if (b->view == SDB_BANK)
        memset(b->slot, 0, sizeof b->slot);
    r = sdb_src_open(b);
    if (b->view == SDB_BANK && (r == FR_NO_PATH || r == FR_NO_FILE)) {
        r = FR_OK;
        sdb_ps.none = 1;                             /* (no folder: nothing to read, nothing to close) */
    } else if (r == FR_NO_FILE && b->view == SDB_RECENT) {
        r = FR_OK;
    }
    sdb_ps.r = r;
}

static void sdb_ps_keep(sdb_t *b, const sdb_item *it)  /* entry it among the 16 the page keeps */
{
    const sdb_item *after = sdb_ps.has_after ? &sdb_ps.after : 0, *before = sdb_ps.has_before ? &sdb_ps.before : 0;
    uint32_t n = sdb_ps.n, i;
    if ((after && sdb_cmp(b->view, it, after) <= 0) || (before && sdb_cmp(b->view, it, before) >= 0))
        return;
    if (n == SDB_KEYS && (before ? sdb_cmp(b->view, it, &b->slot[0]) <= 0
                                 : sdb_cmp(b->view, it, &b->slot[SDB_KEYS - 1u]) >= 0))
        return;                                      /* (outside the 16 kept) */
    if (n == SDB_KEYS && before) {                   /* the smallest drops out */
        memmove(&b->slot[0], &b->slot[1], (SDB_KEYS - 1u) * sizeof *it);
        n--;
    } else if (n == SDB_KEYS) {
        n--;                                         /* the largest drops out */
    }
    for (i = n; i && sdb_cmp(b->view, it, &b->slot[i - 1u]) < 0; i--)
        b->slot[i] = b->slot[i - 1u];
    b->slot[i] = *it;
    sdb_ps.n = n + 1u;
}

/* up to units of the source's entries: 1 more to read, 0 the pass done (b filled, sdb_ps.r its result) */
static int sdb_ps_step(sdb_t *b, uint32_t units)
{
    sdb_item it;
    uint32_t nn;
    int st = 0;
    if (sdb_ps.none) {
        b->n = SDB_KEYS, b->total = 0, b->pages = 1;
        return 0;
    }
    while (sdb_ps.r == FR_OK && units-- && (st = sdb_src_next(b, &it, &sdb_ps.r)) > 0) {
        if (b->view != SDB_BANK) {
            sdb_ps.total++;
            sdb_ps_keep(b, &it);
        } else if ((nn = sdb_bank_no(it.name)) != 0 && !b->slot[nn - 1u].name[0]) {
            b->slot[nn - 1u] = it;
            sdb_ps.total++;
        }
    }
    if (sdb_ps.r == FR_OK && st > 0)
        return 1;
    sdb_src_close(b);
    b->total = sdb_ps.total;
    if (b->view == SDB_BANK) {
        b->n = SDB_KEYS;
        b->pages = 1;
    } else {
        b->n = (uint8_t)sdb_ps.n;
        b->pages = b->total ? (b->total + SDB_KEYS - 1u) / SDB_KEYS : 1u;
    }
    return 0;
}

static FRESULT sdb_fill(sdb_t *b, const sdb_item *after, const sdb_item *before)
{
    sdb_ps_begin(b, after, before);
    while (sdb_ps_step(b, 0xFFFFFFFFu))
        ;
    return sdb_ps.r;
}

/* ---- the read in slices (the UI's: a page asked with sdb_go or sdb_go_turn comes in sdb_pump's slices, the main loop
 * going on; what the screen shows stays in b until the page is whole). One read at a time: a new sdb_go drops the one
 * running, turns add up; whatever else needs the source or the page first ends the read (sdb_finish) */
static struct {
    sdb_t n;                                         /* the page being read; root and view: where the list is going */
    sdb_t *to;
    int32_t steps;                                   /* the pages to turn after this one */
    int32_t dir;                                     /* the page being read, from n.page: +1 the next, < 0 back */
    uint8_t on;
} sdb_rd IN_POOL;

static int sdb_reading(void) { return sdb_rd.on; }
static const sdb_t *sdb_going(const sdb_t *b) { return sdb_rd.on ? &sdb_rd.n : b; }
static uint32_t sdb_going_page(void)                 /* (reading) the page it ends on */
{
    return (uint32_t)((int32_t)sdb_rd.n.page + sdb_rd.dir + sdb_rd.steps);
}
static int sdb_rd_turn(void)                         /* the next page of the turns asked: 1 its read begun */
{
    sdb_t *b = &sdb_rd.n;
    sdb_item edge;
    sdb_rd.dir = 0;
    if (sdb_rd.steps > 0 && b->page + 1u < b->pages && b->n == SDB_KEYS) {
        edge = b->slot[b->n - 1u];
        sdb_rd.dir = 1;
        sdb_rd.steps--;
        sdb_ps_begin(b, &edge, 0);
    } else if (sdb_rd.steps < 0 && b->page) {
        edge = b->slot[0];
        sdb_rd.steps++;
        if (b->page == 1u || !b->n) {                /* (the first page: from the start, whole) */
            sdb_rd.dir = -(int32_t)b->page;
            sdb_ps_begin(b, 0, 0);
        } else {
            sdb_rd.dir = -1;
            sdb_ps_begin(b, 0, &edge);
        }
    } else {
        sdb_rd.steps = 0;
    }
    return sdb_rd.dir != 0;
}
static void sdb_abort(void)                          /* the read dropped (what it was showing stays) */
{
    if (sdb_rd.on && !sdb_ps.none)
        sdb_src_close(&sdb_rd.n);
    sdb_rd.on = 0;
}
static FRESULT sdb_go(sdb_t *b, uint32_t view, const char *root)
{
    sdb_abort();
    sdb_rd.n.view = (uint8_t)view;
    sdb_rd.n.page = 0;
    sdb_rd.n.n = 0;
    sdb_rd.n.pages = 0;                              /* (not known yet) */
    if (sdl_cat(sdb_rd.n.root, 0, sizeof sdb_rd.n.root, root ? root : "") >= sizeof sdb_rd.n.root)
        return FR_INVALID_NAME;
    sdb_rd.to = b;
    sdb_rd.steps = 0;
    sdb_rd.dir = 0;
    sdb_rd.on = 1;
    sdb_ps_begin(&sdb_rd.n, 0, 0);
    return FR_OK;
}
static void sdb_go_turn(sdb_t *b, int32_t step)
{
    if (!sdb_rd.on) {
        sdb_rd.n = *b;
        sdb_rd.to = b;
        sdb_rd.steps = step;
        sdb_rd.on = (uint8_t)sdb_rd_turn();
    } else {
        int32_t at = (int32_t)sdb_going_page() + step;
        if (at >= 0 && (!sdb_rd.n.pages || at < (int32_t)sdb_rd.n.pages))
            sdb_rd.steps += step;
    }
}
/* a slice of the read: up to units entries; its result when a page ended */
static FRESULT sdb_pump(uint32_t units)
{
    sdb_t *b = &sdb_rd.n;
    FRESULT r;
    if (!sdb_rd.on || sdb_ps_step(b, units))
        return FR_OK;
    r = sdb_ps.r;
    if (r == FR_OK && sdb_rd.dir > 0 && !b->n) {     /* (the card changed under the page: back to the first) */
        b->page = 0;
        sdb_rd.dir = 0;
        sdb_rd.steps = 0;
        sdb_ps_begin(b, 0, 0);
        return FR_OK;
    }
    b->page = (uint32_t)((int32_t)b->page + sdb_rd.dir);
    sdb_rd.dir = 0;
    if (r != FR_OK) {
        b->n = 0;
        b->pages = 1;
        sdb_rd.steps = 0;
    }
    *sdb_rd.to = *b;
    if (!sdb_rd_turn())
        sdb_rd.on = 0;
    return r;
}
static void sdb_finish(void)                         /* the read ended at once */
{
    while (sdb_rd.on)
        (void)sdb_pump(0xFFFFFFFFu);
}

static FRESULT sdb_open(sdb_t *b, uint32_t view, const char *root)
{
    sdb_finish();
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
    sdb_finish();
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
    sdb_finish();
    if (k >= b->n)
        return 0;
    if (b->view == SDB_BANK && !b->slot[k].name[0])
        return 0;                                    /* (a bank's free slot) */
    if (SDB_DIRLIKE(b->view) && !b->slot[k].cut) {
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
        if (SDB_DIRLIKE(b->view)) {
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
    sdb_finish();
    if (b->view != SDB_FOLDER || k >= b->n || !b->slot[k].dir)
        return FR_NO_PATH;
    if (!sdb_path(b, k, p, sizeof p))
        return FR_INVALID_NAME;                      /* (too deep for b->root) */
    return sdb_open(b, SDB_FOLDER, p);
}

static FRESULT sdb_up(sdb_t *b)
{
    uint32_t n = 0, cut = 0;
    sdb_finish();
    for (; b->root[n]; n++)
        if (b->root[n] == '/')
            cut = n;
    if (!SDB_DIRLIKE(b->view) || !cut)
        return FR_NO_PATH;                           /* ("Optimist": the top) */
    b->root[cut] = 0;
    return sdb_open(b, SDB_FOLDER, b->root);
}
