/* SPDX-License-Identifier: GPL-3.0-only */
/* Each drum lane's own sends (FELUCCA_DRUM_SENDS; included by drums.c after drum_edit.c): REV, DLY and CHO
 * per lane, into the three FX buses (fx.c), from each drum voice as it renders (drums_mix). DRIVE and CUT
 * per lane are the inserts (drum_edit.c, SOUND 2).
 *
 * How they combine with the drum track's send (GLO > DRUMS REV, G_DRREV): a lane's REV is TRK (the track's
 * REV, as every lane had it before) or the lane's own level, which then replaces the track's for that lane
 * ("reverb on the snare only": DRUMS REV 0, the snare's REV up). The drum track has no delay or chorus send
 * of its own: a lane's DLY and CHO are its sends (0 = none). Levels 0..31: level v sends as the track's REV
 * at 4v + v / 8 would (31 = 127, the top). The FX bypass (GLO + key 12) leaves every lane dry. With the
 * drum track's SLICER on and any lane sending on its own, every send is taken before the SLICER (the buses
 * hear the hits unsliced); with all lanes at TRK / 0 the reverb send stays after the SLICER, as before.
 *
 * Every build keeps the sends with the lanes (a project's drum record, drum_store.c; a user kit, drum_kits.c):
 * a build without the switch keeps them and plays every lane at TRK / 0. All lanes TRK / 0 = the sound as
 * before, sample for sample (the voices' send code is the old one then). */

/* a lane's sends, one word: REV bits 0..4, DLY 5..9, CHO 10..14, bit 15 = REV is the lane's own (else TRK).
 * 0 = TRK, no delay, no chorus. A TRK word keeps its REV bits 0 (dsend_canon), so equal sends = equal words */
#define DSEND_MAX 31u
#define DSEND_OWN 0x8000u
static uint16_t dsend[DRUM_LANES];             /* the working project's */

/* the drum record: what a project keeps of the drum lanes (flash: its own record, drum_store.c) */
typedef struct {
    dlanes_t l;                                /* drum_edit.c: offsets, sources, sample references, user kit */
    uint16_t snd[DRUM_LANES];                  /* the sends, as dsend[] */
} dlrec_t;
_Static_assert(sizeof(dlrec_t) == 236u, "drum record: the lanes (204) + the sends (32)");

AINL int32_t dsend_rev(uint32_t w) { return w & DSEND_OWN ? (int32_t)(w & 31u) : -1; }   /* -1 = TRK */
AINL uint32_t dsend_dly(uint32_t w) { return (w >> 5) & 31u; }
AINL uint32_t dsend_cho(uint32_t w) { return (w >> 10) & 31u; }
static uint16_t dsend_canon(uint32_t w) { return (uint16_t)(w & DSEND_OWN ? w : w & 0x7FE0u); }
static uint16_t dsend_word(int32_t rev, uint32_t dly, uint32_t cho)
{
    return (uint16_t)((rev >= 0 ? DSEND_OWN | ((uint32_t)rev & 31u) : 0u) | (dly & 31u) << 5 | (cho & 31u) << 10);
}
/* level 0..31 -> a bus send, Q15: as the track's REV knob at 4v + v / 8 (31: 127 x 258, its top) */
AINL int32_t dsend_amt(uint32_t v) { return (int32_t)(4u * v + (v >> 3)) * 258; }

/* the bus sends of a drum voice that plays note (Q15, 0 = none); on: the drum track's FX are on, send: the
 * track's REV send. The click's wood block (76, 77) has no lane: the track's send */
#if FELUCCA_DRUM_SENDS
AINL void dsend_of(uint32_t note, int32_t on, int32_t send, int32_t *r, int32_t *d, int32_t *c)
{
    uint32_t w = note == 76u || note == 77u ? 0u : dsend[lane_of_note(note)];
    *r = (w & DSEND_OWN) && on ? dsend_amt(w & 31u) : send;
    *d = on ? dsend_amt(dsend_dly(w)) : 0;
    *c = on ? dsend_amt(dsend_cho(w)) : 0;
}
#else
AINL void dsend_of(uint32_t note, int32_t on, int32_t send, int32_t *r, int32_t *d, int32_t *c)
{
    (void)note, (void)on;
    *r = send;
    *d = *c = 0;
}
#endif
#if FELUCCA_GLIDE
/* the sends of lane l (DRUM_LANES: the click's wood block), as dsend_of gives them for its notes */
static void dsend_lane(uint32_t l, int32_t on, int32_t send, int32_t *r, int32_t *d, int32_t *c)
{
#if FELUCCA_DRUM_SENDS
    uint32_t w = l < DRUM_LANES ? dsend[l] : 0u;
    *r = (w & DSEND_OWN) && on ? dsend_amt(w & 31u) : send;
    *d = on ? dsend_amt(dsend_dly(w)) : 0;
    *c = on ? dsend_amt(dsend_cho(w)) : 0;
#else
    (void)l, (void)on;
    *r = send;
    *d = *c = 0;
#endif
}
#endif

/* a lane sends on its own (not all TRK / 0): the SLICER then takes every send before it */
AINL int dsend_any(void)
{
#if FELUCCA_DRUM_SENDS
    uint32_t l, o = 0;
    for (l = 0; l < DRUM_LANES; l++)
        o |= dsend[l];
    return o != 0u;
#else
    return 0;
#endif
}
#if FELUCCA_DRUM_SENDS
/* fx.c's bus inputs, written by drums_mix (tentative definitions: fx.c, included after drums.c, defines them) */
static int32_t send_c[CTL], send_d[CTL], send_r[CTL];
static int32_t dsend_buf[CTL];                 /* a voice's samples in this block (drums_mix: DSEND_KEEP) */
/* after a voice's block, its samples [i0, i1): its delay and chorus sends; pre (the SLICER's mono path, a
 * lane sending on its own): its reverb send too, before the SLICER (drums_mix sends the reverb itself
 * otherwise, as before). One loop per bus, apart from the voices' loops; only for a lane that sends on its
 * own, so it runs from XIP (no RAMTEXT; drums_mix calls it through FAR) */
static __attribute__((noinline)) void dsend_post(uint32_t i0, uint32_t i1, int32_t r, int32_t d, int32_t c, int32_t pre)
{
    uint32_t i;
    if (pre && r)
        for (i = i0; i < i1; i++)
            send_r[i] += mulq15(dsend_buf[i], r);
    if (d)
        for (i = i0; i < i1; i++)
            send_d[i] += mulq15(dsend_buf[i], d);
    if (c)
        for (i = i0; i < i1; i++)
            send_c[i] += mulq15(dsend_buf[i], c);
}
#define DSEND_KEEP(i, s) (dsend_buf[i] = (s))
#define DSEND_POST(i0, i1, r, d, c, pre) do { if ((d) | (c) | ((pre) & ((r) != 0))) FAR(dsend_post)(i0, i1, r, d, c, pre); } while (0)
#else
#define DSEND_KEEP(i, s) ((void)0)
#define DSEND_POST(i0, i1, r, d, c, pre) ((void)(i0), (void)(i1), (void)(r), (void)(d), (void)(c), (void)(pre))
#endif

/* ---- the drum record <-> the working lanes and sends */
static void dlrec_capture(dlrec_t *d)
{
    d->l = dl;
    memcpy(d->snd, dsend, sizeof d->snd);
}
static __attribute__((noinline)) void dlrec_fix(dlrec_t *d)   /* every value inside its range (a project, a kit, the editor) */
{
    uint32_t l;
    dl_fix(&d->l);
    for (l = 0; l < DRUM_LANES; l++)
        d->snd[l] = dsend_canon(d->snd[l]);
}
static void dlrec_apply(const dlrec_t *d)      /* (the audio ISR must not run meanwhile, or be the caller) */
{
    dlrec_t t;
    if (d)
        t = *d;
    else
        memset(&t, 0, sizeof t);
    dlrec_fix(&t);
    dl = t.l;
    memcpy(dsend, t.snd, sizeof dsend);
}
/* the record's key: 0 = all zero (every lane the kit as it is, TRK / 0: nothing to store), else FNV-1a */
static uint32_t dlrec_hash(const dlrec_t *d)
{
    const uint8_t *b = (const uint8_t *)d;
    uint32_t i, s = 0x811C9DC5u, any = 0;
    for (i = 0; i < sizeof *d; i++) {
        any |= b[i];
        s = (s ^ b[i]) * 16777619u;
    }
    return !any ? 0u : s ? s : 1u;
}

/* ---- SOUND 3 (ui_drums.c, params.c): REV DLY CHO of the sound picked */
static const param_desc_t DSEND_DESC[3] = {
    {"REV", F_INT, -1, (int16_t)DSEND_MAX, -1, 0, 0},  /* -1: TRK */
    {"DLY", F_INT, 0, (int16_t)DSEND_MAX, 0, 0, 0},
    {"CHO", F_INT, 0, (int16_t)DSEND_MAX, 0, 0, 0},
};
static int16_t dsend_v[3];
/* value id (0 REV, 1 DLY, 2 CHO) of lane l: its descriptor, *vp its value; 0 = not in this build */
static __attribute__((noinline)) const param_desc_t *dsend_desc(uint32_t l, uint32_t id, int16_t **vp)
{
    uint32_t w = dsend[l & 15u];
    if (!FELUCCA_DRUM_SENDS || id > 2u)
        return 0;
    dsend_v[id] = (int16_t)(id == 0u ? dsend_rev(w) : id == 1u ? (int32_t)dsend_dly(w) : (int32_t)dsend_cho(w));
    *vp = &dsend_v[id];
    return &DSEND_DESC[id];
}
static __attribute__((noinline)) void dsend_set(uint32_t l, uint32_t id, int32_t v)   /* a knob: the next block hears it */
{
    uint32_t w = dsend[l & 15u];
    int32_t r = dsend_rev(w);
    uint32_t dy = dsend_dly(w), ch = dsend_cho(w);
    if (id == 0u)
        r = clamp(v, -1, (int32_t)DSEND_MAX);
    else if (id == 1u)
        dy = (uint32_t)clamp(v, 0, (int32_t)DSEND_MAX);
    else if (id == 2u)
        ch = (uint32_t)clamp(v, 0, (int32_t)DSEND_MAX);
    dsend[l & 15u] = dsend_word(r, dy, ch);
}
/* param_format: REV at -1 reads TRK; 1 = done */
static int dsend_fmt(const param_desc_t *d, int32_t v, char *val)
{
    if (d != &DSEND_DESC[0] || v >= 0)
        return 0;
    str_cpy(val, "TRK", 6);
    return 1;
}
