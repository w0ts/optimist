/* SPDX-License-Identifier: GPL-3.0-only */
/* The song sections' log (firmware/src/storage/sections/sec_log.c) on a simulated NOR flash: records come back after a restart,
 * the newest of an id wins, compaction keeps every live record, a write cut at every flash program (and an erase
 * cut halfway) never loses or damages a section (the old one stays), MEM FULL keeps the reserve so the playing
 * section can always be saved, the gauge. Phase 0 (docs/PATTERNS-DESIGN.md): the random writes and cuts use all 88
 * ids, the patterns' 24..87 too; 64 pattern records beside 16 sections are kept across restarts and compactions and
 * counted in the gauge; a record of an id past 87 (a later firmware's) is skipped, its sector read on. Phase 0b: 16
 * busy 64-step sections fit in codec B (6 in codec A); with 16 pattern records, stores of the playing section cut at
 * a random program leave every section decoding to its project. Run by tests/run_tests.sh. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
#define SEC_TEST_A 1
static uint32_t trk_def_engine(uint32_t i) { return i < NPART ? (const uint8_t[]){0, 1, 4}[i] : 0u; }
#include "../firmware/src/storage/project.c"
#include "../firmware/src/storage/sections/sec_codec.c"
#include "sec_projects.h"

static uint8_t nor[0x100000];
static int fail_after = -1, erase_fail = -1;     /* the n-th program fails / the n-th erase stops halfway */
static uint32_t progs, erases;
static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off)
{
    erases++;
    if (erase_fail == 0) {
        uint32_t i;
        for (i = 0; i < 4096u; i += 2)                /* (half the bits back to 1: an erase cut short) */
            nor[off + i] = 0xFF;
        erase_fail = -1;
        return -9;
    }
    if (erase_fail > 0)
        erase_fail--;
    memset(nor + off, 0xFF, 4096);
    return 0;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    uint32_t i;
    if (fail_after == 0)
        return -9;
    if (fail_after > 0)
        fail_after--;
    progs++;
    for (i = 0; i < n; i++)
        nor[off + i] &= ((const uint8_t *)src)[i];
    return 0;
}
#include "../firmware/src/storage/storage.c"
#include "../firmware/src/storage/sections/sec_log.c"

static int bad;
static void check(const char *what, int ok)
{
    printf("%-78s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}
static uint32_t sc_rng = 777u;
static uint32_t rnd(uint32_t n) { sc_rng = sc_rng * 1664525u + 1013904223u; return (sc_rng >> 8) % n; }

static uint8_t model[SLG_IDS][SEC_REC_MAX];     /* what each id holds (the last write that succeeded) */
static uint32_t mlen[SLG_IDS];
static uint8_t data[SEC_REC_MAX], got[SEC_REC_MAX];

static void make(uint32_t n, uint32_t tag)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        data[i] = (uint8_t)(tag * 31u + i * 7u + rnd(3));
}
static int same_as_model(void)
{
    uint32_t i;
    for (i = 0; i < SLG_IDS; i++) {
        int n = slg_get(i, got);
        if (n != (int)mlen[i] || (n > 0 && memcmp(got, model[i], (uint32_t)n)))
            return 0;
    }
    return 1;
}
static int put(uint32_t id, uint32_t n, int playing)
{
    int rc = slg_put(id, data, n, playing);
    if (!rc) {
        memcpy(model[id], data, n);
        mlen[id] = n;
    }
    return rc;
}

/* a record of any id (a later firmware's too: past SLG_IDS) appended to the head as slg_append_raw writes one,
 * without the index: what another build left in the log */
static void put_foreign(uint32_t id, uint32_t seq, const uint8_t *b, uint32_t n)
{
    sec_rhead_t h;
    uint32_t off = slg_off(slg.head) + slg.fill;
    uint8_t done = 0;
    h.magic = SEC_RMAGIC, h.id = (uint8_t)id, h.state = 0xFFu, h.seq = seq, h.len = (uint16_t)n, h.proj = 0xFFu, h.dead = 0xFFu;
    h.crc = slg_rcrc(&h, b);
    st_prog(off, &h, sizeof h);
    st_prog(off + SEC_HEAD, b, n);
    st_prog(off + 3u, &done, 1);
    slg.fill += SEC_ALIGN(SEC_HEAD + n);
}
static void log_reset(void)
{
    memset(nor, 0xFF, sizeof nor);
    memset(mlen, 0, sizeof mlen);
    slg_boot();
}

/* phase 0 (docs/PATTERNS-DESIGN.md): the patterns' records (ids 24..87), as a PATTERNS build writes them, are read,
 * counted and kept by this build; an id past them (a later firmware's) is skipped, the rest of its sector read */
static void foreign_ids(void)
{
    uint32_t i, k, n, ok, before;
    int rc;
    log_reset();
    for (i = 0; i < 64u; i++) {                         /* every pattern slot, between the sections */
        n = 60u + rnd(200);
        make(n, 5000 + i);
        rc = put(SEC_ID_PAT0 + i, n, 0);
        if (i % 4u == 0u) {
            make(300, 5100 + i);
            rc |= put(i / 4u, 300, 0);
        }
        if (rc)
            break;
    }
    before = slg_live_bytes();
    slg_boot();
    check("the 64 pattern records (ids 24..87) beside 16 sections: all back after a restart", i == 64u && same_as_model());
    check("... counted in the MEM gauge (they take room) and the reserve still kept", slg_live_bytes() == before &&
          before * 100u / SEC_ROOM == slg_used_pct() && slg_used_pct() > 50u);
    for (k = 0, ok = 1; k < 300u; k++) {               /* sections stored over and over: compaction moves them */
        uint32_t id = rnd(SEC_IDS);
        n = 200u + rnd(700);
        make(n, 5200 + k);
        rc = put(id, n, 1);
        if (k % 13u == 0u)
            slg_boot();
        ok &= same_as_model() && rc != -1;
    }
    check("... 300 section stores after (compactions, restarts): every pattern record kept, byte for byte", ok);
    log_reset();
    make(400, 6000);
    ok = put(2, 400, 0) == 0;
    make(100, 6001);
    put_foreign(SLG_IDS + 12u, slg.seq++, data, 100);    /* (id 100: past this build's) */
    make(500, 6002);
    ok &= put(30, 500, 0) == 0;
    make(450, 6003);
    ok &= put(3, 450, 0) == 0;
    slg_boot();
    check("a record of an id past this build's (a later firmware's): skipped, the records after it in its sector read",
          ok && same_as_model() && slg_has(30) && slg_has(3));
}

/* codec B: the busy 64-step section (tests/sec_projects.h) as codec A wrote it and as codec B does: how many fit
 * (the reserve kept); 16 busy codec B sections and 16 pattern records, stores of the playing one cut at a random
 * program (compactions in them): each section decodes to its project after every restart */
static void busy_sections(void)
{
    static uint8_t ra[SEC_REC_MAX], rb[SEC_REC_MAX];
    static project_t want[SEC_IDS], got_p;
    static dlrec_t dl, got_d;
    uint32_t na, nb, fit_a, fit_b, i, k, ok = 1, cuts = 0, decodes = 1;
    int rc;
    sp_busy();
    proj_capture(&want[0], &dl);
    sec_test_a = 1;
    na = sec_encode(&want[0], &dl, ra);
    sec_test_a = 0;
    nb = sec_encode(&want[0], &dl, rb);
    log_reset();
    for (fit_a = 0; fit_a < SEC_IDS && slg_put(fit_a, ra, na, 0) == 0; fit_a++)
        ;
    log_reset();
    for (fit_b = 0; fit_b < SEC_IDS && slg_put(fit_b, rb, nb, 0) == 0; fit_b++)
        ;
    printf("busy 64-step sections: codec A %u B, %u of 16 fit; codec B %u B, %u of 16 fit (the reserve kept)\n", na, fit_a,
           nb, fit_b);
    check("codec B: 16 busy 64-step sections fit with codec B (codec A: fewer)", fit_b == SEC_IDS && fit_a < fit_b);
    log_reset();
    for (i = 0; i < 16u; i++) {                         /* 16 pattern records, as a PATTERNS build would keep */
        make(150, 7000 + i);
        put(SEC_ID_PAT0 + 4u * i, 60u + rnd(90), 0);
    }
    for (i = 0; i < SEC_IDS; i++) {                     /* 16 busy sections, each its own */
        sp_busy();
        trk[i % NTRK].step[i].note[0] = (uint8_t)(20u + i);
        proj_capture(&want[i], &dl);
        sec_canon(&want[i]);
        nb = sec_encode(&want[i], &dl, rb);
        memcpy(data, rb, nb);
        ok &= put(i, nb, 0) == 0;
    }
    check("... 16 busy codec B sections beside 16 pattern records: stored", ok);
    for (k = 0; k < 300u; k++) {                        /* the playing section stored again, the write cut */
        uint32_t id = rnd(SEC_IDS), p0 = progs;
        sp_busy();
        trk[k % NTRK].step[k % 64u].note[0] = (uint8_t)(30u + k % 90u);
        proj_capture(&got_p, &dl);
        sec_canon(&got_p);
        nb = sec_encode(&got_p, &dl, rb);
        memcpy(data, rb, nb);
        fail_after = (int)rnd(40);
        rc = slg_put(id, data, nb, 1);
        fail_after = -1;
        slg_boot();
        if (rc == 0) {
            memcpy(model[id], data, nb);
            mlen[id] = nb;
            want[id] = got_p;
        }
        cuts += rc != 0 && progs > p0;
        ok &= same_as_model() && slg_erased() < SEC_LOG_SECTORS;
        for (i = 0; i < SEC_IDS; i++) {
            int n = slg_get(i, got);
            decodes &= n > 0 && sec_decode(got, (uint32_t)n, &got_p, &got_d) && !memcmp(&got_p, &want[i], sizeof got_p);
        }
    }
    check("... stores of the playing section cut at a random program: every section and pattern record intact",
          ok && cuts > 50u);
    check("... after each restart all 16 sections decode to their projects, the spare kept", decodes);
}

#if SLG_V2
/* ---- format 2 (docs/PROJECTS-DESIGN.md): every record tagged with its project, the index holding the open one */
static const uint32_t PJ[2] = {3u, 5u};
static uint8_t pmodel[2][SLG_IDS][SEC_REC_MAX];   /* what each id holds in project PJ[j] (a device-wide id: in both) */
static uint32_t pmlen[2][SLG_IDS];
static int pput(uint32_t j, uint32_t id, uint32_t n, int playing)   /* project PJ[j] open: id := data */
{
    int rc;
    if (slg.proj != PJ[j])
        slg_open_proj(PJ[j]);
    rc = slg_put(id, data, n, playing);
    if (!rc) {
        uint32_t t;
        for (t = 0; t < 2u; t++)
            if (t == j || SLG_DEV(id)) {             /* (20, 21: device-wide) */
                memcpy(pmodel[t][id], data, n);
                pmlen[t][id] = n;
            }
    }
    return rc;
}
static int psame(uint32_t j)                          /* project PJ[j] opened: every id as its model */
{
    uint32_t i;
    if (slg_open_proj(PJ[j]))
        return 0;
    for (i = 0; i < SLG_IDS; i++) {
        int n = slg_get(i, got);
        if (n != (int)pmlen[j][i] || (n > 0 && memcmp(got, pmodel[j][i], (uint32_t)n)))
            return 0;
    }
    return 1;
}
/* valid records of (proj, id) in the whole area: how many (a cut dead mark leaves two until a compaction) */
static uint32_t pcount(uint32_t proj, uint32_t id)
{
    uint32_t s, p, n = 0;
    for (s = 0; s < SEC_LOG_SECTORS; s++) {
        sec_shead_t sh;
        memcpy(&sh, nor + slg_off(s), sizeof sh);
        if (sh.magic != SEC_MAGIC)
            continue;
        for (p = SEC_HEAD; p + SEC_HEAD <= SEC_SECT;) {
            sec_rhead_t h;
            memcpy(&h, nor + slg_off(s) + p, sizeof h);
            if (h.magic != SEC_RMAGIC || h.len > SEC_REC_MAX)
                break;
            n += h.state == 0x00u && h.dead == 0xFFu && h.proj == proj && h.id == id;
            p += SEC_ALIGN(SEC_HEAD + h.len);
        }
    }
    return n;
}
static void scoped_log(void)
{
    uint32_t i, k, n, ok = 1, cuts = 0, spare = 1, before;
    int rc;
    memset(nor, 0xFF, sizeof nor);
    memset(pmlen, 0, sizeof pmlen);
    slg_boot();
    ok = slg.up && slg.proj == 0u;
    make(500, 9000);
    ok &= pput(0, 0, 500, 0) == 0;
    make(300, 9001);
    ok &= pput(0, 30, 300, 0) == 0;
    ok &= slg_open_proj(PJ[1]) == 0 && !slg_has(0) && !slg_has(30) && slg_get(0, got) == 0;
    make(200, 9002);
    ok &= pput(1, 0, 200, 0) == 0;
    ok &= psame(0) && psame(1);
    slg_boot();
    check("two projects: one's records are not seen by the other; back and forth and after a restart, each as written",
          ok && slg.proj == 0u && psame(1) && psame(0));
    make(192, 9004);
    ok = pput(1, 21, 192, 0) == 0 && slg_has(21) && slg_open_proj(PJ[0]) == 0 && slg_has(21) && psame(0) && psame(1);
    make(192, 9005);
    ok &= pput(0, 21, 192, 0) == 0 && psame(1) && psame(0);
    check("a device-wide id (21 the project names) written in one project is seen in every project", ok);
    for (k = 0, ok = 1; k < 400u; k++) {              /* random writes in both, compactions, restarts */
        uint32_t j = rnd(2), id = rnd(SLG_IDS);
        n = rnd(4) ? 100u + rnd(900) : 0u;
        make(n, 9100 + k);
        rc = pput(j, id, n, 0);
        if (rc == 1)
            pput(j, id, 0, 0);
        ok &= rc != -1;
        if (k % 17u == 0u)
            slg_boot();
        if (k % 5u == 0u)
            ok &= psame(rnd(2));
    }
    check("400 random writes in two projects (switches, compactions, restarts): no flash error, each project's records as last written on the way", ok);
    ok = psame(0);
    k = psame(1);
    before = slg_live_bytes();
    ok &= slg_open_proj(PJ[0]) == 0;
    n = slg_live_bytes();
    check("... at the end each project's records as last written", ok && k);
    if (n != before)
        printf("gauge: %u B seen from project %u, %u B from project %u\n", before, PJ[1], n, PJ[0]);
    check("... the gauge the same from either project", n == before);
    for (k = 0, ok = 1; k < 300u; k++) {              /* writes cut at a random program, in either project */
        uint32_t j = rnd(2), id = rnd(SLG_IDS), p0;
        n = 300u + rnd(1200);
        make(n, 9600 + k);
        if (k % 5u == 0u)
            pput(j, rnd(SEC_IDS), 0, 0);
        if (slg.proj != PJ[j])
            slg_open_proj(PJ[j]);
        p0 = progs;
        fail_after = rnd(3) ? (int)rnd(5) : (int)rnd(30);
        rc = slg_put(id, data, n, 1);
        fail_after = -1;
        slg_boot();
        if (rc == 0) {
            uint32_t t;
            for (t = 0; t < 2u; t++)
                if (t == j || SLG_DEV(id)) {             /* (20, 21: device-wide) */
                    memcpy(pmodel[t][id], data, n);
                    pmlen[t][id] = n;
                }
        }
        cuts += rc != 0 && progs > p0;
        ok &= psame(j) && psame(1u - j);
        spare &= slg_erased() < SEC_LOG_SECTORS;
    }
    check("writes cut at a random program in either project (compactions in them): nothing of either lost, a spare after every start",
          ok && cuts > 30u && spare);
    {   /* the dead mark: a superseded record's byte is programmed by the write; a mark cut (two live records of one key)
         * is resolved by the compaction of their sector: the newer stays, the older never comes back in that project */
        uint32_t s, two;
        memset(nor, 0xFF, sizeof nor);
        memset(pmlen, 0, sizeof pmlen);
        slg_boot();
        make(400, 9900);
        ok = pput(0, 5, 400, 0) == 0;
        make(400, 9901);
        ok &= pput(0, 5, 400, 0) == 0 && pcount(PJ[0], 5) == 1u;   /* (the first marked dead by the second) */
        make(400, 9902);
        fail_after = 3;                                /* (head, bytes, state byte: then the mark of the old one fails) */
        rc = slg_put(5, data, 400, 0);
        fail_after = -1;
        if (!rc)
            memcpy(pmodel[0][5], data, 400), pmlen[0][5] = 400;
        two = pcount(PJ[0], 5);
        ok &= rc == 0 && two == 2u;
        for (k = 0; k < 200u && ok; k++) {             /* the other project fills the log: the sector holding both compacts */
            make(1000, 9910 + k);
            rc = pput(1, (k % 14u) + 1u, 1000, 0);
            if (rc == 1)
                pput(1, (k % 14u) + 1u, 0, 0);
            ok &= rc != -1;
        }
        for (s = 0; s < SEC_LOG_SECTORS; s++)
            ok &= slg.sorder[s] == 0u || slg.sorder[s] > 1u;   /* (the first sector was compacted and erased) */
        check("a superseded record is marked dead by the write; a mark cut short leaves two until their sector compacts: the newer wins",
              ok && psame(0) && pcount(PJ[0], 5) == 1u && psame(1));
    }
    {   /* MEM FULL counts every project: one fills the log, the other is refused, but its playing section's store takes the reserve */
        memset(nor, 0xFF, sizeof nor);
        memset(pmlen, 0, sizeof pmlen);
        slg_boot();
        make(SEC_REC_MAX, 9950);
        for (i = 0, rc = 0; i < SLG_IDS && rc == 0; i++)
            rc = pput(0, i, SEC_REC_MAX, 0);
        ok = rc == 1 && slg_open_proj(PJ[1]) == 0 && slg_used_pct() >= 60u && slg_more(SEC_REC_MAX) == 0u;
        make(SEC_REC_MAX, 9951);
        ok &= pput(1, 0, SEC_REC_MAX, 0) == 1 && pput(1, 0, SEC_REC_MAX, 1) == 0 && psame(1) && psame(0);
        check("MEM FULL counts every project: the full one refuses the other, whose playing section still saves (the reserve)", ok);
        for (i = 0; i < SLG_IDS; i++)
            pput(0, i, 0, 0), pput(1, i, 0, 0);
        check("... both cleared: room again", slg_used_pct() == 0u && slg_more(500) >= 30u);
    }
    printf("format 2: %u sectors at 0x%X, room %u B; 500-byte records: %u fit\n", SEC_LOG_SECTORS, SEC_LOG_BASE, SEC_ROOM, slg_more(500));
}
#endif

int main(void)
{
    uint32_t i, k, n, cut_ok = 1, cuts = 0, comp0, spare = 1;
    int rc, ok;
    memset(nor, 0xFF, sizeof nor);
    slg_boot();
    check("an empty area: up, no section", slg.up && !slg_has(0) && slg_get(5, got) == 0 && slg_used_pct() == 0u);
    make(500, 1);
    ok = put(0, 500, 0) == 0;
    make(80, 2);
    ok &= put(3, 80, 0) == 0;
    slg_boot();
    check("two sections written, a restart: both back", ok && same_as_model());
    check("an empty section costs nothing (no record, no flash)", slg_put(7, data, 0, 0) == 0 && !slg_has(7));
    /* many writes, compaction on the way, a restart after each */
    comp0 = erases;
    ok = 1;
    for (i = 0; i < 400u; i++) {
        uint32_t id = rnd(SLG_IDS);                 /* (the sections, the songs, the patterns: every id) */
        n = rnd(4) ? 200u + rnd(900) : 0u;          /* (0: the section cleared) */
        make(n, i);
        rc = put(id, n, 0);
        if (rc == 1)
            put(id, 0, 0);                            /* (MEM FULL: clear it to make room) */
        if (i % 17u == 0u)
            slg_boot();
        ok &= same_as_model();
    }
    check("400 writes over the 88 ids (sections, songs, patterns; compaction, restarts): each as last written", ok && erases > comp0);
    /* a write cut at every program, with compaction in it: the old version stays */
    for (k = 0; k < 400u && cut_ok; k++) {
        static uint8_t snap[SEC_LOG_SECTORS * SEC_SECT];
        static uint8_t msnap[SLG_IDS][SEC_REC_MAX];
        static uint32_t lsnap[SLG_IDS];
        uint32_t id = rnd(SLG_IDS), p0;
        memcpy(snap, nor + SEC_LOG_BASE, sizeof snap);
        memcpy(msnap, model, sizeof model);
        memcpy(lsnap, mlen, sizeof mlen);
        n = 300u + rnd(1200);
        make(n, 1000 + k);
        if (k % 5u == 0u)
            put(rnd(SEC_IDS), 0, 0);                    /* (some cleared: room for the next) */
        p0 = progs;
        fail_after = rnd(3) ? (int)rnd(4) : (int)rnd(30);   /* (the record's programs, or a compaction's) */
        rc = slg_put(id, data, n, 1);
        fail_after = -1;
        slg_boot();
        if (rc == 0) {
            memcpy(model[id], data, n);
            mlen[id] = n;
        }
        cut_ok &= same_as_model();
        spare &= slg_erased() < SEC_LOG_SECTORS;    /* (a start healed what the cut left: a spare again) */
        cuts += rc != 0 && progs > p0;
        (void)snap, (void)msnap, (void)lsnap;
    }
    check("a write cut at a random program (also in a compaction): the old section stays, nothing else lost",
          cut_ok && cuts > 20u);
    /* an erase cut halfway (during a compaction) */
    ok = 1;
    for (k = 0; k < 40u; k++) {
        uint32_t id = rnd(SEC_IDS);
        n = 1000u + rnd(2000);
        make(n, 2000 + k);
        erase_fail = (int)rnd(3);
        rc = slg_put(id, data, n, 1);
        erase_fail = -1;
        slg_boot();
        if (rc == 0) {
            memcpy(model[id], data, n);
            mlen[id] = n;
        }
        ok &= same_as_model();
        spare &= slg_erased() < SEC_LOG_SECTORS;
    }
    check("an erase cut halfway: every section as last written", ok);
    check("... after every cut (writes, compactions, erases) the next start leaves a spare sector (slg_heal)", spare);
    {   /* a compaction cut in its second copy: the half-written copy takes the head's room, the oldest's other
         * records no longer fit there; the start empties another sector instead (one whose records are all dead) */
        memset(nor, 0xFF, sizeof nor);
        memset(mlen, 0, sizeof mlen);
        slg_boot();
        ok = 1;
        for (i = 0; i < 21u; i++) {                     /* sector 0: 0 1 2, 1: 3 3 3 (all dead), 2: 3 4 5 .. 6: 15 16 17 */
            uint32_t id = i < 3u ? i : i < 6u ? 3u : i - 3u;
            make(1200, 3000 + i);
            ok &= put(id, 1200, 1) == 0;
        }
        ok &= slg_erased() < SEC_LOG_SECTORS && slg_oldest() == 0u;
        make(1200, 3200);
        fail_after = 6;                                 /* (the spare's head, id 0's three programs, id 1's head */
        rc = slg_put(18, data, 1200, 1);                /* and data: its state byte is cut) */
        fail_after = -1;
        slg_boot();
        check("a compaction cut with the oldest's records too large for the head left: the start heals another sector",
              ok && rc == -1 && slg_erased() < SEC_LOG_SECTORS && same_as_model() && put(18, 1200, 1) == 0 && same_as_model());
    }
    /* MEM FULL: raw-size sections until refused; the reserve takes the playing section's save */
    for (i = 0; i < SLG_IDS; i++)
        put(i, 0, 0);
    make(SEC_REC_MAX, 7);
    for (i = 0, rc = 0; i < SEC_IDS && rc == 0; i++)
        rc = put(i, SEC_REC_MAX, 0);
    check("MEM FULL: dense sections until refused (rc 1), the gauge near full, 0 more", rc == 1 && slg_used_pct() >= 60u &&
          slg_more(SEC_REC_MAX) == 0u);
    make(SEC_REC_MAX, 8);
    check("... the playing section (an id stored) can still be saved, raw size", put(0, SEC_REC_MAX, 1) == 0 &&
          same_as_model());
    for (i = 0; i < SEC_IDS; i++)
        put(i, 0, 0);
    check("... cleared: room again", slg_used_pct() == 0u && slg_more(500) >= 30u);
    {   /* dense distinct ids until refused, then a new id's playing store takes the reserve: a clear must still go in
         * (else nothing can ever be deleted to make room: the log is stuck at MEM FULL) */
        memset(nor, 0xFF, sizeof nor);
        memset(mlen, 0, sizeof mlen);
        slg_boot();
        make(SEC_REC_MAX, 9);
        for (i = 0, rc = 0; i < SEC_IDS && rc == 0; i++)
            rc = put(i, SEC_REC_MAX, 0);
        ok = rc == 1;
        make(SEC_REC_MAX, 10);
        ok &= put(SEC_ID_PAT0, SEC_REC_MAX, 1) == 0;
        rc = put(0, 0, 0);
        ok &= rc == 0 && !slg_has(0) && same_as_model();
        rc = put(1, 0, 0);
        make(500, 11);
        check("a dense log whose reserve a playing store took: a clear still goes in; two clears, and a store goes in again",
              ok && rc == 0 && put(2, 500, 0) == 0 && same_as_model());
    }
    printf("log: %u sectors of 4 KiB, room %u B (a raw section %u B kept in reserve); 500-byte sections: %u fit\n",
           SEC_LOG_SECTORS, SEC_ROOM, SEC_REC_MAX, slg_more(500));
    foreign_ids();
    busy_sections();
#if SLG_V2
    scoped_log();
#endif
    printf("sec log test %s\n", bad ? "FAILED" : "passed");
    return bad != 0;
}
