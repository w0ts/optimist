/* SPDX-License-Identifier: GPL-3.0-only */
/* The automation's one parameter list and one base (FELUCCA_MOTION or FELUCCA_PLOCK; included by auto.c before
 * motion.c and seq24.c). 2026-10-15 (the user: "everything that can be p-locked can also be motion-recorded and vice
 * versa"; a change made while playing on an automated value is put back at STOP, for both):
 *
 *   auto_param      the values an event may set, hold (motion) and step-only (a lock) alike
 *   the base        motion_base / motion_active: the patch under the automation. The first time a hold event or a
 *                   lock changes a value (auto_take), the value found is kept as its base and the value marked; a
 *                   knob turned while not recording sets the base (auto_knob_base); STOP, a pass's step 0 (with the
 *                   track's motion playing), a load, a pattern switch put every marked value back (motion_restore).
 *                   A capture stores the base (auto.c auto_patch). Whatever else wrote a marked value meanwhile (a
 *                   preset, MIDI, the editor) goes back with it.
 *   the locks       lk_*: the locks in force, each with the value it lets go to at the next step without one (the
 *                   base, or what a hold event of the same value set; a hold applied under a lock updates it)
 * The ISR (auto_step) and the main loop with the interrupts off change them. */

/* the values an event may set: every sound, ARP, scale, chord and GATE value a stored id names. Not: the voice
 * allocation (VOICE GLMODE PRIO ALLOC, VOICES: the voices in flight), MUTE (the mixer's), the FX bypass, the pattern's
 * LEN DIV SWING (the sequencer's own grid), the LFO DEST slots and the old ENV DEST / LFO depth ids (but the drum
 * track's COLOR and SPACE macro positions, kept in two of them) (their ids are the stored depths of before: auto.c
 * lfo_leg_ev), a value with no stored id (LFO 2's: auto.c MOT_TAIL; GATER's PATTERN RATE SHAPE, JUNO's MODE and the shapers' amounts have
 * one since 2026-10-18) */
static int auto_param(const track_t *t, uint32_t id)
{
    if (is_drum(t) && (id == P_ED_FLT || id == P_ED_SHP))
        return 1;                                            /* (the drum track's: the macros' COLOR SPACE, params.c MAC_ID) */
    if (id >= P_COUNT || LEG_ID(id))
        return 0;
    switch (id) {
    case P_VOICE: case P_GLMODE: case P_PRIO: case P_ALLOC: case P_MUTE: case P_FXOFF: case P_SLEN: case P_SDIV: case P_SSWING:
        return 0;
    default:
        break;
    }
#if SL24_TP
    if (id == P_TFLT)
        return TFLT_ON;                                      /* (the FILTER insert: a value only when it is built) */
#endif
    return id < P_ENG_END || mot_id(mot_sid(id)) == id;      /* (past P_E7: one of the stored tail's, auto.c MOT_TAIL) */
}
#define motion_param auto_param                             /* (the names motion.c and seq24.c had) */
#define lock_ok auto_param
#define p_lockable(id) auto_param(&trk[0], (id))           /* (a synth track's list: tests/auto_int8_check.c) */

#define MOTION_WORDS ((P_COUNT + 31u) / 32u)
static int16_t motion_base[NTRK][P_COUNT] __attribute__((section(".pool")));   /* the patch under the automation */
static uint32_t motion_active[NTRK][MOTION_WORDS];   /* the values the automation changed (base kept) */
#define AB_ON(k, id) ((motion_active[k][(id) / 32u] >> ((id) % 32u)) & 1u)
#if FELUCCA_PLOCK
static uint8_t lk_n[NTRK], lk_param[NTRK][NLOCK];
static int16_t lk_ret[NTRK][NLOCK];                  /* the value each lets go to */
#endif

/* the automation is about to change value id of track t (track k): its base kept, the first time */
static void auto_take(const track_t *t, uint32_t k, uint32_t id)
{
    if (!AB_ON(k, id)) {
        motion_base[k][id] = t->p[id];
        motion_active[k][id / 32u] |= 1u << (id % 32u);
    }
}

/* every value the automation changed back to the patch, the locks let go (interrupts off or in the ISR) */
static void motion_restore(track_t *t)
{
    uint32_t k = trk_index(t) % NTRK, id;
    for (id = 0; id < P_COUNT; id++)
        if (AB_ON(k, id))
            t->p[id] = motion_base[k][id];
    for (id = 0; id < MOTION_WORDS; id++)
        motion_active[k][id] = 0;
#if FELUCCA_PLOCK
    lk_n[k] = 0;
#endif
#if DLA_ON
    if (is_drum(t)) {
        dla_holds_off();                                     /* (the lanes' motion and locks too: drum_edit.c dla) */
        dla_locks_off();
    }
#endif
#if SLOT_AUTO
    sa_restore(k);                                           /* (the insert slots' values: auto_slot.c) */
#endif
}
static void motion_restore_all(void)                         /* PLAY, STOP (seq.c, the ISR): every track */
{
    uint32_t k;
    for (k = 0; k < NTRK; k++)
        motion_restore(&trk[k]);
}
#define motion_begin motion_restore_all
#define motion_end motion_restore_all

/* a knob set value id of track t while not recording (the main loop): the new patch, kept by STOP; a lock in force on
 * it let go (it would put the old value back) */
static void auto_knob_base(track_t *t, uint32_t id, int32_t value)
{
    uint32_t k = trk_index(t) % NTRK;
    fm1_irq_off();
    if (id < P_COUNT && AB_ON(k, id)) {
        motion_base[k][id] = (int16_t)value;
        motion_active[k][id / 32u] &= ~(1u << (id % 32u));
    }
#if FELUCCA_PLOCK
    {
        uint32_t i, n = 0;
        for (i = 0; i < lk_n[k] && i < NLOCK; i++)
            if (lk_param[k][i] != id)
                lk_param[k][n] = lk_param[k][i], lk_ret[k][n++] = lk_ret[k][i];
        lk_n[k] = (uint8_t)n;
    }
#endif
    fm1_irq_on();
}
