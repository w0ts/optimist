/* SPDX-License-Identifier: GPL-3.0-only */
/* Measurements for docs/PATTERNS-DESIGN.md (per-track patterns and scenes; nothing here is firmware). Built and run
 * by tools/patterns_measure/run.sh against the real project.c, sec_codec.c and sec_log.c (copies with SLG_IDS set
 * for 16 or 32 patterns a track, or to the 24 ids before phase 0):
 *   sizes  the structs a PATTERNS build would keep resident, the log index;
 *   codec  section records as codec A wrote them (every record before phase 0b) and as the firmware writes them now
 *          (codec B steps, phase 0b); prototype pattern records (the steps up to LEN in the firmware's codec A or B
 *          step form, LEN DIV SWING GATE, the motion) and scene records (the section body with no steps + 4
 *          pattern references); on the projects of tests/sec_projects.h (demo, busy, dense) and sec_codec_test's
 *          typical one;
 *   log    the real log on a simulated NOR: how many sections / scenes / patterns fit, the reserve kept.
 * Usage: patterns_measureN full | log | ids */
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
#if PM_IDS == 24
#include "sec_log_ids24.c"
#elif PM_IDS == 88
#include "sec_log_ids88.c"
#else
#include "sec_log_ids152.c"
#endif

/* ------------------------------------------------------------------ the prototype pattern record --- */
/* flags, LEN, DIV, SWG, GATE, [motion: count, count x (step, param, value)], a bitmap of the non-empty steps up to
 * LEN, then each such step: A its 10 bytes; B as sec_codec.c's codec B (sec_step_b) */
#define PAT_MOT 1u
#define PAT_B 2u
#define PAT_DRUM 4u
#define PAT_ON 8u                                    /* the motion plays (PLAY on) */
typedef struct { uint8_t step, param; int8_t value; } pev_t;
static uint32_t pat_encode(const proj_trk_t *t, uint32_t trk, const pev_t *ev, uint32_t nev, int codec_b, uint8_t *out)
{
    uint8_t *o = out + 5, *m;
    uint32_t len = sec_len(t), k;
    out[0] = (uint8_t)((nev ? PAT_MOT | PAT_ON : 0u) | (codec_b ? PAT_B : 0u) | (trk == TRK_DRUM ? PAT_DRUM : 0u));
    out[1] = (uint8_t)t->p[P_SLEN], out[2] = (uint8_t)t->p[P_SDIV], out[3] = (uint8_t)t->p[P_SSWING], out[4] = (uint8_t)t->p[P_SGATE];
    if (nev) {
        *o++ = (uint8_t)nev;
        for (k = 0; k < nev; k++)
            *o++ = ev[k].step, *o++ = ev[k].param, *o++ = (uint8_t)ev[k].value;
    }
    m = o, o += (len + 7u) / 8u;
    memset(m, 0, (len + 7u) / 8u);
    for (k = 0; k < len; k++) {
        if (sec_step_empty(&t->step[k], trk))
            continue;
        m[k >> 3] |= (uint8_t)(1u << (k & 7u));
        if (codec_b)
            o += sec_step_b((const uint8_t *)&t->step[k], o);
        else
            memcpy(o, &t->step[k], 10), o += 10;
    }
    return (uint32_t)(o - out);
}
/* decode B back (the round trip proves the format holds everything) */
static int pat_decode(const uint8_t *a, uint32_t n, proj_trk_t *t, uint32_t trk, pev_t *ev, uint32_t *nev)
{
    const uint8_t *e = a + n, *m;
    uint32_t len, k;
    if (n < 5u)
        return 0;
    t->p[P_SLEN] = a[1], t->p[P_SDIV] = a[2], t->p[P_SSWING] = a[3], t->p[P_SGATE] = a[4];
    len = sec_len(t);
    a += 5;
    *nev = 0;
    if (a[-5] & PAT_MOT) {
        *nev = *a++;
        for (k = 0; k < *nev; k++)
            ev[k].step = a[0], ev[k].param = a[1], ev[k].value = (int8_t)a[2], a += 3;
    }
    m = a, a += (len + 7u) / 8u;
    for (k = 0; k < NSTEP; k++) {
        sec_step_clear(&t->step[k], trk);
        if (k < len && ((m[k >> 3] >> (k & 7u)) & 1u) && !sec_unstep_b(&a, e, (uint8_t *)&t->step[k]))
            return 0;
    }
    return a == e;
}

/* ------------------------------------------------------------------ projects --- */
static project_t P;
static dlrec_t D;
static void typical(void)                            /* tests/sec_codec_test.c's typical section */
{
    uint32_t i, k;
    host_tracks_init();
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        uint32_t every = i == TRK_DRUM ? 1u : 2u, base = 48u + 5u * i;
        t->p[P_SLEN] = 16;
        for (k = 0; k < 16u; k += every) {
            uint8_t n = (uint8_t)(base + (k * 7u) % 12u);
            if (is_drum(t))
                dstep_set(&t->dstep[k], k % 4u == 0u ? 0u : k % 4u == 2u ? 2u : 4u, LV_NORM, 0);
            else
                put_step(t, k, 1, &n, ST_NOTE, 0);
        }
    }
    trk[0].p[P_E0 + 2] = 77, trk[1].p[P_REV] = 40, trk[2].p[P_DLY] = 30, trk[1].p[P_ATK] = 12;
}
static void motion_fill(pev_t *ev, uint32_t n, uint32_t len)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        ev[i].step = (uint8_t)(i % len), ev[i].param = (uint8_t)(P_ED_FLT + i / len % 3u), ev[i].value = (int8_t)(i * 3u);
}

/* the section record of the working project (codec A: as before phase 0b; else as the firmware writes it), with m
 * events of motion (sec_codec.c's chunk) */
static uint8_t rec[SEC_REC_MAX + 64], rec2[SEC_REC_MAX + 64];
static uint32_t section_len(uint32_t mev, int codec_a)
{
    uint32_t n;
    proj_capture(&P, &D);
    sec_test_a = codec_a;
    n = sec_body(&P, &D, rec);
    sec_test_a = 0;
    if (mev) {
        motion_store_t m;
        uint32_t i;
        memset(&m, 0, sizeof m);
        m.count = (uint8_t)mev, m.on = 15;
        for (i = 0; i < mev; i++)
            m.ev[i].place = (uint8_t)((i % 4u) << 6 | (i / 4u)), m.ev[i].param = P_ED_FLT, m.ev[i].value = (int8_t)i;
        n = sec_put_motion(rec, n, &P, &D, &m);
    }
    return n;
}
/* the scene record: today's body with no steps (LEN DIV SWG GATE at their defaults: they are the patterns'), less
 * the 2-byte step bitmap each track's default LEN 16 leaves, + one pattern reference a track */
static uint32_t scene_len(int *raw)
{
    project_t s;
    uint32_t i, k, n;
    proj_capture(&P, &D);
    s = P;
    for (i = 0; i < NTRK; i++) {
        for (k = 0; k < NSTEP; k++)
            sec_step_clear(&s.t[i].step[k], i);
        s.t[i].p[P_SLEN] = TP[P_SLEN].def, s.t[i].p[P_SDIV] = TP[P_SDIV].def;
        s.t[i].p[P_SSWING] = TP[P_SSWING].def, s.t[i].p[P_SGATE] = TP[P_SGATE].def;
    }
    s.sum = proj_sum(&s);
    n = sec_body(&s, &D, rec);
    *raw = sec_is_raw(rec[0]);
    return n - NTRK * ((TP[P_SLEN].def + 7u) / 8u) + NTRK;
}
static uint32_t pat_len(uint32_t i, uint32_t mev, int b, int *rt)
{
    pev_t ev[64], ev2[64];
    proj_trk_t back;
    uint32_t n, n2;
    proj_capture(&P, &D);
    motion_fill(ev, mev, sec_len(&P.t[i]));
    n = pat_encode(&P.t[i], i, ev, mev, b, rec);
    if (b == 2) {                                    /* AB: the smaller of the two (the flag says which) */
        uint32_t na = pat_encode(&P.t[i], i, ev, mev, 0, rec2);
        if (na < n) {
            memcpy(rec, rec2, na);
            n = na;
        }
        return n;
    }
    if (b && rt) {
        memset(&back, 0, sizeof back);
        *rt = pat_decode(rec, n, &back, i, ev2, &n2) && n2 == mev && !memcmp(ev, ev2, 3u * mev);
        for (n2 = 0; *rt && n2 < sec_len(&P.t[i]); n2++)
            *rt = !memcmp(&back.step[n2], &P.t[i].step[n2], 10);
    }
    return n;
}

static const char *const TRKN[NTRK] = {"T1", "T2", "T3", "DR"};
static uint32_t pat_max, scene_max;                  /* the worst cases (the reserve) */
static int quiet;
static void codec_report(const char *name, void (*make)(void))
{
    uint32_t i, a, b, b16, b64, s, n0, n0a, n64, sum = 0;
    int raw, rt = 1, r;
    make();
    n0a = section_len(0, 1);
    n0 = section_len(0, 0);
    n64 = section_len(64, 0);
    s = scene_len(&raw);
    if (!quiet)
        printf("\n%s: section codec A %u B, codec B %u B (with 64 motion events %u B); scene %u B%s\n", name, n0a, n0, n64, s,
               raw ? " (RAW!)" : "");
    if (s > scene_max)
        scene_max = s;
    if (!quiet)
        printf("  track  LEN  pat A  pat B  pat AB  AB+16ev  AB+64ev\n");
    for (i = 0; i < NTRK; i++) {
        uint32_t ab;
        a = pat_len(i, 0, 0, 0);
        b = pat_len(i, 0, 1, &r);
        rt &= r;
        (void)pat_len(i, 16, 1, &r);
        rt &= r;
        (void)pat_len(i, 64, 1, &r);
        rt &= r;
        ab = pat_len(i, 0, 2, 0);
        b16 = pat_len(i, 16, 2, 0);
        b64 = pat_len(i, 64, 2, 0);
        if (b64 > pat_max)
            pat_max = b64;
        sum += ab;
        if (!quiet)
            printf("  %-5s %4d  %5u  %5u  %6u  %7u  %7u\n", TRKN[i], P.t[i].p[P_SLEN], a, b, ab, b16, b64);
    }
    if (!quiet)
        printf("  scene + its 4 patterns (AB, no motion, with the log's record heads): %u B; the section: codec A %u B, B %u B "
               "(with its head); pattern codec B round trips: %s\n",
               s + sum + 5u * SEC_HEAD, n0a + SEC_HEAD, n0 + SEC_HEAD, rt ? "ok" : "FAIL");
}

/* ------------------------------------------------------------------ the log --- */
static uint8_t data[SEC_REC_MAX];
#define PM_PER ((SLG_IDS - 24u) / NTRK)              /* patterns a track */
#define ID_PAT(k, c) (24u + PM_PER * (k) + (c))
/* a PATTERNS build's reserve: after this store, the playing scene (scene_max) and four new patterns (pat_max) fit */
static int pm_reserve(uint32_t id, uint32_t len)
{
    slg_model_t m;
    uint32_t k;
    sm_init(&m);
    if (!sm_put(&m, id, len) || !sm_put(&m, SLG_IDS, scene_max))
        return 0;
    for (k = 0; k < NTRK; k++)
        if (!sm_put(&m, SLG_IDS, pat_max))
            return 0;
    return 1;
}
static int put(uint32_t id, uint32_t len, int pats)
{
    memset(data, (int)(id * 7u + 1u), len);
    if (pats ? !pm_reserve(id, len) : 0)
        return 1;
    return slg_put(id, data, len, pats);            /* (pats: pm_reserve was the check; else slg_put's own) */
}
static void log_reset(void)
{
    memset(nor, 0xFF, sizeof nor);
    slg_boot();
}
static uint32_t fill_sections(uint32_t n)           /* 16 sections of n bytes, the reserve kept; -> how many fit */
{
    uint32_t s;
    log_reset();
    for (s = 0; s < 16u && put(s, n, 0) == 0; s++)
        ;
    return s;
}

/* store the 16 scenes, each with its own four patterns (the special case: today's sections); -> scenes stored */
static uint32_t fill_own(uint32_t sc, const uint32_t *cl)
{
    uint32_t s, k;
    log_reset();
    for (s = 0; s < 16u && s < PM_PER; s++) {
        for (k = 0; k < NTRK; k++)
            if (put(ID_PAT(k, s), cl[k], 1))
                return s;
        if (put(s, sc, 1))
            return s;
    }
    return s;
}
/* 16 scenes of sc bytes, then every pattern slot (cl[k] bytes) until MEM FULL; -> patterns stored */
static uint32_t fill_slots(uint32_t sc, const uint32_t *cl)
{
    uint32_t s, k, c, n = 0;
    log_reset();
    for (s = 0; s < 16u; s++)
        put(s, sc, 1);
    for (c = 0; c < PM_PER; c++)
        for (k = 0; k < NTRK; k++) {
            if (put(ID_PAT(k, c), cl[k], 1))
                return n;
            n++;
        }
    return n;
}
static void log_report(void)
{
    uint32_t n, k, typ_c[NTRK], dem_c[NTRK], dem_s, busy_c[NTRK], busy0_c[NTRK], busy_s, worst[NTRK];
    uint32_t typ_a, typ_b, dem_a, dem_b, busy_a, busy_b;
    int raw;
    typical();
    typ_a = section_len(0, 1), typ_b = section_len(0, 0);
    for (k = 0; k < NTRK; k++)
        typ_c[k] = pat_len(k, 0, 2, 0);
    sp_demo_sounds();
    dem_a = section_len(0, 1), dem_b = section_len(0, 0);
    dem_s = scene_len(&raw);
    for (k = 0; k < NTRK; k++)
        dem_c[k] = pat_len(k, 0, 2, 0);
    sp_busy();
    busy_a = section_len(0, 1), busy_b = section_len(0, 0);
    busy_s = scene_len(&raw);
    for (k = 0; k < NTRK; k++)
        busy_c[k] = pat_len(k, 16, 2, 0), busy0_c[k] = pat_len(k, 0, 2, 0), worst[k] = pat_max;
    printf("\nlog of %u sectors (%u KiB), SLG_IDS %u; reserve: today one record of %u B; patterns: a scene of %u B + 4 patterns of %u B\n",
           SEC_LOG_SECTORS, SEC_LOG_SECTORS * 4u, SLG_IDS, SEC_REC_MAX, scene_max, pat_max);
    printf("  sections, codec A (before phase 0b): typical (%u B) %u of 16; demo (%u B) %u of 16; busy 64-step (%u B) %u of 16\n",
           typ_a, fill_sections(typ_a), dem_a, fill_sections(dem_a), busy_a, fill_sections(busy_a));
    printf("  sections, codec B (phase 0b):        typical (%u B) %u of 16; demo (%u B) %u of 16; busy 64-step (%u B) %u of 16\n",
           typ_b, fill_sections(typ_b), dem_b, fill_sections(dem_b), busy_b, fill_sections(busy_b));
    {   /* how large a section may be for 16 to fit (the reserve kept) */
        uint32_t lo = 100u, hi = SEC_REC_MAX;
        while (lo < hi) {
            uint32_t mid = (lo + hi + 1u) / 2u;
            if (fill_sections(mid) == 16u)
                lo = mid;
            else
                hi = mid - 1u;
        }
        printf("  16 sections fit up to %u B each (with the reserve of one %u B record)\n", lo, SEC_REC_MAX);
    }
    if (SLG_IDS < 24u + 16u * NTRK) {
        printf("  (patterns: need their ids)\n");
        return;
    }
    printf("  patterns, each scene its own 4: demo %u of 16 scenes; busy %u of 16; busy with 16 motion events a pattern %u of 16\n",
           fill_own(dem_s, dem_c), fill_own(busy_s, busy0_c), fill_own(busy_s, busy_c));
    n = fill_slots(dem_s, typ_c);
    printf("  16 demo scenes + every slot (%u) a typical 16-step pattern (%u %u %u %u B): %u patterns (live %u B of %u)\n", NTRK * PM_PER,
           typ_c[0], typ_c[1], typ_c[2], typ_c[3], n, slg_live_bytes(), SEC_ROOM);
    n = fill_slots(dem_s, dem_c);
    printf("  16 demo scenes + every slot a demo pattern (%u %u %u %u B): %u patterns\n", dem_c[0], dem_c[1], dem_c[2], dem_c[3], n);
    n = fill_slots(busy_s, busy_c);
    printf("  16 busy scenes + every slot a busy 64-step pattern with 16 events (%u %u %u %u B): %u patterns\n", busy_c[0], busy_c[1],
           busy_c[2], busy_c[3], n);
    n = fill_slots(busy_s, worst);
    printf("  16 busy scenes + every slot a worst-case pattern (%u B: 64 dense steps, 64 events): %u patterns\n", pat_max, n);
    {   /* a song as it is made: 16 busy scenes sharing patterns (drums 4, bass 6, chords 4, lead 8) */
        static const uint8_t USE[NTRK] = {6, 4, 8, 4};
        uint32_t s, c, ok = 1;
        log_reset();
        for (k = 0; k < NTRK; k++)
            for (c = 0; c < USE[k]; c++)
                ok &= put(ID_PAT(k, c), busy_c[k], 1) == 0;
        for (s = 0; s < 16u; s++)
            ok &= put(s, busy_s, 1) == 0;
        printf("  a shared song: 16 busy scenes over 22 busy patterns (bass 6, chords 4, lead 8, drums 4): %s, live %u B (%u %%)\n",
               ok ? "stored" : "MEM FULL", slg_live_bytes(), slg_live_bytes() * 100u / SEC_ROOM);
    }
}

static void ids_report(void)
{
    printf("SLG_IDS %u: the log's index (slg) %u B, its model (slg_model_t, on the stack, x3 in sm_more) %u B\n",
           SLG_IDS, (uint32_t)sizeof slg, (uint32_t)sizeof(slg_model_t));
}

int main(int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : "full";
    quiet = !strcmp(mode, "log");
    if (!strcmp(mode, "ids")) {
        ids_report();
        return 0;
    }
    if (!quiet)
        printf("sizes: step_t %u, track_t %u, trk[] %u, project_t %u, proj_trk_t %u, dlrec_t %u, motion_store_t %u, motion_ev_t %u\n",
               (uint32_t)sizeof(step_t), (uint32_t)sizeof(track_t), (uint32_t)sizeof trk, (uint32_t)sizeof(project_t),
               (uint32_t)sizeof(proj_trk_t), (uint32_t)sizeof(dlrec_t), (uint32_t)sizeof(motion_store_t), (uint32_t)sizeof(motion_ev_t));
    if (!quiet)
        printf("       SEC_REC_MAX %u, SEC_RAW_N %u, PJ_NP %u, P_COUNT %u, G_COUNT %u\n", SEC_REC_MAX, SEC_RAW_N, PJ_NP, P_COUNT, G_COUNT);
    {   /* the proposed resident state of a PATTERNS build (docs/PATTERNS-DESIGN.md section 4) */
        typedef struct { step_t st[NSTEP]; int16_t pp[4]; uint8_t nev, on, slot, ready; pev_t ev[64]; } pat_stage_t;
        typedef struct { uint32_t org; uint8_t cur, req, when, mod; } pat_trk_t;
        if (!quiet)
            printf("       pattern stage (one track) %u B, x4 %u B; pattern state per track %u B; scene refs 16x4 %u B\n",
                   (uint32_t)sizeof(pat_stage_t), 4u * (uint32_t)sizeof(pat_stage_t), (uint32_t)sizeof(pat_trk_t), 16u * NTRK);
    }
    if (!quiet)
        ids_report();
    codec_report("power-on", host_tracks_init);
    codec_report("demo (tracks_demo: acid 16, chords 32, lead 12, drums 16; factory presets)", sp_demo_sounds);
    codec_report("typical (sec_codec_test: 16 steps a track, a few edits)", typical);
    codec_report("busy (64 steps everywhere: 16ths bass, held chords, 8ths lead, full drums)", sp_busy);
    codec_report("dense worst case (64 random full steps a track)", sp_dense);
    {   /* the scene body's bound: flags, globals' bitmap + all 32, sel + rsv, 4 tracks (engine, preset, bitmap, every value),
         * three FM6 voices, the drum key + record, 4 references (no steps: no raw fallback is ever needed) */
        uint32_t w = 1u + 4u + 2u * 32u + 4u + NTRK * (2u + (PJ_NP + 7u) / 8u + 2u * PJ_NP) + 1u + NPART * (128u + 1u + 16u) + 4u +
                     (uint32_t)sizeof(dlrec_t) + NTRK;
        if (!quiet)
            printf("\nscene bound (every value moved, 3 FM6 voices, a drum record) %u B\n", w);
        if (w > scene_max)
            scene_max = w;
    }
    if (!quiet)
        printf("\nworst cases: pattern %u B (<= SEC_REC_MAX %u: one log record), scene %u B\n", pat_max, SEC_REC_MAX, scene_max);
    if (!strcmp(mode, "full") || !strcmp(mode, "log"))
        log_report();
    return 0;
}
