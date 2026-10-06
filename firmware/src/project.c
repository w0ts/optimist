/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Projects: four slots in .noinit RAM (the song sections A..D), so they survive resets and UBOOT
 * entry. With FELUCCA_FLASH every save also goes to flash through storage.c, and an empty RAM slot
 * is filled from flash on load. The working project is also kept in flash by itself (autosave, when
 * the transport is stopped and nothing sounds) and comes back at power-on: SLOOP starts where you
 * left it.
 *
 * Formats (the integrated build, FELUCCA_ANALOG2: format 7, see below; without ANALOG 2 as here):
 * 6 ("FUN6", written): format 5 and the FM6 parts' voices (eng_fm6.c edit buffers, DX7 packed, with
 * their operator switches and DX7 function settings; as Melodee's formats 4 and 7 keep them). 5 ("FUN5"):
 * today's P_COUNT / G_COUNT, 10-byte steps (levels and ratchets; the drum track: 16 lanes); read as it is,
 * its FM6 parts load their VOICE (and engine 10 was DX7: such a part plays FM6). Read and converted: 4 ("FUN4", SLOOP 2.0 .. 2.2: the same with PROJ_NP_V4 parameters,
 * before P_FXOFF, which takes its default: the effects on), 3 ("FUN3", SLOOP 1.x: 8-byte steps, the
 * drum track's notes become its lanes, the swings x 0.8 for the MPC scale), 2 ("FUN2") and 1 ("FUN1"),
 * which held PROJ_NP_V2 parameters per track, mapped by count as user presets are (the first
 * PROJ_NP_V2 - 8 are P_LEVEL.. in order, the last 8 P_E0..P_E7; the parameters added since take their
 * defaults). Their engine bytes are kept: formats 1 and 2 had engines 0..7 (ANALOG .. WHEEL), and the
 * engines added since were appended, no index moved; the drum track's byte (it has no engine) becomes 0.
 *
 * Built on the host too (tests/project_test.c, -DPROJ_HOST): the part above the #ifndef
 * PROJ_HOST needs core.h, params.c (TP), drums.c (the lanes), the engines and trk_def_engine (ui.c). */
#if FELUCCA_ANALOG2
/* ANALOG 2 + FM6 (the integrated build): format 7 ("FUN7", written): the 8 ANALOG 2 parameters just before
 * P_E0 (core.h P_A2WAVE..), the FM6 parts' voices after the tracks, FM6 = engine 9 and no SUPER engine.
 * Two test branches both wrote a "FUN6" (told apart by their size): ANALOG 2's (P_COUNT parameters, FM6's
 * numbering, no FM6 voices) and FM6's (format 5 + the FM6 voices: SUPER 9, FM6 10). Formats 6, 5 and 4 are
 * read by count (proj_from_np: the parameters added since take their defaults); the formats that numbered
 * SUPER 9 and DX7 / FM6 10 (FM6's 6, 5, 4) get today's numbers: DX7 / FM6 -> FM6 (9), SUPER -> ANALOG on
 * the swarm (proj_trk_from_super) */
#define PROJ_MAGIC 0x46554E38u                 /* "FUN8": four tracks, P_COUNT parameters each, 10-byte steps,
                                                * the FM6 voices, the drum lanes (drum_edit.c dlanes_t) */
#define PROJ_MAGIC_V7 0x46554E37u              /* "FUN7": format 8 without the drum lanes; read only */
#define PROJ_MAGIC_V6 0x46554E36u              /* "FUN6": ANALOG 2's or FM6's (test builds only); read only */
#define PROJ_MAGIC_V5 0x46554E35u              /* "FUN5": SLOOP plus, PROJ_NP_V5 parameters; read only */
#define PROJ_NP_V5 59u                         /* P_COUNT of format 5 (P_E0 was 51) */
#else
#define PROJ_MAGIC 0x46554E36u                 /* "FUN6": format 5 + the FM6 parts' voices */
#define PROJ_MAGIC_V5 0x46554E35u              /* "FUN5": four tracks, P_COUNT parameters each, 10-byte steps */
#endif
#define PROJ_MAGIC_V4 0x46554E34u              /* "FUN4": SLOOP 2.0 .. 2.2, PROJ_NP_V4 parameters; read only */
#define PROJ_NP_V4 58u                         /* P_COUNT of format 4 (P_E0 was 50: no P_FXOFF) */
#define PROJ_MAGIC_V3 0x46554E33u              /* "FUN3": SLOOP 1.x; read only */
#define PROJ_MAGIC_V2 0x46554E32u              /* "FUN2": four tracks, PROJ_NP_V2 parameters; read only */
#define PROJ_MAGIC_V1 0x46554E31u              /* "FUN1": one instrument; loads into track 1 */
#define PROJ_NP_V3 57u                         /* P_COUNT of format 3 (P_E0 was 49) */
#define PROJ_NG_V3 27u                         /* G_COUNT of formats 1..3 */
#define PROJ_NP_V2 53u                         /* P_COUNT of formats 1 and 2 (P_E0 was 45) */
#define PROJ_NG_V2 27u                         /* G_COUNT of formats 1 and 2 */
typedef struct {                               /* one track; the drum track ignores engine / preset */
    int16_t p[P_COUNT];
    uint8_t engine, preset;
    union {
        step_t step[NSTEP];
        dstep_t dstep[NSTEP];                  /* (the drum track: 16 lanes, the same size) */
    };
} proj_trk_t;
typedef struct {
    uint32_t magic, size;
    int16_t g[G_COUNT];
    uint8_t sel, rsv[3];                       /* the selected track */
    proj_trk_t t[NTRK];
    uint8_t fm6[NPART][128];                   /* the FM6 parts' voices, DX7 packed (fm6_has: which) */
    uint8_t fm6_on[NPART], fm6_has;            /* their operator switches (bit n - 1: OP n); bit k: part k */
    int8_t fm6_fn[NPART][16];                  /* the parts' FM6 functions (FN_PBUP..), [0] < 0: defaults */
#if FELUCCA_ANALOG2
    dlanes_t drum;                             /* format 8: the drum lanes' sounds (all 0: the kit as it is) */
#endif
    uint32_t sum;
} project_t;
#if !FELUCCA_ANALOG2
typedef struct {                               /* format 5 (SLOOP 2.3 .. plus), read only */
    uint32_t magic, size;
    int16_t g[G_COUNT];
    uint8_t sel, rsv[3];
    proj_trk_t t[NTRK];
    uint32_t sum;
} project_v5_t;
#endif
typedef struct {                               /* a track of format 4, read only */
    int16_t p[PROJ_NP_V4];
    uint8_t engine, preset;
    union {
        step_t step[NSTEP];
        dstep_t dstep[NSTEP];
    };
} proj_trk_v4_t;
typedef struct {                               /* format 4 (SLOOP 2.0 .. 2.2), read only */
    uint32_t magic, size;
    int16_t g[G_COUNT];                        /* (G_COUNT has not changed since: G_VIEW took the ROUT slot) */
    uint8_t sel, rsv[3];
    proj_trk_v4_t t[NTRK];
    uint32_t sum;
} project_v4_t;
typedef struct { uint8_t note[4], n, time, flags, vel; } step8_t;   /* the steps of formats 1..3 */
typedef struct {                               /* a track of format 3, read only */
    int16_t p[PROJ_NP_V3];
    uint8_t engine, preset;
    step8_t step[NSTEP];
} proj_trk_v3_t;
typedef struct {                               /* format 3 (SLOOP 1.x), read only */
    uint32_t magic, size;
    int16_t g[PROJ_NG_V3];
    uint8_t sel, rsv[3];
    proj_trk_v3_t t[NTRK];
    uint32_t sum;
} project_v3_t;
typedef struct {                               /* a track of formats 1 and 2, read only */
    int16_t p[PROJ_NP_V2];
    uint8_t engine, preset;
    step8_t step[NSTEP];
} proj_trk_v2_t;
typedef struct {                               /* format 2 (until 0.9), read only */
    uint32_t magic, size;
    int16_t g[PROJ_NG_V2];
    uint8_t sel, rsv[3];
    proj_trk_v2_t t[NTRK];
    uint32_t sum;
} project_v2_t;
typedef struct {                               /* format 1 (until 0.5 beta), read only */
    uint32_t magic, size;
    int16_t g[PROJ_NG_V2];
    proj_trk_v2_t t;
    uint32_t sum;
} project_v1_t;
_Static_assert(sizeof(project_v4_t) == 3112u, "format 4 as it was stored");
_Static_assert(sizeof(project_v2_t) == 2552u && sizeof(project_v1_t) == 688u && sizeof(project_v3_t) == 2584u,
               "formats 1 / 2 / 3 as they were stored");
project_t proj_slot[4] __attribute__((section(".noinit")));

static uint32_t proj_hash(const void *p, uint32_t n)   /* FNV-1a over n bytes */
{
    const uint8_t *b = (const uint8_t *)p;
    uint32_t i, s = 0x811C9DC5u;
    for (i = 0; i < n; i++)
        s = (s ^ b[i]) * 16777619u;
    return s;
}
static uint32_t proj_sum(const project_t *p) { return proj_hash(p, sizeof *p - 4u); }
static int proj_ok(const project_t *q) { return q->magic == PROJ_MAGIC && q->size == sizeof *q && q->sum == proj_sum(q); }
#if FELUCCA_MOTION
#include "motion_proj.c"       /* each project buffer's motion store (motion.c) */
#endif

/* ---- old formats -> format 4 */
/* an old step into a synth step (no level, no ratchet) */
static void step_from8(step_t *d, const step8_t *s)
{
    memcpy(d->note, s->note, 4);
    d->n = s->n;
    d->time = s->time;
    d->flags = s->flags;
    d->vel = s->vel;
    d->lvl = d->rat = 0;
}
/* an old drum step (GM notes) into the drum track's lanes; its velocity / accent -> their level */
static void dstep_from8(dstep_t *d, const step8_t *s)
{
    uint32_t i, lvl = (s->flags & SF_ACCENT) || s->vel > 115u ? LV_HARD : !s->vel ? LV_NORM : vel_lvl(s->vel);
    memset(d, 0, sizeof *d);
    if (s->time != ST_NOTE)
        return;
    for (i = 0; i < s->n && i < 4u; i++)
        dstep_set(d, lane_of_note(s->note[i] & 127u), lvl, 0);
}
static int16_t swing_from_v3(int32_t v) { return (int16_t)clamp((v * 4 + 2) / 5, 0, 100); }   /* /250 -> /200 */

/* the globals of formats 1..3 (G_* unchanged since; any added later: their defaults) */
static void proj_g_from_old(int16_t *g, const int16_t *g2)
{
    uint32_t i;
    for (i = 0; i < G_COUNT; i++)
        g[i] = i < PROJ_NG_V3 ? g2[i] : GP[i].def;
    g[G_SWING] = swing_from_v3(g[G_SWING]);
}

/* a track of format 3 -> today's (by id up to P_SLDEPTH; P_E0.. moved) */
static void proj_trk_from_v3(proj_trk_t *d, const proj_trk_v3_t *s, int drum)
{
    uint32_t k, nc = PROJ_NP_V3 - 8u;
    for (k = 0; k < P_E0; k++)
        d->p[k] = k < nc ? s->p[k] : TP[k].def;
    for (k = 0; k < 8u; k++)
        d->p[P_E0 + k] = s->p[nc + k];
    d->p[P_SSWING] = swing_from_v3(d->p[P_SSWING]);
    d->p[P_ASWING] = swing_from_v3(d->p[P_ASWING]);
    d->engine = drum ? 0u : s->engine;
    d->preset = drum ? 0u : s->preset;
    for (k = 0; k < NSTEP; k++) {
        if (drum)
            dstep_from8(&d->dstep[k], &s->step[k]);
        else
            step_from8(&d->step[k], &s->step[k]);
    }
}

/* a track of formats 1 and 2 -> format 3 (mapped by count, see the top) */
static void proj_trk_v2_to_v3(proj_trk_v3_t *d, const proj_trk_v2_t *s, int drum)
{
    uint32_t k, nc = PROJ_NP_V2 - 8u;
    for (k = 0; k < PROJ_NP_V3 - 8u; k++)
        d->p[k] = k < nc ? s->p[k] : TP[k].def;
    for (k = 0; k < 8u; k++)
        d->p[PROJ_NP_V3 - 8u + k] = s->p[nc + k];
    d->engine = drum ? 0u : s->engine;          /* (indices 0..7 as they were) */
    d->preset = drum ? 0u : s->preset;
    memcpy(d->step, s->step, sizeof d->step);
}

/* a format 3 project -> slot q as format 4 */
static void proj_from_v3_ok(project_t *q, const project_v3_t *v3)
{
    uint32_t i;
    memset(q, 0, sizeof *q);
    memset(q->fm6_fn, 0xFF, sizeof q->fm6_fn);         /* FM6 functions: the defaults */
    q->magic = PROJ_MAGIC;
    q->size = sizeof *q;
    proj_g_from_old(q->g, v3->g);
    q->sel = v3->sel;
    for (i = 0; i < NTRK; i++)
        proj_trk_from_v3(&q->t[i], &v3->t[i], i == TRK_DRUM);
    q->sum = proj_sum(q);
}
static int proj_from_v3(project_t *q, const project_v3_t *v3, int n)
{
    if (n != (int)sizeof *v3 || v3->magic != PROJ_MAGIC_V3 || v3->size != sizeof *v3 ||
        v3->sum != proj_hash(v3, sizeof *v3 - 4u))
        return 0;
    proj_from_v3_ok(q, v3);
    return 1;
}

static project_v3_t proj_v3_tmp;               /* (formats 1, 2: through format 3) */
/* a format 2 project (n bytes in *v2) -> slot q as format 4 */
static int proj_from_v2(project_t *q, const project_v2_t *v2, int n)
{
    project_v3_t *v3 = &proj_v3_tmp;
    uint32_t i;
    if (n != (int)sizeof *v2 || v2->magic != PROJ_MAGIC_V2 || v2->size != sizeof *v2 ||
        v2->sum != proj_hash(v2, sizeof *v2 - 4u))
        return 0;
    memset(v3, 0, sizeof *v3);
    memcpy(v3->g, v2->g, sizeof v3->g);
    v3->sel = v2->sel;
    for (i = 0; i < NTRK; i++)
        proj_trk_v2_to_v3(&v3->t[i], &v2->t[i], i == TRK_DRUM);
    proj_from_v3_ok(q, v3);
    return 1;
}

/* a format 1 project (n bytes in *v1) -> slot q as format 4: the instrument becomes track 1,
 * tracks 2..4 start empty (their sounds as at power-on) */
static int proj_from_v1(project_t *q, const project_v1_t *v1, int n)
{
    project_v3_t *v3 = &proj_v3_tmp;
    uint32_t i;
    if (n != (int)sizeof *v1 || v1->magic != PROJ_MAGIC_V1 || v1->size != sizeof *v1 ||
        v1->sum != proj_hash(v1, sizeof *v1 - 4u))
        return 0;
    memset(v3, 0, sizeof *v3);
    memcpy(v3->g, v1->g, sizeof v3->g);
    proj_trk_v2_to_v3(&v3->t[0], &v1->t, 0);
    proj_from_v3_ok(q, v3);
    for (i = 1; i < NTRK; i++) {               /* the other tracks: their defaults, no steps */
        uint32_t k;
        for (k = 0; k < P_COUNT; k++)
            q->t[i].p[k] = k >= P_E0 ? ENGINES[trk_def_engine(i)]->edit[k - P_E0].def : TP[k].def;
        q->t[i].engine = (uint8_t)eng_uid(trk_def_engine(i));   /* (a UID) */
        q->t[i].preset = 0xFF;                 /* 0xFF: its default preset (project_load) */
        memset(q->t[i].step, 0, sizeof q->t[i].step);
        if (i != TRK_DRUM)
            for (k = 0; k < NSTEP; k++)
                q->t[i].step[k].time = ST_REST;
    }
    q->sum = proj_sum(q);
    return 1;
}

#if FELUCCA_ANALOG2
/* the FM6 voices after the tracks (fm6[][], fm6_on[], fm6_has, fm6_fn[][]: today's and FM6's FUN6 alike) */
#define PROJ_FM6_N (NPART * 128u + NPART + 1u + NPART * 16u)
_Static_assert(__builtin_offsetof(project_t, fm6_fn) + sizeof(((project_t *)0)->fm6_fn) - __builtin_offsetof(project_t, fm6) ==
               PROJ_FM6_N,
               "the FM6 voices: one block");
#define PROJ_A2_SUPER A2_SUPER0                 /* (params.c analog2_from_super) */

/* a format 7 project (format 8 without the drum lanes, at its end) -> slot q: the lanes as the kit (0) */
#define PROJ_V7_N ((uint32_t)__builtin_offsetof(project_t, drum))
_Static_assert(PROJ_V7_N == 3632u && sizeof(project_t) == PROJ_V7_N + sizeof(dlanes_t) + 4u, "FUN7 + the drum lanes");
static int proj_from_v7(project_t *q, const void *b, int n)
{
    const uint8_t *c = (const uint8_t *)b;
    if (n != (int)(PROJ_V7_N + 4u) || ((const uint32_t *)b)[0] != PROJ_MAGIC_V7 ||
        ((const uint32_t *)b)[1] != PROJ_V7_N + 4u || *(const uint32_t *)(c + PROJ_V7_N) != proj_hash(b, PROJ_V7_N))
        return 0;
    memcpy(q, b, PROJ_V7_N);
    memset(&q->drum, 0, sizeof q->drum);
    q->magic = PROJ_MAGIC;
    q->size = sizeof *q;
    q->sum = proj_sum(q);
    return 1;
}

/* a track that played SUPER (formats 4..6 of FM6's numbering) -> ANALOG on the swarm (params.c) */
static void proj_trk_from_super(proj_trk_t *d)
{
    d->preset = (uint8_t)analog2_from_super(d->p, d->preset);
    d->engine = 0;
}

/* a project of format 4, 5 or 6 (n bytes at b, np parameters a track, as project_t laid out then, nfm6
 * bytes of FM6 voices after the tracks or none) -> slot q as today's format: by count, the parameters added
 * since (just before P_E0) their defaults; old_eng: SUPER was 9, DX7 / FM6 10 (today: ANALOG's swarm, 9) */
static int proj_from_np(project_t *q, const void *b, int n, uint32_t magic, uint32_t np, uint32_t nfm6,
                        int old_eng)
{
    const uint8_t *c = (const uint8_t *)b;
    uint32_t ts = 2u * np + 2u + sizeof q->t[0].step, tr = 12u + sizeof q->g + NTRK * ts, i, k, nc = np - 8u;
    uint32_t sz = ((tr + nfm6 + 3u) & ~3u) + 4u;
    if (n != (int)sz || ((const uint32_t *)b)[0] != magic || ((const uint32_t *)b)[1] != sz ||
        *(const uint32_t *)(c + sz - 4u) != proj_hash(b, sz - 4u))
        return 0;
    memset(q, 0, sizeof *q);
    memset(q->fm6_fn, 0xFF, sizeof q->fm6_fn);         /* FM6 functions: the defaults */
    if (nfm6)
        memcpy(q->fm6, c + tr, nfm6);
    q->magic = PROJ_MAGIC;
    q->size = sizeof *q;
    memcpy(q->g, c + 8, sizeof q->g);
    q->sel = c[8u + sizeof q->g];
    for (i = 0; i < NTRK; i++) {
        proj_trk_t *d = &q->t[i];
        const uint8_t *s = c + 12u + sizeof q->g + i * ts;
        for (k = 0; k < P_COUNT; k++) {
            if (k >= P_E0 || k < nc)
                memcpy(&d->p[k], s + 2u * (k >= P_E0 ? nc + k - P_E0 : k), 2);
            else
                d->p[k] = TP[k].def;
        }
        d->engine = s[2u * np];
        d->preset = s[2u * np + 1u];
        memcpy(d->step, s + 2u * np + 2u, sizeof d->step);
        if (old_eng && i != TRK_DRUM) {
            if (d->engine == 10u)                       /* DX7 (SLOOP plus) or FM6: FM6 */
                d->engine = ENG_UID_FM6;
            else if (d->engine == 9u)                   /* SUPER: ANALOG's swarm */
                proj_trk_from_super(d);
            else if (d->engine == 11u)                  /* SLICE (FELUCCA_SLICE builds): one down, as SUPER left */
                d->engine = 10u;
        }
    }
    q->sum = proj_sum(q);
    return 1;
}
#else
/* a format 4 project (n bytes in *v4) -> slot q as format 5: by count, P_FXOFF (just before P_E0) its default */
static int proj_from_v4(project_t *q, const project_v4_t *v4, int n)
{
    uint32_t i, k, nc = PROJ_NP_V4 - 8u;
    if (n != (int)sizeof *v4 || v4->magic != PROJ_MAGIC_V4 || v4->size != sizeof *v4 ||
        v4->sum != proj_hash(v4, sizeof *v4 - 4u))
        return 0;
    memset(q, 0, sizeof *q);
    memset(q->fm6_fn, 0xFF, sizeof q->fm6_fn);         /* FM6 functions: the defaults */
    q->magic = PROJ_MAGIC;
    q->size = sizeof *q;
    memcpy(q->g, v4->g, sizeof q->g);
    q->sel = v4->sel;
    for (i = 0; i < NTRK; i++) {
        proj_trk_t *d = &q->t[i];
        const proj_trk_v4_t *s = &v4->t[i];
        for (k = 0; k < P_E0; k++)
            d->p[k] = k < nc ? s->p[k] : TP[k].def;
        for (k = 0; k < 8u; k++)
            d->p[P_E0 + k] = s->p[nc + k];
        d->engine = s->engine;
        d->preset = s->preset;
        memcpy(d->step, s->step, sizeof d->step);
    }
    q->sum = proj_sum(q);
    return 1;
}
#endif

#if !FELUCCA_ANALOG2
/* a format 5 project (n bytes in *v5) -> slot q as format 6: no FM6 voices (the parts load their VOICE) */
static int proj_from_v5(project_t *q, const project_v5_t *v5, int n)
{
    if (n != (int)sizeof *v5 || v5->magic != PROJ_MAGIC_V5 || v5->size != sizeof *v5 ||
        v5->sum != proj_hash(v5, sizeof *v5 - 4u))
        return 0;
    memset(q, 0, sizeof *q);
    memset(q->fm6_fn, 0xFF, sizeof q->fm6_fn);         /* FM6 functions: the defaults */
    q->magic = PROJ_MAGIC;
    q->size = sizeof *q;
    memcpy(q->g, v5->g, sizeof q->g);
    q->sel = v5->sel;
    memcpy(q->t, v5->t, sizeof q->t);
    q->sum = proj_sum(q);
    return 1;
}
#endif

/* n bytes of a stored project (any format) -> slot q as today's format; 0 = not a project */
static int proj_import(project_t *q, const void *b, int n)
{
    if (n == (int)sizeof *q && proj_ok((const project_t *)b)) {
        memcpy(q, b, sizeof *q);
        return 1;
    }
#if FELUCCA_ANALOG2
    return proj_from_v7(q, b, n) ||                                       /* FUN7: no drum lanes */
           proj_from_np(q, b, n, PROJ_MAGIC_V6, P_COUNT, 0, 0) ||          /* ANALOG 2's FUN6 */
           proj_from_np(q, b, n, PROJ_MAGIC_V6, PROJ_NP_V5, PROJ_FM6_N, 1) || /* FM6's FUN6 */
           proj_from_np(q, b, n, PROJ_MAGIC_V5, PROJ_NP_V5, 0, 1) || proj_from_np(q, b, n, PROJ_MAGIC_V4, PROJ_NP_V4, 0, 1) ||
#else
    return proj_from_v5(q, (const project_v5_t *)b, n) || proj_from_v4(q, (const project_v4_t *)b, n) ||
#endif
           proj_from_v3(q, (const project_v3_t *)b, n) || proj_from_v2(q, (const project_v2_t *)b, n) ||
           proj_from_v1(q, (const project_v1_t *)b, n);
}

/* ---- orphans: a part whose engine this build leaves out (registry.h). It plays the fallback engine with that
 * engine's defaults; the project's engine UID, preset, EDIT values and FM6 voice are kept here and written back
 * when the project is captured (save, autosave, a song section), as long as the part's engine and EDIT values
 * are still the ones it was given: a project survives a reduced build and plays as before on a full one */
typedef struct {
    uint8_t live, uid, preset, slot, fm6_on, fm6_has;
    int16_t e[8], given[8];
    uint8_t fm6[FELUCCA_ENG_FM6 ? 1 : 128];             /* (FM6 built: its parts keep their voice anyway) */
} proj_orphan_t;
static proj_orphan_t proj_orph[NPART];
static int proj_orph_is(uint32_t uid) { return !ENG_ALL && uid < ENG_UID_N && !eng_built(uid); }
static void proj_orph_take(uint32_t k, const project_t *p, const track_t *t)   /* proj_apply, part k */
{
    proj_orphan_t *o = &proj_orph[k];
    const proj_trk_t *s = &p->t[k];
    o->live = (uint8_t)proj_orph_is(s->engine);
    if (!o->live)
        return;
    o->uid = s->engine;
    o->preset = s->preset;
    o->slot = t->eng_req;
    memcpy(o->e, &s->p[P_E0], sizeof o->e);
    memcpy(o->given, &t->p[P_E0], sizeof o->given);
    o->fm6_has = (uint8_t)(!FELUCCA_ENG_FM6 && ((p->fm6_has >> k) & 1u));
    o->fm6_on = p->fm6_on[k];
    memcpy(o->fm6, p->fm6[k], sizeof o->fm6);
}
static void proj_orph_give(uint32_t k, project_t *p)                           /* proj_capture, part k */
{
    const proj_orphan_t *o = &proj_orph[k];
    proj_trk_t *d = &p->t[k];
    if (!o->live || trk[k].eng_req != o->slot || memcmp(&trk[k].p[P_E0], o->given, sizeof o->given))
        return;                                         /* (the user gave the part another sound: it is that now) */
    d->engine = o->uid;
    d->preset = o->preset;
    memcpy(&d->p[P_E0], o->e, sizeof o->e);
    if (!FELUCCA_ENG_FM6 && o->fm6_has) {
        memcpy(p->fm6[k], o->fm6, sizeof o->fm6);
        p->fm6_on[k] = o->fm6_on;
        p->fm6_has |= (uint8_t)(1u << k);
    }
}
/* the part plays an engine this build leaves out: its UID (the UI marks it), else 0xFF */
static uint32_t proj_orph_uid(uint32_t k)
{
    const proj_orphan_t *o = &proj_orph[k % NPART];
    return o->live && trk[k % NPART].eng_req == o->slot && !memcmp(&trk[k % NPART].p[P_E0], o->given, sizeof o->given) ? o->uid : 0xFFu;
}

/* ---- the working project <-> a project_t */
static void proj_capture(project_t *p)        /* what is playing now, as a project */
{
    uint32_t i;
    memset(p, 0, sizeof *p);
    p->magic = PROJ_MAGIC;
    p->size = sizeof *p;
    for (i = 0; i < G_COUNT; i++)
        p->g[i] = i == G_MIDI ? 0 : song.g[i];          /* (G_MIDI: a status, not saved) */
    p->sel = song.sel;
#if BP_SET_ANY
    p->rsv[0] = bps_pack();                             /* the backported features' settings (bp_set.c) */
#endif
    for (i = 0; i < NTRK; i++) {
        memcpy(p->t[i].p, trk[i].p, sizeof trk[i].p);
        p->t[i].engine = (uint8_t)(i < NPART ? eng_uid(trk[i].eng_req % NENGINES) : 0u);   /* (a UID) */
        p->t[i].preset = trk[i].preset;
        memcpy(p->t[i].step, trk[i].step, sizeof trk[i].step);
    }
    for (i = 0; i < NPART; i++) {                       /* the FM6 parts' voices, edits and all */
        uint32_t k;
        if (ENG_IS(ENGINES[trk[i].eng_req % NENGINES], FM6) && fm6_cur[i]) {
            fm6_pack(p->fm6[i], fm6_ed[i]);
            for (k = 0; k < 6u; k++)
                p->fm6_on[i] |= (uint8_t)(fm6_ed[i][FV_ON + k] ? 1u << k : 0u);
            p->fm6_has |= (uint8_t)(1u << i);
        }
        memset(p->fm6_fn[i], 0xFF, sizeof p->fm6_fn[i]);
        if (fm6_fnok[i])
            for (k = 0; k < FM6_NFN; k++)
                p->fm6_fn[i][k] = (int8_t)fm6_ed[i][FN_PBUP + k];
    }
    for (i = 0; i < NPART; i++)                         /* the orphans as they came (a reduced build) */
        proj_orph_give(i, p);
#if FELUCCA_ANALOG2
    p->drum = dl;                                       /* the drum lanes (kept in every build) */
#endif
#if FELUCCA_MOTION
    motion_capture_params(p);                           /* the patch under the motion (motion_proj.c) */
#endif
    p->sum = proj_sum(p);
#if FELUCCA_MOTION
    motion_capture_store(p);
#endif
}

/* a project's tracks (and its globals, all: a load; or only the drum level / reverb: a song
 * section) into the working one, every value back inside its range. The audio ISR must not run
 * meanwhile (the song sections: called from it; a load: IRQ off) */
static void proj_apply(const project_t *p, int all)
{
    uint32_t i, k;
#if FELUCCA_MOTION
    motion_apply_store(p);                              /* its motion, if it is this project's (motion_proj.c) */
#endif
    undo_clear();                                       /* (undo.c: the history was of other steps) */
    for (i = 0; i < G_COUNT; i++)
        if (all ? i != G_SLOT && i != G_LOAD && i != G_SAVE && i != G_VIEW && i != G_MIDI : i == G_DRLVL || i == G_DRREV)
            song.g[i] = (int16_t)clamp(p->g[i], GP[i].min, GP[i].max);
#if BP_SET_ANY
    if (all)
        bps_unpack(p->rsv[0]);                          /* (older projects: 0, every default) */
#endif
    for (k = 0; k < NTRK; k++) {
        track_t *t = &trk[k];
        const proj_trk_t *s = &p->t[k];
        uint32_t e = k < NPART ? eng_slot(s->engine) : 0u;   /* UID -> slot (not built: its fallback) */
        t->eng_req = (uint8_t)e;
        t->user = 0;                                    /* (no user preset slot is saved) */
        for (i = 0; i < P_COUNT; i++) {                 /* every value back inside its range */
            const param_desc_t *d = k == TRK_DRUM && i == P_E0 ? &DRUM_KIT_DESC :   /* the drum kit */
                                    i >= P_E0 && i <= P_E7 ? &ENGINES[e]->edit[i - P_E0] : &TP[i];
            t->p[i] = (int16_t)clamp(s->p[i], d->min, d->max);
        }
        t->preset = (uint8_t)(ENGINES[e]->npresets ? (s->preset == 0xFFu ? 0u : s->preset) % ENGINES[e]->npresets : 0u);
        if (k < NPART && proj_orph_is(s->engine)) {     /* an engine left out: the fallback's own sound */
            for (i = 0; i < 8u; i++)
                t->p[P_E0 + i] = ENGINES[e]->edit[i].def;
            t->preset = 0;
        }
        if (k < NPART)
            proj_orph_take(k, p, t);
        if (k < NPART) {                                /* FM6: its saved voice, else its VOICE afresh */
            fm6_fn_reset(fm6_ed[k]);                    /* its functions (older formats: the defaults) */
            if (p->fm6_fn[k][0] >= 0)
                for (i = 0; i < FM6_NFN; i++)
                    fm6_set(fm6_ed[k], FN_PBUP + i, p->fm6_fn[k][i]);
            fm6_fnok[k] = 1;
            fm6_cur[k] = 0;
            if (ENG_IS(ENGINES[e], FM6) && ((p->fm6_has >> k) & 1u)) {
                fm6_unpack(fm6_ed[k], p->fm6[k]);
                for (i = 0; i < 6u; i++)
                    fm6_ed[k][FV_ON + i] = (int16_t)((p->fm6_on[k] >> i) & 1u);
                fm6_cur[k] = (int16_t)(t->p[P_E0] + 1);
            }
        }
        memcpy(t->step, s->step, sizeof t->step);
        if (k != TRK_DRUM)
            for (i = 0; i < NSTEP; i++) {
                step_t *st = &t->step[i];
                uint32_t j;
                if (st->n > 4u)
                    st->n = 4;
                if (st->time > ST_REST)
                    st->time = ST_REST;
                for (j = 0; j < 4u; j++)
                    st->note[j] &= 127u;
            }
    }
#if FELUCCA_ANALOG2
    dl = p->drum;                                       /* the drum lanes, for the kit they were set on */
    dl_fix(&dl);
    dl_e0 = TDRUM->p[P_E0];
#endif
}

#ifndef PROJ_HOST
#if FELUCCA_ARRANGER
#include "arranger_scene.c"
#endif
static uint8_t sec_dirty, song_dirty;           /* live sections / the song: in RAM, not yet in flash */
#if FELUCCA_MOTION && FELUCCA_FLASH
#include "motion_flash.c"      /* the motion stores beside their projects in flash */
#define MOTION_SAVED(obj, p) motion_flash_write(obj, p)
#define MOTION_READ(obj, p) motion_flash_read(obj, p)
#else
#define MOTION_SAVED(obj, p) ((void)0)
#define MOTION_READ(obj, p) ((void)0)
#endif
#if FELUCCA_MOTION
#define MOTION_HASH() motion_hash()
#else
#define MOTION_HASH() 0u
#endif
#if FELUCCA_FLASH
/* slot from flash into RAM (format 4, or an old one converted) */
static union {
    project_t cur;                             /* (today's format: FUN7; FUN6 without ANALOG 2) */
#if !FELUCCA_ANALOG2
    project_v5_t v5;
#endif
    project_v4_t v4old;
    project_v3_t v3;
    project_v2_t v2;
    project_v1_t v1;
} proj_tmp;
static void proj_fetch(uint32_t slot)
{
    project_t *q = &proj_slot[slot & 3u];
    int n = st_load(OBJ_PROJECT0 + (slot & 3u), &proj_tmp, sizeof proj_tmp);
    if (!proj_import(q, &proj_tmp, n))
        q->magic = 0;
    else
        MOTION_READ(OBJ_PROJECT0 + (slot & 3u), q);
}
#endif

static void project_save(uint32_t slot)
{
    project_t *p = &proj_slot[slot & 3u];
#if FELUCCA_ARRANGER
    if (song.playing || transport_req) { ui_message("STOP BEFORE SAVE"); return; }
#endif
    proj_capture(p);
#if FELUCCA_FLASH
    if (flash_ok) {
#if FELUCCA_MOTION
        if (st_save(OBJ_PROJECT0 + (slot & 3u), p, sizeof *p)) {
            ui_message("SAVE ERROR");
            return;
        }
        MOTION_SAVED(OBJ_PROJECT0 + (slot & 3u), p);
        ui_message("SAVED");
#else
        ui_message(st_save(OBJ_PROJECT0 + (slot & 3u), p, sizeof *p) ? "SAVE ERROR" : "SAVED");
#endif
        return;
    }
#endif
    ui_message("SAVED (RAM)");
}

/* a project into the working one: the transport stops, everything sounding is released */
static void project_apply(const project_t *p)
{
    uint32_t k;
    transport_req = 2;
    panic_req = (1u << NTRK) - 1u;
    fm1_irq_off();                                      /* the audio ISR must not see half a project */
    proj_apply(p, 1);
    song.sel = (uint8_t)(p->sel < NTRK ? p->sel : 0u);
    fm1_irq_on();
    for (k = 0; k < NPART; k++)                         /* a format 1 project: the default sounds of tracks 2, 3 */
        if (p->t[k].preset == 0xFFu) {
            apply_preset_to(&trk[k], trk_def_preset(k));
            steps_clear(&trk[k]);
        }
    sync_reload = 1;
    ui.force = 1;
}

static void project_load(uint32_t slot)
{
    project_t *p = &proj_slot[slot & 3u];
#if FELUCCA_ARRANGER
    if (song.playing || transport_req) { ui_message("STOP BEFORE LOAD"); return; }
#endif
#if FELUCCA_FLASH
    if (flash_ok && !proj_ok(p))
        proj_fetch(slot);
#endif
    if (!proj_ok(p)) {
        ui_message("EMPTY SLOT");
        return;
    }
    project_apply(p);
    ui_message("LOADED");
}

/* ---- the working project, kept in flash by itself: saved when it changed, the transport is stopped,
 * nothing sounds and the panel was not touched for AUTOSAVE_IDLE (a flash erase stops the audio for
 * ~50 ms: never while something plays); loaded at power-on (autosave_resume) */
#define AUTOSAVE_IDLE 2500u                    /* ms without input */
#define AUTOSAVE_GAP 20000u                    /* ms between two saves at least */
static project_t autosave_buf __attribute__((section(".pool")));
static uint32_t autosave_hash, autosave_ms, autosave_checked;

static int audio_quiet(void)
{
    uint32_t p, i;
    for (p = 0; p < NPART; p++)
        for (i = 0; i < NVOICE; i++)
            if (trk[p].v[i].active)
                return 0;
    for (i = 0; i < NDRUM; i++)
        if (drums.v[i].active)
            return 0;
    return 1;
}

static void autosave_tick(void)                /* main loop */
{
#if FELUCCA_FLASH
    uint32_t h, now = fm1_ms;
    if (!flash_ok || song.playing || transport_req || rec_wait || ft_on || ui.menu ||
        now - ui_input_ms < AUTOSAVE_IDLE || now - autosave_ms < AUTOSAVE_GAP || now - autosave_checked < 1000u)
        return;
    autosave_checked = now;
    proj_capture(&autosave_buf);
    h = autosave_buf.sum ^ MOTION_HASH();
    if (h == autosave_hash || !audio_quiet())
        return;
    if (st_save(OBJ_AUTOSAVE, &autosave_buf, sizeof autosave_buf) == 0) {
        MOTION_SAVED(OBJ_AUTOSAVE, &autosave_buf);
        autosave_hash = h;
    }
    autosave_ms = fm1_ms;
#endif
}

static void autosave_resume(void)              /* power-on: the project as it was left (felucca_init) */
{
#if FELUCCA_FLASH
    project_t *q = &autosave_buf;
    int n;
    if (!flash_ok)
        return;
    n = st_load(OBJ_AUTOSAVE, &proj_tmp, sizeof proj_tmp);
    if (!proj_import(q, &proj_tmp, n))
        return;
#if FELUCCA_MOTION
    MOTION_READ(OBJ_AUTOSAVE, q);
    proj_apply(q, 1);
    autosave_hash = q->sum ^ MOTION_HASH();
#else
    autosave_hash = q->sum;
    proj_apply(q, 1);
#endif
    song.sel = (uint8_t)(q->sel < NTRK ? q->sel : 0u);
    for (n = 0; n < NPART; n++)
        trk[n].engine = trk[n].eng_req;        /* (nothing sounds yet: no fade) */
#endif
}

/* settings + learned panel table: one flash object. The flash copy wins at
 * boot (the .noinit copies are garbage after a power-off). */
typedef struct {
    uint32_t magic, palette, lowcut, zoom;
    panel_t panel;
#if FELUCCA_ARRANGER
    arr_config_t arrangement;
#endif
    uint32_t view;                                 /* (appended: a shorter record, saved before it, reads as ALL) */
#if FELUCCA_BRIGHT
    uint32_t bright;                               /* appended (bright.c): 0 = full; a record without it: full */
#endif
} persist_t;
#define PERSIST_NO_VIEW ((int)__builtin_offsetof(persist_t, view))   /* the record's length before view */
#if FELUCCA_BRIGHT
#define PERSIST_NO_BRIGHT ((int)__builtin_offsetof(persist_t, bright))   /* .. before bright (a build without it) */
#endif
#if FELUCCA_ARRANGER
#define PERSIST_MAGIC 0x50455233u                  /* "PER3": includes the song order */
#else
#define PERSIST_MAGIC 0x50455232u
#endif
#if FELUCCA_FLASH
static persist_t persist_saved;
#endif

static void persist_boot(void)                    /* before settings_init / panel_init */
{
#if FELUCCA_ARRANGER
    arr_defaults(&arrangement);
#endif
#if FELUCCA_FLASH
    persist_t p;
    uint32_t f = irq_save();
    flash_ok = FL_FAR(fl_jedec_ram)() == 0x856014u;       /* the expected 1 MiB part, else stay RAM-only */
    irq_restore(f);
    if (!flash_ok)
        return;
    fl_plain_window_init();                        /* flash above 0x93000 reads as plaintext through XIP
                                                    * (user sample sets are played from there) */
    {
        uint32_t k;
        for (k = 0; k < SMP_USER_SLOTS; k++)
            smp_user_scan(k);
    }
    {
        int n = st_load(OBJ_SETTINGS, &p, sizeof p);
#if FELUCCA_BRIGHT
        if (n < (int)sizeof p)
            p.bright = 0;                          /* saved without BRIGHT: full */
        if (n == PERSIST_NO_BRIGHT)
            n = (int)sizeof p;                     /* (a build without it: the rest as ours) */
#endif
        if (n < (int)sizeof p)
            p.view = 1;                            /* saved before VIEW (or PER2): the overview, as a fresh device */
        if (((n == (int)sizeof p || n == PERSIST_NO_VIEW) && p.magic == PERSIST_MAGIC)
#if FELUCCA_ARRANGER
            || (n == (int)(16u + sizeof(panel_t)) && p.magic == 0x50455232u)
#endif
            ) {
            settings.magic = SETTINGS_MAGIC;
            settings.palette = p.palette;
            settings.lowcut = p.lowcut;
            settings.zoom = p.zoom;
            settings.view = p.view > 1u ? 1u : p.view;
#if FELUCCA_BRIGHT
            bl_dim = (uint8_t)(p.bright & 7u);
#endif
            if (p.panel.magic == PANEL_MAGIC)
                panel = p.panel;
#if FELUCCA_ARRANGER
            if (p.magic == PERSIST_MAGIC && arr_valid(&p.arrangement, 15u))
                arrangement = p.arrangement;
            else
                p.arrangement = arrangement;
#endif
            persist_saved = p;
        } else if (n == (int)(8u + sizeof(panel_t)) && p.magic == 0x50455231u) {   /* "PER1": palette, panel */
            const uint32_t *w = (const uint32_t *)&p;
            panel_t old;
            memcpy(&old, w + 2, sizeof old);
            settings.magic = SETTINGS_MAGIC;
            settings.palette = w[1];
            settings.lowcut = 0;
            settings.zoom = 0;
            settings.view = 1;
            if (old.magic == PANEL_MAGIC)
                panel = old;
        }
    }
    {   /* projects: fill empty RAM slots from flash, so the slot list is right after power-on. A slot
         * still valid in RAM (a warm reset: an update, UPDATE MODE, a crash) may never have reached
         * flash (a live section stored while playing): marked to be written when quiet */
        uint32_t i;
        for (i = 0; i < 4u; i++)
            if (!proj_ok(&proj_slot[i])) {
                proj_fetch(i);
            } else {
                MOTION_READ(OBJ_PROJECT0 + i, &proj_slot[i]);   /* (the pool is cleared at boot; a slot only
                                                                 * in RAM: its sum differs, no motion) */
                int n = st_load(OBJ_PROJECT0 + i, &proj_tmp, sizeof proj_tmp);
                if (n != (int)sizeof proj_slot[i] || memcmp(&proj_tmp.cur, &proj_slot[i], sizeof proj_slot[i]))
                    sec_dirty |= (uint8_t)(1u << i);
            }
    }
    up_boot();                                     /* user presets */
#endif
}

static int project_used(uint32_t slot) { return proj_ok(&proj_slot[slot & 3u]); }

static void settings_save(void)
{
#if FELUCCA_FLASH
    persist_t p;
    if (!flash_ok)
        return;
    memset(&p, 0, sizeof p);
    p.magic = PERSIST_MAGIC;
    p.palette = settings.palette;
    p.lowcut = settings.lowcut;
    p.zoom = settings.zoom;
    p.view = settings.view;
#if FELUCCA_BRIGHT
    p.bright = bl_dim & 7u;
#endif
    p.panel = panel;
#if FELUCCA_ARRANGER
    p.arrangement = arrangement;
#endif
    if (!memcmp(&p, &persist_saved, sizeof p))
        return;                                    /* unchanged: no erase cycle */
    if (st_save(OBJ_SETTINGS, &p, sizeof p) == 0)
        persist_saved = p;
#endif
}

#if FELUCCA_FLASH
_Static_assert(sizeof(project_t) <= ST_PAYLOAD_MAX, "project does not fit one flash sector");
#endif
#if FELUCCA_ARRANGER
static void arrangement_save(void)
{
    if (song.playing || transport_req) { ui_message("STOP BEFORE SAVE"); return; }
    settings_save();
#if FELUCCA_FLASH
    if (flash_ok) {
        ui_message(memcmp(&persist_saved.arrangement, &arrangement, sizeof arrangement) ? "SAVE ERROR" : "SONG SAVED");
        return;
    }
#endif
    ui_message("SONG IN RAM ONLY");
}

/* ---- live sections (SAVE + key, ui_layers.c). A section is a project slot (A..D = 1..4): stored into RAM
 * at once (playing too), written to flash once the transport is stopped and nothing sounds (an erase
 * stops the audio for ~50 ms); a song recorded with SONG REC is saved the same way. */
static void section_store(uint32_t s)
{
    s &= 3u;
    fm1_irq_off();                                      /* (the audio ISR may be applying a section) */
    proj_capture(&proj_slot[s]);
    live_sec = (int8_t)s;
    fm1_irq_on();
    sec_dirty |= (uint8_t)(1u << s);
}
static void section_load(uint32_t s)                    /* stopped: the section is the loop now */
{
    s &= 3u;
    project_apply(&proj_slot[s]);
    live_sec = (int8_t)s;
}
static void sections_write(void)                        /* the dirty sections and song into flash */
{
    uint32_t i;
#if FELUCCA_FLASH
    if (flash_ok)
        for (i = 0; i < 4u; i++)
            if (((sec_dirty >> i) & 1u) && st_save(OBJ_PROJECT0 + i, &proj_slot[i], sizeof proj_slot[i]) == 0) {
                MOTION_SAVED(OBJ_PROJECT0 + i, &proj_slot[i]);
                sec_dirty &= (uint8_t)~(1u << i);       /* (a failed write stays dirty: tried again later) */
            }
    if (!flash_ok)
#endif
        sec_dirty = 0;
    (void)i;
    if (song_dirty) {
        song_dirty = 0;
        settings_save();
    }
}
/* before an intentional reset (an update, UPDATE MODE, UBOOT from the host): the audio is stopped, so
 * whatever is only in RAM goes to flash now: the live sections, the song, the working project */
static void persist_flush_now(void)
{
    sections_write();
#if FELUCCA_FLASH
    if (flash_ok && !arrangement_clock.running) {     /* (a song playing: the tracks hold a section) */
        proj_capture(&autosave_buf);
        if ((autosave_buf.sum ^ MOTION_HASH()) != autosave_hash &&
            st_save(OBJ_AUTOSAVE, &autosave_buf, sizeof autosave_buf) == 0) {
            MOTION_SAVED(OBJ_AUTOSAVE, &autosave_buf);
            autosave_hash = autosave_buf.sum ^ MOTION_HASH();
        }
    }
#endif
}
static void sections_flush(void)                        /* main loop */
{
    static uint32_t tried;
    if (srec_done) {
        song_dirty = srec_done != 0xFFu;
        if (song_dirty) {
            char b[8];
            fmt_int(b, srec_done);
            ui_say("SONG PARTS ", b);
        } else {
            ui_message("NO SONG");
        }
        srec_done = 0;
    }
    if (settings_later) {                               /* the menu closed while playing */
        settings_later = 0;
        song_dirty = 1;                                 /* (settings_save when quiet, with the song) */
    }
    if ((!sec_dirty && !song_dirty) || song.playing || transport_req || !audio_quiet() || fm1_ms - ui_input_ms < 1500u ||
        fm1_ms - tried < 5000u)
        return;
    tried = fm1_ms;                                     /* (a failed write: again in 5 s, not every frame) */
    sections_write();
    if (sec_dirty)
        ui_message("SAVE ERROR: RETRYING");
}
#endif
#endif /* PROJ_HOST */
