/* SPDX-License-Identifier: GPL-3.0-only */
/* The drum lanes' LFO record (drums/lane_play.c: one LFO a lane; the user, 2026-10-12, D6): what a project keeps of
 * them, outside project_t (FUNB is full) and outside the FX record (fx_rec.c: 13 bytes left of its 96, and an older
 * reader refuses a longer FX record whole), built as the FX record is: a store per project buffer, named by the
 * project's sum, each holding the record's stored form (lane_play.c dlfo_encode). Included by project.c beside
 * fx_rec.c; in flash: sections/dlfo_rec_log.c (its own log ids, which an older build skips: it reads the project with
 * every LFO off, and loses nothing else).
 *   proj_capture  the working LFOs, encoded, into the store of the buffer it captured into
 *   proj_apply    decoded from the store of the buffer it applies (none, or another project's: every LFO off, as a
 *                 project from before plays)
 * Nothing to keep (every lane at the defaults): no record, so most projects write nothing new. */
#define DLR_MAX DLFO_ENC_MAX
#define DLR_AUX 4u
typedef struct {
    uint32_t psum;                                     /* the project this belongs to (0: none) */
    uint16_t n;                                        /* the stored form's length (0: nothing to keep) */
    uint8_t b[DLR_MAX];
} dlr_store_t;
static dlr_store_t dlr_aux[DLR_AUX] __attribute__((section(".pool")));
static const project_t *dlr_aux_p[DLR_AUX];

/* the store of a project buffer; mk: take a free one for a buffer that has none (0: none) */
static dlr_store_t *dlr_for(const project_t *p, int mk)
{
    uint32_t i;
    for (i = 0; i < DLR_AUX; i++)
        if (dlr_aux_p[i] == p)
            return &dlr_aux[i];
    for (i = 0; mk && i < DLR_AUX; i++)
        if (!dlr_aux_p[i]) {
            dlr_aux_p[i] = p;
            return &dlr_aux[i];
        }
    return 0;
}
/* the buffer's store if it belongs to p as it is now, else 0 */
static const dlr_store_t *dlr_of(const project_t *p)
{
    const dlr_store_t *m = dlr_for(p, 0);
    return m && m->psum == p->sum ? m : 0;
}
/* proj_capture, after the sum: the working LFOs into the buffer's store */
static void dlr_capture_store(const project_t *p)
{
    dlr_store_t *m = dlr_for(p, 1);
    if (!m)
        return;
    m->n = (uint16_t)dlfo_encode(m->b);
    m->psum = p->sum;
}
/* proj_apply (a load, a song section; the ISR or interrupts off): none, refused: every LFO off */
static void dlr_apply_store(const project_t *p)
{
    const dlr_store_t *m = dlr_of(p);
    if (!m || !dlfo_decode(m->b, m->n))
        (void)dlfo_decode(0, 0);
}
/* the record of the project in buffer p (key first) -> o (4 + DLR_MAX), its length; 0: nothing to keep */
static uint32_t dlr_rec(uint32_t key, const project_t *p, uint8_t *o)
{
    const dlr_store_t *m = dlr_of(p);
    if (!m || !m->n)
        return 0;
    memcpy(o, &key, 4);
    memcpy(o + 4, m->b, m->n);
    return 4u + m->n;
}
/* a record (n bytes at r) into p's store when its key is key, else none */
static void dlr_from_rec(const project_t *p, const uint8_t *r, uint32_t n, uint32_t key)
{
    dlr_store_t *m = dlr_for(p, 1);
    uint32_t k = 0;
    if (!m)
        return;
    if (n >= 4u)
        memcpy(&k, r, 4);
    m->psum = p->sum;
    m->n = (uint16_t)(n > 4u && n <= 4u + DLR_MAX && k == key ? n - 4u : 0u);
    memcpy(m->b, r + 4, m->n);
}
/* the autosave's change test: a hash of the LFOs captured with autosave_buf (turned, so it never cancels the FX
 * record's equal hash in the XOR the test makes of them) */
static uint32_t dlr_hash(const project_t *p)
{
    const dlr_store_t *m = dlr_of(p);
    uint32_t h = m && m->n ? proj_hash(m->b, m->n) : 0u;
    return h << 7 | h >> 25;
}
#define DLR_HASH(p) dlr_hash(p)
