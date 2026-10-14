/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments (Felucca 1.2 .. 1.5.6 SEQ TOOLS,
 * hugelton/Felucca 9f6a885, firmware/src/ui_tools.c: REVERSE, RANDOM, COOK, EUCLID, DOUBLE, HALF) */
/* STEP's TOOLS and EUCLID pages (FELUCCA_SEQTOOLS, backports15.h; docs/UI-OPTIMIST-DESIGN.md section 22). Included by
 * optimist.c before op_step.c, which shows them as STEP's last two pages (SELECT, SEQ tapped again).
 *
 *   TOOLS   REVERSE · RANDOM · COOK · LENGTH. A turn arms the card (the hot one; LENGTH's turn says x2 or 1/2), YES
 *           (SAVE tapped) does it, a toast says what was done. Each is one undo level (SAVE then HOME).
 *   EUCLID  HITS · STEPS · ROT · LANE: each turn writes the pattern at once (heard), from the pattern as it was when the
 *           run began (the undo session's start, seq/undo.c: undo.st, undo.al; no copy of our own): a run of turns
 *           is one undo level. HOME + a knob: its default (4, 16 or LEN, 0). LANE is a read-out.
 *
 * The scope is ALGORITHM's: a synth track; DR itself the whole kit; a drum lane (lane_picked) that lane only
 * (REVERSE RANDOM COOK EUCLID; EUCLID on DR itself the last picked lane, shown on LANE). LENGTH is the whole pattern.
 * Every tool works on steps 1..LEN (the steps past LEN stay). The steps' events (seq/auto.h: locks, motion, chance,
 * conditions, nudges, fills, a drum lane's values) move with their steps; a step a tool empties loses its step-only
 * events (Felucca's tl_unlock). The exact length of a note survives: a note is its NOTE step, its TIEs and the carry
 * steps (SF_CARRY) that go on with its pitch to its own end (seq.c note_span); REVERSE moves that whole run as one
 * unit, RANDOM and COOK change a pitch all along it (tl_pitch) and never treat a carried note as a new one.
 * Felucca's tools as ported (ui_tools.c lines): REVERSE 72-109, RANDOM 140-153 / 257-308, COOK 175-222 / 310-336,
 * EUCLID 338-469, DOUBLE / HALF 471-546. Our changes: 16 drum lanes (the beat's lanes KICK 0, SNARE 2, CLOSED HAT 4,
 * OPEN HAT 5; RANDOM on DR draws lanes 1..8 only), COOK's accent is the note's level NORM <-> HARD, RANDOM on a CHR track
 * walks the minor pentatonic of its ROOT, HALF keeps the steps past the new LEN, DOUBLE refuses when the copies of
 * the events do not fit the track's 128 (PATTERN FULL). */
#if FELUCCA_SEQTOOLS
enum { TL_REV, TL_RND, TL_COOK, TL_LEN };
#define TL_KICK 0u                      /* drums.c LANE_NAME: KICK, SNARE, CLOSED HAT, OPEN HAT */
#define TL_SNARE 2u
#define TL_HATC 4u
#define TL_HATO 5u
#define TL_ALL ((1u << DRUM_SLANES) - 1u)      /* every step lane: the 16 sounds and the ACCENT lane (core.h DRUM_ACC) */
static struct {
    uint8_t half;                       /* LENGTH armed: 0 x2 (DOUBLE), 1 1/2 (HALF) */
    uint8_t hits, steps, rot;           /* EUCLID's values (kept until the power goes, as Felucca's) */
    uint32_t sess;                      /* the undo session of the last tool */
    uint32_t eu_sess;                   /* .. of the EUCLID run (its start: what each turn writes from) */
} tl = {0, 4, 16, 0, 0, 0};

static uint32_t tl_rnd(uint32_t n) { return n ? rng() % n : 0u; }
static int32_t tl_lane(void) { return is_drum(TSEL) && lane_picked() ? (int32_t)lane_selected() : -1; }
static void tl_mark(track_t *t)                         /* a new undo level: the steps, LEN, the events as they are */
{
    tl.sess = (undo_sess += 4u) | 3u;
    undo_mark(t, tl.sess);
}
static void tl_clr(step_t *s)
{
    memset(s, 0, sizeof *s);
    s->time = ST_REST;
}
static int tl_gate(const step_t *s) { return s->time == ST_NOTE && s->n; }
static const dstep_t *tl_was_d(void) { return (const dstep_t *)(const void *)undo.st; }   /* (the union's bytes: seq/undo.c) */

/* ---- the events */
/* the events of steps 0..len-1 move: step i's go to step to[i]; lane >= 0: that drum lane's only */
static void tl_ev_move(track_t *t, uint32_t len, const uint8_t *to, int32_t lane)
{
#if FELUCCA_AUTO
    auto_list_t *l = AL(t);
    uint32_t i;
    for (i = 0; i < l->n && i < AUTO_MAX; i++) {
        auto_ev_t *e = &l->ev[i];
        uint32_t s = e->place & AUTO_STEP;
        if (s >= len || (lane >= 0 && (!AUTO_IS_LANE(e->param) || AUTO_LANE_L(e->param) != (uint32_t)lane)))
            continue;
        e->place = (uint8_t)((e->place & ~AUTO_STEP) | (to[s] & AUTO_STEP));
    }
    auto_touch(trk_index(t));
#else
    (void)t, (void)len, (void)to, (void)lane;
#endif
}
/* what the session's start held (undo.st) and the track holds no longer: its step-only events go (a drum lane's hit
 * gone: that lane's) */
static void tl_unlock(track_t *t, uint32_t len)
{
#if FELUCCA_AUTO
    uint32_t i, l;
    for (i = 0; i < len; i++) {
        if (!is_drum(t)) {
            if (undo.st[i].time != ST_REST && t->step[i].time == ST_REST)
                (void)auto_step_clear(t, i, AUTO_ONLYS);
            continue;
        }
        if (dstep_lanes(&tl_was_d()[i]) && !dstep_lanes(&t->dstep[i])) {   /* (the ACCENT lane is a lane) */
            (void)auto_step_clear(t, i, AUTO_ONLYS);
            continue;
        }
#if DLA_ON
        for (l = 0; l < DRUM_LANES; l++)
            if (dstep_has(&tl_was_d()[i], l) && !dstep_has(&t->dstep[i], l))
                (void)lane_step_drop(t, i, l);
#endif
    }
    (void)l;
    auto_touch(trk_index(t));
#else
    (void)t, (void)len;
#endif
}
/* DOUBLE's events: those of steps len..nl-1 go, those of steps 0..nl-len-1 copied len later; 0 they would not fit
 * (nothing done; check only: nothing done either) */
static int tl_ev_double(track_t *t, uint32_t len, uint32_t nl, int check)
{
#if FELUCCA_AUTO
    auto_list_t *l = AL(t);
    uint32_t i, n = 0, drop = 0, add = 0, m;
    for (i = 0; i < l->n && i < AUTO_MAX; i++) {
        uint32_t s = l->ev[i].place & AUTO_STEP;
        drop += s >= len && s < nl;
        add += s < nl - len;
    }
    if (l->n - drop + add > AUTO_MAX)
        return 0;
    if (check)
        return 1;
    for (i = 0; i < l->n; i++) {
        uint32_t s = l->ev[i].place & AUTO_STEP;
        if (!(s >= len && s < nl))
            l->ev[n++] = l->ev[i];
    }
    for (i = 0, m = n; i < m; i++)
        if ((l->ev[i].place & AUTO_STEP) < nl - len) {
            l->ev[n] = l->ev[i];
            l->ev[n++].place = (uint8_t)((l->ev[i].place & ~AUTO_STEP) | ((l->ev[i].place & AUTO_STEP) + len));
        }
    l->n = (uint8_t)n;
    auto_touch(trk_index(t));
#else
    (void)t, (void)len, (void)nl, (void)check;
#endif
    return 1;
}

/* ---- a note's chain (seq.c note_span): NOTE, its TIEs, the carry steps that go on with its pitch */
/* the carry step note k of NOTE step j goes on into (its slot into *c), -1 none: it ends in its own step (ngate),
 * at its TIEs' end, or the step after them strikes anew */
static int32_t tl_next(const track_t *t, uint32_t len, uint32_t j, uint32_t k, uint32_t *c)
{
    const step_t *s = &t->step[j % NSTEP];
    uint32_t i, m, x = s->note[k & 3u];
    if (ngate_of(s, k) || s->rat || len < 2u)
        return -1;
    for (i = 1; i < len && t->step[(j + i) % len].time == ST_TIE; i++)
        ;
    if (i >= len)
        return -1;
    s = &t->step[(j + i) % len];
    if (s->time != ST_NOTE || !(s->flags & SF_CARRY) || s->rat)
        return -1;
    for (m = 0; m < s->n && m < 4u && s->note[m] != x; m++)
        ;
    if (m >= s->n || m >= 4u)
        return -1;
    *c = m;
    return (int32_t)((j + i) % len);
}
/* cont[j] bit k: note k of step j goes on from an earlier step (not a note of its own) */
static void tl_cont(const track_t *t, uint32_t len, uint8_t *cont)
{
    uint32_t i, k, c;
    int32_t j;
    memset(cont, 0, NSTEP);
    for (i = 0; i < len; i++)
        for (k = 0; t->step[i].time == ST_NOTE && k < t->step[i].n && k < 4u; k++)
            if ((j = tl_next(t, len, i, k, &c)) >= 0)
                cont[j] |= (uint8_t)(1u << c);
}
/* note k of step i becomes x, and so all along its chain */
static void tl_pitch(track_t *t, uint32_t len, uint32_t i, uint32_t k, uint32_t x)
{
    uint32_t g, c = 0;
    int32_t j;
    for (g = 0; g < len; g++) {
        j = tl_next(t, len, i, k, &c);                  /* (before the change: the walk matches the old pitch) */
        t->step[i].note[k & 3u] = (uint8_t)x;
        if (j < 0)
            break;
        i = (uint32_t)j, k = c;
    }
}
static int32_t tl_first(const step_t *s, uint32_t cont)   /* the step's first note of its own, -1 none */
{
    uint32_t k;
    for (k = 0; k < s->n && k < 4u; k++)
        if (!((cont >> k) & 1u))
            return (int32_t)k;
    return -1;
}

/* ---- REVERSE */
/* a unit starts on a REST or a NOTE of its own (not a carry): a note's TIEs and carry steps stay after it */
static int tl_bound(const step_t *s) { return s->time == ST_REST || (s->time == ST_NOTE && !(s->flags & SF_CARRY)); }
static void tl_reverse(track_t *t, uint32_t len, int32_t lane)
{
    uint8_t to[NSTEP];
    uint32_t i, b, s, e, p;
    if (is_drum(t)) {
        const dstep_t *o = tl_was_d();
        for (i = 0; i < len; i++)
            to[i] = (uint8_t)(len - 1u - i);
        for (i = 0; i < len; i++) {
            if (lane < 0) {
                t->dstep[to[i]] = o[i];
                continue;
            }
            dstep_clr(&t->dstep[to[i]], (uint32_t)lane);
        }
        for (i = 0; lane >= 0 && i < len; i++)
            if (dstep_has(&o[i], (uint32_t)lane))
                dstep_set(&t->dstep[to[i]], (uint32_t)lane, dstep_lvl(&o[i], (uint32_t)lane), dstep_rat(&o[i], (uint32_t)lane));
        tl_ev_move(t, len, to, lane);
        return;
    }
    for (b = 0; b < len && !tl_bound(&undo.st[b]); b++)
        ;
    if (b == len)
        b = 0;                                          /* (one unit: the whole pattern, as it is) */
    for (s = b; s < b + len; s = e) {                   /* the units in loop order from the first start, mirrored */
        for (e = s + 1u; e < b + len && !tl_bound(&undo.st[e % len]); e++)
            ;
        p = (2u * len - e) % len;                       /* (unit [s, e) -> [len - e, len - s), round the loop) */
        for (i = 0; i < e - s; i++)
            to[(s + i) % len] = (uint8_t)((p + i) % len);
    }
    for (i = 0; i < len; i++)
        t->step[to[i]] = undo.st[i];
    tl_ev_move(t, len, to, -1);
}

/* ---- the scale */
static uint32_t tl_mask(const track_t *t) { return t->p[P_SCALE] ? scale_mask(t) : SCALE_MASK[6]; }   /* (CHR: MPEN) */
static int tl_in(const track_t *t, int32_t n) { return (int)((tl_mask(t) >> (uint32_t)((n - t->p[P_ROOT] + 120) % 12)) & 1u); }
static int32_t tl_neigh(const track_t *t, int32_t n, int32_t d)   /* the next scale note above (d 1) / below (-1) */
{
    uint32_t g = 12;
    do
        n += d;
    while (g-- && !tl_in(t, n));
    return clamp(n, 0, 127);
}
static int32_t tl_snap(const track_t *t, int32_t x)    /* down into the scale */
{
    x = clamp(x, 1, 127);
    while (!tl_in(t, x) && x > 1)
        x--;
    return x;
}

/* ---- RANDOM */
/* a melodic track: the rhythm stays, new notes, a walk through the scale 9 semitones either side of the notes' mean
 * (an empty pattern: 8ths with some rests around the root near C4); a chord keeps its shape */
static void tl_random_notes(track_t *t, uint32_t len)
{
    uint8_t deg[20], cont[NSTEP];
    int32_t c = 0, lo, d, x, f;
    uint32_t i, k, g = 0, nd = 0, at;
    tl_cont(t, len, cont);
    for (i = 0; i < len; i++)
        if (tl_gate(&t->step[i]) && (f = tl_first(&t->step[i], cont[i])) >= 0) {
            c += t->step[i].note[f];
            g++;
        }
    if (g) {
        c /= (int32_t)g;
    } else {
        c = 60 + t->p[P_ROOT] % 12;
        c -= c > 66 ? 12 : 0;
        for (i = 0; i < len; i++) {
            tl_clr(&t->step[i]);
            if (i % 2u == 0u && (!i || tl_rnd(4))) {
                t->step[i].time = ST_NOTE;
                t->step[i].n = 1;
                t->step[i].vel = 100;
            }
        }
        memset(cont, 0, sizeof cont);
    }
    for (lo = clamp(c - 9, 12, 100), x = lo; x <= lo + 18 && nd < sizeof deg; x++)
        if (tl_in(t, x))
            deg[nd++] = (uint8_t)x;
    if (!nd)
        return;
    at = nd / 2u + tl_rnd(5);
    at = at >= 2u ? at - 2u : 0u;
    for (i = 0; i < len; i++) {
        step_t *s = &t->step[i];
        if (!tl_gate(s) || (f = tl_first(s, cont[i])) < 0)
            continue;
        at = at >= nd ? nd - 1u : at;
        d = (int32_t)deg[at] - s->note[f];
        for (k = 0; k < s->n && k < 4u; k++)
            if (!((cont[i] >> k) & 1u))
                tl_pitch(t, len, i, k, (uint32_t)(k == (uint32_t)f ? deg[at] : tl_snap(t, s->note[k] + d)));
        if (!tl_rnd(6)) {                               /* a leap now and then, else a step or two */
            at = tl_rnd(nd);
        } else {
            x = (int32_t)at + (int32_t)tl_rnd(5) - 2;
            at = (uint32_t)(x < 0 ? -x : x >= (int32_t)nd ? 2 * ((int32_t)nd - 1) - x : x);
        }
    }
}
/* hits per 16 steps -> per len (at least one when any) */
static uint32_t tl_per(uint32_t k16, uint32_t len) { return !k16 ? 0u : k16 * len < 16u ? 1u : (k16 * len + 8u) / 16u; }
/* k hits spread evenly over len steps into lane l (the first on step rot; mir: mirrored around it) */
static void tl_euclid(track_t *t, uint32_t len, uint32_t l, uint32_t k, uint32_t rot, uint32_t mir)
{
    uint32_t i;
    for (i = 0; k && len && i < len; i++)
        if ((i * k) % len < k)
            dstep_set(&t->dstep[((mir ? len - i : i) + rot) % len], l, LV_NORM, 0);
}
/* lane l at random, the drum machine way: the kick 3..5 a bar (one on step 1), the snare on beats 2 and 4, the closed
 * hat 8..12, the others sparse (any: they may stay empty, the whole kit's RANDOM; else at least one hit) */
static void tl_lane_rnd(track_t *t, uint32_t len, uint32_t l, int any)
{
    uint32_t i;
    for (i = 0; i < len; i++)
        dstep_clr(&t->dstep[i], l);
    if (l == TL_KICK)
        tl_euclid(t, len, l, tl_per(3u + tl_rnd(3), len), 0, tl_rnd(2));
    else if (l == TL_SNARE)
        tl_euclid(t, len, l, tl_per(2, len), 4u % len, 0);
    else if (l == TL_HATC)
        tl_euclid(t, len, l, tl_per(8u + tl_rnd(5), len), tl_rnd(2), 0);
    else if (!any || !tl_rnd(l == TL_HATO ? 2u : 3u))
        tl_euclid(t, len, l, tl_per(1u + tl_rnd(2), len), l == TL_HATO ? (2u + 4u * tl_rnd(4)) % len : tl_rnd(len), 0);
    if (l == TL_SNARE && !tl_rnd(3))
        dstep_set(&t->dstep[tl_rnd(len)], l, LV_GHOST, 0);   /* (a ghost snare now and then) */
}

/* ---- COOK */
static uint32_t tl_sig(const track_t *t)                /* the steps and events as they are (a change happened?) */
{
    const uint8_t *p = (const uint8_t *)t->step;
    uint32_t i, h = 2166136261u;
    for (i = 0; i < sizeof t->step; i++)
        h = (h ^ p[i]) * 16777619u;
#if FELUCCA_AUTO
    p = (const uint8_t *)AL(t);
    for (i = 0; i < 1u + 3u * AL(t)->n; i++)
        h = (h ^ p[i]) * 16777619u;
#endif
    return h;
}
/* a melodic track: 15..25 % of its notes' steps (at least one): a note to the next scale note up or down, or an
 * octave; a level NORM <-> HARD (our accent); a chance of 50 / 75 % or back to 100 (an event; a step with a condition
 * keeps it). Again until something changed (64 tries at most) */
static void tl_cook_notes(track_t *t, uint32_t len)
{
    uint8_t cont[NSTEP];
    uint32_t m, i, k, r, n, g = 0, sig = tl_sig(t), tries = 64;
    tl_cont(t, len, cont);
    for (i = 0; i < len; i++)                           /* (the steps with a note of their own) */
        g += tl_gate(&t->step[i]) && tl_first(&t->step[i], cont[i]) >= 0;
    if (!g)
        return;
    m = g * (15u + tl_rnd(11)) / 100u;
    for (m = m ? m : 1u; tries-- && (m || tl_sig(t) == sig); m -= m != 0u) {
        step_t *s;
        n = tl_rnd(g);
        for (i = 0; i < len && (!tl_gate(&t->step[i]) || tl_first(&t->step[i], cont[i]) < 0 || n--); i++)
            ;
        if (i >= len)
            break;
        s = &t->step[i];
        r = tl_rnd(8);
        if (r == 6u) {
            uint32_t f = (uint32_t)tl_first(s, cont[i]), hard = ((s->lvl >> (2u * f)) & 3u) == LV_HARD;
            for (k = 0; k < s->n && k < 4u; k++)
                if (!((cont[i] >> k) & 1u))
                    s->lvl = (uint8_t)((s->lvl & ~(3u << (2u * k))) | (hard ? LV_NORM : LV_HARD) << (2u * k));
            continue;
        }
#if FELUCCA_CHANCE
        if (r == 7u) {
#if FELUCCA_COND
            if (step_cond(t, i))
                continue;                               /* (a condition stays as it is) */
#endif
            (void)step_chance_set(t, i, step_chance_ev(t, i) < 100u ? 100u : tl_rnd(2) ? 75u : 50u);
            continue;
        }
#endif
        {
            int32_t d = tl_rnd(2) ? 1 : -1, o = r == 5u ? d * 12 : 0;
            for (k = 0; k < s->n && k < 4u; k++)       /* (an octave out of 24 .. 108: a scale step instead) */
                if (!((cont[i] >> k) & 1u) && (s->note[k] + o < 24 || s->note[k] + o > 108))
                    o = 0;
            for (k = 0; k < s->n && k < 4u; k++)
                if (!((cont[i] >> k) & 1u))
                    tl_pitch(t, len, i, k, (uint32_t)(o ? s->note[k] + o : tl_neigh(t, s->note[k], d)));
        }
    }
}
/* the n-th hit of lanes lm in steps 1..len (its step and lane), the kick on step 1 not counted; ~0: -> the count */
static uint32_t tl_hit_nth(const track_t *t, uint32_t len, uint32_t lm, uint32_t n, uint32_t *ip, uint32_t *lp)
{
    uint32_t i, l, c = 0;
    for (i = 0; i < len; i++)
        for (l = 0; l < DRUM_SLANES; l++)
            if (((lm >> l) & 1u) && dstep_has(&t->dstep[i], l) && (i || l != TL_KICK) && c++ == n) {
                *ip = i, *lp = l;
                return 1;
            }
    return c;
}
static uint32_t tl_lane_hits(const track_t *t, uint32_t len, uint32_t l)
{
    uint32_t i, c = 0;
    for (i = 0; i < len; i++)
        c += (uint32_t)dstep_has(&t->dstep[i], l);
    return c;
}
/* drums: a hit moved a step (with its level and ratchet), one added on a lane in use, or one taken away (never a lane's
 * last, never the kick on step 1); -> 1 done (a try that cannot does not count) */
static uint32_t tl_cook_hit(track_t *t, uint32_t len, uint32_t lm)
{
    uint32_t i = 0, l = 0, j, r = tl_rnd(4), n = tl_hit_nth(t, len, lm, ~0u, &i, &l), used = 0, lv, rt;
    if (r == 2u || !n) {                                /* add: on a lane in use (none: the closed hat, or the lane) */
        for (j = 0; j < len; j++)
            used |= dstep_lanes(&t->dstep[j]);
        used &= lm;
        l = (lm >> TL_HATC) & 1u ? TL_HATC : (uint32_t)__builtin_ctz(lm | 1u << DRUM_SLANES) % DRUM_SLANES;
        for (j = tl_rnd(DRUM_SLANES); used && j < 2u * DRUM_SLANES; j++)
            if ((used >> (j % DRUM_SLANES)) & 1u) {
                l = j % DRUM_SLANES;
                break;
            }
        j = tl_rnd(len);
        if (dstep_has(&t->dstep[j], l))
            return 0;
        dstep_set(&t->dstep[j], l, LV_NORM, 0);
        return 1;
    }
    (void)tl_hit_nth(t, len, lm, tl_rnd(n), &i, &l);
    if (r == 3u) {                                      /* take one away */
        if (tl_lane_hits(t, len, l) < 2u)
            return 0;
        dstep_clr(&t->dstep[i], l);
        return 1;
    }
    j = (i + (tl_rnd(2) ? 1u : len - 1u)) % len;        /* move one a step on (or back) */
    if (dstep_has(&t->dstep[j], l))
        return 0;
    lv = dstep_lvl(&t->dstep[i], l), rt = dstep_rat(&t->dstep[i], l);
    dstep_clr(&t->dstep[i], l);
    dstep_set(&t->dstep[j], l, lv, rt);
    return 1;
}
static void tl_cook_drum(track_t *t, uint32_t len, uint32_t hits, uint32_t lm)
{
    uint32_t m = hits * (10u + tl_rnd(11)) / 100u, n, sig = tl_sig(t);
    for (m = m ? m : 1u, n = 4u * m + 64u; n; n--) {
        if (tl_cook_hit(t, len, lm) && m)
            m--;
        if (!m && tl_sig(t) != sig)                     /* (a hit moved there and back: one more) */
            break;
    }
}

/* ---- EUCLID: HITS spread as evenly as they go over a cycle of STEPS, turned by ROT, repeated through LEN */
static void eu_fit(const track_t *t)
{
    uint32_t len = trk_len(t);
    if (!tl.steps || tl.steps > len)
        tl.steps = (uint8_t)(len < 16u ? len : 16u);
    if (tl.hits > tl.steps)
        tl.hits = tl.steps;
    tl.rot = (uint8_t)(tl.rot % tl.steps);
}
static int eu_hit(uint32_t i)                           /* step i (0..) of the cycle repeated, turned by ROT */
{
    uint32_t n = tl.steps ? tl.steps : 1u, p = (i + n - tl.rot % n) % n;
    return (p * tl.hits) % n < tl.hits;
}
/* the note a hit on step i plays: the NOTE step there, or the note a TIE there holds (its NOTE before it), else none */
static const step_t *eu_src(uint32_t i)
{
    uint32_t j = i;
    while (j && undo.st[j].time == ST_TIE)
        j--;
    return tl_gate(&undo.st[j]) && (j == i || undo.st[i].time == ST_TIE) ? &undo.st[j] : 0;
}
static int eu_open(const track_t *t)                    /* the run's start is still the open undo session */
{
    return tl.eu_sess && undo.valid && !undo.undone && undo.sess == tl.eu_sess && undo.trk == trk_index(t);
}
/* the pattern from the run's start, written again with the values now (the IRQ off) */
static void eu_render(track_t *t)
{
    uint32_t len = trk_len(t), i, l = lane_selected();
    if (!eu_open(t)) {
        tl_mark(t);                                     /* (a new run: from the pattern as it is now) */
        tl.eu_sess = tl.sess;
    }
    fm1_irq_off();
    memcpy(t->step, undo.st, sizeof t->step);
#if FELUCCA_AUTO
    *AL(t) = undo.al;
#endif
    if (is_drum(t)) {
        for (i = 0; i < len; i++)
            if (!eu_hit(i))
                dstep_clr(&t->dstep[i], l);
            else if (!dstep_has(&t->dstep[i], l))
                dstep_set(&t->dstep[i], l, LV_NORM, 0);
    } else {
        for (i = 0; i < len; i++) {
            step_t *s = &t->step[i];
            const step_t *o = eu_src(i);
            if (eu_hit(i)) {
                if (o != &undo.st[i]) {                 /* (a REST, an empty step, a TIE: a NOTE of its own) */
                    int32_t r = 60 + t->p[P_ROOT] % 12;
                    tl_clr(s);
                    s->time = ST_NOTE;
                    s->vel = 100;
                    if (o)
                        memcpy(s->note, o->note, sizeof s->note), s->n = o->n;
                    else
                        s->note[0] = (uint8_t)(r > 66 ? r - 12 : r), s->n = 1;
                } else if (i && !tl_gate(&t->step[i - 1u]) && t->step[i - 1u].time != ST_TIE) {
                    s->flags &= (uint8_t)~SF_CARRY;     /* (nothing to go on from: struck) */
                }
            } else if (!(s->time == ST_TIE && i && (t->step[i - 1u].time == ST_TIE || tl_gate(&t->step[i - 1u])))) {
                tl_clr(s);
            }
        }
    }
    tl_unlock(t, len);
    t->seq_active = 1;
    fm1_irq_on();
    sync_reload = 1;
}
static void eu_turn(uint32_t k, int32_t s)
{
    track_t *t = TSEL;
    uint32_t len = trk_len(t);
    eu_fit(t);
    if (k == 0u)
        tl.hits = (uint8_t)(s == OP_RESET ? 4 : clamp((int32_t)tl.hits + s, 0, tl.steps));
    else if (k == 1u)
        tl.steps = (uint8_t)(s == OP_RESET ? (len < 16u ? len : 16u) : (uint32_t)clamp((int32_t)tl.steps + s, 1, (int32_t)len));
    else if (k == 2u)
        tl.rot = (uint8_t)(s == OP_RESET ? 0 : ((int32_t)tl.rot + s % (int32_t)tl.steps + (int32_t)tl.steps) % (int32_t)tl.steps);
    else
        return;
    eu_fit(t);
    eu_render(t);
}

/* ---- DOUBLE, HALF (and the EDIT layer's LENGTH, op_layers.c) */
static uint32_t tl_dbl_len(uint32_t len) { return 2u * len < NSTEP ? 2u * len : NSTEP; }
/* steps 1..len after themselves, LEN x2 (64 at most), their events copied (the IRQ off; tl_ev_double(.., 1) said yes) */
static void tl_double_do(track_t *t, uint32_t len)
{
    uint32_t nl = tl_dbl_len(len), i;
    for (i = len; i < nl; i++)
        if (is_drum(t))
            t->dstep[i] = t->dstep[i - len];
        else
            t->step[i] = t->step[i - len];
    (void)tl_ev_double(t, len, nl, 0);
    t->p[P_SLEN] = (int16_t)nl;
}

/* ---- the pages */
static void tl_cell(uint32_t pg, uint32_t k, cell_t *c)   /* pg 0 TOOLS, 1 EUCLID */
{
    static const char *const TN[4] = {"REVERSE", "RANDOM", "COOK", "LENGTH"};
    static const char *const EN[3] = {"HITS", "STEPS", "ROT"};
    const track_t *t = TSEL;
    cell_clear(c);
    if (!pg) {
        cell_act(c, "PATTERN", TN[k & 3u], 0, 0);
        if (k == TL_LEN) {
            c->kind = CK_VAL;
            str_cpy(c->val, tl.half ? "1/2" : "x2", sizeof c->val);
            cell_gauge(c, 1, 0, 1, tl.half);
        }
        return;
    }
    eu_fit(t);
    if (k == 3u) {                                      /* (a synth track: no lane, an empty card) */
        if (is_drum(t)) {
            c->label = "LANE";
            c->kind = CK_RO;
            str_cpy(c->val, LANE_SHORT[lane_selected()], sizeof c->val);
            cell_gauge(c, 1, 0, DRUM_LANES - 1, (int32_t)lane_selected());
        }
        return;
    }
    c->label = EN[k % 3u];
    c->kind = CK_VAL;
    fmt_int(c->val, k == 0u ? tl.hits : k == 1u ? tl.steps : tl.rot);
    cell_gauge(c, 0, k == 1u ? 1 : 0, k == 1u ? (int32_t)trk_len(t) + 1 : k == 2u ? tl.steps : tl.steps,
               k == 0u ? tl.hits : k == 1u ? tl.steps : tl.rot);   /* (a bar never of no width: +1 on STEPS, ROT) */
}
static void tl_turn(uint32_t pg, uint32_t k, int32_t s)
{
    if (pg) {
        eu_turn(k, s);
        return;
    }
    if (k == TL_LEN)
        tl.half = (uint8_t)(s == OP_RESET ? 0 : s > 0 ? 0 : 1);
}
static void tl_say(const char *a, const char *b)       /* what was done: a toast */
{
    ui.toast_next = 1;
    ui_say(a, b);
    ui.toast_next = 0;
}
/* YES on TOOLS' card k: do it on the selected track (or lane); 1 done */
static int tl_yes(uint32_t pg, uint32_t k)
{
    track_t *t = TSEL;
    uint32_t len = trk_len(t), i, n = 0, nl;
    int32_t lane = tl_lane();
    char b[24];
    const char *who = lane >= 0 ? LANE_NAME[lane] : trk_tag(song.sel), *what;
    if (pg)
        return 0;
    if (k == TL_LEN) {
        if (!tl.half && (len >= NSTEP || !tl_ev_double(t, len, tl_dbl_len(len), 1))) {
            tl_say(len >= NSTEP ? "64 STEPS: NO ROOM" : "PATTERN FULL", "");
            return 1;
        }
        if (tl.half && len < 2u) {
            tl_say("1 STEP: NO HALF", "");
            return 1;
        }
        tl_mark(t);
        fm1_irq_off();
        if (tl.half)
            t->p[P_SLEN] = (int16_t)(len / 2u);         /* (the steps past it stay, as the LEN card leaves them) */
        else
            tl_double_do(t, len);
        fm1_irq_on();
        sync_reload = 1;
        nl = trk_len(t);
        str_cpy(b, tl.half ? "HALVED: " : "DOUBLED: ", sizeof b);
        fmt_int(b + str_len(b), (int32_t)nl);
        tl_say(b, " STEPS");
        return 1;
    }
    for (i = 0; i < len; i++)                           /* (COOK: something to cook) */
        n += is_drum(t) ? (uint32_t)__builtin_popcount(dstep_lanes(&t->dstep[i]) & (lane >= 0 ? 1u << lane : TL_ALL))
                        : (uint32_t)tl_gate(&t->step[i]);
    if (k == TL_COOK && !n) {
        tl_say("NOTHING TO COOK", "");
        return 1;
    }
    tl_mark(t);
    fm1_irq_off();
    if (k == TL_REV) {
        tl_reverse(t, len, lane);
        what = " REVERSED";
    } else if (k == TL_RND) {
        if (!is_drum(t)) {
            tl_random_notes(t, len);
        } else if (lane >= 0) {
            tl_lane_rnd(t, len, (uint32_t)lane, 0);
        } else {
            memset(t->dstep, 0, len * sizeof(dstep_t));
            for (i = 0; i < 8u; i++)
                tl_lane_rnd(t, len, i, 1);
        }
        what = " RANDOM";
    } else {
        if (is_drum(t))
            tl_cook_drum(t, len, n, lane >= 0 ? 1u << (uint32_t)lane : TL_ALL);
        else
            tl_cook_notes(t, len);
        what = " COOKED";
    }
    if (k != TL_REV)
        tl_unlock(t, len);
    t->seq_active = 1;
    fm1_irq_on();
    sync_reload = 1;
    tl_say(who, what);
    return 1;
}
#endif
