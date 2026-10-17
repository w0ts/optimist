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

/* the log side: as the FX record's (fx_rec_log.c xrec_t) */
static const xrec_t XR_DL = {dlr_rbuf, sizeof dlr_rbuf, SEC_PEND_DL, DLR_ID0, dlr_rec, dlr_from_rec};
static int dlr_log_put(uint32_t id, uint32_t key, const project_t *p, int playing) { return xrec_log_put(&XR_DL, id, key, p, playing); }
static void dlr_log_get(uint32_t id, uint32_t key, const project_t *p) { xrec_log_get(&XR_DL, id, key, p); }
static uint32_t dlr_sec_get(uint32_t id) { return xrec_sec_get(&XR_DL, id); }   /* -> dlr_rbuf */
/* section s was read into p from its record (key: the record's hash): its LFO record into p's store */
static void dlr_sec_read(uint32_t s, const project_t *p, uint32_t key) { dlr_from_rec(p, dlr_rbuf, dlr_sec_get(s), key); }
static int dlr_pend(uint32_t s, uint32_t n) { return xrec_pend(&XR_DL, s, n); }
static int dlr_sec_put(uint32_t id, const uint8_t *r, uint32_t n) { return xrec_sec_put(&XR_DL, id, r, n); }
