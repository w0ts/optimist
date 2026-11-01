/* SPDX-License-Identifier: GPL-3.0-only */
/* The section log across firmware versions (MIDI tracks' phase 0, docs/MIDI-TRACKS-DESIGN.md 3): this build's loader
 * (firmware/src/storage/sections/sec_log.c, SLG_IDS 226) against the previous firmware's (tests/compat/sec_log_142.c:
 * Optimist fm1-next 4c2a0194's sec_log.c, unchanged since a5451887, SLG_IDS 142), compiled as a second binary
 * (-DSLG_PREV=1). Each run is one step on a log image file; tests/sec_log_compat.sh chains them:
 *   gen IMG DUMP        a deterministic log: random stores and clears of the ids both builds know (0..141) in three
 *                       projects (format 2), compactions, restarts; this build's also writes the MIDI tracks' ids
 *                       (142..225) between them. IMG the log area, DUMP every project's records of ids 0..141
 *   load IMG DUMP       the log booted from IMG, its records dumped (as gen's)
 *   churn IMG OUT DUMP  booted from IMG, every record of 0..141 stored again (the same bytes) three times in each
 *                       project: compaction after compaction; dumped, the log written to OUT
 *   mdump IMG DUMP      this build: the log booted from IMG, every project's records of the MIDI ids (142..225)
 *   ldump IMG DUMP      this build, format 2: every project's live patterns (issue 142: ids 24..87 under the tag 0x10 | p),
 *                       which gen writes between the others; a previous firmware keeps them through its compactions
 *   pused IMG           the projects strip's bits (format 2), as the build reads them
 * -DSLG_PREV=226: the previous firmware is main 64c1d915's sec_log.c (tests/compat/sec_log_226.c), the last before the
 * live patterns.
 * Run by tests/run_tests.sh through tests/sec_log_compat.sh. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
static uint32_t trk_def_engine(uint32_t i) { return i < NPART ? (const uint8_t[]){0, 1, 4}[i] : 0u; }
#include "../firmware/src/storage/project.c"
#include "../firmware/src/storage/sections/sec_codec.c"

static uint8_t nor[0x100000];
static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off) { memset(nor + off, 0xFF, 4096); return 0; }
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        nor[off + i] &= ((const uint8_t *)src)[i];
    return 0;
}
#include "../firmware/src/storage/storage.c"
#if SLG_PREV == 226
#include "compat/sec_log_226.c"                        /* (main 64c1d915's: SLG_IDS 226, before the live patterns) */
#elif SLG_PREV
#include "compat/sec_log_142.c"                        /* (its flash map include resolves through -I.../sections) */
#else
#include "../firmware/src/storage/sections/sec_log.c"
#endif

#define KNOWN 142u                                     /* the ids both builds know */
#define NPROJ 3u
static const uint32_t PJ[NPROJ] = {0u, 3u, 5u};
static uint32_t cp_rng = 4242u;
static uint32_t rnd(uint32_t n) { cp_rng = cp_rng * 1664525u + 1013904223u; return (cp_rng >> 8) % n; }
static uint8_t data[SEC_REC_MAX];
static void make(uint32_t n, uint32_t tag)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        data[i] = (uint8_t)(tag * 13u + i * 7u + (i >> 5));
}
static void scope(uint32_t p)
{
#if SLG_V2
    if (slg.proj != p)
        (void)slg_open_proj(p);
#else
    (void)p;
#endif
}
static int img_load(const char *path)
{
    FILE *f = fopen(path, "rb");
    size_t n;
    memset(nor, 0xFF, sizeof nor);
    if (!f)
        return -1;
    n = fread(nor + SEC_LOG_BASE, 1, SEC_LOG_SECTORS * SEC_SECT, f);
    fclose(f);
    if (n != SEC_LOG_SECTORS * SEC_SECT)
        return -1;
    slg_boot();
    return slg.up ? 0 : -1;
}
static int img_save(const char *path)
{
    FILE *f = fopen(path, "wb");
    size_t n;
    if (!f)
        return -1;
    n = fwrite(nor + SEC_LOG_BASE, 1, SEC_LOG_SECTORS * SEC_SECT, f);
    return fclose(f) || n != SEC_LOG_SECTORS * SEC_SECT ? -1 : 0;
}
/* every project's records of ids lo..hi-1, as the index reads them (each: project, id, length, the bytes) */
static int dump_ids(const char *path, uint32_t lo, uint32_t hi)
{
    static uint8_t b[SEC_REC_MAX];
    FILE *f = fopen(path, "wb");
    uint32_t j, id, recs = 0;
    if (!f)
        return -1;
    for (j = 0; j < (SLG_V2 ? NPROJ : 1u); j++) {
        scope(PJ[j]);
        for (id = lo; id < hi; id++) {
            int n = slg_get(id, b);
            uint8_t h[4] = {(uint8_t)PJ[j], (uint8_t)id, (uint8_t)(n > 0 ? n : 0), (uint8_t)(n > 0 ? n >> 8 : 0)};
            if (n < 0)
                return -1;
            fwrite(h, 1, 4, f);
            if (n > 0)
                fwrite(b, 1, (size_t)n, f), recs++;
        }
    }
    printf("%u records of ids %u..%u dumped\n", recs, lo, hi - 1u);
    return fclose(f) ? -1 : 0;
}
static int dump(const char *path) { return dump_ids(path, 0, KNOWN); }
static int gen(void)
{
    uint32_t k, j, id, n, midi = 0, live = 0;
    int rc;
    memset(nor, 0xFF, sizeof nor);
    slg_boot();
    for (k = 0; k < 900u; k++) {
        j = rnd(NPROJ), id = rnd(KNOWN);
        n = rnd(6) ? 8u + rnd(120) : 0u;
        make(n, k);
        scope(PJ[j]);
        rc = slg_put(id, data, n, (int)(k & 1u));     /* (every other one as the playing section's: the reserve too) */
        if (rc == 1)
            rc = slg_put(id, data, 0, 0);
        if (rc)
            return -1;
#if !SLG_PREV
        if (k % 3u == 0u) {                            /* the MIDI tracks' ids, as a later firmware writes them */
            id = SLG_ID_MPAT0 + rnd(SLG_IDS - SLG_ID_MPAT0);
            n = rnd(5) ? 10u + rnd(150) : 0u;
            make(n, 50000u + k);
            rc = slg_put(id, data, n, (int)(k & 1u));
            if (rc == 1)
                rc = slg_put(id, data, 0, 0);
            if (rc)
                return -1;
            midi++;
        }
#if SLG_V2
        if (k % 9u == 1u) {                            /* a live pattern (issue 142): its base stamp, then a pattern's bytes */
            uint32_t b = k * 7u;
            id = SEC_ID_PAT0 + rnd(SLG_LV_N);
            n = rnd(5) ? 12u + rnd(40) : 0u;
            make(n, 70000u + k);
            if (n) {
                memmove(data + 4, data, n);
                memcpy(data, &b, 4);
            }
            rc = slg_put(SLG_LVX(id), data, n ? n + 4u : 0u, 0);   /* (never the reserve: the log near full already) */
            if (rc == 1)
                rc = slg_put(SLG_LVX(id), data, 0, 0);
            if (rc)
                return -1;
            live++;
        }
#endif
#endif
        if (k % 23u == 0u)
            slg_boot();
    }
    printf("generated: 900 stores of ids 0..141 in %u projects, %u of the MIDI ids, %u live patterns; the gauge %u %%\n",
           SLG_V2 ? NPROJ : 1u, midi, live, slg_used_pct());
    return 0;
}
static int churn(void)
{
    static uint8_t b[SEC_REC_MAX];
    uint32_t r, j, id;
    for (r = 0; r < 3u; r++)
        for (j = 0; j < (SLG_V2 ? NPROJ : 1u); j++) {
            scope(PJ[j]);
            for (id = 0; id < KNOWN; id++) {
                int n = slg_get(id, b);
                if (n < 0 || (n > 0 && slg_put(id, b, (uint32_t)n, 1)))
                    return -1;
            }
        }
    slg_boot();
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 4 && !strcmp(argv[1], "gen"))
        return gen() || img_save(argv[2]) || dump(argv[3]);
    if (argc == 4 && !strcmp(argv[1], "load"))
        return img_load(argv[2]) || dump(argv[3]);
    if (argc == 5 && !strcmp(argv[1], "churn"))
        return img_load(argv[2]) || churn() || img_save(argv[3]) || dump(argv[4]);
#if !SLG_PREV
    if (argc == 4 && !strcmp(argv[1], "mdump"))
        return img_load(argv[2]) || dump_ids(argv[3], SLG_ID_MPAT0, SLG_IDS);
#if SLG_V2
    if (argc == 4 && !strcmp(argv[1], "ldump"))
        return img_load(argv[2]) || dump_ids(argv[3], SLG_LV0, SLG_IX);
#endif
#endif
    if (argc == 3 && !strcmp(argv[1], "pused")) {
        if (img_load(argv[2]))
            return 1;
#if SLG_V2
        printf("%u\n", (unsigned)slg.pused);
#endif
        return 0;
    }
    printf("usage: sec_log_compat_test gen IMG DUMP | load IMG DUMP | churn IMG OUT DUMP | mdump IMG DUMP\n");
    return 2;
}
