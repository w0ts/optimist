/* SPDX-License-Identifier: GPL-3.0-only */
/* Launchpad Note mode: which MIDI note each of the 8x8 pads plays, a port of Launchpad 95's MelodicPattern and ScaleComponent
 * (hdavid/launchpad95, 'diatonic', 'diatonic_chords' and 'chromatic' layouts only). The scale is passed in (the Digitone II's
 * own list is not read from the unit yet: tests/test_lpnote.c uses a major scale and a pentatonic one). Pure, host-tested.
 *
 *   chromatic  a row up is a fourth (5 semitones), a column right a semitone; all 12 notes from the key.
 *   diatonic   a row up is a third (3 scale steps), a column right one scale step.
 *   chords     (diatonic, vertical) a column right is a third (2 scale steps), a row up one scale step.
 *   absolute   the bottom left pad is a fixed note (C) whatever the key; relative: it is the key's root.
 * Pads (x = column 0..7 from the left, y = row 0..7 from the bottom) that come out above 127 or below 0 play nothing. */
#ifndef JANK_LPNOTE_H
#define JANK_LPNOTE_H
#include <stdint.h>

#define JLN_SCALE_MAX 12u
#define JLN_TRACKS 16u

enum jln_layout { JLN_CHROMATIC, JLN_DIATONIC, JLN_CHORDS };

struct jln_scale {
    const char *name;
    uint8_t n;                         /* notes in the scale, 1..JLN_SCALE_MAX */
    uint8_t iv[JLN_SCALE_MAX];         /* semitones above the root, the first 0 */
};

struct jln {
    uint8_t layout;                    /* enum jln_layout */
    uint8_t key;                       /* 0 C .. 11 B */
    uint8_t octave;                    /* LP95's: the bottom left pad is in MIDI octave (octave + 1): 0..5 diatonic, 0..6 else */
    uint8_t absolute;
    uint8_t horizontal;                /* a row is a run of scale steps (chords: a column is) */
    uint8_t track;                     /* the DN2 track the pads play on, 0..15 */
};

struct jln_pad {
    int16_t note;                      /* MIDI note, -1: the pad plays nothing */
    uint8_t root;                      /* the note is the scale's root (any octave) */
    uint8_t highlight;                 /* the scale's 3rd or 5th step (Launchpad 95: scale[2] and scale[4]): lit brighter than the other scale notes */
    uint8_t in_scale;                  /* in the reference scale (the current one, or major for Chromatic) */
    uint8_t minor_only;                /* not in the reference, which is major, but in natural minor: the flat 3rd, 6th, 7th */
};

/* Launchpad palette indexes (manual p. 12): the colours of Live's Note mode */
/* the colours of Launchpad 95's Note mode (SkinMK2.py Note.Pads and Note.Feedback; the indexes are its ColorsMK2.py's names) */
#define JLN_COLOR_ROOT 49u             /* PURPLE_HALF (owner 2026-11-25, picked on the Session colour chart: "root note must be bright"; was Launchpad 95's BLUE 45) */
#define JLN_COLOR_HIGHLIGHT 37u        /* LIGHT_BLUE: the scale's 3rd and 5th step (Note.Pads.Highlight) */
#define JLN_COLOR_IN_SCALE 38u         /* LIGHT_BLUE_HALF */
/* Notes outside the scale: 43, picked by the owner (2026-11-24: "white we cannot use it for off scale notes"; white is the track pads' colour); and, when the
 * reference is the major scale, the notes that are in natural minor but not in major (the flat 3rd, 6th and 7th): 46, also picked by the owner. The reference is the current scale, except
 * the Digitone's "Chromatic" (all 12 notes: nothing would stand out), where it is the MAJOR scale of the key. */
#define JLN_COLOR_OUT_OF_SCALE 43u     /* (owner 2026-11-25, picked on the Session colour chart; was DARK_GREY 1) */
#define JLN_COLOR_MINOR_ONLY 46u       /* BLUE_HALF (owner 2026-11-25, picked on the Session colour chart; before: amber too loud, PURPLE_HALF 49 / PURPLE_THIRD 50 too bright, then 51) */
#define JLN_COLOR_PLAYING 21u          /* GREEN: a note the DN2 is sounding */

void jln_init(struct jln *s);                                        /* diatonic, C, octave 3, relative, track 0 */
void jln_set_layout(struct jln *s, enum jln_layout l);               /* sets the orientation, pulls the octave under the layout's top */
void jln_octave_step(struct jln *s, int dir);                        /* +1 up / -1 down, held inside the layout's octaves */
unsigned jln_octaves(const struct jln *s);                           /* how many octaves the layout has: the octaves are 0 .. this - 1 */
void jln_set_octave(struct jln *s, unsigned octave);                 /* a place past the layout's last octave changes nothing */
void jln_track_step(struct jln *s, int dir);                         /* one track left / right, no wrap */
void jln_pad(const struct jln *s, const struct jln_scale *sc, uint8_t x, uint8_t y, struct jln_pad *out);

/* The step modes' 4x4 keyboard (owner 2026-11-24): 16 DIFFERENT notes, no overlap. Key i = y * 4 + x (the bottom left is 0): the scale's steps one after the other from the
 * origin (the key's root at the octave, or the fixed C when absolute), a row up = 4 steps; the chromatic layout: semitones. The chords layout gives the same keys as diatonic. */
void jln_key(const struct jln *s, const struct jln_scale *sc, unsigned i, struct jln_pad *out);

/* the pads (bit y * 8 + x) that play `note`: one note lights every pad with that pitch; 0 for a note out of 0..127 */
uint64_t jln_pads_with_note(const struct jln *s, const struct jln_scale *sc, int note);

/* a pad's palette index; playing wins over everything; a pad that plays nothing (outside MIDI's 0..127) is 0 */
uint8_t jln_color(const struct jln_pad *p, int playing);
#endif
