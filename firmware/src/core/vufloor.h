/* SPDX-License-Identifier: GPL-3.0-only */
/* VU FLOOR (SYSTEM > METERS): the dB at the bottom of every level meter (the mixer's rows, MASTER's tall one and the FX
 * returns'): -80 .. -20 dB in 10 dB steps, -40 by default (-60 before 2026-10-31). Kept in the settings
 * record, one byte (storage/persist_rec.c rsv_pj[0]), stored XOR 2 so byte 0 stays -60 and a record from before keeps its floor.
 * Included by ui/panel.c (before the storage and the Optimist UI) and so by the host tests. */
#ifndef FELUCCA_VUFLOOR_H
#define FELUCCA_VUFLOOR_H
#include <stdint.h>
#define VUF_N 7u                                    /* -80 -70 -60 -50 -40 -30 -20 */
#define VUF_DEF 4u                                  /* -40 (the user, 2026-10-31: "we are a groovebox, not a DAW"; was -60) */
#define VUF_REC0 2u                                 /* the record's byte 0 reads -60: a record saved before the default moved keeps its floor */
static uint8_t vu_floor_i = VUF_DEF;                /* 0 .. VUF_N - 1 */
static int32_t vuf_db(uint32_t i) { return -80 + 10 * (int32_t)(i < VUF_N ? i : VUF_DEF); }   /* the floor, dB (<= -20) */
static uint8_t vuf_enc(uint32_t i) { return (uint8_t)(i ^ VUF_REC0); }                       /* the record's byte: the default is 0 */
static uint8_t vuf_dec(uint32_t b) { return (uint8_t)((b ^ VUF_REC0) < VUF_N ? (b ^ VUF_REC0) : VUF_DEF); }   /* (out of range: the default) */
#endif
