/* SPDX-License-Identifier: GPL-3.0-only */
/* A plugin's translation unit (docs/SD-MODE-DESIGN.md section 10; tools/plugin_build.py compiles it): felucca.c's
 * prelude up to core.h, the DSP helpers, one engine's file, and the descriptor exported as plugin_desc. Built with
 * the firmware's flags and -DPLUG_ENGINE_FILE="engines/sid/eng_sid.c" -DPLUG_ENGINE_SYM=ENG_SID, linked at address
 * 0 with its relocations kept (--gc-sections drops the prelude's functions: the engine's own code and tables are
 * what remains; SID 2026-11-06: 1,594 B of code, 43,200 B of tables, nothing imported from the firmware).
 *
 * What the engine takes from the firmware comes through the API table (core/plugin.h plug_api_t), read through
 * plugin_api, the one word the loader sets to the table's address (relocation kind 2): the part's engine memory
 * (PART_MEM_AT and the engines' accessors, which engines.c defines for the built-in engines), and the tables the
 * boot decodes into RAM (SINE, TANH_Q15). The tables that only TAB_CODED_ALL moves into RAM (the core curves, the
 * engine's own: PLAITS's PL_WS, JUNO's) are compiled into the plugin as constants (the same values): the firmware
 * that loads it may not hold them (its build lacks the engine, or TAB_CODED_ALL is off). */
#include <stdint.h>
#define PLUG_BUILD 1                                    /* (core.h: no RAM code; gen_tables.py: no RAM tables; the sample
                                                         * engines: the card's material alone, through plugin_api) */
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
#include "core/plugin.h"
/* the API table's address, set by the loader (relocation kind 2, tools/plugin_build.py finds the word by this name):
 * not const, so the compiler cannot take the file's 0 for its value (it loads the word), yet with the constants
 * (.rodata: the plugin is all flash); a GC root (the tool links with --undefined=plugin_api), so every plugin has it */
const plug_api_t *plugin_api __attribute__((section(".rodata.plugin_api"))) = 0;
#undef FELUCCA_TAB_CODED_ALL
#define FELUCCA_TAB_CODED_ALL 0                    /* (the curves only it moves into RAM: constants of the plugin's own) */
#include "felucca_tables.h"
/* the tables the boot always decodes into RAM: the firmware's, through the API (their types kept: sizeof works) */
#define SINE (*(__typeof__(SINE) *)plugin_api->tab[PLUG_TAB_SINE])
#define TANH_Q15 (*(__typeof__(TANH_Q15) *)plugin_api->tab[PLUG_TAB_TANH])
#if FELUCCA_DUAL
#include "fm1_dual.h"
#endif
#ifndef FELUCCA_USB_AUDIO
#define FELUCCA_USB_AUDIO 1
#endif
#include "system/libc.c"
#define rng() (plugin_api->rng())                  /* (libc.c's state is the firmware's) */
#include "display/lcd.c"
#include "display/gfx.c"
#include "core/core.h"
#define trk (*(__typeof__(trk) *)plugin_api->trk)    /* the firmware's tracks (the engines' t - trk: the part) */
#include "dsp/dsp.c"
/* the part's engine memory (engines.c PART_MEM_AT, for the built-in engines) */
#define PART_MEM_AT(p) (plugin_api->part_mem(p))
#ifndef PLUG_SAMPLES
#define PLUG_SAMPLES 0                                  /* (tools/plugin_build.py: 1 for SAMPLE, GRAIN, SLICE, STRETCH) */
#endif
#if PLUG_SAMPLES
/* the sample engines (plugins/samples, 2026-11-06): SD card mode only. Their material is the card's, by hash, through the
 * streaming plan (the firmware's, plug_api_t's card entries); eng_sample.c's types and card helpers compiled here, the
 * plan's data the firmware's (sd_stream.h, sd_stream_rc.h under PLUG_BUILD); the flash's sets and user slots never
 * reach a plugin (eng_sample.c SMP_FLASH 0) */
#if !FELUCCA_SD_STREAM
#error "a sample engine is a plugin in SD card mode only (FELUCCA_SD_STREAM: its samples are the card's)"
#endif
#include "engines/sample/eng_sample.c"
#endif
#include PLUG_ENGINE_FILE
#if defined(PLUG_ENG_GRAIN)
AINL gr_part_t *gr_mem(uint32_t p) { return (gr_part_t *)PART_MEM_AT(p); }
#endif
#if defined(PLUG_ENG_SLICE)
AINL slc_rbuf_t *slc_mem(uint32_t p) { return (slc_rbuf_t *)PART_MEM_AT(p); }
#endif
/* the engines' accessors of it (engines.c's, the same shapes; the tool sets PLUG_ENG_<NAME> for the engine built) */
#if defined(PLUG_ENG_PHYS)
AINL phys_slot_t *phys_mem(uint32_t p) { return (phys_slot_t *)PART_MEM_AT(p); }
#endif
#if defined(PLUG_ENG_PLAITS)
AINL pl_voice_t *pl_mem(uint32_t p) { return (pl_voice_t *)PART_MEM_AT(p); }
#endif
#if defined(PLUG_ENG_JUNO)
AINL juno_mem_t *juno_mem(uint32_t p) { return (juno_mem_t *)PART_MEM_AT(p); }
#endif
#if defined(PLUG_ENG_DRAWBAR)
AINL drw_mem_t *drw_mem(uint32_t p) { return (drw_mem_t *)PART_MEM_AT(p); }
#endif
#if defined(PLUG_ENG_CZ)
AINL cz_mem_t *cz_mem(uint32_t p) { return (cz_mem_t *)PART_MEM_AT(p); }
#endif
const engine_t *const plugin_desc = &PLUG_ENGINE_SYM;
const uint32_t plugin_abi = PLUG_ABI;              /* (the tool reads it into the header: the firmware checks it) */
