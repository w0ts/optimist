/* SPDX-License-Identifier: GPL-3.0-only */
/* SLOOP 2.4 (isod89/sloop-fm1 v2.4, 8d3823f, GPL-3.0-only) in Optimist: one build switch each, as backports.h
 * (tools/backports.json: provenance, measured cost). A switch at 0 builds exactly the firmware without it.
 * Included by backports.h. Plan: docs/SLOOP24-BACKPORT.md (fm1-firmware), phase 0 here.
 *
 *   FELUCCA_SL24_SAFE  started on flash SLOOP 2.4 wrote: nothing Optimist cannot read is erased or written over
 *                      (the project slots, the autosave, 2.4's FM6 bank, a user sample past our slots); 2.4's
 *                      projects show as 2.4's, not EMPTY; 2.4's settings word and user presets read right   ours
 *   FELUCCA_SL24_XSTEP the step extras (nudge, locks, fills, stepx.h, 2.4's layout) kept with each project: a store
 *                      per project buffer, a record of their own beside each section and the autosave in the
 *                      section log (SECTIONS 8 / 16; with 4: RAM only)                                      ours
 *   FELUCCA_SL24_IMPORT a SLOOP 2.4 project (FUN5, 3840 B) kept in an old slot: LOAD twice imports it as the working
 *                      project (params, engines, kits, FM6 PTCH -> VOICE, the extras with XSTEP); never automatic ours
 *   FELUCCA_SEL_PAGES  SELECT on a page turns to the previous / next page of its family (ENV, LFO, FX, EDIT, ARP,
 *                      SEQ, SCL, GLO, SAVE); on TRACKS, a screen of its own, the REC screen and while a layer is
 *                      held it stays the tempo                                                  SLOOP 2.4 ui.c page_walk
 *   FELUCCA_VIS        the full-screen visualiser, 12 styles: HOME on TRACKS opens it, SELECT changes the style (the
 *                      style in the settings word, bits 17-20); the tap is a copy of each audio block (fx.c)
 *                                                                                              SLOOP 2.4 ui_vis.c  */
#ifndef FELUCCA_BACKPORTS24_H
#define FELUCCA_BACKPORTS24_H

#ifndef FELUCCA_SL24_SAFE
#define FELUCCA_SL24_SAFE 1      /* storage.c st_keep / st_save, sec_log.c SLG_KEPT, sections.c sec_migrate, sl24_guard.c,
                                  * snap_store.c, fm6_store.c, upreset.c (2.3 / 2.4 records), project.c (settings) */
#endif
#ifndef FELUCCA_SL24_XSTEP
#define FELUCCA_SL24_XSTEP 0     /* stepx.h, stepx_proj.c, stepx_log.c: the step extras' storage (phase 2 turns it on with
                                  * its micro timing / fills / locks) */
#endif
#ifndef FELUCCA_SL24_IMPORT
#define FELUCCA_SL24_IMPORT 1    /* sl24_import.c, sl24_guard.c sl24_load: a SLOOP 2.4 slot, LOAD twice: imported */
#endif
#ifndef FELUCCA_SEL_PAGES
#define FELUCCA_SEL_PAGES 1      /* ui.c page_walk, ui_input.c (the SELECT knob) */
#endif
#if FELUCCA_SL24_SAFE
enum { PJ_EMPTY, PJ_USED, PJ_SL24, PJ_ALIEN };   /* a project slot's state (sl24_guard.c project_state) */
#endif

#endif
