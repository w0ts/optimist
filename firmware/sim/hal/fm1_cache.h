/* SPDX-License-Identifier: GPL-3.0-only */
/* The simulator's cache RAM (firmware/hal/fm1_cache.h): never (CACHE_RAM is switched off for the simulator). */
#pragma once
#include <stdint.h>
#include "fm1_cc.h"

#ifndef FELUCCA_CACHE_RAM
#define FELUCCA_CACHE_RAM 0
#endif
#if FELUCCA_CACHE_RAM
#error "the simulator has no cache RAM (tools/sim.py SIM_FORCE CACHE_RAM=0)"
#endif
#define FM1_CR_LO 0x01F28000u
#define FM1_CR_HI FM1_CR_LO
#define FM1_CR_KEEP 0xFFu
enum { FM1_CR_OFF, FM1_CR_OK, FM1_CR_BUSY, FM1_CR_REG, FM1_CR_BAD };
static struct {
    uint32_t status, con, dway0, iway0;
    uint32_t dway, iway, bad_at;
} fm1_cache;

FM1_INLINE int fm1_cache_ram_ok(void) { return 0; }
static inline void fm1_cache_ram_init(void) {}
