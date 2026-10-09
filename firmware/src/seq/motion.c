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
 * event). Only continuous sound parameters (envelopes, filter / pitch / LFO amounts, sends, pan, glide, the engine's
 * EDIT values; never the drum track's kit): every recordable value fits a signed byte (tests/auto_int8_check.c). The
 * store travels with the project but outside it (storage/auto_proj.c); auto_step (the ISR) applies a step's hold
 * events, the main loop writes them with the interrupts off (as the parameter batches). */
#define MOTION_WORDS ((P_COUNT + 31u) / 32u)
static int16_t motion_base[NTRK][P_COUNT] __attribute__((section(".pool")));   /* the patch under the motion */
static uint32_t motion_active[NTRK][MOTION_WORDS];   /* the parameters the motion set (base saved) */
static uint8_t motion_base_valid, motion_full;

/* the parameters a motion may set (on track t) */
static int motion_param(const track_t *t, uint32_t id)
{
    if (id >= P_COUNT)
        return 0;
#if FELUCCA_MASTER_COMP
    if (id == P_TCOMP)
        return 1;                                            /* (the COMP insert's amount: fx_slots.c; the drum
                                                              * track's: the drum bus's, fx.c dbus_run) */
#endif
#if SL24_TP
    if (id > P_E7)
        return FELUCCA_TRK_FILT && id == P_TFLT;             /* (SLOOP 2.4's FILT, on every track: a sweep) */
#endif
    if (id >= P_E0)
        return !is_drum(t);                                  /* (the drum track's P_E0 is its kit) */
    return id <= P_REL || (id >= P_ED_FLT && id <= P_ED_SHP) || (id >= P_LRATE && id <= P_LD_AMP && id != P_LWAVE) ||
           (id >= P_DIST && id <= P_REV) || id == P_GLIDE || id == P_PAN || id == P_DETUNE || id == P_SLDEPTH
#if FELUCCA_ANALOG2
           || id == P_A2SEMI || id == P_A2DRFT || id == P_A2FATK || id == P_A2FDEC || id == P_A2FENV ||
           id == P_A2SDTN || (id >= P_A2EPIT && id <= P_A2ESDT)   /* (ENV2 DEST's amounts) */
#endif
        ;
}

/* an old motion store (the MOTN record) as it may be used: counts in range, every event recordable, no duplicate */
static int motion_valid(const motion_store_t *m)
{
    uint32_t i, j;
    if (m->count > MOTION_MAX || (m->on & ~((1u << NTRK) - 1u)))
        return 0;
    for (i = 0; i < m->count; i++) {
        const motion_ev_t *e = &m->ev[i];
        if ((e->place >> 6) >= NTRK || (!motion_param(&trk[e->place >> 6], mot_id(e->param)) &&
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

/* the patch's value of parameter id (the base while the motion holds it) */
static int16_t motion_base_value(const track_t *t, uint32_t id)
{
    uint32_t k = trk_index(t);
    return (motion_active[k][id / 32u] >> (id % 32u)) & 1u ? motion_base[k][id] : t->p[id];
}

/* the motion's values back to the patch (interrupts off or in the ISR) */
static void motion_restore(track_t *t)
{
    uint32_t k = trk_index(t), id;
    for (id = 0; id < P_COUNT; id++)
        if ((motion_active[k][id / 32u] >> (id % 32u)) & 1u)
            t->p[id] = motion_base[k][id];
    for (id = 0; id < MOTION_WORDS; id++)
        motion_active[k][id] = 0;
}

/* PLAY (seq.c seq_reset_tracks, the ISR): every track from its patch */
static void motion_begin(void)
{
    uint32_t k;
    for (k = 0; k < NTRK; k++) {
        motion_restore(&trk[k]);
        memcpy(motion_base[k], trk[k].p, sizeof trk[k].p);
    }
    motion_base_valid = (1u << NTRK) - 1u;
}

/* STOP (seq.c seq_stop, the ISR): the patches back */
static void motion_end(void)
{
    uint32_t k;
    for (k = 0; k < NTRK; k++)
        motion_restore(&trk[k]);
    motion_base_valid = 0;
}

#if FELUCCA_PLOCK
static int16_t lock_base_of(const track_t *t, uint32_t id, int16_t v);   /* seq24.c */
#endif
/* a new step of track t (its motion plays) is about to play (auto_step, the ISR): its base; step 0, the patch */
static void motion_pass(track_t *t, uint32_t step)
{
    uint32_t k = trk_index(t);
    if (!((motion_base_valid >> k) & 1u)) {
        memcpy(motion_base[k], t->p, sizeof t->p);
        motion_base_valid |= (uint8_t)(1u << k);
    }
    if (!step)
        motion_restore(t);                                   /* each pass from the patch */
}
/* hold event e of track t's step (track k) applies (auto_step, the ISR) */
static void motion_hold(track_t *t, uint32_t k, const auto_ev_t *e)
{
    const param_desc_t *d;
    uint32_t id = mot_id(e->param);
    if (!motion_param(t, id))
        return;                                              /* (a value this build lacks) */
    d = track_desc(t, id);
    if (!((motion_active[k][id / 32u] >> (id % 32u)) & 1u))
#if FELUCCA_PLOCK
        motion_base[k][id] = lock_base_of(t, id, t->p[id]);  /* (its base: the patch as it is now, under a lock) */
#else
        motion_base[k][id] = t->p[id];                       /* (its base: the patch as it is now) */
#endif
    t->p[id] = (int16_t)clamp(e->value, d->min, d->max);
    motion_active[k][id / 32u] |= 1u << (id % 32u);
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
        fm1_irq_off();
        if ((motion_base_valid >> k) & 1u) {
            motion_base[k][id] = (int16_t)value;             /* (a live edit: the new base) */
            motion_active[k][id / 32u] &= ~(1u << (id % 32u));
        }
        fm1_irq_on();
        return;
    }
    abs = trk_grid(t, &into, &slen);
    idx = TRK_IDX(t, abs, trk_len(t));
    if (into > slen / 2u)
        idx = (idx + 1u) % trk_len(t);
    fm1_irq_off();
    if ((motion_base_valid >> k) & 1u && !((motion_active[k][id / 32u] >> (id % 32u)) & 1u)) {
        motion_base[k][id] = t->p[id];                       /* (the knob's own value is transient too: */
        motion_active[k][id / 32u] |= 1u << (id % 32u);      /* STOP puts the patch back) */
    }
    fm1_irq_on();
    undo_mark(t, (t->pass << 2) | 1u);                       /* (undo.c UNDO_REC: the list is in the pass's level) */
    (void)motion_set_event(t, idx, id, value);
}
