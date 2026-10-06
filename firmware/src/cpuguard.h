/* SPDX-License-Identifier: GPL-3.0-only */
/* The predictive CPU guard (FELUCCA_CPU_GUARD, cpuguard.c; docs/CPU-GUARD.md): its state and what the render
 * code reads. After Flowstate's guard (guard.c, docs/guardrails.md 2.5) by Zakaria Chowdhury, GPL-3.0-only.
 * Without the switch CG_LEVEL is the constant 0 and every use below folds away: the build is the one before. */
#pragma once
#include <stdint.h>

/* the levels, in the order they ease back: engine quality (ACID without oversampling, ANALOG 2's swarm at 2
 * copies), then UNISON parts at 2 voices, then voices shed (never from a MONO / LEGATO / UNISON part: the bass
 * and the lead; never the drums) */
enum { CG_L_OFF, CG_L_QUALITY, CG_L_UNISON, CG_L_NOTES };
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
    uint32_t gsum;
    uint32_t steps, early, sheds; /* steps up (early: on a prediction, before the half), voices shed */
} cg_t;
static cg_t cg;
#define CG_LEVEL ((uint32_t)cg.level)
#else
#define CG_LEVEL 0u
#endif
/* the render's limits: the swarm's copies and the UNISON voices (2 from their levels on) */
#define CG_SWARM(n) (CG_LEVEL >= CG_L_QUALITY && (n) > 2u ? 2u : (n))
#define CG_UNI(n) (CG_LEVEL >= CG_L_UNISON && (n) > 2u ? 2u : (n))
