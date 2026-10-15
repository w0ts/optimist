/* SPDX-License-Identifier: GPL-3.0-only */
/* The predictive CPU guard (FELUCCA_CPU_GUARD, cpuguard.c; docs/CPU-GUARD.md): its state and what the render
 * code reads. After Flowstate's guard (guard.c, docs/guardrails.md 2.5) by Zakaria Chowdhury, GPL-3.0-only.
 * Without the switch CG_LEVEL is the constant 0 and every use below folds away: the build is the one before. */
#pragma once
#include <stdint.h>

/* the levels, in the order they ease back: engine quality (ACID without oversampling, ANALOG 2's swarm at 2
 * copies), then UNISON parts at 2 voices, then the reverb off (its wet fades out, its tank stops: fx.c rev_guard;
 * the reverb costs as much as the rest of an idle mix, and a tail goes on with nothing left to shed), then voices
 * shed (never from a MONO / LEGATO / UNISON part: the bass and the lead; never the drums) */
enum { CG_L_OFF, CG_L_QUALITY, CG_L_UNISON, CG_L_REVERB, CG_L_NOTES };
#define CG_TOP CG_L_NOTES

#if FELUCCA_CPU_GUARD
typedef struct {
    uint8_t level;               /* CG_L_*: what is eased back now */
    uint8_t over;                /* halves in a row over the ceiling */
    uint8_t shed;                /* shed one voice before the next half */
    uint8_t gpend, gn;           /* measuring what the step to level gpend saved: halves so far */
    uint8_t gain[CG_TOP + 1];    /* the load each level saved (1/256 of a half), measured after its step */
    uint16_t under;              /* halves in a row under the release level, counting what the level saves */
    uint16_t ips;                /* instructions a sample at the measured clock (cpu_khz), 0 = not known */
    uint16_t est, meas, load;    /* the last half: predicted, measured, their larger; 1/256 of a half */
    uint16_t model;              /* the cost model's load at the last half's start (cpuguard_costs.h) */
    uint16_t gpre;               /* (the saving: the load before the step; gsum: the loads after it) */
    uint16_t smeas;              /* the measured load of the half before the last step (the console: what set it off) */
    uint16_t avg;                /* the measured load over the last ~8 halves (each held to a whole half): the load
                                  * before a step, so a spike's own half never counts as what a level saves */
    uint32_t gsum;
    uint32_t steps, early, sheds; /* steps up (early: on a prediction, before the half), voices shed */
    /* the reactive path (docs/CPU-GUARD.md "Spikes"): late halves among lighter ones, as a busy FM-1 shows them (other
     * interrupts, flash cache misses: a half 1.5 x the mean now and then), which neither 8 halves in a row over the ceiling
     * nor a late half after one over it ever catch */
    uint16_t spk;                /* the late halves' leaky count: + CG_SPK_HIT a late half, - 1 a half; CG_SPK_ON reacts */
    uint8_t late;                /* this half ran late: the DMA moved on while it rendered (audio.c sets it before cg_post) */
    uint8_t vcap;                /* the voices allowed while reacting at the top (0: NVOICE, no cap); a new note steals */
    uint16_t vcn;                /* halves since the cap was last lowered or raised */
    uint8_t rcap[CG_TOP + 1];    /* a level stepped up by a reaction: the load it may come off under (1/256), 0 = none */
    uint32_t reacts;             /* reactive steps / voice cuts */
} cg_t;
static cg_t cg;
static uint8_t cg_react = 1;     /* the reactive path on (tests/overload_stress.c turns it off to compare) */
#define CG_LEVEL ((uint32_t)cg.level)
#define CG_VBUDGET ((uint32_t)(cg.vcap ? cg.vcap : NVOICE))   /* the voices the parts share now (voice.c voice_room) */
#else
#define CG_LEVEL 0u
#define CG_VBUDGET NVOICE
#endif
/* the render's limits: the swarm's copies and the UNISON voices (2 from their levels on); a synthesised drum voice
 * ends under CG_DTAIL (Q15: 8, -72 dBFS; from quality on 327, -40 dBFS; drum_synth.c ds_alive) */
#define CG_SWARM(n) (CG_LEVEL >= CG_L_QUALITY && (n) > 2u ? 2u : (n))
#define CG_DTAIL (CG_LEVEL >= CG_L_QUALITY ? 327 : 8)
#define CG_UNI(n) (CG_LEVEL >= CG_L_UNISON && (n) > 2u ? 2u : (n))
