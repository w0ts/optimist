/* SPDX-License-Identifier: GPL-3.0-only */
/* The load meters (SYSTEM > METERS; the user, 2026-11-01: "one or two pixel lines at the bottom of the screen for cpu ... make
 * it a preference: meters: cpus, memory, both"): thin lines on the screen's last rows, drawn by ui/optimist/op_meters.c.
 * The setting lives here, before the storage and the UI; the settings record keeps it in one byte, mode + 1
 * (storage/persist_rec.c rsv_pj[2]); a record from before has 0 there and reads the default, CPUS. Pure: no state but the mode. */
#ifndef FELUCCA_METERS_H
#define FELUCCA_METERS_H
#include <stdint.h>
enum { MTR_OFF, MTR_CPUS, MTR_MEM, MTR_BOTH, MTR_N };
#define MTR_DEFAULT MTR_CPUS
#define MTR_ROWS 3                                      /* the rows the screen keeps at its foot for them (BOTH: memory, CPU0, CPU1) */
static uint8_t meters_mode = MTR_DEFAULT;
static uint8_t meters_to_rec(void) { return (uint8_t)(meters_mode + 1u); }
static void meters_from_rec(uint32_t b)                 /* 0 (a record from before) or out of range: the default */
{
    meters_mode = b >= 1u && b <= MTR_N ? (uint8_t)(b - 1u) : (uint8_t)MTR_DEFAULT;
}
static uint32_t meters_cpu(void) { return meters_mode == MTR_CPUS || meters_mode == MTR_BOTH; }
static uint32_t meters_mem(void) { return meters_mode == MTR_MEM || meters_mode == MTR_BOTH; }
#endif
