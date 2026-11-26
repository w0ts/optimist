/* SPDX-License-Identifier: GPL-3.0-only */
/* What a Launchpad press asks of the Digitone (and of the transport), as data: the firmware turns each act into a live-control request (usb_lp.c). Shared by the
 * Note mode (jank_lpapp.c) and the step modes (jank_lpstep.c). */
#ifndef JANK_LPACT_H
#define JANK_LPACT_H
#include <stddef.h>
#include <stdint.h>

enum jlapp_act_kind {
    JLAPP_ACT_NONE,
    JLAPP_ACT_NOTE_ON,                 /* track, note, vel */
    JLAPP_ACT_NOTE_OFF,                /* track, note */
    JLAPP_ACT_TRACK_ON,                /* a track pad: the track's own default note */
    JLAPP_ACT_TRACK_OFF,
    JLAPP_ACT_SELECT,                  /* the Digitone's active track becomes `track` */
    JLAPP_ACT_STEP,                    /* a step edit: track, step (0..127), op (enum jdl_step_op: 0 clear, 1 trig, 2 note), note, vel (255: the track's), len (255) */
    JLAPP_ACT_TRANSPORT,               /* start / stop the clock (Jank is the transport master) */
    JLAPP_ACT_MIX,                     /* a mixer fader: track (0..15), op = the parameter (enum jdl_mix_param), vel = the value 0..127 (MIXER SET) */
    JLAPP_ACT_MUTE,                    /* a mute / solo column: op = which (0 mute, 1 solo), the whole new mask = note (low byte) | step << 8 (MUTE SET) */
    JLAPP_ACT_RECORD,                  /* the Record button: vel = 1 arm / 0 off (RECORD, 58: the Digitone's own live record) */
    JLAPP_ACT_ERASE,                 /* the Delete button: track, vel = 1 on / 0 off: the Digitone erases that track's steps as its playhead passes (LIVE ERASE, 44 sub 3) */
    JLAPP_ACT_SCALE,                   /* the Launchpad changed the scale or the key: note = the scale's index (the unit's: jank_lpscales.h; the Digitone's 0..35), vel = the root (0..11): the Digitone follows (SCALE SET) */
    JLAPP_ACT_PATTERN,                 /* a Session page pad: note = the pattern (0..127), vel = how (enum jdl_pattern_how; a press cues: 0) (PATTERN, 60, provisional) */
    JLAPP_ACT_DN2_TRANSPORT,           /* start / stop while the Digitone leads: vel = the op (enum jdl_transport_op) (TRANSPORT, 61, provisional); no clock, no Start byte */
    JLAPP_ACT_CHAIN,                   /* the Session page's chain: vel = JDL_CHAIN_SET (the list: jlapp_chain_list) or JDL_CHAIN_CLEAR (CHAIN, 59, provisional) */
    JLAPP_ACT_CLIP,                    /* the clips page (JLAPP_CLIPS): track 0..JLCL_TRACKS-1, note = the slot 0..JLCL_SLOTS-1 or 255 stop, vel = when (0 the pattern's end, 1 the next bar, 2 now) */
    JLAPP_ACT_SCENE                    /* ... a scene pad: note = the scene 0..JLCL_SLOTS-1 */
};

struct jlapp_act {
    uint8_t kind, track, note, vel;
    uint8_t step, op, len;
};

/* an act with kind, track and note, the rest 0 (the few with a vel, op ... set them after); returns 1, the acts made (jank_lpstep.c: every file that makes acts links it) */
size_t jlapp_act(struct jlapp_act *o, uint8_t kind, uint8_t track, uint8_t note);

/* the most acts one pad event makes: a drum track pad on another track while the transport is stopped selects the track AND plays it. A caller that gives room for
 * fewer drops such a press whole (nothing happens: owner 2026-11-24, "in drum step, i cannot change track when transport is stopped") */
#define JLAPP_ACTS_MAX 2u

/* a note the Digitone's sequencer played (its push), kept for the lights: both Note mode and the step modes flash what plays */
struct jlapp_recent {
    uint32_t at_ms;
    uint8_t track, note, used;
};
#define JLAPP_PUSH_LIGHT_MS 150u       /* a pushed note lights its pad this long (the note's length is not mapped yet: not measured) */
#define JLAPP_RECENT 32u               /* pushed notes remembered for the lights */
/* a note of `track` (and this pitch, unless note < 0) among the n recent ones that the Digitone played in the last JLAPP_PUSH_LIGHT_MS */
int jlapp_played(const struct jlapp_recent *r, unsigned n, uint32_t now_ms, uint8_t track, int note);
/* a TRACK_OFF act for each bit of *held (track pads that are down), cleared, at most max; returns how many */
size_t jlapp_tracks_off(uint16_t *held, struct jlapp_act *out, size_t max);
#endif
