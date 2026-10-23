/* SPDX-License-Identifier: GPL-3.0-only */
/* Backports from Felucca 1.5.6 (hugelton/Felucca 9f6a885, Leo Kuroshita (@kurogedelic), Hügelton Instruments,
 * GPL-3.0-only): one build switch each, as backports24seq.h (tools/backports.json: provenance, cost). A switch at 0
 * builds exactly the firmware without it. Included by backports.h, before backports24seq.h (FELUCCA_AUTO counts COND).
 *
 *   FELUCCA_COND      step conditions 1:2 2:2 1:3 2:3 3:3 1:4 2:4 3:4 4:4: a step plays on loop a of every b of its
 *                     track (Felucca 1.5.5 #209, seq.c seq_cond_miss). Here a step-only event of the automation store
 *                     (seq/auto.h AUTO_COND), on synth and drum steps (a drum step: every lane), beside FILL and
 *                     the chance: condition, then FILL, then the chance roll. The Optimist UI: the held step's EXTRA
 *                     page, COND (KNOB 4); SLOOP's UI plays them only. Builds the automation store
 *   FELUCCA_SEQTOOLS  the Optimist UI's STEP page TOOLS (REVERSE RANDOM COOK LENGTH x2 / half, QUANTIZE), Felucca
 *                     1.2 .. 1.5.5 ui_tools.c; the EDIT layer's SHIFT and LENGTH move the steps' events with them. UI=1
 *                     only (SLOOP's UI has none of it)
 *   FELUCCA_NOTEGEN   the note generators EUCLID (after Felucca's), RANDOM, PULSE (seq/gen.c), a layer played over the
 *                     pattern, on STEP's GEN and GEN OUT pages (ui/optimist/op_tools.c). Apart from SEQTOOLS since
 *                     2026-10-22 (the tools and the generators share nothing but the toast and the undo mark)
 *   FELUCCA_GENS      the generators' framework (the slots, the merge into what plays, GEN_TLV, WRITE, GEN and GEN OUT):
 *                     built with NOTEGEN or GEN_GRIDS, either one alone
 *   FELUCCA_GEN_GRIDS Mutable Instruments Grids' drum map (Emilie Gillet, pichenettes/eurorack, GPL-3.0-or-later;
 *                     seq/gen.c, seq/grids_map.h: 1.2 KB of table): a GEN TYPE beside EUCLID RANDOM PULSE, and the kit's
 *                     GRIDS on DR (STEP's GRIDS and DENSITY pages, the drum track's P_E2 .. P_E7). With or without NOTEGEN
 *   FELUCCA_GRIDS_EUCLID the kit's GRIDS in Grids' EUCLIDEAN output mode too (GRIDS > OUT: DRUMS / EUCL; seq/gen.c
 *                     gr_eu_hit): needs GEN_GRIDS */
#ifndef FELUCCA_BACKPORTS15_H
#define FELUCCA_BACKPORTS15_H

#ifndef FELUCCA_COND
#define FELUCCA_COND 0           /* seq/auto.c auto_step, cond_pass; ui/optimist/op_step.c the COND card */
#endif
#ifndef FELUCCA_SEQTOOLS
#define FELUCCA_SEQTOOLS 0       /* ui/optimist/op_tools.c (only the Optimist UI includes it: UI=0 builds the same) */
#endif
#ifndef FELUCCA_NOTEGEN
#define FELUCCA_NOTEGEN 0        /* seq/gen.c (seq.c GEN_BUILT), ui/optimist/op_tools.c GEN and GEN OUT */
#endif
#ifndef FELUCCA_GEN_GRIDS
#define FELUCCA_GEN_GRIDS 0      /* seq/gen.c GT_GRIDS */
#endif
#ifndef FELUCCA_GRIDS_EUCLID
#define FELUCCA_GRIDS_EUCLID 0   /* seq/gen.c gr_eu_hit, ui/optimist/op_tools.c the OUT card */
#endif
#ifndef FELUCCA_GRIDS_LFO
#define FELUCCA_GRIDS_LFO 0        /* drums/lane_play.c klfo (DR's LFO and LFO 2 on the kit GRIDS' values; with the drum editor) */
#endif
#if FELUCCA_GRIDS_LFO && !FELUCCA_GEN_GRIDS
#error "GRIDS_LFO moves the kit GRIDS' values: it needs GEN_GRIDS"
#endif
#ifndef FELUCCA_GRIDS_VEL
#define FELUCCA_GRIDS_VEL 0      /* seq/gen.c gr_lv_vel (a GRIDS hit's velocity from its level), op_tools.c the VEL card */
#endif
#if FELUCCA_GRIDS_VEL && !FELUCCA_GEN_GRIDS
#error "GRIDS_VEL is the kit GRIDS' velocity: it needs GEN_GRIDS"
#endif
#if FELUCCA_GRIDS_EUCLID && !FELUCCA_GEN_GRIDS
#error "GRIDS_EUCLID is a mode of the kit's GRIDS: it needs GEN_GRIDS"
#endif
#define FELUCCA_GENS (FELUCCA_NOTEGEN || FELUCCA_GEN_GRIDS)   /* the generators' framework (seq.c GEN_BUILT) */
#define SEQ_SKIP (FELUCCA_FILLS || FELUCCA_COND)   /* seq.c: a step that fails its FILL or its condition is a REST */

#endif
