/* SPDX-License-Identifier: GPL-3.0-only */
/* The SD card's SPI clocks on the FM-1 (hal/fm1_sd_bus.h; storage/sd/sd_mmc.c sd_clock_probe; docs/SD-CARD-DESIGN.md
 * "The clock"): SPI1's clock is lsb / (BAUD + 1), lsb 60 MHz (the LCD's and the card's start-up: BAUD 149 = 400 kHz),
 * BAUD a whole number, so the clocks between the floor and the high-speed limit are these and no others:
 *   BAUD    1     2     3     4     5     6      7     8      9
 *   MHz    30    20    15    12    10    8.57   7.5   6.67   6 (the floor)
 * The steps, fastest first; the last is the floor (6 MHz: the first SD board reads there for sure, 2026-11-04). 30 MHz is
 * past the default speed's 25: tried only on a card that took CMD6's high speed (FM1_SD_NSTEP_HS steps). 60 MHz (BAUD 0)
 * is past high speed's 50 and never tried. No registers here: the host tests (tests/sd_card_model.h) use the table too. */
#pragma once
#include <stdint.h>

#define FM1_SD_LSB_KHZ 60000u
#define FM1_SD_NSTEP 9u
#define FM1_SD_NSTEP_HS 1u                 /* the first steps (30 MHz) only after CMD6 switched the card to high speed */
#define FM1_SD_FLOOR_BAUD 9u                /* the last step */
static const uint8_t FM1_SD_STEP_BAUD[FM1_SD_NSTEP] = {1, 2, 3, 4, 5, 6, 7, 8, FM1_SD_FLOOR_BAUD};
#define FM1_SD_STEP_KHZ(k) (FM1_SD_LSB_KHZ / (FM1_SD_STEP_BAUD[k] + 1u))
