/* SPDX-License-Identifier: GPL-3.0-only */
/* The extras record (FELUCCA_AUTO; auto_proj.c): what a section's or the autosave's automation store has beyond the
 * motion form (its step-only events as SLOOP 2.4's step extras, stepx.h; or, when today's forms cannot hold the store,
 * the whole store in its new form, auto.h AUTO_ENC_TAG), included by sections.c: a record of its own in the section
 * log, id SX_ID0 + section (88..103), the autosave's id SX_ID_AUTO (104), beside the section's record and never inside
 * it: a section record already fills a log sector at its longest (sec_codec.c SEC_REC_MAX) and a record with a flag an
 * older build does not know reads as empty there (SEC_UNKNOWN). An older build skips these ids and its compaction drops
 * them: the sections load there, without their nudges, locks and fills (a build with SLOOP 2.4's step extras reads the
 * old form, and refuses the new one: its first byte is no count it takes).
 *
 *   record  key (4 bytes: the FNV-1a hash of the section's record as stored; the autosave: its project's sum), then
 *           the bytes (auto_proj.c ax_encode). None (nothing beyond the motion form): no record (a tombstone clears
 *           an older one)
 *
 * The key pairs the two: the section saved again without its extras record (a cut between the two writes, a
 * restore, a snapshot) finds an old key and plays no extras, never another version's. A section stored while
 * playing keeps its extras in the pending arena beside it (id SEC_IDS + section), written with it. */
#define SX_ID0 88u                                     /* sec_log.c SLG_IDS: 105 with the extras */
#define SX_ID_AUTO (SX_ID0 + 16u)
_Static_assert(SX_ID_AUTO < SLG_IDS && SEC_IDS <= 16u, "the extras' log ids");
static uint8_t sx_rbuf[4u + AX_ENC_MAX] __attribute__((section(".pool"), aligned(4)));
_Static_assert(sizeof sx_rbuf <= SEC_REC_MAX, "an extras record is never longer than a section record");

/* the record for key and store m -> sx_rbuf, its length; 0: nothing to keep */
static uint32_t sx_rec(uint32_t key, const auto_store_t *m)
{
    uint32_t n = ax_encode(m, sx_rbuf + 4);
    if (!n)
        return 0;
    memcpy(sx_rbuf, &key, 4);
    return 4u + n;
}
/* n bytes of a record -> m when its key is key (1: added, or the store replaced), else nothing (0) */
static int sx_from_rec(const uint8_t *r, uint32_t n, uint32_t key, auto_store_t *m)
{
    uint32_t k;
    if (n < 4u || (memcpy(&k, r, 4), k != key))
        return 0;
    return ax_decode(m, r + 4, n - 4u);
}
/* the store of the project p into the log as id; 0 ok, 1 MEM FULL, -1 flash */
static int sx_log_put(uint32_t id, uint32_t key, const project_t *p, int playing)
{
    const auto_store_t *m = auto_of(p);
    uint32_t n = m ? sx_rec(key, m) : 0u;
    return slg_put(id, sx_rbuf, n, playing);
}
/* the log's record id for the project p just read (key; its motion form read before): into p's store */
static void sx_log_get(uint32_t id, uint32_t key, const project_t *p)
{
    auto_store_t *m = auto_for(p, 1);
    int n;
    if (!m)
        return;
    if (m->psum != p->sum)
        auto_store_clear(m);
    m->psum = p->sum;
    n = slg_has(id) && slg.alen[id] <= sizeof sx_rbuf ? slg_get(id, sx_rbuf) : 0;   /* (a longer one: not ours) */
    if (n > 0)
        (void)sx_from_rec(sx_rbuf, (uint32_t)n, key, m);
}
