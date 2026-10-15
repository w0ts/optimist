/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments (Felucca 1.0: motion recording,
 * hugelton/Felucca 727f272, motion.c: the sparse store of 64 step events, the base / active bookkeeping, the
 * capture of a knob move while recording, motion_step) */
/* Motion recording (FELUCCA_MOTION, backports.h): knob moves recorded per step. While a track records (REC
 * armed, playing), turning a knob of a sound parameter on its pages (or HOME's macros) stores the value at the
 * step playing (the next one past its half); when the step comes round the value is set again. The loop starts
 * each pass from the patch (the "base"), and STOP puts the patch back: the automation never overwrites the
 * sound. A knob turned while not recording changes the base. SEQ > MOTION: PLAY on / off per track, the events
 * of the track, CLEAR (twice). Included by auto.c.
 *
 * The events are the hold events of the automation store (auto.h: place = the step, bit 6 clear; the parameter, its
 * value), at most AUTO_MAX a track with the track's step-only events (Felucca: 64 for the four tracks, 4 bytes an
 * event). The values auto_base.c auto_param names, the locks' list too (2026-10-15: one list): every one fits a signed
 * byte (tests/auto_int8_check.c); the base under them is auto_base.c's, shared with the locks. The
 * store travels with the project but outside it (storage/auto_proj.c); auto_step (the ISR) applies a step's hold
 * events, the main loop writes them with the interrupts off (as the parameter batches). */
static uint8_t motion_full;

/* a stored event's id that a store may hold: one recordable here, or a depth of before (P_LEG_*: played onto the LFO
 * slot on that destination, lfo_leg_ev; LFO DEST's slots are not recorded yet) */
#define MOTION_STORED_OK(t, id) (motion_param((t), (id)) || (!is_drum(t) && LEG_ID(id)))
/* an old motion store (the MOTN record) as it may be used: counts in range, every event recordable, no duplicate */
static int motion_valid(const motion_store_t *m)
{
    uint32_t i, j;
    if (m->count > MOTION_MAX || (m->on & ~((1u << NTRK) - 1u)))
        return 0;
    for (i = 0; i < m->count; i++) {
        const motion_ev_t *e = &m->ev[i];
        if ((e->place >> 6) >= NTRK || (!MOTION_STORED_OK(&trk[e->place >> 6], mot_id(e->param)) &&
                                         (e->param < P_ENG_END || e->param >= MOT_TAIL_END)))
            return 0;                                        /* (a value after P_E7 not here: kept, not played) */
        for (j = 0; j < i; j++)
            if (m->ev[j].place == e->place && m->ev[j].param == e->param)
                return 0;
    }
    return 1;
}

#if FELUCCA_MOTION_MARK
/* #63 (after Felucca 1.0.2 motion.c motion_mask, hugelton/Felucca db70550, by Leo Kuroshita, GPL-3.0-only): does
 * track k's MOTION change parameter id (PLAY on, a hold event for it)? The page's cards mark it (ui_draw.c). Main
 * loop only, at most AUTO_MAX events, per card drawn */
static int motion_drives(uint32_t k, uint32_t id)
{
    const auto_list_t *l = AUTO_L(k);
    uint32_t i;
    if (k >= NTRK || !((auto_w.on >> k) & 1u))
        return 0;
    for (i = 0; i < l->n; i++)
        if (!(l->ev[i].place & AUTO_ONLY) && mot_id(l->ev[i].param) == id)
            return 1;
    return 0;
}
#endif
static uint32_t motion_count(const track_t *t) { return auto_count(AL(t), AUTO_HOLDS); }

/* a new step of track t (its motion plays) is about to play (auto_step, the ISR): step 0, each pass from the patch */
static void motion_pass(track_t *t, uint32_t step)
{
    if (!step)
        motion_restore(t);                                   /* (auto_base.c: the locks let go too) */
}
/* hold event e of track t's step (track k) applies (auto_step, the ISR) */
static void motion_put(track_t *t, uint32_t k, uint32_t id, int32_t val);
static void motion_hold(track_t *t, uint32_t k, const auto_ev_t *e)
{
    uint32_t id = mot_id(e->param), did, taken = 0;
    int32_t val = e->value;
    if (!is_drum(t) && LEG_ID(id)) {
        uint32_t dest;
        if ((id = lfo_leg_ev(t, id, &val, &taken, &did, &dest)) >= P_COUNT)
            return;                                          /* (a depth of before: no slot free for it) */
        if (did < P_COUNT)
            motion_put(t, k, did, (int32_t)dest);            /* (a free slot put on its destination) */
    } else if (!motion_param(t, id)) {
        return;                                              /* (a value this build lacks) */
    }
    motion_put(t, k, id, val);
}
/* track t's (track k) value id held at val by its motion (the base kept; a lock in force on it lets go to val) */
static void motion_put(track_t *t, uint32_t k, uint32_t id, int32_t val)
{
    const param_desc_t *d = track_desc(t, id);
    auto_take(t, k, id);
    t->p[id] = (int16_t)clamp(val, d->min, d->max);
#if FELUCCA_PLOCK
    {
        uint32_t i;
        for (i = 0; i < lk_n[k] && i < NLOCK; i++)
            if (lk_param[k][i] == id)
                lk_ret[k][i] = t->p[id];
    }
#endif
}

/* track t's motion on / off (the main loop) */
static void motion_set_enabled(track_t *t, uint32_t on)
{
    uint32_t b = 1u << trk_index(t);
    fm1_irq_off();
    auto_w.on = (uint8_t)(on ? auto_w.on | b : auto_w.on & ~b);
    if (!on)
        motion_restore(t);
    fm1_irq_on();
}

/* every hold event of track t gone, its patch back (the main loop) */
static void motion_clear(track_t *t)
{
    fm1_irq_off();
    motion_restore(t);
    (void)auto_drop_kind(AL(t), AUTO_HOLDS);
    auto_w.on &= (uint8_t)~(1u << trk_index(t));
    motion_full = 0;
    fm1_irq_on();
}

/* a hold event set (a new one or its value): 0 ok, 1 not recordable, 2 the list is full */
static int motion_set_event(track_t *t, uint32_t step, uint32_t id, int32_t value)
{
    uint32_t k = trk_index(t), rc = 0;
    if (k >= NTRK || step >= NSTEP || !motion_param(t, id) || value < -128 || value > 127)
        return 1;
    fm1_irq_off();
    if (auto_put(AUTO_L(k), step, mot_sid(id), value) < 0) {
        motion_full = 1;
        rc = 2;
    } else {
        auto_w.on |= (uint8_t)(1u << k);
    }
    fm1_irq_on();
    return (int)rc;
}

static void undo_mark(const track_t *t, uint32_t sess);   /* undo.c */
/* a knob set parameter id of track t to value (ui_input.c, the main loop): recording, an event at the step
 * playing (the next one once past its half), undone with the recording pass (undo.c UNDO_REC); else the patch's
 * new value */
static void motion_knob(track_t *t, uint32_t id, int32_t value)
{
    uint32_t k = trk_index(t), into, slen, abs, idx;
    if (!motion_param(t, id))
        return;
    if (!song.playing || !((song.rec >> k) & 1u)) {
        auto_knob_base(t, id, value);                        /* (a live edit: the new base) */
        return;
    }
    abs = trk_grid(t, &into, &slen);
    idx = TRK_IDX(t, abs, trk_len(t));
    if (into > slen / 2u)
        idx = (idx + 1u) % trk_len(t);
    fm1_irq_off();
    auto_take(t, k, id);                                     /* (the knob's own value is transient too: STOP puts the patch back) */
    fm1_irq_on();
    undo_mark(t, (t->pass << 2) | 1u);                       /* (undo.c UNDO_REC: the list is in the pass's level) */
    (void)motion_set_event(t, idx, id, value);
}
