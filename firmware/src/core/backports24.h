/* SPDX-License-Identifier: GPL-3.0-only */
/* SLOOP 2.4 (isod89/sloop-fm1 v2.4, 8d3823f, GPL-3.0-only) in Optimist: one build switch each, as backports.h
 * (tools/backports.json: provenance, measured cost). A switch at 0 builds exactly the firmware without it.
 * Included by backports.h. Plan: docs/SLOOP24-BACKPORT.md (fm1-firmware), phase 0 here.
 *
 *   FELUCCA_SL24_SAFE  started on flash SLOOP (2.7; its map is 2.4's) wrote: nothing Optimist cannot read is erased or
 *                      written over (the section slots, the autosave, SLOOP's FM6 bank, a user sample past our
 *                      slots); its projects show as SLOOP's, not EMPTY (its user presets: not read, written over, 2026-10-24); its settings
 *                      (the SYN kits, the MIDI part, 2.7's side record of theme and favourites) never read as ours,
 *                      never written over (ours kept beside them: sl24_guard.c sl25_set_scan)                   ours
 *   FELUCCA_SL24_XSTEP the step extras (nudge, locks, fills, stepx.h, 2.4's layout) kept with each project: a store
 *                      per project buffer, a record of their own beside each section and the autosave in the
 *                      section log (SECTIONS 8 / 16; with 4: RAM only)                                      ours
 *   FELUCCA_SEL_PAGES  SELECT on a page turns to the previous / next page of its family (ENV, LFO, FX, EDIT, ARP,
 *                      SEQ, SCL, GLO, SAVE); on TRACKS, a screen of its own, the REC screen and while a layer is
 *                      held it stays the tempo                                                  SLOOP 2.4 ui.c page_walk
 *   FELUCCA_VIS        the full-screen visualiser, 12 styles (DROPPED from the builder and the UI 2026-10-10: nothing opens
 *                      it for now; code kept, default 0; the style is the settings word's bits 17-20, 2.4's)
 *                                                                                              SLOOP 2.4 ui_vis.c
 *   FELUCCA_BIGVALS    pages without a graph (EDIT, VOICE, the DEST pages, GLOBAL, MASTER, SYSTEM...) show their four
 *                      values in large type in the empty middle, placed as the knobs are, the turned one white
 *                                                                                  SLOOP 2.4 ui_draw.c graph_big
 *
 * SLOOP projects are not read, written or converted (the importer, the exporter and the editor's import were removed
 * 2026-10-29; their builder items SL24_IMPORT, SL24_EXPORT, SL24_EDIMPORT, bits 163, 164, 181, are never reused).
 * FELUCCA_SL24_SAFE is a builder item, on by default (2026-10-29; the user's profiles may say 0) */
#ifndef FELUCCA_BACKPORTS24_H
#define FELUCCA_BACKPORTS24_H

#ifndef FELUCCA_SL24_SAFE
#if defined(FELUCCA_DRUMS) && !FELUCCA_DRUMS
#define FELUCCA_SL24_SAFE 0      /* (no drum track: SLOOP's projects cannot be kept, nodrums.h) */
#else
#define FELUCCA_SL24_SAFE 1
#endif
/* (storage.c st_keep / st_save, sec_log.c SLG_KEPT, sections.c sec_migrate, sl24_guard.c, snap_store.c, fm6_store.c,
 * upreset.c (2.3 / 2.4 records), project.c (settings)) */
#endif
#ifndef FELUCCA_SL24_XSTEP
#define FELUCCA_SL24_XSTEP 0     /* stepx.h, stepx_proj.c, stepx_log.c: the step extras' storage (phase 2 turns it on with
                                  * its micro timing / fills / locks) */
#endif
#if FELUCCA_SL24_SAFE
enum { PJ_EMPTY, PJ_USED, PJ_SL24, PJ_ALIEN };   /* a project slot's state (sl24_guard.c project_state) */
#endif

#endif
