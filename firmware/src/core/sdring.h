/* SPDX-License-Identifier: GPL-3.0-only */
/* SYSTEM > SD CARD > SMPL RING (the user, 2026-11-08): the KB each voice that streams a sample from the SD card keeps ahead
 * of it, 4, 8, 12 or 16, a setting of the FM-1 (not of a project: the ring bridges the card's stalls, the card's and
 * this FM-1's, not a song's). The builder's SD_RING is the size a fresh FM-1 starts with; the memory that does not
 * move (each ring's slot tags, the SAMPLES list's preview ring) is sized for the largest, SDR_MAX_KB. Kept in the
 * settings record (storage/persist_rec.c rsv_pj[1] bits 1..3: 0 = a record from before, the build's size; k = 4k KB).
 * The streams take the new size when the plan places them next (storage/sd/sd_stream_plan.c sst_plan_poll: the CARD
 * voices let go, the block given back, placed again at that size, or refused with the drop list).
 * Included by ui/panel.c (before the storage and the Optimist UI), by sd_stream.h and so by the host tests. */
#ifndef FELUCCA_SDRING_H
#define FELUCCA_SDRING_H
#include <stdint.h>
#define SDR_N 4u                                    /* the choices: 4, 8, 12, 16 KB */
#define SDR_MAX_KB 16u
#ifdef FELUCCA_SD_RING_KB
#define SDR_BUILD_KB ((uint32_t)FELUCCA_SD_RING_KB)  /* the builder's SD_RING: the size a fresh FM-1 starts with */
#else
#define SDR_BUILD_KB 0u
#endif
static uint8_t sd_ring_sel;                         /* 0: the build's size; 1 .. SDR_N: 4 x that KB (checked when set) */
static uint32_t sd_ring_kb(void) { return sd_ring_sel ? 4u * sd_ring_sel : SDR_BUILD_KB; }   /* the size the streams take, KB */
static uint32_t sd_ring_pos(void) { return sd_ring_kb() / 4u - 1u; }   /* its place in the list (the builder offers 4 .. 16 KB) */
static void sd_ring_set_pos(uint32_t pos) { sd_ring_sel = (uint8_t)(pos + 1u); }   /* the menu's choice, 0 .. SDR_N - 1 */
static void sd_ring_from_rec(uint32_t v) { sd_ring_sel = (uint8_t)(v <= SDR_N ? v : 0u); }   /* out of range: the build's */
static uint32_t sd_ring_to_rec(void) { return sd_ring_sel; }
#endif
