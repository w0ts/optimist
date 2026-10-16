/* SPDX-License-Identifier: GPL-3.0-only */
/* User drum kits (FELUCCA_DRUM_KITS): a bank of 16 in the data flash (storage.c OBJ_UKIT, A/B at 0xDA000 /
 * 0xDB000), after the factory kits in the kit list (ui_drums.c). A kit is the lanes of drum_edit.c (DRUM_LANES):
 * each one's instrument, source (a kit's sound for that lane, a user sample) and its 8 offsets, plus the kit KIT
 * means on a lane: 196 bytes. No names: a kit is KIT n, its slot's number (user decision: save memory). Loading
 * one copies it into the project's lanes (a project keeps its kit even if the bank changes); SAVE stores
 * the project's lanes, every KIT lane written as the kit it plays then. No RAM copy: a read leaves the
 * bank in storage.c's st_buf; a write builds the new bank in project.c's proj_tmp (only the main loop uses
 * either). User samples stay in their USR slots (a kit names slot, hit, start, length).
 * Bank versions: "DKB4" (2026-10-16, the 10 lanes and their instruments) the 16 kits, then each kit's lane send
 * words (DRUM_LSTORE a kit, as dsend[]: drum_sends.c; the last 6 zero). The banks of the 16 fixed lanes ("DKB3",
 * "DKB1") are not read (the user: no compatibility needed): the bank starts empty. */
#define UK_N 16u
#define UK_USED 0xA5u
#define UK_MAGIC 0x34424B44u                                 /* "DKB4" */
typedef struct {
    uint8_t used, base;                                      /* UK_USED; the factory kit KIT means */
    uint8_t src[DRUM_LANES];                                 /* as dlanes_t */
    uint8_t ref[DRUM_LANES][3];
    int8_t ofs[DRUM_LANES][DE_N];
    uint8_t ins[DRUM_LANES];
    uint8_t rsv[194u - DRUM_LANES * (DE_N + 5u)];            /* (the record's size kept) */
} ukit_t;
typedef struct {
    uint32_t magic;
    uint16_t rsize, n;
    ukit_t k[UK_N];
    uint16_t snd[UK_N][DRUM_LSTORE];                         /* kit u's lane sends (dsend words) */
} ukit_bank_t;
_Static_assert(sizeof(ukit_t) == 196u && sizeof(ukit_bank_t) == 8u + UK_N * 196u + UK_N * DRUM_LSTORE * 2u &&
               sizeof(ukit_bank_t) <= ST_PAYLOAD_MAX, "user kit: 196 B (+ 32 B of sends), a bank in a sector");
#ifndef UK_TMP
_Static_assert(sizeof(ukit_bank_t) <= sizeof proj_tmp, "the new bank is built in proj_tmp");
#define UK_TMP ((ukit_bank_t *)(void *)&proj_tmp)
#endif
static uint16_t uk_mask;                                     /* bit per used slot */
static uint8_t uk_read;                                      /* uk_mask is the bank's */

static int uk_valid(const ukit_t *k) { return k->used == UK_USED; }

/* the bank as stored (left in st_buf), 0 = none / not this shape */
static const ukit_bank_t *uk_bank(void)
{
    ukit_bank_t *b = (ukit_bank_t *)(void *)st_buf;
    st_hdr_t h;
#if FELUCCA_FLASH && !defined(UK_HOST)
    if (!flash_ok)
        return 0;
#endif
    if (st_current(OBJ_UKIT, &h) < 0 || b->n != UK_N)
        return 0;
    return h.len == sizeof *b && b->magic == UK_MAGIC && b->rsize == sizeof(ukit_t) ? b : 0;
}
/* slot u's lane sends -> s (DRUM_LANES words); 0 = empty */
static int ukit_sends(uint32_t u, uint16_t *s)
{
    const ukit_bank_t *b = u < UK_N ? uk_bank() : 0;
    uint32_t l;
    if (!b || !uk_valid(&b->k[u]))
        return 0;
    for (l = 0; l < DRUM_LANES; l++)
        s[l] = dsend_canon(b->snd[u][l]);
    return 1;
}
static void uk_scan(void)
{
    const ukit_bank_t *b = uk_bank();
    uint32_t u;
    uk_mask = 0;
    for (u = 0; u < UK_N; u++)
        if (b && uk_valid(&b->k[u]))
            uk_mask |= (uint16_t)(1u << u);
    uk_read = 1;
}
static void uk_need(void)
{
    if (!uk_read)
        uk_scan();
}
static int ukit_used(uint32_t u) { uk_need(); return u < UK_N && ((uk_mask >> u) & 1u); }
static uint32_t ukit_count(void)
{
    uint32_t u, n = 0;
    uk_need();
    for (u = 0; u < UK_N; u++)
        n += (uk_mask >> u) & 1u;
    return n;
}
static uint32_t ukit_nth(uint32_t n)                         /* the slot of the n-th used one */
{
    uint32_t u;
    uk_need();
    for (u = 0; u < UK_N; u++)
        if (((uk_mask >> u) & 1u) && !n--)
            return u;
    return 0;
}
static uint32_t ukit_rank(uint32_t u)                        /* its place among the used ones */
{
    uint32_t i, r = 0;
    uk_need();
    for (i = 0; i < u && i < UK_N; i++)
        r += (uk_mask >> i) & 1u;
    return r;
}
static const char *const UK_NAME[UK_N] = {"KIT 1", "KIT 2", "KIT 3", "KIT 4", "KIT 5", "KIT 6", "KIT 7", "KIT 8",
                                          "KIT 9", "KIT 10", "KIT 11", "KIT 12", "KIT 13", "KIT 14", "KIT 15", "KIT 16"};
static void ukit_name(uint32_t u, char *b)                   /* b holds 9: "KIT n" */
{
    str_cpy(b, u < UK_N ? UK_NAME[u] : "", 9);
}

/* slot u -> *k; 0 = empty */
static int ukit_get(uint32_t u, ukit_t *k)
{
    const ukit_bank_t *b = u < UK_N ? uk_bank() : 0;
    if (!b || !uk_valid(&b->k[u]))
        return 0;
    *k = b->k[u];
    return 1;
}

/* slot u := *k with its lane sends snd (0: each lane as it is; k 0: erased); 0 ok, else the storage error (-1: no flash) */
static int ukit_put_snd(uint32_t u, const ukit_t *k, const uint16_t *snd)
{
    ukit_bank_t *nb = UK_TMP;
    const ukit_bank_t *b = uk_bank();
    uint32_t l;
    int rc;
    if (u >= UK_N)
        return -2;
#ifndef UK_HOST
    if (proj_tmp_busy())                                     /* (proj_tmp: a restore holds it) */
        return -10;
#endif
    if (b)
        memcpy(nb, b, sizeof *nb);
    else
        memset(nb, 0, sizeof *nb);
    nb->magic = UK_MAGIC;
    nb->rsize = sizeof(ukit_t);
    nb->n = UK_N;
    if (k)
        nb->k[u] = *k;
    else
        memset(&nb->k[u], 0, sizeof nb->k[u]);
    for (l = 0; l < DRUM_LANES; l++)
        nb->snd[u][l] = k && snd ? dsend_canon(snd[l]) : 0u;
#if FELUCCA_FLASH && !defined(UK_HOST)
    if (!flash_ok)
        return -1;
#endif
    rc = st_save(OBJ_UKIT, nb, sizeof *nb);
    uk_scan();
    return rc;
}
static int ukit_put(uint32_t u, const ukit_t *k) { return ukit_put_snd(u, k, 0); }   /* (sends: each lane as it is) */

/* slot u into the project's lanes and kit: 1 done */
static int ukit_load(uint32_t u)
{
    ukit_t k;
    uint16_t s[DRUM_LANES];
    if (!ukit_get(u, &k) || !ukit_sends(u, s))
        return 0;
    fm1_irq_off();
    memcpy(dsend, s, sizeof dsend);                          /* (its lanes' sends) */
    memcpy(dl.ofs, k.ofs, sizeof dl.ofs);
    memcpy(dl.src, k.src, sizeof dl.src);
    memcpy(dl.ref, k.ref, sizeof dl.ref);
    memcpy(dl.ins, k.ins, sizeof dl.ins);
    memset(dl.name, 0, sizeof dl.name);                     /* (reserved: names are numbers now) */
    dl.ukit = (uint8_t)(u + 1u);
    dl_fix(&dl);
    TDRUM->p[P_E0] = (int16_t)(k.base < DRUM_KITS ? k.base : DRUM_DEFAULT_KIT);
    dl_e0 = TDRUM->p[P_E0];
    fm1_irq_on();
    MISS_BUMP();                                             /* (its kits, samples, sends: miss.c) */
    return 1;
}

/* the project's lanes as slot u; 0 ok */
static int ukit_store(uint32_t u)
{
    ukit_t k;
    uint32_t l;
    memset(&k, 0, sizeof k);
    k.used = UK_USED;
    k.base = (uint8_t)drum_kit();
    for (l = 0; l < DRUM_LANES; l++)                         /* KIT: the kit it plays now */
        k.src[l] = dl.src[l] == DL_KIT ? (uint8_t)(DL_KIT0 + dl_kit_of(l, k.base)) : dl.src[l];
    memcpy(k.ref, dl.ref, sizeof k.ref);
    memcpy(k.ofs, dl.ofs, sizeof k.ofs);
    memcpy(k.ins, dl.ins, sizeof k.ins);
    {
        int rc = ukit_put_snd(u, &k, dsend);
        if (!rc) {
            dl.ukit = (uint8_t)(u + 1u);                     /* the project plays that kit now */
            memset(dl.name, 0, sizeof dl.name);
            dl_e0 = TDRUM->p[P_E0];
        }
        return rc;
    }
}

/* the KIT page's SAVE (0) / ERASE (1) */
static void ukit_ui(uint32_t op, uint32_t u)
{
    char l[12];
    int rc;
    if (song.playing || transport_req) {                     /* a flash erase stops the audio ~50 ms */
        ui_message("STOP BEFORE SAVE");
        return;
    }
    if (op && !ukit_used(u)) {
        ui_message("EMPTY SLOT");
        return;
    }
    ukit_name(u, l);
    rc = op ? ukit_put(u, 0) : ukit_store(u);
    if (rc)
        ui_message(rc == -1 ? "NO FLASH" : op ? "ERASE ERROR" : "SAVE ERROR");
    else
        ui_say(op ? "ERASED " : "SAVED ", l);
    ui.force = 1;
}
