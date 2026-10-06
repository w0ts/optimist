/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Engine table: order = PRESETS browsing order (and the engine numbers of the editor protocol). */
#include "dsp.c"
#include "eng_analog.c"
#include "eng_digital.c"
#include "eng_phase.c"
#include "eng_lofi.c"
#include "eng_sample.c"
#include "eng_formant.c"
#include "eng_trio.c"
#include "eng_drawbar.c"
#include "eng_grain.c"
#if !FELUCCA_ANALOG2
#include "eng_super.c"                   /* (ANALOG 2: its swarm, eng_analog2.c) */
#endif
#include "eng_fm6.c"
#if FELUCCA_SLICE
#include "eng_slice.c"
#endif
#if (FELUCCA_ENG_PHYS || FELUCCA_ENG_ACID) && !FELUCCA_ANALOG2
#error "the backported engines take the engine numbers of the ANALOG 2 build (11, 12)"
#endif
#if FELUCCA_ENG_PHYS
#include "eng_phys.c"                    /* PHYS (from Felucca 1.0; DaisySP / Rings parts MIT): engine 11 */
#endif
#if FELUCCA_ENG_ACID
#include "eng_acid.c"                    /* ACID (from X0X; Open303 parts MIT): engine 12, EXPERIMENTAL */
#endif

static const engine_t *const ENGINES[NENGINES] = {&ENG_ANALOG, &ENG_DIGITAL, &ENG_PHASE, &ENG_LOFI, &ENG_SAMPLE,
                                                    &ENG_FORMANT, &ENG_TRIO, &ENG_DRAWBAR, &ENG_GRAIN,
#if !FELUCCA_ANALOG2
                                                    &ENG_SUPER,
#endif
                                                    &ENG_FM6,
#if FELUCCA_SLICE
                                                    &ENG_SLICE,
#elif FELUCCA_ENG_PHYS || FELUCCA_ENG_ACID
                                                    &ENG_SAMPLE,   /* 10: SLICE's number, kept free (eng_free) */
#endif
#if FELUCCA_ENG_PHYS
                                                    &ENG_PHYS,     /* 11 */
#elif FELUCCA_ENG_ACID
                                                    &ENG_ANALOG,   /* 11: PHYS's number, kept free (eng_free) */
#endif
#if FELUCCA_ENG_ACID
                                                    &ENG_ACID,     /* 12 */
#endif
};
#if FELUCCA_ENG_PHYS || FELUCCA_ENG_ACID
/* The backported engines keep their numbers in every build (projects, user presets and the editor store the
 * number): 10 SLICE, 11 PHYS, 12 ACID. A number of an engine left out holds a stand-in (SLICE: SAMPLE, PHYS:
 * ANALOG; what a project that names it plays), which the engine knob steps over */
#define ENG_IX_PHYS 11u
#define ENG_IX_ACID 12u
static int eng_free(uint32_t e)
{
    return (!FELUCCA_SLICE && e == 10u) || (!FELUCCA_ENG_PHYS && e == ENG_IX_PHYS);
}
#endif

#if FELUCCA_ANALOG2
#define ENG_IX_FM6 9u                    /* ENGINES[] index of FM6 (the preset list, ui.c BANK); the DX7 engine's
                                         * slot: projects that played DX7 play FM6 */
#define ENG_IX_SUPER 0u                  /* the superwave presets: ANALOG's (its swarm) */
#else
#define ENG_IX_FM6 10u                   /* ENGINES[] index of FM6 (the preset list, ui.c BANK); the DX7 engine's
                                         * slot: projects that played DX7 play FM6 */
#define ENG_IX_SUPER 9u                  /* the superwave presets: SUPER's */
#endif
_Static_assert(ENG_IX_FM6 < NENGINES, "FM6 in the engine table");

/* every factory sound as loud as the others: a level trim per preset, 1/2 dB, measured on a phrase
 * that fits the sound (tools/level_presets.py writes preset_trim.h); a track keeps it in P_ED_FX.
 * (ANALOG 2: row 9 is SUPER's, no engine now; FM6, engine 9, has no trims, as before. ANALOG's presets
 * past the table bring their trim in A2_PX) */
#include "preset_trim.h"
static int16_t preset_trim(uint32_t e, uint32_t pi)
{
#if FELUCCA_ANALOG2
    return e < ENG_IX_FM6 && pi < PT_MAX ? PRESET_TRIM[e][pi] : 0;
#else
    return e < PT_ENGINES && pi < PT_MAX ? PRESET_TRIM[e][pi] : 0;
#endif
}
