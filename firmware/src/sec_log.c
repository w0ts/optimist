/* SPDX-License-Identifier: GPL-3.0-only */
/* The song sections' log (FELUCCA_SECTIONS): records of sec_codec.c in one shared flash area, by section id
 * (0..SEC_IDS-1: A..P), the newest record of an id wins. User design (2026-10-06): an empty section costs
 * nothing; room for one raw worst-case record is always kept, so saving the playing section never fails; with
 * no room left for another section: MEM FULL. A song is a chain of section ids (later: several songs).
 *
 * Area: SEC_LOG_SECTORS sectors of 4 KiB from SEC_LOG_BASE (the old project slots, 0x97000..0x9EFFF). Each
 * sector: a 16-byte head ("SLG1", its sequence number, the head's CRC), then records, each a 16-byte head and
 * its bytes, 4-aligned. Writing a record: its head with state 0xFF, then its bytes, then the state byte to 0x00
 * (flash bits only clear): a record counts once its state is 0x00 and its CRC holds. A write cut anywhere leaves
 * the old record of that id the newest. A head whose own CRC fails seals the rest of its sector.
 * One sector is always kept erased: compaction copies the live records of the oldest sector into it (the same
 * sequence numbers: a copy and its original are the same record), then erases the oldest. A tombstone (length
 * 0) clears an id. */
#ifndef SEC_LOG_BASE
#define SEC_LOG_BASE 0x97000u
#endif
#ifndef SEC_LOG_SECTORS
#define SEC_LOG_SECTORS 8u
#endif
#ifndef SEC_IDS
#define SEC_IDS 16u
#endif
/* the log's ids, the same in every build: 0..15 the sections A..P, 16..23 the songs (SEC_ID_SONG: the song chain
 * past the settings record's 16 parts). A build with fewer sections keeps the others' records (they count in the
 * gauge and compaction copies them), so switching FELUCCA_SECTIONS 16 -> 8 -> 16 loses nothing. */
#define SLG_IDS 24u
#define SEC_ID_SONG 16u
_Static_assert(SEC_IDS <= SEC_ID_SONG, "the sections' ids come before the songs'");
#define SEC_SECT 4096u
#define SEC_MAGIC 0x31474C53u                          /* "SLG1" */
#define SEC_RMAGIC 0x5345u                             /* "SE" */
#define SEC_HEAD 16u
#define SEC_ALIGN(n) (((n) + 3u) & ~3u)

typedef struct { uint32_t magic, seq, rsv, hcrc; } sec_shead_t;
typedef struct {
    uint16_t magic;
    uint8_t id, state;                                 /* state: 0xFF written, 0x00 done */
    uint32_t seq;
    uint16_t len, rsv;
    uint32_t crc;                                      /* CRC-32 of id, seq, len and the bytes; */
} sec_rhead_t;                                         /* (state left out: it changes) */
_Static_assert(sizeof(sec_shead_t) == SEC_HEAD && sizeof(sec_rhead_t) == SEC_HEAD, "16-byte heads");

static struct {
    uint32_t seq;                                      /* the next record's sequence number */
    uint32_t sseq;                                     /* the next sector's */
    uint32_t at[SLG_IDS];                              /* where the newest record of id is (0: none) */
    uint32_t aseq[SLG_IDS];                            /* its sequence number */
    uint16_t alen[SLG_IDS];                            /* its length */
    uint32_t head;                                     /* the sector written now (index) */
    uint32_t fill;                                     /* where in it the next record goes */
    uint32_t sorder[SEC_LOG_SECTORS];                  /* each sector's sequence number, 0 = erased */
    uint8_t up;
} slg;

static uint32_t slg_off(uint32_t s) { return SEC_LOG_BASE + s * SEC_SECT; }
static uint32_t slg_rcrc(const sec_rhead_t *h, const uint8_t *data)
{
    uint8_t b[7];
    uint32_t c;
    b[0] = h->id;
    memcpy(b + 1, &h->seq, 4);
    memcpy(b + 5, &h->len, 2);
    c = st_crc32(b, 7);
    return c ^ (h->len ? st_crc32(data, h->len) : 0x5A5A5A5Au);
}
static int slg_empty(uint32_t off, uint32_t n)          /* n bytes erased */
{
    uint8_t b[64];
    while (n) {
        uint32_t k = n < sizeof b ? n : (uint32_t)sizeof b, i;
        if (st_read(off, b, k))
            return 0;
        for (i = 0; i < k; i++)
            if (b[i] != 0xFFu)
                return 0;
        off += k, n -= k;
    }
    return 1;
}
#ifndef SLG_BUF_ATTR
#define SLG_BUF_ATTR                                   /* (the firmware: the pool, sections.c) */
#endif
static uint8_t slg_buf[SEC_REC_MAX + 3u] SLG_BUF_ATTR __attribute__((aligned(4)));

/* scan sector s: its records into slg.at (the newest per id), -> where the next record would go (SEC_SECT: full
 * or sealed); a sector without its head: 0 (erased, or erased while being written: erased again before use) */
static uint32_t slg_scan(uint32_t s)
{
    sec_shead_t sh;
    uint32_t p = SEC_HEAD;
    if (st_read(slg_off(s), &sh, sizeof sh) || sh.magic != SEC_MAGIC || sh.hcrc != st_crc32(&sh, 12)) {
        slg.sorder[s] = 0;
        return 0;
    }
    slg.sorder[s] = sh.seq;
    if (sh.seq >= slg.sseq)
        slg.sseq = sh.seq + 1u;
    while (p + SEC_HEAD <= SEC_SECT) {
        sec_rhead_t h;
        if (st_read(slg_off(s) + p, &h, sizeof h))
            return SEC_SECT;
        if (h.magic == 0xFFFFu && h.id == 0xFFu && h.len == 0xFFFFu && slg_empty(slg_off(s) + p, SEC_HEAD))
            return p;                                  /* the end: erased from here */
        if (h.magic != SEC_RMAGIC || h.id >= SLG_IDS || h.len > SEC_REC_MAX || p + SEC_HEAD + h.len > SEC_SECT)
            return SEC_SECT;                           /* a torn head: the rest of the sector is sealed */
        if (h.state == 0x00u && (!h.len || !st_read(slg_off(s) + p + SEC_HEAD, slg_buf, h.len)) &&
            slg_rcrc(&h, slg_buf) == h.crc) {
            if (h.seq >= slg.seq)
                slg.seq = h.seq + 1u;
            if (!slg.at[h.id] || h.seq >= slg.aseq[h.id]) {   /* (a compaction copy has its original's number: */
                slg.at[h.id] = slg_off(s) + p;                 /* the same record, either will do) */
                slg.aseq[h.id] = h.seq;
                slg.alen[h.id] = h.len;
            }
        }
        p += SEC_ALIGN(SEC_HEAD + h.len);
    }
    return SEC_SECT;
}

/* open sector s as the head: erase it (it may hold the rest of an erase cut short), write its head */
static int slg_open(uint32_t s)
{
    sec_shead_t sh;
    if (st_erase(slg_off(s)))
        return -1;
    sh.magic = SEC_MAGIC;
    sh.seq = slg.sseq++;
    sh.rsv = 0xFFFFFFFFu;
    sh.hcrc = st_crc32(&sh, 12);
    if (st_prog(slg_off(s), &sh, sizeof sh))
        return -1;
    slg.sorder[s] = sh.seq;
    slg.head = s;
    slg.fill = SEC_HEAD;
    return 0;
}

static int slg_heal(void);
static uint32_t slg_erased(void);
/* boot: the area scanned, the newest record of each id found; an area never used: its first sector opened */
static void slg_boot(void)
{
    uint32_t s, best = 0, bseq = 0, fill[SEC_LOG_SECTORS];
    memset(&slg, 0, sizeof slg);
    slg.seq = slg.sseq = 1;
    for (s = 0; s < SEC_LOG_SECTORS; s++)
        fill[s] = slg_scan(s);
    for (s = 0; s < SEC_LOG_SECTORS; s++)              /* the head: the sector opened last */
        if (slg.sorder[s] && slg.sorder[s] >= bseq)
            bseq = slg.sorder[s], best = s;
    if (!bseq) {
        slg.up = !slg_open(0);
        return;
    }
    slg.head = best;
    slg.fill = fill[best];
    slg.up = 1;
    if (slg_erased() >= SEC_LOG_SECTORS)
        slg_heal();                                    /* (a compaction cut short: finish it now) */
}

static uint32_t slg_free_in_head(void) { return slg.fill >= SEC_SECT ? 0u : SEC_SECT - slg.fill; }
static uint32_t slg_live_bytes(void)                    /* the newest records, with their heads */
{
    uint32_t i, n = 0;
    for (i = 0; i < SLG_IDS; i++)
        if (slg.at[i] && slg.alen[i])
            n += SEC_ALIGN(SEC_HEAD + slg.alen[i]);
    return n;
}
/* the room for records once compacted: every sector but the spare, less the sector heads and an allowance for
 * the end of each sector a record did not fit (records do not span sectors) */
#define SEC_WASTE 512u
#define SEC_ROOM ((SEC_LOG_SECTORS - 1u) * (SEC_SECT - SEC_HEAD - SEC_WASTE))

static int slg_append_raw(uint32_t id, uint32_t seq, const uint8_t *data, uint32_t len)
{
    sec_rhead_t h;
    uint32_t off = slg_off(slg.head) + slg.fill;
    uint8_t done = 0;
    h.magic = SEC_RMAGIC;
    h.id = (uint8_t)id;
    h.state = 0xFFu;
    h.seq = seq;
    h.len = (uint16_t)len;
    h.rsv = 0xFFFFu;
    h.crc = slg_rcrc(&h, data);
    if (st_prog(off, &h, sizeof h) || (len && st_prog(off + SEC_HEAD, data, len)) ||
        st_prog(off + 3u, &done, 1))              /* (the state byte: the record counts from here) */
        return -1;
    slg.fill += SEC_ALIGN(SEC_HEAD + len);
    if (!slg.at[id] || seq >= slg.aseq[id]) {
        slg.at[id] = off;
        slg.aseq[id] = seq;
        slg.alen[id] = (uint16_t)len;
    }
    return 0;
}

/* the oldest sector (not the head) */
static uint32_t slg_oldest(void)
{
    uint32_t s, best = SEC_LOG_SECTORS, bseq = 0xFFFFFFFFu;
    for (s = 0; s < SEC_LOG_SECTORS; s++)
        if (s != slg.head && slg.sorder[s] && slg.sorder[s] < bseq)
            bseq = slg.sorder[s], best = s;
    return best;
}
static uint32_t slg_erased(void)                        /* an erased sector other than the head, SEC_LOG_SECTORS none */
{
    uint32_t s;
    for (s = 0; s < SEC_LOG_SECTORS; s++)
        if (s != slg.head && !slg.sorder[s])
            return s;
    return SEC_LOG_SECTORS;
}

/* where a valid record of id with sequence seq is in sector t (0: none) */
static uint32_t slg_find(uint32_t t, uint32_t id, uint32_t seq)
{
    uint32_t p = SEC_HEAD;
    if (!slg.sorder[t])
        return 0;
    while (p + SEC_HEAD <= SEC_SECT) {
        sec_rhead_t h;
        if (st_read(slg_off(t) + p, &h, sizeof h) || h.magic != SEC_RMAGIC || h.id >= SLG_IDS || h.len > SEC_REC_MAX)
            return 0;
        if (h.id == id && h.seq == seq && h.state == 0x00u && (!h.len || !st_read(slg_off(t) + p + SEC_HEAD, slg_buf, h.len)) &&
            slg_rcrc(&h, slg_buf) == h.crc)
            return slg_off(t) + p;
        p += SEC_ALIGN(SEC_HEAD + h.len);
    }
    return 0;
}
static int slg_at_in(uint32_t i, uint32_t s) { return slg.at[i] && (slg.at[i] - SEC_LOG_BASE) / SEC_SECT == s; }
/* id i's newest record (in sector s) as a valid copy in another sector: where (0: none) */
static uint32_t slg_copy_of(uint32_t s, uint32_t i)
{
    uint32_t t, c;
    for (t = 0; t < SEC_LOG_SECTORS; t++)
        if (t != s && (c = slg_find(t, i, slg.aseq[i])) != 0)
            return c;
    return 0;
}
/* the head's bytes it takes to empty sector s: its live records found nowhere else, its tombstones too when it is
 * not the oldest (an older record of that section may sit in a sector older than the head) */
static uint32_t slg_heal_need(uint32_t s, int tombs)
{
    uint32_t i, n = 0;
    for (i = 0; i < SLG_IDS; i++)
        if (slg_at_in(i, s))
            n += !slg.alen[i] ? (tombs ? SEC_HEAD : 0u) : slg_copy_of(s, i) ? 0u : SEC_ALIGN(SEC_HEAD + slg.alen[i]);
    return n;
}
/* no erased sector left (a compaction cut between opening the spare and erasing the oldest): a sector's live
 * records that exist nowhere else are copied into the head, then that sector is erased: the one that needs the
 * fewest bytes, the oldest on a tie (a cut copy may have left a record half written in the head, taking its room:
 * the oldest's records may not fit there any more) */
static int slg_heal(void)
{
    uint32_t o = slg_oldest(), s = SEC_LOG_SECTORS, t, i, best = 0xFFFFFFFFu, n;
    for (t = 0; t < SEC_LOG_SECTORS; t++)              /* (the oldest first: it wins a tie) */
        if (t != slg.head && slg.sorder[t] && (n = slg_heal_need(t, t != o) + (t != o)) <= slg_free_in_head() &&
            n < best)
            best = n, s = t;
    if (s >= SEC_LOG_SECTORS)
        return -2;
    for (i = 0; i < SLG_IDS; i++) {
        uint32_t c;
        if (!slg_at_in(i, s))
            continue;
        if (!slg.alen[i]) {                            /* a tombstone: dropped from the oldest (nothing older left), */
            if (s == o)                                /* else kept */
                slg.at[i] = 0;
            else if (slg_append_raw(i, slg.aseq[i], slg_buf, 0))
                return -1;
        } else if ((c = slg_copy_of(s, i)) != 0)
            slg.at[i] = c;                             /* (copied before the cut: point at the copy) */
        else if (st_read(slg.at[i] + SEC_HEAD, slg_buf, slg.alen[i]) || slg_append_raw(i, slg.aseq[i], slg_buf, slg.alen[i]))
            return -1;
    }
    if (st_erase(slg_off(s)))
        return -1;
    slg.sorder[s] = 0;
    return 0;
}

/* need bytes in the head: a fresh sector, compacting the oldest into it when only the spare is left */
static int slg_make_room(uint32_t need)
{
    uint32_t guard;
    if (slg_erased() >= SEC_LOG_SECTORS) {             /* (heal first: the head's room is what heals) */
        int rc = slg_heal();
        if (rc)
            return rc;
    }
    for (guard = 0; guard < 2u * SEC_LOG_SECTORS && slg_free_in_head() < need; guard++) {
        uint32_t e = slg_erased(), n = 0, i;
        if (e >= SEC_LOG_SECTORS) {                    /* (no spare: finish what a cut compaction left) */
            int rc = slg_heal();
            if (rc)
                return rc;
            continue;
        }
        for (i = 0; i < SEC_LOG_SECTORS; i++)
            n += !slg.sorder[i] && i != slg.head;
        if (e < SEC_LOG_SECTORS && n >= 2u) {          /* (one stays the spare) */
            if (slg_open(e))
                return -1;
            continue;
        }
        {   /* compaction: the oldest sector's live records into the spare, then the oldest erased */
            uint32_t o = slg_oldest();
            if (o >= SEC_LOG_SECTORS || e >= SEC_LOG_SECTORS)
                return -2;                             /* (no room: every sector holds live records) */
            if (slg_open(e))
                return -1;
            for (i = 0; i < SLG_IDS; i++)
                if (slg.at[i] && slg.at[i] / SEC_SECT == slg_off(o) / SEC_SECT) {
                    uint32_t len = slg.alen[i];
                    if (len && st_read(slg.at[i] + SEC_HEAD, slg_buf, len))
                        return -1;
                    if (SEC_ALIGN(SEC_HEAD + len) > slg_free_in_head())
                        return -2;                     /* (cannot happen: a sector's live records fit a sector) */
                    if (len && slg_append_raw(i, slg.aseq[i], slg_buf, len))
                        return -1;
                    if (!len)
                        slg.at[i] = 0;                  /* (a tombstone in the oldest sector: nothing older left) */
                }
            if (st_erase(slg_off(o)))
                return -1;
            slg.sorder[o] = 0;
        }
    }
    return slg_free_in_head() >= need ? 0 : -2;
}

/* The reserve, exactly. A byte count (SEC_ROOM) does not tell whether a record fits: records do not span sectors,
 * so sectors holding one ~3 KB record each are full at 3 KB. slg_make_room is modelled instead (where each id's
 * newest record is, the sectors' order, the head): the same decisions on the same state, nothing read or written.
 * A store that is not the playing section's is taken only when, after it, a record of the longest size
 * (SEC_REC_MAX: raw, a full motion chunk, a drum record) for a new id would still go in. Fewer live bytes never
 * make room harder (a compaction copies less, the decisions are the same), so the playing section, whose old
 * record is copied like any other until the new one is in, can then always be saved. */
typedef struct {
    uint32_t in[SLG_IDS];                              /* the sector of id's newest record with bytes, 0xFF none */
    uint32_t sz[SLG_IDS];                              /* its bytes in the log, with its head */
    uint32_t live[SEC_LOG_SECTORS];                    /* each sector's live records, with their heads (bytes) */
    uint32_t sorder[SEC_LOG_SECTORS], head, fill, sseq;
} slg_model_t;
static void sm_init(slg_model_t *m)
{
    uint32_t i;
    memset(m->live, 0, sizeof m->live);
    for (i = 0; i < SLG_IDS; i++) {
        m->in[i] = 0xFFu;
        if (slg.at[i] && slg.alen[i]) {                /* (a tombstone is not copied: nothing to model) */
            m->in[i] = (slg.at[i] - SEC_LOG_BASE) / SEC_SECT;
            m->sz[i] = SEC_ALIGN(SEC_HEAD + slg.alen[i]);
            m->live[m->in[i]] += m->sz[i];
        }
    }
    memcpy(m->sorder, slg.sorder, sizeof m->sorder);
    m->head = slg.head, m->fill = slg.fill, m->sseq = slg.sseq;
}
static uint32_t sm_free(const slg_model_t *m) { return m->fill >= SEC_SECT ? 0u : SEC_SECT - m->fill; }
static void sm_open(slg_model_t *m, uint32_t s) { m->sorder[s] = m->sseq++, m->head = s, m->fill = SEC_HEAD, m->live[s] = 0; }
/* slg_make_room on the model: 1 the head has need bytes (a state that needs healing: 0) */
static int sm_room(slg_model_t *m, uint32_t need)
{
    uint32_t guard, s, e, o, n, i;
    for (n = 0, s = 0; s < SEC_LOG_SECTORS; s++)
        n += s != m->head && !m->sorder[s];
    if (!n)
        return 0;                                      /* (slg_make_room heals first: not modelled) */
    for (guard = 0; guard < 2u * SEC_LOG_SECTORS && sm_free(m) < need; guard++) {
        for (e = SEC_LOG_SECTORS, o = SEC_LOG_SECTORS, n = 0, s = 0; s < SEC_LOG_SECTORS; s++) {
            if (s == m->head)
                continue;
            if (!m->sorder[s]) {
                n++;
                if (e == SEC_LOG_SECTORS)
                    e = s;
            } else if (o == SEC_LOG_SECTORS || m->sorder[s] < m->sorder[o])
                o = s;
        }
        if (e == SEC_LOG_SECTORS)
            return 0;
        if (n >= 2u) {
            sm_open(m, e);
            continue;
        }
        if (o == SEC_LOG_SECTORS)
            return 0;
        sm_open(m, e);                                 /* compaction: the oldest's live records into the spare */
        m->fill += m->live[o];
        m->live[e] = m->live[o];
        m->live[o] = 0;
        for (i = 0; i < SLG_IDS; i++)
            if (m->in[i] == o)
                m->in[i] = e;
        m->sorder[o] = 0;
    }
    return sm_free(m) >= need;
}
/* a record of len bytes (0: a tombstone) for id (>= SLG_IDS: a new one) on the model: 1 it went in */
static int sm_put(slg_model_t *m, uint32_t id, uint32_t len)
{
    uint32_t need = SEC_ALIGN(SEC_HEAD + len);
    if (!sm_room(m, need))
        return 0;
    if (id < SLG_IDS && m->in[id] != 0xFFu) {          /* (its old record: dead now) */
        m->live[m->in[id]] -= m->sz[id];
        m->in[id] = 0xFFu;
    }
    if (id < SLG_IDS && len)
        m->in[id] = m->head, m->sz[id] = need;
    if (len)
        m->live[m->head] += need;
    m->fill += need;
    return 1;
}
/* id := len bytes on the model, then a record of the longest size for a new id: 1 both go in (the reserve kept) */
static int sm_reserve(slg_model_t *m, uint32_t id, uint32_t len) { return sm_put(m, id, len) && sm_put(m, SLG_IDS, SEC_REC_MAX); }

/* how full the area is, 0..100 % (the MEM gauge), and how many more sections of n bytes fit */
static uint32_t slg_used_pct(void) { return slg_live_bytes() * 100u / SEC_ROOM; }
/* how many more new records of n bytes the model m takes, the reserve kept after each, live bytes stored (and
 * waiting); counted on the model up to 64 (past that the byte count: room is not what runs out) */
static uint32_t sm_more(const slg_model_t *m0, uint32_t n, uint32_t live)
{
    slg_model_t m = *m0, t, r;
    uint32_t room = SEC_ROOM - SEC_ALIGN(SEC_HEAD + SEC_REC_MAX), bytes, k, cap;   /* (the reserve) */
    n = n ? n : 1u;
    bytes = live >= room ? 0u : (room - live) / SEC_ALIGN(SEC_HEAD + n);
    cap = bytes < 64u ? bytes : 64u;
    for (k = 0; k < cap; k++) {
        t = m;
        if (!sm_put(&t, SLG_IDS, n))
            break;
        r = t;
        if (!sm_put(&r, SLG_IDS, SEC_REC_MAX))
            break;
        m = t;
    }
    return k == 64u ? bytes : k;
}
static uint32_t slg_more(uint32_t n)
{
    slg_model_t m;
    sm_init(&m);
    return sm_more(&m, n, slg_live_bytes());
}

/* section id := the record (len bytes; 0: cleared). 0 ok, 1 MEM FULL (another section would not leave the
 * reserve; the playing one, playing != 0, may use it), -1 flash */
static int slg_put(uint32_t id, const uint8_t *data, uint32_t len, int playing)
{
    uint32_t need = SEC_ALIGN(SEC_HEAD + len);
    if (!slg.up || id >= SLG_IDS)
        return -1;
    if (!len && !slg.at[id])
        return 0;                                      /* (empty and nothing stored: nothing to write) */
    if (len && !playing) {                             /* (the reserve, exactly: the longest record still goes in; */
        slg_model_t m;                                 /* clearing a section is always allowed) */
        sm_init(&m);
        if (!sm_reserve(&m, id, len))
            return 1;
    }
    {
        int rc = slg_make_room(need);
        if (rc)
            return rc == -2 ? 1 : -1;                  /* (-2: the sectors' ends: MEM FULL as well) */
    }
    return slg_append_raw(id, slg.seq++, data, len) ? -1 : 0;
}
/* section id -> buf (SEC_REC_MAX), -> its length; 0 none (empty), -1 unreadable */
static int slg_get(uint32_t id, uint8_t *buf)
{
    if (id >= SLG_IDS || !slg.at[id] || !slg.alen[id])
        return 0;
    return st_read(slg.at[id] + SEC_HEAD, buf, slg.alen[id]) ? -1 : (int)slg.alen[id];
}
static int slg_has(uint32_t id) { return id < SLG_IDS && slg.at[id] && slg.alen[id]; }
