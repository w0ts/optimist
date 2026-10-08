/* SPDX-License-Identifier: GPL-3.0-only */
/* The FX record: what a project keeps of the generic FX slots (fx_slots.c), outside project_t (FUNB is full), built
 * as SLOOP 2.4's step extras (stepx_proj.c): a store per project buffer, named by the project's sum, each holding
 * the record's stored form. Included by project.c beside stepx_proj.c; in flash: fx_rec_log.c.
 *   proj_capture  the working FX state, encoded, into the store of the buffer it captured into
 *   proj_apply    decoded from the store of the buffer it applies (none, or another project's: the defaults, as a
 *                 project from before plays). The slot layout is the project's (a load, `all`), as REVERB > TYPE.
 *
 *   stored form (version 1)  u8 version, u8 flags, u8 slot[4] (type ids: fx_slots.c FXT_*), then TLVs: u8 type,
 *                            u8 length, the bytes. A reader skips a TLV it does not know (new types need no version
 *                            bump); a newer version is ignored as a whole (the defaults play).
 * Nothing to keep (the default layout, no TLV): no record, so most projects write nothing new. */
#define FXR_VER 1u
#define FXR_HEAD (2u + FX_NSLOT)
#define FXR_MAX 96u                                    /* the stored form at its longest (FXR_HEAD + the TLVs) */
_Static_assert(FXR_HEAD + 2u * (FXT_N - 1u) + FXT_TLV_SUM <= FXR_MAX, "the FX record: every TLV fits");
#define FXR_AUX 4u
typedef struct {
    uint32_t psum;                                     /* the project this belongs to (0: none) */
    uint16_t n;                                        /* the stored form's length (0: nothing to keep) */
    uint8_t b[FXR_MAX];
} fxr_store_t;
static fxr_store_t fxr_aux[FXR_AUX] __attribute__((section(".pool")));
static const project_t *fxr_aux_p[FXR_AUX];

/* the store of a project buffer; mk: take a free one for a buffer that has none (0: none) */
static fxr_store_t *fxr_for(const project_t *p, int mk)
{
    uint32_t i;
    for (i = 0; i < FXR_AUX; i++)
        if (fxr_aux_p[i] == p)
            return &fxr_aux[i];
    for (i = 0; mk && i < FXR_AUX; i++)
        if (!fxr_aux_p[i]) {
            fxr_aux_p[i] = p;
            return &fxr_aux[i];
        }
    return 0;
}
/* the buffer's store if it belongs to p as it is now, else 0 */
static const fxr_store_t *fxr_of(const project_t *p)
{
    const fxr_store_t *m = fxr_for(p, 0);
    return m && m->psum == p->sum ? m : 0;
}

/* the working FX state -> o (FXR_MAX), its length; 0: nothing to keep */
static uint32_t fxr_encode(uint8_t *o)
{
    uint32_t k, n = FXR_HEAD, any = 0;
    o[0] = FXR_VER;
    o[1] = 0;
    for (k = 0; k < FX_NSLOT; k++)
        any |= (uint32_t)((o[2u + k] = fxs_slot[k]) != FXS_DEF[k]);
    for (k = 1; k < FXT_N; k++) {                      /* each type's TLV (fx_slots.c fxs_tlv) */
        uint32_t m = fxs_tlv(k, o + n + 2u);
        if (m) {
            o[n] = (uint8_t)k, o[n + 1u] = (uint8_t)m;
            n += 2u + m;
        }
    }
    return any || n > FXR_HEAD ? n : 0u;
}
/* n bytes at a (0: none) -> the working FX state; all: the slot layout too (a load; a song section keeps it).
 * 0 refused (the defaults) */
static int fxr_decode(const uint8_t *a, uint32_t n, int all)
{
    uint32_t at = FXR_HEAD;
    int ok = n >= FXR_HEAD && n <= FXR_MAX && a[0] == FXR_VER;
    while (ok && at < n)                               /* (the TLVs: checked whole before any is taken) */
        if (at + 2u > n || at + 2u + a[at + 1u] > n)
            ok = 0;
        else
            at += 2u + a[at + 1u];
    fxs_untlv_none(all);
    for (at = FXR_HEAD; ok && at < n; at += 2u + a[at + 1u])
        fxs_untlv(a[at], a + at + 2u, a[at + 1u], all);   /* (an unknown type: skipped) */
    if (all && ok)
        fxs_set(a + 2);
    else if (all)
        fxs_auto();                                    /* (none: the default, a track FILTER in use in a slot) */
    return ok;
}
/* proj_capture, after the sum: the working state into the buffer's store */
static void fxr_capture_store(const project_t *p)
{
    fxr_store_t *m = fxr_for(p, 1);
    if (!m)
        return;
    m->n = (uint16_t)fxr_encode(m->b);
    m->psum = p->sum;
}
/* proj_apply (a load, a song section; the ISR or interrupts off) */
static void fxr_apply_store(const project_t *p, int all)
{
    const fxr_store_t *m = fxr_of(p);
    if (!m || !fxr_decode(m->b, m->n, all))
        (void)fxr_decode(0, 0, all);
}
/* the record of the project in buffer p (key first) -> o (4 + FXR_MAX), its length; 0: nothing to keep */
static uint32_t fxr_rec(uint32_t key, const project_t *p, uint8_t *o)
{
    const fxr_store_t *m = fxr_of(p);
    if (!m || !m->n)
        return 0;
    memcpy(o, &key, 4);
    memcpy(o + 4, m->b, m->n);
    return 4u + m->n;
}
/* a record (n bytes at r) into p's store when its key is key, else none */
static void fxr_from_rec(const project_t *p, const uint8_t *r, uint32_t n, uint32_t key)
{
    fxr_store_t *m = fxr_for(p, 1);
    uint32_t k = 0;
    if (!m)
        return;
    if (n >= 4u)
        memcpy(&k, r, 4);
    m->psum = p->sum;
    m->n = (uint16_t)(n > 4u && n <= 4u + FXR_MAX && k == key ? n - 4u : 0u);
    memcpy(m->b, r + 4, m->n);
}
/* the autosave's change test: a hash of the FX state captured with autosave_buf */
static uint32_t fxr_hash(const project_t *p)
{
    const fxr_store_t *m = fxr_of(p);
    return m && m->n ? proj_hash(m->b, m->n) : 0u;
}
