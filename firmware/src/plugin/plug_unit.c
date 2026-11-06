/* SPDX-License-Identifier: GPL-3.0-only */
/* A plugin's translation unit (docs/SD-MODE-DESIGN.md section 10; tools/plugin_build.py compiles it): felucca.c's
 * prelude up to core.h, the DSP helpers, one engine's file, and the descriptor exported as plugin_desc. Built with
 * the firmware's flags and -DPLUG_ENGINE_FILE="engines/sid/eng_sid.c" -DPLUG_ENGINE_SYM=ENG_SID, linked at address
 * 0 with its relocations kept (--gc-sections drops the prelude's functions: the engine's own code and tables are
 * what remains; SID 2026-11-06: 1,594 B of code, 43,200 B of tables, nothing imported from the firmware). */
#include <stdint.h>
#ifndef FELUCCA_DUAL
#define FELUCCA_DUAL 0
#endif
#ifndef FELUCCA_BENCH
#define FELUCCA_BENCH 0
#endif
#if FELUCCA_DUAL
static void dual_flash_enter(void);
static void dual_flash_leave(void);
#define FM1_FLASH_ENTER() dual_flash_enter()
#define FM1_FLASH_LEAVE() dual_flash_leave()
#endif
#include "fm1_time.h"
#include "fm1_clock.h"
#include "fm1_sys.h"
#include "fm1_irq.h"
#include "fm1_guard.h"
#include "fm1_cache.h"
#include "core/backports.h"
#include "fm1_input.h"
#include "fm1_timer.h"
#include "fm1_audio.h"
#include "fm1_adc.h"
#include "fm1_lcd_hw.h"
#include "felucca_tables.h"
#if FELUCCA_DUAL
#include "fm1_dual.h"
#endif
#ifndef FELUCCA_USB_AUDIO
#define FELUCCA_USB_AUDIO 1
#endif
#include "system/libc.c"
#include "display/lcd.c"
#include "display/gfx.c"
#include "core/core.h"
#include "dsp/dsp.c"
#include "core/plugin.h"
#include PLUG_ENGINE_FILE
const engine_t *const plugin_desc = &PLUG_ENGINE_SYM;
const uint32_t plugin_abi = PLUG_ABI;              /* (the tool reads it into the header: the firmware checks it) */
