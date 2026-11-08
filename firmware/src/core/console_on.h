/* SPDX-License-Identifier: GPL-3.0-only */
/* FELUCCA_CONSOLE: the console's code is built (io/console.c, the serial port FELUCCA_CDC or SysEx on USB MIDI
 * FELUCCA_CON_MIDI). felucca.c defines it after the transports; this gives the same default to the units that test it and
 * are compiled without felucca.c (the host tests: ui/optimist/optimist.c's WIFI_UI, io/editor/ed_opti.h, bt/bt_page.c;
 * the release's units). Without it those read an undefined FELUCCA_CONSOLE as 0 and a test build passing FELUCCA_CDC=1
 * built no Wi-Fi UI (2026-11-08: the Wi-Fi UI tests ran without it after the console over MIDI). */
#ifndef FELUCCA_CONSOLE
#if (defined(FELUCCA_CDC) && FELUCCA_CDC) || (defined(FELUCCA_CON_MIDI) && FELUCCA_CON_MIDI)
#define FELUCCA_CONSOLE 1
#else
#define FELUCCA_CONSOLE 0
#endif
#endif
