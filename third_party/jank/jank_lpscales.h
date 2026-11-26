/* SPDX-License-Identifier: GPL-3.0-only */
/* OPTIMIST'S OWN (not copied from jank: tools/sync_jank.sh leaves it): the Launchpad's scales are the FM-1's, in its order (seq/seq.c
 * SCALE_MASK, core/params.c N_SCALE: SCL's 16), so the scale page and the FM-1's SCL show the same list. jank's jank_lpscales.h
 * (the Digitone II's 36) has the same functions and the same JLS_* names; here they carry the FM-1's indices. */
#ifndef JANK_LPSCALES_H
#define JANK_LPSCALES_H
#include "jank_lpnote.h"
#define JLS_COUNT 16u
/* the places the page's relative-scale pad needs (major <-> natural / harmonic / melodic minor, the two pentatonics) */
enum jls_index {
    JLS_CHROMATIC = 0,                 /* CHR */
    JLS_MAJOR = 1,                     /* MAJ */
    JLS_MINOR = 2,                     /* MIN */
    JLS_MAJOR_PENTATONIC = 5,          /* PEN */
    JLS_MINOR_PENTATONIC = 6,          /* MPEN */
    JLS_HARMONIC_MINOR = 7,            /* HARM */
    JLS_MELODIC_MINOR = 11             /* MEL */
};
unsigned jls_count(void);
const struct jln_scale *jls_get(unsigned i);               /* NULL past the end */
int jls_index_of(const struct jln_scale *s);               /* its place in the list, -1 if it is not one of them */
unsigned jls_mask(unsigned i);                             /* the 12-bit interval mask of scale i (0 past the end) */
#endif
