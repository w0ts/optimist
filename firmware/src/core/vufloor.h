/* SPDX-License-Identifier: GPL-3.0-only */
/* The dB at the bottom of every level meter (the mixer's rows, MASTER's tall one and the FX meters): fixed at -40 dBFS. It was a
 * setting (SYSTEM > METERS > VU FLOOR, -80 .. -20) until the user's ruling of 2026-10-31: "we are a groovebox, not a DAW".
 * The settings record keeps its byte (persist_rec.c rsv_pj[0]); it is ignored on load and written 0. */
#ifndef FELUCCA_VUFLOOR_H
#define FELUCCA_VUFLOOR_H

#define VU_FLOOR_DB (-40)

#endif
