/* SPDX-License-Identifier: GPL-3.0-only */
/* SLOOP 2.4's step extras in flash (FELUCCA_SL24_XSTEP; stepx.h, stepx_proj.c), included by sections.c: a section's
 * extras are a record of their own in the section log, id SX_ID0 + section (88..103), the autosave's id SX_ID_AUTO
 * (104), beside the section's record and never inside it: a section record already fills a log sector at its
 * longest (sec_codec.c SEC_REC_MAX) and a record with a flag an older build does not know reads as empty there
 * (SEC_UNKNOWN). An older build skips these ids and its compaction drops them: the sections load there, without
 * their nudges, locks and fills.
 *
 *   record  key (4 bytes: the FNV-1a hash of the section's record as stored; the autosave: its project's sum), then
 *           the stored form of the four tracks (stepx.h stepx_encode_trk). None (every track empty): no record (a
 *           tombstone clears an older one)
 *
 * The key pairs the two: the section saved again without its extras record (a cut between the two writes, a
 * restore, a snapshot) finds an old key and plays no extras, never another version's. A section stored while
 * playing keeps its extras in the pending arena beside it (id SEC_IDS + section), written with it. */
#define SX_ID0 88u                                     /* sec_log.c SLG_IDS: 105 with the extras */
#define SX_ID_AUTO (SX_ID0 + 16u)
_Static_assert(SX_ID_AUTO < SLG_IDS && SEC_IDS <= 16u, "the extras' log ids");
static uint8_t sx_rbuf[4u + STEPX_ENC_MAX] __attribute__((section(".pool"), aligned(4)));
_Static_assert(sizeof sx_rbuf <= SEC_REC_MAX, "an extras record is never longer than a section record");

/* the record for key and x -> sx_rbuf, its length; 0: nothing to keep */
static uint32_t sx_rec(uint32_t key, const stepx_t *x)
{
    uint32_t n = sx_encode(x, sx_rbuf + 4);
    if (!n)
        return 0;
    memcpy(sx_rbuf, &key, 4);
    return 4u + n;
}
/* n bytes of a record -> x when its key is key (1), else x cleared (0) */
static int sx_from_rec(const uint8_t *r, uint32_t n, uint32_t key, stepx_t *x)
{
    uint32_t k;
    if (n < 4u || (memcpy(&k, r, 4), k != key) || !sx_decode(x, r + 4, n - 4u)) {
        sx_clear_all(x);
        return 0;
    }
    return 1;
}
/* the extras of the project p (in its buffer's store) into the log as id; 0 ok, 1 MEM FULL, -1 flash */
static int sx_log_put(uint32_t id, uint32_t key, const project_t *p, int playing)
{
    const sx_store_t *m = sx_for(p, 0);
    uint32_t n = m && m->psum == p->sum ? sx_rec(key, m->x) : 0u;
    return slg_put(id, sx_rbuf, n, playing);
}
/* the log's record id for the project p just read (key): into p's store */
static void sx_log_get(uint32_t id, uint32_t key, const project_t *p)
{
    sx_store_t *m = sx_for(p, 1);
    int n;
    if (!m)
        return;
    m->psum = p->sum;
    n = slg_has(id) && slg.alen[id] <= sizeof sx_rbuf ? slg_get(id, sx_rbuf) : 0;   /* (a longer one: not ours) */
    if (n <= 0)
        sx_clear_all(m->x);
    else
        (void)sx_from_rec(sx_rbuf, (uint32_t)n, key, m->x);
}
