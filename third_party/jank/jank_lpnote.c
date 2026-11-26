/* SPDX-License-Identifier: GPL-3.0-only */
/* Launchpad Note mode's pad layouts (jank_lpnote.h): a port of Launchpad 95's MelodicPattern. */
#include "jank_lpnote.h"
#include "jank_lpstr.h"

#define TOP_OCTAVE_DIATONIC 6u         /* LP95's TOP_OCTAVE: octaves 0 .. top - 1 */
#define TOP_OCTAVE_OTHER 7u
#define DEFAULT_OCTAVE 3u
#define CHROMATIC_SIZE 12              /* a chromatic pattern runs over the 12 notes from the key */
#define CHROMATIC_ROW_STEP 5           /* a row up is a fourth */
#define DIATONIC_ROW_STEP 3
#define CHORDS_COLUMN_STEP 2

static uint8_t top_octave(uint8_t layout) { return layout == JLN_DIATONIC ? TOP_OCTAVE_DIATONIC : TOP_OCTAVE_OTHER; }

void jln_init(struct jln *s)
{
    s->key = 0;
    s->octave = DEFAULT_OCTAVE;
    s->absolute = 0;
    s->track = 0;
    s->layout = JLN_DIATONIC;
    s->horizontal = 1;
}

void jln_set_layout(struct jln *s, enum jln_layout l)
{
    s->layout = (uint8_t)l;
    s->horizontal = l != JLN_CHORDS;
    if (s->octave >= top_octave(s->layout))
        s->octave = (uint8_t)(top_octave(s->layout) - 1u);
}

void jln_octave_step(struct jln *s, int dir)
{
    if (dir > 0 && s->octave + 1u < top_octave(s->layout))
        s->octave++;
    else if (dir < 0 && s->octave > 0)
        s->octave--;
}

unsigned jln_octaves(const struct jln *s) { return top_octave(s->layout); }

void jln_set_octave(struct jln *s, unsigned octave)
{
    if (octave < top_octave(s->layout))
        s->octave = (uint8_t)octave;
}

void jln_track_step(struct jln *s, int dir)
{
    if (dir > 0 && s->track + 1u < JLN_TRACKS)
        s->track++;
    else if (dir < 0 && s->track > 0)
        s->track--;
}

/* floored division and remainder: a negative index goes down an octave, as Python's // and % do */
static int floor_div(int a, int b) { return a / b - (a % b != 0 && ((a < 0) != (b < 0))); }

static int floor_mod(int a, int b) { return a - b * floor_div(a, b); }

/* where index 0 sits: absolute puts the bottom left pad on a fixed note (C), whatever the key */
static int origin_of(const struct jln *s, const struct jln_scale *sc)
{
    int origin = 0, first = s->key;                            /* scale[0]: the key's note */
    if (s->absolute) {
        if (s->layout == JLN_CHROMATIC) {
            origin = -first;
        } else {
            for (int k = 0; k < sc->n; k++)
                if (first + sc->iv[k] >= 12) {
                    origin = k - sc->n;
                    break;
                }
        }
    }
    return origin;
}

/* the pad's index along the scale, LP95's steps[0] * (origin[0] + x) + steps[1] * (origin[1] + y) */
static int pad_index(const struct jln *s, const struct jln_scale *sc, uint8_t x, uint8_t y)
{
    int chromatic = s->layout == JLN_CHROMATIC, origin = origin_of(s, sc), step_x, step_y, ox, oy;
    if (chromatic) {                                           /* orientation fixed: a row up is a fourth */
        step_x = 1;
        step_y = CHROMATIC_ROW_STEP;
        ox = origin;
        oy = 0;
    } else if (s->horizontal) {
        step_x = 1;
        step_y = DIATONIC_ROW_STEP;
        ox = origin;
        oy = 0;
    } else {
        step_x = CHORDS_COLUMN_STEP;
        step_y = 1;
        ox = 0;
        oy = origin;
    }
    return step_x * (ox + x) + step_y * (oy + y);
}

static const uint8_t MAJOR_IV[7] = {0, 2, 4, 5, 7, 9, 11};
static const uint8_t MINOR_IV[7] = {0, 2, 3, 5, 7, 8, 10};      /* natural minor */
#define MAJOR_N 7

/* the note is key + one of the list's intervals */
static int in_list(const uint8_t *iv, int n, int key, int note)
{
    for (int i = 0; i < n; i++)
        if (note == key + iv[i])
            return 1;
    return 0;
}

/* the note `index` steps along the scale (chromatic: semitones) from the layout's origin */
static void pad_from_index(const struct jln *s, const struct jln_scale *sc, int index, struct jln_pad *out)
{
    int chromatic = s->layout == JLN_CHROMATIC;
    int size = chromatic ? CHROMATIC_SIZE : sc->n;
    int k = floor_mod(index, size);
    int note_in_octave = chromatic ? s->key + k : s->key + sc->iv[k];
    int midi = (s->octave + 1) * 12 + 12 * floor_div(index, size) + note_in_octave;
    const uint8_t *ref = sc->iv;       /* the reference the colours are relative to: the current scale, but for Chromatic the major scale of the key */
    int ref_n = sc->n, ref_major;
    out->note = (int16_t)(midi >= 0 && midi < 128 ? midi : -1);
    out->root = note_in_octave == s->key;
    if (sc->n == CHROMATIC_SIZE) {     /* the Digitone's Chromatic: every note is in it, so nothing would stand out: major is the reference */
        ref = MAJOR_IV;
        ref_n = MAJOR_N;
    }
    ref_major = ref_n == MAJOR_N && !memcmp(ref, MAJOR_IV, MAJOR_N);
    out->in_scale = in_list(ref, ref_n, s->key, note_in_octave);
    /* Launchpad 95 (ScaleComponent.note): highlight = note == scale[2] or note == scale[4], the third and fifth STEP of the scale (a scale with fewer steps has no such note) */
    out->highlight = (ref_n > 2 && note_in_octave == s->key + ref[2]) || (ref_n > 4 && note_in_octave == s->key + ref[4]);
    out->minor_only = ref_major && !out->in_scale && in_list(MINOR_IV, MAJOR_N, s->key, note_in_octave);
}

void jln_pad(const struct jln *s, const struct jln_scale *sc, uint8_t x, uint8_t y, struct jln_pad *out)
{
    pad_from_index(s, sc, pad_index(s, sc, x, y), out);
}

void jln_key(const struct jln *s, const struct jln_scale *sc, unsigned i, struct jln_pad *out)
{
    struct jln t = *s;
    if (t.layout == JLN_CHORDS)        /* the chords layout's notes are the scale's steps: the keyboard has no chords */
        t.layout = JLN_DIATONIC;
    pad_from_index(&t, sc, origin_of(&t, sc) + (int)i, out);
}

uint64_t jln_pads_with_note(const struct jln *s, const struct jln_scale *sc, int note)
{
    uint64_t mask = 0;
    if (note < 0 || note > 127)
        return 0;
    for (uint8_t y = 0; y < 8; y++)
        for (uint8_t x = 0; x < 8; x++) {
            struct jln_pad p;
            jln_pad(s, sc, x, y, &p);
            if (p.note == note)
                mask |= 1ull << (y * 8u + x);
        }
    return mask;
}

uint8_t jln_color(const struct jln_pad *p, int playing)
{
    if (p->note < 0)
        return 0;
    if (playing)
        return JLN_COLOR_PLAYING;
    if (p->root)
        return JLN_COLOR_ROOT;
    if (p->highlight)
        return JLN_COLOR_HIGHLIGHT;
    if (p->in_scale)
        return JLN_COLOR_IN_SCALE;
    return p->minor_only ? JLN_COLOR_MINOR_ONLY : JLN_COLOR_OUT_OF_SCALE;
}
