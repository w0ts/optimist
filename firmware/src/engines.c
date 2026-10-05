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

/* the engines built, in UID order (registry.h ENGINE_LIST): the slots */
#define ENG_PTR_(u, N, fb, s) FIF(FELUCCA_ENG_##N)(&ENG_##N,)
#define ENG_IX_FM6 ((uint32_t)ENG_SLOT_FM6)   /* FM6's slot (bench.c, the tests), 0xFF when not built */
static const engine_t *const ENGINES[NENGINES] = {ENGINE_LIST(ENG_PTR_)};

/* every factory sound as loud as the others: a level trim per preset, 1/2 dB, measured on a phrase
 * that fits the sound (tools/level_presets.py writes preset_trim.h); a track keeps it in P_ED_FX.
 * (ANALOG 2: row 9 is SUPER's, no engine now; FM6, engine 9, has no trims, as before. ANALOG's presets
 * past the table bring their trim in A2_PX) */
#include "preset_trim.h"
static int16_t preset_trim(uint32_t uid, uint32_t pi)    /* by engine UID */
{
#if FELUCCA_ANALOG2
    return uid < ENG_UID_FM6 && pi < PT_MAX ? PRESET_TRIM[uid][pi] : 0;
#else
    return uid < PT_ENGINES && pi < PT_MAX ? PRESET_TRIM[uid][pi] : 0;
#endif
}
