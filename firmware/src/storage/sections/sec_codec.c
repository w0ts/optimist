/* SPDX-License-Identifier: GPL-3.0-only */
/* Song sections, compressed (FELUCCA_SECTIONS: the section log, sec_log.c). A section is a project_t and its drum
 * record; stored, it is a small custom record, no general LZ (user design, 2026-10-06):
 *   - each track's values: a bitmap of those that differ from the build-independent baseline (TP's defaults; the
 *     EDIT values 0), then those values;
 *   - each track's steps up to its pattern length only (LEN): a bitmap of the non-empty ones (a REST on a synth
 *     track, no lane on the drum track: seq.c steps_clear), then those steps; the steps past LEN are not kept
 *     (they read as empty). A step is 10 bytes (codec A, every record before 2026-10-07) or, codec B
 *     (docs/PATTERNS-DESIGN.md phase 0b), a mask of its non-zero bytes then those bytes: a busy 64-step
 *     section takes about half. The encoder keeps B when its steps take fewer bytes than A's (dense random
 *     steps do not), so a record is never longer than before;
 *   - the FM6 voices (128 B), switches and functions only of the parts in fm6_has; the others' functions read
 *     as their defaults;
 *   - the drum record (lanes and sends, 236 B) only when the project names one (dl_hash != 0);
 *   - the globals: a bitmap against GP's defaults, then the values.
 * Whenever the compressed form would not be smaller, the record is the raw project_t (+ the drum record): the
 * worst case (four full 64-step tracks, three FM6 parts) always fits SEC_RAW_N.
 * Motion (FELUCCA_MOTION, motion.c): a section with recorded knob moves carries them in a chunk right after the
 * flags byte (SEC_MOT: count, the PLAY bits, count x 3-byte events, as motion.c keeps them); none recorded, no
 * chunk: it costs nothing. With a chunk, a raw body leaves out the project's magic, size and sum (rebuilt), so
 * the worst case still fits one log sector. Every build reads a chunk (a build without motion skips it: the
 * section loads, without its motion), and SEC_REC_MAX counts it in every build: a record is never too long for
 * another build's log (sec_log.c seals a sector at a record longer than SEC_REC_MAX).
 * Needs project.c (project_t, proj_trk_t, PJ_NP, PJ_E0, proj_sum), params.c (TP, GP), drum_sends.c (dlrec_t). */
#define SEC_RAW 1u                                    /* flags: the raw project follows */
#define SEC_DL 2u                                     /* flags: a drum record follows */
#define SEC_MOT 4u                                    /* flags: a motion chunk follows the flags byte */
#if FELUCCA_ANALOG2
/* flags: the project as format 11 (FUNB: ENV2's extras packed, project.c pj_x; the motion with today's parameter
 * numbers). A record without it (written before) has format 10's layout: sec_decode converts it (proj_va_fix,
 * motion_from_va), the same sound. Set on every record written */
#define SEC_V2 8u
#else
#define SEC_V2 0u
#endif
/* flags: codec B steps. Always written with SEC_RAW (a compressed body says SEC_RAW | SEC_B): a firmware older
 * than codec B takes the record for a raw one, whose exact length it checks, and a compressed record is always
 * shorter than the raw one: it refuses the record (reads it as empty) and never decodes B steps as 10-byte ones.
 * SEC_RAW alone: the raw project. SEC_B alone: not a record */
#define SEC_B 16u
/* flags: a scene (docs/PATTERNS-DESIGN.md 2.2, every build reads it): the body without its steps (each track's step
 * bitmap all 0), then 4 bytes, each track's pattern (slot 0..15, PAT_NONE: the track empty, PAT_KEEP: it goes on
 * with what it plays); pat.c puts the patterns in (sec_read: flattened). Never raw, never with a motion chunk (the
 * motion is the patterns'). A firmware before phase 1 refuses the flag: the section reads as empty there, kept */
#define SEC_SCN 32u
/* flags this build does not know (a later firmware's): the record is refused, never read as something it is not */
#define SEC_UNKNOWN 0xC0u
#define sec_is_raw(f) (((f) & (SEC_RAW | SEC_B)) == SEC_RAW)
#define SEC_MOT_MAX (2u + 3u * 64u)                   /* count, PLAY bits, 64 events (motion.c MOTION_MAX) */
#define SEC_RAW_N ((uint32_t)sizeof(project_t) + (uint32_t)sizeof(dlrec_t) + 1u)   /* a raw record, no motion */
#define SEC_RAWT_N ((uint32_t)sizeof(project_t) - 12u)  /* raw with motion: the project less magic, size, sum */
#define SEC_REC_MAX SEC_REC_N                         /* the longest record (project.c: proj_tmp receives one) */
_Static_assert(SEC_REC_MAX == 1u + SEC_MOT_MAX + SEC_RAWT_N + (uint32_t)sizeof(dlrec_t), "raw, motion, drum record");
_Static_assert(__builtin_offsetof(project_t, g) == 8u && __builtin_offsetof(project_t, sum) == sizeof(project_t) - 4u,
               "raw with motion: magic, size first, sum last");
_Static_assert(SEC_REC_MAX >= SEC_RAW_N, "the longest record");
_Static_assert(PJ_NG <= 32u, "the globals' mask: 4 bytes (the G_* after PJ_NG live in rsv: project.c mc_pack)");
#define SEC_TRK_MAX(sb) (2u + (PJ_NP + 7u) / 8u + 2u * PJ_NP + NSTEP / 8u + (sb) * NSTEP)   /* one track, compressed */
#define SEC_STEP_B_MAX 12u                            /* a codec B step: 2 mask bytes, its 10 bytes */
#define SEC_TAIL_MAX (1u + NPART * (128u + 1u + 16u) + 4u + (uint32_t)sizeof(dlrec_t))   /* FM6, the drum record */
#include "../../seq/stepx.h"
/* the longest scene (SEC_SCN: no step) and pattern record (pat.c: 64 motion events, 64 codec A steps, the extras) */
#define SCN_REC_MAX (9u + 2u * PJ_NG + NTRK * SEC_TRK_MAX(0u) + SEC_TAIL_MAX + 4u)
#define PAT_REC_MAX (6u + 3u * 64u + NSTEP / 8u + 10u * NSTEP + STEPX_ENC_TRK_MAX)
_Static_assert(PAT_REC_MAX <= SEC_REC_MAX && SCN_REC_MAX <= SEC_REC_MAX, "patterns and scenes: shorter than a section");

static int16_t sec_base(uint32_t k) { return k < PJ_E0 ? TP[k].def : 0; }   /* a track's value k (PJ layout) */
static uint32_t sec_len(const proj_trk_t *t)                                  /* the steps kept: LEN */
{
    int32_t n = t->p[P_SLEN];
    return n < 1 ? 1u : n > NSTEP ? NSTEP : (uint32_t)n;
}
/* an empty step (seq.c steps_clear): a synth track's is a REST, the drum track's all 0 */
static void sec_step_clear(step_t *s, uint32_t trk)
{
    memset(s, 0, sizeof *s);
    if (trk != TRK_DRUM)
        s->time = ST_REST;
}
static int sec_step_empty(const step_t *s, uint32_t trk)
{
    step_t e;
    sec_step_clear(&e, trk);
    return !memcmp(s, &e, sizeof e);
}
static void sec_put16(uint8_t **o, int16_t v) { (*o)[0] = (uint8_t)v, (*o)[1] = (uint8_t)((uint16_t)v >> 8), *o += 2; }
static int16_t sec_get16(const uint8_t **a) { int16_t v = (int16_t)((*a)[0] | (*a)[1] << 8); *a += 2; return v; }

/* codec B, one step (10 bytes s) -> o (0: only counted), -> its length: a mask of the non-zero bytes, then those
 * bytes. The mask's first byte has bytes 0..6 in bits 0..6 and bit 7 set when a second byte follows with bytes
 * 7..9 in bits 0..2 (a synth step's vel / lvl / rat, a drum step's last levels and ratchets: often all 0) */
static uint32_t sec_step_b(const uint8_t *s, uint8_t *o)
{
    uint32_t m = 0, j, n;
    for (j = 0; j < 10u; j++)
        m |= (uint32_t)(s[j] != 0) << j;
    n = m >> 7 ? 2u : 1u;
    if (!o) {
        for (j = 0; j < 10u; j++)
            n += s[j] != 0;
        return n;
    }
    o[0] = (uint8_t)((m & 0x7Fu) | (m >> 7 ? 0x80u : 0u));
    if (m >> 7)
        o[1] = (uint8_t)(m >> 7);
    for (j = 0; j < 10u; j++)
        if (s[j])
            o[n++] = s[j];
    return n;
}
/* codec B, one step at *a (e: the record's end) -> s (10 bytes); 0 cut short or not as sec_step_b writes it */
static int sec_unstep_b(const uint8_t **a, const uint8_t *e, uint8_t *s)
{
    uint32_t m, j;
    if (*a >= e)
        return 0;
    m = **a & 0x7Fu;
    if (*(*a)++ & 0x80u) {
        if (*a >= e || !**a || **a > 7u)
            return 0;
        m |= (uint32_t)*(*a)++ << 7;
    }
    for (j = 0; j < 10u; j++) {
        s[j] = 0;
        if ((m >> j) & 1u) {
            if (*a >= e || !**a)
                return 0;
            s[j] = *(*a)++;
        }
    }
    return 1;
}
#ifdef SEC_TEST_A
static int sec_test_a;                                /* (the host tests: codec A, as every record before phase 0b) */
#else
#define sec_test_a 0
#endif
/* the bytes codec B saves on track t's steps (trk: its number) against codec A's 10 a step (< 0: B is longer) */
static int32_t sec_b_gain(const proj_trk_t *t, uint32_t trk)
{
    uint32_t k;
    int32_t g = 0;
    for (k = 0; k < sec_len(t); k++)
        if (!sec_step_empty(&t->step[k], trk))
            g += 10 - (int32_t)sec_step_b((const uint8_t *)&t->step[k], 0);
    return g;
}
/* codec B for p's steps: 1 when they take fewer bytes than codec A's 10 a step */
static int sec_use_b(const project_t *p, int nost)
{
    uint32_t i;
    int32_t g = 0;
    if (sec_test_a || nost)
        return 0;
    for (i = 0; i < NTRK; i++)
        g += sec_b_gain(&p->t[i], i);
    return g > 0;
}
/* track t's steps up to its LEN (trk: its number; b: codec B; nost: none kept) -> o: their bitmap, the non-empty
 * ones; -> the end (a section's track, a pattern) */
static uint8_t *sec_steps_put(const proj_trk_t *t, uint32_t trk, uint8_t *o, int b, int nost)
{
    uint32_t k, n = sec_len(t);
    uint8_t *m = o;
    o += (n + 7u) / 8u;
    memset(m, 0, (n + 7u) / 8u);
    for (k = 0; k < n; k++)
        if (!nost && !sec_step_empty(&t->step[k], trk)) {
            m[k >> 3] |= (uint8_t)(1u << (k & 7u));
            if (b)
                o += sec_step_b((const uint8_t *)&t->step[k], o);
            else
                memcpy(o, &t->step[k], 10), o += 10;
        }
    return o;
}
/* sec_steps_put's form at *a (e: the record's end) -> t's 64 steps (past LEN: empty); 0 cut short */
static int sec_steps_get(proj_trk_t *t, uint32_t trk, const uint8_t **a, const uint8_t *e, int b)
{
    uint32_t k, len = sec_len(t);
    const uint8_t *m = *a;
    if ((uint32_t)(e - m) < (len + 7u) / 8u)
        return 0;
    *a += (len + 7u) / 8u;
    for (k = 0; k < NSTEP; k++)
        if (k < len && ((m[k >> 3] >> (k & 7u)) & 1u)) {
            if (b) {
                if (!sec_unstep_b(a, e, (uint8_t *)&t->step[k]))
                    return 0;
            } else {
                if ((uint32_t)(e - *a) < 10u)
                    return 0;
                memcpy(&t->step[k], *a, 10), *a += 10;
            }
        } else
            sec_step_clear(&t->step[k], trk);
    return 1;
}

/* project p and its drum record d -> out (SEC_RAW_N bytes room), no motion; -> the record's length. nost: every
 * step left out (a scene's body: never raw) */
static uint32_t sec_body(const project_t *p, const dlrec_t *d, uint8_t *out, int nost)
{
    uint8_t *o = out + 1;
    uint32_t i, k, f = (p->dl_hash ? SEC_DL : 0u) | SEC_V2, b = (uint32_t)sec_use_b(p, nost);
    uint8_t *m;
    m = o, o += 4;                                    /* the globals */
    memset(m, 0, 4);
    for (k = 0; k < PJ_NG; k++)
        if (p->g[k] != GP[k].def) {
            m[k >> 3] |= (uint8_t)(1u << (k & 7u));
            sec_put16(&o, p->g[k]);
        }
    *o++ = p->sel;
    memcpy(o, p->rsv, 3), o += 3;
    for (i = 0; i < NTRK; i++) {
        const proj_trk_t *t = &p->t[i];
        if ((uint32_t)(o - out) + SEC_TRK_MAX(b ? SEC_STEP_B_MAX : 10u) > SEC_RAW_N)
            goto raw;                                 /* (it could only grow past the raw record: raw) */
        *o++ = t->engine;
        *o++ = t->preset;
        m = o, o += (PJ_NP + 7u) / 8u;
        memset(m, 0, (PJ_NP + 7u) / 8u);
        for (k = 0; k < PJ_NP; k++)
            if (t->p[k] != sec_base(k)) {
                m[k >> 3] |= (uint8_t)(1u << (k & 7u));
                sec_put16(&o, t->p[k]);
            }
        o = sec_steps_put(t, i, o, (int)b, nost);
    }
    if ((uint32_t)(o - out) + SEC_TAIL_MAX > SEC_RAW_N)
        goto raw;
    *o++ = p->fm6_has;
    for (i = 0; i < NPART; i++)
        if ((p->fm6_has >> i) & 1u) {
            memcpy(o, p->fm6[i], 128), o += 128;
            *o++ = p->fm6_on[i];
            memcpy(o, p->fm6_fn[i], 16), o += 16;
        }
    memcpy(o, &p->dl_hash, 4), o += 4;
    if (f & SEC_DL)
        memcpy(o, d, sizeof *d), o += sizeof *d;
    if ((uint32_t)(o - out) >= sizeof *p + ((f & SEC_DL) ? sizeof *d : 0u) + 1u)
        goto raw;
    out[0] = (uint8_t)(f | (b ? SEC_RAW | SEC_B : 0u));
    return (uint32_t)(o - out);
raw:
    out[0] = (uint8_t)(f | SEC_RAW);
    memcpy(out + 1, p, sizeof *p);
    if (f & SEC_DL)
        memcpy(out + 1 + sizeof *p, d, sizeof *d);
    return 1u + (uint32_t)sizeof *p + ((f & SEC_DL) ? (uint32_t)sizeof *d : 0u);
}

#if FELUCCA_MOTION
/* the record (n bytes in out, SEC_REC_MAX room) with p's motion store m (not empty): its chunk after the flags
 * byte; a raw body, or a compressed one no smaller than the raw one without magic, size, sum: that raw one */
static uint32_t sec_put_motion(uint8_t *out, uint32_t n, const project_t *p, const dlrec_t *d, const motion_store_t *m)
{
    uint32_t c = 2u + 3u * m->count, dl = (out[0] & SEC_DL) ? (uint32_t)sizeof *d : 0u;
    if (sec_is_raw(out[0]) || n - 1u >= SEC_RAWT_N + dl) {
        out[0] = (uint8_t)((out[0] & ~SEC_B) | SEC_RAW);
        memcpy(out + 1 + c, (const uint8_t *)p + 8, SEC_RAWT_N);
        if (dl)
            memcpy(out + 1 + c + SEC_RAWT_N, d, dl);
        n = 1u + SEC_RAWT_N + dl;
    } else {
        uint32_t i;
        for (i = n - 1u; i; i--)                      /* (the body up past the chunk; from its end: no memmove) */
            out[c + i] = out[i];
    }
    out[0] |= SEC_MOT;
    out[1] = m->count;
    out[2] = m->on;
    memcpy(out + 3, m->ev, 3u * m->count);
    return n + c;
}
/* a record decoded into p (ok): its chunk (none: 0) becomes p's motion store (motion_proj.c), none an empty one;
 * va: the record was format 10's (where each part's AMT2 went: its motion renumbered), else 0 */
static int sec_take_motion(int ok, const project_t *p, const uint8_t *ch, const int8_t *va)
{
    motion_store_t *m = ok ? motion_for(p, 1) : 0;
    if (m) {
        m->psum = p->sum;
        m->count = ch ? ch[0] : 0u;
        m->on = ch ? ch[1] : 0u;
        if (ch)
            memcpy(m->ev, ch + 2, 3u * ch[0]);
#if FELUCCA_ANALOG2
        if (va)
            motion_from_va(m, va);
#endif
    }
    (void)va;
    return ok;
}
#else
#define sec_take_motion(ok, p, ch, va) (ok)
#endif
#if FELUCCA_ANALOG2
/* a record without SEC_V2 (format 10's layout, decoded into p): converted to FUNB, the same sound; -> va (the
 * motion's renumbering) */
static const int8_t *sec_from_va(uint32_t f, project_t *p, int8_t *va)
{
    if (f & SEC_V2)
        return 0;
    proj_va_fix(p, va);
    return va;
}
#endif

/* project p and its drum record d -> out (SEC_REC_MAX bytes room), with p's motion when it has one; -> the
 * record's length */
static uint32_t sec_encode(const project_t *p, const dlrec_t *d, uint8_t *out)
{
    uint32_t n = sec_body(p, d, out, 0);
#if FELUCCA_MOTION
    const motion_store_t *m = motion_for(p, 0);
    if (m && m->psum == p->sum && m->count <= 64u && (m->count || m->on))
        n = sec_put_motion(out, n, p, d, m);
#endif
    return n;
}

/* a record (n bytes) -> project p and its drum record d (none: all 0), its motion (motion builds); 0 = not a
 * section record */
static int sec_decode(const uint8_t *a, uint32_t n, project_t *p, dlrec_t *d)
{
    const uint8_t *e = a + n, *m, *ch = 0;
    const int8_t *va = 0;
    uint32_t i, k, f, c = 0;
#if FELUCCA_ANALOG2
    int8_t vad[NPART];
#endif
    if (n < 1u)
        return 0;
    f = a[0];
    if ((f & SEC_UNKNOWN) || (f & (SEC_RAW | SEC_B)) == SEC_B || ((f & SEC_SCN) && (sec_is_raw(f) || n < 5u)))
        return 0;                                     /* (a later firmware's record, or not one) */
    if (f & SEC_SCN)
        e -= 4, n -= 4u;                              /* (its patterns: sec_read) */
    if (f & SEC_MOT) {                                /* the motion chunk: its count bounds it */
        if (n < 3u || a[1] > 64u || n < 3u + 3u * a[1])
            return 0;
        ch = a + 1;
        c = 2u + 3u * a[1];
    }
    memset(d, 0, sizeof *d);
    if (sec_is_raw(f)) {
        uint32_t pn = ch ? SEC_RAWT_N : (uint32_t)sizeof *p;
        if (n != 1u + c + pn + ((f & SEC_DL) ? sizeof *d : 0u))
            return 0;
        if (ch) {
            memcpy((uint8_t *)p + 8, a + 1 + c, pn);
            p->magic = PROJ_MAGIC;
            p->size = sizeof *p;
            p->sum = proj_sum(p);
        } else
            memcpy(p, a + 1, sizeof *p);
        if (f & SEC_DL)
            memcpy(d, a + 1 + c + pn, sizeof *d);
#if FELUCCA_ANALOG2
        if (!(f & SEC_V2) && !ch) {                   /* (format 10's raw project: FUNA, its own sum) */
            if (p->magic != PROJ_MAGIC_VA || p->size != sizeof *p || p->sum != proj_sum(p))
                return 0;
            p->magic = PROJ_MAGIC;
            p->sum = proj_sum(p);
        }
        va = sec_from_va(f, p, vad);
#endif
        return sec_take_motion(proj_ok(p), p, ch, va);
    }
    memset(p, 0, sizeof *p);
    p->magic = PROJ_MAGIC;
    p->size = sizeof *p;
    a += 1u + c;
#define SEC_NEED(x) do { if ((uint32_t)(e - a) < (uint32_t)(x)) return 0; } while (0)
    SEC_NEED(4);
    m = a, a += 4;
    for (k = 0; k < PJ_NG; k++) {
        if ((m[k >> 3] >> (k & 7u)) & 1u) {
            SEC_NEED(2);
            p->g[k] = sec_get16(&a);
        } else
            p->g[k] = GP[k].def;
    }
    SEC_NEED(4);
    p->sel = *a++;
    memcpy(p->rsv, a, 3), a += 3;
    for (i = 0; i < NTRK; i++) {
        proj_trk_t *t = &p->t[i];

        SEC_NEED(2 + (PJ_NP + 7u) / 8u);
        t->engine = *a++;
        t->preset = *a++;
        m = a, a += (PJ_NP + 7u) / 8u;
        for (k = 0; k < PJ_NP; k++) {
            if ((m[k >> 3] >> (k & 7u)) & 1u) {
                SEC_NEED(2);
                t->p[k] = sec_get16(&a);
            } else
                t->p[k] = sec_base(k);
        }
        if (!sec_steps_get(t, i, &a, e, (f & SEC_B) != 0))
            return 0;
    }
    SEC_NEED(1);
    p->fm6_has = *a++;
    for (i = 0; i < NPART; i++) {
        memset(p->fm6_fn[i], 0xFF, sizeof p->fm6_fn[i]);
        if ((p->fm6_has >> i) & 1u) {
            SEC_NEED(128 + 1 + 16);
            memcpy(p->fm6[i], a, 128), a += 128;
            p->fm6_on[i] = *a++;
            memcpy(p->fm6_fn[i], a, 16), a += 16;
        }
    }
    SEC_NEED(4);
    memcpy(&p->dl_hash, a, 4), a += 4;
    if (f & SEC_DL) {
        SEC_NEED(sizeof *d);
        memcpy(d, a, sizeof *d), a += sizeof *d;
    }
#undef SEC_NEED
    p->sum = proj_sum(p);
#if FELUCCA_ANALOG2
    va = sec_from_va(f, p, vad);
#endif
    return sec_take_motion(a == e, p, ch, va);
}

/* what sec_encode keeps of a project (steps past LEN, the functions of parts without FM6 voice: their defaults);
 * the tests compare a decoded record with this */
static void sec_canon(project_t *p)
{
    uint32_t i, k;
    for (i = 0; i < NTRK; i++)
        for (k = sec_len(&p->t[i]); k < NSTEP; k++)
            sec_step_clear(&p->t[i].step[k], i);
    for (i = 0; i < NPART; i++)
        if (!((p->fm6_has >> i) & 1u)) {
            memset(p->fm6[i], 0, sizeof p->fm6[i]);
            p->fm6_on[i] = 0;
            memset(p->fm6_fn[i], 0xFF, sizeof p->fm6_fn[i]);
        }
    p->sum = proj_sum(p);
}
