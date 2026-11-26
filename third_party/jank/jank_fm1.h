/* SPDX-License-Identifier: GPL-3.0-only */
/* OPTIMIST'S OWN (not copied from jank: tools/sync_jank.sh leaves it): the switches jank's Launchpad core is built with for the FM-1
 * (io/launchpad/launchpad.c includes it before jank's files; jank's core/jank_lpunit.h has the Digitone's defaults). jank's tracks:
 * the FM-1's 4, then the drum track's 11 lanes (15); the clips page with its 4 columns (the patterns are per track); no RECORD and
 * no ERASE (their code gone, their buttons dark). */
#ifndef JANK_FM1_H
#define JANK_FM1_H
#define JLN_TRACKS 15u
#define JLCL_TRACKS 4u
#define JLAPP_CLIPS 1
#define JLAPP_RECORD 0
#define JLAPP_ERASE 0
#define JANK_LPSTR_H                   /* (memset, memcpy, memcmp: Optimist's unit has them declared already, the release renames them
                                        * to fm1-base's felucca_mem*: jank's own declarations would clash) */
#endif
