/* SPDX-License-Identifier: GPL-3.0-only */
/* Measurements for docs/CLIPS-DESIGN.md (per-track clips and scenes; nothing here is firmware). Built and run by
 * tools/clips_measure/run.sh against the real project.c, sec_codec.c and sec_log.c (a copy with SLG_IDS raised):
 *   sizes  the structs a clip build would keep resident, today's and the proposed log index;
 *   codec  clip records (prototype codecs A: 10-byte steps as sec_codec.c keeps them, B: a 10-bit mask of a
 *          step's non-zero bytes, then those bytes), scene records (sec_codec.c's body with no steps, + 4 clip
 *          references), today's section records; on the demo project (tests/hostsim.c tracks_demo's patterns on
 *          factory presets), sec_codec_test's typical project, a busy 64-step project and the dense worst case;
 *   log    the real log on a simulated NOR: how many sections / scenes / clips fit, with the clip reserve.
 * Usage: clips_measureN full | log | ids */
#define main hostsim_main
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
static uint32_t trk_def_engine(uint32_t i) { return i < NPART ? (const uint8_t[]){0, 1, 4}[i] : 0u; }
#include "../firmware/src/project.c"
#include "../firmware/src/sec_codec.c"

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
#include "../firmware/src/storage.c"
#if CM_IDS == 24
#include "sec_log_ids24.c"
#elif CM_IDS == 88
#include "sec_log_ids88.c"
#else
#include "sec_log_ids152.c"
#endif

/* ------------------------------------------------------------------ the prototype clip record --- */
/* flags, LEN, DIV, SWG, GATE, [motion: count, count x (step, param, value)], a bitmap of the non-empty steps up to
 * LEN, then each such step: A its 10 bytes; B a 2-byte mask of its non-zero bytes, then those bytes */
#define CLP_MOT 1u
#define CLP_B 2u
#define CLP_DRUM 4u
#define CLP_ON 8u                                   /* the motion plays (PLAY on) */
typedef struct { uint8_t step, param; int8_t value; } cev_t;
static uint32_t clip_encode(const proj_trk_t *t, uint32_t trk, const cev_t *ev, uint32_t nev, int codec_b, uint8_t *out)
{
    uint8_t *o = out + 5, *m;
    uint32_t len = sec_len(t), k, j;
    out[0] = (uint8_t)((nev ? CLP_MOT | CLP_ON : 0u) | (codec_b ? CLP_B : 0u) | (trk == TRK_DRUM ? CLP_DRUM : 0u));
    out[1] = (uint8_t)t->p[P_SLEN], out[2] = (uint8_t)t->p[P_SDIV], out[3] = (uint8_t)t->p[P_SSWING], out[4] = (uint8_t)t->p[P_SGATE];
    if (nev) {
        *o++ = (uint8_t)nev;
        for (k = 0; k < nev; k++)
            *o++ = ev[k].step, *o++ = ev[k].param, *o++ = (uint8_t)ev[k].value;
    }
    m = o, o += (len + 7u) / 8u;
    memset(m, 0, (len + 7u) / 8u);
    for (k = 0; k < len; k++) {
        const uint8_t *s = (const uint8_t *)&t->step[k];
        if (sec_step_empty(&t->step[k], trk))
            continue;
        m[k >> 3] |= (uint8_t)(1u << (k & 7u));
        if (!codec_b) {
            memcpy(o, s, 10), o += 10;
        } else {
            uint32_t mask = 0;
            for (j = 0; j < 10u; j++)
                mask |= (uint32_t)(s[j] != 0) << j;
            *o++ = (uint8_t)mask, *o++ = (uint8_t)(mask >> 8);
            for (j = 0; j < 10u; j++)
                if (s[j])
                    *o++ = s[j];
        }
    }
    return (uint32_t)(o - out);
}
/* decode B back (the round trip proves the format holds everything) */
static int clip_decode(const uint8_t *a, uint32_t n, proj_trk_t *t, uint32_t trk, cev_t *ev, uint32_t *nev)
{
    const uint8_t *e = a + n, *m;
    uint32_t len, k, j;
    if (n < 5u)
        return 0;
    t->p[P_SLEN] = a[1], t->p[P_SDIV] = a[2], t->p[P_SSWING] = a[3], t->p[P_SGATE] = a[4];
    len = sec_len(t);
    a += 5;
    *nev = 0;
    if (a[-5] & CLP_MOT) {
        *nev = *a++;
        for (k = 0; k < *nev; k++)
            ev[k].step = a[0], ev[k].param = a[1], ev[k].value = (int8_t)a[2], a += 3;
    }
    m = a, a += (len + 7u) / 8u;
    for (k = 0; k < NSTEP; k++) {
        uint8_t *s = (uint8_t *)&t->step[k];
        sec_step_clear(&t->step[k], trk);
        if (k >= len || !((m[k >> 3] >> (k & 7u)) & 1u))
            continue;
        if (e - a < 2)
            return 0;
        {
            uint32_t mask = a[0] | (uint32_t)a[1] << 8;
            a += 2;
            memset(s, 0, 10);
            for (j = 0; j < 10u; j++)
                if ((mask >> j) & 1u)
                    s[j] = *a++;
        }
    }
    return a == e;
}

/* ------------------------------------------------------------------ projects --- */
static project_t P;
static dlrec_t D;
static uint32_t cm_rng = 4242u;
static uint32_t rnd(uint32_t n) { cm_rng = cm_rng * 1664525u + 1013904223u; return (cm_rng >> 8) % n; }

static void demo(void)                               /* tests/hostsim.c tracks_demo: acid 16, chords 32, lead 12, drums 16 */
{
    static const uint8_t ACID[16] = {45, 45, 57, 45, 0, 48, 45, 55, 45, 0, 57, 52, 45, 48, 0, 50};
    static const uint8_t ACIDF[16] = {1, 0, 2, 0, 0, 0, 1, 2, 0, 0, 1, 0, 0, 2, 0, 1};
    static const uint8_t AM[4] = {57, 60, 64, 67}, FM[4] = {53, 57, 60, 64};
    static const uint8_t LEAD[12] = {76, 0, 0, 79, 0, 0, 81, 0, 79, 0, 76, 0};
    track_t *t1 = &trk[0], *t2 = &trk[1], *t3 = &trk[2], *td = TDRUM;
    uint32_t i;
    host_tracks_init();
    host_preset(t1, 0, 4);
    host_preset(t2, 1, 5);
    host_preset(t3, 3, 0);
    for (i = 0; i < 16u; i++) {
        uint8_t n = ACID[i];
        put_step(t1, i, n ? 1u : 0u, &n, n ? ST_NOTE : ST_REST, ACIDF[i]);
    }
    t2->p[P_SLEN] = 32;
    t2->p[P_SGATE] = 120;
    for (i = 0; i < 32u; i++)
        put_step(t2, i, i % 16u == 0u ? 4u : 0u, i < 16u ? AM : FM, i % 16u == 0u ? ST_NOTE : i % 16u < 14u ? ST_TIE : ST_REST, 0);
    t3->p[P_SLEN] = 12;
    for (i = 0; i < 12u; i++) {
        uint8_t n = LEAD[i];
        put_step(t3, i, n ? 1u : 0u, &n, n ? ST_NOTE : ST_REST, i == 0u ? SF_ACCENT : 0u);
    }
    for (i = 0; i < 16u; i++) {
        uint8_t n[4];
        uint32_t k = 0;
        if (i % 4u == 0u)
            n[k++] = 36;
        if (i == 4u || i == 12u)
            n[k++] = 38;
        if (i % 2u == 0u)
            n[k++] = i == 14u ? 46 : 42;
        put_step(td, i, k, n, k ? ST_NOTE : ST_REST, i % 4u == 0u ? SF_ACCENT : 0u);
    }
}
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
static void busy(void)                               /* 64 steps everywhere, played densely (a "full" real loop) */
{
    uint32_t i;
    demo();
    for (i = 0; i < NTRK; i++)
        trk[i].p[P_SLEN] = 64;
    for (i = 0; i < 64u; i++) {
        uint8_t b = (uint8_t)(33u + (i * 5u) % 12u), lead = (uint8_t)(72u + (i * 7u) % 12u), d[4], k = 0;
        put_step(&trk[0], i, 1, &b, ST_NOTE, i % 3u == 0u ? SF_ACCENT : 0u);                 /* 16ths bass */
        put_step(&trk[1], i, i % 8u == 0u ? 4u : 0u, i % 16u < 8u ? (const uint8_t[]){57, 60, 64, 67} : (const uint8_t[]){53, 57, 60, 64},
                 i % 8u == 0u ? ST_NOTE : i % 8u < 6u ? ST_TIE : ST_REST, 0);                  /* chords, held */
        put_step(&trk[2], i, i % 2u == 0u ? 1u : 0u, &lead, i % 2u == 0u ? ST_NOTE : ST_REST, 0); /* 8ths lead */
        if (i % 4u == 0u) d[k++] = 36;
        if (i % 8u == 4u) d[k++] = 38;
        d[k++] = i % 4u == 2u ? 46 : 42;
        if (i % 16u == 15u) d[k++] = 39;
        put_step(TDRUM, i, k, d, ST_NOTE, i % 4u == 0u ? SF_ACCENT : 0u);
        trk[0].step[i].lvl = (uint8_t)(i % 4u), trk[0].step[i].rat = (uint8_t)(i % 8u == 7u);
        TDRUM->dstep[i].lvl[0] = (uint8_t)(i % 3u);                                          /* some dynamics */
    }
}
static void dense(void)                              /* every byte of 64 steps x 4 random, every value moved */
{
    uint32_t i, k, n;
    host_tracks_init();
    proj_capture(&P, &D);
    for (i = 0; i < NTRK; i++) {
        trk[i].p[P_SLEN] = NSTEP;
        trk[i].p[P_SDIV] = 3, trk[i].p[P_SSWING] = 40, trk[i].p[P_SGATE] = 99;
        for (k = 0; k < NSTEP; k++)
            for (n = 0; n < 10u; n++)
                ((uint8_t *)&trk[i].step[k])[n] = (uint8_t)(1u + rnd(200));
    }
}
static void motion_fill(cev_t *ev, uint32_t n, uint32_t len)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        ev[i].step = (uint8_t)(i % len), ev[i].param = (uint8_t)(P_ED_FLT + i / len % 3u), ev[i].value = (int8_t)(i * 3u);
}

/* today's section record of the working project (with m events of motion, sec_codec.c's chunk) */
static uint8_t rec[SEC_REC_MAX + 64], rec2[SEC_REC_MAX + 64];
static uint32_t section_len(uint32_t mev)
{
    uint32_t n;
    proj_capture(&P, &D);
    n = sec_body(&P, &D, rec);
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
/* the scene record: today's body with no steps (LEN DIV SWG GATE at their defaults: they are the clips'), less
 * the 2-byte step bitmap each track's default LEN 16 leaves, + one clip reference a track */
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
    *raw = (rec[0] & SEC_RAW) != 0;
    return n - NTRK * ((TP[P_SLEN].def + 7u) / 8u) + NTRK;
}
static uint32_t clip_len(uint32_t i, uint32_t mev, int b, int *rt)
{
    cev_t ev[64], ev2[64];
    proj_trk_t back;
    uint32_t n, n2;
    proj_capture(&P, &D);
    motion_fill(ev, mev, sec_len(&P.t[i]));
    n = clip_encode(&P.t[i], i, ev, mev, b, rec);
    if (b == 2) {                                    /* AB: the smaller of the two (the flag says which) */
        uint32_t na = clip_encode(&P.t[i], i, ev, mev, 0, rec2);
        if (na < n) {
            memcpy(rec, rec2, na);
            n = na;
        }
        return n;
    }
    if (b && rt) {
        memset(&back, 0, sizeof back);
        *rt = clip_decode(rec, n, &back, i, ev2, &n2) && n2 == mev && !memcmp(ev, ev2, 3u * mev);
        for (n2 = 0; *rt && n2 < sec_len(&P.t[i]); n2++)
            *rt = !memcmp(&back.step[n2], &P.t[i].step[n2], 10);
    }
    return n;
}

static const char *const TRKN[NTRK] = {"T1", "T2", "T3", "DR"};
static uint32_t clip_max, scene_max;                 /* the worst cases (the reserve) */
static int quiet;
static void codec_report(const char *name, void (*make)(void))
{
    uint32_t i, a, b, b16, b64, s, n0, n64, sum = 0;
    int raw, rt = 1, r;
    make();
    n0 = section_len(0);
    n64 = section_len(64);
    s = scene_len(&raw);
    if (!quiet)
        printf("\n%s: today's section %u B (with 64 motion events %u B); scene %u B%s\n", name, n0, n64, s, raw ? " (RAW!)" : "");
    if (s > scene_max)
        scene_max = s;
    if (!quiet)
        printf("  track  LEN  clip A  clip B  clip AB  AB+16ev  AB+64ev\n");
    for (i = 0; i < NTRK; i++) {
        uint32_t ab;
        a = clip_len(i, 0, 0, 0);
        b = clip_len(i, 0, 1, &r);
        rt &= r;
        (void)clip_len(i, 16, 1, &r);
        rt &= r;
        (void)clip_len(i, 64, 1, &r);
        rt &= r;
        ab = clip_len(i, 0, 2, 0);
        b16 = clip_len(i, 16, 2, 0);
        b64 = clip_len(i, 64, 2, 0);
        if (b64 > clip_max)
            clip_max = b64;
        sum += ab;
        if (!quiet)
            printf("  %-5s %4d  %6u  %6u  %7u  %7u  %7u\n", TRKN[i], P.t[i].p[P_SLEN], a, b, ab, b16, b64);
    }
    if (!quiet)
        printf("  scene + its 4 clips (AB, no motion, with the log's record heads): %u B, against the section %u B; codec B round trips: %s\n",
               s + sum + 5u * SEC_HEAD, n0 + SEC_HEAD, rt ? "ok" : "FAIL");
}

/* ------------------------------------------------------------------ the log --- */
static uint8_t data[SEC_REC_MAX];
#define CM_PER ((SLG_IDS - 24u) / NTRK)              /* clips a track */
#define ID_CLIP(k, c) (24u + CM_PER * (k) + (c))
/* a clip build's reserve: after this store, the playing scene (scene_max) and four new clips (clip_max) still fit */
static int cm_reserve(uint32_t id, uint32_t len)
{
    slg_model_t m;
    uint32_t k;
    sm_init(&m);
    if (!sm_put(&m, id, len) || !sm_put(&m, SLG_IDS, scene_max))
        return 0;
    for (k = 0; k < NTRK; k++)
        if (!sm_put(&m, SLG_IDS, clip_max))
            return 0;
    return 1;
}
static int put(uint32_t id, uint32_t len, int clips)
{
    memset(data, (int)(id * 7u + 1u), len);
    if (clips ? !cm_reserve(id, len) : 0)
        return 1;
    return slg_put(id, data, len, clips);           /* (clips: cm_reserve was the check; else slg_put's own) */
}
static void log_reset(void)
{
    memset(nor, 0xFF, sizeof nor);
    slg_boot();
}

/* store the 16 scenes, each with its own four clips (the special case: today's sections); -> scenes stored */
static uint32_t fill_own(uint32_t sc, const uint32_t *cl)
{
    uint32_t s, k;
    log_reset();
    for (s = 0; s < 16u && s < CM_PER; s++) {
        for (k = 0; k < NTRK; k++)
            if (put(ID_CLIP(k, s), cl[k], 1))
                return s;
        if (put(s, sc, 1))
            return s;
    }
    return s;
}
/* 16 scenes of sc bytes, then every clip slot (cl[k] bytes) until MEM FULL; -> clips stored */
static uint32_t fill_slots(uint32_t sc, const uint32_t *cl)
{
    uint32_t s, k, c, n = 0;
    log_reset();
    for (s = 0; s < 16u; s++)
        put(s, sc, 1);
    for (c = 0; c < CM_PER; c++)
        for (k = 0; k < NTRK; k++) {
            if (put(ID_CLIP(k, c), cl[k], 1))
                return n;
            n++;
        }
    return n;
}
static void log_report(void)
{
    uint32_t n, k, typ_c[NTRK], dem_c[NTRK], dem_s, busy_c[NTRK], busy0_c[NTRK], busy_s, sect_typ, sect_busy, sect_dem, worst[NTRK];
    int raw;
    if (SLG_IDS < 24u + 16u * NTRK) {
        printf("(log: needs the clip ids)\n");
        return;
    }
    typical();
    sect_typ = section_len(0);
    for (k = 0; k < NTRK; k++)
        typ_c[k] = clip_len(k, 0, 2, 0);
    demo();
    sect_dem = section_len(0);
    dem_s = scene_len(&raw);
    for (k = 0; k < NTRK; k++)
        dem_c[k] = clip_len(k, 0, 2, 0);
    busy();
    sect_busy = section_len(0);
    busy_s = scene_len(&raw);
    for (k = 0; k < NTRK; k++)
        busy_c[k] = clip_len(k, 16, 2, 0), busy0_c[k] = clip_len(k, 0, 2, 0), worst[k] = clip_max;
    printf("\nlog of %u sectors (%u KiB), SLG_IDS %u (%u clips a track); reserve: today one record of %u B; clips: a scene of %u B + 4 clips of %u B\n",
           SEC_LOG_SECTORS, SEC_LOG_SECTORS * 4u, SLG_IDS, CM_PER, SEC_REC_MAX, scene_max, clip_max);
    log_reset();
    for (n = 0; n < 16u && put(n, sect_typ, 0) == 0; n++)
        ;
    printf("  today: typical sections (%u B) %u of 16;", sect_typ, n);
    log_reset();
    for (n = 0; n < 16u && put(n, sect_dem, 0) == 0; n++)
        ;
    printf(" demo sections (%u B) %u of 16;", sect_dem, n);
    log_reset();
    for (n = 0; n < 16u && put(n, sect_busy, 0) == 0; n++)
        ;
    printf(" busy 64-step sections (%u B) %u of 16\n", sect_busy, n);
    {   /* sections whose steps use codec B (no clips): about the scene + its four AB clips (headers counted once) */
        uint32_t sb = busy_s + busy0_c[0] + busy0_c[1] + busy0_c[2] + busy0_c[3] - 4u * 5u + 4u * 8u;
        log_reset();
        for (n = 0; n < 16u && put(n, sb, 0) == 0; n++)
            ;
        printf("  sections with codec B steps (no clips; busy ~%u B, estimated from the parts): %u of 16\n", sb, n);
    }
    printf("  clips, each scene its own 4 clips: demo %u of 16 scenes; busy %u of 16; busy with 16 motion events a clip %u of 16\n",
           fill_own(dem_s, dem_c), fill_own(busy_s, busy0_c), fill_own(busy_s, busy_c));
    n = fill_slots(dem_s, typ_c);
    printf("  16 demo scenes + every slot (%u) a typical 16-step clip (%u %u %u %u B): %u clips (live %u B of %u)\n", NTRK * CM_PER,
           typ_c[0], typ_c[1], typ_c[2], typ_c[3], n, slg_live_bytes(), SEC_ROOM);
    n = fill_slots(dem_s, dem_c);
    printf("  16 demo scenes + every slot a demo clip (%u %u %u %u B): %u clips\n", dem_c[0], dem_c[1], dem_c[2], dem_c[3], n);
    n = fill_slots(busy_s, busy_c);
    printf("  16 busy scenes + every slot a busy 64-step clip with 16 events (%u %u %u %u B): %u clips\n", busy_c[0], busy_c[1],
           busy_c[2], busy_c[3], n);
    n = fill_slots(busy_s, worst);
    printf("  16 busy scenes + every slot a worst-case clip (%u B: 64 dense steps, 64 events): %u clips\n", clip_max, n);
    {   /* a song as it is made: 16 busy scenes sharing clips (drums 4, bass 6, chords 4, lead 8) */
        static const uint8_t USE[NTRK] = {6, 4, 8, 4};
        uint32_t s, c, ok = 1;
        log_reset();
        for (k = 0; k < NTRK; k++)
            for (c = 0; c < USE[k]; c++)
                ok &= put(ID_CLIP(k, c), busy_c[k], 1) == 0;
        for (s = 0; s < 16u; s++)
            ok &= put(s, busy_s, 1) == 0;
        printf("  a shared song: 16 busy scenes over 22 busy clips (bass 6, chords 4, lead 8, drums 4): %s, live %u B (%u %%)\n",
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
    if (!quiet) printf("sizes: step_t %u, track_t %u, trk[] %u, project_t %u, proj_trk_t %u, dlrec_t %u, motion_store_t %u, motion_ev_t %u\n",
           (uint32_t)sizeof(step_t), (uint32_t)sizeof(track_t), (uint32_t)sizeof trk, (uint32_t)sizeof(project_t),
           (uint32_t)sizeof(proj_trk_t), (uint32_t)sizeof(dlrec_t), (uint32_t)sizeof(motion_store_t), (uint32_t)sizeof(motion_ev_t));
    if (!quiet) printf("       SEC_REC_MAX %u, SEC_RAW_N %u, PJ_NP %u, P_COUNT %u, G_COUNT %u\n", SEC_REC_MAX, SEC_RAW_N, PJ_NP, P_COUNT, G_COUNT);
    {   /* the proposed resident state of a clip build (docs/CLIPS-DESIGN.md section 3) */
        typedef struct { step_t st[NSTEP]; int16_t pp[4]; uint8_t nev, on, slot, ready; cev_t ev[64]; } clip_stage_t;
        typedef struct { uint32_t org; uint8_t cur, req, when, mod; } clip_trk_t;
        if (!quiet) printf("       clip stage (one track) %u B, x4 %u B; clip state per track %u B; scene refs 16x4 %u B; clip hashes 64 x 2 %u B\n",
               (uint32_t)sizeof(clip_stage_t), 4u * (uint32_t)sizeof(clip_stage_t), (uint32_t)sizeof(clip_trk_t), 16u * NTRK,
               64u * 2u);
    }
    if (!quiet) ids_report();
    codec_report("power-on", host_tracks_init);
    codec_report("demo (tracks_demo: acid 16, chords 32, lead 12, drums 16; factory presets)", demo);
    codec_report("typical (sec_codec_test: 16 steps a track, a few edits)", typical);
    codec_report("busy (64 steps everywhere: 16ths bass, held chords, 8ths lead, full drums)", busy);
    codec_report("dense worst case (64 random full steps a track)", dense);
    {   /* the scene body's bound: flags, globals' bitmap + all 32, sel + rsv, 4 tracks (engine, preset, bitmap, every value),
         * three FM6 voices, the drum key + record, 4 references (no steps: no raw fallback is ever needed) */
        uint32_t w = 1u + 4u + 2u * 32u + 4u + NTRK * (2u + (PJ_NP + 7u) / 8u + 2u * PJ_NP) + 1u + NPART * (128u + 1u + 16u) + 4u +
                     (uint32_t)sizeof(dlrec_t) + NTRK;
        if (!quiet) printf("\nscene bound (every value moved, 3 FM6 voices, a drum record) %u B\n", w);
        if (w > scene_max)
            scene_max = w;
    }
    if (!quiet) printf("\nworst cases: clip %u B (<= SEC_REC_MAX %u: one log record), scene %u B\n", clip_max, SEC_REC_MAX, scene_max);
    if (!strcmp(mode, "full") || !strcmp(mode, "log"))
        log_report();
    return 0;
}
