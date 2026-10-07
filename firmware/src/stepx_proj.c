/* SPDX-License-Identifier: GPL-3.0-only */
/* SLOOP 2.4's step extras (stepx.h: nudge, locks, fills; isod89/sloop-fm1 v2.4, 8d3823f) with the projects,
 * FELUCCA_SL24_XSTEP (backports24.h). Included by project.c beside motion_proj.c, and the same pattern: project_t is
 * full (FUNB fills a flash sector's payload), so the extras live outside it, a store per project buffer, named by
 * the project's sum (psum): a project changed without them (a build without the switch, an older save) plays none.
 *
 *   the working extras   STEPX(k), track k's: what the sequencer plays (phase 2 may define STEPX to its own place,
 *                        e.g. inside track_t; by default sx_work here)
 *   a buffer's store     proj_capture copies the working extras into the store of the buffer it captured into;
 *                        proj_apply takes them from the store of the buffer it applies (none, or another project's:
 *                        cleared). The buffers: proj_tmp (a section saved / loaded), the stage (the song's next
 *                        part), the autosave, the song's backup of the loop. Bound at power-on (persist_boot), never
 *                        taken at run time.
 * In flash (stepx_log.c, the section log): a section's extras as their own record beside the section's. */
#ifndef STEPX
static stepx_t sx_work[NTRK];
#define STEPX(k) (&sx_work[k])
#endif
#define SX_AUX 4u
typedef struct {
    uint32_t psum;                                     /* the project these belong to (0: none) */
    stepx_t x[NTRK];
} sx_store_t;
static sx_store_t sx_aux[SX_AUX] __attribute__((section(".pool")));
static const project_t *sx_aux_p[SX_AUX];

/* the store of a project buffer; mk: take a free one for a buffer that has none (0: none) */
static sx_store_t *sx_for(const project_t *p, int mk)
{
    uint32_t i;
    for (i = 0; i < SX_AUX; i++)
        if (sx_aux_p[i] == p)
            return &sx_aux[i];
    if (mk)
        for (i = 0; i < SX_AUX; i++)
            if (!sx_aux_p[i]) {
                sx_aux_p[i] = p;
                return &sx_aux[i];
            }
    return 0;
}
static int sx_none(const stepx_t *x)                    /* NTRK tracks of nothing */
{
    uint32_t k;
    for (k = 0; k < NTRK; k++)
        if (!stepx_is_empty(&x[k]))
            return 0;
    return 1;
}
static void sx_clear_all(stepx_t *x)
{
    uint32_t k;
    for (k = 0; k < NTRK; k++)
        stepx_clear(&x[k]);
}

/* the working extras empty (all zero is NOT empty: 24 locks on step 0, P_LEVEL 0): at power-on (persist_boot; a host
 * test: before it sets any) */
static uint8_t sx_inited;
static void sx_init(void)
{
    uint32_t k;
    if (sx_inited)
        return;
    for (k = 0; k < NTRK; k++)
        stepx_clear(STEPX(k));
    sx_inited = 1;
}

/* proj_capture, after the sum: the working extras into the buffer's store */
static void sx_capture_store(const project_t *p)
{
    sx_store_t *m = sx_for(p, 1);
    uint32_t k;
    if (!m)
        return;
    for (k = 0; k < NTRK; k++)
        m->x[k] = *STEPX(k);
    m->psum = p->sum;
}
/* proj_apply (a load, a song section; the ISR or interrupts off): the buffer's store if it is this project's, else
 * none */
static void sx_apply_store(const project_t *p)
{
    const sx_store_t *m = sx_for(p, 0);
    uint32_t k;
    sx_inited = 1;                                     /* (every track set below) */
    for (k = 0; k < NTRK; k++)
        if (m && m->psum == p->sum)
            *STEPX(k) = m->x[k];
        else
            stepx_clear(STEPX(k));
}
/* the stored form of NTRK tracks x -> o (STEPX_ENC_MAX), its length; 0: nothing to store */
#define STEPX_ENC_MAX (NTRK * STEPX_ENC_TRK_MAX)
static uint32_t sx_encode(const stepx_t *x, uint8_t *o)
{
    uint32_t k, n = 0;
    if (sx_none(x))
        return 0;
    for (k = 0; k < NTRK; k++)
        n += stepx_encode_trk(&x[k], o + n);
    return n;
}
/* n bytes at a -> x (NTRK tracks); 0 refused (x cleared) */
static int sx_decode(stepx_t *x, const uint8_t *a, uint32_t n)
{
    const uint8_t *e = a + n;
    uint32_t k;
    for (k = 0; k < NTRK; k++)
        if (!stepx_decode_trk(&x[k], &a, e)) {
            sx_clear_all(x);
            return 0;
        }
    if (a != e) {
        sx_clear_all(x);
        return 0;
    }
    return 1;
}
/* a hash of the working extras (the autosave saves when the project or its extras changed) */
static uint32_t sx_hash(void)
{
    uint32_t k, h = 0x811C9DC5u;
    for (k = 0; k < NTRK; k++)
        h = (h ^ proj_hash(STEPX(k), sizeof(stepx_t))) * 16777619u;
    return h;
}
