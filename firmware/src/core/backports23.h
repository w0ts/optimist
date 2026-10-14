/* SPDX-License-Identifier: GPL-3.0-only */
/* Backports from SLOOP 2.3 (isod89/sloop-fm1 d691ba7, GPL-3.0; many of its fixes after Felucca 1.0 / 1.0.1 by
 * Leo Kuroshita) and X0X 0.10.1-beta (charlesvestal/fm1-x0x 49b1fc8, GPL-3.0): one build switch each, as
 * backports.h (tools/backports.json: provenance, measured cost). A switch at 0 builds exactly the firmware
 * without it. Included by backports.h.
 *
 *   FELUCCA_REC_MODES     the REC screen's dials: MODE free / tempo, LENGTH, START note / count (4-3-2-1)
 *                                                                                        SLOOP 2.3
 *   FELUCCA_LIGHTS        menu LIGHTS / KEYS (the buttons and keys glow), NOTES (KEYLIT at run time)
 *                                                          SLOOP 2.3 (Felucca 1.0.1 #35, renebohne #11)
 *   FELUCCA_GLIDE         part level, pan and sends, MASTER, the drum track's level, pan and lane sends glide over
 *                         ~10 ms (no zipper)                       X0X 0.10.1-beta (charlesvestal/fm1-x0x 49b1fc8)
 * The fixes of SLOOP 2.3 that were switches here are always built since 2026-10-14 (the refusals below). */
#ifndef FELUCCA_BACKPORTS23_H
#define FELUCCA_BACKPORTS23_H

#ifndef FELUCCA_REC_MODES
#define FELUCCA_REC_MODES 0      /* seq.c, ui_input.c rec_knobs, ui_studio.c rec_screen_draw; settings: persist_t.bp23 */
#endif
#ifndef FELUCCA_LIGHTS
#define FELUCCA_LIGHTS 0         /* hal/fm1_input.h (the backlight layer), lights.c, ui_input.c, ui_menu.c */
#endif
#ifndef FELUCCA_GLIDE
#define FELUCCA_GLIDE 0          /* fx.c glide_next: mix_part, mix_finish (MASTER); drums.c drums_mix (dgl) */
#endif
#ifndef FELUCCA_VIS
#define FELUCCA_VIS 0            /* SLOOP 2.4's visualiser (backports24.h; here for BP23_SET: its style is in the settings word) */
#endif
/* Retired 2026-10-14, always built: upstream's fix is unchanged in its current release (tools/builder/registry.py
 * RETIRED). A build that still asks for one of them off is refused */
#if defined(FELUCCA_MONO_RELEASE) && !FELUCCA_MONO_RELEASE
#error "FELUCCA_MONO_RELEASE was retired: no stuck note after a VOICE change is always built (SLOOP 2.3 .. 2.6)"
#endif
#if defined(FELUCCA_ST_STRICT) && !FELUCCA_ST_STRICT
#error "FELUCCA_ST_STRICT was retired: the stricter flash read-back checks are always built (SLOOP 2.3 .. 2.6)"
#endif
#if defined(FELUCCA_USB_FLOW) && !FELUCCA_USB_FLOW
#error "FELUCCA_USB_FLOW was retired: USB MIDI in flow control (NAK) is always built (SLOOP 2.3 .. 2.6)"
#endif
#if defined(FELUCCA_SHED_FADE) && !FELUCCA_SHED_FADE
#error "FELUCCA_SHED_FADE was retired: the overload fade (one voice at a time, never the bass or the lead) is always built (SLOOP 2.3 .. 2.6)"
#endif
#if defined(FELUCCA_KEYS_FAST) && !FELUCCA_KEYS_FAST
#error "FELUCCA_KEYS_FAST was retired: the keys are always read with their column (SLOOP 2.3 .. 2.6)"
#endif
#if defined(FELUCCA_TRS_NOISE) && !FELUCCA_TRS_NOISE
#error "FELUCCA_TRS_NOISE was retired: TRS MIDI in past line noise (an FD at the reader) is always built (SLOOP 2.3 .. 2.6)"
#endif
#if defined(FELUCCA_BK_CHECK) && !FELUCCA_BK_CHECK
#error "FELUCCA_BK_CHECK was retired: a restore always checks each object as a load would (SLOOP 2.3 .. 2.6)"
#endif
#if defined(FELUCCA_LAYER_QUIET) && !FELUCCA_LAYER_QUIET
#error "FELUCCA_LAYER_QUIET was retired: the knobs are always quiet as a layer is let go (Felucca 1.0.2 #39 .. 1.6, SLOOP 2.6)"
#endif
#if defined(FELUCCA_DLY_HALVE) && !FELUCCA_DLY_HALVE
#error "FELUCCA_DLY_HALVE was retired: a delay time longer than the line always halves (X0X 892a3b5 .. 1.0.6)"
#endif
#define BP23_SET 1   /* project.c: the settings record keeps
                                                    * the settings word (bp23_word: SLOOP 2.3's bits, 2.4's MIDI OUT / IN) */

#endif
