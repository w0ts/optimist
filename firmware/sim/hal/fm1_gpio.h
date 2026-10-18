/* SPDX-License-Identifier: GPL-3.0-only */
/* The simulator's GPIO (firmware/hal/fm1_gpio.h): the names; a port register is a plain word nobody reads. */
#pragma once
#include <stdint.h>

static volatile uint32_t sim_gpio[8][8];
#define FM1_PORT(p) (0x50000u + (p) * 0x40u)
#define FM1_PR(p, r) (sim_gpio[(p) & 7u][((r) >> 2) & 7u])
enum { FM1_PA = 0, FM1_PB = 1, FM1_PC = 2, FM1_PH = 7 };
enum { FM1_OUT = 0x00, FM1_IN = 0x04, FM1_DIR = 0x08, FM1_DIE = 0x0C, FM1_PU = 0x10,
       FM1_PD = 0x14, FM1_HD0 = 0x18, FM1_HD = 0x1C };
