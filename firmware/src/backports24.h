/* SPDX-License-Identifier: GPL-3.0-only */
/* SLOOP 2.4 (isod89/sloop-fm1 v2.4, 8d3823f, GPL-3.0-only) in Optimist: one build switch each, as backports.h
 * (tools/backports.json: provenance, measured cost). A switch at 0 builds exactly the firmware without it.
 * Included by backports.h. Plan: docs/SLOOP24-BACKPORT.md (fm1-firmware), phase 0 here.
 *
 *   FELUCCA_SL24_SAFE  started on flash SLOOP 2.4 wrote: nothing Optimist cannot read is erased or written over
 *                      (the project slots, the autosave, 2.4's FM6 bank, a user sample past our slots); 2.4's
 *                      projects show as 2.4's, not EMPTY; 2.4's settings word and user presets read right   ours
 *   FELUCCA_SEL_PAGES  SELECT on a page turns to the previous / next page of its family (ENV, LFO, FX, EDIT, ARP,
 *                      SEQ, SCL, GLO, SAVE); on TRACKS, a screen of its own, the REC screen and while a layer is
 *                      held it stays the tempo                                                  SLOOP 2.4 ui.c page_walk */
#ifndef FELUCCA_BACKPORTS24_H
#define FELUCCA_BACKPORTS24_H

#ifndef FELUCCA_SL24_SAFE
#define FELUCCA_SL24_SAFE 1      /* storage.c st_keep / st_save, sec_log.c SLG_KEPT, sections.c sec_migrate, sl24_guard.c,
                                  * snap_store.c, fm6_store.c, upreset.c (2.3 / 2.4 records), project.c (settings) */
#endif
#ifndef FELUCCA_SEL_PAGES
#define FELUCCA_SEL_PAGES 1      /* ui.c page_walk, ui_input.c (the SELECT knob) */
#endif
#if FELUCCA_SL24_SAFE
enum { PJ_EMPTY, PJ_USED, PJ_SL24, PJ_ALIEN };   /* a project slot's state (sl24_guard.c project_state) */
#endif

#endif
