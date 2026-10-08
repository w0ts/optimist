/* SPDX-License-Identifier: GPL-3.0-only */
/* The snapshot area (FELUCCA_SNAPSHOTS, docs/SNAPSHOTS.md): whole-state snapshots, each a byte stream (snapshots.c
 * makes and reads it), kept in SN_SECTORS sectors of 4 KiB at USR3's end, below the banks (0xD8000). Every sector is
 * one part of one snapshot: a 32-byte header at 0, up to SN_PAY bytes from 32. Sectors need not be neighbours.
 *
 *   header  "SNS1", slot (0..7 the user's, 8 BEFORE LOAD: the same in every build), part, parts, version 1, seq (one
 *           counter for the area), this part's length and CRC-32, the stream's length and CRC-32, the header's CRC
 *
 * Valid: part 0's header and every part 1..n-1 present (same slot, seq, parts, stream length and CRC) with its CRC,
 * the lengths adding up, the stream's CRC. Per slot the valid part 0 with the highest seq wins (newer but its parts
 * damaged: the slot is DAMAGED); a sector of no winner is free.
 * Save: free sectors erased, the payload programmed, the headers of parts 1..n-1, part 0's header last (the commit),
 * read back; then the slot's older versions' part 0 erased, oldest first. Clear: the slot's part 0 sectors erased,
 * oldest first (a cut never brings an older version back). Neither touches another slot's sectors.
 * Flash access: storage.c's st_read / st_erase / st_prog (RAM sources), st_crc_upd. Host test: tests/snap_store_test.c. */
#define SN_MAGIC 0x31534E53u                          /* "SNS1" */
#define SN_SECT 4096u
#define SN_HEAD 32u
#define SN_PAY (SN_SECT - SN_HEAD)                    /* 4,064 payload bytes a sector */
#define SN_TOP 0xD8000u                               /* the banks (eng_sample.c SMP_BANKS) */
#define SN_BASE (SN_TOP - SN_SECTORS * SN_SECT)
#define SN_USER 8u                                    /* user slots in the format (a build shows FELUCCA_SNAPSHOTS) */
#define SN_BAK SN_USER                                /* BEFORE LOAD */
#define SN_NSLOT (SN_USER + 1u)
#define SN_PARTS 8u                                   /* the longest stream: 8 sectors (docs/SNAPSHOTS.md: <= 29.3 KB) */
#define SN_MAX (SN_PARTS * SN_PAY)
_Static_assert(SN_SECTORS <= 16u && SN_BASE >= 0xC8000u, "the snapshot area: inside USR3");
#include "../../../hal/fm1_flash_map.h"
_Static_assert(FL_IN(SN_BASE, SN_TOP - SN_BASE, FL_DATA_LO, FL_DATA_HI) && !FL_NEVER(SN_BASE, SN_TOP - SN_BASE),
               "the snapshot area: in the store, off the SDK's sectors");
enum { SN_EMPTY, SN_OK, SN_BAD };                     /* a slot's state */
enum { SN_FREE = 0, SN_HELD = 0xFEu };                /* sn.own: 0 free, slot + 1 a winner's, SN_HELD a writer's */
#define SN_KEPT 0xFDu                                 /* (FELUCCA_SL24_SAFE: storage.c st_kept) */

typedef struct {
    uint32_t magic;
    uint8_t slot, part, parts, ver;
    uint32_t seq, len, crc, total, tcrc, hcrc;
} sn_head_t;
_Static_assert(sizeof(sn_head_t) == SN_HEAD, "32-byte sector header");

typedef struct {
    uint8_t state, parts;
    uint8_t sec[SN_PARTS];                            /* part -> sector */
    uint32_t seq, total;
} sn_slot_t;
static struct {
    uint32_t seq;                                     /* the next snapshot's */
    uint8_t own[SN_SECTORS];
    sn_slot_t slot[SN_NSLOT];
    uint8_t up;                                       /* scanned */
} sn;

static uint32_t sn_off(uint32_t s) { return SN_BASE + s * SN_SECT; }
static int sn_head_ok(const sn_head_t *h)
{
    return h->magic == SN_MAGIC && h->ver == 1u && h->slot < SN_NSLOT && h->parts && h->parts <= SN_PARTS &&
           h->part < h->parts && h->len <= SN_PAY && h->total <= SN_MAX && h->hcrc == st_crc32(h, SN_HEAD - 4u);
}
static int sn_head(uint32_t s, sn_head_t *h) { return !st_read(sn_off(s), h, sizeof *h) && sn_head_ok(h); }

/* part p of snapshot (slot, seq) in the headers hd[] (valid[]): its sector, SN_SECTORS none */
static uint32_t sn_find(const sn_head_t *hd, const uint8_t *valid, const sn_head_t *h0, uint32_t p)
{
    uint32_t s;
    for (s = 0; s < SN_SECTORS; s++)
        if (valid[s] && hd[s].slot == h0->slot && hd[s].seq == h0->seq && hd[s].part == p && hd[s].parts == h0->parts &&
            hd[s].total == h0->total && hd[s].tcrc == h0->tcrc)
            return s;
    return SN_SECTORS;
}
/* each part's CRC and the stream's, read in 256-byte pieces: 1 all hold */
static int sn_crcs(const sn_slot_t *e, const sn_head_t *hd)
{
    uint8_t b[256];
    uint32_t p, c = 0xFFFFFFFFu, sum = 0;
    for (p = 0; p < e->parts; p++) {
        const sn_head_t *h = &hd[e->sec[p]];
        uint32_t o, pc = 0xFFFFFFFFu;
        if (h->len != (p + 1u < e->parts ? SN_PAY : e->total - sum))
            return 0;                                 /* (every part full but the last) */
        for (o = 0; o < h->len; o += sizeof b) {
            uint32_t k = h->len - o < sizeof b ? h->len - o : (uint32_t)sizeof b;
            if (st_read(sn_off(e->sec[p]) + SN_HEAD + o, b, k))
                return 0;
            pc = st_crc_upd(pc, b, k);
            c = st_crc_upd(c, b, k);
        }
        if (~pc != h->crc)
            return 0;
        sum += h->len;
    }
    return sum == e->total && ~c == hd[e->sec[0]].tcrc;
}

/* the area read: each slot's winner, the sectors taken, the next seq (power-on and after each change) */
static void sn_scan(void)
{
    sn_head_t hd[SN_SECTORS];
    uint8_t valid[SN_SECTORS], held[SN_SECTORS];
    uint32_t s, k, p, seq = sn.up ? sn.seq : 1u;      /* (a writer's seq is never given again) */
    for (s = 0; s < SN_SECTORS; s++)
        held[s] = sn.up && sn.own[s] == SN_HELD;      /* (a writer's sectors stay its own) */
    memset(&sn, 0, sizeof sn);
    sn.seq = seq;
    for (s = 0; s < SN_SECTORS; s++) {
        valid[s] = (uint8_t)sn_head(s, &hd[s]);
        if (valid[s] && hd[s].seq >= sn.seq)
            sn.seq = hd[s].seq + 1u;
        if (held[s])
            sn.own[s] = SN_HELD;
#if FELUCCA_SL24_SAFE
        else if (!valid[s] && st_kept(sn_off(s)))
            sn.own[s] = SN_KEPT;                      /* (another firmware's sample there: never taken) */
#endif
    }
    for (k = 0; k < SN_NSLOT; k++) {
        sn_slot_t *e = &sn.slot[k];
        uint32_t best = SN_SECTORS;
        for (s = 0; s < SN_SECTORS; s++)              /* the newest part 0 of slot k */
            if (valid[s] && !held[s] && hd[s].slot == k && hd[s].part == 0u && (best == SN_SECTORS || hd[s].seq > hd[best].seq))
                best = s;
        if (best == SN_SECTORS)
            continue;
        e->seq = hd[best].seq;
        e->total = hd[best].total;
        e->parts = hd[best].parts;
        e->state = SN_OK;
        for (p = 0; p < e->parts; p++) {
            uint32_t t = p ? sn_find(hd, valid, &hd[best], p) : best;
            if (t >= SN_SECTORS || held[t]) {
                e->state = SN_BAD;
                continue;
            }
            e->sec[p] = (uint8_t)t;
            sn.own[t] = (uint8_t)(k + 1u);
        }
        if (e->state == SN_OK && !sn_crcs(e, hd))
            e->state = SN_BAD;
    }
    sn.up = 1;
}

static uint32_t sn_free_count(void)
{
    uint32_t s, n = 0;
    for (s = 0; s < SN_SECTORS; s++)
        n += sn.own[s] == SN_FREE;
    return n;
}
static uint32_t sn_parts_for(uint32_t total) { return total ? (total + SN_PAY - 1u) / SN_PAY : 1u; }
/* would a stream of total bytes go into the free sectors (the slot's own winner stays until the new one is in) */
static int sn_fits(uint32_t total) { return total <= SN_MAX && sn_parts_for(total) <= sn_free_count(); }

/* erase every valid part 0 of slot k with a seq below lim (0: all), oldest first; 0 ok */
static int sn_erase_older(uint32_t k, uint32_t lim)
{
    uint32_t guard;
    for (guard = 0; guard <= SN_SECTORS; guard++) {
        sn_head_t h;
        uint32_t s, best = SN_SECTORS, bseq = 0;
        for (s = 0; s < SN_SECTORS; s++)
            if (sn.own[s] != SN_HELD && sn_head(s, &h) && h.slot == k && h.part == 0u && (!lim || h.seq < lim) &&
                (best == SN_SECTORS || h.seq < bseq))
                best = s, bseq = h.seq;
        if (best == SN_SECTORS)
            return 0;
        if (st_erase(sn_off(best)))
            return -1;
    }
    return -1;                                        /* (an erase that did not take) */
}
/* slot k emptied (a damaged one too); 0 ok, -1 flash */
static int sn_clear(uint32_t k)
{
    int rc;
    if (k >= SN_NSLOT)
        return -3;
    rc = sn_erase_older(k, 0);
    sn_scan();
    return rc;
}

/* ---- writing a stream: begin (sectors taken and erased), put (in order), commit (headers, part 0 last) */
typedef struct {
    uint32_t slot, total, at, seq, c;                 /* at: bytes put; c: the stream's CRC so far */
    uint32_t pc[SN_PARTS];                            /* each part's CRC so far */
    uint8_t parts, sec[SN_PARTS], open;
} sn_wr_t;

static void sn_wr_release(sn_wr_t *w)
{
    uint32_t p;
    for (p = 0; w->open && p < w->parts; p++)
        if (sn.own[w->sec[p]] == SN_HELD)
            sn.own[w->sec[p]] = SN_FREE;
    w->open = 0;
}
/* 0 ok, -2 no room, -3 arguments, -1 flash (the sectors given back) */
static int sn_wr_begin(sn_wr_t *w, uint32_t slot, uint32_t total)
{
    uint32_t p, s, i;
    memset(w, 0, sizeof *w);
    if (!sn.up)
        sn_scan();
    if (slot >= SN_NSLOT || !total)
        return -3;
    if (!sn_fits(total))
        return -2;
    w->slot = slot, w->total = total, w->parts = (uint8_t)sn_parts_for(total);
    w->seq = sn.seq++;
    w->c = 0xFFFFFFFFu;
    for (p = 0, i = 0; p < w->parts && i < SN_SECTORS; i++) {   /* (from a place that moves: the wear spread) */
        s = (w->seq + i) % SN_SECTORS;
        if (sn.own[s] == SN_FREE) {
            sn.own[s] = SN_HELD;
            w->sec[p] = (uint8_t)s;
            w->pc[p++] = 0xFFFFFFFFu;
        }
    }
    w->open = 1;
    for (p = 0; p < w->parts; p++)
        if (st_erase(sn_off(w->sec[p]))) {
            sn_wr_release(w);
            return -1;
        }
    return 0;
}
/* n bytes of the stream (in RAM: the driver's rule), in order; 0 ok, -3 past the end, -1 flash */
static int sn_wr_put(sn_wr_t *w, const uint8_t *b, uint32_t n)
{
    if (!w->open || w->at + n > w->total)
        return -3;
    while (n) {
        uint32_t p = w->at / SN_PAY, o = w->at % SN_PAY, k = SN_PAY - o < n ? SN_PAY - o : n;
        if (st_prog(sn_off(w->sec[p]) + SN_HEAD + o, b, k))
            return -1;
        w->pc[p] = st_crc_upd(w->pc[p], b, k);
        w->c = st_crc_upd(w->c, b, k);
        w->at += k, b += k, n -= k;
    }
    return 0;
}
/* the headers (part 0 last: the commit), read back, the slot's older versions erased; 0 ok, -3 not all put,
 * -1 flash (the old version stays) */
static int sn_wr_commit(sn_wr_t *w)
{
    static sn_head_t h;                               /* (a RAM source) */
    uint32_t p, q, ok;
    if (!w->open || w->at != w->total) {
        sn_wr_release(w);                             /* (its sectors back: nothing is committed) */
        return -3;
    }
    for (q = 0; q < w->parts; q++) {
        p = (q + 1u) % w->parts;                      /* 1, 2, .., n-1, then 0 */
        h.magic = SN_MAGIC;
        h.slot = (uint8_t)w->slot, h.part = (uint8_t)p, h.parts = w->parts, h.ver = 1;
        h.seq = w->seq;
        h.len = p + 1u < w->parts ? SN_PAY : w->total - p * SN_PAY;
        h.crc = ~w->pc[p];
        h.total = w->total;
        h.tcrc = ~w->c;
        h.hcrc = st_crc32(&h, SN_HEAD - 4u);
        if (st_prog(sn_off(w->sec[p]), &h, sizeof h)) {
            sn_wr_release(w);
            sn_scan();
            return -1;
        }
    }
    sn_wr_release(w);
    sn_scan();                                        /* (read back: the whole stream's CRCs) */
    ok = sn.slot[w->slot].state == SN_OK && sn.slot[w->slot].seq == w->seq;
    if (!ok) {
        (void)st_erase(sn_off(w->sec[0]));            /* (a write that did not hold: not the winner) */
        sn_scan();
        return -1;
    }
    (void)sn_erase_older(w->slot, w->seq);            /* (housekeeping: a cut here changes nothing) */
    sn_scan();
    return 0;
}

/* ---- reading: a stream by its slot entry (a winner, SN_OK; or a copy of one kept over a change, snapshots.c) */
static uint32_t sn_size(uint32_t k) { return k < SN_NSLOT && sn.slot[k].state == SN_OK ? sn.slot[k].total : 0u; }
/* n bytes from off -> dst; 0 ok, -3 outside, -1 flash */
static int sn_read_e(const sn_slot_t *e, uint32_t off, void *dst, uint32_t n)
{
    uint8_t *d = dst;
    if (e->state != SN_OK || off + n < off || off + n > e->total)
        return -3;
    while (n) {
        uint32_t p = off / SN_PAY, o = off % SN_PAY, c = SN_PAY - o < n ? SN_PAY - o : n;
        if (st_read(sn_off(e->sec[p]) + SN_HEAD + o, d, c))
            return -1;
        off += c, d += c, n -= c;
    }
    return 0;
}
static int sn_read(uint32_t k, uint32_t off, void *dst, uint32_t n) { return k < SN_NSLOT ? sn_read_e(&sn.slot[k], off, dst, n) : -3; }
/* a writer's stream as a slot entry (its bytes all put: checked before the commit) */
static void sn_wr_entry(const sn_wr_t *w, sn_slot_t *e)
{
    memset(e, 0, sizeof *e);
    e->state = SN_OK, e->parts = w->parts, e->total = w->total, e->seq = w->seq;
    memcpy(e->sec, w->sec, sizeof e->sec);
}
