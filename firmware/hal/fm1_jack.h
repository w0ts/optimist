/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Henri David */
/* The headphone jack's switch: PB5, an input (as the boot leaves it: enabled, no pull set here), 0 with headphones in,
 * 1 with nothing plugged (measured on an FM-1 2026-10-29 with a probe of the input registers). Read only: its
 * direction, pull and function are never written. The debounce is the caller's (src/system/hpjack.h).
 *
 *   fm1_hp_in()   1: headphones in (the pin's raw state, bouncing ~150 ms a plug) */
#pragma once
#include <stdint.h>
#include "fm1_gpio.h"

#define FM1_HP_PIN 5u                   /* PB5 */
static inline uint32_t fm1_hp_in(void) { return !(FM1_PR(FM1_PB, FM1_IN) >> FM1_HP_PIN & 1u); }
