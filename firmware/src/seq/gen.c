/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments (EUCLID's formula: Felucca 1.5.6 SEQ TOOLS,
 * hugelton/Felucca 9f6a885, firmware/src/ui_tools.c 338-469) */
/* The note generators (FELUCCA_SEQTOOLS with the Optimist UI; its STEP pages GEN and GEN OUT, ui/optimist/op_tools.c): a
 * layer the sequencer plays OVER the pattern, never written into it. Included by seq.c after chance.c.
 *
 *   gen[]         a slot each synth track (T1..T3: slot k) and each drum step lane (16 sounds + ACCENT: NPART + lane):
 *                 type, mode, on, three values. GEN_TLV in the FX record keeps them with the project (fx/fx_rec.c)
 *   gen_eff       the step the sequencer plays at step i (seq_tick, seq_step's next step, the ratchets): the
 *                 pattern's step, or, with a generator on, the pattern's merged with the generator's output there.
 *                 It goes down the pattern's own path: the step's events (locks, motion, chance, condition, fill,
 *                 nudge, a lane's values) are its place's, whatever plays there; chance then ENERGY then the
 *                 notes then the ratchets, as a stored step
 *   gen_write     WRITE (gen_write_prep with the IRQ on, gen_write_commit with it off): the effective steps 1..LEN into the pattern (the UI, the IRQ off; one undo level, which keeps
 *                 the slots' ON states too: the UI turns the slot OFF after it, an undo brings back both)
 *
 * MODE: REPLACE (the generator's hits are the steps; = the old EUCLID writer bit for bit), ADD (the pattern's steps and
 * the hits), MASK (the pattern's steps only where it hits). A generated hit on a synth track plays the pattern's NOTE
 * there, else the note a TIE there holds, else the track's ROOT (C4 .. F#4, G..B an octave down); a TIE plays on only
 * while its note does. A drum lane's hit: the pattern's level and ratchet there, else NORM x1. A generator's level or
 * ratchet (RANDOM's accent, PULSE's ratchet) is laid over the hit's.
 *
 * A generator is an entry of GEN_TYPES: its name, three values (label, range, default, how shown), a fit (dependent
 * ranges: EUCLID's HITS <= STEPS <= LEN), its step function (values, step, LEN, the slot's seed -> a hit and its level /
 * ratchet) and the state it needs (bytes; the three here need none: each is a pure function of its values, the step
 * and the slot, so a pass plays as the one before, PLAY, a loop start or a launched pattern start it in step, and WRITE
 * writes what was heard). RANDOM draws its own hash, never rng(): the chance rolls stay as they were. Off: one byte
 * tested a step (gen_trk), nothing else. */
#if GEN_BUILT                                            /* (seq.c, before undo.c) */
#define GEN_ON 1
#define GEN_NP 3u                                        /* values a generator */
#define GEN_NSLOT (NPART + DRUM_SLANES)
enum { GT_EUCLID, GT_RANDOM, GT_PULSE, GT_N };
enum { GM_REPLACE, GM_ADD, GM_MASK, GM_N };
enum { GD_INT, GD_PCT, GD_X };                           /* how a value is shown: 5, 50 %, x2 */
typedef struct {
    uint8_t type, mode, on;
    uint8_t p[GEN_NP];
} gen_t;
typedef struct {
    uint8_t hit;
    uint8_t lvl, rat;                                    /* laid over the hit's (0: the hit's own) */
} gen_ev_t;
typedef struct {
    const char *label;
    uint8_t min, max, def, disp;                         /* (max 0: the track's LEN) */
} gen_par_t;
typedef struct {
    const char *name;
    gen_par_t par[GEN_NP];
    void (*fit)(gen_t *g, uint32_t len);                 /* the values within their ranges for this LEN */
    int (*step)(const gen_t *g, uint32_t i, uint32_t len, uint32_t seed, gen_ev_t *e);   /* 1: a hit on step i */
    uint8_t state;                                       /* bytes of state a slot (0: a pure function) */
} gen_type_t;
static gen_t gen[GEN_NSLOT] = {[0 ... GEN_NSLOT - 1u] = {GT_EUCLID, GM_REPLACE, 0, {4, 16, 0}}};   /* (EUCLID's defaults, off) */
static volatile uint8_t gen_trk;                         /* bit k: track k has a generator on (the ISR's test) */
static volatile uint32_t gen_lanes;                      /* the drum lanes with one on */
_Static_assert(NPART == TRK_DRUM && DRUM_SLANES <= 32u, "the slots: the synth tracks, then the drum lanes");

static uint32_t gen_slot(uint32_t k, uint32_t lane) { return k < NPART ? k : NPART + lane % DRUM_SLANES; }
static uint32_t gen_mix(uint32_t h)                      /* (murmur3's finaliser) */
{
    h ^= h >> 16;
    h *= 0x85EBCA6Bu;
    h ^= h >> 13;
    h *= 0xC2B2AE35u;
    return h ^ (h >> 16);
}

/* ---- EUCLID: HITS spread as evenly as they go over a cycle of STEPS, turned by ROT, repeated through LEN */
static uint32_t eu_cyc(const gen_t *g, uint32_t len) { uint32_t n = g->p[1] ? g->p[1] : 16u; return n > len ? len : n ? n : 1u; }
static void eu_gfit(gen_t *g, uint32_t len)
{
    uint32_t n = eu_cyc(g, len);
    g->p[1] = (uint8_t)n;
    g->p[0] = (uint8_t)(g->p[0] > n ? n : g->p[0]);
    g->p[2] = (uint8_t)(g->p[2] % n);
}
static int eu_gstep(const gen_t *g, uint32_t i, uint32_t len, uint32_t seed, gen_ev_t *e)
{
    uint32_t n = eu_cyc(g, len), h = g->p[0] > n ? n : g->p[0], p = (i + n - g->p[2] % n) % n;
    (void)seed, (void)e;
    return (p * h) % n < h;
}
/* ---- RANDOM: each step a hit with DENSITY's chance, drawn from SEED and the slot (the same steps every pass); ACCENT
 * of them HARD */
static void rn_gfit(gen_t *g, uint32_t len) { (void)g, (void)len; }
static int rn_gstep(const gen_t *g, uint32_t i, uint32_t len, uint32_t seed, gen_ev_t *e)
{
    uint32_t h = gen_mix(seed ^ (uint32_t)g->p[1] * 0x9E3779B1u ^ i * 0x85EBCA77u);
    (void)len;
    if (h % 100u >= g->p[0])
        return 0;
    if (gen_mix(h ^ 0x5BD1E995u) % 100u < g->p[2])
        e->lvl = LV_HARD;
    return 1;
}
/* ---- PULSE: a hit every EVERY steps from OFFSET, played as RATCHET hits (x1 .. x4) */
static void pu_gfit(gen_t *g, uint32_t len)
{
    (void)len;
    g->p[0] = (uint8_t)(g->p[0] ? g->p[0] : 1u);
    g->p[1] = (uint8_t)(g->p[1] % g->p[0]);
    g->p[2] = (uint8_t)(g->p[2] < 1u ? 1u : g->p[2] > 4u ? 4u : g->p[2]);
}
static int pu_gstep(const gen_t *g, uint32_t i, uint32_t len, uint32_t seed, gen_ev_t *e)
{
    uint32_t n = g->p[0] ? g->p[0] : 1u;
    (void)len, (void)seed;
    if (i % n != g->p[1] % n)
        return 0;
    e->rat = (uint8_t)(g->p[2] > 1u ? (g->p[2] - 1u) & 3u : 0u);
    return 1;
}
static const gen_type_t GEN_TYPES[GT_N] = {
    {"EUCLID", {{"HITS", 0, 64, 4, GD_INT}, {"STEPS", 1, 0, 16, GD_INT}, {"ROT", 0, 63, 0, GD_INT}}, eu_gfit, eu_gstep, 0},
    {"RANDOM", {{"DENSITY", 0, 100, 50, GD_PCT}, {"SEED", 0, 99, 1, GD_INT}, {"ACCENT", 0, 100, 0, GD_PCT}}, rn_gfit, rn_gstep, 0},
    {"PULSE", {{"EVERY", 1, 16, 4, GD_INT}, {"OFFSET", 0, 15, 0, GD_INT}, {"RATCHET", 1, 4, 2, GD_X}}, pu_gfit, pu_gstep, 0},
};
static const char *const GEN_MODE[GM_N] = {"REPLACE", "ADD", "MASK"};

static void gen_defaults(gen_t *g, uint32_t type)        /* type's values at their defaults (off, REPLACE) */
{
    uint32_t i;
    g->type = (uint8_t)(type % GT_N);
    for (i = 0; i < GEN_NP; i++)
        g->p[i] = GEN_TYPES[g->type].par[i].def;
}
static int gen_is_default(const gen_t *g)
{
    gen_t d = {0, 0, 0, {0, 0, 0}};
    gen_defaults(&d, GT_EUCLID);
    return !g->on && g->mode == d.mode && g->type == d.type && g->p[0] == d.p[0] && g->p[1] == d.p[1] && g->p[2] == d.p[2];
}
static void gen_sync(void)                               /* gen_trk, gen_lanes from gen[] (after every change) */
{
    uint32_t k, l, m = 0, lm = 0;
    for (k = 0; k < NPART; k++)
        m |= (uint32_t)(gen[k].on != 0) << k;
    for (l = 0; l < DRUM_SLANES; l++)
        lm |= (uint32_t)(gen[NPART + l].on != 0) << l;
    gen_lanes = lm;
    gen_trk = (uint8_t)(m | (lm ? 1u << TRK_DRUM : 0u));
}
/* track k's slots ON, a bit each (a synth track: bit 0; the drum track: a bit a lane); undo.c keeps it with the pattern */
static uint32_t gen_on_mask(uint32_t k)
{
    uint32_t l, m = 0;
    if (k < NPART)
        return gen[k].on != 0;
    for (l = 0; l < DRUM_SLANES; l++)
        m |= (uint32_t)(gen[NPART + l].on != 0) << l;
    return m;
}
static void gen_on_put(uint32_t k, uint32_t m)          /* (undo, redo: the IRQ off or the ISR) */
{
    uint32_t l;
    if (k < NPART)
        gen[k].on = (uint8_t)(m & 1u);
    else
        for (l = 0; l < DRUM_SLANES; l++)
            gen[NPART + l].on = (uint8_t)((m >> l) & 1u);
    gen_sync();
}
static void gen_reset_all(void)                          /* every slot off, EUCLID at its defaults (power-on, a load without any) */
{
    uint32_t s;
    for (s = 0; s < GEN_NSLOT; s++) {
        gen[s].on = 0, gen[s].mode = GM_REPLACE;
        gen_defaults(&gen[s], GT_EUCLID);
    }
    gen_sync();
}
static int gen_hit(uint32_t slot, uint32_t i, uint32_t len, gen_ev_t *e)
{
    const gen_t *g = &gen[slot % GEN_NSLOT];
    e->hit = e->lvl = e->rat = 0;
    if (i >= len)
        return 0;
    e->hit = (uint8_t)(GEN_TYPES[g->type % GT_N].step(g, i, len, gen_mix(slot * 0x27D4EB2Fu + 1u), e) != 0);
    return e->hit;
}

/* ---- a synth track's steps as they play */
static int gen_gate(const step_t *s) { return s->time == ST_NOTE && s->n; }
/* step i as it plays, as far as its kind: 0 nothing sounds (a REST, a TIE whose note ended), 1 a note struck, 2 a TIE
 * that holds on (or, ADD, the pattern's TIE as it is). A TIE the generator leaves alone holds on while the step before
 * sounds: back through the TIEs to the first step that decides */
static uint32_t gen_kind(const track_t *t, uint32_t slot, uint32_t i, uint32_t len)
{
    uint32_t mode = gen[slot].mode, j = i, k;
    gen_ev_t e;
    for (;; j--) {
        const step_t *s = &t->step[j % NSTEP];
        int h = gen_hit(slot, j, len, &e);
        if (mode == GM_ADD)
            k = h || gen_gate(s) ? 1u : s->time == ST_TIE ? 3u : 0u;
        else if (mode == GM_MASK)
            k = h && gen_gate(s) ? 1u : s->time == ST_TIE ? 2u : 0u;
        else
            k = h ? 1u : s->time == ST_TIE ? 2u : 0u;
        if (k != 2u)
            break;
        if (!j)
            return 0;                                    /* (a TIE on step 1: nothing to hold) */
    }
    if (j == i)
        return k == 3u ? 2u : k;
    return k ? 2u : 0u;
}
/* the lane's or the note's level / ratchet laid over: 2 bits a note */
static uint8_t gen_rep(uint32_t v) { return (uint8_t)((v & 3u) * 0x55u); }
/* step i of synth track t as it plays (o: room for a step made here) */
static const step_t *gen_eff(const track_t *t, uint32_t i, step_t *o)
{
    uint32_t k = trk_index(t), len, slot;
    const step_t *s = &t->step[i % NSTEP];
    gen_ev_t e;
    int h;
    if (!((gen_trk >> k) & 1u) || k >= NPART)
        return s;
    len = trk_len(t), slot = k;
    if (i >= len)
        return s;
    h = gen_hit(slot, i, len, &e);
    if (h && (gen[slot].mode != GM_MASK || gen_gate(s))) {
        if (gen_gate(s)) {
            *o = *s;
            if (i && !gen_kind(t, slot, i - 1u, len))
                o->flags &= (uint8_t)~SF_CARRY;          /* (nothing to go on from: struck) */
        } else {
            uint32_t j = i;
            int32_t r = 60 + t->p[P_ROOT] % 12;
            while (j && t->step[j].time == ST_TIE)
                j--;
            memset(o, 0, sizeof *o);
            o->time = ST_NOTE;
            o->vel = 100;
            if (s->time == ST_TIE && gen_gate(&t->step[j]))   /* (a TIE: the note it holds) */
                memcpy(o->note, t->step[j].note, sizeof o->note), o->n = t->step[j].n;
            else
                o->note[0] = (uint8_t)(r > 66 ? r - 12 : r), o->n = 1;
        }
        if (e.lvl)
            o->lvl = gen_rep(e.lvl);
        if (e.rat)
            o->rat = gen_rep(e.rat);
        return o;
    }
    if (gen[slot].mode == GM_ADD)
        return s;
    if (s->time == ST_TIE && i && gen_kind(t, slot, i - 1u, len))
        return s;
    memset(o, 0, sizeof *o);
    o->time = ST_REST;
    return o;
}
/* step i of the drum track as it plays: each lane with a generator on merged (of the lanes in `lanes`) */
static const dstep_t *gen_deff_of(const track_t *t, uint32_t i, dstep_t *o, uint32_t lanes)
{
    uint32_t m = gen_lanes & lanes, l, len;
    const dstep_t *s = &t->dstep[i % NSTEP];
    if (!((gen_trk >> TRK_DRUM) & 1u) || !m)
        return s;
    len = trk_len(t);
    if (i >= len)
        return s;
    *o = *s;
    for (l = 0; m; l++, m >>= 1) {
        gen_ev_t e;
        uint32_t mode = gen[NPART + l].mode, has = (uint32_t)dstep_has(s, l);
        int h;
        if (!(m & 1u))
            continue;
        h = gen_hit(NPART + l, i, len, &e);
        if (!h) {
            if (mode != GM_ADD)
                dstep_clr(o, l);
            continue;
        }
        if (!has && mode == GM_MASK)
            continue;
        dstep_set(o, l, e.lvl ? e.lvl : has ? dstep_lvl(s, l) : LV_NORM, e.rat ? e.rat : has ? dstep_rat(s, l) : 0u);
    }
    return o;
}
static const dstep_t *gen_deff(const track_t *t, uint32_t i, dstep_t *o) { return gen_deff_of(t, i, o, ~0u); }
/* the steps as they play now (the ISR: seq_tick fires one, the ratchets play on from it) */
static step_t gen_cur[NPART];
static dstep_t gen_dcur;
static uint8_t gen_cv;                                   /* bit k: track k's step playing is in gen_cur / gen_dcur */
static const step_t *gen_fire(const track_t *t, uint32_t idx)
{
    uint32_t k = trk_index(t) % NPART;
    const step_t *s = gen_eff(t, idx, &gen_cur[k]);
    gen_cv = (uint8_t)((gen_cv & ~(1u << k)) | (uint32_t)(s == &gen_cur[k]) << k);
    return s;
}
static const dstep_t *gen_dfire(const track_t *t, uint32_t idx)
{
    const dstep_t *s = gen_deff(t, idx, &gen_dcur);
    gen_cv = (uint8_t)((gen_cv & ~(1u << TRK_DRUM)) | (uint32_t)(s == &gen_dcur) << TRK_DRUM);
    return s;
}
static const step_t *gen_playing(const track_t *t)
{
    uint32_t k = trk_index(t);
    return k < NPART && ((gen_cv >> k) & 1u) ? &gen_cur[k] : &t->step[t->seq_idx % NSTEP];
}
static const dstep_t *gen_dplaying(const track_t *t)
{
    return (gen_cv >> TRK_DRUM) & 1u ? &gen_dcur : &t->dstep[t->seq_idx % NSTEP];
}
#define GEN_FIRE(t, idx) gen_fire((t), (idx))
#define GEN_DFIRE(t, idx) gen_dfire((t), (idx))
#define GEN_NEXT(t, i, o) gen_eff((t), (i), (o))
#define GEN_PLAYING(t) gen_playing(t)
#define GEN_DPLAYING(t) gen_dplaying(t)

/* ---- the UI's (the main loop) */
/* the steps 1..LEN of track t as they play -> o (a synth track's steps; the drum track's as dsteps over the same bytes);
 * the drum track: only the lanes in `lanes` merged (the others as stored) */
static void gen_render(const track_t *t, step_t *o, uint32_t lanes)
{
    uint32_t i;
    if (is_drum(t)) {
        dstep_t *d = (dstep_t *)(void *)o, x;
        for (i = 0; i < NSTEP; i++)
            d[i] = *gen_deff_of(t, i, &x, lanes);
        return;
    }
    for (i = 0; i < NSTEP; i++) {
        step_t x;
        o[i] = *gen_eff(t, i, &x);
    }
}
/* WRITE, in two halves so that the interrupts are off only for the copy (the render is O(LEN x a walk back) of
 * gen_hit: well over an audio block with long TIE runs): gen_write_prep renders what slot plays into gen_wbuf with the
 * interrupts on (the main loop), gen_write_commit copies it into the pattern with them off (the caller marks the undo
 * level). Only that slot's generator merged (a drum lane: that lane), the others' stay layers. A step the ISR records
 * between the two halves is overwritten, as WRITE overwrites the pattern anyway */
static step_t gen_wbuf[NSTEP];
static uint8_t gen_wdrum;                                  /* (gen_wbuf holds dsteps) */
static int gen_write_prep(const track_t *t, uint32_t slot) /* -> 0: the slot is off, nothing to write */
{
    if (!gen[slot % GEN_NSLOT].on)
        return 0;
    gen_wdrum = (uint8_t)is_drum(t);
    gen_render(t, gen_wbuf, gen_wdrum ? 1u << (slot - NPART) : 0u);
    return 1;
}
static int gen_write_commit(track_t *t)                    /* (the IRQ off) -> 1 something changed */
{
    uint32_t i, len = trk_len(t), ch = 0;
    if (gen_wdrum != (uint8_t)is_drum(t))
        return 0;                                          /* (prepared for another kind of track) */
    if (gen_wdrum) {
        const dstep_t *d = (const dstep_t *)(const void *)gen_wbuf;
        for (i = 0; i < len; i++) {
            ch |= (uint32_t)(memcmp(&t->dstep[i], &d[i], sizeof d[i]) != 0);
            t->dstep[i] = d[i];
        }
    } else {
        for (i = 0; i < len; i++) {
            ch |= (uint32_t)(memcmp(&t->step[i], &gen_wbuf[i], sizeof gen_wbuf[i]) != 0);
            t->step[i] = gen_wbuf[i];
        }
    }
    return ch != 0;
}
static int gen_write(track_t *t, uint32_t slot)            /* both halves at once (the tests; the IRQ off) */
{
    return gen_write_prep(t, slot) ? gen_write_commit(t) : 0;
}

/* ---- the stored form (the FX record's GEN_TLV, fx/fx_rec.c): a slot not at its defaults, 4 bytes (little-endian):
 * slot 5 bits | on 1 | mode 2 | type 3 | the three values 7 bits each. None: every slot off at its defaults. (4 bytes,
 * not 5: 20 slots fit the FX record's ceiling, ed_backup.c FXR1 / FXR2) */
#define GEN_TLV 0x4Bu
#define GEN_TLV_MAX (4u * GEN_NSLOT)
_Static_assert(GEN_NSLOT <= 32u && GM_N <= 4u && GT_N <= 8u, "GEN_TLV: a slot 5 bits, a mode 2, a type 3");
static uint32_t gen_tlv(uint8_t *o)
{
    uint32_t s, n = 0;
    for (s = 0; s < GEN_NSLOT; s++) {
        const gen_t *g = &gen[s];
        uint32_t w;
        if (gen_is_default(g))
            continue;
        w = s | (uint32_t)(g->on != 0) << 5 | (g->mode & 3u) << 6 | (g->type & 7u) << 8 | (g->p[0] & 127u) << 11 |
            (g->p[1] & 127u) << 18 | (uint32_t)(g->p[2] & 127u) << 25;
        o[n] = (uint8_t)w, o[n + 1u] = (uint8_t)(w >> 8), o[n + 2u] = (uint8_t)(w >> 16), o[n + 3u] = (uint8_t)(w >> 24);
        n += 4u;
    }
    return n;
}
static void gen_untlv(const uint8_t *a, uint32_t n)      /* (n 0: none; an entry this build cannot hold: skipped) */
{
    uint32_t i, j;
    gen_reset_all();
    for (i = 0; i + 4u <= n; i += 4u) {
        uint32_t w = a[i] | (uint32_t)a[i + 1u] << 8 | (uint32_t)a[i + 2u] << 16 | (uint32_t)a[i + 3u] << 24;
        uint32_t s = w & 31u, md = (w >> 6) & 3u, ty = (w >> 8) & 7u;
        gen_t g;
        if (s >= GEN_NSLOT || ty >= GT_N || md >= GM_N)
            continue;
        g.on = (uint8_t)((w >> 5) & 1u), g.type = (uint8_t)ty, g.mode = (uint8_t)md;
        for (j = 0; j < GEN_NP; j++) {
            const gen_par_t *d = &GEN_TYPES[ty].par[j];
            uint32_t v = (w >> (11u + 7u * j)) & 127u;
            g.p[j] = (uint8_t)(v < d->min ? d->min : d->max && v > d->max ? d->max : v > NSTEP ? NSTEP : v);
        }
        gen[s] = g;
    }
    gen_sync();
}
#else
#define GEN_ON 0
#define GEN_FIRE(t, idx) (&(t)->step[(idx) % NSTEP])
#define GEN_DFIRE(t, idx) (&(t)->dstep[(idx) % NSTEP])
#define GEN_NEXT(t, i, o) (&(t)->step[(i) % NSTEP])
#define GEN_PLAYING(t) (&(t)->step[(t)->seq_idx % NSTEP])
#define GEN_DPLAYING(t) (&(t)->dstep[(t)->seq_idx % NSTEP])
#endif
