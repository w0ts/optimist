/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments (Felucca 1.2 .. 1.5.6 SEQ TOOLS,
 * hugelton/Felucca 9f6a885, firmware/src/ui_tools.c: REVERSE, RANDOM, COOK, EUCLID, DOUBLE, HALF) */
/* STEP's TOOLS, GEN and GEN OUT pages (FELUCCA_SEQTOOLS, backports15.h; docs/UI-OPTIMIST-DESIGN.md section 22). Included by
 * optimist.c before op_step.c, which shows them as STEP's last three pages (SELECT, SEQ tapped again).
 *
 *   TOOLS   REVERSE · RANDOM · COOK · LENGTH. A turn arms the card (the hot one; LENGTH's turn says x2 or 1/2), YES
 *           (SAVE tapped) does it, a toast says what was done. Each is one undo level (SAVE then HOME).
 *   GEN     TYPE · its three values (EUCLID: HITS STEPS ROT; RANDOM: DENSITY SEED ACCENT; PULSE: EVERY OFFSET
 *           RATCHET): the note generator of the track (a drum track: of its selected lane), seq/gen.c, a layer played
 *           over the pattern, never written into it; a turn is heard at the next step. HOME + a knob: its default.
 *   GEN OUT GEN (ON / OFF: the pattern untouched either way) · MODE (REPLACE ADD MASK) · WRITE (over T1 or the lane's
 *           code; asks: what plays, into the pattern as ordinary steps, and the slot OFF; one undo level brings back
 *           both, the pattern and ON) · KNOB 4 none. Every card keeps its place: OFF, WRITE's place is an empty cell.
 *
 * The scope is ALGORITHM's: a synth track; DR itself the whole kit; a drum lane (lane_picked) that lane only
 * (REVERSE RANDOM COOK; GEN on DR itself the last picked lane, shown on GEN OUT). LENGTH is the whole pattern.
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
    uint8_t sel;                        /* the tool the one TOOLS card has picked: TS_* below */
    uint32_t sess;                      /* the undo session of the last tool */
} tl = {0, 0, 0};
enum { TS_REV, TS_RND, TS_COOK, TS_DBL, TS_HALF, TS_N };   /* (the card's choices; DOUBLE and HALF are LENGTH's two) */
static const char *const TS_NAME[TS_N] = {"REVERSE", "RANDOM", "COOK", "DOUBLE", "HALF"};
static uint32_t ts_tool(uint32_t sel) { return sel == TS_DBL || sel == TS_HALF ? (uint32_t)TL_LEN : sel; }

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
static const dstep_t *tl_was_d(const step_t *was) { return (const dstep_t *)(const void *)was; }   /* (the union's bytes: seq/undo.c) */

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
        if (s >= len || (lane >= 0 && !AUTO_LANE_EV(e->param, lane)))   /* (its values and its mute) */
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
static void tl_unlock_from(track_t *t, uint32_t len, const step_t *was)
{
#if FELUCCA_AUTO
    uint32_t i, l;
    for (i = 0; i < len; i++) {
        if (!is_drum(t)) {
            if (was[i].time != ST_REST && t->step[i].time == ST_REST)
                (void)auto_step_clear(t, i, AUTO_ONLYS);
            continue;
        }
        if (dstep_lanes(&tl_was_d(was)[i]) && !dstep_lanes(&t->dstep[i])) {   /* (the ACCENT lane is a lane) */
            (void)auto_step_clear(t, i, AUTO_ONLYS);
            continue;
        }
        for (l = 0; l < DRUM_LANES; l++)
            if (dstep_has(&tl_was_d(was)[i], l) && !dstep_has(&t->dstep[i], l)) {
#if DLA_ON
                (void)lane_step_drop(t, i, l);
#endif
                (void)step_mute_set(t, i, l, 0);        /* (the hit gone: its mute too) */
            }
    }
    (void)l;
    auto_touch(trk_index(t));
#else
    (void)t, (void)len, (void)was;
#endif
}
static void tl_unlock(track_t *t, uint32_t len) { tl_unlock_from(t, len, undo.st); }
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
        const dstep_t *o = tl_was_d(undo.st);
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

/* ---- the note generators (seq/gen.c): GEN (TYPE and its three values) and GEN OUT (ON, MODE, WRITE, the slot's read-out)
 * set the slot of the selected track (a drum track: its selected lane). A turn is heard at the next step; nothing is
 * written into the pattern but by WRITE (it asks; one undo level). ON / OFF never touch the pattern */
static void tl_say(const char *a, const char *b);       /* (below) */
static uint32_t ng_slot(void) { return gen_slot(song.sel % NTRK, lane_selected()); }
static gen_t *ng_cur(void) { return &gen[ng_slot() % GEN_NSLOT]; }
static const char *ng_who(void)                         /* the slot's name: "T1", a lane's */
{
    return is_drum(TSEL) ? lane_name(lane_selected()) : trk_tag(song.sel);
}
static int32_t ng_max(const gen_par_t *d, uint32_t len) { return d->max ? d->max : (int32_t)len; }
static void ng_put(const gen_t *g)                      /* the slot's settings now (the IRQ off: the ISR reads them) */
{
    gen[ng_slot() % GEN_NSLOT] = *g;
    gen_sync();
}
static void ng_fmt(char *b, uint32_t disp, uint32_t v)
{
    if (disp == GD_X)
        b[0] = 'x', fmt_int(b + 1, (int32_t)v);
    else
        fmt_int(b, (int32_t)v);
    if (disp == GD_PCT)
        str_cpy(b + str_len(b), "%", 2);
}
static void ng_cell(uint32_t pg, uint32_t k, cell_t *c)   /* pg 1 GEN, 2 GEN OUT */
{
    gen_t g = *ng_cur();
    const gen_type_t *ty = &GEN_TYPES[g.type % GT_N];
    uint32_t len = trk_len(TSEL);
    ty->fit(&g, len);
    c->kind = CK_VAL;
    if (pg == 1u) {
        if (!k) {
            cell_list(c, "TYPE", ty->name, GT_N - 1, g.type);
            return;
        }
        {
            const gen_par_t *d = &ty->par[(k - 1u) % GEN_NP];
            c->label = d->label;
            ng_fmt(c->val, d->disp, g.p[k - 1u]);
            cell_gauge(c, 0, d->min, ng_max(d, len) > d->min ? ng_max(d, len) : d->min + 1, g.p[k - 1u]);
        }
        return;
    }
    if (k == 0u) {                                      /* ON / OFF: heard at once, the pattern untouched */
        cell_list(c, "GEN", g.on ? "ON" : "OFF", 1, g.on);
    } else if (k == 1u) {
        cell_list(c, "MODE", GEN_MODE[g.mode % GM_N], GM_N - 1, g.mode);
    } else if (k == 2u && g.on) {                       /* (the verb over its object: what plays, into the pattern; asks.
                                                         * OFF: nothing to write, no card) */
        cell_act(c, is_drum(TSEL) ? lane_code(lane_selected()) : trk_tag(song.sel), "WRITE", 0, 0);
    } else {
        c->kind = CK_NONE;
    }
}
/* a knob on GEN / GEN OUT: read, changed and written back with the IRQ off (a song section the ISR applies meanwhile,
 * fx_rec.c gen_untlv, is not undone by a stale copy) */
static void ng_turn(uint32_t pg, uint32_t k, int32_t s)
{
    gen_t g;
    const gen_type_t *ty;
    uint32_t len = trk_len(TSEL);
    if (pg == 2u && k >= 2u)
        return;                                         /* (WRITE: a turn picks the card; the read-out) */
    fm1_irq_off();
    g = *ng_cur();
    if (pg == 1u && !k) {
        uint32_t ty0 = s == OP_RESET ? GT_EUCLID : (uint32_t)clamp((int32_t)g.type + s, 0, GT_N - 1);
        if (ty0 != g.type)
            gen_defaults(&g, ty0);                      /* (another generator: its values at their defaults) */
    } else if (pg == 1u) {
        const gen_par_t *d = &GEN_TYPES[g.type % GT_N].par[(k - 1u) % GEN_NP];
        uint32_t j = k - 1u;
        GEN_TYPES[g.type % GT_N].fit(&g, len);
        g.p[j] = (uint8_t)(s == OP_RESET ? d->def : clamp((int32_t)g.p[j] + s, d->min, ng_max(d, len)));
    } else if (k == 0u) {
        g.on = (uint8_t)(s == OP_RESET ? 0 : s > 0);
    } else {
        g.mode = (uint8_t)(s == OP_RESET ? GM_REPLACE : clamp((int32_t)g.mode + s, 0, GM_N - 1));
    }
    ty = &GEN_TYPES[g.type % GT_N];
    ty->fit(&g, len);
    ng_put(&g);
    fm1_irq_on();
}
static int ng_yes(uint32_t k, uint32_t ok)              /* GEN OUT's ON and WRITE */
{
    gen_t g = *ng_cur();
    track_t *t = TSEL;
    char a[12];
    if (k == 0u) {
        fm1_irq_off();                                  /* (read, toggled, written: one section, as ng_turn) */
        g = *ng_cur();
        g.on = (uint8_t)!g.on;
        ng_put(&g);
        fm1_irq_on();
        tl_say(g.on ? "GEN ON: " : "GEN OFF: ", ng_who());
        return 1;
    }
    if (k != 2u)
        return 0;
    if (!g.on) {
        tl_say("GEN IS OFF", "");
        return 1;
    }
    if (!ok) {
        str_cpy(a, ng_who(), sizeof a);
        op_arm(SCR_STEP, ui.row[SCR_STEP], 2, "WRITE", a, 0);
        return 1;
    }
    if (!gen_write_prep(t, ng_slot())) {                /* (the render with the IRQ on: seq/gen.c) */
        tl_say("GEN IS OFF", "");
        return 1;
    }
    tl_mark(t);                                         /* (one undo level: the pattern and the slot's ON, seq/undo.c) */
    fm1_irq_off();
    (void)gen_write_commit(t);
    gen[ng_slot() % GEN_NSLOT].on = 0;                  /* (baked: it plays as the pattern now, the generator OFF) */
    gen_sync();
    t->seq_active = 1;
    fm1_irq_on();
    sync_reload = 1;
    tl_say(ng_who(), " WRITTEN, GEN OFF");
    return 1;
}

/* ---- DOUBLE, HALF (and the EDIT layer's LENGTH, op_layers.c) */
static uint32_t tl_dbl_len(uint32_t len) { return 2u * len < NSTEP ? 2u * len : NSTEP; }
/* steps 1..len after themselves, LEN x2 (64 at most), their events copied (the IRQ off; tl_ev_double(.., 1) said yes) */
static void tl_double_do(track_t *t, uint32_t len)
{
    uint32_t nl = tl_dbl_len(len), z = is_drum(t) ? sizeof(dstep_t) : sizeof(step_t);   /* (a drum step, a synth step) */
    if (nl > len)
        memcpy((uint8_t *)t->step + len * z, t->step, (nl - len) * z);
    (void)tl_ev_double(t, len, nl, 0);
    t->p[P_SLEN] = (int16_t)nl;
}

/* ---- the pages */
static void tl_cell(uint32_t pg, uint32_t k, cell_t *c)   /* pg 0 TOOLS, 1 GEN, 2 GEN OUT */
{
    cell_clear(c);
    if (!pg) {
        if (k == 0u) {                                       /* (one card: its knob picks the tool, YES does it) */
            c->kind = CK_VAL;                                /* (a value cell: the turn reaches tl_turn, an action cell's would only pick it) */
            cell_list(c, "TOOL", TS_NAME[tl.sel % TS_N], (int32_t)TS_N - 1, tl.sel);       /* (dots: the five tools) */
        } else if (k == 1u) {                                  /* what it will work on: the track, or the picked drum lane */
            int32_t ln = tl_lane();
            c->kind = CK_VAL;
            cell_list(c, "ON", ln >= 0 ? lane_name((uint32_t)ln) : trk_tag(song.sel), 1, 1);
        }
        return;
    }
    ng_cell(pg, k, c);
}
static void tl_turn(uint32_t pg, uint32_t k, int32_t s)
{
    if (pg) {
        ng_turn(pg, k, s);
        return;
    }
    if (k == 0u) {                                      /* the one TOOLS card: a turn picks the tool */
        int32_t v = s == OP_RESET ? 0 : (int32_t)tl.sel + s;
        v %= (int32_t)TS_N;
        tl.sel = (uint8_t)(v < 0 ? v + (int32_t)TS_N : v);
        tl.half = tl.sel == TS_HALF;
    }
}
static void tl_say(const char *a, const char *b)       /* what was done: a toast */
{
    ui.toast_next = 1;
    ui_say(a, b);
    ui.toast_next = 0;
}
/* tool k (TL_REV .. TL_LEN; half: LENGTH is 1/2, else x2) on track t, drum lane `lane` (-1: the whole kit or a synth track): what it says is
 * a toast as two strings (*sa, *sb; buf holds one of them). One undo level when it changes the pattern. The Optimist UI's YES and the
 * web editor's TOOLS (io/editor/ed_tools.c) both call it. 1 done (or said why not: it is a toast either way) */
static int tl_run(track_t *t, uint32_t k, int32_t lane, uint32_t half, const char **sa, const char **sb, char *buf)
{
    uint32_t len = trk_len(t), i, n = 0, nl;
    const char *who = lane >= 0 ? lane_name((uint32_t)lane) : trk_tag(song.sel), *what;
    *sa = who, *sb = "";
    if (k == TL_LEN) {
        if (!half && (len >= NSTEP || !tl_ev_double(t, len, tl_dbl_len(len), 1))) {
            *sa = len >= NSTEP ? "64 STEPS: NO ROOM" : "PATTERN FULL";
            return 1;
        }
        if (half && len < 2u) {
            *sa = "1 STEP: NO HALF";
            return 1;
        }
        tl_mark(t);
        fm1_irq_off();
        if (half)
            t->p[P_SLEN] = (int16_t)(len / 2u);         /* (the steps past it stay, as the LEN card leaves them) */
        else
            tl_double_do(t, len);
        fm1_irq_on();
        sync_reload = 1;
        nl = trk_len(t);
        str_cpy(buf, half ? "HALVED: " : "DOUBLED: ", 24);
        fmt_int(buf + str_len(buf), (int32_t)nl);
        *sa = buf, *sb = " STEPS";
        return 1;
    }
    for (i = 0; i < len; i++)                           /* (COOK: something to cook) */
        n += is_drum(t) ? (uint32_t)__builtin_popcount(dstep_lanes(&t->dstep[i]) & (lane >= 0 ? 1u << lane : TL_ALL))
                        : (uint32_t)tl_gate(&t->step[i]);
    if (k == TL_COOK && !n) {
        *sa = "NOTHING TO COOK";
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
    *sb = what;
    return 1;
}
/* YES on TOOLS' card k: do it on the selected track (or lane); 1 done */
static int tl_yes(uint32_t pg, uint32_t k, uint32_t ok)
{
    track_t *t = TSEL;
    int32_t lane = tl_lane();
    char b[24];
    const char *who = lane >= 0 ? lane_name((uint32_t)lane) : trk_tag(song.sel), *sa, *sb;
    if (pg)                                             /* GEN OUT's ON (at once) and WRITE (asks) */
        return pg == 2u ? ng_yes(k, ok) : 0;
    if (k != 0u)                                        /* TOOLS has one card: the others do nothing */
        return 0;
    if (!ok) {                                          /* (every tool asks: "Reverse T1?") */
        char a[12];
        if (!ui.hot_lit) {                              /* (a card is picked by turning its knob) */
            tl_say("TURN A KNOB", " TO PICK");
            return 1;
        }
        str_cpy(a, who, sizeof a);
        op_arm(SCR_STEP, ui.row[SCR_STEP], k, TS_NAME[tl.sel % TS_N], a, 0);
        return 1;
    }
    (void)tl_run(t, ts_tool(tl.sel), lane, tl.sel == TS_HALF, &sa, &sb, b);
    tl_say(sa, sb);
    return 1;
}
#endif
