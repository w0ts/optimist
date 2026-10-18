/* SPDX-License-Identifier: GPL-3.0-only */
/* The simulator's TIMER4 (firmware/hal/fm1_time.h): the simulated clock (sim_core.h). A read moves it one tick, so a
 * spin on the timer ends; a delay moves it by the delay, the interrupts due on the way run (as on the FM-1, where
 * they interrupt the spin). */
#pragma once
#include <stdint.h>
#include "sim_core.h"

#define FM1_TICKS_PER_US 24u

static inline void fm1_time_init(void) {}

static inline __attribute__((always_inline)) uint32_t fm1_ticks(void) { return sim_tick(); }

static inline uint32_t fm1_micros(void) { return fm1_ticks() / FM1_TICKS_PER_US; }

static inline void fm1_delay_us(uint32_t us) { sim_advance((uint64_t)us * FM1_TICKS_PER_US); }

static inline void fm1_delay_ms(uint32_t ms) { sim_advance((uint64_t)ms * 1000u * FM1_TICKS_PER_US); }
