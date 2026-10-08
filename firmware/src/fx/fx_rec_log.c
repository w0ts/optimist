/* SPDX-License-Identifier: GPL-3.0-only */
/* The FX record (fx_rec.c) in flash, included by sections.c (the section log, FELUCCA_SECTIONS 8 / 16), as SLOOP 2.4's
 * step extras (stepx_log.c): a section's FX record is a log record of its own, id FXR_ID0 + section (105..120), the
 * autosave's FXR_ID_AUTO (121), beside the section's record and never inside it (an older build reads a section
 * record with a flag it does not know as empty). An older build skips these ids and its compaction drops them: the
 * sections load there in the default layout.
 *   record  key (4 bytes: the FNV-1a hash of the section's record as stored; the autosave: its project's sum), then
 *           the stored form (fx_rec.c). Nothing to keep: no record (a tombstone clears an older one).
 * The key pairs the two: a section saved again without its FX record finds an old key and plays the default layout,
 * never another version's. A section stored while playing keeps its FX record in the pending arena beside it
 * (SEC_PEND_FX + section), written with it. Snapshots (snapshots.c SNR_FXR) and the editor's backup (ed_backup.c
 * FXSL) carry these records too. */
#define FXR_ID0 105u                                   /* (the patterns 24..87, the step extras 88..104) */
#define FXR_ID_AUTO (FXR_ID0 + 16u)
_Static_assert(FXR_ID_AUTO < SLG_IDS && SEC_IDS <= 16u, "the FX records' log ids");
static uint8_t fxr_rbuf[4u + FXR_MAX] __attribute__((section(".pool"), aligned(4)));
_Static_assert(sizeof fxr_rbuf <= SEC_REC_MAX, "an FX record is never longer than a section record");

/* the FX record of the project p (in its buffer's store) into the log as id; 0 ok, 1 MEM FULL, -1 flash */
static int fxr_log_put(uint32_t id, uint32_t key, const project_t *p, int playing)
{
    return slg_put(id, fxr_rbuf, fxr_rec(key, p, fxr_rbuf), playing);
}
/* the log's record id for the project p just read (key): into p's store */
static void fxr_log_get(uint32_t id, uint32_t key, const project_t *p)
{
    int n = slg_has(id) && slg.alen[id] <= sizeof fxr_rbuf ? slg_get(id, fxr_rbuf) : 0;   /* (a longer one: not ours) */
    fxr_from_rec(p, fxr_rbuf, n > 0 ? (uint32_t)n : 0u, key);
}
/* the FX record of section id (0..15; 16: the autosave's) as stored: the pending arena's, else the log's -> fxr_rbuf,
 * its length (0 none) */
static uint32_t fxr_sec_get(uint32_t id)
{
    uint32_t rl = 0;
    if (id < SEC_IDS && sec_pend_has(SEC_PEND_FX + id)) {
        rl = sec_pend.len[SEC_PEND_FX + id];
        if (rl <= sizeof fxr_rbuf)
            memcpy(fxr_rbuf, sec_pend.data + sec_pend.off[SEC_PEND_FX + id], rl);
        else
            rl = 0;
    } else if (flash_ok && slg_has(FXR_ID0 + id) && slg.alen[FXR_ID0 + id] <= sizeof fxr_rbuf) {
        int g = slg_get(FXR_ID0 + id, fxr_rbuf);
        rl = g > 0 ? (uint32_t)g : 0u;
    }
    return rl;
}
/* section s was read into p from its record (key: the record's hash): its FX record into p's store */
static void fxr_sec_read(uint32_t s, const project_t *p, uint32_t key)
{
    fxr_from_rec(p, fxr_rbuf, fxr_sec_get(s), key);
}
/* section s's record (n bytes in sec_rbuf, just put in the arena): its FX record beside it; 0 ok, 1 no room */
static int fxr_pend(uint32_t s, uint32_t n)
{
    uint32_t r = fxr_rec(proj_hash(sec_rbuf, n), &proj_tmp.cur, fxr_rbuf);
    sec_pend_del(SEC_PEND_FX + s);
    return r ? sec_pend_put(SEC_PEND_FX + s, fxr_rbuf, r) : 0;
}
/* a record for section id (0..16) as a snapshot or a backup gives it (key and all; n 0: none) into the log, its
 * pending one dropped: 0 ok, else not written */
static int fxr_sec_put(uint32_t id, const uint8_t *r, uint32_t n)
{
    if (id < SEC_IDS)
        sec_pend_del(SEC_PEND_FX + id);
    if (n > sizeof fxr_rbuf || (n && n <= 4u))
        return 1;
    if (r != fxr_rbuf)
        memcpy(fxr_rbuf, r, n);
    return slg_put(FXR_ID0 + id, fxr_rbuf, n, 1);
}
