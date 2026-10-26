/* SPDX-License-Identifier: GPL-3.0-only */
/* FELUCCA_DRUMS 0 (core.h): every switch of the drums (the builder's Drums group and the drum-only items of the others,
 * tools/builder/registry.py DRUMS_ONLY) is 0; one set on is a configuration error */
#pragma once
#if (defined(FELUCCA_DRUM_SYNTH) && FELUCCA_DRUM_SYNTH) || (defined(FELUCCA_DS_KAWA) && FELUCCA_DS_KAWA) || \
    (defined(FELUCCA_DS_PLAITS) && FELUCCA_DS_PLAITS) || (defined(FELUCCA_DRUM_X909) && FELUCCA_DRUM_X909) || \
    (defined(FELUCCA_DRUM_X808) && FELUCCA_DRUM_X808) || (defined(FELUCCA_DRUM_SAMPLED) && FELUCCA_DRUM_SAMPLED) || \
    (defined(FELUCCA_DRUM_EDIT) && FELUCCA_DRUM_EDIT) || (defined(FELUCCA_DRUM_USR) && FELUCCA_DRUM_USR) || \
    (defined(FELUCCA_DRUM_KITS) && FELUCCA_DRUM_KITS) || (defined(FELUCCA_LANE_FX) && FELUCCA_LANE_FX) || \
    (defined(FELUCCA_FX_DUCK) && FELUCCA_FX_DUCK) || (defined(FELUCCA_ENERGY) && FELUCCA_ENERGY) || \
    (defined(FELUCCA_GRIDS_EUCLID) && FELUCCA_GRIDS_EUCLID) || (defined(FELUCCA_GRIDS_LFO) && FELUCCA_GRIDS_LFO) || \
    (defined(FELUCCA_GRIDS_VEL) && FELUCCA_GRIDS_VEL) || (defined(FELUCCA_SL24_SAFE) && FELUCCA_SL24_SAFE) || \
    (defined(FELUCCA_SL24_IMPORT) && FELUCCA_SL24_IMPORT) || (defined(FELUCCA_SL24_EXPORT) && FELUCCA_SL24_EXPORT) || \
    (defined(FELUCCA_SL24_EDIMPORT) && FELUCCA_SL24_EDIMPORT)
#error "FELUCCA_DRUMS 0: a drum switch is on (tools/builder: the Drums group's items are DRUMS' options)"
#endif
#define FELUCCA_DRUM_SYNTH 0
#define FELUCCA_DS_KAWA 0
#define FELUCCA_DS_PLAITS 0
#define FELUCCA_DRUM_X909 0
#define FELUCCA_DRUM_X808 0
#define FELUCCA_KIT_ACOUSTIC 0
#define FELUCCA_KIT_DEEP 0
#define FELUCCA_KIT_TIGHT 0
#define FELUCCA_KIT_BRIGHT 0
#define FELUCCA_KIT_DUST 0
#define FELUCCA_DRUM_SAMPLED 0
#define FELUCCA_DRUM_EDIT 0
#define FELUCCA_DS_NTUNE 0
#define FELUCCA_DRUM_USR 0
#define FELUCCA_DRUM_KITS 0
#define FELUCCA_LANE_FX 0
#define FELUCCA_FX_DUCK 0
#define FELUCCA_ENERGY 0
#define FELUCCA_GRIDS_EUCLID 0
#define FELUCCA_GRIDS_LFO 0
#define FELUCCA_GRIDS_VEL 0
#define FELUCCA_DRUM_ACCENT 0
#define FELUCCA_SL24_SAFE 0                    /* (SLOOP's projects and backups: a drum track) */
#define FELUCCA_SL24_IMPORT 0
#define FELUCCA_SL24_EXPORT 0
#define FELUCCA_SL24_EDIMPORT 0
