/* SPDX-License-Identifier: GPL-3.0-only */
/* Backported features from the FM-1 scene, each a build switch of its own (tools/backports.json: provenance,
 * licence, measured cost; OPTIMIST.md: what each does). A switch at 0 builds exactly the firmware without it:
 * the goldens stay as they were. Unset: the defaults below. tools/build.py passes FELUCCA_x=0/1 from the
 * environment.
 *
 *   FELUCCA_CHANCE    per-step chance on the synth tracks              Felucca 1.0 (hugelton/Felucca 727f272,
 *                                                                      Leo Kuroshita, GPL-3.0-only)
 *   FELUCCA_KEYLIT    the keys light the notes the sequencer / arp     Felucca 1.0.1 (20c275e, #38) and
 *                     of the selected track play                       renebohne/sloop-fm1 e2e5099 (GPL-3.0)
 *   FELUCCA_QNT_SEQ   SCL > QNT SEQ: sequenced notes snap to the scale Felucca 1.0.1 (20c275e, #37)
 *   FELUCCA_SPRING    REV/CHO > TYPE: a spring reverb                  Felucca 1.0 (727f272)
 *   FELUCCA_BASSPLUS  MASTER > BASS+: small-speaker bass               Felucca 1.0 (727f272)
 *   FELUCCA_BRIGHT    SYSTEM menu: the screen's backlight level         X0X (charlesvestal/fm1-x0x 61654ba)
 *   FELUCCA_DLY_HALVE a delay longer than the line halves (on beat)    X0X (charlesvestal/fm1-x0x 892a3b5)
 *   FELUCCA_MOTION    knob moves recorded per step                     Felucca 1.0 (727f272)
 *   FELUCCA_ENG_PHYS  the PHYS engine (modal / string / membrane)      Felucca 1.0 (727f272; DaisySP + Rings
 *                                                                      parts MIT)
 *   FELUCCA_ENG_ACID  the ACID engine (303 voice + generator)          X0X (charlesvestal/fm1-x0x 80b7d40;
 *                                                                      Open303 parts MIT) EXPERIMENTAL
 *   FELUCCA_LAYER_QUIET knob turns as a layer is let go: no tap, not  Felucca 1.0.2 (db70550, #39)
 *                     the page's (and 250 ms after it closed)
 * Felucca 1.0.2 / 1.0.3 small options (default off): */
#ifndef FELUCCA_BACKPORTS_H
#define FELUCCA_BACKPORTS_H

#ifndef FELUCCA_CHANCE
#define FELUCCA_CHANCE 0         /* +832 B flash; nothing changes until a step's chance is turned down */
#endif
#ifndef FELUCCA_KEYLIT
#define FELUCCA_KEYLIT 1         /* on: +224 B flash, LEDs only (Felucca 1.0.1 has it always on) */
#endif
#ifndef FELUCCA_QNT_SEQ
#define FELUCCA_QNT_SEQ 0        /* SCL > QNT SEQ: the sequenced notes snap to the scale as they play */
#endif
#ifndef FELUCCA_SPRING
#define FELUCCA_SPRING 0         /* FX > REVERB > TYPE: ROOM or SPRING */
#endif
#ifndef FELUCCA_BASSPLUS
#define FELUCCA_BASSPLUS 0       /* MENU > LOWCUT: OFF / LOWCUT / BASS+ (the small speaker) */
#endif
#ifndef FELUCCA_BRIGHT
#define FELUCCA_BRIGHT 0         /* MENU > BRIGHT: the backlight, 1..8 */
#endif
#ifndef FELUCCA_DLY_HALVE
#define FELUCCA_DLY_HALVE 1      /* a delay time longer than the line halves (on the beat) instead of being cut */
#endif
#ifndef FELUCCA_MOTION
#define FELUCCA_MOTION 0         /* knob moves recorded per step (SEQ > MOTION) */
#endif
#ifndef FELUCCA_ENG_PHYS
#define FELUCCA_ENG_PHYS 0       /* the PHYS engine (engine 11): 51.5 KB of pool */
#endif
#ifndef FELUCCA_ENG_ACID
#define FELUCCA_ENG_ACID 0       /* the ACID engine (engine 12), X0X-derived, EXPERIMENTAL */
#endif
#ifndef FELUCCA_LAYER_QUIET
#define FELUCCA_LAYER_QUIET 1    /* #39: knob turns as a layer is let go (and 250 ms after) never reach the page */
#endif

#include "backports23.h"     /* SLOOP 2.3 and X0X 0.10.1 backports */

#define BP_SET_ANY (FELUCCA_SPRING || FELUCCA_ENG_ACID)   /* bp_set.c: the settings with a page of their own (SC_BPSET) */

#endif
