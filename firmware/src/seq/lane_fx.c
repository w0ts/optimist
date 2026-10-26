/* SPDX-License-Identifier: GPL-3.0-only */
/* The drum lanes' FX (FELUCCA_LANE_FX; the user, 2026-10-26: "while in drum mode, hold a black key to select a drum
 * lane, then a 4 line screen shows up with midi fx like fill, rolls etc for that lane"; "we can have more than one page
 * of knobs"). Included by seq.c before drum_step. The values are the pattern's events (auto.h AUTO_IS_LFX), read at each
 * drum step into lfx_v (auto.c):
 *
 *   ROLL       every hit of the lane plays x2 x3 x4 x6 x8 in its step, evenly (a hit's own ratchet: the larger wins)
 *   CHANCE     each hit of the lane plays with this probability (its rolls with it); on top of the step's own chance
 *   FILL       the lane plays always / only in a fill / never in one (GLO + 9 held, + 10 the next bar)
 *   NUDGE      the lane's hits early or late by 1/64 of a step (up to half a step)
 *   VEL RAND   each hit's velocity moved at random by up to this much, either way
 *   FLAM       a grace note at 60 % of the hit's velocity, 5..40 ms before the step's first hit
 *   ROLL RAMP  a ROLL's (or a ratchet's) hits fade in (+) or out (-): the softest at 100 - |RAMP| % of the velocity
 *   EVERY      the lane plays on loop a of every b of the pattern (1:2 .. 4:4, as a step's condition; COND's count)
 *
 * A lane with a ROLL, a NUDGE, a VEL RAND, a FLAM or a ROLL RAMP is played here (lfx_man), its sounds in order: the
 * grace note (FLAM), then the step's hits; each at its time from the lane's grid (+ NUDGE). Sounds due before the grid
 * (an early NUDGE, a FLAM) play in the step before (lfx_ahead), which also decides there whether the lane plays (CHANCE,
 * FILL, EVERY); not across a pattern switch (pat_staged: the next pattern's step is not known), nor before PLAY's first
 * step: those come at the grid, late. The ISR */
static const uint8_t LFX_HITS[LFX_ROLL_N + 1u] = {1, 2, 3, 4, 6, 8};
static uint16_t lfx_out;                /* lanes whose hit this step stays silent (CHANCE, FILL, EVERY): their rolls too */
static uint16_t lfx_man;                /* lanes this step plays here */
static uint16_t lfx_early, lfx_eout;    /* lanes the step to come was decided for early: it plays (lfx_pre) / it does not */
static uint8_t lfx_done[LFX_LANES];     /* the sounds each managed lane played in this step */
static uint8_t lfx_pre[LFX_LANES];      /* ... in the step before, for the step to come (lfx_ahead) */

#if FELUCCA_COND
/* EVERY a:b misses the loop playing (next: the step to come is a new loop's step 0) */
static int lfx_every_miss(const track_t *t, uint32_t l, int next)
{
    uint32_t c = (uint32_t)lfx_v[l][LFX_I_EVERY], v = cond_loop[trk_index(t) % NTRK];
    if (!c)
        return 0;
    if (next)
        v = v == COND_NEW || v + 1u >= COND_LOOPS ? 0u : v + 1u;
    c = (c - 1u) % AUTO_NCOND;
    return (v == COND_NEW ? 0u : v) % COND_B[c] != COND_A[c] - 1u;
}
#else
#define lfx_every_miss(t, l, next) 0
#endif
/* lane l's hit stays silent: its EVERY, its FILL, then its CHANCE (a roll only below 100 %) */
static int lfx_drop(const track_t *t, uint32_t l, int next)
{
    int32_t c = lfx_v[l][LFX_I_CHANCE];
    if (lfx_every_miss(t, l, next))
        return 1;
#if FELUCCA_FILLS
    {
        uint32_t f = (uint32_t)lfx_v[l][LFX_I_FILL];
        if ((f == FC_FILL && !fill_now) || (f == FC_NOFILL && fill_now))
            return 1;
    }
#endif
    (void)t, (void)next;
    return c < 100 && (c <= 0 || (int32_t)(rng() % 100u) >= c);
}
static int lfx_managed(uint32_t l)
{
    const int8_t *v = lfx_v[l];
    return v[LFX_I_ROLL] || v[LFX_I_NUDGE] || v[LFX_I_VRND] || v[LFX_I_FLAM] || v[LFX_I_RAMP];
}
static uint32_t lfx_hits(const dstep_t *s, uint32_t l)
{
    uint32_t r = (uint32_t)lfx_v[l][LFX_I_ROLL], own = 1u + dstep_rat(s, l);
    r = LFX_HITS[r <= LFX_ROLL_N ? r : 0u];
    return r > own ? r : own;
}
static uint32_t lfx_flam_u(uint32_t l)                  /* the FLAM's lead in clock units (samples x BPM) */
{
    return (uint32_t)lfx_v[l][LFX_I_FLAM] * (uint32_t)LFX_FLAM_MS * (FS / 1000u) * (uint32_t)clamp(song.g[G_BPM], 1, 999);
}
static uint32_t lfx_sounds(const dstep_t *s, uint32_t l) { return lfx_hits(s, l) + (lfx_v[l][LFX_I_FLAM] != 0); }
/* sound e of lane l in a step slen long (s its hits): its time from the grid's step start, in clock units */
static int32_t lfx_time(const dstep_t *s, uint32_t l, uint32_t e, uint32_t slen)
{
    int32_t t = (int32_t)lfx_v[l][LFX_I_NUDGE] * (int32_t)slen / 64;
    if (lfx_v[l][LFX_I_FLAM]) {
        if (!e)
            return t - (int32_t)lfx_flam_u(l);          /* (the grace note) */
        e--;
    }
    return t + (int32_t)(e * slen / lfx_hits(s, l));
}
static void lfx_sound(track_t *t, const dstep_t *s, uint32_t l, uint32_t e)
{
    int32_t v = (int32_t)DACC_VEL(lvl_vel(dstep_lvl(s, l), 100), s), r = lfx_v[l][LFX_I_RAMP], x = lfx_v[l][LFX_I_VRND];
    uint32_t n = lfx_hits(s, l), grace = lfx_v[l][LFX_I_FLAM] && !e;
    if (lfx_v[l][LFX_I_FLAM] && e)
        e--;                                            /* (e: the hit, after the grace note) */
    if (grace)
        v = v * 60 / 100;
    else if (r && n > 1u)                               /* (ROLL RAMP: + the first the softest, - the last) */
        v = v * (100 - (r > 0 ? r * (int32_t)(n - 1u - e) : -r * (int32_t)e) / (int32_t)(n - 1u)) / 100;
    if (x)
        v += (int32_t)(rng() % (2u * (uint32_t)x + 1u)) - x;
    v = clamp(v, 1, 127);
    trk_note_on(t, DLANE_NOTE(l), (uint32_t)v);
    seq_out_on(t, LANE_NOTE_OF(l), (uint32_t)v);
}

/* a drum step fires with lanes m to hit (drum_step): -> those it plays now. The others: silent (lfx_out), or played
 * here (lfx_man, from the sounds played early: lfx_pre) */
static uint32_t lfx_step(const track_t *t, uint32_t m)
{
    uint32_t l, mine = lfx_eabs == t->seq_abs, was = mine ? lfx_early : 0u, wout = mine ? lfx_eout : 0u;
    lfx_out = lfx_man = lfx_early = lfx_eout = 0;
    lfx_eabs = SEQ_NONE;
    for (l = 0; l < LFX_LANES; l++) {
        uint32_t b = 1u << l;
        lfx_done[l] = (uint8_t)((was & b) ? lfx_pre[l] : 0u);
        lfx_pre[l] = 0;
        if (!(m & b))
            continue;
        if (wout & b || (!(was & b) && lfx_drop(t, l, 0))) {   /* (decided in the step before, or now) */
            m &= ~b;
            lfx_out |= b;
        } else if ((was & b) || lfx_managed(l)) {
            m &= ~b;
            lfx_man |= b;
        }
    }
    return m;
}
/* each block, the step playing (into units of slen in, its hits in s): the managed lanes' sounds now due */
static void lfx_tick(track_t *t, const dstep_t *s, uint32_t into, uint32_t slen)
{
    uint32_t l, m = lfx_man & ~(uint32_t)smute_lane;
    for (l = 0; m; l++, m >>= 1) {
        uint32_t n;
        int32_t at;
        if (!(m & 1u))
            continue;
        for (n = lfx_sounds(s, l); lfx_done[l] < n && (at = lfx_time(s, l, lfx_done[l], slen)) <= (int32_t)into; lfx_done[l]++) {
            ev_at(into - (uint32_t)(at > 0 ? at : 0));
            lfx_sound(t, s, l, lfx_done[l]);
        }
    }
}
/* each block, the step playing (idx of len, into units of slen in): the next step's sounds due before its grid */
static void lfx_ahead(track_t *t, uint32_t idx, uint32_t len, uint32_t into, uint32_t slen)
{
    uint32_t l, wrap = (idx + 1u) % len == 0u;
    const dstep_t *n;
#if FELUCCA_PATTERNS
    if ((pat_staged >> trk_index(t)) & 1u)
        return;                                         /* (a pattern switch at the end: its first step is not known) */
#endif
    if (lfx_eabs != t->seq_abs + 1u) {
        lfx_early = lfx_eout = 0, lfx_eabs = t->seq_abs + 1u;
        memset(lfx_pre, 0, sizeof lfx_pre);
    }
    n = &t->dstep[(idx + 1u) % len % NSTEP];
    for (l = 0; l < LFX_LANES; l++) {
        uint32_t b = 1u << l, e;
        int32_t at;
        if ((lfx_eout & b) || !dstep_has(n, l) || !lfx_managed(l) || (at = lfx_time(n, l, lfx_pre[l], slen)) >= 0 ||
            (int32_t)into < (int32_t)slen + at)
            continue;
        if (!(lfx_early & b)) {                         /* (its first sound due: decided now, for the step to come) */
            if (smute_lane_on(l) || lfx_drop(t, l, (int)wrap)) {
                lfx_eout |= b;
                continue;
            }
            lfx_early |= b;
        }
        for (e = lfx_pre[l]; e < lfx_sounds(n, l) && (at = lfx_time(n, l, e, slen)) < 0 && (int32_t)into >= (int32_t)slen + at; e++) {
            ev_at(into - (uint32_t)((int32_t)slen + at));
            lfx_sound(t, n, l, e);
        }
        lfx_pre[l] = (uint8_t)e;
    }
}
