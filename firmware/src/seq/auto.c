/* SPDX-License-Identifier: GPL-3.0-only */
/* The automation store (FELUCCA_AUTO: motion recording, SLOOP 2.4's step extras, per-step chance; docs/UI-OPTIMIST-
 * DESIGN.md section 6), included by seq.c. One list of events per track (auto.h): what motion recording (motion.c)
 * and the locks, nudges and fill conditions (seq24.c) kept in two stores before (motion.c's 64 shared events,
 * stepx.h's 176 B a track), and chance on drum and synth steps.
 *
 *   auto_w          the working project's lists and motion's PLAY bits: what the sequencer plays; the storage
 *                   (storage/auto_proj.c) captures and applies it with the project buffers
 *   auto_step       the sequencer enters a step (seq_tick, the audio ISR, once a step): one scan of the track's list.
 *                   Its hold events apply in list order (motion.c motion_hold), its step-only events are gathered:
 *                   the fill condition (FELUCCA_FILLS: a step that fails it plays as a REST, its step-only events
 *                   skipped, its hold events applied: a recorded sweep goes on through a fill), the chance
 *                   (FELUCCA_CHANCE; an event wins over a synth step's chance bits, chance.c), the locks (FELUCCA_PLOCK,
 *                   seq24.c lock_apply: each saves the base it found, which is whatever a hold event left, and lets go
 *                   at the next step without one). Order kept: automation, then fill, then macros (EN_STEP)
 *   auto_nudge      FELUCCA_MICRO: each step's nudge, from the list, for micro_units (read a block, ahead of the step):
 *                   rebuilt whenever a list changes (auto_touch), never per sample
 * The main loop changes a list with the interrupts off (as the parameter batches); auto_touch after each change. */
#include "auto.h"

_Static_assert(NTRK == AUTO_NTRK && NSTEP == SX_NSTEP, "the automation store: four tracks of 64 steps");
typedef struct {
    uint32_t psum;                       /* the project the store belongs to (storage/auto_proj.c; the working one: 0) */
    uint8_t on, rsv[3];                  /* bit k: track k plays its hold events (motion's PLAY) */
    auto_list_t l[NTRK];
} auto_store_t;
static auto_store_t auto_w;              /* the working project's */
#define AUTO_L(k) (&auto_w.l[(uint32_t)(k) % NTRK])
#define AL(t) AUTO_L(trk_index(t))

/* Motion's old form, kept as a stored form (motion_flash.c's MOTN record, sec_codec.c's SEC_MOT chunk, a V1 pattern
 * record's chunk): at most 64 hold events for the four tracks together, place = track << 6 | step */
#define MOTION_MAX 64u
typedef auto_ev_t motion_ev_t;
typedef struct {
    uint32_t psum;
    uint8_t count, on, rsv[2];
    motion_ev_t ev[MOTION_MAX];
} motion_store_t;
_Static_assert(sizeof(motion_store_t) == 200u, "motion store layout");

/* The ids an event stores do not depend on the build: P_LEVEL .. P_E7 are the same in every build of a project format,
 * and after P_E7 the layout of a build with every value there: SLOOP 2.4's FILT STRUM VLEAD (SL24_TP: P_TFLT P_STRUM
 * P_VLEAD), then the COMP insert's amount (MOT_TCOMP). A build without 2.4's values numbers P_TCOMP P_ENG_END itself,
 * the id 2.4's FILT has elsewhere: without the map a FILT event from a TRK_FILT build played as COMP here. An event for
 * a value after P_E7 that this build lacks is kept and not played: it plays again in a build that has the value. The
 * pseudo-parameters come after every stored id (auto.h) */
#define MOT_TAIL_END (P_ENG_END + 4u)                        /* (stored: P_ENG_END FILT, +1 STRUM, +2 VLEAD, +3 COMP) */
_Static_assert(MOT_TAIL_END < AUTO_NUDGE, "the pseudo-parameters: past every stored id");
#if FELUCCA_MASTER_COMP && !SL24_TP
static uint32_t mot_id(uint32_t s)                           /* stored -> this build's (P_COUNT: none here) */
{
    return s == P_ENG_END + 3u ? (uint32_t)P_TCOMP : s >= P_ENG_END ? (uint32_t)P_COUNT : s;
}
#define mot_sid(id) ((id) == P_TCOMP ? P_ENG_END + 3u : (id))   /* this build's -> stored */
#else
#if FELUCCA_MASTER_COMP
_Static_assert(P_TCOMP == P_ENG_END + 3u, "motion: the stored ids are this build's own");
#endif
#if SL24_TP
_Static_assert(P_TFLT == P_ENG_END, "motion: the stored ids are this build's own");
#endif
static uint32_t mot_id(uint32_t s) { return s < P_COUNT ? s : (uint32_t)P_COUNT; }   /* (a pseudo-parameter: none) */
#define mot_sid(id) (id)
#endif

#if FELUCCA_MICRO
static int8_t auto_nudge[NTRK][NSTEP];   /* each step's nudge (the ISR: micro_units) */
#endif
/* track k's list changed (the IRQ off, or the ISR): the nudges again */
static void auto_touch(uint32_t k)
{
#if FELUCCA_MICRO
    const auto_list_t *l = AUTO_L(k);
    uint32_t i;
    memset(auto_nudge[k % NTRK], 0, sizeof auto_nudge[0]);
    for (i = 0; i < l->n; i++)
        if (l->ev[i].param == AUTO_NUDGE && (l->ev[i].place & AUTO_ONLY))
            auto_nudge[k % NTRK][l->ev[i].place & AUTO_STEP] = l->ev[i].value;
#else
    (void)k;
#endif
}
static void auto_touch_all(void)
{
    uint32_t k;
    for (k = 0; k < NTRK; k++)
        auto_touch(k);
}

/* the old motion form <-> the lists. A list's hold events (track order) into m: 1 all fitted */
static int auto_to_motion(motion_store_t *m, const auto_list_t *l, uint32_t on)
{
    uint32_t k, i, all = 1;
    m->count = 0;
    m->on = (uint8_t)on;
    m->rsv[0] = m->rsv[1] = 0;
    for (k = 0; k < NTRK; k++)
        for (i = 0; i < l[k].n; i++) {
            const auto_ev_t *e = &l[k].ev[i];
            if (e->place & AUTO_ONLY)
                continue;
            if (m->count >= MOTION_MAX) {
                all = 0;
                continue;
            }
            m->ev[m->count].place = (uint8_t)(k << 6 | (e->place & AUTO_STEP));
            m->ev[m->count].param = e->param;
            m->ev[m->count++].value = e->value;
        }
    return (int)all;
}
/* m's events added to the lists as hold events (a list full: the rest cut, 0) */
static int auto_from_motion(auto_list_t *l, const motion_store_t *m)
{
    uint32_t i, all = 1;
    for (i = 0; i < m->count && i < MOTION_MAX; i++) {
        const motion_ev_t *e = &m->ev[i];
        if (auto_put(&l[(e->place >> 6) % NTRK], e->place & AUTO_STEP, e->param, e->value) < 0)
            all = 0;
    }
    return (int)all;
}

#if FELUCCA_MOTION
#include "motion.c"            /* hold events: knob moves recorded per step (from Felucca 1.0) */
#endif
#if SL24_STEPX
#include "seq24.c"             /* SLOOP 2.4: micro timing, fills, parameter locks (backports24seq.h) */
#endif
#if FELUCCA_CHANCE
static uint8_t auto_ch[NTRK];            /* the step playing's chance event, 100: none (chance.c, seq_tick) */
#endif

/* the sequencer enters step idx of track t (seq_tick, the ISR): its events. -> 1 the step plays, 0 its fill
 * condition fails (it plays as a REST) */
static uint32_t auto_step(track_t *t, uint32_t idx)
{
    uint32_t k = trk_index(t) % NTRK, i, plays = 1, fill = FC_NORM;
    const auto_list_t *l = AUTO_L(k);
#if FELUCCA_PLOCK
    uint8_t lp[NLOCK];
    int8_t lv[NLOCK];
    uint32_t m = 0;
#endif
#if FELUCCA_MOTION
    uint32_t mo = (auto_w.on >> k) & 1u;
    if (mo)
        motion_pass(t, idx);                         /* (its base; step 0: each pass from the patch) */
#endif
#if FELUCCA_CHANCE
    auto_ch[k] = 100;
#endif
    for (i = 0; i < l->n && i < AUTO_MAX; i++) {
        const auto_ev_t *e = &l->ev[i];
        if ((e->place & AUTO_STEP) != idx)
            continue;
        if (!(e->place & AUTO_ONLY)) {
#if FELUCCA_MOTION
            if (mo)
                motion_hold(t, k, e);
#endif
            continue;
        }
        if (e->param == AUTO_FILL)
            fill = (uint8_t)e->value;
#if FELUCCA_CHANCE
        else if (e->param == AUTO_CHANCE)
            auto_ch[k] = (uint8_t)e->value;
#endif
#if FELUCCA_PLOCK
        else if (!AUTO_PSEUDO(e->param) && m < NLOCK && lock_ok(t, mot_id(e->param)))
            lp[m] = (uint8_t)mot_id(e->param), lv[m++] = e->value;
#endif
    }
#if FELUCCA_FILLS
    plays = fill == FC_FILL ? fill_now : fill == FC_NOFILL ? !fill_now : 1u;
#else
    (void)fill;
#endif
#if FELUCCA_PLOCK
    lock_apply(t, lp, lv, plays ? m : 0u);           /* (a fill-skipped step: its locks do not apply) */
#endif
    return plays;
}

/* ---- the edits (the UIs, the editor; the main loop with the IRQ off, unless said) */
/* step idx's nudge, fill condition, chance (100: none) */
static int32_t step_micro(const track_t *t, uint32_t idx) { return auto_pseudo(AL(t), idx % NSTEP, AUTO_NUDGE, 0); }
static uint32_t step_fill(const track_t *t, uint32_t idx) { return (uint32_t)auto_pseudo(AL(t), idx % NSTEP, AUTO_FILL, FC_NORM); }
static uint32_t step_chance_ev(const track_t *t, uint32_t idx) { return (uint32_t)auto_pseudo(AL(t), idx % NSTEP, AUTO_CHANCE, 100); }
/* ... set (clamped; the default: no event); 0 the list is full */
static int step_micro_set(track_t *t, uint32_t idx, int32_t v)
{
    int ok = auto_pseudo_set(AL(t), idx % NSTEP, AUTO_NUDGE, clamp(v, MICRO_MIN, MICRO_MAX));
    auto_touch(trk_index(t));
    return ok;
}
static int step_fill_set(track_t *t, uint32_t idx, uint32_t v)
{
    return auto_pseudo_set(AL(t), idx % NSTEP, AUTO_FILL, v > FC_NOFILL ? FC_NORM : (int32_t)v);
}
static int step_chance_set(track_t *t, uint32_t idx, uint32_t pct) { return auto_pseudo_set(AL(t), idx % NSTEP, AUTO_CHANCE, pct >= 100u ? 100 : (int32_t)pct); }
/* the step-only event of (step idx, our id): 1 there is one, its value into *v */
static int lock_get(const track_t *t, uint32_t idx, uint32_t id, int32_t *v)
{
    return id < P_COUNT && auto_get(AL(t), (idx % NSTEP) | AUTO_ONLY, mot_sid(id), v);
}
static void lock_drop(track_t *t, uint32_t idx, uint32_t id)
{
    int i = id < P_COUNT ? auto_find(AL(t), (idx % NSTEP) | AUTO_ONLY, mot_sid(id)) : -1;
    if (i >= 0)
        auto_del(AL(t), (uint32_t)i);
}
/* the hold event of (step idx, our id) goes; 1 there was one */
static int auto_step_hold_drop(track_t *t, uint32_t idx, uint32_t id)
{
    int i = id < P_COUNT ? auto_find(AL(t), idx % NSTEP, mot_sid(id)) : -1;
    if (i >= 0)
        auto_del(AL(t), (uint32_t)i);
    return i >= 0;
}
/* the hold event of (step idx, our id) */
static int hold_get(const track_t *t, uint32_t idx, uint32_t id, int32_t *v)
{
    return id < P_COUNT && auto_get(AL(t), idx % NSTEP, mot_sid(id), v);
}
/* step idx's events of the kinds which (AUTO_HOLDS, AUTO_ONLYS) go; -> how many */
static uint32_t auto_step_clear(track_t *t, uint32_t idx, uint32_t which)
{
    uint32_t n = auto_drop_step(AL(t), idx % NSTEP, which);
    auto_touch(trk_index(t));
    return n;
}
/* what step idx of track t holds, as marks to draw (SLOOP's SEQ tiles, the DRUMS grid's cells): AUTO_MK_ONLY any event
 * but a fill: a nudge, a lock, a chance or a motion (hold) event (the user treats them as one: "motion is a plock", so
 * one dot); AUTO_MK_FILL / AUTO_MK_NOFILL its fill condition. Main loop, one pass of the list */
#define AUTO_MK_ONLY 1u
#define AUTO_MK_FILL 4u
#define AUTO_MK_NOFILL 8u
static uint32_t auto_mark_of(const auto_ev_t *e)
{
    if (!(e->place & AUTO_ONLY) || e->param != AUTO_FILL)
        return AUTO_MK_ONLY;
    return e->value == FC_FILL ? AUTO_MK_FILL : AUTO_MK_NOFILL;
}
static uint32_t auto_step_marks(const track_t *t, uint32_t idx)
{
    const auto_list_t *l = AL(t);
    uint32_t i, m = 0, s = idx % NSTEP;
    for (i = 0; i < l->n && i < AUTO_MAX; i++)
        if ((l->ev[i].place & AUTO_STEP) == s)
            m |= auto_mark_of(&l->ev[i]);
    return m;
}
/* the marks of every step at once (the DRUMS grid draws 16 and hashes all: one pass, not NSTEP) */
static void auto_marks_all(const track_t *t, uint8_t *m)
{
    const auto_list_t *l = AL(t);
    uint32_t i;
    for (i = 0; i < NSTEP; i++)
        m[i] = 0;
    for (i = 0; i < l->n && i < AUTO_MAX; i++)
        m[l->ev[i].place & AUTO_STEP] |= (uint8_t)auto_mark_of(&l->ev[i]);
}
/* every step-only event of track t gone (a cleared pattern: seq.c steps_clear) */
static void auto_only_clear(track_t *t)
{
    (void)auto_drop_kind(AL(t), AUTO_ONLYS);
    auto_touch(trk_index(t));
}
/* the event of (step idx, our id) changes kind (the Optimist UI's YES with a step held and a cell hot): a step-only
 * event becomes a hold event (FELUCCA_MOTION, a recordable value; its PLAY bit on), a hold event a step-only one
 * (FELUCCA_PLOCK, a lockable value); one of each there: the step-only one goes, the hold one takes its value. -> 1
 * now a hold, 2 now step-only, 0 none there or the other kind not allowed for it */
static uint32_t auto_kind_toggle(track_t *t, uint32_t idx, uint32_t id)
{
    auto_list_t *l = AL(t);
    uint32_t s = idx % NSTEP, sid = id < P_COUNT ? mot_sid(id) : AUTO_NUDGE;
    int o = auto_find(l, s | AUTO_ONLY, sid), h = auto_find(l, s, sid);
    int32_t v;
    if (sid == AUTO_NUDGE)
        return 0;
    if (o >= 0) {
#if FELUCCA_MOTION
        if (!motion_param(t, id))
            return 0;
        v = l->ev[o].value;
        auto_del(l, (uint32_t)o);
        (void)auto_put(l, s, sid, v);                    /* (a slot freed above: never full) */
        auto_w.on |= (uint8_t)(1u << trk_index(t));
        return 1;
#else
        return 0;
#endif
    }
    if (h >= 0) {
#if FELUCCA_PLOCK
        if (!lock_ok(t, id))
            return 0;
        v = l->ev[h].value;
        auto_del(l, (uint32_t)h);
        (void)auto_put(l, s | AUTO_ONLY, sid, v);
        return 2;
#else
        return 0;
#endif
    }
    (void)v;
    return 0;
}
