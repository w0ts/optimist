/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 the Optimist contributors */
/* WALK and UPDN (generator types 4 and 5, FELUCCA_EX_NOTEGEN, off by default): the example note generators of
 * CONTRIBUTING.md, "Add a note generator". Included by gen.c, before its GEN_TYPES table. A template, so every line a
 * new generator must have is marked [GEN].
 *
 * A generator (gen.c gen_type_t) is a step function: for step i of a pattern of len steps, from its three values g->p[]
 * and the slot's seed, it says whether step i is a hit and may set the hit's level (e->lvl), ratchet (e->rat) and, with
 * this example built, its note (e->note: 0 the pattern's note there, else the track's ROOT). The sequencer plays the
 * result over the pattern (REPLACE, ADD, MASK: STEP's GEN OUT page) and WRITE copies it into the pattern. Two rules:
 *   - a pure function of (the values, i, len, the slot): no state, so a pass plays as the one before, a loop or a song
 *     section restarts it in step, and WRITE writes what was heard. Both below recompute their walk from step 0 (a
 *     pattern has at most 64 steps: cheap);
 *   - it runs in the audio interrupt (seq_tick): integers, bounded loops, no divide by a variable (tools/div_audit.py).
 * The notes are scale degrees above the track's ROOT, an octave under C4 (C3 .. B3), in the track's scale (SCL: seq.c
 * scale_mask; CHR: the major one). A drum lane takes the hits only (a note means nothing there). */
#if FELUCCA_EX_NOTEGEN
#define EXG_RANGE_MAX 15u                               /* degrees: two octaves and a note of a 7-note scale */

/* [GEN] the note of scale degree deg on slot's track (slot < NPART; a drum lane: 0, its hits only): from the ROOT an
 * octave under C4, deg notes of the track's scale up (seq.c scale_mask; CHR, no scale: the major one, MAJ) */
static uint8_t exg_note(uint32_t slot, uint32_t deg)
{
    const track_t *t = &trk[slot % NTRK];
    uint32_t mask = t->p[P_SCALE] ? scale_mask(t) : SCALE_MASK[1], root = (uint32_t)t->p[P_ROOT] % 12u, n = 48u + root, k;
    if (slot >= NPART)
        return 0;
    for (k = 0; deg > 0 && k < 48u; k++)                /* (bounded: a scale has a note in every 12) */
        if ((mask >> ((++n - root) % 12u)) & 1u)
            deg--;
    return (uint8_t)(n > 127u ? 127u : n);
}

/* ---- WALK: a random walk in the scale, a note on every step: from the middle of RANGE, each step up or down by
 * 1 .. LEAP degrees (drawn from DRAW and the slot: the same walk every pass), turned back at the ends */
static void wk_gfit(gen_t *g, uint32_t len)           /* [GEN] the values into their ranges */
{
    (void)len;
    g->p[0] = (uint8_t)clamp(g->p[0], 2, (int32_t)EXG_RANGE_MAX);
    g->p[1] = (uint8_t)clamp(g->p[1], 1, 4);
    g->p[2] = (uint8_t)clamp(g->p[2], 0, 99);
}
static int wk_gstep(const gen_t *g, uint32_t i, uint32_t len, uint32_t seed, gen_ev_t *e)   /* [GEN] the step function */
{
    int32_t range = g->p[0] < 2u ? 2 : g->p[0], leap = g->p[1] ? g->p[1] : 1, d = range / 2;
    uint32_t j;
    (void)len;
    for (j = 1; j <= i; j++) {                         /* (step 0 starts in the middle) */
        uint32_t h = gen_mix(seed ^ (uint32_t)g->p[2] * 0x9E3779B1u ^ j * 0x85EBCA77u);
        int32_t m = 1 + (int32_t)(((h & 0xFFu) * (uint32_t)leap) >> 8);   /* 1 .. LEAP, no divide */
        d += (h & 0x100u) ? m : -m;
        if (d < 0)
            d = -d;                                    /* (turned back at the bottom ...) */
        if (d > range - 1)
            d = 2 * (range - 1) - d;                   /* (... and at the top) */
        d = clamp(d, 0, range - 1);                    /* (a leap wider than RANGE) */
    }
    e->note = exg_note((uint32_t)(g - gen), (uint32_t)d);
    return 1;                                          /* a hit on every step (MASK keeps the pattern's rhythm) */
}

/* ---- UPDN: up the scale by HOP degrees a note, then down, over RANGE degrees, a note every RATE steps */
static void ud_gfit(gen_t *g, uint32_t len)           /* [GEN] */
{
    (void)len;
    g->p[0] = (uint8_t)clamp(g->p[0], 2, (int32_t)EXG_RANGE_MAX);
    g->p[1] = (uint8_t)clamp(g->p[1], 1, 4);
    g->p[2] = (uint8_t)clamp(g->p[2], 1, 4);
}
static int ud_gstep(const gen_t *g, uint32_t i, uint32_t len, uint32_t seed, gen_ev_t *e)   /* [GEN] */
{
    int32_t top = (g->p[0] < 2u ? 2 : g->p[0]) - 1, hop = g->p[1] ? g->p[1] : 1, d = 0, dir = 1;
    uint32_t rate = g->p[2] ? g->p[2] : 1u, j, c = 0;
    (void)len, (void)seed;
    for (j = 1; j <= i; j++) {                         /* (counted from step 0, no divide: c is i mod RATE) */
        if (++c < rate)
            continue;
        c = 0;
        d += dir * hop;
        if (d >= top)
            d = 2 * top - d, dir = -1;                 /* (bounced at the top ...) */
        else if (d <= 0)
            d = -d, dir = 1;                           /* (... and at the bottom) */
        d = clamp(d, 0, top);                          /* (a HOP wider than RANGE) */
    }
    if (c)
        return 0;                                      /* between two notes: no hit (REPLACE: a rest there) */
    e->note = exg_note((uint32_t)(g - gen), (uint32_t)d);
    return 1;
}
#endif
