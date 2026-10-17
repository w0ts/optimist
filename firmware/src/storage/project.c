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
        pstep_t step[NSTEP];
        dstep_sn_t dstep[NSTEP];               /* (the drum track: 16 lanes, the same size; its ACCENT lane: an event) */
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
        pstep_t step[NSTEP];
        dstep_sn_t dstep[NSTEP];
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
/* n bytes at b are a stored form of sz bytes: its magic and size sz first, its sum (FNV-1a of the rest) last */
static int proj_form_ok(const void *b, int n, uint32_t magic, uint32_t sz)
{
    const uint32_t *w = (const uint32_t *)b;
    return n == (int)sz && w[0] == magic && w[1] == sz && w[sz / 4u - 1u] == proj_hash(b, sz - 4u);
}
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
#if P_TAIL
    for (k = P_ENG_END; k < P_COUNT; k++)                /* (SLOOP 2.4's: px_unpack; the COMP insert's: fx_rec.c) */
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
/* a part's stored values s: the words of the depths of before at 0 (LFO DEST's, ENV2's FLT; the slots are the
 * modulation record's, dlfo_rec.c). ENV 1 MOD's FLT SHP are depths in their words (2026-10-16) */
static void pj_mods_zero(int16_t *s)
{
    s[P_LEG_PIT] = s[P_LEG_FLT] = s[P_LEG_SHP] = s[P_LEG_AMP] = 0;
#if FELUCCA_ANALOG2
    s[P_A2FENV] = 0;
#endif
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
    if (!proj_form_ok(b, n, PROJ_MAGIC_VA, sizeof *q))
        return 0;
    memcpy(q, b, sizeof *q);
    proj_va.from = a->sum;
    proj_va_fix(q, proj_va.dst);
    proj_va.to = q->sum;
    return 1;
}
#endif

#if FELUCCA_AUTO
#include "auto_proj.c"         /* each project buffer's automation store (seq/auto.c: motion, locks, nudges, fills) */
#endif
#include "../fx/fx_rec.c"            /* the FX slots' record: each project buffer's store (fx_slots.c) */
#include "dlfo_rec.c"               /* the drum lanes' LFO record: each project buffer's store (drums/lane_play.c) */

/* ---- old formats -> format 4 */
/* an old step into a synth step (no level, no ratchet) */
static void step_from8(pstep_t *d, const step8_t *s)
{
    memcpy(d->note, s->note, 4);
    d->n = s->n;
    d->time = s->time;
    d->flags = s->flags;
    d->vel = s->vel;
    d->lvl = d->rat = 0;
}
/* an old drum step (GM notes) into the drum track's lanes; its velocity / accent -> their level */
static void dstep_from8(dstep_sn_t *o, const step8_t *s)
{
    uint32_t i, lvl = (s->flags & SF_ACCENT) || s->vel > 115u ? LV_HARD : !s->vel ? LV_NORM : vel_lvl(s->vel);
    dstep_t d;
    memset(&d, 0, sizeof d);
    if (s->time == ST_NOTE)
        for (i = 0; i < s->n && i < 4u; i++)
            dstep_set(&d, lane_of_note(s->note[i] & 127u), lvl, 0);
    memcpy(o, &d, DSTEP_SN);
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
    if (!proj_form_ok(v3, n, PROJ_MAGIC_V3, sizeof *v3))
        return 0;
    proj_from_v3_ok(q, v3);
    return 1;
}

static project_v3_t proj_v3_tmp IN_POOL;               /* (formats 1, 2: through format 3) */
/* a format 2 project (n bytes in *v2) -> slot q as format 4 */
static int proj_from_v2(project_t *q, const project_v2_t *v2, int n)
{
    project_v3_t *v3 = &proj_v3_tmp;
    uint32_t i;
    if (!proj_form_ok(v2, n, PROJ_MAGIC_V2, sizeof *v2))
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
    if (!proj_form_ok(v1, n, PROJ_MAGIC_V1, sizeof *v1))
        return 0;
    memset(v3, 0, sizeof *v3);
    memcpy(v3->g, v1->g, sizeof v3->g);
    proj_trk_v2_to_v3(&v3->t[0], &v1->t, 0);
    proj_from_v3_ok(q, v3);
    for (i = 1; i < NTRK; i++) {               /* the other tracks: their defaults, no steps */
        const engine_t *e = ENGINES[trk_def_engine(i)];
        uint32_t k;
        for (k = 0; k < PJ_NP; k++)
            q->t[i].p[k] = k >= PJ_E0 ? e->edit[k - PJ_E0].def : TP[k].def;
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
    if (!proj_form_ok(b, n, PROJ_MAGIC_V7, PROJ_V7_N + 4u))
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
    return proj_form_ok(b, n, PROJ_MAGIC_V8, PROJ_V8_N) || proj_form_ok(b, n, PROJ_MAGIC_V9, PROJ_V8_N);
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
    if (!proj_form_ok(b, n, magic, sz))
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
    if (!proj_form_ok(v4, n, PROJ_MAGIC_V4, sizeof *v4))
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
    if (!proj_form_ok(v5, n, PROJ_MAGIC_V5, sizeof *v5))
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

/* The projects of before (read only since 2026-10-14: a project saved since keeps every modulation slot in its
 * modulation record, dlfo_rec.c, those words at 0). Then:
 * LFO DEST's slots (core.h P_L1DA .. P_L1AB) are stored in the four words that held the depths PIT FLT SHP AMP:
 * params.c lfo1_store / lfo1_load (the depths themselves while the slots are only those: a project of before loads and
 * saves byte for byte, an older build plays it; else the tagged layout). A project of before with three or four depths
 * keeps the two of the most weight (lfo_from_depths).
 *
 * LFO 2 (core.h P_L2RATE .. P_L2AB, the parts') in the drum track's stored LFO slots, which it never reads (its lanes
 * have LFOs of their own: lane_play.c), no format change (as px_pack, pj_x, the macros' MAC_ID), and only when a part's
 * LFO 2 is not at its defaults: a project without it is byte for byte one of before. Then the drum track's RATE slot
 * holds L2_TAG, its WAVE PHS FADE slots the parts' first words, its PIT SHP AMP slots their second (FLT: MAC_ID's):
 *   first   RATE (8 bits, two's complement: -10..127) | AMT A << 8 (8 bits, two's complement)
 *   second  WAVE (3 bits) | TRIG << 3 | POL << 4 | DEST A << 5 (5 bits) | DEST B << 10 (5 bits)
 *   AMT B   in the part's own LFO DEST words (lfo1_store's spill: their tagged layout then)
 * L2_TAG_V1 (feat/lfo2, one slot: AMT 7 bits at its old scale, x 2 now; WAVE 3 | DEST << 3 (3) | TRIG << 6 | POL << 7)
 * is read. Without a tag (every project before; one saved without a LFO 2 on) the slots are the drum track's own. Every
 * build reads the tags */
#define L2_TAG 0x4D00u                                   /* (out of the RATE slot's range, -10..127) */
#define LT_N_V1 25                                       /* (LT_N of then: OFF .. XAMT) */
#define L2_TAG_V1 0x4C00u                                /* (LFO 2 with one DEST, 2026-10-13) */
#define L2_NV 8u                                         /* RATE WAVE TRIG POL DEST A, AMT A, DEST B, AMT B */
static const uint8_t L2_W0[NPART] = {P_LWAVE, P_LPHASE, P_LFADE};
static const uint8_t L2_W1[NPART] = {P_LEG_PIT, P_LEG_SHP, P_LEG_AMP};
static uint32_t l2_tag(const project_t *p) { return (uint16_t)p->t[TRK_DRUM].p[P_LRATE] & 0xFF00u; }
/* the project in p holds LFO 2 values (its drum track's LFO slots are not the drum track's) */
static int l2_tagged(const project_t *p) { return l2_tag(p) == L2_TAG || l2_tag(p) == L2_TAG_V1; }
/* part k's LFO 2 in p (its defaults without a tag): v[L2_NV], RATE .. AMT B, each inside its range (a destination
 * past this build's: OFF) */
static void l2_get(const project_t *p, uint32_t k, int16_t *v)
{
    static const int16_t D[L2_NV] = {60, 0, 0, 0, LT_OFF, 0, LT_OFF, 0};   /* (TP's P_L2RATE ..: every build's) */
    const int16_t *d = p->t[TRK_DRUM].p;
    uint32_t w0 = (uint16_t)d[L2_W0[k]], w1 = (uint16_t)d[L2_W1[k]], j;
    int16_t s[4];
    int32_t spill;
    memcpy(v, D, sizeof D);
    if (l2_tag(p) == L2_TAG_V1) {
        v[0] = (int16_t)(int8_t)(uint8_t)w0, v[1] = (int16_t)(w1 & 7u), v[2] = (int16_t)(w1 >> 6 & 1u);
        v[3] = (int16_t)(w1 >> 7 & 1u), v[4] = (int16_t)(w1 >> 3 & 7u);
        v[5] = (int16_t)(2 * ((int32_t)((w0 >> 8 & 0x7Fu) ^ 0x40u) - 0x40));   /* (its AMT was half today's) */
    } else if (l2_tag(p) == L2_TAG) {
        (void)lfo1_load(&p->t[k].p[P_LEG_PIT], s, &spill);
        v[0] = (int16_t)(int8_t)(uint8_t)w0, v[1] = (int16_t)(w1 & 7u), v[2] = (int16_t)(w1 >> 3 & 1u);
        v[3] = (int16_t)(w1 >> 4 & 1u), v[4] = (int16_t)(w1 >> 5 & 31u), v[5] = (int16_t)(int8_t)(uint8_t)(w0 >> 8);
        v[6] = (int16_t)(w1 >> 10 & 31u), v[7] = (int16_t)spill;
    }
    v[0] = (int16_t)clamp(v[0], -LSYNC_N, 127), v[1] = (int16_t)clamp(v[1], 0, 4);
    for (j = 4u; j < L2_NV; j += 2u)                     /* (a DEST past the list of then, 25: OFF) */
        if (v[j] >= LT_NB || v[j] >= LT_N_V1)
            v[j] = LT_OFF, v[j + 1u] = 0;
}
/* a project's drum track values v (P_COUNT) whose LFO slots held LFO 2: their defaults */
static void l2_slots_def(int16_t *v)
{
    uint32_t k;
    v[P_LRATE] = TP[P_LRATE].def;
    for (k = 0; k < NPART; k++)
        v[L2_W0[k]] = TP[L2_W0[k]].def, v[L2_W1[k]] = 0;
}
_Static_assert(P_L2WAVE == P_L2RATE + 1 && P_L2TRIG == P_L2RATE + 2 && P_L2POL == P_L2RATE + 3 && P_L2DA == P_L2RATE + 4 &&
               P_L2AB == P_L2RATE + 7 && P_E1PIT == P_L2RATE + L2_NV && P_COUNT == P_VOICES + 1,
               "LFO 2's values: in a row (the projects of before: l2_get)");
#if FELUCCA_SL24_SAFE || FELUCCA_SL24_IMPORT
/* the parts' LFO 2 values x (P_L2RATE ..) into p's drum track, when one is not at its default (AMT B: lfo1_store's
 * spill, written with the part's LFO DEST): the form of before, which only SLOOP's importer still makes (sl24_import.c,
 * the project it hands to proj_apply; never saved so: proj_capture keeps LFO 2 in the modulation record) */
static void l2_pack(project_t *p, const int16_t (*x)[L2_NV])
{
    int16_t *d = p->t[TRK_DRUM].p;
    uint32_t k, j, any = 0;
    for (k = 0; k < NPART; k++)
        for (j = 0; j < L2_NV; j++)
            any |= (uint32_t)(x[k][j] != TP[P_L2RATE + j].def);
    if (!any)
        return;
    d[P_LRATE] = (int16_t)L2_TAG;
    for (k = 0; k < NPART; k++) {
        d[L2_W0[k]] = (int16_t)(((uint32_t)x[k][0] & 0xFFu) | ((uint32_t)x[k][5] & 0xFFu) << 8);
        d[L2_W1[k]] = (int16_t)(((uint32_t)x[k][1] & 7u) | ((uint32_t)x[k][2] & 1u) << 3 | ((uint32_t)x[k][3] & 1u) << 4 |
                                ((uint32_t)x[k][4] & 31u) << 5 | ((uint32_t)x[k][6] & 31u) << 10);
    }
}
#endif
/* -> x, the parts' LFO 2 values (their defaults without a tag, each inside its range); 1 when p has them */
static int l2_unpack(const project_t *p, int16_t (*x)[L2_NV])
{
    uint32_t k;
    for (k = 0; k < NPART; k++)
        l2_get(p, k, x[k]);
    return l2_tagged(p);
}
#if FELUCCA_SL24_IMPORT || FELUCCA_SL24_EXPORT
#include "sl24/sl24_fm6.c"         /* SLOOP 2.4's FM6 patches <-> our voices (the importer, the exporter) */
#endif
#if FELUCCA_SL24_SAFE || FELUCCA_SL24_IMPORT
#if FELUCCA_SL24_IMPORT
#include "../seq/stepx.h"             /* (2.4's step extras) */
#endif
#if SL24_TP
static void px_pack(project_t *p, const int16_t (*x)[3]);   /* (below: SLOOP 2.4's track values) */
#endif
#include "sl24/sl24_import.c"       /* SLOOP 2.4's projects: told apart (sl24_is), imported when asked (proj_from_sl24) */
#endif
#if FELUCCA_SL24_EXPORT
#include "../seq/stepx.h"
#include "sl24/sl24_export.c"       /* ours as a SLOOP 2.4 project (proj_to_sl24): the editor saves it (ed_sl24.c) */
#endif
#if FELUCCA_AUTO && ((FELUCCA_SL24_IMPORT && FELUCCA_FLASH) || FELUCCA_SL24_EXPORT)
/* 2.4's step extras, one scratch for both ways: the import's on their way into the automation store (sl24_guard.c
 * sl24_import_buf) and the export's, the automation as 2.4's extras (ed_sl24.c ed_sl24_make). Each fills it and is
 * done with it inside one main-loop call, and neither calls the other */
static stepx_t sl24_xs[NTRK] __attribute__((section(".pool")));
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
/* track k's values as a project keeps them, by parameter id (v[P_COUNT]): its patch (with motion or locks: the
 * values under them, auto.c auto_patch) */
static void proj_patch(int16_t *v, uint32_t k)
{
#if FELUCCA_MOTION || FELUCCA_PLOCK
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
 * before has every one at its default (RATIO 2:1, ATK 10 ms, REL AUTO) with THRS and CEIL OFF. CEIL's field holds the
 * value itself (mc_base: its default was OFF until 2026-10-11, -3.0 dB since; the stored values keep their meaning).
 * Every reserved bit is now taken: a sidechain SOURCE (master_comp.h) needs a format change.
 * Kept in every build (a build without MASTER_COMP keeps the values it loads; miss.c says they are not heard). */
static const uint8_t MC_SHIFT[6] = {0, 5, 8, 11, 14, 18}, MC_MASK[6] = {31, 7, 7, 7, 15, 7};   /* (w: rsv[1], rsv[2], rsv[0] >> 3) */
/* the value field i counts from: its default, but CEIL's OFF. CEIL's default became -3.0 dB (2026-10-11: the lookahead
 * limiter by default, a new project's); the format stays: CEIL is stored as it is (0 OFF, as every project saved
 * before, with or without MASTER_COMP, reads and plays as it did) */
static int32_t mc_base(uint32_t i) { return G_CTHR + i == G_CCEIL ? 0 : GP[G_CTHR + i].def; }
static void mc_pack(project_t *p)
{
    uint32_t w = 0, i;
    for (i = 0; i < 6u; i++)
        w |= ((uint32_t)(song.g[G_CTHR + i] - mc_base(i)) & MC_MASK[i]) << MC_SHIFT[i];
    p->rsv[1] = (uint8_t)w;
    p->rsv[2] = (uint8_t)(w >> 8);
    p->rsv[0] = (uint8_t)((p->rsv[0] & 7u) | (w >> 16) << 3);   /* (bits 0..2: rev_pack, written before) */
}
static void mc_unpack(const project_t *p)
{
    uint32_t w = p->rsv[1] | (uint32_t)p->rsv[2] << 8 | (uint32_t)(p->rsv[0] >> 3) << 16, i;
    for (i = 0; i < 6u; i++) {
        const param_desc_t *d = &GP[G_CTHR + i];
        int32_t v = (int32_t)(((w >> MC_SHIFT[i]) + (uint32_t)mc_base(i)) & MC_MASK[i]);
        song.g[G_CTHR + i] = (int16_t)clamp(v > d->max ? v - MC_MASK[i] - 1 : v, d->min, d->max);   /* (THRS: < 0) */
    }
}

/* REC QUANTIZE (core.h G_RECQ) in the project's G_INFO slot: the CPU read-out, a value no build stores (0..0: every
 * project so far holds 0 there, and a build from before clamps whatever it finds to 0 on a load, so ON); bit 0 = OFF.
 * The read-outs draw the CPU (cpu_info), never the slot; a SLOOP 2.4 import / export clears it (sl24_import.c,
 * sl24_export.c sl24_g_out) */
#define RECQ_PACK() ((int16_t)(song.g[G_RECQ] ? 0 : 1))
#define RECQ_UNPACK(v) ((int16_t)!((v) & 1))
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

#if FELUCCA_PATTERNS
static void pat_mark(const project_t *p, int apply);   /* pat.c */
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
    p->g[G_INFO] = RECQ_PACK();                         /* (G_INFO's slot, the CPU read-out: REC QUANTIZE) */
    p->sel = song.sel;
    p->rsv[0] = rev_pack();                             /* the reverb's algorithm (rev_type.c; 0 ROOM): bits 0..2 */
    mc_pack(p);                                         /* the master COMP / LIMIT (above): rsv[0] bits 3..7, rsv[1], rsv[2] */
    for (i = 0; i < NTRK; i++) {
        proj_patch(v, i);
        if (i == TRK_DRUM)                              /* (the drum bus's amounts: the FX record's, fx_slots.c) */
            v[P_DIST] = v[P_CHOR] = v[P_DLY] = v[P_REV] = 0;
        pj_from_p(p->t[i].p, v);
        if (i == TRK_DRUM)                              /* (the ACCENT amount stored from its default: 0 in a project of before) */
            p->t[i].p[PJ_E0 + (P_DACC - P_E0)] = (int16_t)(v[P_DACC] - DACC_DEF);
#if SL24_TP
        memcpy(px[i], &v[P_TFLT], sizeof px[i]);
#endif
        if (i < NPART)                                  /* (LFO DEST's, ENV DEST's, ENV2's slots: the modulation record,
                                                         * dlfo_rec.c; the words of their depths of before at 0) */
            pj_mods_zero(p->t[i].p);
        p->t[i].engine = (uint8_t)(i < NPART ? eng_uid(trk[i].eng_req % NENGINES) : 0u);   /* (a UID) */
        p->t[i].preset = trk[i].preset;
        steps_store(p->t[i].step, &trk[i], i);         /* (each note's own end: an event of its store, below) */
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
    for (i = 0; i < NPART; i++) {                       /* the parts' ENV2 SUS2 REL2 (pj_x; its amounts of before: 0) */
        proj_patch(v, i);
        v[P_A2EPIT] = v[P_A2ESHP] = v[P_A2EOS2] = v[P_A2ESDT] = 0;
        a2x_pack(pj_x(p, i), &v[P_A2ESUS]);
    }
#else
    (void)d;
#endif
#if SL24_TP
    px_pack(p, px);                                     /* (after the drum track's values: in its free slots) */
#endif
    p->sum = proj_sum(p);
#if FELUCCA_AUTO
    auto_capture_store(p);                              /* its automation (auto_proj.c) */
    ngate_capture(p);                                   /* and the notes' own ends in it (seq/ngate.h) */
#endif
#if FELUCCA_PATTERNS
    pat_mark(p, 0);                                     /* the tracks' pattern sources with it (pat.c) */
#endif
    fxr_capture_store(p);                               /* its FX slots (fx_rec.c) */
    dlr_capture_store(p);                               /* its drum lanes' LFOs (dlfo_rec.c) */
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
    int16_t l2[NPART][L2_NV];
    int l2t = l2_unpack(p, l2);                         /* the parts' LFO 2 (l2_pack) */
#if FELUCCA_AUTO
    auto_apply_store(p);                                /* its automation, if it is this project's (auto_proj.c) */
#endif
    undo_clear();                                       /* (undo.c: the history was of other steps) */
#if FELUCCA_PATTERNS
    pat_mark(p, 1);                                     /* the tracks' pattern sources: the buffer's (pat.c) */
#endif
    for (i = 0; i < PJ_NG; i++)
        if (all ? i != G_SLOT && i != G_LOAD && i != G_SAVE && i != G_VIEW && i != G_MIDI && i != G_DRREV && i != G_SYNC : i == G_DRLVL)   /* (G_SYNC: the HOME menu's, device-wide) */
            song.g[i] = (int16_t)clamp(p->g[i], GP[i].min, GP[i].max);
    if (all)
        rev_unpack(p->rsv[0]);                          /* the reverb's algorithm (rev_type.c; older projects: 0 ROOM) */
    if (all)
        mc_unpack(p);                                   /* (older projects: 0, COMP and LIMIT off) */
    if (all)
        song.g[G_RECQ] = RECQ_UNPACK(p->g[G_INFO]);     /* (older projects: 0, ON) */
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
        if (k == TRK_DRUM)                              /* (the ACCENT amount: stored from its default) */
            v[P_DACC] = (int16_t)(v[P_DACC] + DACC_DEF);
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
        if (k == TRK_DRUM && l2t)                       /* (its LFO slots that held LFO 2: the drum track's defaults) */
            l2_slots_def(v);
        if (k < NPART) {                                /* LFO DEST's slots from their stored form (params.c lfo1_load) */
            int32_t spill;
            (void)lfo1_load(&s->p[P_LEG_PIT], &v[P_L1DA], &spill);
            (void)env_load(v);                          /* ENV DEST's, ENV2 DEST's (env_store.h) */
        }
        if (k < NPART)
            memcpy(&v[P_L2RATE], l2[k], sizeof l2[k]);
        for (i = 0; i < P_COUNT; i++) {                 /* every value back inside its range */
            const param_desc_t *d = k == TRK_DRUM && i == P_E0 ? &DRUM_KIT_DESC :   /* the drum kit */
                                    k == TRK_DRUM && i == P_DACC ? &DRUM_ACC_DESC :   /* its ACCENT amount */
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
        steps_load(t, k, s->step);
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
#if FELUCCA_AUTO
    ngate_apply();                                      /* the notes' own ends out of the lists into the steps */
#endif
    fxr_apply_store(p, all);                            /* its FX slots (fx_rec.c; the layout with a load only; none:
                                                         * after the tracks, which the layout of an older one reads) */
    dlr_apply_store(p);                                 /* its drum lanes' LFOs, the parts' modulation (dlfo_rec.c; none:
                                                         * every LFO off) */
    MISS_BUMP();                                        /* the main loop says what this build lacks (miss.c) */
}

#ifndef PROJ_HOST
#if FELUCCA_ARRANGER
#include "../seq/arranger_scene.c"
#endif
static uint16_t sec_dirty;                     /* live sections in RAM, not yet in flash (bit per section) */
static uint8_t song_dirty;                      /* the song: in RAM, not yet in flash */
#if FELUCCA_MOTION && FELUCCA_FLASH
#include "motion_flash.c"      /* the motion stores beside their projects in flash */
#if !SEC_LOGGED                /* (the four slots: the MOTN record is their store; the log: the extras record) */
#define MOTION_SAVED(obj, p) motion_flash_write(obj, p)
#else
#define MOTION_SAVED(obj, p) ((void)0)
#endif
#define MOTION_READ(obj, p) motion_flash_read(obj, p)
#else
#define MOTION_SAVED(obj, p) ((void)0)
#if FELUCCA_AUTO
#define MOTION_READ(obj, p) ((void)auto_fresh(p))    /* (no motion form here: the extras record adds to an empty store) */
#else
#define MOTION_READ(obj, p) ((void)0)
#endif
#endif
#if FELUCCA_AUTO
#define AUTO_HASH() auto_hash()
#else
#define AUTO_HASH() 0u
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
} proj_tmp IN_POOL;
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
#include "sections/sections.c"          /* FELUCCA_SECTIONS 8 / 16: the sections in a log (A..P), staged for the ISR */
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

/* ---- the working project, kept in flash by itself (autosave_tick), loaded at power-on (autosave_resume). Only while
 * stopped and silent (a flash erase stops the audio for ~50 ms), its change hash taken every second:
 *   settled   the work has not changed for AUTOSAVE_SETTLE: saved (no panel idle time: an edit is a change of the
 *             hash, whatever made it);
 *   changing  the work keeps changing (a tweak every few seconds, MIDI CCs): saved anyway once
 *             it has been unsaved for AUTOSAVE_MAX_WAIT, then again at most every AUTOSAVE_MAX_WAIT while it goes on;
 *   gap       never two saves within AUTOSAVE_MIN_GAP (the flash wear, the user's choice 2026-10-13); the first save
 *             after power-on is not held back. Before fix/autosave nothing was saved in the first 20 s after power-on
 *             (and a save waited for 2.5 s without input): a power-off there lost the edit.
 * Wear: a save erases one 4 KiB sector, the other copy of OBJ_AUTOSAVE (A 0x9F000 /
 * B 0xFE000, alternately: each copy 0.5 erase a save); the drum record pair (0xE5000 / 0xE6000) only when the drum
 * lanes changed; the motion form is programmed into the copy just written (no erase); the extras, FX and LFO records
 * are appended to the section log (8 sectors, ~16..400 B a save: ~0.006..0.12 erase of a log sector a save). The
 * flash, a Puya P25Q80H (JEDEC 0x856014, hal/fm1_flash.h), is rated 100k erase cycles a sector: the copies last
 * 200k saves. 4 h a day of editing: the worst (a save every MIN_GAP) 240 saves/day ~2.3 years; continuous changes (one
 * save per MAX_WAIT) 120/day ~4.6 years; a settled edit every 5 min 48/day ~11 years. (Without the gap, the settle
 * rule alone allowed a save every 11 s: ~1,300/day, ~150 days.) */
#define AUTOSAVE_SETTLE 10000u                 /* ms the work stays the same before it is saved */
#define AUTOSAVE_MAX_WAIT 120000u              /* ms the work may stay unsaved while it keeps changing */
#define AUTOSAVE_MIN_GAP 60000u                /* ms between two saves at least (the first after power-on: none) */
#ifdef SEC_RB_WORK
#define autosave_buf SEC_RB_WORK               /* (sections.c sec_rb: shared with the section records' buffer) */
#else
static project_t autosave_buf __attribute__((section(".pool")));
#endif
static dlrec_t autosave_dl;                    /* its drum record */
static uint32_t autosave_hash, autosave_checked;   /* the saved work's hash; the last check */
static uint32_t autosave_seen, autosave_seen_ms;   /* the work's hash at the last check, since when it is that */
static uint32_t autosave_dirty_ms, autosave_put_ms; /* since when the work differs from the saved one; the last try */
static uint8_t autosave_dirty, autosave_tried; /* it differs (autosave_dirty_ms counts); a save tried since power-on */

#if FELUCCA_FLASH
/* the change test of the working project captured in p: everything stored with it (the project, its automation,
 * its FX record, its drum lanes' LFO record) */
static uint32_t autosave_hash_of(const project_t *p)
{
    return p->sum ^ AUTO_HASH() ^ fxr_hash(p) ^ DLR_HASH(p);   /* (the sum covers the drum record) */
}
/* autosave_buf (captured, its hash h) into OBJ_AUTOSAVE with its records; 0 written */
static int autosave_put(uint32_t h)
{
    int rc = proj_put(OBJ_AUTOSAVE, &autosave_buf, &autosave_dl);
    if (rc != 0)
        return rc;
    MOTION_SAVED(OBJ_AUTOSAVE, &autosave_buf);
#if FELUCCA_AUTO && SEC_LOGGED
    (void)sx_log_put(SX_ID_AUTO, autosave_buf.sum, &autosave_buf, 0);   /* its extras record (stepx_log.c) */
#endif
#if SEC_LOGGED
    (void)fxr_log_put(FXR_ID_AUTO, autosave_buf.sum, &autosave_buf, 0);   /* its FX record (fx_rec_log.c) */
    (void)dlr_log_put(DLR_ID_AUTO, autosave_buf.sum, &autosave_buf, 0);   /* its LFO record (dlfo_rec_log.c) */
#endif
    autosave_hash = h;
    return 0;
}
#endif

static int audio_quiet(void)
{
    uint32_t p, i;
#if FELUCCA_USB_AUDIO
    if (ua_streaming(fm1_ticks()))                         /* a flash write holds every IRQ off: the computer's stream would glitch */
        return 0;
#endif
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
    int settled, overdue;
    if (!flash_ok || song.playing || transport_req || rec_wait || ft_on || ui.menu || now - autosave_checked < 1000u)
        return;
    autosave_checked = now;
    proj_capture(&autosave_buf, &autosave_dl);
    h = autosave_hash_of(&autosave_buf);
    if (h != autosave_seen) {                           /* changed (or the first check after playing): settle again */
        autosave_seen = h;
        autosave_seen_ms = now;
    }
    if (h == autosave_hash)
        autosave_dirty = 0;
    else if (!autosave_dirty) {
        autosave_dirty = 1;
        autosave_dirty_ms = now;
    }
    settled = now - autosave_seen_ms >= AUTOSAVE_SETTLE;
    overdue = autosave_dirty && now - autosave_dirty_ms >= AUTOSAVE_MAX_WAIT;
    if (!(settled || overdue) || (autosave_tried && now - autosave_put_ms < AUTOSAVE_MIN_GAP) || !audio_quiet())
        return;
#if FELUCCA_PATTERNS
    pat_state_save();                                   /* the tracks' pattern sources (pat.c), when they changed */
#endif
    if (!autosave_dirty)
        return;
    autosave_put_ms = now;                              /* (a failed write: tried again after AUTOSAVE_MIN_GAP) */
    autosave_tried = 1;
    if (autosave_put(h) == 0)
        autosave_dirty = 0;
    else
        autosave_seen_ms = now;
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
    MOTION_READ(OBJ_AUTOSAVE, q);                       /* (its motion first: the extras record adds to it) */
#if FELUCCA_AUTO && SEC_LOGGED
    sx_log_get(SX_ID_AUTO, q->sum, q);                  /* its extras record (stepx_log.c), applied with it */
#endif
#if SEC_LOGGED
    fxr_log_get(FXR_ID_AUTO, q->sum, q);                /* its FX record (fx_rec_log.c), applied with it */
    dlr_log_get(DLR_ID_AUTO, q->sum, q);                /* its LFO record (dlfo_rec_log.c), applied with it */
#endif
    proj_apply(q, &autosave_dl, 1);
    autosave_hash = autosave_hash_of(q);                /* (all of it, as autosave_tick: else a needless save each power-on) */
    song.sel = (uint8_t)(q->sel < NTRK ? q->sel : 0u);
#if FELUCCA_PATTERNS
    pat_state_load();                                   /* the tracks' pattern sources as they were left (pat.c) */
#endif
    for (n = 0; n < NPART; n++)
        trk[n].engine = trk[n].eng_req;        /* (nothing sounds yet: no fade) */
#endif
}

/* settings + learned panel table: one flash object. The flash copy wins at
 * boot (the .noinit copies are garbage after a power-off). */
#include "settings_word.c"                 /* bp23_word / bp23_from_word: the settings word */
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
static uint16_t song_tag;                          /* the log's whole chain that goes with the settings record (sections.c) */
#endif

#if FELUCCA_SNAPSHOTS
static void sn_boot(void);                         /* snapshots.c */
#endif
#if FELUCCA_SL24_SAFE
#if !SEC_LOGGED                /* (with the log: sections.c includes it) */
#include "sl24/sl24_guard.c"        /* SLOOP 2.4's projects, autosave, FM6 bank, samples kept (never erased), shown as 2.4's */
#endif
static uint32_t persist_view_kept;                 /* the settings record's last word as read, when not our VIEW */
#endif
#include "persist_rec.c"                      /* persist_t: the settings record's layout, persist_upgrade (older records) */
#if FELUCCA_BLE
/* the BLE fields of a record read (persist_boot) into the BLE code's copies, and out of them (settings_save, out 1) */
static void persist_ble(persist_t *p, int out)
{
    static uint8_t *const K[4] = {ble_addr_kept, ble_rf_kept, ble_bond_kept, ble_dev_kept};
    static const uint8_t N[4] = {sizeof p->ble_addr, sizeof p->ble_rf, sizeof p->ble_bond, sizeof p->ble_dev};
    uint8_t *f = p->ble_addr;                      /* (in a row: PERSIST_BLE) */
    uint32_t i;
    for (i = 0; i < 4u; f += N[i++])
        memcpy(out ? f : K[i], out ? K[i] : f, N[i]);
}
_Static_assert(sizeof ble_addr_kept == 8u && sizeof ble_rf_kept == BLE_RF_COPY_SIZE && sizeof ble_bond_kept == BLE_BOND_SIZE &&
               sizeof ble_dev_kept == BLE_DEV_STORE_SIZE && BLE_DEV_STORE_SIZE < 256u && BLE_RF_COPY_SIZE < 256u,
               "the BLE copies: the record's fields' sizes");
#endif
#if FELUCCA_FLASH
static persist_t persist_saved;
#endif
static void persist_boot(void)                    /* before settings_init / panel_init */
{
    static const project_t *const BOUND[] = {&proj_tmp.cur, &autosave_buf,
#if SEC_LOGGED
                                             &sec_stage_p,
#endif
#if FELUCCA_ARRANGER
                                             &song_keep,
#endif
    };
    uint32_t b;
    for (b = 0; b < sizeof BOUND / sizeof BOUND[0]; b++)   /* the FX records' stores (fx_rec.c) and the LFO records' */
        (void)fxr_for(BOUND[b], 1), (void)dlr_for(BOUND[b], 1);   /* (dlfo_rec.c): bound now, never taken at run time */
#if FELUCCA_AUTO
    auto_init();                                   /* the working automation: none (auto_proj.c) */
    (void)auto_for(&proj_tmp.cur, 1);              /* the stores of the buffers a section passes through */
#if SEC_LOGGED
    (void)auto_for(&sec_stage_p, 1);               /* (auto_proj.c: bound now, never taken at run time) */
#endif
    (void)auto_for(&autosave_buf, 1);
#if FELUCCA_ARRANGER
    (void)auto_for(&song_keep, 1);
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
#if FELUCCA_SL24_SAFE
        sl25_set_scan();                           /* (SLOOP 2.5 / 2.6's record: never read as ours, never erased) */
#endif
        int n = st_load(OBJ_SETTINGS, &p, sizeof p);
#if FELUCCA_SL24_SAFE
        if (n < 0)
            n = sl25_set_load(&p, sizeof p);       /* (none of ours yet: their 88 bytes, read as 2.4's) */
#endif
        n = persist_upgrade(&p, n);
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
#if FELUCCA_FAVORITES
            persist_fav_load(&p);
#endif
#if FELUCCA_USB_AUDIO
            persist_ua_load(&p);                   /* SYSTEM > USB AUDIO: the capture channels' sources */
#endif
#if FELUCCA_DUAL && FELUCCA_DUALCFG
            if (p.dualcfg.magic == DUALCFG_MAGIC)
                dualcfg = p.dualcfg;               /* (else the defaults: dualcfg_boot) */
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
#if FELUCCA_BLE
            persist_ble(&p, 0);                    /* (the BLE address, trims, bond, devices) */
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
    p.view = persist_view_out(settings.view, persist_view_kept ? sl_word_out(bp23_word(), persist_view_kept) : 0u);   /* (2.3 / 2.4's
                                                    * word as they read it: our settings now, NOTES inverted) */
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
#if FELUCCA_FAVORITES
    persist_fav_save(&p);
#endif
#if FELUCCA_USB_AUDIO
    persist_ua_save(&p);
#endif
#if FELUCCA_DUAL && FELUCCA_DUALCFG
    p.dualcfg = dualcfg;
#endif
#if FELUCCA_BLE
    persist_ble(&p, 1);
#endif
    if (!memcmp(&p, &persist_saved, sizeof p))
        return;                                    /* unchanged: no erase cycle */
    if (st_save(OBJ_SETTINGS, &p, sizeof p) == 0)
        persist_saved = p;
#endif
}

#if FELUCCA_FLASH
_Static_assert(sizeof(project_t) <= ST_PAYLOAD_MAX, "project does not fit one flash sector");
_Static_assert(sizeof(persist_t) <= ST_PAYLOAD_MAX, "the settings record fits one flash sector");
#if FELUCCA_SL24_SAFE
_Static_assert(sizeof(persist_t) < SL25_PERSIST + SL25_DSU_LEN, "SLOOP 2.5 / 2.6's settings are longer than any of ours");
#endif
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
        uint32_t h;
        proj_capture(&autosave_buf, &autosave_dl);
        h = autosave_hash_of(&autosave_buf);            /* (as autosave_tick) */
        if (h != autosave_hash)
            (void)autosave_put(h);
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
#include "snapshots/snapshots.c"     /* whole-state snapshots: the work, every section, the song in a slot (docs/SNAPSHOTS.md) */
#endif
#endif /* PROJ_HOST */
