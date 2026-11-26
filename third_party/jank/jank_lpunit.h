/* SPDX-License-Identifier: GPL-3.0-only */
/* The unit the Launchpad core drives, at compile time (owner 2026-11-26: the core is shared with the FM-1, "make it ultra compact. on fm1 flash is very pricey"): another
 * unit passes -D flags (or keeps its own copy of this header); the defaults are jank's Digitone II. A build compiles only what its unit has: nothing here is a run-time
 * choice. docs/launchpad.md, "Using the core on another unit". */
#ifndef JANK_LPUNIT_H
#define JANK_LPUNIT_H

#ifndef JLN_TRACKS
#define JLN_TRACKS 16u                 /* 1..16: the track pads, the select overlay, the drum quadrant and the mixer show this many; a pad past them is dark and does nothing */
#endif
#ifndef JLAPP_CLIPS
#define JLAPP_CLIPS 0                  /* the Session page: 0 the Digitone's 128 patterns (jank_lpsess.[ch]), 1 the unit's clips, per track (jank_lpclips.[ch]) */
#endif
#ifndef JLCL_TRACKS
#define JLCL_TRACKS JLN_TRACKS         /* the clips page's track columns, 1..7 (the 8th holds the scenes): the FM-1's 4 (its drum lanes, tracks of their own, share the drum track's clips) */
#endif
#ifndef JLCL_SLOTS
#define JLCL_SLOTS 16u                 /* a track's clip slots (and the scenes): two halves of 8 rows */
#endif
#ifndef JLAPP_RECORD
#define JLAPP_RECORD 1                 /* scene 8 records (JLAPP_ACT_RECORD); 0: dark, does nothing, compiled away */
#endif
#ifndef JLAPP_ERASE
#define JLAPP_ERASE 1                  /* scene 6 held erases (JLAPP_ACT_ERASE); 0: the same */
#endif
#if JLN_TRACKS < 1 || JLN_TRACKS > 16
#error "JLN_TRACKS: 1..16"
#endif

/* track t is past the unit's last one: the pads name up to 16 (two rows of 8, the 4x4 quadrant, the mixer's two banks). Constant 0 with 16 tracks */
#define JLN_NO_TRACK(t) (JLN_TRACKS < 16u && (unsigned)(t) >= JLN_TRACKS)
#endif
