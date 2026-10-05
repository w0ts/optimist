/* SPDX-License-Identifier: GPL-3.0-only */
/* Backported features from the FM-1 scene, each a build switch of its own (tools/backports.json: provenance,
 * licence, measured cost; SLOOP.md: what each does). A switch at 0 builds exactly the firmware without it:
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
 *                                                                      Open303 parts MIT) EXPERIMENTAL */
#ifndef FELUCCA_BACKPORTS_H
#define FELUCCA_BACKPORTS_H

#ifndef FELUCCA_CHANCE
#define FELUCCA_CHANCE 0         /* +832 B flash; nothing changes until a step's chance is turned down */
#endif

#endif
