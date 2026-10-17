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

/* A record of a section's beside its own (this FX record, the drum lanes' LFO record: dlfo_rec_log.c), the same way
 * for both: its buffer (key first), the arena's index and the log's id of section 0, its store's two ways */
typedef struct {
    uint8_t *buf;
    uint16_t size;                                     /* (sizeof buf) */
    uint8_t pend0, id0;
    uint32_t (*rec)(uint32_t key, const project_t *p, uint8_t *o);           /* fxr_rec */
    void (*from)(const project_t *p, const uint8_t *r, uint32_t n, uint32_t key);   /* fxr_from_rec */
} xrec_t;
_Static_assert(SEC_PEND_DL + SEC_IDS <= 255u, "a record kind's arena index: a byte");
/* the record of the project p (in its buffer's store) into the log as id; 0 ok, 1 MEM FULL, -1 flash */
static int xrec_log_put(const xrec_t *k, uint32_t id, uint32_t key, const project_t *p, int playing)
{
    return slg_put(id, k->buf, k->rec(key, p, k->buf), playing);
}
/* the log's record id for the project p just read (key): into p's store */
static void xrec_log_get(const xrec_t *k, uint32_t id, uint32_t key, const project_t *p)
{
    int n = slg_has(id) && slg.alen[id] <= k->size ? slg_get(id, k->buf) : 0;   /* (a longer one: not ours) */
    k->from(p, k->buf, n > 0 ? (uint32_t)n : 0u, key);
}
/* the record of section id (0..15; 16: the autosave's) as stored: the pending arena's, else the log's -> its buffer,
 * its length (0 none) */
static uint32_t xrec_sec_get(const xrec_t *k, uint32_t id)
{
    return sec_fetch(id < SEC_IDS ? k->pend0 + id : SEC_PEND_N, k->id0 + id, k->buf, k->size);
}
/* section s's record (n bytes in sec_rbuf, just put in the arena): its record beside it; 0 ok, 1 no room */
static int xrec_pend(const xrec_t *k, uint32_t s, uint32_t n)
{
    uint32_t r = k->rec(proj_hash(sec_rbuf, n), &proj_tmp.cur, k->buf);
    sec_pend_del(k->pend0 + s);
    return r ? sec_pend_put(k->pend0 + s, k->buf, r) : 0;
}
/* a record for section id (0..16) as a snapshot or a backup gives it (key and all; n 0: none) into the log, its
 * pending one dropped: 0 ok, else not written */
static int xrec_sec_put(const xrec_t *k, uint32_t id, const uint8_t *r, uint32_t n)
{
    if (id < SEC_IDS)
        sec_pend_del(k->pend0 + id);
    if (n > k->size || (n && n <= 4u))
        return 1;
    if (r != k->buf)
        memcpy(k->buf, r, n);
    return slg_put(k->id0 + id, k->buf, n, 1);
}

static const xrec_t XR_FX = {fxr_rbuf, sizeof fxr_rbuf, SEC_PEND_FX, FXR_ID0, fxr_rec, fxr_from_rec};
static int fxr_log_put(uint32_t id, uint32_t key, const project_t *p, int playing) { return xrec_log_put(&XR_FX, id, key, p, playing); }
static void fxr_log_get(uint32_t id, uint32_t key, const project_t *p) { xrec_log_get(&XR_FX, id, key, p); }
static uint32_t fxr_sec_get(uint32_t id) { return xrec_sec_get(&XR_FX, id); }   /* -> fxr_rbuf */
/* section s was read into p from its record (key: the record's hash): its FX record into p's store */
static void fxr_sec_read(uint32_t s, const project_t *p, uint32_t key) { fxr_from_rec(p, fxr_rbuf, fxr_sec_get(s), key); }
static int fxr_pend(uint32_t s, uint32_t n) { return xrec_pend(&XR_FX, s, n); }
static int fxr_sec_put(uint32_t id, const uint8_t *r, uint32_t n) { return xrec_sec_put(&XR_FX, id, r, n); }
