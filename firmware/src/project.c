/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Projects: four slots in .noinit RAM (the song sections A..D), so they survive resets and UBOOT
 * entry. With FELUCCA_FLASH every save also goes to flash through storage.c, and an empty RAM slot
 * is filled from flash on load. The working project is also kept in flash by itself (autosave, when
 * the transport is stopped and nothing sounds) and comes back at power-on: SLOOP starts where you
 * left it.
 *
 * Formats (the integrated build, FELUCCA_ANALOG2: format 9, see below; without ANALOG 2 as here):
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
 * PROJ_HOST needs core.h, params.c (TP), drums.c (the lanes), the engines and trk_def_engine (ui.c).
 *
 * The drum record (format 10, "FUNA"): a project's drum lanes and their sends (drums.c drum_sends.c dlrec_t, 236 B) are
 * no part of project_t: in RAM each project_t has its own (proj_dl[] for the slots, autosave_dl, the
 * song's), in flash they are a record of their own (drum_store.c), named by the project's dl_hash (its
 * content key; 0 = every lane the kit as it is, its sends as they are: no record). proj_capture / proj_apply take both;
 * proj_ok of a slot holds only while proj_dl[slot] is the record its dl_hash names (every write keeps it).
 * Since 2026-10 a project's G_DRREV is DRREV_MOVED (-1): the lanes' REV took GLO > DRUMS REV (drum_sends.c); an
 * older project's G_DRREV becomes its TRK lanes' REV when it is applied (proj_apply). No format change. */
#if FELUCCA_ANALOG2
/* ANALOG 2 + FM6 (the integrated build): format 7 ("FUN7", written): the 8 ANALOG 2 parameters just before
 * P_E0 (core.h P_A2WAVE..), the FM6 parts' voices after the tracks, FM6 = engine 9 and no SUPER engine.
 * Two test branches both wrote a "FUN6" (told apart by their size): ANALOG 2's (P_COUNT parameters, FM6's
 * numbering, no FM6 voices) and FM6's (format 5 + the FM6 voices: SUPER 9, FM6 10). Formats 6, 5 and 4 are
 * read by count (proj_from_np: the parameters added since take their defaults); the formats that numbered
 * SUPER 9 and DX7 / FM6 10 (FM6's 6, 5, 4) get today's numbers: DX7 / FM6 -> FM6 (9), SUPER -> ANALOG on
 * the swarm (proj_trk_from_super) */
#define PROJ_MAGIC 0x46554E42u                 /* "FUNB" (format 11): format 10 with ENV2's extras packed in the drum
                                                * track's ANALOG 2 slots (pj_x: SUS2 REL2 and the destinations' amounts
                                                * PIT SHP OSC2 SDTN, a byte each); the same size */
#define PROJ_MAGIC_VA 0x46554E41u              /* "FUNA" (format 10): format 9's tracks (PJ_NP parameters each, ENV2's
                                                * SUS2 REL2 DST2 in the drum track's ANALOG 2 slots, a word each),
                                                * 10-byte steps, the FM6 voices, and the drum record's key
                                                * (drum_store.c) for the lanes; read and converted (proj_va_fix) */
#define PROJ_MAGIC_V9 0x46554E39u              /* "FUN9": format 10 with the drum lanes inline (laid out as format 8);
                                                * read only: its lanes become a drum record, ENV2 is converted */
#define PROJ_MAGIC_V8 0x46554E38u              /* "FUN8": format 9 without ENV2's SUS2 REL2 DST2; read only */
#define PROJ_NP_V8 69u                         /* P_COUNT of formats 6 (ANALOG 2's), 7 and 8 */
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
#if FELUCCA_ANALOG2
/* ENV2's SUS2, REL2 and its amounts on PIT, SHP, OSC2, SDTN (core.h P_A2ESUS..P_A2ESDT, the last ANALOG 2 values;
 * FLT's is P_A2FENV, stored with the track) are not among a track's stored values: the project kept its size,
 * within one flash sector's payload (storage.c ST_PAYLOAD_MAX). A synth part's six sit in the drum track's stored
 * ANALOG 2 slots, which the drum track has no use for, a byte each (core.h a2x_pack): part k's three words at
 * P_A2WAVE + 3 k (pj_x). Format 10 (FUNA) kept SUS2 REL2 DST2 there, a word each (proj_va_fix). A track's stored
 * values: P_LEVEL .. P_A2SDTN, then P_E0 .. P_E7 (PJ_E0 ..) */
#define PROJ_XN A2X_N                                /* values not stored with their track */
#define PROJ_XW A2X_W                                /* their words in the drum track, a part's */
#else
#define PROJ_XN 0u
#endif
#define PJ_NP (P_ENG_END - PROJ_XN)                  /* a track's stored values (SLOOP 2.4's after P_E7: px_pack) */
#define PJ_E0 (P_E0 - PROJ_XN)                       /* where its P_E0 .. P_E7 are */
#if FELUCCA_ANALOG2
_Static_assert(PJ_NP == 69u && PJ_E0 == P_A2ESUS && P_A2WAVE + NPART * PROJ_XW <= PJ_E0,
               "FUNB: a track's stored values as format 10 (FUN8's P_COUNT); the parts' extras in the drum track's ANALOG 2 slots");
#endif
typedef struct {                               /* one track; the drum track ignores engine / preset */
    int16_t p[PJ_NP];
    uint8_t engine, preset;
    union {
        step_t step[NSTEP];
        dstep_t dstep[NSTEP];                  /* (the drum track: 16 lanes, the same size) */
    };
} proj_trk_t;
typedef struct {
    uint32_t magic, size;
    int16_t g[PJ_NG];                         /* (the G_* up to PJ_NG; the master COMP: mc_pack) */
    uint8_t sel, rsv[3];                       /* the selected track */
    proj_trk_t t[NTRK];
    uint8_t fm6[NPART][128];                   /* the FM6 parts' voices, DX7 packed (fm6_has: which) */
    uint8_t fm6_on[NPART], fm6_has;            /* their operator switches (bit n - 1: OP n); bit k: part k */
    int8_t fm6_fn[NPART][16];                  /* the parts' FM6 functions (FN_PBUP..), [0] < 0: defaults */
#if FELUCCA_ANALOG2
    uint32_t dl_hash;                          /* format 10: its drum record's key (dlrec_hash), 0 = the kit as it is */
#endif
    uint32_t sum;
} project_t;
#if !FELUCCA_ANALOG2
typedef struct {                               /* format 5 (SLOOP 2.3 .. plus), read only */
    uint32_t magic, size;
    int16_t g[PJ_NG];
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
    int16_t g[PJ_NG];                            /* (G_COUNT has not changed since: G_VIEW took the ROUT slot) */
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
#if !SEC_LOGGED                               /* (FELUCCA_SECTIONS 8 / 16: sections.c, the sections in a log) */
project_t proj_slot[4] __attribute__((section(".noinit")));
/* the slots' drum records (RAM, not .noinit: four would not fit there; after a reset drum_store.c finds them
 * again by the slots' dl_hash) */
static dlrec_t proj_dl[4];
#endif

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
/* the longest song-section record (sec_codec.c SEC_REC_MAX, the same in every build): the flags byte, a full motion
 * chunk (count, PLAY bits, 64 x 3 bytes), the raw project less its magic, size and sum, the drum record. proj_tmp
 * (and the tests' copies of it) receive one */
#define SEC_REC_N (1u + 2u + 3u * 64u + (uint32_t)sizeof(project_t) - 12u + (uint32_t)sizeof(dlrec_t))

/* a track's stored values (PJ_NP) <-> today's P_* (P_COUNT): ENV2's extras (SUS2 REL2, the amounts but FLT's) are
 * not among them (their defaults here; pj_x keeps them) */
static void pj_to_p(int16_t *p, const int16_t *s)
{
    uint32_t k;
    for (k = 0; k < PJ_E0; k++)
        p[k] = s[k];
    for (k = PJ_E0; k < P_E0; k++)
        p[k] = TP[k].def;
    for (k = 0; k < 8u; k++)
        p[P_E0 + k] = s[PJ_E0 + k];
#if SL24_TP
    for (k = P_ENG_END; k < P_COUNT; k++)                /* (SLOOP 2.4's: px_unpack) */
        p[k] = TP[k].def;
#endif
}
static void pj_from_p(int16_t *d, const int16_t *p)
{
    uint32_t k;
    for (k = 0; k < PJ_E0; k++)
        d[k] = p[k];
    for (k = 0; k < 8u; k++)
        d[PJ_E0 + k] = p[P_E0 + k];
}
#if FELUCCA_ANALOG2
static int16_t *pj_x(project_t *q, uint32_t part) { return &q->t[TRK_DRUM].p[P_A2WAVE + PROJ_XW * part]; }
/* a project read from an older format: the parts' ENV2 SUS2 REL2 and amounts at 0, the sound as it was (the AD
 * envelope of the cutoff) */
static void pj_x_reset(project_t *q)
{
    uint32_t k;
    for (k = 0; k < NPART; k++)
        memset(pj_x(q, k), 0, PROJ_XW * sizeof(int16_t));
    q->sum = proj_sum(q);
}
/* the last project converted from format 10's ENV2 (proj_va_fix): its sum before and after, and where each part's
 * AMT2 went (A2E_*, 0: it stayed FLT's): a motion store of the old project follows it (motion_flash_read) */
static struct { uint32_t from, to; int8_t dst[NPART]; } proj_va;
/* q, laid out as format 10 (FUNA, FUN9; a section record without SEC_V2): its parts' SUS2 REL2 DST2 (a word each)
 * -> FUNB's bytes, DST2 with AMT2 -> the amount on that destination (core.h a2x_from_dst): the same sound. q
 * becomes a valid FUNB project; dst (NPART, may be 0) gets where each AMT2 went */
static void proj_va_fix(project_t *q, int8_t *dst)
{
    uint32_t k;
    for (k = 0; k < NPART; k++) {
        int16_t *w = pj_x(q, k), x[A2X_N], amt = (int16_t)clamp(q->t[k].p[P_A2FENV], -64, 63);
        int32_t d = clamp(w[2], A2E_CUT, A2E_SDTN);
        x[0] = (int16_t)clamp(w[0], 0, 127);                  /* SUS2, REL2: as a load clamps them */
        x[1] = (int16_t)clamp(w[1], 0, 127);
        a2x_from_dst(x, &amt, d);
        q->t[k].p[P_A2FENV] = amt;
        a2x_pack(w, x);
        if (dst)                                              /* (AMT2 moved: to d; none moved: CUT) */
            dst[k] = (int8_t)(d != A2E_CUT && x[1 + d] ? d : A2E_CUT);
    }
    q->magic = PROJ_MAGIC;
    q->size = sizeof *q;
    q->sum = proj_sum(q);
}
/* n bytes of a format 10 project (FUNA) -> slot q as FUNB (its old and new sums: proj_va) */
static int proj_from_va(project_t *q, const void *b, int n)
{
    const project_t *a = (const project_t *)b;
    if (n != (int)sizeof *q || a->magic != PROJ_MAGIC_VA || a->size != sizeof *q || a->sum != proj_sum(a))
        return 0;
    memcpy(q, b, sizeof *q);
    proj_va.from = a->sum;
    proj_va_fix(q, proj_va.dst);
    proj_va.to = q->sum;
    return 1;
}
#endif

#if FELUCCA_MOTION
#include "motion_proj.c"       /* each project buffer's motion store (motion.c) */
#endif
#if FELUCCA_SL24_XSTEP
#include "stepx.h"             /* SLOOP 2.4's step extras: nudge, locks, fills */
#include "stepx_proj.c"        /* the working extras, each project buffer's store */
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
    for (i = 0; i < PJ_NG; i++)
        g[i] = i < PROJ_NG_V3 ? g2[i] : GP[i].def;
    g[G_SWING] = swing_from_v3(g[G_SWING]);
}

/* a track of format 3 -> today's (by id up to P_SLDEPTH; P_E0.. moved) */
static void proj_trk_from_v3(proj_trk_t *d, const proj_trk_v3_t *s, int drum)
{
    uint32_t k, nc = PROJ_NP_V3 - 8u;
    for (k = 0; k < PJ_E0; k++)
        d->p[k] = k < nc ? s->p[k] : TP[k].def;
    for (k = 0; k < 8u; k++)
        d->p[PJ_E0 + k] = s->p[nc + k];
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
        for (k = 0; k < PJ_NP; k++)
            q->t[i].p[k] = k >= PJ_E0 ? ENGINES[trk_def_engine(i)]->edit[k - PJ_E0].def : TP[k].def;
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

/* a format 7 project (format 10 without dl_hash) -> slot q: the lanes as the kit (0) */
#define PROJ_V7_N ((uint32_t)__builtin_offsetof(project_t, dl_hash))
#define PROJ_V8_N (PROJ_V7_N + (uint32_t)sizeof(dlanes_t) + 4u)   /* format 8: format 7 + the lanes inline */
_Static_assert(PROJ_V7_N == 3632u && sizeof(project_t) == PROJ_V7_N + 8u && PROJ_V8_N == 3840u,
               "FUN7 + the drum record's key; FUN8: FUN7 + the drum lanes");
static int proj_from_v7(project_t *q, const void *b, int n)
{
    const uint8_t *c = (const uint8_t *)b;
    if (n != (int)(PROJ_V7_N + 4u) || ((const uint32_t *)b)[0] != PROJ_MAGIC_V7 ||
        ((const uint32_t *)b)[1] != PROJ_V7_N + 4u || *(const uint32_t *)(c + PROJ_V7_N) != proj_hash(b, PROJ_V7_N))
        return 0;
    memcpy(q, b, PROJ_V7_N);
    q->dl_hash = 0;
    q->magic = PROJ_MAGIC;
    q->size = sizeof *q;
    q->sum = proj_sum(q);
    return 1;
}
/* a format 8 or 9 project's drum lanes (at PROJ_V7_N) as a drum record (sends: TRK / 0), 0 = neither (format 9,
 * feat/ui-overview2's, has format 8's layout: its ENV2 values in the drum track's slots are kept as they are) */
static int proj_v8_ok(const void *b, int n)
{
    const uint8_t *c = (const uint8_t *)b;
    return n == (int)PROJ_V8_N && (((const uint32_t *)b)[0] == PROJ_MAGIC_V8 || ((const uint32_t *)b)[0] == PROJ_MAGIC_V9) &&
           ((const uint32_t *)b)[1] == PROJ_V8_N &&
           *(const uint32_t *)(c + PROJ_V8_N - 4u) == proj_hash(b, PROJ_V8_N - 4u);
}
static void proj_v8_dl(dlrec_t *d, const void *b)
{
    memset(d, 0, sizeof *d);
    memcpy(&d->l, (const uint8_t *)b + PROJ_V7_N, sizeof d->l);
    dlrec_fix(d);
}
/* a format 8 or 9 project -> slot q (its lanes: proj_import_dl) */
static int proj_from_v8(project_t *q, const void *b, int n)
{
    dlrec_t d;
    if (!proj_v8_ok(b, n))
        return 0;
    memcpy(q, b, PROJ_V7_N);
    proj_v8_dl(&d, b);
    q->dl_hash = dlrec_hash(&d);
    q->magic = PROJ_MAGIC;
    q->size = sizeof *q;
    q->sum = proj_sum(q);
    return 1;
}

/* a track that played SUPER (formats 4..6 of FM6's numbering) -> ANALOG on the swarm (params.c) */
static void proj_trk_from_super(proj_trk_t *d)
{
    int16_t v[P_COUNT];
    pj_to_p(v, d->p);
    d->preset = (uint8_t)analog2_from_super(v, d->preset);
    pj_from_p(d->p, v);
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
        for (k = 0; k < PJ_NP; k++) {
            if (k >= PJ_E0 || k < nc)
                memcpy(&d->p[k], s + 2u * (k >= PJ_E0 ? nc + k - PJ_E0 : k), 2);
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
    if (proj_from_va(q, b, n))                                           /* FUNA: ENV2's one destination */
        return 1;
    if (proj_from_v8(q, b, n)) {                                         /* FUN8 / 9: lanes inline (record) */
        if (((const uint32_t *)b)[0] == PROJ_MAGIC_V8)
            pj_x_reset(q);                                                /* (FUN8: no ENV2 extras yet) */
        else
            proj_va_fix(q, 0);                                            /* (FUN9: FUNA's ENV2 words; no motion) */
        return 1;
    }
    if (proj_from_v7(q, b, n) ||                                          /* FUN7: no drum lanes */
           proj_from_np(q, b, n, PROJ_MAGIC_V6, PROJ_NP_V8, 0, 0) ||       /* ANALOG 2's FUN6 */
           proj_from_np(q, b, n, PROJ_MAGIC_V6, PROJ_NP_V5, PROJ_FM6_N, 1) || /* FM6's FUN6 */
           proj_from_np(q, b, n, PROJ_MAGIC_V5, PROJ_NP_V5, 0, 1) || proj_from_np(q, b, n, PROJ_MAGIC_V4, PROJ_NP_V4, 0, 1) ||
           proj_from_v3(q, (const project_v3_t *)b, n) || proj_from_v2(q, (const project_v2_t *)b, n) ||
           proj_from_v1(q, (const project_v1_t *)b, n)) {
        pj_x_reset(q);                                /* (their drum track's ANALOG 2 slots: not ENV2's) */
        return 1;
    }
    return 0;
#else
    return proj_from_v5(q, (const project_v5_t *)b, n) || proj_from_v4(q, (const project_v4_t *)b, n) ||
           proj_from_v3(q, (const project_v3_t *)b, n) || proj_from_v2(q, (const project_v2_t *)b, n) ||
           proj_from_v1(q, (const project_v1_t *)b, n);
#endif
}

#if FELUCCA_SL24_SAFE || FELUCCA_SL24_IMPORT
#if FELUCCA_SL24_IMPORT
#include "stepx.h"             /* (2.4's step extras) */
#endif
#if SL24_TP
static void px_pack(project_t *p, const int16_t (*x)[3]);   /* (below: SLOOP 2.4's track values) */
#endif
#include "sl24_import.c"       /* SLOOP 2.4's projects: told apart (sl24_is), imported when asked (proj_from_sl24) */
#endif

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
    memcpy(o->e, &s->p[PJ_E0], sizeof o->e);          /* (stored: PJ_E0 .., project.c pj_x) */
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
    memcpy(&d->p[PJ_E0], o->e, sizeof o->e);
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

/* the drum record that comes with n bytes of a stored project (proj_import): format 8's lanes, else none (all
 * 0; format 10: drum_store.c finds it by the project's dl_hash) */
static void proj_import_dl(dlrec_t *d, const void *b, int n)
{
#if FELUCCA_ANALOG2
    if (proj_v8_ok(b, n)) {
        proj_v8_dl(d, b);
        return;
    }
#endif
    (void)b, (void)n;
    memset(d, 0, sizeof *d);
}

/* ---- the working project <-> a project_t and its drum record d */
/* track k's values as a project keeps them, by parameter id (v[P_COUNT]): its patch (with motion: the values
 * under the motion, motion_proj.c) */
static void proj_patch(int16_t *v, uint32_t k)
{
#if FELUCCA_MOTION
    motion_base_params(v, k);
#else
    memcpy(v, trk[k].p, sizeof trk[k].p);
#endif
}
/* The master COMP / LIMIT (core.h G_CTHR .. G_CCEIL, master_comp.c) in the project's reserved bytes, which every
 * project written so far left 0 (proj_capture clears it first): 0 = THRS OFF, CEIL OFF, the master as before.
 *   rsv[1]  bits 0..4 THRS (-30..0, two's complement), 5..7 RATIO (0..7)
 *   rsv[2]  bits 0..2 ATK (0..5), 3..5 REL (0..6), 6..7 GAIN's bits 0..1
 *   rsv[0]  bits 0..2 the reverb's algorithm (rev_type.c rev_pack), 3..4 GAIN's bits 2..3 (0..15), 5..7 CEIL (0..7)
 * Each field holds the value minus its default (modulo the field): a 0 field is the default, so a project from
 * before has every one at its default (RATIO 2:1, ATK 10 ms, REL AUTO) with THRS and CEIL OFF.
 * Every reserved bit is now taken: a sidechain SOURCE (master_comp.h) needs a format change.
 * Kept in every build (a build without MASTER_COMP keeps the values it loads; miss.c says they are not heard). */
static const uint8_t MC_SHIFT[6] = {0, 5, 8, 11, 14, 18}, MC_MASK[6] = {31, 7, 7, 7, 15, 7};   /* (w: rsv[1], rsv[2], rsv[0] >> 3) */
static void mc_pack(project_t *p)
{
    uint32_t w = 0, i;
    for (i = 0; i < 6u; i++)
        w |= ((uint32_t)(song.g[G_CTHR + i] - GP[G_CTHR + i].def) & MC_MASK[i]) << MC_SHIFT[i];
    p->rsv[1] = (uint8_t)w;
    p->rsv[2] = (uint8_t)(w >> 8);
    p->rsv[0] = (uint8_t)((p->rsv[0] & 7u) | (w >> 16) << 3);   /* (bits 0..2: rev_pack, written before) */
}
static void mc_unpack(const project_t *p)
{
    uint32_t w = p->rsv[1] | (uint32_t)p->rsv[2] << 8 | (uint32_t)(p->rsv[0] >> 3) << 16, i;
    for (i = 0; i < 6u; i++) {
        const param_desc_t *d = &GP[G_CTHR + i];
        int32_t v = (int32_t)(((w >> MC_SHIFT[i]) + (uint32_t)d->def) & MC_MASK[i]);
        song.g[G_CTHR + i] = (int16_t)clamp(v > d->max ? v - MC_MASK[i] - 1 : v, d->min, d->max);   /* (THRS: < 0) */
    }
}

#if SL24_TP
/* SLOOP 2.4's track values (core.h P_TFLT P_STRUM P_VLEAD: each track's FILT, the parts' STRUM and VLEAD; x[k][0..2])
 * in the drum track's stored slots that it never reads (no format change: as the macros' MAC_ID and pj_x), and only
 * when one of them is not 0 (its default: a project without them is byte for byte one of before). Then the drum
 * track's SDTN slot (P_A2SDTN, the word after pj_x's, which every other build writes as its default, 34) holds PX_TAG
 * and the three VLEAD bits, and its GLMOD PRIO ALLOC DTUNE slots the values, 7 bits each (two's complement): GLMOD the
 * FILT of tracks 1 and 2, PRIO tracks 3 and 4, ALLOC the STRUM of parts 1 and 2, DTUNE part 3's. Without the tag
 * (every project before; one saved by a build without the switches, which keeps the drum's own values there) all
 * are 0. Without ANALOG 2 they are not kept. */
#define PX_TAG 0x5A00u
#if FELUCCA_ANALOG2
_Static_assert(P_A2SDTN < PJ_E0 && P_A2WAVE + NPART * PROJ_XW <= P_A2SDTN && P_DETUNE < PJ_E0 &&
               P_STRUM == P_TFLT + 1 && P_VLEAD == P_TFLT + 2, "px: the drum track's free slots, the values' order");
static const uint8_t PX_SLOT[4] = {P_GLMODE, P_PRIO, P_ALLOC, P_DETUNE};
static uint32_t px7(int32_t v) { return (uint32_t)v & 0x7Fu; }
static int16_t px7v(uint32_t w) { return (int16_t)((int32_t)((w & 0x7Fu) ^ 0x40u) - 0x40); }
static void px_pack(project_t *p, const int16_t (*x)[3])
{
    int16_t *d = p->t[TRK_DRUM].p;
    uint32_t k, any = 0;
    for (k = 0; k < NTRK; k++)
        any |= (uint32_t)(x[k][0] | (k < NPART ? x[k][1] | x[k][2] : 0));
    if (!any)
        return;
    d[P_A2SDTN] = (int16_t)(PX_TAG | (x[0][2] != 0) | (uint32_t)(x[1][2] != 0) << 1 | (uint32_t)(x[2][2] != 0) << 2);
    d[P_GLMODE] = (int16_t)(px7(x[0][0]) | px7(x[1][0]) << 7);
    d[P_PRIO] = (int16_t)(px7(x[2][0]) | px7(x[3][0]) << 7);
    d[P_ALLOC] = (int16_t)(px7(x[0][1]) | px7(x[1][1]) << 7);
    d[P_DETUNE] = (int16_t)px7(x[2][1]);
}
/* -> x (0 without the tag); 1 when the project has them (its drum slots are not the drum track's then) */
static int px_unpack(const project_t *p, int16_t (*x)[3])
{
    const int16_t *d = p->t[TRK_DRUM].p;
    uint32_t w = (uint16_t)d[P_A2SDTN];
    memset(x, 0, NTRK * sizeof *x);
    if ((w & 0xFF00u) != PX_TAG)
        return 0;
    x[0][0] = px7v((uint16_t)d[P_GLMODE]), x[1][0] = px7v((uint16_t)d[P_GLMODE] >> 7);
    x[2][0] = px7v((uint16_t)d[P_PRIO]), x[3][0] = px7v((uint16_t)d[P_PRIO] >> 7);
    x[0][1] = px7v((uint16_t)d[P_ALLOC]), x[1][1] = px7v((uint16_t)d[P_ALLOC] >> 7);
    x[2][1] = px7v((uint16_t)d[P_DETUNE]);
    x[0][2] = (int16_t)(w & 1u), x[1][2] = (int16_t)(w >> 1 & 1u), x[2][2] = (int16_t)(w >> 2 & 1u);
    return 1;
}
#else
static void px_pack(project_t *p, const int16_t (*x)[3]) { (void)p, (void)x; }
static int px_unpack(const project_t *p, int16_t (*x)[3]) { (void)p; memset(x, 0, NTRK * sizeof *x); return 0; }
#endif
#endif

#if FELUCCA_MIDI_CH
/* the synth tracks' MIDI channels in the project's G_MIDI slot (a status, never saved before: 0 in every older project,
 * and SLOOP 2.4's MIDI OUT flag 0 / 1 there, which reads as channel 1 for part 1: its own): five bits a track, 0 = the
 * default (part i + 1), 1..16 the channel, 17 OFF. The drum track's channel is the project's G_DRCH. */
static int16_t midi_ch_pack(void)
{
    uint32_t i, w = 0;
    for (i = 0; i < NPART; i++) {
        uint32_t c = (uint32_t)clamp(bp_set[BPS_CH0 + i], 0, 16), code = c == i + 1u ? 0u : c ? c : 17u;
        w |= code << (5u * i);
    }
    return (int16_t)w;
}
static void midi_ch_unpack(int16_t g)
{
    uint32_t i;
    for (i = 0; i < NPART; i++) {
        uint32_t code = ((uint32_t)(uint16_t)g >> (5u * i)) & 31u;
        bp_set[BPS_CH0 + i] = (int16_t)(code == 0u || code > 17u ? i + 1u : code == 17u ? 0u : code);
    }
}
#endif
static void proj_capture(project_t *p, dlrec_t *d)   /* what is playing now, as a project */
{
    int16_t v[P_COUNT];
    uint32_t i;
#if SL24_TP
    int16_t px[NTRK][3];                                /* (SLOOP 2.4's track values: px_pack) */
#endif
    memset(p, 0, sizeof *p);
    p->magic = PROJ_MAGIC;
    p->size = sizeof *p;
    for (i = 0; i < PJ_NG; i++)
        p->g[i] = i == G_MIDI ? 0 : i == G_DRREV ? DRREV_MOVED : song.g[i];   /* (G_MIDI: a status, not saved; G_DRREV:
                                                         * the lanes have their REV, drum_sends.c) */
#if FELUCCA_MIDI_CH
    p->g[G_MIDI] = midi_ch_pack();                      /* (G_MIDI's slot: the synth tracks' MIDI channels) */
#endif
    p->sel = song.sel;
    p->rsv[0] = rev_pack();                             /* the reverb's algorithm (rev_type.c; 0 ROOM): bits 0..2 */
    mc_pack(p);                                         /* the master COMP / LIMIT (above): rsv[0] bits 3..7, rsv[1], rsv[2] */
    for (i = 0; i < NTRK; i++) {
        proj_patch(v, i);
        pj_from_p(p->t[i].p, v);
#if SL24_TP
        memcpy(px[i], &v[P_TFLT], sizeof px[i]);
#endif
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
    dlrec_capture(d);                                   /* the drum lanes and sends (kept in every build) */
    p->dl_hash = dlrec_hash(d);
    for (i = 0; i < NPART; i++) {                       /* the parts' ENV2 SUS2 REL2 and amounts (pj_x) */
        proj_patch(v, i);
        a2x_pack(pj_x(p, i), &v[P_A2ESUS]);
    }
#else
    (void)d;
#endif
#if SL24_TP
    px_pack(p, px);                                     /* (after the drum track's values: in its free slots) */
#endif
    p->sum = proj_sum(p);
#if FELUCCA_MOTION
    motion_capture_store(p);
#endif
#if FELUCCA_SL24_XSTEP
    sx_capture_store(p);                                /* its step extras (stepx_proj.c) */
#endif
}

/* a project's tracks (and its globals, all: a load; or only the drum level: a song section) into the
 * working one, every value back inside its range (a project from before the lanes had their own REV: its
 * G_DRREV into its TRK lanes, drum_sends.c dlrec_migrate). The audio ISR must not run meanwhile (the song
 * sections: called from it; a load: IRQ off) */
static void proj_apply(const project_t *p, const dlrec_t *d, int all)
{
    uint32_t i, k;
    int16_t v[P_COUNT];
#if SL24_TP
    int16_t px[NTRK][3];
    int pxt = px_unpack(p, px);                         /* SLOOP 2.4's track values (px_pack) */
#endif
#if FELUCCA_MOTION
    motion_apply_store(p);                              /* its motion, if it is this project's (motion_proj.c) */
#endif
#if FELUCCA_SL24_XSTEP
    sx_apply_store(p);                                  /* its step extras, if they are this project's (stepx_proj.c) */
#endif
    undo_clear();                                       /* (undo.c: the history was of other steps) */
    for (i = 0; i < PJ_NG; i++)
        if (all ? i != G_SLOT && i != G_LOAD && i != G_SAVE && i != G_VIEW && i != G_MIDI && i != G_DRREV : i == G_DRLVL)
            song.g[i] = (int16_t)clamp(p->g[i], GP[i].min, GP[i].max);
    if (all)
        rev_unpack(p->rsv[0]);                          /* the reverb's algorithm (rev_type.c; older projects: 0 ROOM) */
    if (all)
        mc_unpack(p);                                   /* (older projects: 0, COMP and LIMIT off) */
#if FELUCCA_MIDI_CH
    if (all)
        midi_ch_unpack(p->g[G_MIDI]);                   /* the synth tracks' channels (older projects: 0, the defaults) */
#endif
    for (k = 0; k < NTRK; k++) {
        track_t *t = &trk[k];
        const proj_trk_t *s = &p->t[k];
        uint32_t e = k < NPART ? eng_slot(s->engine) : 0u;   /* UID -> slot (not built: its fallback) */
        t->eng_req = (uint8_t)e;
        t->user = 0;                                    /* (no user preset slot is saved) */
        pj_to_p(v, s->p);
#if FELUCCA_ANALOG2
        if (k == TRK_DRUM)                              /* (its ANALOG 2 slots hold the parts' ENV2 values) */
            for (i = P_A2WAVE; i < P_E0; i++)
                v[i] = TP[i].def;
        else
            a2x_unpack(&v[P_A2ESUS], &p->t[TRK_DRUM].p[P_A2WAVE + PROJ_XW * k]);
#endif
#if SL24_TP
        memcpy(&v[P_TFLT], px[k], sizeof px[k]);
#if FELUCCA_ANALOG2
        if (k == TRK_DRUM && pxt)                       /* (its slots that held them: the drum track's defaults) */
            for (i = 0; i < 4u; i++)
                v[PX_SLOT[i]] = TP[PX_SLOT[i]].def;
#endif
        (void)pxt;
#endif
        for (i = 0; i < P_COUNT; i++) {                 /* every value back inside its range */
            const param_desc_t *d = k == TRK_DRUM && i == P_E0 ? &DRUM_KIT_DESC :   /* the drum kit */
                                    i >= P_E0 && i <= P_E7 ? &ENGINES[e]->edit[i - P_E0] : &TP[i];
            t->p[i] = (int16_t)clamp(v[i], d->min, d->max);
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
    dlrec_apply(p->dl_hash ? d : 0, p->g[G_DRREV]);    /* the drum lanes and sends, for the kit they were set on
                                                         * (older projects: G_DRREV into the TRK lanes) */
    dl_e0 = TDRUM->p[P_E0];
#else
    (void)d;
#endif
    MISS_BUMP();                                        /* the main loop says what this build lacks (miss.c) */
}

#ifndef PROJ_HOST
#if FELUCCA_ARRANGER
#include "arranger_scene.c"
#endif
static uint16_t sec_dirty;                     /* live sections in RAM, not yet in flash (bit per section) */
static uint8_t song_dirty;                      /* the song: in RAM, not yet in flash */
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
#if FELUCCA_SL24_XSTEP
#define SX_HASH() sx_hash()
#else
#define SX_HASH() 0u
#endif
#if FELUCCA_FLASH
/* slot from flash into RAM (format 4, or an old one converted) */
static union {
    project_t cur;                             /* (today's format: FUNA; FUN6 without ANALOG 2) */
#if FELUCCA_ANALOG2
    uint8_t v8[PROJ_V8_N];                     /* FUN8 / FUN9 (the lanes inline) */
#endif
#if !FELUCCA_ANALOG2
    project_v5_t v5;
#endif
    project_v4_t v4old;
    project_v3_t v3;
    project_v2_t v2;
    project_v1_t v1;
#if SEC_LOGGED || FELUCCA_SNAPSHOTS
    uint8_t rec[SEC_REC_N];                    /* a section record, its motion too (sec_codec.c; ed_backup.c; snapshots.c) */
#endif
} proj_tmp;
#include "drum_store.c"        /* the drum records' own flash record; proj_put / proj_get */
#if !SEC_LOGGED
static void proj_fetch(uint32_t slot)
{
    project_t *q = &proj_slot[slot & 3u];
    if (!proj_get(OBJ_PROJECT0 + (slot & 3u), q, &proj_dl[slot & 3u]))
        q->magic = 0;
    else
        MOTION_READ(OBJ_PROJECT0 + (slot & 3u), q);
}
#endif
#endif

#if !SEC_LOGGED
static void project_save(uint32_t slot)
{
    project_t *p = &proj_slot[slot & 3u];
#if FELUCCA_ARRANGER
    if (song.playing || transport_req) { ui_message("STOP BEFORE SAVE"); return; }
#endif
    proj_capture(p, &proj_dl[slot & 3u]);
#if FELUCCA_FLASH
    if (flash_ok) {
#if FELUCCA_MOTION
        if (proj_put(OBJ_PROJECT0 + (slot & 3u), p, &proj_dl[slot & 3u])) {
            ui_message("SAVE ERROR");
            return;
        }
        MOTION_SAVED(OBJ_PROJECT0 + (slot & 3u), p);
        ui_message("SAVED");
#else
        ui_message(proj_put(OBJ_PROJECT0 + (slot & 3u), p, &proj_dl[slot & 3u]) ? "SAVE ERROR" : "SAVED");
#endif
        return;
    }
#endif
    ui_message("SAVED (RAM)");
}
#endif

/* a project into the working one: the transport stops, everything sounding is released */
static void project_apply(const project_t *p, const dlrec_t *d)
{
    uint32_t k;
    transport_req = 2;
    panic_req = (1u << NTRK) - 1u;
    fm1_irq_off();                                      /* the audio ISR must not see half a project */
    proj_apply(p, d, 1);
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

#if FELUCCA_SL24_SAFE
static void sl24_load(uint32_t slot);         /* sl24_guard.c: a slot of another firmware's */
#endif
#if SEC_LOGGED
static void settings_save(void);
#include "sections.c"          /* FELUCCA_SECTIONS 8 / 16: the sections in a log (A..P), staged for the ISR */
#else
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
#if FELUCCA_SL24_SAFE
        sl24_load(slot);
#else
        ui_message("EMPTY SLOT");
#endif
        return;
    }
    project_apply(p, &proj_dl[slot & 3u]);
    ui_message("LOADED");
}
#endif

/* ---- the working project, kept in flash by itself: saved when it changed, the transport is stopped,
 * nothing sounds and the panel was not touched for AUTOSAVE_IDLE (a flash erase stops the audio for
 * ~50 ms: never while something plays); loaded at power-on (autosave_resume) */
#define AUTOSAVE_IDLE 2500u                    /* ms without input */
#define AUTOSAVE_GAP 20000u                    /* ms between two saves at least */
static project_t autosave_buf __attribute__((section(".pool")));
static dlrec_t autosave_dl;                    /* its drum record */
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
#if DRUM_X0X
    if (x0x_sounding())                         /* (the X0X kits' channels) */
        return 0;
#endif
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
    proj_capture(&autosave_buf, &autosave_dl);
    h = autosave_buf.sum ^ MOTION_HASH() ^ SX_HASH();   /* (the sum covers the drum record: dl_hash) */
    if (h == autosave_hash || !audio_quiet())
        return;
    if (proj_put(OBJ_AUTOSAVE, &autosave_buf, &autosave_dl) == 0) {
        MOTION_SAVED(OBJ_AUTOSAVE, &autosave_buf);
#if FELUCCA_SL24_XSTEP && SEC_LOGGED
        (void)sx_log_put(SX_ID_AUTO, autosave_buf.sum, &autosave_buf, 0);   /* its step extras (stepx_log.c) */
#endif
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
    if (!proj_get(OBJ_AUTOSAVE, q, &autosave_dl))
        return;
#if FELUCCA_SL24_XSTEP && SEC_LOGGED
    sx_log_get(SX_ID_AUTO, q->sum, q);                  /* its step extras (stepx_log.c), applied with it */
#endif
#if FELUCCA_MOTION
    MOTION_READ(OBJ_AUTOSAVE, q);
    proj_apply(q, &autosave_dl, 1);
    autosave_hash = q->sum ^ MOTION_HASH();
#else
    autosave_hash = q->sum;
    proj_apply(q, &autosave_dl, 1);
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
    arr_rec_t arrangement;                         /* (16 parts; a longer song: the log, paired by its tag) */
#endif
    uint32_t view;                                 /* (appended: a shorter record, saved before it, reads as ALL) */
#if FELUCCA_BRIGHT || BP23_SET
    uint32_t bright;                               /* appended (bright.c): 0 = full, now always written 0 and never
                                                    * applied (a saved dim level froze an FM-1 at every boot: bright.c). A
                                                    * build with BP23_SET and no BRIGHT keeps it as read (its place) */
#endif
#if BP23_SET
    uint32_t bp23;                                 /* appended: the SLOOP 2.3 settings of the FM-1 (bp23_word) */
#endif
} persist_t;
#define PERSIST_NO_VIEW ((int)__builtin_offsetof(persist_t, view))   /* the record's length before view */
#if FELUCCA_BRIGHT || BP23_SET
#define PERSIST_NO_BRIGHT ((int)__builtin_offsetof(persist_t, bright))   /* .. before bright (a build without it) */
#endif
#if BP23_SET
/* the settings SLOOP 2.3 made settings of the FM-1 (not of a project), one word: bit 9 the REC screen's MODE
 * TEMPO, bit 10 its START COUNT (FELUCCA_REC_MODES); bits 0..3 LIGHTS, 4..7 KEYS, 8 NOTES OFF (FELUCCA_LIGHTS);
 * SLOOP 2.4's bit 14 MIDI OUT = SEQ, bit 15 MIDI IN = CLOCK (FELUCCA_MIDI_OUT, FELUCCA_MIDI_INCLK), bit 16 USB SERIAL
 * (FELUCCA_CDC: usb.c usb_serial, 0 = off, the console not presented).
 * 0 = as before (a record without the word reads as 0). A build without a switch keeps its bits as read */
static uint32_t bp23_kept;                         /* the bits this build has no switch for, as read */
static uint32_t bp23_word(void)
{
    uint32_t w = bp23_kept;
#if FELUCCA_REC_MODES
    w = (w & ~(3u << 9)) | (uint32_t)(rec_tempo != 0u) << 9 | (uint32_t)(rec_count != 0u) << 10;
#endif
#if FELUCCA_LIGHTS
    w = (w & ~0x1FFu) | lights_word();
#endif
#if FELUCCA_CDC
    w = (w & ~(1u << 16)) | (uint32_t)(usb_serial != 0u) << 16;
#endif
#if FELUCCA_MIDI_OUT
    w = (w & ~(1u << 14)) | (uint32_t)(bp_set[BPS_MOUT] != 0) << 14;     /* (SLOOP 2.4: the same bits) */
#endif
#if FELUCCA_MIDI_INCLK
    w = (w & ~(1u << 15)) | (uint32_t)(bp_set[BPS_MIN] != 0) << 15;
#endif
    return w;
}
static void bp23_from_word(uint32_t w)
{
    bp23_kept = w;
#if FELUCCA_REC_MODES
    rec_tempo = (uint8_t)((w >> 9) & 1u);
    rec_count = (uint8_t)((w >> 10) & 1u);
#endif
#if FELUCCA_LIGHTS
    lights_from_word(w);
#endif
#if FELUCCA_CDC
    usb_serial = (uint8_t)((w >> 16) & 1u);         /* (presented from the next start: usb_start) */
#endif
#if FELUCCA_MIDI_OUT
    bp_set[BPS_MOUT] = (int16_t)((w >> 14) & 1u);
#endif
#if FELUCCA_MIDI_INCLK
    bp_set[BPS_MIN] = (int16_t)((w >> 15) & 1u);
#endif
}
#endif
#if BP23_SET
/* the settings word changed by something that is no menu (a page, the editor): saved as the menu's are, at once when
 * stopped, else once the transport stops */
static void settings_poll(void)                    /* main loop */
{
    static uint32_t seen;
    static uint8_t known;
    uint32_t w = bp23_word();
    if (!known) {
        known = 1;
        seen = w;
        return;
    }
    if (w == seen)
        return;
    seen = w;
    if (song.playing || transport_req)
        settings_later = 1;
    else
        settings_save();
}
#endif
#if FELUCCA_ARRANGER
#define PERSIST_MAGIC 0x50455233u                  /* "PER3": includes the song order */
#else
#define PERSIST_MAGIC 0x50455232u
#endif
#if FELUCCA_FLASH
static persist_t persist_saved;
#endif
#if FELUCCA_ARRANGER
static uint16_t song_tag;                          /* the log's whole chain that goes with the settings record (sections.c) */
#endif

#if FELUCCA_SNAPSHOTS
static void sn_boot(void);                         /* snapshots.c */
#endif
#if FELUCCA_SL24_SAFE
#if !SEC_LOGGED                /* (with the log: sections.c includes it) */
#include "sl24_guard.c"        /* SLOOP 2.4's projects, autosave, FM6 bank, samples kept (never erased), shown as 2.4's */
#endif
static uint32_t persist_view_kept;                 /* the settings record's last word as read, when not our VIEW */
#endif
static void persist_boot(void)                    /* before settings_init / panel_init */
{
#if FELUCCA_MOTION && SEC_LOGGED
    (void)motion_for(&proj_tmp.cur, 1);            /* the motion stores of the buffers a section passes through */
    (void)motion_for(&sec_stage_p, 1);             /* (motion_proj.c: bound now, never taken at run time) */
    (void)motion_for(&autosave_buf, 1);
#if FELUCCA_ARRANGER
    (void)motion_for(&song_keep, 1);
#endif
#endif
#if FELUCCA_SL24_XSTEP
    sx_init();                                     /* the working step extras: none (stepx_proj.c) */
    (void)sx_for(&proj_tmp.cur, 1);                /* the step extras' stores, the same buffers (stepx_proj.c) */
#if SEC_LOGGED
    (void)sx_for(&sec_stage_p, 1);
#endif
    (void)sx_for(&autosave_buf, 1);
#if FELUCCA_ARRANGER
    (void)sx_for(&song_keep, 1);
#endif
#endif
#if !FELUCCA_FLASH && FELUCCA_ANALOG2 && !SEC_LOGGED
    {   /* RAM only: a slot kept over a reset lost its drum record (not .noinit): the kit as it is */
        uint32_t i;
        for (i = 0; i < 4u; i++)
            if (proj_ok(&proj_slot[i]) && proj_slot[i].dl_hash != dlrec_hash(&proj_dl[i])) {
                memset(&proj_dl[i], 0, sizeof proj_dl[i]);
                proj_slot[i].dl_hash = 0;
                proj_slot[i].sum = proj_sum(&proj_slot[i]);
            }
    }
#endif
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
#if BP23_SET
        if (n < (int)sizeof p) {                   /* a shorter record (an older build, one with fewer switches) */
            if (n < PERSIST_NO_VIEW + 4)
                p.view = 1;                        /* saved before VIEW (or PER2): the overview, as a fresh device */
            if (n < PERSIST_NO_BRIGHT + 4)
                p.bright = 0;                      /* saved without BRIGHT: full */
            p.bp23 = 0;                            /* without the SLOOP 2.3 settings: as before */
            if (n > PERSIST_NO_VIEW && p.magic == PERSIST_MAGIC)
                n = (int)sizeof p;                 /* (the rest as ours) */
        }
#else
#if FELUCCA_BRIGHT
        if (n < (int)sizeof p)
            p.bright = 0;                          /* saved without BRIGHT: full */
        if (n == PERSIST_NO_BRIGHT)
            n = (int)sizeof p;                     /* (a build without it: the rest as ours) */
#endif
        if (n < (int)sizeof p)
            p.view = 1;                            /* saved before VIEW (or PER2): the overview, as a fresh device */
#endif
        if (((n == (int)sizeof p || n == PERSIST_NO_VIEW) && p.magic == PERSIST_MAGIC)
#if FELUCCA_ARRANGER
            || (n == (int)(16u + sizeof(panel_t)) && p.magic == 0x50455232u)
#endif
            ) {
            settings.magic = SETTINGS_MAGIC;
            settings.palette = p.palette;
            settings.lowcut = p.lowcut;
            settings.zoom = p.zoom;
#if FELUCCA_SL24_SAFE
            settings.view = persist_view_in(p.view, &persist_view_kept);   /* (SLOOP 2.3 / 2.4's lights word) */
#else
            settings.view = p.view > 1u ? 1u : p.view;
#endif
#if FELUCCA_BRIGHT
            bright_boot();                         /* full at every boot, never the saved level (bright.c) */
#endif
#if BP23_SET
            bp23_from_word(p.bp23);
#endif
            if (p.panel.magic == PANEL_MAGIC)
                panel = p.panel;
#if FELUCCA_ARRANGER
            arr_config_t c;
            arr_from_rec(&c, &p.arrangement);
            if (p.magic == PERSIST_MAGIC && arr_stored_ok(&c))   /* (any of A..P, not only A..D) */
                arrangement = c, song_tag = (uint16_t)arr_tag_of(&p.arrangement);
            else
                arr_to_rec(&p.arrangement, &arrangement, 0);
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
#if FELUCCA_SL24_SAFE
    sl24_boot_scan();                              /* (before the log: what it must never erase) */
#endif
#if SEC_LOGGED
    sec_boot();                                    /* sections.c: the log (old project slots migrated first) */
#if FELUCCA_ARRANGER
    if (song_tag && !sec_song_get(&arrangement, song_tag))
        song_tag = 0;                              /* (not the log's chain: the record's 16 parts) */
#endif
#else
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
                if (!proj_slot_boot(i))                 /* (and its drum record, drum_store.c) */
                    sec_dirty |= (uint16_t)(1u << i);
            }
    }
#endif
#if FELUCCA_SNAPSHOTS
    sn_boot();                                     /* snapshots.c: the snapshot area read */
#endif
    up_boot();                                     /* user presets */
#endif
}

#if !SEC_LOGGED
static int project_used(uint32_t slot) { return proj_ok(&proj_slot[slot & 3u]); }
#endif

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
#if FELUCCA_SL24_SAFE
    p.view = persist_view_out(settings.view, persist_view_kept);
#else
    p.view = settings.view;
#endif
#if FELUCCA_BRIGHT
    p.bright = 0;                                  /* full: a dim level is never stored (bright.c) */
#elif BP23_SET
    p.bright = persist_saved.bright;               /* (no BRIGHT here: kept as read) */
#endif
#if BP23_SET
    p.bp23 = bp23_word();
#endif
    p.panel = panel;
#if FELUCCA_ARRANGER
    arr_to_rec(&p.arrangement, &arrangement, song_tag);
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
#if FELUCCA_FLASH && SEC_LOGGED
    if (flash_ok) {                                /* the whole chain in the log first, then its tag (sections.c) */
        int rc = sec_song_put(&arrangement, &song_tag);
        if (rc) {
            ui_message(rc == 1 ? "MEM FULL" : "SAVE ERROR");
            return;
        }
    }
#endif
    settings_save();
#if FELUCCA_FLASH
    if (flash_ok) {
        arr_rec_t r;
        arr_to_rec(&r, &arrangement, song_tag);
        ui_message(memcmp(&persist_saved.arrangement, &r, sizeof r) ? "SAVE ERROR" : "SONG SAVED");
        return;
    }
#endif
    ui_message("SONG IN RAM ONLY");
}

/* ---- live sections (SAVE + key, ui_layers.c). A section is a project slot (A..D = 1..4): stored into RAM
 * at once (playing too), written to flash once the transport is stopped and nothing sounds (an erase
 * stops the audio for ~50 ms); a song recorded with SONG REC is saved the same way. */
#if !SEC_LOGGED
static void section_store(uint32_t s)
{
    s &= 3u;
    fm1_irq_off();                                      /* (the audio ISR may be applying a section) */
    proj_capture(&proj_slot[s], &proj_dl[s]);
    live_sec = (int8_t)s;
    fm1_irq_on();
    sec_dirty |= (uint16_t)(1u << s);
}
static void section_load(uint32_t s)                    /* stopped: the section is the loop now */
{
    s &= 3u;
    project_apply(&proj_slot[s], &proj_dl[s]);
    live_sec = (int8_t)s;
}
static void sections_write(void)                        /* the dirty sections and song into flash */
{
    uint32_t i;
#if FELUCCA_FLASH
    if (flash_ok)
        for (i = 0; i < 4u; i++)
            if (((sec_dirty >> i) & 1u) && proj_put(OBJ_PROJECT0 + i, &proj_slot[i], &proj_dl[i]) == 0) {
                MOTION_SAVED(OBJ_PROJECT0 + i, &proj_slot[i]);
                sec_dirty &= (uint16_t)~(1u << i);      /* (a failed write stays dirty: tried again later) */
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
#endif
/* before an intentional reset (an update, UPDATE MODE, UBOOT from the host): the audio is stopped, so
 * whatever is only in RAM goes to flash now: the live sections, the song, the working project */
static void persist_flush_now(void)
{
    sections_write();
#if FELUCCA_FLASH
    if (flash_ok && !arrangement_clock.running) {     /* (a song playing: the tracks hold a section) */
        proj_capture(&autosave_buf, &autosave_dl);
        if ((autosave_buf.sum ^ MOTION_HASH()) != autosave_hash &&
            proj_put(OBJ_AUTOSAVE, &autosave_buf, &autosave_dl) == 0) {
            MOTION_SAVED(OBJ_AUTOSAVE, &autosave_buf);
            autosave_hash = autosave_buf.sum ^ MOTION_HASH();
        }
    }
#endif
}
/* a restore done (ed_backup.c): the .noinit slots and their drum records are not written back (the FM-1
 * restarts and loads everything from flash) */
#if !SEC_LOGGED
static void proj_slots_drop(void)
{
    uint32_t i;
    for (i = 0; i < 4u; i++)
        proj_slot[i].magic = 0;
    sec_dirty = 0;
    song_dirty = 0;
}
#endif
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
#if FELUCCA_SNAPSHOTS
#if !FELUCCA_FLASH
#error "FELUCCA_SNAPSHOTS keeps its slots in flash (FELUCCA_FLASH)"
#endif
#include "snapshots.c"     /* whole-state snapshots: the work, every section, the song in a slot (docs/SNAPSHOTS.md) */
#endif
#endif /* PROJ_HOST */
