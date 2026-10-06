/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments (Felucca 1.0: motion recording,
 * hugelton/Felucca 727f272, motion.c: the sparse store of 64 step events, the base / active bookkeeping, the
 * capture of a knob move while recording, motion_step) */
/* Motion recording (FELUCCA_MOTION, backports.h): knob moves recorded per step. While a track records (REC
 * armed, playing), turning a knob of a sound parameter on its pages (or HOME's macros) stores the value at the
 * step playing (the next one past its half); when the step comes round the value is set again. The loop starts
 * each pass from the patch (the "base"), and STOP puts the patch back: the automation never overwrites the
 * sound. A knob turned while not recording changes the base. SEQ > MOTION: PLAY on / off per track, the events
 * of the track, CLEAR (twice). Included by seq.c (motion_step before each step plays).
 *
 * As Felucca: at most 64 events for the four tracks together (place = track << 6 | step, the parameter, its
 * value), only continuous sound parameters (envelopes, filter / pitch / LFO amounts, sends, pan, glide, the
 * engine's EDIT values; never the drum track's kit). Here an event is 3 bytes (Felucca: 4): every recordable
 * value fits a signed byte. The store travels with the project but outside it (project format FUN8 is full):
 * motion_proj.c keeps one per project buffer (the 4 slots, the autosave, the song's backup), motion_flash.c
 * writes it next to the project in the same flash sector. The ISR scans the 64 records at a step; the main loop
 * writes them with the interrupts off (as the parameter batches). */
#define MOTION_MAX 64u
typedef struct { uint8_t place, param; int8_t value; } motion_ev_t;
typedef struct {
    uint32_t psum;                       /* the project's sum it belongs to (motion_proj.c; the working one: 0) */
    uint8_t count, on, rsv[2];           /* events in use; bit k: track k plays its motion */
    motion_ev_t ev[MOTION_MAX];
} motion_store_t;
_Static_assert(sizeof(motion_store_t) == 200u, "motion store layout");
#define MOTION_WORDS ((P_COUNT + 31u) / 32u)
static motion_store_t motion;            /* the working project's */
static int16_t motion_base[NTRK][P_COUNT] __attribute__((section(".pool")));   /* the patch under the motion */
static uint32_t motion_active[NTRK][MOTION_WORDS];   /* the parameters the motion set (base saved) */
static uint8_t motion_base_valid, motion_full;

/* the parameters a motion may set (on track t) */
static int motion_param(const track_t *t, uint32_t id)
{
    if (id >= P_COUNT)
        return 0;
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

/* a store as it may be used: counts in range, every event recordable, no duplicate */
static int motion_valid(const motion_store_t *m)
{
    uint32_t i, j;
    if (m->count > MOTION_MAX || (m->on & ~((1u << NTRK) - 1u)))
        return 0;
    for (i = 0; i < m->count; i++) {
        const motion_ev_t *e = &m->ev[i];
        if ((e->place >> 6) >= NTRK || !motion_param(&trk[e->place >> 6], e->param))
            return 0;
        for (j = 0; j < i; j++)
            if (m->ev[j].place == e->place && m->ev[j].param == e->param)
                return 0;
    }
    return 1;
}

static uint32_t motion_count(const track_t *t)
{
    uint32_t i, n = 0, k = trk_index(t);
    for (i = 0; i < motion.count; i++)
        n += (motion.ev[i].place >> 6) == k;
    return n;
}

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

/* a new step of track t is about to play (seq.c seq_tick, the ISR) */
static void motion_step(track_t *t, uint32_t step)
{
    uint32_t k = trk_index(t), i;
    if (!((motion.on >> k) & 1u))
        return;
    if (!((motion_base_valid >> k) & 1u)) {
        memcpy(motion_base[k], t->p, sizeof t->p);
        motion_base_valid |= (uint8_t)(1u << k);
    }
    if (!step)
        motion_restore(t);                                   /* each pass from the patch */
    for (i = 0; i < motion.count; i++) {
        const motion_ev_t *e = &motion.ev[i];
        const param_desc_t *d;
        if (e->place != (k << 6 | step))
            continue;
        d = track_desc(t, e->param);
        if (!((motion_active[k][e->param / 32u] >> (e->param % 32u)) & 1u))
            motion_base[k][e->param] = t->p[e->param];       /* (its base: the patch as it is now) */
        t->p[e->param] = (int16_t)clamp(e->value, d->min, d->max);
        motion_active[k][e->param / 32u] |= 1u << (e->param % 32u);
    }
}

/* track t's motion on / off (the main loop) */
static void motion_set_enabled(track_t *t, uint32_t on)
{
    uint32_t b = 1u << trk_index(t);
    fm1_irq_off();
    motion.on = (uint8_t)(on ? motion.on | b : motion.on & ~b);
    if (!on)
        motion_restore(t);
    fm1_irq_on();
}

/* every event of track t gone, its patch back (the main loop) */
static void motion_clear(track_t *t)
{
    uint32_t k = trk_index(t), i, n = 0;
    fm1_irq_off();
    motion_restore(t);
    for (i = 0; i < motion.count; i++)
        if ((motion.ev[i].place >> 6) != k)
            motion.ev[n++] = motion.ev[i];
    motion.count = (uint8_t)n;
    motion.on &= (uint8_t)~(1u << k);
    motion_full = 0;
    fm1_irq_on();
}

/* an event set (a new one or its value): 0 ok, 1 not recordable, 2 the store is full */
static int motion_set_event(track_t *t, uint32_t step, uint32_t id, int32_t value)
{
    uint32_t k = trk_index(t), i, rc = 0;
    if (k >= NTRK || step >= NSTEP || !motion_param(t, id) || value < -128 || value > 127)
        return 1;
    fm1_irq_off();
    for (i = 0; i < motion.count; i++)
        if (motion.ev[i].place == (k << 6 | step) && motion.ev[i].param == id)
            break;
    if (i == MOTION_MAX) {
        motion_full = 1;
        rc = 2;
    } else {
        motion.ev[i].place = (uint8_t)(k << 6 | step);
        motion.ev[i].param = (uint8_t)id;
        motion.ev[i].value = (int8_t)value;
        if (i == motion.count)
            motion.count++;
        motion.on |= (uint8_t)(1u << k);
    }
    fm1_irq_on();
    return (int)rc;
}

/* a knob set parameter id of track t to value (ui_input.c, the main loop): recording, an event at the step
 * playing (the next one once past its half); else the patch's new value */
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
    idx = abs % trk_len(t);
    if (into > slen / 2u)
        idx = (idx + 1u) % trk_len(t);
    fm1_irq_off();
    if ((motion_base_valid >> k) & 1u && !((motion_active[k][id / 32u] >> (id % 32u)) & 1u)) {
        motion_base[k][id] = t->p[id];                       /* (the knob's own value is transient too: */
        motion_active[k][id / 32u] |= 1u << (id % 32u);      /* STOP puts the patch back) */
    }
    fm1_irq_on();
    (void)motion_set_event(t, idx, id, value);
}
