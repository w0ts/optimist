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

/* MIDI tracks' phase 0 (docs/MIDI-TRACKS-DESIGN.md 3): the ids 142..225 (64 MIDI patterns, 16 scenes' MIDI refs, 4
 * working copies), as a later firmware writes them, read, counted and kept by this build, beside the sections and the
 * synth patterns, across restarts and compactions */
static void midi_ids(void)
{
    uint32_t i, k, n, ok = 1, e0, before;
    int rc = 0;
    log_reset();
    check("the MIDI tracks' ids: 142..205 patterns, 206..221 the scenes' refs, 222..225 the working copies, SLG_IDS 226",
          SLG_ID_MPAT0 == 142u && SLG_ID_MSCN0 == 206u && SLG_ID_MWORK0 == 222u && SLG_IDS == 226u);
    for (i = SLG_ID_MPAT0; i < SLG_IDS && !rc; i++) {   /* every MIDI id: small records (the log's 28 KiB in mind) */
        n = i < SLG_ID_MSCN0 ? 20u + rnd(70) : i < SLG_ID_MWORK0 ? 4u : 60u + rnd(120);
        make(n, 11000 + i);
        rc = put(i, n, 0);
    }
    for (i = 0; i < SEC_IDS && !rc; i += 3u) {         /* a few sections and synth patterns beside them */
        make(400, 11300 + i);
        rc = put(i, 400, 0);
        make(120, 11400 + i);
        rc |= put(SEC_ID_PAT0 + i, 120, 0);
    }
    before = slg_live_bytes();
    slg_boot();
    check("... a record at every MIDI id beside sections and patterns: all back after a restart, counted in the gauge",
          !rc && same_as_model() && slg_live_bytes() == before);
    for (k = 0, e0 = erases; k < 300u; k++) {           /* sections stored over and over: compactions move them all */
        uint32_t id = rnd(SEC_IDS);
        n = 200u + rnd(700);
        make(n, 11500 + k);
        rc = put(id, n, 1);
        if (k % 13u == 0u)
            slg_boot();
        ok &= same_as_model() && rc != -1;
    }
    check("... 300 section stores after (compactions, restarts): every MIDI record kept, byte for byte", ok && erases > e0 + 10u);
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
static uint8_t pmodel[2][SLG_IX][SEC_REC_MAX];    /* what each id holds in project PJ[j] (a device-wide id: in both; past
                                                   * SLG_IDS: the live patterns, SLG_LV0 + id - 24) */
static uint32_t pmlen[2][SLG_IX];
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
    for (i = 0; i < SLG_IX; i++) {
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
/* ---- a later firmware's ids (past SLG_IDS: 226..255) in format 2: never indexed here, kept byte for byte */
/* the newest valid record of (proj, id) in the whole area, read from the flash as it is: its length (0 a clear, -1
 * none), its bytes -> b, its head -> *ho */
static int fnewest(uint32_t proj, uint32_t id, uint8_t *b, sec_rhead_t *ho, uint32_t *off)
{
    uint32_t s, p, best = 0;
    int n = -1;
    for (s = 0; s < SEC_LOG_SECTORS; s++) {
        sec_shead_t sh;
        memcpy(&sh, nor + slg_off(s), sizeof sh);
        if (sh.magic != SEC_MAGIC || sh.hcrc != st_crc32(&sh, 12))
            continue;
        for (p = SEC_HEAD; p + SEC_HEAD <= SEC_SECT;) {
            sec_rhead_t h;
            memcpy(&h, nor + slg_off(s) + p, sizeof h);
            if (h.magic != SEC_RMAGIC || h.len > SEC_REC_MAX || p + SEC_HEAD + h.len > SEC_SECT)
                break;
            if (h.state == 0x00u && h.proj == proj && h.id == id && (!best || h.seq > ho->seq) &&
                slg_rcrc(&h, nor + slg_off(s) + p + SEC_HEAD) == h.crc) {
                best = slg_off(s) + p, *ho = h, n = h.len;
                if (b)
                    memcpy(b, nor + best + SEC_HEAD, h.len);
            }
            p += SEC_ALIGN(SEC_HEAD + h.len);
        }
    }
    if (off)
        *off = best;
    return n;
}
/* a later firmware's record of (proj, id), n bytes (0: a clear), written as that firmware writes it: room first (this
 * build's compaction: it must keep what is there), the record, then its key's older record marked dead (mark 0: that
 * mark cut). The index read again (a restart) */
static uint32_t put_future(uint32_t id, uint32_t proj, const uint8_t *b, uint32_t n, int mark)
{
    sec_rhead_t h, o;
    uint32_t off, old = 0, done = 0;
    if (slg_make_room(SEC_ALIGN(SEC_HEAD + n), SLG_IDS))
        return 0;
    (void)fnewest(proj, id, 0, &o, &old);
    off = slg_off(slg.head) + slg.fill;
    h.magic = SEC_RMAGIC, h.id = (uint8_t)id, h.state = 0xFFu, h.seq = slg.seq++, h.len = (uint16_t)n, h.proj = (uint8_t)proj;
    h.dead = 0xFFu;
    h.crc = slg_rcrc(&h, b);
    st_prog(off, &h, sizeof h);
    if (n)
        st_prog(off + SEC_HEAD, b, n);
    st_prog(off + 3u, &done, 1);
    if (old && mark)
        st_prog(old + 11u, &done, 1);
    slg_boot();
    return off;
}
static uint32_t plive(uint32_t proj, uint32_t id)     /* (proj, id)'s records with bytes, not marked dead */
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
            n += h.state == 0x00u && h.dead == 0xFFu && h.len && h.proj == proj && h.id == id;
            p += SEC_ALIGN(SEC_HEAD + h.len);
        }
    }
    return n;
}
#define FK 40u
static struct { uint32_t id, proj, seq; int len; uint8_t b[200]; } fut[FK];   /* what each later firmware's key holds */
static int fut_same(void)                              /* every key's newest record in the flash as fut says, byte for byte */
{
    uint32_t k;
    sec_rhead_t h;
    for (k = 0; k < FK; k++) {
        int n = fnewest(fut[k].proj, fut[k].id, got, &h, 0);
        if (fut[k].len <= 0 ? n > 0 : n != fut[k].len || h.seq != fut[k].seq || memcmp(got, fut[k].b, (uint32_t)n))
            return 0;
    }
    return 1;
}
static void future_ids(void)
{
    uint32_t k, n, i, ok = 1, e0, cuts = 0, l0, l1, fb = 0;
    int rc;
    memset(nor, 0xFF, sizeof nor);
    memset(pmlen, 0, sizeof pmlen);
    slg_boot();
    for (i = 0; i < 6u; i++) {                          /* sections of both projects */
        make(500, 12000 + i);
        ok &= pput(i & 1u, i, 500, 0) == 0;
    }
    slg_open_proj(PJ[0]);
    l0 = slg_live_bytes();
    for (k = 0; k < FK; k++) {                          /* 30 ids of project PJ[0], 5 of PJ[1], 5 device-wide */
        fut[k].id = k < 30u ? SLG_IDS + k : SLG_IDS + 2u * (k % 5u) + (k >= 35u);
        fut[k].proj = k < 30u ? PJ[0] : k < 35u ? PJ[1] : 0xFFu;
        fut[k].len = (int)(1u + rnd(199));
        make((uint32_t)fut[k].len, 12100 + k);
        memcpy(fut[k].b, data, (uint32_t)fut[k].len);
        fut[k].seq = slg.seq;
        ok &= put_future(fut[k].id, fut[k].proj, data, (uint32_t)fut[k].len, 1) != 0;
        fb += SEC_ALIGN(SEC_HEAD + (uint32_t)fut[k].len);
    }
    l1 = slg_live_bytes();
    check("a later firmware's ids (226..255; two projects, device-wide): written, then a restart: each there, counted in the gauge",
          ok && fut_same() && l1 == l0 + fb && psame(0) && psame(1));
    for (k = 0; k < 16u; k++) {                         /* newer versions and clears, as that firmware writes them */
        uint32_t j = k * 2u + 1u;
        n = k < 10u ? 1u + rnd(199) : 0u;
        make(n, 12200 + k);
        fut[j].seq = slg.seq;
        ok &= put_future(fut[j].id, fut[j].proj, data, n, k % 3u != 0u) != 0;   /* (every third dead mark cut) */
        fut[j].len = (int)n;
        memcpy(fut[j].b, data, n);
    }
    check("... newer versions and clears of 16 of them (a third of their dead marks cut): the newest of each as written",
          ok && fut_same());
    for (k = 0, e0 = erases; k < 500u; k++) {           /* this build: stores in both projects, cut at random, restarts */
        uint32_t j = rnd(2), id = rnd(SEC_IDS), p0;
        n = 200u + rnd(900);
        make(n, 12300 + k);
        if (slg.proj != PJ[j])
            slg_open_proj(PJ[j]);
        p0 = progs;
        fail_after = rnd(4) ? -1 : (int)rnd(30);
        rc = slg_put(id, data, n, 1);
        fail_after = -1;
        if (rc == 1)
            rc = slg_put(id, data, 0, 1), n = 0;
        if (rc == 0)
            memcpy(pmodel[j][id], data, n), pmlen[j][id] = n;
        cuts += rc != 0 && progs > p0;
        if (k % 7u == 0u || rc != 0)
            slg_boot();
        ok &= fut_same() && psame(j) && psame(1u - j);
    }
    printf("later firmware's ids: %u compactions' erases, %u cut writes\n", erases - e0, cuts);
    check("... 500 stores of this build's (compactions, cuts, restarts): every later firmware's record byte for byte (id, project, sequence, bytes), no clear undone, no older version back",
          ok && erases > e0 + 20u && cuts > 20u);
    for (k = 1, ok = 1; k < FK; k += 2u)                /* superseded: at most one live record each (the newest) */
        ok &= pcount(fut[k].proj, fut[k].id) <= 1u;
    check("... the superseded versions all gone (each key at most its newest record live)", ok);
    {   /* a project's clear (sections.c proj_tomb) and copy (proj_copy) of those ids: slg_unk_clear, slg_unk_copy */
        sec_rhead_t h;
        ok = slg_unk_copy(PJ[0], 9u) == 0;
        for (k = 0; k < 30u; k++) {
            int c = fnewest(9u, fut[k].id, got, &h, 0);
            ok &= fut[k].len > 0 ? c == fut[k].len && !memcmp(got, fut[k].b, (uint32_t)c) : c < 0;
        }
        check("slg_unk_copy: project PJ[0]'s later firmware's records into project 9, bytes as they are, the clears not copied",
              ok && fut_same());
        ok = slg_unk_clear(PJ[0]) == 0;
        slg_boot();
        for (k = 0; k < 30u; k++)
            ok &= fnewest(PJ[0], fut[k].id, 0, &h, 0) <= 0 && plive(PJ[0], fut[k].id) == 0u;
        for (k = 30u; k < FK; k++)
            ok &= fut[k].len <= 0 || fnewest(fut[k].proj, fut[k].id, got, &h, 0) == fut[k].len;
        check("slg_unk_clear: project PJ[0]'s later firmware's ids cleared (a clear each, the older marked dead), the others' kept",
              ok && psame(0) && psame(1));
        for (k = 0, ok = 1; k < 200u; k++) {            /* compactions after: nothing cleared comes back */
            make(1000, 12900 + k);
            rc = pput(k & 1u, rnd(SEC_IDS), 1000, 0);
            ok &= rc != -1;
        }
        slg_boot();
        for (k = 0; k < 30u; k++) {
            int c = fnewest(9u, fut[k].id, got, &h, 0);
            ok &= fnewest(PJ[0], fut[k].id, 0, &h, 0) <= 0 && (fut[k].len > 0 ? c == fut[k].len && !memcmp(got, fut[k].b, (uint32_t)c) : c < 0);
        }
        check("... 200 stores after (compactions): the cleared stay cleared, project 9's copies kept", ok && psame(0) && psame(1));
    }
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
    for (k = 0, ok = 1; k < 400u; k++) {              /* random writes in both, compactions, restarts (the live patterns too) */
        uint32_t j = rnd(2), id = rnd(SLG_IX);
        id = id == SLG_IDS ? SLG_LV0 + 7u : id;
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
    for (k = 0, ok = 1; k < 300u; k++) {              /* writes cut at a random program, in either project (live patterns too) */
        uint32_t j = rnd(2), id = rnd(SLG_IX), p0;
        id = id == SLG_IDS ? SLG_LV0 + 9u : id;
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
/* ---- the live patterns (issue 142, docs/PATTERNS-DESIGN.md 9): ids 24..87 again under the tag 0x10 | p */
static int lput(uint32_t j, uint32_t id, uint32_t base, uint32_t n)   /* project PJ[j]: id's live record, base stamp + n bytes */
{
    memcpy(data + 4, data, n);
    memcpy(data, &base, 4);
    return pput(j, SLG_LVX(id), n ? n + 4u : 0u, 0);
}
static void live_scope(void)
{
    uint32_t ok, k, j, b, s, f0;
    int rc;
    memset(nor, 0xFF, sizeof nor);
    memset(pmlen, 0, sizeof pmlen);
    slg_boot();
    make(300, 7000);
    ok = pput(0, 30, 300, 0) == 0;                     /* saved (PJ0, 30) */
    b = slg.aseq[30];
    make(200, 7001);
    ok &= lput(0, 30, b, 200) == 0;                    /* its live record beside it */
    ok &= slg_get(30, got) == 300 && slg_get(SLG_LVX(30), got) == 204 && slg_live_ok(30) && pcount(PJ[0] | 0x10u, 30) == 1u &&
          pcount(PJ[0], 30) == 1u;
    check("a live pattern (tag 0x10 | p) beside its saved one (the same id): both kept, each its own record", ok);
    ok = slg_open_proj(PJ[1]) == 0 && !slg_has(SLG_LVX(30)) && !slg_has(30);
    for (s = 0, f0 = 0; s < SEC_LOG_SECTORS; s++)
        f0 += slg.fliv[s];
    check("... another project sees neither, the live one counted with the other projects' bytes", ok && f0 >= 2u * 220u);
    slg_boot();
    ok = slg_open_proj(PJ[0]) == 0 && psame(0) && psame(1);
    check("... after a restart: as written", ok);
    make(250, 7002);
    ok = lput(0, 30, b, 250) == 0 && pcount(PJ[0] | 0x10u, 30) == 1u && psame(0);
    check("... a newer live record supersedes the older (marked dead), the saved one stays", ok);
    for (k = 0, ok = 1; k < 300u; k++) {              /* the other project fills and compacts the log */
        make(900, 7100 + k);
        rc = pput(1, k % 15u, 900, 0);
        if (rc == 1)
            pput(1, k % 15u, 0, 0);
        ok &= rc != -1;
    }
    check("... 300 stores of the other project (compactions): the live and saved records of the first kept", ok && psame(1) && psame(0) &&
          slg_live_ok(30));
    memset(nor, 0xFF, sizeof nor);                     /* the projects strip: a project with only live patterns is used */
    memset(pmlen, 0, sizeof pmlen);
    slg_boot();
    make(100, 7200);
    ok = lput(1, 40, 0, 100) == 0 && slg_open_proj(0) == 0;
    slg_boot();
    check("a project with only a live pattern counts as used (pused)", ok && ((slg.pused >> PJ[1]) & 1u) && !((slg.pused >> PJ[0]) & 1u));
    /* the stale sweep: a live record whose base is not the saved record's sequence now (saved again since, here or by an
     * older firmware, or cleared) is dropped at the next project open; one that stands stays */
    memset(nor, 0xFF, sizeof nor);
    memset(pmlen, 0, sizeof pmlen);
    slg_boot();
    ok = slg_open_proj(PJ[0]) == 0;
    for (j = 0; j < 4u; j++) {
        make(120, 7300 + j);
        ok &= pput(0, 24u + j, 120, 0) == 0;
        make(80, 7310 + j);
        ok &= lput(0, 24u + j, slg.aseq[24u + j], 80) == 0;
    }
    make(60, 7320);
    ok &= lput(0, 60, 0, 60) == 0;                     /* (no saved record: base 0) */
    make(60, 7321);
    ok &= lput(0, 61, 12345u, 60) == 0;               /* (base names a saved record that is not there) */
    make(120, 7330);
    ok &= pput(0, 25, 120, 0) == 0;                    /* saved again (an older firmware's pattern save) */
    ok &= pput(0, 26, 0, 0) == 0;                      /* cleared (an older firmware's clear) */
    slg_live_sweep(0);
    pmlen[0][SLG_LVX(25)] = pmlen[0][SLG_LVX(26)] = pmlen[0][SLG_LVX(61)] = 0;
    slg_boot();
    ok &= slg_open_proj(PJ[0]) == 0 && psame(0) && slg_live_ok(24) && slg_live_ok(27) && slg_live_ok(60) && !slg_has(SLG_LVX(25)) &&
          !slg_has(SLG_LVX(26)) && !slg_has(SLG_LVX(61));
    check("the stale sweep: live patterns made from a saved record since replaced or cleared dropped, the others kept", ok);
    {   /* a sweep's skip: the slot whose saved record waits elsewhere is left for later */
        uint8_t sk[SLG_LV_N] = {0};
        make(120, 7340);
        ok = pput(0, 24, 120, 0) == 0 && slg_has(SLG_LVX(24)) && !slg_live_ok(24);
        sk[0] = 1;
        slg_live_sweep(sk);
        ok &= slg_has(SLG_LVX(24));
        slg_live_sweep(0);
        check("... a slot skipped (its saved record waits in the arena): left; swept once not skipped", ok && !slg_has(SLG_LVX(24)));
    }
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
    check("400 writes over the 226 ids (sections, songs, patterns, MIDI; compaction, restarts): each as last written", ok && erases > comp0);
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
    midi_ids();
    busy_sections();
#if SLG_V2
    scoped_log();
    future_ids();
    live_scope();
#endif
    printf("sec log test %s\n", bad ? "FAILED" : "passed");
    return bad != 0;
}
