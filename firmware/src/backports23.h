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
 *                                                          SLOOP 2.3 (Felucca 1.0.1 #35, renebohne #11)
 *   FELUCCA_GLIDE         part level, pan and sends, MASTER, the drum track's level, pan and lane sends glide over
 *                         ~10 ms (no zipper)                       X0X 0.10.1-beta (charlesvestal/fm1-x0x 49b1fc8)
 *   FELUCCA_KNOB_ONEREST  knobs: one rest state per detent (a full cycle), steps rounded to whole cycles, with our
 *                         X0X decoder (no two-scan filter): a pause mid-click no longer doubles every later click
 *                                                                  SLOOP 2.3 (Felucca 1.0 #23) + X0X b637df3
 *   FELUCCA_TRS_NOISE     TRS MIDI in: a received FD at the reader no longer stalls the jack until a restart
 *                                                          SLOOP 2.3 (Felucca [Salt] by ChanceTheMaker)
 *   FELUCCA_BK_CHECK      restore: each storage object checked at its commit as a load checks it, else refused
 *                                                                                        SLOOP 2.3 */
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
#ifndef FELUCCA_GLIDE
#define FELUCCA_GLIDE 0          /* fx.c glide_next: mix_part, mix_finish (MASTER); drums.c drums_mix (dgl) */
#endif
#ifndef FELUCCA_KNOB_ONEREST
#define FELUCCA_KNOB_ONEREST 1   /* hal/fm1_input.h fm1__frame (the knobs' detents) */
#endif
#ifndef FELUCCA_TRS_NOISE
#define FELUCCA_TRS_NOISE 1      /* midi_uart.c uart_midi_peek: looks one slot past an FD at the reader */
#endif
#ifndef FELUCCA_BK_CHECK
#define FELUCCA_BK_CHECK 1       /* ed_backup.c BK_COMMIT: a storage object written only if a load would take it (rc 8) */
#endif
#define BP23_SET (FELUCCA_REC_MODES || FELUCCA_LIGHTS || FELUCCA_CDC || FELUCCA_MIDI_OUT || FELUCCA_MIDI_INCLK)
                                               /* project.c: the settings record keeps the settings word (bp23_word:
                                                * SLOOP 2.3's bits; 2.4's MIDI OUT / IN bits 14, 15; FELUCCA_CDC: USB SERIAL, bit 16) */

#endif
