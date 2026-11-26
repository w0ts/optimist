/* SPDX-License-Identifier: GPL-3.0-only */
/* The Launchpad app's packets for the Digitone wait here, in order, until the Digitone's own queue has room (firmware/main/
 * usb_lp.c): a Note Off that met a full queue would otherwise be dropped and leave the note sounding. 4-byte USB-MIDI event
 * packets, oldest first. Only a push into a full FIFO refuses (the new packet); nothing already in it is ever dropped.
 * No ESP-IDF: host-tested (tests/test_lpout.c). */
#ifndef JANK_LPOUT_H
#define JANK_LPOUT_H
#include <stddef.h>
#include <stdint.h>

#define JLO_PACKETS 128u               /* one Launchpad's 64 pads can all be down; a whole JANK PAGE (62) of 7 full lines is 91 packets (a SysEx goes in whole or not at all) */

struct jlo {
    uint8_t b[JLO_PACKETS][4];
    uint8_t head;                      /* the oldest */
    uint8_t count;
};

void jlo_init(struct jlo *q);
void jlo_clear(struct jlo *q);
size_t jlo_count(const struct jlo *q);
/* 1: stored; 0: full */
int jlo_push(struct jlo *q, const uint8_t pkt[4]);
/* moves the oldest packets, at most `room`, into out (4 bytes each); returns how many */
size_t jlo_drain(struct jlo *q, size_t room, uint8_t *out);
#endif
