/* SPDX-License-Identifier: GPL-3.0-only */
/* The projects' drum records in flash (FELUCCA_FLASH; included by project.c after proj_tmp). A project (format
 * 10, "FUNA") no longer holds its drum lanes: its dl_hash names its drum record (drums.c drum_sends.c dlrec_t: the
 * lanes and their sends, 236 B), kept in one storage object of its own, OBJ_DLANES (storage.c: A/B at
 * 0xE5000 / 0xE6000). The object holds two entries per project slot (0..3: the projects / live sections, 4:
 * the working project, autosave), each a record and its key:
 *
 *   dls_t   magic "DLS1", entry size, entries (10), then per slot s: e[2s], e[2s + 1]
 *   entry   key (dlrec_hash; 0 = empty), the record (236 B)                                   2408 B in all
 *
 * A record of all zeros (every lane the kit as it is, TRK / 0) has key 0 and is never stored. Saving a project
 * (proj_put): its record first, into the slot's entry the project now in flash does not name (that one stays),
 * then the project. A save cut short anywhere leaves the old project with its record, or the new one with its
 * own: the key names it, and a record that does not hash to its key is never taken. A record already there
 * (the lanes unchanged since) is not written again: most saves write the project only. Loading (proj_get):
 * the project, then the record its key names (the slot's entries first, then any); none (a damaged object):
 * the project loads with the kit as it is. */
#define DLS_MAGIC 0x31534C44u                  /* "DLS1" */
#define DLS_SLOTS 5u                           /* the four project slots, the working project */
#define DLS_N (2u * DLS_SLOTS)
typedef struct {
    uint32_t key;                              /* dlrec_hash(&r), 0 = empty */
    dlrec_t r;
} dls_ent_t;
typedef struct {
    uint32_t magic;
    uint16_t esize, n;
    dls_ent_t e[DLS_N];
} dls_t;
_Static_assert(sizeof(dls_ent_t) == 240u && sizeof(dls_t) == 2408u && sizeof(dls_t) <= ST_PAYLOAD_MAX,
               "drum records: 10 x 240 B, one flash object");
_Static_assert(sizeof(dls_t) <= sizeof proj_tmp, "the new object is built in proj_tmp");

/* where the records live (storage.c ST_DLANES_SECTOR, 0xE5000..0xE6FFF): past the whole USR1..USR3 range (0xA0000..
 * 0xDBFFF, eng_sample.c), so clear of anything carved from USR3's end (today's kit bank 0xDA000; the shared
 * 16 KiB kit + FM6 bank planned at 0xD8000..0xDBFFF), past the user preset banks (..0xDFFFF) and the update
 * loader's staging (0xE0000..0xE4FFF, its record at 0xE4F00), before 0xE7000 (hal/fm1_flash_map.h: the retired
 * UP_FM6 sector, then the stock firmware's SDK VM at 0xE8000 and BTIF at 0xE9000, never written) */
_Static_assert(ST_DLANES_SECTOR >= SMP_USER_BASE + SMP_USER_SLOTS * SMP_USER_SIZE && ST_DLANES_SECTOR >= 0xE5000u &&
               ST_DLANES_SECTOR + 2u * ST_SECTOR <= FL_OLD_UPF_LO && !FL_NEVER(ST_DLANES_SECTOR, 2u * ST_SECTOR),
               "drum records: outside USR1..3, the banks, the update area, the SDK's sectors");

/* proj_tmp lent to the editor's restore (ed_backup.c): it holds an object being received, so proj_put and the
 * kit bank (drum_kits.c) refuse meanwhile; a restore left for 10 s gives it back */
static uint8_t proj_tmp_lent;
static uint32_t proj_tmp_t0;
static int proj_tmp_busy(void)
{
    if (proj_tmp_lent && fm1_ms - proj_tmp_t0 > 10000u)
        proj_tmp_lent = 0;
    return proj_tmp_lent;
}

static uint32_t dls_slot(uint32_t obj) { return obj == OBJ_AUTOSAVE ? 4u : (obj - OBJ_PROJECT0) & 3u; }

/* the object as stored (left in st_buf), 0 = none / not this shape */
static const dls_t *dls_cur(void)
{
    const dls_t *b = (const dls_t *)(void *)st_buf;
    st_hdr_t h;
    if (st_current(OBJ_DLANES, &h) < 0 || h.len != sizeof *b || b->magic != DLS_MAGIC || b->esize != sizeof(dls_ent_t) ||
        b->n != DLS_N)
        return 0;
    return b;
}
static int dls_has(const dls_t *b, uint32_t s, uint32_t key)   /* slot s keeps the record key */
{
    return b && (b->e[2u * s].key == key || b->e[2u * s + 1u].key == key);
}

/* the record named key -> *d (slot s's entries first, then any); 1 found (key 0: all zero) */
static int dls_find(uint32_t s, uint32_t key, dlrec_t *d)
{
    const dls_t *b;
    uint32_t k;
    if (!key) {
        memset(d, 0, sizeof *d);
        return 1;
    }
    if (!(b = dls_cur()))
        return 0;
    for (k = 0; k < DLS_N; k++) {
        const dls_ent_t *e = &b->e[(2u * s + k) % DLS_N];
        if (e->key == key && dlrec_hash(&e->r) == key) {
            *d = e->r;
            return 1;
        }
    }
    return 0;
}

/* project p and its drum record d into storage object obj (a project slot, OBJ_AUTOSAVE): the record first
 * (when not there yet), then the project; 0 ok, else the storage error (the project is then not written) */
static int proj_put(uint32_t obj, const project_t *p, const dlrec_t *d)
{
    uint32_t s = dls_slot(obj), key = p->dl_hash, keep = 0, v;
    dls_t *nb = (dls_t *)(void *)&proj_tmp;
    const dls_t *b;
    int n, rc;
    if (proj_tmp_busy())
        return -10;                                     /* (a restore: tried again later, as a failed write) */
    if (key && !dls_has(dls_cur(), s, key)) {
        n = st_load(obj, &proj_tmp, sizeof proj_tmp);    /* the project in flash now: its record stays */
        if (n == (int)sizeof *p && proj_tmp.cur.magic == PROJ_MAGIC)
            keep = proj_tmp.cur.dl_hash;
        if ((b = dls_cur()) != 0) {
            memcpy(nb, b, sizeof *nb);
        } else {
            memset(nb, 0, sizeof *nb);
            nb->magic = DLS_MAGIC;
            nb->esize = sizeof(dls_ent_t);
            nb->n = DLS_N;
        }
        v = 2u * s + (keep && nb->e[2u * s].key == keep ? 1u : 0u);
        nb->e[v].key = key;
        nb->e[v].r = *d;
        if ((rc = st_save(OBJ_DLANES, nb, sizeof *nb)) != 0
#if FELUCCA_SL24_SAFE
            && rc != ST_E_KEPT                          /* (both copies another firmware's, SLOOP 2.4's FM6 bank: the */
#endif                                                  /* project saved, its lanes the kit as it is when loaded) */
            )
            return rc;
    }
    return st_save(obj, p, sizeof *p);
}

/* storage object obj (any format) -> q and its drum record d; 0 = not a project. A record that cannot be
 * found (a damaged object): the kit as it is (q's key 0) */
static int proj_get(uint32_t obj, project_t *q, dlrec_t *d)
{
    int n;
    if (proj_tmp_busy())
        return 0;                                       /* (a restore holds proj_tmp: "empty" for now) */
    n = st_load(obj, &proj_tmp, sizeof proj_tmp);
    if (!proj_import(q, &proj_tmp, n))
        return 0;
    proj_import_dl(d, &proj_tmp, n);                    /* FUN8 / 9: inline; FUNA: below */
#if FELUCCA_ANALOG2
    if (q->dl_hash != dlrec_hash(d) && !dls_find(dls_slot(obj), q->dl_hash, d)) {
        memset(d, 0, sizeof *d);
        q->dl_hash = 0;
        q->sum = proj_sum(q);
    }
#endif
    return 1;
}

#if !SEC_LOGGED
/* power-on, slot i still valid in RAM (.noinit, a warm reset): its drum record (RAM, not kept) from flash by
 * its key, else the slot as flash has it, else the kit as it is; 1 = the slot is as in flash (not dirty) */
static int proj_slot_boot(uint32_t i)
{
    project_t *q = &proj_slot[i];
    int n;
#if FELUCCA_ANALOG2
    if (q->dl_hash != dlrec_hash(&proj_dl[i]) && !dls_find(i, q->dl_hash, &proj_dl[i])) {
        if (proj_get(OBJ_PROJECT0 + i, q, &proj_dl[i]))   /* (the record is nowhere: the flash copy, if any; */
            return 1;                                       /* none: q is left as it was) */
        memset(&proj_dl[i], 0, sizeof proj_dl[i]);
        q->dl_hash = 0;
        q->sum = proj_sum(q);
        return 0;
    }
#endif
    n = st_load(OBJ_PROJECT0 + i, &proj_tmp, sizeof proj_tmp);
    return n == (int)sizeof *q && !memcmp(&proj_tmp.cur, q, sizeof *q);
}
#endif
