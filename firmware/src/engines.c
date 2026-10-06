/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Engine table: the engines built (registry.h), in UID order = PRESETS browsing order; their slots are the engine
 * numbers of the editor protocol (INFO v6 adds each slot's UID). */
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
#if (FELUCCA_ENG_PHYS || FELUCCA_ENG_ACID || FELUCCA_ENG_CZ) && !FELUCCA_ANALOG2
#error "the backported engines take the engine numbers of the ANALOG 2 build (11, 12, 13)"
#endif
#if FELUCCA_ENG_CZ
#include "eng_cz.c"                      /* CZ (from Melodee 0.11; uPD933 parts BSD-3-Clause): engine 13 */
#endif
#if FELUCCA_ENG_PHYS
#include "eng_phys.c"                    /* PHYS (from Felucca 1.0; DaisySP / Rings parts MIT): engine 11 */
#endif
#if FELUCCA_ENG_ACID
#include "eng_acid.c"                    /* ACID (from X0X; Open303 parts MIT): engine 12, EXPERIMENTAL */
#endif

/* the engines built, in UID order (registry.h ENGINE_LIST): the slots */
#define ENG_PTR_(u, N, fb, s) FIF(FELUCCA_ENG_##N)(&ENG_##N,)
#define ENG_IX_FM6 ((uint32_t)ENG_SLOT_FM6)   /* FM6's slot (bench.c, the tests), 0xFF when not built */
static const engine_t *const ENGINES[NENGINES] = {ENGINE_LIST(ENG_PTR_)};
#define ENG_IX_PHYS ((uint32_t)ENG_SLOT_PHYS)  /* (the tests) */
#define ENG_IX_ACID ((uint32_t)ENG_SLOT_ACID)
#define ENG_IX_CZ ((uint32_t)ENG_SLOT_CZ)
static int eng_free(uint32_t e) { (void)e; return 0; }   /* (registry.h: no stand-ins, the slots are dense) */

/* a factory preset this build can play: a SAMPLE / GRAIN preset needs its sample set (a set left out of the
 * build has no zones: tools/gen_samples.py), the UI's list leaves the others out */
static int preset_playable(const engine_t *e, uint32_t pi)
{
    uint32_t s;
    if (!ENG_IS(e, SAMPLE) && !ENG_IS(e, GRAIN))
        return 1;
    s = (uint32_t)e->presets[pi].e[0];
    return s >= SMP_NSETS || SMP_SETS[s].nz != 0;
}

/* every factory sound as loud as the others: a level trim per preset, 1/2 dB, measured on a phrase
 * that fits the sound (tools/level_presets.py writes preset_trim.h); a track keeps it in P_ED_FX.
 * (ANALOG 2: row 9 is SUPER's, no engine now; FM6, engine 9, has no trims, as before. ANALOG's presets
 * past the table bring their trim in A2_PX) */
#include "preset_trim.h"
static int16_t preset_trim(uint32_t uid, uint32_t pi)    /* by engine UID */
{
#if SMP_TRIM_SHIFTED                                     /* SAMPLE with sets left out: its presets moved up */
    if (uid == 4u)                                       /* (tools/gen_samples.py SMP_TRIM_IX) */
        pi = pi < sizeof SMP_TRIM_IX ? SMP_TRIM_IX[pi] : 255u;
#endif
#if FELUCCA_ANALOG2
    return uid < ENG_UID_FM6 && pi < PT_MAX ? PRESET_TRIM[uid][pi] : 0;
#else
    return uid < PT_ENGINES && pi < PT_MAX ? PRESET_TRIM[uid][pi] : 0;
#endif
}
