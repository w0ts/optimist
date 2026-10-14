/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Engine table: the engines built (registry.h), in UID order = PRESETS browsing order; their slots are the engine
 * numbers of the editor protocol (INFO v6 adds each slot's UID). */
#include "../dsp/dsp.c"
#include "analog/eng_analog.c"
#include "digital/eng_digital.c"
#include "phase/eng_phase.c"
#include "lofi/eng_lofi.c"
#include "sample/eng_sample.c"
#include "formant/eng_formant.c"
#include "trio/eng_trio.c"
#include "drawbar/eng_drawbar.c"
#include "grain/eng_grain.c"
#if !FELUCCA_ANALOG2
#include "super/eng_super.c"                   /* (ANALOG 2: its swarm, eng_analog2.c) */
#endif
#include "fm6/eng_fm6.c"
#if FELUCCA_SLICE
#include "slice/eng_slice.c"
#endif
#if (FELUCCA_ENG_PHYS || FELUCCA_ENG_ACID || FELUCCA_ENG_CZ || FELUCCA_ENG_JUNO) && !FELUCCA_ANALOG2
#error "the backported engines take the engine numbers of the ANALOG 2 build (11, 12, 13, 14)"
#endif
#if FELUCCA_ENG_CZ
#include "cz/eng_cz.c"                      /* CZ (from Melodee 0.11; uPD933 parts BSD-3-Clause): engine 13 */
#endif
#if FELUCCA_ENG_JUNO
#include "juno/eng_juno.c"                  /* JUNO (fm-106 / KR-106's Juno-106 voice): engine 14 */
#endif
#if FELUCCA_ENG_PHYS
#include "phys/eng_phys.c"                    /* PHYS (from Felucca 1.0; DaisySP / Rings parts MIT): engine 11 */
#endif
#if FELUCCA_ENG_ACID
#include "acid/eng_acid.c"                    /* ACID (from X0X; Open303 parts MIT): engine 12, EXPERIMENTAL */
#endif

/* The part's engine memory: the running state an engine keeps per part beyond its voices (FM6's voices and MONO
 * keys, PHYS's model slots, GRAIN's grains and seek index, ACID's 303). A part plays one engine at a time, so one
 * memory per part holds whichever of them it plays: the largest of the engines built, not their sum (pool: the
 * user's FM6 + PHYS build keeps 11,712 B; ACID's 1,416 B of main RAM go). voice.c engine_block claims it when the
 * part switches engine (all its voices over, CPU0 before the second core's job): to an engine that uses it from
 * another that did, it is zeroed (as at power-on: each engine builds its state from zeros) and the engine told;
 * a switch to an engine that keeps nothing here leaves it, so ANALOG and back finds FM6's state as it was */
#if FELUCCA_ENG_ACID
#include "acid/acid_mem.h"
#endif
typedef union {
#if FELUCCA_ENG_FM6
    fm6_mem_t fm6;
#endif
#if FELUCCA_ENG_PHYS
    phys_slot_t phys[PHYS_POLY];
#endif
#if FELUCCA_ENG_GRAIN
    gr_part_t grain;
#endif
#if FELUCCA_ENG_ACID
    uint32_t acid[ACID_MEM_BYTES / 4u];
#endif
    uint32_t none;
} part_mem_t;
#define PART_MEM_ANY (FELUCCA_ENG_FM6 || FELUCCA_ENG_PHYS || FELUCCA_ENG_GRAIN || FELUCCA_ENG_ACID)
static part_mem_t part_mem[PART_MEM_ANY ? NPART : 1] __attribute__((section(".pool"), aligned(8)));
static uint8_t part_mem_owner[NPART];                    /* the engine slot + 1 whose state it holds, 0 = none */
AINL fm6_mem_t *fm6_mem(uint32_t p)
{
#if FELUCCA_ENG_FM6
    return &part_mem[p].fm6;
#else
    (void)p;
    return 0;
#endif
}
AINL gr_part_t *gr_mem(uint32_t p)
{
#if FELUCCA_ENG_GRAIN
    return &part_mem[p].grain;
#else
    (void)p;
    return 0;
#endif
}
#if FELUCCA_ENG_PHYS
AINL phys_slot_t *phys_mem(uint32_t p) { return part_mem[p].phys; }
#endif
#if FELUCCA_ENG_ACID
void *acid_mem(int k) { return part_mem[(uint32_t)k % NPART].acid; }
#endif

/* the engines built, in UID order (registry.h ENGINE_LIST): the slots */
#define ENG_PTR_(u, N, fb, s) FIF(FELUCCA_ENG_##N)(&ENG_##N,)
#define ENG_IX_FM6 ((uint32_t)ENG_SLOT_FM6)   /* FM6's slot (bench.c, the tests), 0xFF when not built */
static const engine_t *const ENGINES[NENGINES] = {ENGINE_LIST(ENG_PTR_)};
#define ENG_IX_PHYS ((uint32_t)ENG_SLOT_PHYS)  /* (the tests) */
#define ENG_IX_ACID ((uint32_t)ENG_SLOT_ACID)
#define ENG_IX_CZ ((uint32_t)ENG_SLOT_CZ)
#define ENG_IX_JUNO ((uint32_t)ENG_SLOT_JUNO)
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

/* part k switched to engine slot e (voice.c engine_block, every voice of the part over): the part's engine memory
 * for e (part_mem above) */
static void part_mem_claim(uint32_t k, uint32_t e)
{
    int uses = 0;
    e %= NENGINES;
#if FELUCCA_ENG_FM6
    uses |= e == (uint32_t)ENG_SLOT_FM6;
#endif
#if FELUCCA_ENG_PHYS
    uses |= e == (uint32_t)ENG_SLOT_PHYS;
#endif
#if FELUCCA_ENG_GRAIN
    uses |= e == (uint32_t)ENG_SLOT_GRAIN;
#endif
#if FELUCCA_ENG_ACID
    uses |= e == (uint32_t)ENG_SLOT_ACID;
#endif
    if (!uses || k >= NPART || part_mem_owner[k] == e + 1u)
        return;
    {                                                    /* another engine's state (or none): from zeros, as at power-on */
        uint32_t i, *w = (uint32_t *)(void *)&part_mem[k];
        for (i = 0; i < sizeof part_mem[k] / 4u; i++)
            w[i] = 0;
#if FELUCCA_ENG_ACID
        acid_forget((int)k);
#endif
#if FELUCCA_ENG_FM6
        fm6_pt[k].lav = fm6_pt[k].mcur = fm6_pt[k].mlav = fm6_pt[k].mnew = fm6_pt[k].mlive = 0;   /* (into those) */
#endif
    }
    part_mem_owner[k] = (uint8_t)(e + 1u);
}
