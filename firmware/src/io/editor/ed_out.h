/* SPDX-License-Identifier: GPL-3.0-only */
/* Editor protocol: the size of a reply (editor.c ed_out, F0 .. F7). What limits it is RAM, not the wire: a reply
 * leaves through ota_wire_send (usb.c) in USB-MIDI packets of 3 bytes, any length (the FM6 bank's 4104-byte DX7
 * SysEx goes the same way), and the web editor takes a SysEx of any length (WebMIDI). Main RAM is full in the larger
 * builds ("mots", 2026-10: 176 B left, which the undo history takes; ed_out at 768 B left 48), so a list that can
 * grow (BK_LIST) is paged instead of this buffer growing (ed_backup.c BK_PAGE). The frames the FM-1 receives are a
 * separate limit (usb.c sx_frame, 640).
 * A reply's payload (after the 5-byte header, the F7 kept): ED_OUT_N - 6 bytes; ed_b drops what does not fit, so a
 * reply that can grow carries a _Static_assert of its own (ED_PAYLOAD_N). Host tests include this file for the
 * same sizes. */
#ifndef ED_OUT_H
#define ED_OUT_H
#define ED_OUT_N 640u
#define ED_PAYLOAD_N (ED_OUT_N - 6u)
#endif
