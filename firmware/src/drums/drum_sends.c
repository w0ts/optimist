/* SPDX-License-Identifier: GPL-3.0-only */
/* Each drum lane's sends (included by drums.c after drum_edit.c): REV, DLY and CHO per lane, into the three FX
 * buses (fx.c), from each drum voice as it renders (drums_mix), 16 x 3. The drum track has its own sends on top, taken
 * from the drum bus after its inserts (fx.c dbus_run; FX slots decision D5, 2026-10-08, which reverses the 2026-10-07
 * "no drum-track send on top"). DRIVE and CUT per lane are the inserts (drum_edit.c, SOUND 2).
 *
 * Levels 0..31: level v sends as a synth track's send at 4v + v / 8 would (dsend_lvl; 31 = 127, the top). A lane
 * left as it is has REV 4 (= 16, GLO > DRUMS REV's old default), no delay, no chorus. The FX bypass (GLO + key 12)
 * leaves every lane dry. With the drum track's SLICER on, the sends are taken after it when every lane sends the
 * same (the same REV, no DLY / CHO: the sum's one reverb send, as before), else before it, per voice.
 *
 * Before 2026-10 a lane's REV could be TRK, the drum track's REV (GLO > DRUMS REV, G_DRREV), and was by default:
 * a project of that time (its G_DRREV not DRREV_MOVED) gives each TRK lane G_DRREV as its own REV, the nearest
 * level (dlrec_migrate, proj_apply); the default (16) is level 4 exactly: such a project sounds as before, sample
 * for sample. A user kit has no G_DRREV: its TRK lanes take level 4. The stored forms are unchanged. */

/* a lane's sends, one word: REV bits 0..4 with bit 15 (else REV 4, DSEND_DEF), DLY 5..9, CHO 10..14. 0 = a lane
 * left as it is. A REV of 4 keeps no bits (dsend_canon), so equal sends = equal words and the default is 0. (Bit 15
 * clear meant TRK before: dlrec_migrate) */
#define DSEND_MAX 31u
#define DSEND_OWN 0x8000u
#define DSEND_DEF 4u                           /* REV of a lane as it is: 16, the old GLO > DRUMS REV default */
#define DRREV_MOVED (-1)                       /* a project's G_DRREV since the lanes' REV took it (proj_capture) */
static uint16_t dsend[DRUM_LANES];             /* the working project's */
static uint8_t fxs_lm[3];                      /* (a tentative definition: fx_slots.c, after, sets it: REV DLY CHO's level
                                                * masks, 0 while the type is in no FX slot) */

/* the drum record: what a project keeps of the drum lanes (flash: its own record, drum_store.c) */
typedef struct {
    dlanes_t l;                                /* drum_edit.c: offsets, sources, sample references, user kit */
    uint16_t snd[DRUM_LANES];                  /* the sends, as dsend[] */
} dlrec_t;
_Static_assert(sizeof(dlrec_t) == 236u, "drum record: the lanes (204) + the sends (32)");

AINL uint32_t dsend_rev(uint32_t w) { return w & DSEND_OWN ? w & 31u : DSEND_DEF; }
AINL uint32_t dsend_dly(uint32_t w) { return (w >> 5) & 31u; }
AINL uint32_t dsend_cho(uint32_t w) { return (w >> 10) & 31u; }
static uint16_t dsend_word(uint32_t rev, uint32_t dly, uint32_t cho)
{
    rev &= 31u;
    return (uint16_t)((rev != DSEND_DEF ? DSEND_OWN | rev : 0u) | (dly & 31u) << 5 | (cho & 31u) << 10);
}
static uint16_t dsend_canon(uint32_t w) { return dsend_word(dsend_rev(w), dsend_dly(w), dsend_cho(w)); }
/* level 0..31 -> a send in a track's steps (0..127): 4v + v / 8 (31: 127) */
AINL int32_t dsend_lvl(uint32_t v) { return (int32_t)(4u * v + (v >> 3)); }
/* a track's send level 0..127 -> the nearest lane level (ties: the lower) */
static uint32_t dsend_near(int32_t g)
{
    uint32_t v, best = 0;
    int32_t e = g < 0 ? -g : g;
    for (v = 1; v <= DSEND_MAX; v++) {
        int32_t x = dsend_lvl(v) - g;
        if ((x < 0 ? -x : x) < e)
            best = v, e = x < 0 ? -x : x;
    }
    return best;
}

#if FELUCCA_MACROS
/* GLO > MACRO SPACE on the lanes' reverb sends (macro.c MK_DREV: scaled by sc / 64, then + add steps, as it moved
 * GLO > DRUMS REV before): written by mac_pre every block; 64 / 0 = home */
static int16_t dsend_msc = 64, dsend_madd;
#define DSEND_MKEY ((uint32_t)(uint16_t)dsend_msc << 9 ^ (uint32_t)(uint16_t)dsend_madd << 17)
#define DSEND_RLVL_AT(l, sc, add) clamp((l) * (sc) / 64 + (add), 0, 127)   /* (macro.c mac_effective_drev: the UI) */
#define DSEND_RLVL(l) DSEND_RLVL_AT(l, dsend_msc, dsend_madd)
#else
#define DSEND_MKEY 0u
#define DSEND_RLVL(l) (l)
#endif

/* the sends of each level 0..31 (Q15) as the voices take them this block: dsend_rt the reverb (MACRO SPACE in it),
 * dsend_lt the delay and chorus; 0 with the FX bypassed. dsend_table refreshes them when the bypass or the macro
 * moved (drums_mix, once a block, XIP: the voices only read them: no multiply, no call in RAM code) */
static int32_t dsend_rt[DSEND_MAX + 1u], dsend_lt[DSEND_MAX + 1u];
static uint32_t dsend_key = 0xFFFFFFFFu;
static __attribute__((noinline)) void dsend_table(int32_t on)
{
    uint32_t v, k = (uint32_t)(on != 0) | DSEND_MKEY;
    if (k == dsend_key)
        return;
    dsend_key = k;
    for (v = 0; v <= DSEND_MAX; v++) {
        dsend_lt[v] = on ? dsend_lvl(v) * 258 : 0;
        dsend_rt[v] = on ? DSEND_RLVL(dsend_lvl(v)) * 258 : 0;
    }
}
/* the bus sends of a drum voice that plays note (Q15, 0 = none), from the tables (dsend_table this block). The click's
 * wood block (76, 77) has no lane: a lane's as it is */
AINL void dsend_of(uint32_t note, int32_t *r, int32_t *d, int32_t *c)
{
    uint32_t w = note == 76u || note == 77u ? 0u : dsend[lane_of_note(note)];
    *r = dsend_rt[dsend_rev(w) & fxs_lm[0]];
    *d = dsend_lt[dsend_dly(w) & fxs_lm[1]];
    *c = dsend_lt[dsend_cho(w) & fxs_lm[2]];
}
#if FELUCCA_GLIDE
/* the sends of lane l (DRUM_LANES: the click's wood block), as dsend_of gives them for its notes */
static void dsend_lane(uint32_t l, int32_t *r, int32_t *d, int32_t *c)
{
    uint32_t w = l < DRUM_LANES ? dsend[l] : 0u;
    *r = dsend_rt[dsend_rev(w) & fxs_lm[0]];
    *d = dsend_lt[dsend_dly(w) & fxs_lm[1]];
    *c = dsend_lt[dsend_cho(w) & fxs_lm[2]];
}
#endif

/* every lane sends the same reverb and nothing else: that send (Q15; the SLICER then takes it after itself, from
 * the sum, as the drum track's one send was), else -1 (each voice its own, before the SLICER). XIP */
static __attribute__((noinline)) int32_t dsend_one(int32_t on)
{
    uint32_t l, w = dsend[0];
    dsend_table(on);
    for (l = 1; l < DRUM_LANES; l++)
        if (dsend[l] != w)
            return -1;
    if ((dsend_dly(w) & fxs_lm[1]) | (dsend_cho(w) & fxs_lm[2]))
        return -1;
    return dsend_rt[dsend_rev(w) & fxs_lm[0]];
}

/* fx.c's bus inputs, written by drums_mix (tentative definitions: fx.c, included after drums.c, defines them) */
static int32_t send_c[CTL], send_d[CTL], send_r[CTL];
static int32_t dsend_buf[CTL];                 /* a voice's samples in this block (drums_mix: DSEND_KEEP) */
/* after a voice's block, its samples [i0, i1): its delay and chorus sends; pre (the SLICER's mono path, the lanes
 * not all alike): its reverb send too, before the SLICER (drums_mix sends the reverb itself otherwise). One loop
 * per bus, apart from the voices' loops; only for a lane with a delay or chorus send, so it runs from XIP (no
 * RAMTEXT; drums_mix calls it through FAR) */
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
/* a record of a project from before 2026-10 whose G_DRREV was g: each TRK lane (bit 15 clear) takes g as its own REV
 * (the nearest level; 16, the default: level 4, the word unchanged) */
static void dlrec_migrate(dlrec_t *d, int32_t g)
{
    uint32_t l, v = dsend_near(clamp(g, 0, 127));
    for (l = 0; l < DRUM_LANES; l++)
        if (!(d->snd[l] & DSEND_OWN))
            d->snd[l] = dsend_word(v, dsend_dly(d->snd[l]), dsend_cho(d->snd[l]));
}
/* d (0: all zero) into the working lanes; g: its project's G_DRREV (DRREV_MOVED: nothing to migrate). The audio ISR
 * must not run meanwhile, or be the caller */
static void dlrec_apply(const dlrec_t *d, int32_t g)
{
    dlrec_t t;
    if (d)
        t = *d;
    else
        memset(&t, 0, sizeof t);
    if (g != DRREV_MOVED)
        dlrec_migrate(&t, g);
    dlrec_fix(&t);
    dl = t.l;
    memcpy(dsend, t.snd, sizeof dsend);
}
/* the record's key: 0 = all zero (every lane the kit as it is, sends as they are: nothing to store), else FNV-1a */
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
    {"REV", F_INT, 0, (int16_t)DSEND_MAX, (int16_t)DSEND_DEF, 0, 0},
    {"DLY", F_INT, 0, (int16_t)DSEND_MAX, 0, 0, 0},
    {"CHO", F_INT, 0, (int16_t)DSEND_MAX, 0, 0, 0},
};
static int16_t dsend_v[3];
/* value id (0 REV, 1 DLY, 2 CHO) of lane l: its descriptor, *vp its value */
static __attribute__((noinline)) const param_desc_t *dsend_desc(uint32_t l, uint32_t id, int16_t **vp)
{
    uint32_t w = dsend[l & 15u];
    if (id > 2u)
        return 0;
    dsend_v[id] = (int16_t)(id == 0u ? dsend_rev(w) : id == 1u ? dsend_dly(w) : dsend_cho(w));
    *vp = &dsend_v[id];
    return &DSEND_DESC[id];
}
static __attribute__((noinline)) void dsend_set(uint32_t l, uint32_t id, int32_t v)   /* a knob: the next block hears it */
{
    uint32_t w = dsend[l & 15u], r = dsend_rev(w), dy = dsend_dly(w), ch = dsend_cho(w);
    uint32_t x = (uint32_t)clamp(v, 0, (int32_t)DSEND_MAX);
    if (id == 0u)
        r = x;
    else if (id == 1u)
        dy = x;
    else if (id == 2u)
        ch = x;
    dsend[l & 15u] = dsend_word(r, dy, ch);
}
