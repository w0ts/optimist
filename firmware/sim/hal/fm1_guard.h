/* SPDX-License-Identifier: GPL-3.0-only */
/* The simulator's hardware guards (firmware/hal/fm1_guard.h): none (the names only). */
#pragma once
#include <stdint.h>
#include "fm1_irq.h"

enum { FM1_GUARD_STACK = 1, FM1_GUARD_WRITE = 2, FM1_GUARD_BUS = 4, FM1_GUARD_PC = 8 };

static void fm1_guard_enable(uint32_t which) { (void)which; }
static void fm1_guard_pc_open(void) {}
static void fm1_guard_c1_writes(void) {}
static void fm1_guard_lock_top(void) {}
static void fm1_guard_unlock_top(void) {}
