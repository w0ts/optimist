/* SPDX-License-Identifier: GPL-3.0-only */
/* Song sections, compressed (FELUCCA_SECTIONS: the section log, sec_log.c). A section is a project_t and its drum
 * record; stored, it is a small custom record, no general LZ (user design, 2026-10-06):
 *   - each track's values: a bitmap of those that differ from the build-independent baseline (TP's defaults; the
 *     EDIT values 0), then those values;
 *   - each track's steps up to its pattern length only (LEN): a bitmap of the non-empty ones (a REST on a synth
 *     track, no lane on the drum track: seq.c steps_clear), then those steps (10 bytes each); the steps past LEN
 *     are not kept (they read as empty);
 *   - the FM6 voices (128 B), switches and functions only of the parts in fm6_has; the others' functions read
 *     as their defaults;
 *   - the drum record (lanes and sends, 236 B) only when the project names one (dl_hash != 0);
 *   - the globals: a bitmap against GP's defaults, then the values.
 * Whenever the compressed form would not be smaller, the record is the raw project_t (+ the drum record): the
 * worst case (four full 64-step tracks, three FM6 parts) always fits SEC_REC_MAX.
 * Needs project.c (project_t, proj_trk_t, PJ_NP, PJ_E0, proj_sum), params.c (TP, GP), drum_sends.c (dlrec_t). */
#define SEC_RAW 1u                                    /* flags: the raw project follows */
#define SEC_DL 2u                                     /* flags: a drum record follows */
#define SEC_REC_MAX ((uint32_t)sizeof(project_t) + (uint32_t)sizeof(dlrec_t) + 1u)
#define SEC_TRK_MAX (2u + (PJ_NP + 7u) / 8u + 2u * PJ_NP + NSTEP / 8u + 10u * NSTEP)   /* one track, compressed */
#define SEC_TAIL_MAX (1u + NPART * (128u + 1u + 16u) + 4u + (uint32_t)sizeof(dlrec_t))   /* FM6, the drum record */

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

/* project p and its drum record d -> out (SEC_REC_MAX bytes room); -> the record's length */
static uint32_t sec_encode(const project_t *p, const dlrec_t *d, uint8_t *out)
{
    uint8_t *o = out + 1;
    uint32_t i, k, f = p->dl_hash ? SEC_DL : 0u;
    uint8_t *m;
    m = o, o += 4;                                    /* the globals */
    memset(m, 0, 4);
    for (k = 0; k < G_COUNT && k < 32u; k++)
        if (p->g[k] != GP[k].def) {
            m[k >> 3] |= (uint8_t)(1u << (k & 7u));
            sec_put16(&o, p->g[k]);
        }
    *o++ = p->sel;
    memcpy(o, p->rsv, 3), o += 3;
    for (i = 0; i < NTRK; i++) {
        const proj_trk_t *t = &p->t[i];
        uint32_t n = sec_len(t);
        if ((uint32_t)(o - out) + SEC_TRK_MAX > SEC_REC_MAX)
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
        m = o, o += (n + 7u) / 8u;
        memset(m, 0, (n + 7u) / 8u);
        for (k = 0; k < n; k++) {
            if (!sec_step_empty(&t->step[k], i)) {
                m[k >> 3] |= (uint8_t)(1u << (k & 7u));
                memcpy(o, &t->step[k], 10), o += 10;
            }
        }
    }
    if ((uint32_t)(o - out) + SEC_TAIL_MAX > SEC_REC_MAX)
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
    out[0] = (uint8_t)f;
    return (uint32_t)(o - out);
raw:
    out[0] = (uint8_t)(f | SEC_RAW);
    memcpy(out + 1, p, sizeof *p);
    if (f & SEC_DL)
        memcpy(out + 1 + sizeof *p, d, sizeof *d);
    return 1u + (uint32_t)sizeof *p + ((f & SEC_DL) ? (uint32_t)sizeof *d : 0u);
}

/* a record (n bytes) -> project p and its drum record d (none: all 0); 0 = not a section record */
static int sec_decode(const uint8_t *a, uint32_t n, project_t *p, dlrec_t *d)
{
    const uint8_t *e = a + n, *m;
    uint32_t i, k, f;
    if (n < 1u)
        return 0;
    f = a[0];
    memset(d, 0, sizeof *d);
    if (f & SEC_RAW) {
        if (n != 1u + sizeof *p + ((f & SEC_DL) ? sizeof *d : 0u))
            return 0;
        memcpy(p, a + 1, sizeof *p);
        if (f & SEC_DL)
            memcpy(d, a + 1 + sizeof *p, sizeof *d);
        return proj_ok(p);
    }
    memset(p, 0, sizeof *p);
    p->magic = PROJ_MAGIC;
    p->size = sizeof *p;
    a++;
#define SEC_NEED(x) do { if ((uint32_t)(e - a) < (uint32_t)(x)) return 0; } while (0)
    SEC_NEED(4);
    m = a, a += 4;
    for (k = 0; k < G_COUNT; k++) {
        if (k < 32u && (m[k >> 3] >> (k & 7u)) & 1u) {
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
        uint32_t len;
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
        len = sec_len(t);
        SEC_NEED((len + 7u) / 8u);
        m = a, a += (len + 7u) / 8u;
        for (k = 0; k < NSTEP; k++)
            if (k < len && ((m[k >> 3] >> (k & 7u)) & 1u)) {
                SEC_NEED(10);
                memcpy(&t->step[k], a, 10), a += 10;
            } else
                sec_step_clear(&t->step[k], i);
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
    return a == e;
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
