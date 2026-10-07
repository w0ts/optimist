/* SPDX-License-Identifier: GPL-3.0-only */
/* Backports from SLOOP 2.4 (isod89/sloop-fm1 v2.4, 8d3823f, GPL-3.0-only; on Felucca by Leo Kuroshita): the
 * sequencer features, one build switch each, as backports23.h (tools/backports.json: provenance, measured cost).
 * A switch at 0 builds exactly the firmware without it. Included by backports.h.
 *
 *   FELUCCA_DIV_LONG  SEQ DIV gains 1/2, 1BAR, 2BAR (appended: old values unchanged)         SLOOP 2.4
 *   FELUCCA_DLY_DOT   delay TIME gains 1/8D and 1/16D (appended)                              SLOOP 2.4
 *   FELUCCA_MICRO     micro timing: a step held + KNOB 4 nudges it, +-1/2 step in 1/64        SLOOP 2.4
 *   FELUCCA_FILLS     fills: a step held + OCT+ (normal / fill only / no fill); GLO + 9 a fill
 *                     while held, GLO + 10 the next bar; the FX bypass moves to GLO + black 1-4 SLOOP 2.4
 *   FELUCCA_PLOCK     parameter locks: 24 a track, a step held + PRESETS (value) / ALGORITHM
 *                     (parameter); back at the next step without one; beside motion recording SLOOP 2.4
 *   FELUCCA_QCHAIN    quick chain: SAVE held + up to 8 section taps, looped                   SLOOP 2.4 */
#ifndef FELUCCA_BACKPORTS24SEQ_H
#define FELUCCA_BACKPORTS24SEQ_H

#ifndef FELUCCA_DIV_LONG
#define FELUCCA_DIV_LONG 1       /* core.h div_units, seq.c grid_at, params.c N_SDIV */
#endif
#ifndef FELUCCA_DLY_DOT
#define FELUCCA_DLY_DOT 1        /* core.h dly_units, fx.c delay_samples, params.c N_DLY */
#endif
#ifndef FELUCCA_MICRO
#define FELUCCA_MICRO 0          /* seq.c seq_tick (the fire logic), ui_layers.c steps_held_edit */
#endif
#ifndef FELUCCA_FILLS
#define FELUCCA_FILLS 0          /* seq.c step_plays, events_block; ui_layers.c GLO keys 9 / 10, steps_held_fill */
#endif
#ifndef FELUCCA_PLOCK
#define FELUCCA_PLOCK 0          /* seq.c lock_step; ui_layers.c PRESETS / ALGORITHM with a step held */
#endif
#ifndef FELUCCA_QCHAIN
#define FELUCCA_QCHAIN 0         /* seq.c live_block chain_*; ui_layers.c chain_release (needs the arranger) */
#endif
#define SL24_STEPX (FELUCCA_MICRO || FELUCCA_FILLS || FELUCCA_PLOCK)   /* the per-step extras (micro, locks, fill) */
#if SL24_STEPX && !FELUCCA_SL24_XSTEP   /* (their storage, backports24.h: without it nothing of them would be saved) */
#undef FELUCCA_SL24_XSTEP
#define FELUCCA_SL24_XSTEP 1
#endif

#endif
