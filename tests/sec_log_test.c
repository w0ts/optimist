/* SPDX-License-Identifier: GPL-3.0-only */
/* The song sections' log (firmware/src/sec_log.c) on a simulated NOR flash: records come back after a restart,
 * the newest of an id wins, compaction keeps every live record, a write cut at every flash program (and an erase
 * cut halfway) never loses or damages a section (the old one stays), MEM FULL keeps the reserve so the playing
 * section can always be saved, the gauge. Run by tests/run_tests.sh. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
static uint32_t trk_def_engine(uint32_t i) { return i < NPART ? (const uint8_t[]){0, 1, 4}[i] : 0u; }
#include "../firmware/src/project.c"
#include "../firmware/src/sec_codec.c"

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
#include "../firmware/src/storage.c"
#include "../firmware/src/sec_log.c"

static int bad;
static void check(const char *what, int ok)
{
    printf("%-78s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}
static uint32_t sc_rng = 777u;
static uint32_t rnd(uint32_t n) { sc_rng = sc_rng * 1664525u + 1013904223u; return (sc_rng >> 8) % n; }

static uint8_t model[SEC_IDS][SEC_REC_MAX];     /* what each id holds (the last write that succeeded) */
static uint32_t mlen[SEC_IDS];
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
    for (i = 0; i < SEC_IDS; i++) {
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

int main(void)
{
    uint32_t i, k, n, cut_ok = 1, cuts = 0, comp0;
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
        uint32_t id = rnd(SEC_IDS);
        n = rnd(4) ? 200u + rnd(900) : 0u;          /* (0: the section cleared) */
        make(n, i);
        rc = put(id, n, 0);
        if (rc == 1)
            put(id, 0, 0);                            /* (MEM FULL: clear it to make room) */
        if (i % 17u == 0u)
            slg_boot();
        ok &= same_as_model();
    }
    check("400 writes of 16 sections (compaction, restarts): every one as last written", ok && erases > comp0);
    /* a write cut at every program, with compaction in it: the old version stays */
    for (k = 0; k < 400u && cut_ok; k++) {
        static uint8_t snap[SEC_LOG_SECTORS * SEC_SECT];
        static uint8_t msnap[SEC_IDS][SEC_REC_MAX];
        static uint32_t lsnap[SEC_IDS];
        uint32_t id = rnd(SEC_IDS), p0;
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
    }
    check("an erase cut halfway: every section as last written", ok);
    /* MEM FULL: raw-size sections until refused; the reserve takes the playing section's save */
    for (i = 0; i < SEC_IDS; i++)
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
    printf("log: %u sectors of 4 KiB, room %u B (a raw section %u B kept in reserve); 500-byte sections: %u fit\n",
           SEC_LOG_SECTORS, SEC_ROOM, SEC_REC_MAX, slg_more(500));
    printf("sec log test %s\n", bad ? "FAILED" : "passed");
    return bad != 0;
}
