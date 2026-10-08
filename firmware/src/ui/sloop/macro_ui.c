/* SPDX-License-Identifier: GPL-3.0-only */
/* GLO > MACRO's knobs (FELUCCA_MACROS, macro.c), the main loop. With motion recording (FELUCCA_MOTION) a macro
 * turned while any track records is an event of the drum track (where the positions are kept: MAC_ID) at the step
 * playing, the next one once past its half, as motion_knob does for the recording track; played back on that
 * track's steps (the sequencer writes the position, mac_pre reads it in the same block). Not recording: the knob
 * sets the position under the motion, as any knob. */
#if FELUCCA_MOTION
static void mac_motion(uint32_t id, int32_t value)
{
    track_t *t = TDRUM;
    uint32_t into, slen, idx;
    if (!song.playing || !song.rec) {
        motion_knob(t, id, value);                     /* (not recording: the base under the motion) */
        return;
    }
    idx = TRK_IDX(t, trk_grid(t, &into, &slen), trk_len(t));
    if (into > slen / 2u)
        idx = (idx + 1u) % trk_len(t);
    fm1_irq_off();
    if ((motion_base_valid >> TRK_DRUM) & 1u && !((motion_active[TRK_DRUM][id / 32u] >> (id % 32u)) & 1u)) {
        motion_base[TRK_DRUM][id] = t->p[id];          /* (STOP puts the position back, as motion_knob) */
        motion_active[TRK_DRUM][id / 32u] |= 1u << (id % 32u);
    }
    fm1_irq_on();
    (void)motion_set_event(t, idx, id, value);
}
#endif
