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
#include "acid/eng_acid.c"                    /* ACID (from X0X; Open303 parts MIT): engine 12 */
#endif
#if FELUCCA_ENG_NOISE && !FELUCCA_ANALOG2
#error "the NOISE engine takes its engine number (15) from the ANALOG 2 build"
#endif
#if FELUCCA_ENG_NOISE
#include "noise/eng_noise.c"                  /* NOISE (Felucca-Engines eng_noise.c, MIT; this file MIT-headed): engine 15 */
#endif
#if FELUCCA_ENG_DRONE && !FELUCCA_ANALOG2
#error "the DRONE engine takes its engine number (16) from the ANALOG 2 build"
#endif
#if FELUCCA_ENG_DRONE
#include "drone/eng_drone.c"                  /* DRONE (drones, ambient, binaural beats): engine 16 */
#endif
#if FELUCCA_ENG_PLAITS && !FELUCCA_ANALOG2
#error "the PLAITS engine takes its engine number (17) from the ANALOG 2 build"
#endif
#if FELUCCA_ENG_PLAITS
#include "plaits/eng_plaits.c"                /* PLAITS (Mutable Instruments Plaits' models, MIT; this file MIT-headed): engine 17 */
#endif
#if (FELUCCA_ENG_STRINGS || FELUCCA_ENG_VPS || FELUCCA_ENG_TERRAIN || FELUCCA_ENG_WSEQ || FELUCCA_ENG_VECTOR) && !FELUCCA_ANALOG2
#error "Clipper's engines take their engine numbers (18..22) from the ANALOG 2 build"
#endif
#if FELUCCA_ENG_STRINGS
#include "clipper/eng_strings.c"              /* STRINGS (Clipper's string machine): engine 18 */
#endif
#if FELUCCA_ENG_VPS
#include "clipper/eng_vps.c"                  /* VPS (Clipper's vector phaseshaping): engine 19 */
#endif
#if FELUCCA_ENG_TERRAIN
#include "clipper/eng_terrain.c"              /* TERRAIN (Clipper's wave terrain): engine 20 */
#endif
#if FELUCCA_ENG_WSEQ
#include "clipper/eng_wseq.c"                 /* WSEQ (Clipper's wave sequencing): engine 21 */
#endif
#if FELUCCA_ENG_VECTOR
#include "clipper/eng_vector.c"               /* VECTOR (Clipper's vector synthesis): engine 22 */
#endif
#if FELUCCA_ENG_SID && !FELUCCA_ANALOG2
#error "the SID engine takes its engine number (23) from the ANALOG 2 build"
#endif
#if FELUCCA_ENG_SID
#include "sid/eng_sid.c"                      /* SID (reSID 0.16 via Mini-Tone): engine 23 */
#endif
#if FELUCCA_ENG_PROPHET && !FELUCCA_ANALOG2
#error "the PROPHET engine takes its engine number (24) from the ANALOG 2 build"
#endif
#if FELUCCA_ENG_PROPHET
#include "prophet/eng_prophet.c"              /* PROPHET (a Prophet-5 style voice, Melodee's DSP): engine 24 */
#endif
#if FELUCCA_ENG_WAVE && !FELUCCA_ANALOG2
#error "the WAVE engine takes its engine number (25) from the ANALOG 2 build"
#endif
#if FELUCCA_ENG_WAVE
#include "clipper/eng_wave.c"                 /* WAVE (Clipper's wavetables): engine 25 */
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
#if FELUCCA_ENG_PLAITS
    pl_voice_t plaits[PL_POLY];
#endif
    uint32_t none;
} part_mem_t;
#define PART_MEM_ANY (FELUCCA_ENG_FM6 || FELUCCA_ENG_PHYS || FELUCCA_ENG_GRAIN || FELUCCA_ENG_ACID || FELUCCA_ENG_PLAITS)
/* the memories live in the arena (core/arena.c, docs/SD-MODE-DESIGN.md 6b): by default NPART x the largest, placed at
 * fixed slots (ARN_SLOT): the same room and the same places as the fixed memories had, nothing ever refused.
 * FELUCCA_ARENA_BYTES smaller: placed by need, a change that does not fit refused (part_mem_reserve) */
#define ARN_SLOT (PART_MEM_ANY ? (sizeof(part_mem_t) + 7u) & ~7u : 8u)
#ifndef FELUCCA_ARENA_BYTES
#define ARN_BYTES (NPART * ARN_SLOT)
#define ARN_FIXED 1
#else
#define ARN_BYTES FELUCCA_ARENA_BYTES
#endif
/* FELUCCA_FX_ARENA with an arena placed by need: the effects' lines (the chorus, delay and reverb sends', the inserts'
 * own room) are blocks of it too, after the parts' (fx/fx_arena.c: a line placed while its effect is used); never
 * named by arn_drop_for (a part's change proposes parts only). Else the lines stay where they were (fx.c) */
#if FELUCCA_FX_ARENA && !defined(FELUCCA_ARENA_BYTES)
#error "FELUCCA_FX_ARENA: the effects' lines in an arena placed by need, which needs FELUCCA_ARENA_BYTES"
#endif
#ifdef FELUCCA_ARENA_BYTES
#define FXM_NEED (FELUCCA_FX_ARENA != 0)
#else
#define FXM_NEED 0
#endif
#if FXM_NEED
#define FXM_NC 4u                                        /* the effects' blocks (fx_arena.c FXM_CHO DLY REV INS) */
#define ARN_PARTS (NPART + FXM_NC)
#define ARN_DROPPABLE ((1u << NPART) - 1u)
#define ARN_TOPS (((1u << FXM_NC) - 1u) << NPART)        /* (the lines from the top, best fit: core/arena.c) */
#else
#define ARN_PARTS NPART
#endif
#include "../core/arena.c"
#if ARN_FIXED                                            /* part p's memory: its slot, from power-on (as the fixed array) */
#define PART_MEM_AT(p) ((void *)(arn_mem + (p) * ARN_SLOT))
#else                                                    /* placed by need: where its switch took it (0 before) */
static void *part_mem_p[NPART];
#define PART_MEM_AT(p) part_mem_p[p]
#endif
static uint8_t part_mem_owner[NPART];                    /* the engine slot + 1 whose state it holds, 0 = none */
AINL fm6_mem_t *fm6_mem(uint32_t p)
{
#if FELUCCA_ENG_FM6
    return (fm6_mem_t *)PART_MEM_AT(p);
#else
    (void)p;
    return 0;
#endif
}
AINL gr_part_t *gr_mem(uint32_t p)
{
#if FELUCCA_ENG_GRAIN
    return (gr_part_t *)PART_MEM_AT(p);
#else
    (void)p;
    return 0;
#endif
}
#if FELUCCA_ENG_PHYS
AINL phys_slot_t *phys_mem(uint32_t p) { return (phys_slot_t *)PART_MEM_AT(p); }
#endif
#if FELUCCA_ENG_ACID
void *acid_mem(int k) { return PART_MEM_AT((uint32_t)k % NPART); }
#endif
#if FELUCCA_ENG_PLAITS
AINL pl_voice_t *pl_mem(uint32_t p) { return (pl_voice_t *)PART_MEM_AT(p); }
#endif

/* the engines built, in UID order (registry.h ENGINE_LIST): the slots */
#define ENG_PTR_(u, N, fb, s) FIF(FELUCCA_ENG_##N)(&ENG_##N,)
#define ENG_IX_FM6 ((uint32_t)ENG_SLOT_FM6)   /* FM6's slot (bench.c, the tests), 0xFF when not built */
static const engine_t *const ENGINES[NENGINES] = {ENGINE_LIST(ENG_PTR_)};
#define ENG_IX_NOISE ((uint32_t)ENG_SLOT_NOISE)
#define ENG_IX_DRONE ((uint32_t)ENG_SLOT_DRONE)
#define ENG_IX_PLAITS ((uint32_t)ENG_SLOT_PLAITS)
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
 * that fits the sound (tools/level_presets.py writes preset_trim.h, a row per engine UID, every engine);
 * a track keeps it in P_ED_FX */
#include "preset_trim.h"
static int16_t preset_trim(uint32_t uid, uint32_t pi)    /* by engine UID */
{
#if SMP_TRIM_SHIFTED                                     /* SAMPLE with sets left out: its presets moved up */
    if (uid == 4u)                                       /* (tools/gen_samples.py SMP_TRIM_IX) */
        pi = pi < sizeof SMP_TRIM_IX ? SMP_TRIM_IX[pi] : 255u;
#endif
    return uid < PT_ENGINES && pi < PT_MAX ? PRESET_TRIM[uid][pi] : 0;
}

/* the bytes engine slot e keeps in its part's memory (0: none) */
static uint32_t part_mem_bytes(uint32_t e)
{
    e %= NENGINES;
#if FELUCCA_ENG_FM6
    if (e == (uint32_t)ENG_SLOT_FM6)
        return sizeof(fm6_mem_t);
#endif
#if FELUCCA_ENG_PHYS
    if (e == (uint32_t)ENG_SLOT_PHYS)
        return sizeof(phys_slot_t) * PHYS_POLY;
#endif
#if FELUCCA_ENG_GRAIN
    if (e == (uint32_t)ENG_SLOT_GRAIN)
        return sizeof(gr_part_t);
#endif
#if FELUCCA_ENG_ACID
    if (e == (uint32_t)ENG_SLOT_ACID)
        return ACID_MEM_BYTES;
#endif
#if FELUCCA_ENG_PLAITS
    if (e == (uint32_t)ENG_SLOT_PLAITS)
        return sizeof(pl_voice_t) * PL_POLY;
#endif
    return 0;
}

/* a refused change (the user, 2026-10-25: refuse, and propose what to drop; the UI shows it and offers the drops).
 * An edit's (one part: the engine it asked for, arn_drop_for's proposal; the UI's one confirm, op_input.c mem_tick), and
 * a load's apart (every part a project's load could not place, lmask, with the engine each wants, leng: each a silent
 * placeholder meanwhile, the UI's list of what to drop, op_memlist.c; one load's refusals never overwrite each other) */
static struct {
    uint8_t on, part, eng;
    int8_t drop;                                         /* arn_drop_for's parts (a bit mask; -1 none would make room) */
    uint8_t asked;                                       /* the parts already proposed (model.c mem_offer: never twice) */
    uint8_t waiting;                                     /* the parts dropped, their switch to the placeholder not done yet */
    uint8_t lmask;                                       /* a load's parts refused (a bit by part): placeholders, waiting */
    uint8_t lseq;                                        /* + 1 at each load (the UI's list starts afresh) */
    uint8_t leng[NPART];                                 /* .. the engine slot each wants */
} part_mem_refused;
#define PART_MEM_SHORT() (part_mem_refused.on || part_mem_refused.lmask)   /* (the effects wait: a part's sound first) */

/* part k's memory for engine e set aside (the interrupts off: a section applied inside the audio ISR reaches here
 * through the project's apply); 0 ok or none needed, -1 no room. Nothing recorded */
static int part_mem_try(uint32_t k, uint32_t e)
{
    uint32_t n = part_mem_bytes(e);
    int r;
    if (k >= NPART || !n || (part_mem_owner[k] == e % NENGINES + 1u && arn_ptr(k)))
        return 0;                                        /* (none needed, or its own state still there; an idle one
                                                          * another reservation takes back first: the switch finds no
                                                          * room, part_mem_unplaced) */
    {
        IRQ_SAVE(ie);
        r = arn_reserve(k, n);
        IRQ_RESTORE(ie);
    }
    return r;
}

/* the main loop, before part k's engine becomes e (model.c set_engine_of, the user preset loads): its memory set
 * aside. 0 ok; -1 no room (only with a smaller FELUCCA_ARENA_BYTES): part_mem_refused says what would make room */
static int part_mem_reserve(uint32_t k, uint32_t e)
{
    int r = part_mem_try(k, e);
    if (r) {
        if (!part_mem_refused.on || part_mem_refused.part != k)   /* (a new refusal: nothing asked yet) */
            part_mem_refused.asked = part_mem_refused.waiting = 0;
        part_mem_refused.on = 1, part_mem_refused.part = (uint8_t)k, part_mem_refused.eng = (uint8_t)(e % NENGINES);
        part_mem_refused.drop = (int8_t)arn_drop_for(k, part_mem_bytes(e));
    }
    return r;
}

/* a project's load (storage/project.c, before its tracks): the refusals of before forgotten (an edit's change waiting
 * included: the load replaces the parts) */
static void part_mem_load_begin(void)
{
    part_mem_refused.on = part_mem_refused.asked = part_mem_refused.waiting = 0;
    part_mem_refused.lmask = 0;
    part_mem_refused.lseq++;
}
/* .. part k's engine e on that load: 0 placed (or none needed); -1 refused, recorded with the others (the caller makes
 * the part a placeholder until the UI's list finds it room, op_memlist.c) */
static int part_mem_load(uint32_t k, uint32_t e)
{
    if (part_mem_try(k, e))
        return part_mem_refused.lmask |= (uint8_t)(1u << k), part_mem_refused.leng[k] = (uint8_t)(e % NENGINES), -1;
    if (k < NPART)
        part_mem_refused.lmask &= (uint8_t)~(1u << k);
    return 0;
}

static int part_mem_claim(uint32_t k, uint32_t e);

/* the engine a silent placeholder sits on: the first built that keeps nothing in the arena (its memory freed) */
#define ENG_UID_SILENT 0xFEu                             /* a project's engine byte for a silent placeholder (project.c) */
static uint32_t eng_silent_slot(void)
{
    uint32_t e;
    for (e = 0; e < NENGINES; e++)
        if (!part_mem_bytes(e))
            return e;
    return 0;
}

/* (the audio, voice.c engine_block) part k's switch to engine e found no room: nobody reserved for it, or its own idle
 * state was taken back by another part's reservation since. The part a silent placeholder, recorded as a load's
 * refusal (the UI's list offers what to drop; it never keeps its old engine with the new sound's values) */
static void part_mem_unplaced(track_t *t, uint32_t e)
{
    uint32_t k = (uint32_t)(t - trk);
    if (k >= NPART)
        return;
    part_mem_refused.lmask |= (uint8_t)(1u << k);
    part_mem_refused.leng[k] = (uint8_t)(e % NENGINES);
    t->engine = t->eng_req = (uint8_t)eng_silent_slot();
    t->silent = 1;
    (void)part_mem_claim(k, t->engine);                  /* (its block idle: free for the others) */
}

/* at power-on, before the audio (main.c felucca_init): each part's first engine gets its memory (an arena placed by
 * need; the fixed slots are there from the start) */
static void part_mem_boot(void)
{
#if !ARN_FIXED
    uint32_t k;
    for (k = 0; k < NPART; k++)
        if (part_mem_bytes(trk[k].engine))
            (void)part_mem_claim(k, trk[k].engine);
#endif
}

/* part k switched to engine slot e (voice.c engine_block, every voice of the part over): the part's engine memory
 * for e (part_mem above). To an engine that uses it from another that did: zeroed (as at power-on: each engine
 * builds its state from zeros) and the engine told; to one that keeps nothing here: left, idle (back finds it) */
static int part_mem_claim(uint32_t k, uint32_t e)
{
    uint32_t n, i, *w;
    e %= NENGINES;
    n = part_mem_bytes(e);
    if (k >= NPART)
        return 0;
    if (!n) {
        arn_rest(k);
        return 0;
    }
    if (part_mem_owner[k] == e + 1u && arn_ptr(k) && !arn_has_next[k]) {
        (void)arn_take(k);                               /* (its own state, as it was) */
        return 0;
    }
    if (!arn_has_next[k] && arn_reserve(k, n))           /* (a switch nobody reserved for: here; the default arena */
        return -1;                                       /* never refuses; a smaller one: the caller keeps the old one) */
    w = (uint32_t *)arn_take(k);                         /* another engine's state (or none): from zeros */
#if !ARN_FIXED
    part_mem_p[k] = w;
#endif
    for (i = 0; i < n / 4u; i++)
        w[i] = 0;
#if FELUCCA_ENG_ACID
    acid_forget((int)k);
#endif
#if FELUCCA_ENG_FM6
    fm6_pt[k].lav = fm6_pt[k].mcur = fm6_pt[k].mlav = fm6_pt[k].mnew = fm6_pt[k].mlive = 0;   /* (into those) */
#endif
    part_mem_owner[k] = (uint8_t)(e + 1u);
    return 0;
}

#include "../core/overlay.c"                  /* the RAMTEXT overlays: the engines' hot code in a slot per part (FELUCCA_OVERLAY) */
