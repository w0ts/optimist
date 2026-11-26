/* SPDX-License-Identifier: GPL-3.0-only */
/* The Session page as clips (JLAPP_CLIPS 1, jank_lpunit.h), for a unit whose patterns are per track (the FM-1, owner 2026-11-26), as Ableton's session view: the tracks in
 * the columns 0..JLCL_TRACKS-1, a track's JLCL_SLOTS slots in the rows, 8 at a time from the top (up / down: slots 1-8 / 9-16, as the Digitone's page switches its banks), and
 * the scenes in the rightmost column (scenes 1-8 / 9-16 with the slots shown; the scene buttons stay start / stop, as on the Digitone's page). Lights as the Digitone's page
 * (Launchpad 95's session skin): the playing slot GREEN pulsing (flashing when a stop is queued), a queued slot GREEN flashing, a slot that holds a pattern RED_THIRD, an empty
 * one dark; a stored scene AMBER_THIRD (my pick, not the owner's yet), an empty one dark. A slot pressed: JLAPP_ACT_CLIP {track, note = the slot, vel = 0: at the pattern's
 * end}, the playing slot a stop (note JLCL_NONE); a scene pad: JLAPP_ACT_SCENE {note = the scene}. What shows is only the unit's news (jlapp_on_clips): the unit calls it
 * whenever its clips change, a press's queue included. Instead of jank_lpsess.[ch]; jank's firmware does not build it (host-tested: tests/test_lpunit.c). */
#ifndef JANK_LPCLIPS_H
#define JANK_LPCLIPS_H
#include <stdint.h>
#include "jank_lpunit.h"

#define JLCL_NONE 255u                 /* playing / queued: none; JLAPP_ACT_CLIP's note: stop */
#define JLCL_STOP 254u                 /* queued: a stop */
#define JLCL_COLOR_PLAYING 21u         /* GREEN, pulsing (flashing: a stop is queued) */
#define JLCL_COLOR_FILLED 7u           /* RED_THIRD */
#define JLCL_COLOR_SCENE 11u           /* AMBER_THIRD: a stored scene */
#if JLCL_TRACKS < 1 || JLCL_TRACKS > 7 || JLCL_SLOTS != 16
#error "JLCL_TRACKS: 1..7 (the 8th column holds the scenes); JLCL_SLOTS: 16 (two halves of 8 rows)"
#endif

struct jlcl_track {
    uint8_t playing;                   /* the slot that plays, JLCL_NONE none */
    uint8_t queued;                    /* the slot that plays next, JLCL_NONE none, JLCL_STOP a stop */
    uint16_t filled;                   /* bit s: slot s holds a pattern */
};

struct jlcl {
    struct jlcl_track t[JLCL_TRACKS];
    uint32_t scenes;                   /* bit n: scene n is stored */
    uint8_t half;                      /* 0: slots and scenes 1-8, 1: 9-16 */
};
#endif
