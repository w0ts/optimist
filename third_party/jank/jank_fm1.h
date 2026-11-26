/* SPDX-License-Identifier: GPL-3.0-only */
/* OPTIMIST'S OWN (not copied from jank: tools/sync_jank.sh leaves it): the switches jank's Launchpad app is built with for the FM-1
 * (io/launchpad/launchpad.c includes it before jank's files). jank's tracks: the FM-1's 4, then the drum track's 11 lanes (15);
 * the clips page with its 4 columns (the patterns are per track). jank's own defaults (core/jank_lpunit.h) are the Digitone's.
 * Until jank's generic core is copied in (its jank_lpunit.h), its JLN_TRACKS is a fixed 16 and it has no clips page: these
 * switches then do nothing and the app has 16 tracks, of which the FM-1 uses 15. */
#ifndef JANK_FM1_H
#define JANK_FM1_H
#define JLN_TRACKS_FM1 15u
#ifndef JLAPP_CLIPS
#define JLAPP_CLIPS 0                  /* (1 once jank's clips page is here: jank_lpclips.h) */
#endif
#ifndef JLCL_TRACKS
#define JLCL_TRACKS 4u
#endif
#endif
