/* SPDX-License-Identifier: GPL-3.0-only */
/* The simulator's XIP window (firmware/hal/fm1_xip.h): a flash offset reads from the RAM image (sim_core.h), at the
 * same offsets as the FM-1's flash. */
#pragma once
#include <stdint.h>
#include "fm1_cc.h"
#include "sim_core.h"

#define FM1_XIP(off) ((uint32_t)(uintptr_t)(sim_flash_img + (uint32_t)(off)))

FM1_INLINE const uint8_t *fm1_xip_ptr(uint32_t off) { return sim_flash_img + off; }
