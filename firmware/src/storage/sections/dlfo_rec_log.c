/* SPDX-License-Identifier: GPL-3.0-only */
/* The drum lanes' LFO record (storage/dlfo_rec.c) in flash, included by sections.c (the section log, FELUCCA_SECTIONS 8 /
 * 16), as the FX record (fx/fx_rec_log.c): a section's LFO record is a log record of its own, id DLR_ID0 + section
 * (122..137), the autosave's DLR_ID_AUTO (138), beside the section's record and never inside it. An older build skips
 * these ids (past its SLG_IDS: not indexed) and its compaction drops them: its sections load there with every LFO off.
 *   record  key (4 bytes: the FNV-1a hash of the section's record as stored; the autosave: its project's sum), then the
 *           stored form (drums/lane_play.c dlfo_encode). Nothing to keep: no record (a tombstone clears an older one).
 * The key pairs the two: a section saved again without its LFO record finds an old key and plays every LFO off, never
 * another version's. A section stored while playing keeps its LFO record in the pending arena beside it (SEC_PEND_DL +
 * section), written with it. Snapshots (snapshots.c SNR_DLFO) and the editor's backup (ed_backup.c DLFO) carry these
 * records too. */
#define DLR_ID0 122u                                   /* (the FX records 105..121) */
#define DLR_ID_AUTO (DLR_ID0 + 16u)
_Static_assert(DLR_ID0 == FXR_ID_AUTO + 1u && DLR_ID_AUTO < SLG_IDS && SEC_IDS <= 16u, "the lane LFO records' log ids");
static uint8_t dlr_rbuf[4u + DLR_MAX] __attribute__((section(".pool"), aligned(4)));
_Static_assert(sizeof dlr_rbuf <= SEC_REC_MAX && sizeof dlr_rbuf <= 255u, "an LFO record: shorter than a section record, a byte's length");

/* the LFO record of the project p (in its buffer's store) into the log as id; 0 ok, 1 MEM FULL, -1 flash */
static int dlr_log_put(uint32_t id, uint32_t key, const project_t *p, int playing)
{
    return slg_put(id, dlr_rbuf, dlr_rec(key, p, dlr_rbuf), playing);
}
/* the log's record id for the project p just read (key): into p's store */
static void dlr_log_get(uint32_t id, uint32_t key, const project_t *p)
{
    int n = slg_has(id) && slg.alen[id] <= sizeof dlr_rbuf ? slg_get(id, dlr_rbuf) : 0;   /* (a longer one: not ours) */
    dlr_from_rec(p, dlr_rbuf, n > 0 ? (uint32_t)n : 0u, key);
}
/* the LFO record of section id (0..15; 16: the autosave's) as stored: the pending arena's, else the log's -> dlr_rbuf,
 * its length (0 none) */
static uint32_t dlr_sec_get(uint32_t id)
{
    uint32_t rl = 0;
    if (id < SEC_IDS && sec_pend_has(SEC_PEND_DL + id)) {
        rl = sec_pend.len[SEC_PEND_DL + id];
        if (rl <= sizeof dlr_rbuf)
            memcpy(dlr_rbuf, sec_pend.data + sec_pend.off[SEC_PEND_DL + id], rl);
        else
            rl = 0;
    } else if (flash_ok && slg_has(DLR_ID0 + id) && slg.alen[DLR_ID0 + id] <= sizeof dlr_rbuf) {
        int g = slg_get(DLR_ID0 + id, dlr_rbuf);
        rl = g > 0 ? (uint32_t)g : 0u;
    }
    return rl;
}
/* section s was read into p from its record (key: the record's hash): its LFO record into p's store */
static void dlr_sec_read(uint32_t s, const project_t *p, uint32_t key)
{
    dlr_from_rec(p, dlr_rbuf, dlr_sec_get(s), key);
}
/* section s's record (n bytes in sec_rbuf, just put in the arena): its LFO record beside it; 0 ok, 1 no room */
static int dlr_pend(uint32_t s, uint32_t n)
{
    uint32_t r = dlr_rec(proj_hash(sec_rbuf, n), &proj_tmp.cur, dlr_rbuf);
    sec_pend_del(SEC_PEND_DL + s);
    return r ? sec_pend_put(SEC_PEND_DL + s, dlr_rbuf, r) : 0;
}
/* a record for section id (0..16) as a snapshot or a backup gives it (key and all; n 0: none) into the log, its pending
 * one dropped: 0 ok, else not written */
static int dlr_sec_put(uint32_t id, const uint8_t *r, uint32_t n)
{
    if (id < SEC_IDS)
        sec_pend_del(SEC_PEND_DL + id);
    if (n > sizeof dlr_rbuf || (n && n <= 4u))
        return 1;
    if (r != dlr_rbuf)
        memcpy(dlr_rbuf, r, n);
    return slg_put(DLR_ID0 + id, dlr_rbuf, n, 1);
}
