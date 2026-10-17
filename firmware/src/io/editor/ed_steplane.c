/* SPDX-License-Identifier: GPL-3.0-only */
/* Editor protocol v14: a step in the context of a track and a drum lane (included by editor.c; web/EDITOR_PROTOCOL.md "v14: step lanes"). INFO tag 0x62.
 * The Optimist UI's held step, for the editor: the lane's level and ratchet, the step's extras (chance, condition, nudge, fill) as the one automation
 * store keeps them (seq/auto.c), and the device's own list of lockable values for the track and lane with the locks and hold events on the step. The
 * list is the Optimist UI's lock pages (op_step.c lock_pg_ok / hp_locks / lock_param, with the track and the lane set as the held step has them), or,
 * with SLOOP's UI, the pages' values that lock_ok takes; a drum lane's values are TUNE .. LEVEL (auto_lane.c), the ACCENT lane's the accent amount.
 *   105 STEPLANE  op track lane [...]    lane 0..15 a drum lane, 16 the ACCENT lane, 127 none (a synth track: 127)
 *       op 0 LIST                  -> 0, track, lane, n (<= 127), np
 *       op 1 DESC  i               -> 1, track, lane, i, kind, id, page, cell, title (12), DESC's body (fmt, min v14, max v14, def v14, label, unit, names)
 *       op 2 STEP  step            -> the state
 *       op 3 SET   step, field, v14   field 0 level, 1 ratchet, 2 chance, 3 cond, 4 nudge, 5 fill
 *       op 4 LOCK  step, i, v14    op 5 UNLOCK  step, i    op 6 KIND  step, i (lock <-> hold)      -> the state (after)
 *       state: op, rc, track, lane, step, flags, level, ratchet, chance, cond, nudge + 64, fill, n, n x (i, kind 1 lock / 2 hold, v14)
 *       rc: 0 done, 1 refused, 2 the list is full, 3 no hit (D4), 4 nothing to do. A bad argument: no reply.
 * Writes mark the undo history (ed_undo_edit) and take the interrupt off (IRQ_SAVE / IRQ_RESTORE: they nest). INFO tag 0x62: version 1, a, b (the doc). */
#include "ed_reply.h"         /* (ed_b2..ed_b4, ed_b32) */
#include "ed_desc.h"          /* (DESC's body) */
enum { ED_STEPLANE = 105, STL_MAX = 127 };
static void ed9_known_step(uint32_t t, uint32_t i);   /* ed_sync9.c (included after this) */
#define STL_ON (FELUCCA_ED_STEPLANE && FELUCCA_AUTO)
#if STL_ON
typedef struct {
    uint8_t kind, id, page, cell;                       /* kind 0: a track parameter (P_*), 1: a drum lane's value (DE_*) */
    const char *title;
    const param_desc_t *d;
} stl_e;
typedef void (*stl_visit_t)(void *ctx, uint32_t i, const stl_e *e);
typedef struct {
    uint32_t n, want;
    uint8_t seen[(P_COUNT + 7u) / 8u + 1u], lseen;
    uint8_t page, pg_has;
    stl_e *out;
    stl_visit_t visit;
    void *ctx;
} stl_walk_t;
static void stl_add(stl_walk_t *w, uint32_t kind, uint32_t id, uint32_t page, uint32_t cell, const char *title, const param_desc_t *d)
{
    stl_e e;
    if (!d || w->n >= STL_MAX)
        return;
    if (kind == 0u ? (w->seen[id >> 3] >> (id & 7u)) & 1u : (w->lseen >> (id & 7u)) & 1u)
        return;                                         /* (a value once) */
    if (kind == 0u)
        w->seen[id >> 3] |= (uint8_t)(1u << (id & 7u));
    else
        w->lseen |= (uint8_t)(1u << (id & 7u));
    e.kind = (uint8_t)kind, e.id = (uint8_t)id, e.page = (uint8_t)page, e.cell = (uint8_t)cell, e.title = title, e.d = d;
    if (w->out && w->n == w->want)
        *w->out = e;
    if (w->visit)
        w->visit(w->ctx, w->n, &e);
    w->n++;
    w->pg_has = 1;
}
#if FELUCCA_UI == 1 && FELUCCA_PLOCK
static struct { uint8_t sel, lane, lsel; } stl_was;
static void stl_in(uint32_t track, uint32_t lane)       /* the held step's context: the track, the lane picked (as ed_scrs.c scrs_in) */
{
    stl_was.sel = (uint8_t)song.sel, stl_was.lane = ui.lane, stl_was.lsel = lane_sel;
    song.sel = (uint8_t)track;
    ui.lane = lane <= DRUM_ACC && is_drum(&trk[track]);
    if (ui.lane)
        lane_sel = (uint8_t)lane;
}
static void stl_out(void) { song.sel = stl_was.sel, ui.lane = stl_was.lane, lane_sel = stl_was.lsel; }
/* the Optimist UI's p-lock pages of the track (hp_locks) and their lockable cells (lock_param) */
static void stl_pages(stl_walk_t *w, uint32_t track, uint32_t lane)
{
    uint8_t ix[OP_MAXROWS];
    uint32_t p, k, np;
    stl_in(track, lane);
    np = hp_locks(ix);
    for (p = 0; p < np; p++) {
        const page_t *pg = &PAGES[ix[p]];
        w->pg_has = 0;
        for (k = 0; k < 4u; k++) {
            const param_desc_t *d;
            int32_t id = lock_param(pg, k, &d);
            if (id >= 0 && (lane < DRUM_LANES || !((uint32_t)id & LOCK_LANE)))   /* (DR itself has no lane values) */
                stl_add(w, (uint32_t)id & LOCK_LANE ? 1u : 0u, (uint32_t)id & LOCK_LANE ? (uint32_t)id & 7u : (uint32_t)id, w->page, k,
                        dr_alias(pg) ? dr_alias(pg) : pg->title, d);
        }
        w->page = (uint8_t)(w->page + w->pg_has);
    }
    stl_out();
}
#define STL_UI 1u
#else
#define STL_UI 0u
static void stl_pages(stl_walk_t *w, uint32_t track, uint32_t lane)
{
    const track_t *t = &trk[track];
    uint32_t p, k;
#if FELUCCA_PLOCK
    uint8_t was = (uint8_t)song.sel;
    (void)lane;
    song.sel = (uint8_t)track;                          /* (page_shown, page_for_drum read the selected track) */
    for (p = 0; p < NPAGES; p++) {
        const page_t *pg = &PAGES[p];
        if ((pg->scope != SC_TRACK && pg->scope != SC_ENGINE) || !page_shown(pg) || (is_drum(t) && !page_for_drum(pg)))
            continue;
        w->pg_has = 0;
        for (k = 0; k < 4u; k++) {
            uint32_t id = page_id(pg, k);
            if (id < P_COUNT && lock_ok(t, id) && lock_desc(t, id)->max > lock_desc(t, id)->min && !(is_drum(t) && id > P_E0 && id <= P_E7))
                stl_add(w, 0u, id, w->page, k, pg->title, lock_desc(t, id));
        }
        w->page = (uint8_t)(w->page + w->pg_has);
    }
    song.sel = was;
#else
    (void)w, (void)t, (void)p, (void)k, (void)lane;
#endif
}
#endif
/* the list for (track, lane): every entry to w->visit, entry w->want to *w->out; -> the count */
static uint32_t stl_walk(uint32_t track, uint32_t lane, uint32_t want, stl_e *out, stl_visit_t visit, void *ctx)
{
    stl_walk_t w;
    memset(&w, 0, sizeof w);
    w.want = want, w.out = out, w.visit = visit, w.ctx = ctx;
    if (lane == DRUM_ACC) {
#if FELUCCA_DRUM_ACCENT && FELUCCA_PLOCK
        stl_add(&w, 0u, P_DACC, 0u, 0u, "ACCENT", &DRUM_ACC_DESC);
        w.page = 1;
#endif
        return w.n;
    }
#if !(FELUCCA_UI == 1 && FELUCCA_PLOCK)
    if (is_drum(&trk[track]) && lane < DRUM_LANES) {    /* (SLOOP's UI has no lane pages: the lane's values are TUNE .. LEVEL) */
#if DLA_ON && FELUCCA_PLOCK
        uint32_t v;
        for (v = 0; v < DE_N; v++)
            stl_add(&w, 1u, v, 0u, v & 3u, "LANE", &DLA_DESC[v]);
#endif
        return w.n;
    }
#endif
    stl_pages(&w, track, lane);
    return w.n;
}
static uint32_t stl_count(uint32_t track, uint32_t lane, uint32_t *np)
{
    uint32_t n = stl_walk(track, lane, 0xFFFFu, 0, 0, 0), i, last = 0xFFFFu, pages = 0;
    stl_e e;
    for (i = 0; i < n; i++) {
        (void)stl_walk(track, lane, i, &e, 0, 0);
        if (e.page != last)
            pages++, last = e.page;
    }
    *np = pages;
    return n;
}

/* the step's events of the list's values go out as (i, kind, value); the count is patched in after (as ed_stepx.c LOCK_GET) */
typedef struct {
    const track_t *t;
    uint32_t step, lane, n;
} stl_ev_t;
static uint32_t stl_event(const track_t *t, uint32_t step, uint32_t lane, const stl_e *e, int32_t *v)
{
    if (e->kind == 1u) {
#if DLA_ON
        return lane_lock_get(t, step, lane, e->id, v) ? 1u : lane_hold_get(t, step, lane, e->id, v) ? 2u : 0u;
#else
        return 0;
#endif
    }
    return lock_get(t, step, e->id, v) ? 1u : hold_get(t, step, e->id, v) ? 2u : 0u;
}
static void stl_visit_ev(void *ctx, uint32_t i, const stl_e *e)
{
    stl_ev_t *x = (stl_ev_t *)ctx;
    int32_t v = 0;
    uint32_t k = stl_event(x->t, x->step, x->lane, e, &v);
    if (k) {
        ed_b2(i, k), ed_v(v);
        x->n++;
    }
}
static int stl_step_has(const track_t *t, uint32_t step, uint32_t lane)
{
    if (is_drum(t))
        return lane == 127u ? dstep_lanes(&t->dstep[step]) != 0u : dstep_has(&t->dstep[step], lane);
    return t->step[step].time == ST_NOTE && t->step[step].n;
}
/* the state reply of step `step` in (track, lane) */
static void stl_state(uint32_t op, uint32_t rc, uint32_t track, uint32_t lane, uint32_t step)
{
    track_t *t = &trk[track];
    stl_ev_t x;
    uint32_t lv = 0, rt = 0, chance = 100, cond = 0, fill = 0, flags, at;
    int32_t nudge = 0;
    int has = stl_step_has(t, step, lane);
    if (is_drum(t)) {
        if (lane < DRUM_LANES || lane == DRUM_ACC)
            lv = dstep_lvl(&t->dstep[step], lane), rt = dstep_rat(&t->dstep[step], lane);
    } else {
        lv = t->step[step].lvl & 3u, rt = t->step[step].rat & 3u;
    }
#if FELUCCA_CHANCE
    chance = step_chance_ev(t, step);
    if (chance >= 100u && !is_drum(t))
        chance = step_chance(&t->step[step]);
#endif
#if FELUCCA_COND
    cond = step_cond(t, step);
#endif
#if FELUCCA_MICRO
    nudge = step_micro(t, step);
#endif
#if FELUCCA_FILLS
    fill = step_fill(t, step) % 3u;
#endif
    flags = (has ? 1u : 0u) | ((is_drum(t) ? has : 1) ? 2u : 0u) | (step >= trk_len(t) ? 4u : 0u);
    ed_b4(op, rc, track, lane); ed_b4(step, flags, lv, rt); ed_b4(chance, cond, (uint32_t)(nudge + 64), fill);
    at = ed_n;
    ed_b(0);
    x.t = t, x.step = step, x.lane = lane, x.n = 0;
    (void)stl_walk(track, lane, 0xFFFFu, 0, stl_visit_ev, &x);
    ed_out[at] = (uint8_t)(x.n & 127u);
}
/* SET: 0 done, 1 refused, 2 full, 3 no hit */
static uint32_t stl_set(track_t *t, uint32_t lane, uint32_t step, uint32_t field, int32_t v)
{
    uint32_t k;
    switch (field) {
    case 0:
    case 1:
        if (v < 0 || v > 3)
            return 1;
        if (is_drum(t)) {
            dstep_t *d = &t->dstep[step];
            if (lane == 127u)
                return 1;
            if (!dstep_has(d, lane))
                return 3;
            dstep_set(d, lane, field ? dstep_lvl(d, lane) : (uint32_t)v, field ? (uint32_t)v : dstep_rat(d, lane));
        } else {
            step_t *s = &t->step[step];
            if (!stl_step_has(t, step, lane))
                return 3;
            for (k = 0; k < s->n && k < 4u; k++) {
                uint32_t sh = 2u * k;
                if (field)
                    s->rat = (uint8_t)((s->rat & ~(3u << sh)) | (uint32_t)v << sh);
                else
                    s->lvl = (uint8_t)((s->lvl & ~(3u << sh)) | (uint32_t)v << sh);
            }
        }
        return 0;
    case 2:
#if FELUCCA_CHANCE
        if (v < 0 || v > 100)
            return 1;
        if (is_drum(t) ? !dstep_lanes(&t->dstep[step]) : !stl_step_has(t, step, lane))
            return 3;
        return step_chance_set(t, step, (uint32_t)v) ? 0u : 2u;
#else
        return 1;
#endif
    case 3:
#if FELUCCA_COND
        return v < 0 || v > (int32_t)AUTO_NCOND ? 1u : step_cond_set(t, step, (uint32_t)v) ? 0u : 2u;
#else
        return 1;
#endif
    case 4:
#if FELUCCA_MICRO
        return v < MICRO_MIN || v > MICRO_MAX ? 1u : step_micro_set(t, step, v) ? 0u : 2u;
#else
        return 1;
#endif
    case 5:
#if FELUCCA_FILLS
        return v < 0 || v > 2 ? 1u : step_fill_set(t, step, (uint32_t)v) ? 0u : 2u;
#else
        return 1;
#endif
    default:
        return 1;
    }
}
/* LOCK / UNLOCK / KIND of list entry e on the step, as the device's knob, SHIFT + knob and YES do */
static uint32_t stl_lock(track_t *t, uint32_t lane, uint32_t step, uint32_t op, const stl_e *e, int32_t v)
{
#if FELUCCA_PLOCK
    int32_t x;
    if (e->kind == 1u) {
#if DLA_ON
        if (op == 5u) {
            lane_lock_drop(t, step, lane, e->id);
            (void)lane_hold_drop(t, step, lane, e->id);
            return 0;
        }
        if (op == 6u)
            return lane_kind_toggle(t, step, lane, e->id) ? 0u : 4u;
        if (!lane_lock_get(t, step, lane, e->id, &x) && lane_hold_get(t, step, lane, e->id, &x))
            return auto_put(AL(t), step, AUTO_LANE(lane, e->id), clamp(v, DE_MIN[e->id], DE_MAX[e->id])) >= 0 ? 0u : 2u;
        {
            int rc = lane_lock_set(t, step, lane, e->id, v);
            return rc == LANE_NOHIT ? 3u : rc ? 0u : 2u;
        }
#else
        (void)lane, (void)v;
        return 1;
#endif
    }
    if (op == 5u) {
        lock_drop(t, step, e->id);
        (void)auto_step_hold_drop(t, step, e->id);
        return 0;
    }
    if (op == 6u)
        return auto_kind_toggle(t, step, e->id) ? 0u : 4u;
    if (is_drum(t) && e->id == P_DACC && !dstep_has(&t->dstep[step], DRUM_ACC))
        return 3;                                       /* (the accent amount's lock needs the accent hit on the step) */
    v = clamp(v, e->d->min, e->d->max);
    if (!lock_get(t, step, e->id, &x) && hold_get(t, step, e->id, &x))
        return auto_put(AL(t), step, mot_sid(e->id), v) >= 0 ? 0u : 2u;
    return lock_set(t, step, e->id, v) ? 0u : 2u;
#else
    (void)t, (void)lane, (void)step, (void)op, (void)e, (void)v;
    return 1;
#endif
}
static int ed_steplane(uint32_t cmd, const uint8_t *a, uint32_t na)
{
    uint32_t op = na ? a[0] : 127u, track, lane, step = 0, i, n, np, rc = 0;
    track_t *t;
    stl_e e;
    if (cmd != ED_STEPLANE || na < 3u || op > 6u || a[1] >= NTRK)
        return 0;
    track = a[1], lane = a[2] == 127u ? 127u : edl_in(a[2]), t = &trk[track];   /* (the wire's lane: ed_drums.c) */
    if (is_drum(t) ? !(lane < DRUM_LANES || lane == DRUM_ACC || lane == 127u) || (lane == DRUM_ACC && !FELUCCA_DRUM_ACCENT) : lane != 127u)
        return 0;
    if (op == 0u) {
        n = stl_count(track, lane, &np);
        ed_b4(0, track, edl_out(lane), n); ed_b(np);
        return 1;
    }
    if (op == 1u) {
        if (na < 4u)
            return 0;
        i = a[3];
        n = stl_walk(track, lane, i, &e, 0, 0);
        ed_b4(1, track, edl_out(lane), i);
        if (i >= n)
            return 1;
        ed_b4(e.kind, e.id, e.page, e.cell);
        ed_str(e.title, 12);
        ed_desc_body(e.d);
        return 1;
    }
    if (na < 4u || a[3] >= NSTEP || (op == 3u && na < 7u) || (op == 4u && na < 7u) || (op >= 5u && na < 5u))
        return 0;
    step = a[3];
    if (op != 2u) {
        ed_undo_edit(t);                                /* (it takes the interrupt off itself: it marks first) */
        int32_t v = op <= 4u ? ed_rv(a + 5) : 0;        /* (SET, LOCK: the value) */
        if (op != 3u && a[4] >= stl_walk(track, lane, a[4], &e, 0, 0)) {
            rc = 1;                                     /* (LOCK, UNLOCK, KIND: no such entry) */
        } else {
            IRQ_SAVE(ie2);
            rc = op == 3u ? stl_set(t, lane, step, a[4], v) : stl_lock(t, lane, step, op, &e, v);
            auto_touch(trk_index(t));
            IRQ_RESTORE(ie2);
        }
        if (track == song.sel)
            ed_w.st[step] = ed_step_sig(t, step);
        ed9_known_step(track, step);                    /* (the editor's own write: no STEPS push for it) */
        ui.force = 1;
    }
    stl_state(op, rc, track, lane, step);
    return 1;
}
#define ED_STEPLANE_FN ed_steplane,         /* (editor.c: its table of commands) */
static void ed_steplane_info(void)
{
    uint32_t a = STL_UI | (FELUCCA_PLOCK ? 2u : 0u) | (DLA_ON && FELUCCA_PLOCK ? 4u : 0u) | (FELUCCA_MOTION ? 8u : 0u);
    uint32_t b = (FELUCCA_CHANCE ? 1u : 0u) | (FELUCCA_COND ? 2u : 0u) | (FELUCCA_MICRO ? 4u : 0u) | (FELUCCA_FILLS ? 8u : 0u);
    ed_b4(0x62, 3, 1, a); ed_b(b);
}
#else
static int ed_steplane(uint32_t cmd, const uint8_t *a, uint32_t na) { (void)cmd, (void)a, (void)na; return 0; }
#define ED_STEPLANE_FN
static void ed_steplane_info(void) {}
#endif
