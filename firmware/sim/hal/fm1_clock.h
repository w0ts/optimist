/* SPDX-License-Identifier: GPL-3.0-only */
/* The simulator's CPU clock (firmware/hal/fm1_clock.h): no PLL registers, nothing measured (0: "none"). */
#pragma once
#include <stdint.h>
#include "fm1_cc.h"
#include "fm1_time.h"

#define FM1_CLK_CHAIN 32u
#define FM1_CLK_PASSES 1000u
#define FM1_CLK_TRIES 4u

static inline uint32_t fm1_clk_reg(uint32_t i) { (void)i; return 0; }

static uint32_t fm1_cpu_khz(void) { return 0; }
