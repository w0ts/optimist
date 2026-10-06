/* SPDX-License-Identifier: GPL-3.0-only */
/* Backports from SLOOP 2.3 (isod89/sloop-fm1 d691ba7, GPL-3.0; many of its fixes after Felucca 1.0 / 1.0.1 by
 * Leo Kuroshita) and X0X 0.10.1-beta (charlesvestal/fm1-x0x 49b1fc8, GPL-3.0): one build switch each, as
 * backports.h (tools/backports.json: provenance, measured cost). A switch at 0 builds exactly the firmware
 * without it. Included by backports.h.
 *
 *   FELUCCA_MONO_RELEASE  a key let go just after a VOICE change leaves no stuck note   SLOOP 2.3 (Felucca 1.0)
 *   FELUCCA_ST_STRICT     stricter checks of what is read back from flash               SLOOP 2.3 (Felucca 1.0)
 *   FELUCCA_USB_FLOW      USB MIDI in: nothing dropped under load (NAK), malformed ignored  SLOOP 2.3 (Felucca 1.0)
 *   FELUCCA_SHED_FADE     overload: fade one voice at a time, never the bass or the lead   SLOOP 2.3 (Felucca 1.0)
 *   FELUCCA_KEYS_FAST     keys debounced as their column is read: ~1 ms sooner             SLOOP 2.3 (Felucca 1.0)
 *   FELUCCA_REC_MODES     the REC screen's dials: MODE free / tempo, LENGTH, START note / count (4-3-2-1)
 *                                                                                        SLOOP 2.3
 *   FELUCCA_LIGHTS        menu LIGHTS / KEYS (the buttons and keys glow), NOTES (KEYLIT at run time)
 *                                                          SLOOP 2.3 (Felucca 1.0.1 #35, renebohne #11) */
#ifndef FELUCCA_BACKPORTS23_H
#define FELUCCA_BACKPORTS23_H

#ifndef FELUCCA_MONO_RELEASE
#define FELUCCA_MONO_RELEASE 1   /* trk_note_off takes the key out of the MONO stack in every VOICE mode */
#endif
#ifndef FELUCCA_ST_STRICT
#define FELUCCA_ST_STRICT 1      /* storage.c: a record only in the copy it was written to, objects in range, the whole
                                  * header read back after a save; panel.c: the calibration a permutation */
#endif
#ifndef FELUCCA_USB_FLOW
#define FELUCCA_USB_FLOW 1       /* usb.c: an EP1 OUT packet waits in the endpoint (the host is NAKed) until the MIDI
                                  * ring has room; malformed events and stray status bytes in SysEx are ignored */
#endif
#ifndef FELUCCA_SHED_FADE
#define FELUCCA_SHED_FADE 1      /* audio.c shed_voice: a held voice fades (voice_kill) instead of being released, never a
                                  * part's bass (POLY) or lead (MONO...); only after two overloaded halves in a row */
#endif
#ifndef FELUCCA_KEYS_FAST
#define FELUCCA_KEYS_FAST 1      /* hal/fm1_input.h: a press after 2 samples closed in a row, read with its column; a
                                  * release after 8 open in a row */
#endif
#ifndef FELUCCA_REC_MODES
#define FELUCCA_REC_MODES 0      /* seq.c, ui_input.c rec_knobs, ui_studio.c rec_screen_draw; settings: persist_t.bp23 */
#endif
#ifndef FELUCCA_LIGHTS
#define FELUCCA_LIGHTS 0         /* hal/fm1_input.h (the backlight layer), lights.c, ui_input.c, ui_menu.c */
#endif
#define BP23_SET (FELUCCA_REC_MODES || FELUCCA_LIGHTS)   /* project.c: the settings record keeps the SLOOP 2.3 word (bp23_word) */

#endif
