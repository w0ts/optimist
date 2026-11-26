/* SPDX-License-Identifier: GPL-3.0-only */
/* What the Launchpad part remembers across a restart (owner 2026-11-24: "jank should remember the settings, like enabling the 16 bottom pads"): the note layout and its
 * switches, the octave, the track pads, the step mode the Custom button opens, the mixer's view and FOLLOW. Not the scale and the key: while the Digitone's live control is up
 * they come from the Digitone (jank_lpapp.h), and a restart would only start from a stale pair; not the mode (it starts in Note mode) or the bank / page / track.
 * A small packed blob (version 1, 8 settings, a checksum): a blob that is short, of another version, damaged or out of range is not used (the defaults stay). The firmware
 * keeps it in NVS and writes it the way the other preferences are written (jank_prefs.h: 400 ms after the last change, never while the transport plays). Pure, host-tested
 * (tests/test_lpprefs.c). */
#ifndef JANK_LPPREFS_H
#define JANK_LPPREFS_H
#include <stddef.h>
#include <stdint.h>
#include "jank_lpapp.h"

#define JLPREFS_VERSION 2u
#define JLPREFS_SIZE 11u               /* version, 9 settings, checksum */
#define JLPREFS_SIZE_V1 10u            /* version 1 (before fixed velocity, 2026-11-25): still read, fixed velocity off */

struct jlprefs {
    uint8_t layout;                    /* enum jln_layout */
    uint8_t horizontal, absolute;
    uint8_t octave;
    uint8_t track_pads;
    uint8_t step_kind;                 /* enum jlst_kind: the step mode Custom opens */
    uint8_t mix_view;                  /* enum jlmx_view */
    uint8_t follow;
    uint8_t fixed_vel;
};

void jlprefs_defaults(struct jlprefs *p);                                    /* diatonic, horizontal, relative, octave 3, no track pads, melodic, volume, no follow */
size_t jlprefs_pack(const struct jlprefs *p, uint8_t *out, size_t cap);      /* JLPREFS_SIZE, 0: no room */
int jlprefs_unpack(struct jlprefs *p, const uint8_t *in, size_t n);          /* 1 loaded; 0 not a valid blob (p untouched) */
int jlprefs_equal(const struct jlprefs *a, const struct jlprefs *b);
/* the app's settings, and the other way round (an octave a layout does not have is pulled under its top, as jln_set_layout does) */
void jlprefs_from_app(const struct jlapp *a, struct jlprefs *p);
void jlprefs_apply(struct jlapp *a, const struct jlprefs *p);
#endif
