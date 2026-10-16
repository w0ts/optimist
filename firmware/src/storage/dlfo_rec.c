/* SPDX-License-Identifier: GPL-3.0-only */
/* The drum lanes' LFO record (drums/lane_play.c: one LFO a lane; the user, 2026-10-12, D6): what a project keeps of
 * them, outside project_t (FUNB is full) and outside the FX record (fx_rec.c: 13 bytes left of its 96, and an older
 * reader refuses a longer FX record whole), built as the FX record is: a store per project buffer, named by the
 * project's sum, each holding the record's stored form (lane_play.c dlfo_encode). Included by project.c beside
 * fx_rec.c; in flash: sections/dlfo_rec_log.c (its own log ids, which an older build skips: it reads the project with
 * every LFO off, and loses nothing else).
 *   proj_capture  the working LFOs, encoded, into the store of the buffer it captured into
 *   proj_apply    decoded from the store of the buffer it applies (none, or another project's: every LFO off, as a
 *                 project from before plays)
 * Nothing to keep (every lane at the defaults): no record, so most projects write nothing new. */
/* The record, since 2026-10-14 the project's modulation record: the drum lanes' LFOs, then the synth parts' modulation
 * slots, each in fields of its own (the user, 2026-10-14: no value packed into another's words):
 *   byte 0      MREC_TAG (a record of before: the lanes' stored form alone, lane_play.c DLFO_VER)
 *   byte 1      the lanes' stored form's length L (0: every lane at its defaults), then those L bytes (dlfo_encode)
 *   then        each part's MREC_PART bytes (int8): LFO DEST A AMT A DEST B AMT B (P_L1DA ..), LFO 2 RATE WAVE TRIG POL
 *               DEST A AMT A DEST B AMT B (P_L2RATE ..), ENV 1 MOD's PIT and three moves (P_E1PIT ..), ENV2 DEST's four
 *               (P_N2DA ..); a value this build lacks: its default written, read and dropped
 * A project saved since writes the words of the depths of before (LFO DEST's, ENV2's amounts, the drum track's LFO 2
 * slots) at 0, ENV 1's FLT SHP in their words, and this record whenever a value or a lane is not at its default; one
 * without the parts' block (of before, or every value at its default) reads those words as before (proj_apply): 0 gives
 * the defaults. MREC_TAG_D2 (2026-10-14 .. 16): ENV 1's four bytes were ENV DEST's slots, DEST A AMT A DEST B AMT B,
 * folded on reading (env_store.h env1_fold; ENV DEST FLT SHP's words were 0 then) */
#define MREC_TAG 0xD3u
#define MREC_TAG_D2 0xD2u
#define MREC_TAG_OK(x) ((x) == MREC_TAG || (x) == MREC_TAG_D2)
#define MREC_PART 20u
#define DLR_MAX (2u + DLFO_ENC_MAX + NPART * MREC_PART)
static void proj_patch(int16_t *v, uint32_t k);        /* (project.c: a track's values as stored, under the motion) */
/* the first id of field group g (0 LFO, 1 LFO 2, 2 ENV, 3 ENV2) and its size; P_COUNT: not in this build */
static uint32_t mrec_grp(uint32_t g, uint32_t *n)
{
    *n = g == 1u ? 8u : 4u;
#if FELUCCA_LFO2
    if (g == 1u)
        return P_L2RATE;
#endif
#if FELUCCA_ANALOG2
    if (g == 3u)
        return P_N2DA;
#endif
    return g == 0u ? P_L1DA : g == 2u ? P_E1PIT : P_COUNT;
}
/* the parts' block -> o (NPART x MREC_PART); -> 1 when a value is not its default */
static uint32_t mrec_parts_put(uint8_t *o)
{
    static const int8_t L2D[8] = {60, 0, 0, 0, 0, 0, 0, 0};   /* (LFO 2's defaults, a build without it) */
    int16_t v[P_COUNT];
    uint32_t k, g, i, n, at = 0, nd = 0;
    for (k = 0; k < NPART; k++) {
        proj_patch(v, k);
        for (g = 0; g < 4u; g++) {
            uint32_t f = mrec_grp(g, &n);
            for (i = 0; i < n; i++, at++) {
                o[at] = (uint8_t)(f < P_COUNT ? (int8_t)v[f + i] : g == 1u ? L2D[i] : g == 2u ? (int8_t)TP[P_E1PIT + i].def : 0);
                nd |= f < P_COUNT && v[f + i] != TP[f + i].def;
            }
        }
    }
    return nd;
}
/* part k's MREC_PART bytes of the parts' block a (record tag tag) onto its values v (P_COUNT ids), each inside its range
 * (a DEST this build lacks: OFF). MREC_TAG_D2: ENV DEST's slots folded onto ENV 1 MOD (after ENV2's, which a slot may
 * move to) -> the slots not kept */
static __attribute__((noinline)) uint32_t mrec_part_vals(const uint8_t *a, uint32_t k, int16_t *v, uint32_t tag)
{
    uint32_t g, i, n, at = k * MREC_PART, d2 = tag == MREC_TAG_D2;
    for (g = 0; g < 4u; g++) {
        uint32_t f = mrec_grp(g, &n);
        for (i = 0; i < n; i++, at++)
            if (f < P_COUNT)                            /* (ENV DEST's slots of 0xD2 as they are: env1_fold) */
                v[f + i] = (int16_t)(g == 2u && d2 ? (int8_t)a[at] : clamp((int8_t)a[at], TP[f + i].min, TP[f + i].max));
        if (f < P_COUNT && g != 2u)                    /* (ENV 1 MOD: depths, no DEST) */
            for (i = g == 1u ? 4u : 0u; i < n; i += 2u)
                if (v[f + i] >= LT_NB)
                    v[f + i] = LT_OFF, v[f + i + 1u] = 0;
    }
    return d2 ? env1_fold(v) : 0u;
}
static void mrec_parts_get(const uint8_t *a, uint32_t tag)
{
    uint32_t k;
    for (k = 0; k < NPART; k++)
        if (mrec_part_vals(a, k, trk[k].p, tag))
            env1_drop |= (uint8_t)(1u << k);
}
/* the record's stored form -> o: the tag, the lanes', the parts'; 0 bytes when every one is at its defaults (no record:
 * read as a project of before, whose words at 0 give those very defaults) */
static uint32_t mrec_encode(uint8_t *o)
{
    uint32_t n = dlfo_encode(o + 2);
    o[0] = MREC_TAG, o[1] = (uint8_t)n;
    if (!mrec_parts_put(o + 2 + n) && !n)
        return 0;
    return 2u + n + NPART * MREC_PART;
}
/* a stored form (0 / 0: none) onto the lanes (refused: every LFO off) and the parts (when it has them) -> the record's
 * tag when the parts' block was there (MREC_TAG, MREC_TAG_D2), else 0 */
static uint32_t mrec_decode(const uint8_t *a, uint32_t n)
{
    uint32_t l;
    if (!a || n < 2u || !MREC_TAG_OK(a[0])) {           /* (none, or a record of before: the lanes' alone) */
        if (!a || !dlfo_decode(a, n))
            (void)dlfo_decode(0, 0);
        return 0;
    }
    l = a[1];
    if (2u + l > n || (l ? !dlfo_decode(a + 2, l) : !dlfo_decode(0, 0)))
        (void)dlfo_decode(0, 0);
    if (n < 2u + l + NPART * MREC_PART)
        return 0;
    mrec_parts_get(a + 2 + l, a[0]);
    return a[0];
}
#define DLR_AUX 4u
typedef struct {
    uint32_t psum;                                     /* the project this belongs to (0: none) */
    uint16_t n;                                        /* the stored form's length (0: nothing to keep) */
    uint8_t b[DLR_MAX];
} dlr_store_t;
static dlr_store_t dlr_aux[DLR_AUX] __attribute__((section(".pool")));
static const project_t *dlr_aux_p[DLR_AUX];

/* the store of a project buffer; mk: take a free one for a buffer that has none (0: none) */
static dlr_store_t *dlr_for(const project_t *p, int mk)
{
    uint32_t i;
    for (i = 0; i < DLR_AUX; i++)
        if (dlr_aux_p[i] == p)
            return &dlr_aux[i];
    for (i = 0; mk && i < DLR_AUX; i++)
        if (!dlr_aux_p[i]) {
            dlr_aux_p[i] = p;
            return &dlr_aux[i];
        }
    return 0;
}
/* the buffer's store if it belongs to p as it is now, else 0 */
static const dlr_store_t *dlr_of(const project_t *p)
{
    const dlr_store_t *m = dlr_for(p, 0);
    return m && m->psum == p->sum ? m : 0;
}
/* the parts' block of the record of project p (in its buffer's store), its tag in *tag; 0: none (a project of before,
 * or every slot at its default: its words then; SLOOP's exporter) */
static const uint8_t *mrec_parts_of(const project_t *p, uint32_t *tag)
{
    const dlr_store_t *m = dlr_of(p);
    if (!m || m->n < 2u || !MREC_TAG_OK(m->b[0]) || 2u + m->b[1] + NPART * MREC_PART > m->n)
        return 0;
    *tag = m->b[0];
    return m->b + 2 + m->b[1];
}
/* proj_capture, after the sum: the working LFOs into the buffer's store */
static void dlr_capture_store(const project_t *p)
{
    dlr_store_t *m = dlr_for(p, 1);
    if (!m)
        return;
    m->n = (uint16_t)mrec_encode(m->b);
    m->psum = p->sum;
}
/* proj_apply (a load, a song section; the ISR or interrupts off): none, refused: every LFO off -> the parts' block's
 * tag (0: none) */
static uint32_t dlr_apply_store(const project_t *p)
{
    const dlr_store_t *m = dlr_of(p);
    env1_drop = 0;
    return mrec_decode(m ? m->b : 0, m ? m->n : 0u);
}
/* the record of the project in buffer p (key first) -> o (4 + DLR_MAX), its length; 0: nothing to keep */
static uint32_t dlr_rec(uint32_t key, const project_t *p, uint8_t *o)
{
    const dlr_store_t *m = dlr_of(p);
    if (!m || !m->n)
        return 0;
    memcpy(o, &key, 4);
    memcpy(o + 4, m->b, m->n);
    return 4u + m->n;
}
/* a record (n bytes at r) into p's store when its key is key, else none */
static void dlr_from_rec(const project_t *p, const uint8_t *r, uint32_t n, uint32_t key)
{
    dlr_store_t *m = dlr_for(p, 1);
    uint32_t k = 0;
    if (!m)
        return;
    if (n >= 4u)
        memcpy(&k, r, 4);
    m->psum = p->sum;
    m->n = (uint16_t)(n > 4u && n <= 4u + DLR_MAX && k == key ? n - 4u : 0u);
    memcpy(m->b, r + 4, m->n);
}
/* the autosave's change test: a hash of the LFOs captured with autosave_buf (turned, so it never cancels the FX
 * record's equal hash in the XOR the test makes of them) */
static uint32_t dlr_hash(const project_t *p)
{
    const dlr_store_t *m = dlr_of(p);
    uint32_t h = m && m->n ? proj_hash(m->b, m->n) : 0u;
    return h << 7 | h >> 25;
}
#define DLR_HASH(p) dlr_hash(p)
