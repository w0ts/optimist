/* SPDX-License-Identifier: GPL-3.0-only */
/* Backports from SLOOP 2.3 (isod89/sloop-fm1 d691ba7, GPL-3.0; many of its fixes after Felucca 1.0 / 1.0.1 by
 * Leo Kuroshita) and X0X 0.10.1-beta (charlesvestal/fm1-x0x 49b1fc8, GPL-3.0): one build switch each, as
 * backports.h (tools/backports.json: provenance, measured cost). A switch at 0 builds exactly the firmware
 * without it. Included by backports.h.
 *
 *   FELUCCA_MONO_RELEASE  a key let go just after a VOICE change leaves no stuck note   SLOOP 2.3 (Felucca 1.0)
 *   FELUCCA_ST_STRICT     stricter checks of what is read back from flash               SLOOP 2.3 (Felucca 1.0)
 *   FELUCCA_USB_FLOW      USB MIDI in: nothing dropped under load (NAK), malformed ignored  SLOOP 2.3 (Felucca 1.0) */
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

#endif
