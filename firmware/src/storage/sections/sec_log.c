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
 * 0) clears an id.
 * Format 2 (SLG_V2, the PATTERNS builds; docs/PROJECTS-DESIGN.md): every record carries its project (0..15, 0xFF
 * device-wide) and a dead byte (0xFF live, 0x00 once a newer record of its key is done); the index holds the open
 * project's and the device-wide records, slg_open_proj walks the area again for another project; the other
 * projects' live bytes are counted a sector (fliv) and their records copied as they are by a compaction. */
#ifndef SLG_V2
#define SLG_V2 FELUCCA_PATTERNS
#endif
#if SLG_V2
#ifndef SEC_LOG_BASE
#define SEC_LOG_BASE 0xA0000u                          /* (user sample slot 1, which the samples lose: eng_sample.c) */
#endif
#ifndef SEC_LOG_SECTORS
#if FELUCCA_ENG_SAMPLE || FELUCCA_DRUM_USR
#define SEC_LOG_SECTORS 20u                            /* 80 KiB: slot 1 exactly */
#else
#define SEC_LOG_SECTORS 32u                            /* 128 KiB: no samples built, the slots' room is free */
#endif
#endif
#else
#ifndef SEC_LOG_BASE
#define SEC_LOG_BASE 0x97000u
#endif
#ifndef SEC_LOG_SECTORS
#define SEC_LOG_SECTORS 8u
#endif
#endif
#ifndef SEC_IDS
#define SEC_IDS 16u
#endif
#include "../../../hal/fm1_flash_map.h"
_Static_assert(FL_IN(SEC_LOG_BASE, SEC_LOG_SECTORS * 0x1000u, FL_DATA_LO, FL_DATA_HI) &&
               !FL_NEVER(SEC_LOG_BASE, SEC_LOG_SECTORS * 0x1000u), "the section log: in the store, off the SDK's sectors");
/* the log's ids, the same in every build: 0..15 the sections A..P, 16..23 the songs (SEC_ID_SONG: the song chain
 * past the settings record's 16 parts; 17: kept for the patterns' state), 24..87 the per-track patterns
 * (docs/PATTERNS-DESIGN.md: 24 + 16 x track + slot). A build reads, counts and keeps (compaction copies them) the
 * records of every id here, whether it uses them or not: switching FELUCCA_SECTIONS 16 -> 8 -> 16, or a PATTERNS
 * build -> this one -> back, loses nothing (phase 0: this must ship long before patterns are written; a firmware
 * from before it seals a sector at an id past 23 and its compaction drops the rest). A record of an id past these
 * (a later firmware's) is skipped and the sector read on; not indexed, a compaction does not keep it. */
#define SLG_IDS 142u                                   /* 88..103 the sections' step extras, 104 the autosave's (stepx_log.c,
                                                        * kept by every build); 105..120 the sections' FX records, 121 the
                                                        * autosave's (fx_rec_log.c); 122..137 the sections' drum lane LFO
                                                        * records, 138 the autosave's (dlfo_rec_log.c); format 2: 19 the
                                                        * project's state (sections.c SEC_ID_STATE), 139..141 its extras, FX
                                                        * and LFO records */
#define SEC_ID_SONG 16u
#define SEC_ID_PAT0 24u                                /* the first pattern record (track 0, slot 0) */
_Static_assert(SEC_IDS <= SEC_ID_SONG, "the sections' ids come before the songs'");
#define SEC_SECT 4096u
_Static_assert(SEC_LOG_SECTORS <= 64u, "the index keeps where a record is as sector << 10 | offset / 4 (16 bits)");
/* format 2: the device-wide ids (proj 0xFF, in the index whatever project is open); every other id is the open
 * project's. 21 the project names, 20 a save as under way (sections.c). Which project is open is the settings record's
 * (project.c), not the log's: opening one must never need room in the log */
#define SEC_ID_PNAMES 21u
#define SEC_ID_SAVEAS 20u                              /* a save as under way (sections.c project_save_as: from, to) */
#define SLG_GLOBAL(id) ((id) == SEC_ID_PNAMES)         /* (the names: no project's record, never copied, cleared or streamed) */
#define SLG_DEV(id) (((id) | 1u) == SEC_ID_PNAMES)     /* the device-wide records, tagged 0xFF: the names and the marker (20) */
#define SLG_PROJ_GLOBAL 0xFFu
#define SLG_PROJ_NONE 0xFEu                            /* (slg.proj while booting: the device-wide ids alone) */
#define SLG_MINE(pr, id) ((pr) == SLG_PROJ_GLOBAL || (pr) == slg.proj)
#if SLG_V2
#define SEC_MAGIC 0x32474C53u                          /* "SLG2" */
#else
#define SEC_MAGIC 0x31474C53u                          /* "SLG1" */
#endif
#define SEC_RMAGIC 0x5345u                             /* "SE" */
#define SEC_HEAD 16u
#define SEC_ALIGN(n) (((n) + 3u) & ~3u)
/* a sector the log never uses (sections.c: another firmware's project kept there, FELUCCA_SL24_SAFE): never opened,
 * erased or counted as room */
#ifndef SLG_KEPT
#define SLG_KEPT(s) 0
#endif

typedef struct { uint32_t magic, seq, rsv, hcrc; } sec_shead_t;
typedef struct {
    uint16_t magic;
    uint8_t id, state;                                 /* state: 0xFF written, 0x00 done */
    uint32_t seq;
    uint16_t len;
    uint8_t proj, dead;                                /* format 2: the project (0xFF device-wide), 0xFF live / 0x00 superseded; format 1: 0xFF both */
    uint32_t crc;                                      /* CRC-32 of id, seq, len (format 2: proj too) and the bytes; */
} sec_rhead_t;                                         /* (state and dead left out: they change) */
_Static_assert(sizeof(sec_shead_t) == SEC_HEAD && sizeof(sec_rhead_t) == SEC_HEAD, "16-byte heads");

static struct {
    uint32_t seq;                                      /* the next record's sequence number */
    uint32_t sseq;                                     /* the next sector's */
    uint32_t aseq[SLG_IDS];                            /* the sequence number of id's newest record */
    uint16_t at[SLG_IDS];                              /* where it is, from SEC_LOG_BASE (0: none; slg_at) */
    uint16_t alen[SLG_IDS];                            /* its length */
    uint32_t head;                                     /* the sector written now (index) */
    uint32_t fill;                                     /* where in it the next record goes */
    uint32_t sorder[SEC_LOG_SECTORS];                  /* each sector's sequence number, 0 = erased */
    uint32_t tomb[(SLG_IDS + 31u) / 32u];              /* ids cleared while playing, the flash not written yet (slg_clear_pending) */
#if SLG_V2
    uint32_t fliv[SEC_LOG_SECTORS];                    /* each sector's live bytes of the other projects' records (with heads) */
    uint16_t pused;                                    /* the projects with a live record (a bit each; sections.c project_slot_used) */
#endif
    uint8_t up;
    uint8_t proj;                                      /* the open project (format 2; SLG_PROJ_NONE while booting) */
} slg IN_POOL;
#define SLG_TOMB(id) ((slg.tomb[(id) >> 5] >> ((id) & 31u)) & 1u)

static uint32_t slg_off(uint32_t s) { return SEC_LOG_BASE + s * SEC_SECT; }
/* where id's newest record is in flash, 0 none (a record is never at offset 0: the first sector's head is); kept as
 * sector << 10 | offset in it / 4 */
static uint32_t slg_at(uint32_t i) { return slg.at[i] ? slg_off(slg.at[i] >> 10) + ((slg.at[i] & 1023u) << 2) : 0u; }
static int slg_at_in(uint32_t i, uint32_t s) { return slg.at[i] && (slg.at[i] >> 10) == s; }
static void slg_index(uint32_t i, uint32_t off, uint32_t seq, uint32_t len)
{
    uint32_t r = off - SEC_LOG_BASE;
    slg.at[i] = (uint16_t)((r / SEC_SECT) << 10 | (r % SEC_SECT) >> 2);
    slg.aseq[i] = seq;
    slg.alen[i] = (uint16_t)len;
}
static uint32_t slg_rcrc(const sec_rhead_t *h, const uint8_t *data)
{
    uint8_t b[8];
    uint32_t c;
    b[0] = h->id;
    memcpy(b + 1, &h->seq, 4);
    memcpy(b + 5, &h->len, 2);
    b[7] = h->proj;
    c = st_crc32(b, SLG_V2 ? 8u : 7u);
    return c ^ (h->len ? st_crc32(data, h->len) : 0x5A5A5A5Au);
}
#if SLG_V2
/* the record at off: superseded (its dead byte to 0x00; a failure is left alone: a compaction finds the newer one) */
static void slg_mark_dead(uint32_t off)
{
    uint8_t z = 0;
    if (off && slg.up && !st_read(off + 11u, &z, 1) && z == 0xFFu)
        z = 0, (void)st_prog(off + 11u, &z, 1);
}
#endif
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

/* walk sector s: (ix) its records into slg.at (the newest per id), -> where the next record would go (SEC_SECT: full
 * or sealed); a sector without its head: 0 (erased, or erased while being written: erased again before use) */
static uint32_t slg_walk(uint32_t s, int ix)
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
        if (h.magic != SEC_RMAGIC || h.len > SEC_REC_MAX || p + SEC_HEAD + h.len > SEC_SECT)
            return SEC_SECT;                           /* a torn head: the rest of the sector is sealed */
        if (ix && h.state == 0x00u && (!h.len || !st_read(slg_off(s) + p + SEC_HEAD, slg_buf, h.len)) &&
            slg_rcrc(&h, slg_buf) == h.crc) {
            if (h.seq >= slg.seq)
                slg.seq = h.seq + 1u;              /* (an id past SLG_IDS too: its number stays used) */
#if SLG_V2
            if (h.len && h.dead == 0xFFu && h.proj < 16u)
                slg.pused |= (uint16_t)(1u << h.proj);
            if (!SLG_MINE(h.proj, h.id)) {         /* another project's: counted where it is (its tombstones not: */
                if (h.dead == 0xFFu && h.len)      /* the index counts none either), never indexed */
                    slg.fliv[s] += SEC_ALIGN(SEC_HEAD + h.len);
            } else
#endif
            if (h.id < SLG_IDS) {
                if (!slg.at[h.id] || h.seq >= slg.aseq[h.id]) {   /* (a compaction copy has its original's */
#if SLG_V2                                             /*  number: the same record) */
                    if (slg.at[h.id] && h.seq > slg.aseq[h.id])
                        slg_mark_dead(slg_at(h.id));   /* (the one indexed before: older) */
#endif
                    slg_index(h.id, slg_off(s) + p, h.seq, h.len);
                }
#if SLG_V2
                else if (h.seq < slg.aseq[h.id])
                    slg_mark_dead(slg_off(s) + p);     /* (older than the one indexed: its mark was cut) */
#endif
            }
        }
        p += SEC_ALIGN(SEC_HEAD + h.len);
    }
    return SEC_SECT;
}
static uint32_t slg_scan(uint32_t s) { return slg_walk(s, 1); }

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
#if SLG_V2
static int slg_open_proj(uint32_t p);
#endif
/* boot: the area scanned, the newest record of each id found; an area never used: its first sector opened. Format
 * 2: the device-wide ids first, then project 0's (the caller opens the project the settings say) */
static void slg_boot(void)
{
    uint32_t s, best = 0, bseq = 0, fill[SEC_LOG_SECTORS];
    memset(&slg, 0, sizeof slg);
    slg.seq = slg.sseq = 1;
#if SLG_V2
    slg.proj = SLG_PROJ_NONE;
#endif
    for (s = 0; s < SEC_LOG_SECTORS; s++)
        fill[s] = slg_scan(s);
    for (s = 0; s < SEC_LOG_SECTORS; s++)              /* the head: the sector opened last */
        if (slg.sorder[s] && slg.sorder[s] >= bseq)
            bseq = slg.sorder[s], best = s;
    if (!bseq) {
#if FELUCCA_SL24_SAFE
        for (s = 0; s < SEC_LOG_SECTORS && SLG_KEPT(s); s++)
            ;
        slg.up = s < SEC_LOG_SECTORS && !slg_open(s);
#else
        slg.up = !slg_open(0);
#endif
    } else {
        slg.head = best;
        slg.fill = fill[best];
        slg.up = 1;
        if (slg_erased() >= SEC_LOG_SECTORS)
            slg_heal();                                /* (a compaction cut short: finish it now) */
    }
#if SLG_V2
    if (slg.up)
        (void)slg_open_proj(0);
    else
        slg.proj = 0;
#endif
}

static uint32_t slg_free_in_head(void) { return slg.fill >= SEC_SECT ? 0u : SEC_SECT - slg.fill; }
static uint32_t slg_live_bytes(void)                    /* the newest records, with their heads (every project's) */
{
    uint32_t i, n = 0;
    for (i = 0; i < SLG_IDS; i++)
        if (slg.at[i] && slg.alen[i])
            n += SEC_ALIGN(SEC_HEAD + slg.alen[i]);
#if SLG_V2
    for (i = 0; i < SEC_LOG_SECTORS; i++)
        n += slg.fliv[i];
#endif
    return n;
}
/* the room for records once compacted: every sector but the spare, less the sector heads and an allowance for
 * the end of each sector a record did not fit (records do not span sectors) */
#define SEC_WASTE 512u
#if FELUCCA_SL24_SAFE
static uint32_t slg_nsect(void)                         /* the sectors the log may use */
{
    uint32_t s, n = 0;
    for (s = 0; s < SEC_LOG_SECTORS; s++)
        n += !SLG_KEPT(s);
    return n > 1u ? n : 2u;                            /* (none kept but one: at least no underflow) */
}
#define SEC_ROOM ((slg_nsect() - 1u) * (SEC_SECT - SEC_HEAD - SEC_WASTE))
#else
#define SEC_ROOM ((SEC_LOG_SECTORS - 1u) * (SEC_SECT - SEC_HEAD - SEC_WASTE))
#endif

/* a record of (proj, id) appended to the head: its head with state 0xFF, its bytes, the state byte; format 2: the
 * older record of the key (the index's, when this one is newer) is marked dead after. 0 ok, -1 flash */
static int slg_append_rawp(uint32_t id, uint32_t proj, uint32_t seq, const uint8_t *data, uint32_t len)
{
    sec_rhead_t h;
    uint32_t off = slg_off(slg.head) + slg.fill;
    uint8_t done = 0;
    h.magic = SEC_RMAGIC;
    h.id = (uint8_t)id;
    h.state = 0xFFu;
    h.seq = seq;
    h.len = (uint16_t)len;
    h.proj = (uint8_t)proj;
    h.dead = 0xFFu;
    h.crc = slg_rcrc(&h, data);
    if (st_prog(off, &h, sizeof h) || (len && st_prog(off + SEC_HEAD, data, len)) ||
        st_prog(off + 3u, &done, 1))              /* (the state byte: the record counts from here) */
        return -1;
    slg.fill += SEC_ALIGN(SEC_HEAD + len);
#if SLG_V2
    if (len && proj < 16u)
        slg.pused |= (uint16_t)(1u << proj);
    if (!SLG_MINE(proj, id)) {                         /* (a compaction's copy of another project's record) */
        if (len)
            slg.fliv[slg.head] += SEC_ALIGN(SEC_HEAD + len);
        return 0;
    }
#endif
    if (!slg.at[id] || seq >= slg.aseq[id]) {
#if SLG_V2
        if (slg.at[id] && seq > slg.aseq[id])
            slg_mark_dead(slg_at(id));
#endif
        slg_index(id, off, seq, len);
    }
    return 0;
}
static int slg_append_raw(uint32_t id, uint32_t seq, const uint8_t *data, uint32_t len)
{
    return slg_append_rawp(id, SLG_V2 && !SLG_DEV(id) ? slg.proj : SLG_PROJ_GLOBAL, seq, data, len);
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
        if (s != slg.head && !slg.sorder[s] && !SLG_KEPT(s))
            return s;
    return SEC_LOG_SECTORS;
}

/* where a valid record of (proj, id) with sequence seq (exactly, or, newer, any past it) is in sector t (0: none) */
static uint32_t slg_findp(uint32_t t, uint32_t proj, uint32_t id, uint32_t seq, int newer)
{
    uint32_t p = SEC_HEAD;
    if (!slg.sorder[t])
        return 0;
    while (p + SEC_HEAD <= SEC_SECT) {
        sec_rhead_t h;
        if (st_read(slg_off(t) + p, &h, sizeof h) || h.magic != SEC_RMAGIC || h.len > SEC_REC_MAX)
            return 0;
        if (h.id == id && (newer ? h.seq > seq : h.seq == seq) && h.state == 0x00u && (!SLG_V2 || h.proj == proj) &&
            (!h.len || !st_read(slg_off(t) + p + SEC_HEAD, slg_buf, h.len)) && slg_rcrc(&h, slg_buf) == h.crc)
            return slg_off(t) + p;
        p += SEC_ALIGN(SEC_HEAD + h.len);
    }
    return 0;
}
static uint32_t slg_find(uint32_t t, uint32_t id, uint32_t seq)
{
    return slg_findp(t, SLG_V2 && !SLG_DEV(id) ? slg.proj : SLG_PROJ_GLOBAL, id, seq, 0);
}
/* id i's newest record (in sector s) as a valid copy in another sector: where (0: none) */
static uint32_t slg_copy_of(uint32_t s, uint32_t i)
{
    uint32_t t, c;
    for (t = 0; t < SEC_LOG_SECTORS; t++)
        if (t != s && (c = slg_find(t, i, slg.aseq[i])) != 0)
            return c;
    return 0;
}
#if SLG_V2
/* sector s's live records of the other projects (done, not dead, their CRC) that must move before s is erased:
 * not those copied already into another sector, nor those a newer record of their key in s supersedes (the dead
 * mark cut), nor, in the oldest sector, its tombstones (nothing older is left to clear). dry: their bytes with
 * heads (nothing written); else copied into the head: 0 ok, -1 flash, -2 no room there */
static int slg_copy_foreign(uint32_t s, int oldest, int dry)
{
    uint32_t p = SEC_HEAD, t, n = 0;
    if (!slg.sorder[s] || (!slg.fliv[s] && oldest))   /* (not the oldest: its tombstones, uncounted, move too) */
        return 0;
    while (p + SEC_HEAD <= SEC_SECT) {
        sec_rhead_t h;
        if (st_read(slg_off(s) + p, &h, sizeof h))
            return -1;
        if (h.magic != SEC_RMAGIC || h.len > SEC_REC_MAX || p + SEC_HEAD + h.len > SEC_SECT)
            break;
        if (h.state == 0x00u && h.dead == 0xFFu && !SLG_MINE(h.proj, h.id) && h.proj < SLG_PROJ_NONE && !(oldest && !h.len) &&
            !slg_findp(s, h.proj, h.id, h.seq, 1)) {
            for (t = 0; t < SEC_LOG_SECTORS && (t == s || !slg_findp(t, h.proj, h.id, h.seq, 0)); t++)
                ;
            if (t >= SEC_LOG_SECTORS && (!h.len || !st_read(slg_off(s) + p + SEC_HEAD, slg_buf, h.len)) &&
                slg_rcrc(&h, slg_buf) == h.crc) {
                if (dry)
                    n += SEC_ALIGN(SEC_HEAD + h.len);
                else if (SEC_ALIGN(SEC_HEAD + h.len) > slg_free_in_head())
                    return -2;
                else if (slg_append_rawp(h.id, h.proj, h.seq, slg_buf, h.len))
                    return -1;
            }
        }
        p += SEC_ALIGN(SEC_HEAD + h.len);
    }
    return dry ? (int)n : 0;
}
#else
#define slg_copy_foreign(s, oldest, dry) 0
#endif
/* the head's bytes it takes to empty sector s: its live records found nowhere else, its tombstones too when it is
 * not the oldest (an older record of that section may sit in a sector older than the head) */
static uint32_t slg_heal_need(uint32_t s, int tombs)
{
    uint32_t i, n = 0;
    for (i = 0; i < SLG_IDS; i++)
        if (slg_at_in(i, s))
            n += !slg.alen[i] ? (tombs ? SEC_HEAD : 0u) : slg_copy_of(s, i) ? 0u : SEC_ALIGN(SEC_HEAD + slg.alen[i]);
    return n + (uint32_t)slg_copy_foreign(s, !tombs, 1);
}
/* no sector can be emptied into the head (below): when every record the index finds in the head has a valid copy
 * elsewhere (the head was opened by a compaction cut before it erased the oldest: it holds copies only, their
 * originals still there), the head is erased: the log is as it was before that compaction, the next write
 * compacts again. Else -2 (nothing done) */
static int slg_unopen(void)
{
    uint32_t h = slg.head, i, s, best = SEC_LOG_SECTORS;
    for (i = 0; i < SLG_IDS; i++)
        if (slg_at_in(i, h) && !slg_copy_of(h, i))
            return -2;
    if (slg_copy_foreign(h, 0, 1))                     /* (another project's record only there) */
        return -2;
    for (s = 0; s < SEC_LOG_SECTORS; s++)              /* the sector opened before it: the head again */
        if (s != h && slg.sorder[s] && (best >= SEC_LOG_SECTORS || slg.sorder[s] > slg.sorder[best]))
            best = s;
    if (best >= SEC_LOG_SECTORS)
        return -2;
    for (i = 0; i < SLG_IDS; i++)
        if (slg_at_in(i, h))
            slg_index(i, slg_copy_of(h, i), slg.aseq[i], slg.alen[i]);
    if (st_erase(slg_off(h)))
        return -1;
    slg.sorder[h] = 0;
    slg.head = best;
    slg.fill = slg_walk(best, 0);                      /* (where its records end; the index left as it is) */
    return 0;
}
/* no erased sector left (a compaction cut between opening the spare and erasing the oldest): a sector's live
 * records that exist nowhere else are copied into the head, then that sector is erased: the one that needs the
 * fewest bytes, the oldest on a tie (a cut copy may have left a record half written in the head, taking its room:
 * the oldest's records may not fit there any more). None fits (the oldest was nearly full of live records, the
 * torn copy took the room the rest needed, every other sector live too: likelier with the patterns' records):
 * slg_unopen */
static int slg_heal(void)
{
    uint32_t o = slg_oldest(), s = SEC_LOG_SECTORS, t, i, best = 0xFFFFFFFFu, n;
    for (t = 0; t < SEC_LOG_SECTORS; t++)              /* (the oldest first: it wins a tie) */
        if (t != slg.head && slg.sorder[t] && (n = slg_heal_need(t, t != o) + (t != o)) <= slg_free_in_head() &&
            n < best)
            best = n, s = t;
    if (s >= SEC_LOG_SECTORS)
        return slg_unopen();
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
            slg_index(i, c, slg.aseq[i], slg.alen[i]); /* (copied before the cut: point at the copy) */
        else if (st_read(slg_at(i) + SEC_HEAD, slg_buf, slg.alen[i]) || slg_append_raw(i, slg.aseq[i], slg_buf, slg.alen[i]))
            return -1;
    }
    {
        int rc = slg_copy_foreign(s, s == o, 0);       /* (the other projects' live records with them) */
        if (rc)
            return rc;
    }
    if (st_erase(slg_off(s)))
        return -1;
    slg.sorder[s] = 0;
#if SLG_V2
    slg.fliv[s] = 0;
#endif
    return 0;
}

/* need bytes in the head: a fresh sector, compacting the oldest into it when only the spare is left. drop: an id
 * whose record a compaction drops instead of copying (SLG_IDS none): the tombstone about to be written clears it
 * anyway, so a dense log (every sector a live record) still takes a clear; a cut leaves the id cleared */
static int slg_make_room(uint32_t need, uint32_t drop)
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
            n += !slg.sorder[i] && i != slg.head && !SLG_KEPT(i);
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
                if (slg_at_in(i, o)) {
                    uint32_t len = slg.alen[i];
                    if (i == drop) {
                        slg.at[i] = 0;                  /* (cleared: as its tombstone will say) */
                        continue;
                    }
                    if (len && st_read(slg_at(i) + SEC_HEAD, slg_buf, len))
                        return -1;
                    if (SEC_ALIGN(SEC_HEAD + len) > slg_free_in_head())
                        return -2;                     /* (cannot happen: a sector's live records fit a sector) */
                    if (len && slg_append_raw(i, slg.aseq[i], slg_buf, len))
                        return -1;
                    if (!len)
                        slg.at[i] = 0;                  /* (a tombstone in the oldest sector: nothing older left) */
                }
            {
                int rc = slg_copy_foreign(o, 1, 0);    /* (the other projects' live records with them) */
                if (rc)
                    return rc;
            }
            if (st_erase(slg_off(o)))
                return -1;
            slg.sorder[o] = 0;
#if SLG_V2
            slg.fliv[o] = 0;
#endif
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
typedef struct {                                       /* (on the stack, three in sm_more: kept narrow) */
    uint8_t in[SLG_IDS];                               /* the sector of id's newest record with bytes, 0xFF none */
    uint16_t sz[SLG_IDS];                              /* its bytes in the log, with its head */
    uint32_t live[SEC_LOG_SECTORS];                    /* each sector's live records, with their heads (bytes) */
    uint32_t sorder[SEC_LOG_SECTORS], head, fill, sseq;
} slg_model_t;
_Static_assert(SEC_LOG_SECTORS < 0xFFu && SEC_ALIGN(SEC_HEAD + SEC_REC_MAX) <= 0xFFFFu, "the model's narrow fields");
static void sm_init(slg_model_t *m)
{
    uint32_t i;
    memset(m->live, 0, sizeof m->live);
    for (i = 0; i < SLG_IDS; i++) {
        m->in[i] = 0xFFu;
        if (slg.at[i] && slg.alen[i]) {                /* (a tombstone is not copied: nothing to model) */
            m->in[i] = (uint8_t)(slg.at[i] >> 10);
            m->sz[i] = (uint16_t)SEC_ALIGN(SEC_HEAD + slg.alen[i]);
            m->live[m->in[i]] += m->sz[i];
        }
    }
    memcpy(m->sorder, slg.sorder, sizeof m->sorder);
    m->head = slg.head, m->fill = slg.fill, m->sseq = slg.sseq;
#if SLG_V2
    for (i = 0; i < SEC_LOG_SECTORS; i++)
        m->live[i] += slg.fliv[i];                     /* (the other projects' records: copied by a compaction too) */
#endif
}
static uint32_t sm_free(const slg_model_t *m) { return m->fill >= SEC_SECT ? 0u : SEC_SECT - m->fill; }
static void sm_open(slg_model_t *m, uint32_t s) { m->sorder[s] = m->sseq++, m->head = s, m->fill = SEC_HEAD, m->live[s] = 0; }
/* slg_make_room on the model: 1 the head has need bytes (a state that needs healing: 0) */
static int sm_room(slg_model_t *m, uint32_t need)
{
    uint32_t guard, s, e, o, n, i;
    for (n = 0, s = 0; s < SEC_LOG_SECTORS; s++)
        n += s != m->head && !m->sorder[s] && !SLG_KEPT(s);
    if (!n)
        return 0;                                      /* (slg_make_room heals first: not modelled) */
    for (guard = 0; guard < 2u * SEC_LOG_SECTORS && sm_free(m) < need; guard++) {
        for (e = SEC_LOG_SECTORS, o = SEC_LOG_SECTORS, n = 0, s = 0; s < SEC_LOG_SECTORS; s++) {
            if (s == m->head || SLG_KEPT(s))
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
                m->in[i] = (uint8_t)e;
        m->sorder[o] = 0;
    }
    return sm_free(m) >= need;
}
/* a record of len bytes (0: a tombstone) for id (>= SLG_IDS: a new one) on the model: 1 it went in */
static int sm_put(slg_model_t *m, uint32_t id, uint32_t len)
{
    uint32_t need = SEC_ALIGN(SEC_HEAD + len);
    if (!len && id < SLG_IDS && m->in[id] != 0xFFu) {  /* (a clear: its record dropped by a compaction, slg_make_room) */
        m->live[m->in[id]] -= m->sz[id];
        m->in[id] = 0xFFu;
    }
    if (!sm_room(m, need))
        return 0;
    if (id < SLG_IDS && m->in[id] != 0xFFu) {          /* (its old record: dead now) */
        m->live[m->in[id]] -= m->sz[id];
        m->in[id] = 0xFFu;
    }
    if (id < SLG_IDS && len)
        m->in[id] = (uint8_t)m->head, m->sz[id] = (uint16_t)need;
    if (len)
        m->live[m->head] += need;
    m->fill += need;
    return 1;
}
/* the reserve on the model: a record of the longest size for a new id (FELUCCA_PATTERNS: the playing scene stored
 * with four new patterns, each its own record); 1 it goes in */
static int sm_keep(slg_model_t *m)
{
#if FELUCCA_PATTERNS
    uint32_t k;
    for (k = 0; k < 4u; k++)
        if (!sm_put(m, SLG_IDS, PAT_REC_MAX))
            return 0;
    return sm_put(m, SLG_IDS, SCN_REC_MAX);
#else
    return sm_put(m, SLG_IDS, SEC_REC_MAX);
#endif
}
/* id := len bytes on the model, then the reserve: 1 both go in */
static int sm_reserve(slg_model_t *m, uint32_t id, uint32_t len) { return sm_put(m, id, len) && sm_keep(m); }

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
        if (!sm_keep(&r))
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
        int rc = slg_make_room(need, len ? SLG_IDS : id);
        if (rc)
            return rc == -2 ? 1 : -1;                  /* (-2: the sectors' ends: MEM FULL as well) */
    }
    if (slg_append_raw(id, slg.seq++, data, len))
        return -1;
    slg.tomb[id >> 5] &= ~(1u << (id & 31u));          /* (written: no clear is pending any more) */
    return 0;
}
/* id cleared while playing (the user, 2026-10-20): it reads as none at once (slg_has, slg_get), the tombstone is
 * written when quiet (slg_tombs_write). A record put again meanwhile is written, and the clear is gone. 1: pending */
static int slg_clear_pending(uint32_t id)
{
    if (!slg.up || id >= SLG_IDS || !slg.at[id] || !slg.alen[id])
        return 0;
    slg.tomb[id >> 5] |= 1u << (id & 31u);
    return 1;
}
static int slg_tombs_any(void)
{
    uint32_t i;
    for (i = 0; i < sizeof slg.tomb / sizeof slg.tomb[0]; i++)
        if (slg.tomb[i])
            return 1;
    return 0;
}
/* quiet: the pending clears into the log (a failed one stays pending, tried again) -> 0 all written */
static int slg_tombs_write(void)
{
    uint32_t id;
    int left = 0;
    for (id = 0; id < SLG_IDS; id++)
        if (SLG_TOMB(id) && slg_put(id, 0, 0, 1))
            left = 1;
    return left;
}
/* section id -> buf (SEC_REC_MAX), -> its length; 0 none (empty), -1 unreadable */
static int slg_get(uint32_t id, uint8_t *buf)
{
    if (id >= SLG_IDS || !slg.at[id] || !slg.alen[id] || SLG_TOMB(id))
        return 0;
    return st_read(slg_at(id) + SEC_HEAD, buf, slg.alen[id]) ? -1 : (int)slg.alen[id];
}
static int slg_has(uint32_t id) { return id < SLG_IDS && slg.at[id] && slg.alen[id] && !SLG_TOMB(id); }
#if SLG_V2
/* project p opened: the pending clears of the one before written, then the index holds p's records and the
 * device-wide ones (the area walked again; the sectors' order, the head and the sequence numbers stay). Stopped,
 * the arena written or dropped (sections.c); nothing is written for the opening itself. 0 ok, -1 flash */
static int slg_open_proj(uint32_t p)
{
    uint32_t s;
    if (p >= 16u)
        return -1;
    if (slg.up && slg.proj != SLG_PROJ_NONE && slg_tombs_write())
        return -1;
    slg.proj = (uint8_t)p;
    memset(slg.at, 0, sizeof slg.at);
    memset(slg.aseq, 0, sizeof slg.aseq);
    memset(slg.alen, 0, sizeof slg.alen);
    memset(slg.tomb, 0, sizeof slg.tomb);
    memset(slg.fliv, 0, sizeof slg.fliv);
    for (s = 0; s < SEC_LOG_SECTORS; s++)
        if (slg.sorder[s])
            (void)slg_walk(s, 1);
    return 0;
}
#endif
