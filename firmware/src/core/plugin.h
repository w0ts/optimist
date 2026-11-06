/* SPDX-License-Identifier: GPL-3.0-only */
/* Plugins in flash (docs/SD-MODE-DESIGN.md section 10; the user, 2026-11-06: every engine a file, loaded into flash
 * when a build misses it). An engine is built as a file by tools/plugin_build.py (firmware/src/plugin/plug_unit.c:
 * the engine alone, linked at address 0, its relocations kept), written into the PLUGINS region of the flash at any
 * 4 KB place by core/plugin.c, which patches the absolute addresses as it writes. This header is the layout the
 * firmware, the tool and the tests share: macros and one struct, no code.
 *
 * The region: the last FELUCCA_PLUGIN_KB of the app slot, below the app area's tail (the SDK's cfg_tool.bin and
 * the cfg directory, tools/flash_layout.py APP_TAIL), so app.bin's room (app.ld's XIP) ends where the region starts
 * (tools/build.py). A placed plugin: its header in the first bytes of a sector, the image right after it, as many
 * sectors as it needs; the sectors with no valid header are free. There is no directory: the region is scanned at
 * boot (plug_init), a header's CRCs say whether what follows is whole. */
#pragma once
#include <stdint.h>

#ifndef FELUCCA_PLUGIN_KB
#define FELUCCA_PLUGIN_KB 0               /* the region's size, KB (the builder item PLUGINS; whole sectors; at most 256:
                                           * the used map is 64 bits); 0: no plugins */
#endif
#ifndef FELUCCA_PLUGINS
#define FELUCCA_PLUGINS (FELUCCA_PLUGIN_KB > 0)   /* the region, the loader, FELUCCA_PLUGIN_SLOTS engine slots */
#endif
#ifndef FELUCCA_PLUGIN_SLOTS
#define FELUCCA_PLUGIN_SLOTS 2            /* engine slots after the built-in ones (1..4) */
#endif
#define PLUG_SECTOR 4096u
#if FELUCCA_PLUGINS                       /* (the region's place needs the flash map: hal/; a host test without plugins
                                           * compiles with firmware/src alone) */
#include "fm1_flash_map.h"
#define PLUG_TAIL 0xF24u                  /* the app area's tail after app.bin (tools/flash_layout.py APP_TAIL) */
#define FL_PLUG_HI ((FL_APP_HI - PLUG_TAIL) & ~(PLUG_SECTOR - 1u))
#define FL_PLUG_LO (FL_PLUG_HI - (uint32_t)FELUCCA_PLUGIN_KB * 1024u)
#define PLUG_NSECT ((uint32_t)FELUCCA_PLUGIN_KB * 1024u / PLUG_SECTOR)
#if FELUCCA_PLUGIN_KB > 256 || FELUCCA_PLUGIN_SLOTS < 1 || FELUCCA_PLUGIN_SLOTS > 4
#error "FELUCCA_PLUGIN_KB at most 256, FELUCCA_PLUGIN_SLOTS 1..4"
#endif
#if FELUCCA_SDCARD
static int plug_card_install(uint32_t uid);   /* core/plugin_card.c (after the card's code): the plugin of this UID from
                                               * Optimist/Plugins/<NAME>.opl into the region: its slot, or PLUG_E_* */
#endif
#endif

#define PLUG_MAGIC 0x474C504Fu            /* "OPLG" */
#define PLUG_API 2u                       /* bumped when engine_t, voice_t, track_t, vmod_t or plug_api_t change shape
                                           * (2: the API table and relocation kind 2, 2026-11-06) */
#define PLUG_KIND_ENGINE 0u
#define PLUG_HDR_LEN 48u
#define PLUG_NREL_MAX 4096u               /* relocation words a file may carry (SID: 73) */

/* The header: the file's first 48 bytes, and the placed plugin's first 48 (then nrel 0, crc of the image as patched,
 * base set). hcrc covers the 44 bytes before it. */
typedef struct {
    uint32_t magic;
    uint16_t api;
    uint8_t kind;
    uint8_t uid;                          /* the engine's UID (core/registry.h ENGINE_LIST) */
    char name[12];                        /* what the lists show, 0-padded */
    uint32_t len;                         /* the image (code and constants) after the header, a multiple of 4 */
    uint32_t desc;                        /* the descriptor's offset in the image (an engine: its engine_t) */
    uint32_t nrel;                        /* the file: relocation words after the image; placed: 0 */
    uint32_t crc;                         /* CRC-32 of the image: the file's as linked at 0; placed: as patched */
    uint32_t base;                        /* the file: 0; placed: the XIP address of the image's first byte */
    uint32_t abi;                         /* PLUG_ABI of the build the plugin was compiled against */
    uint32_t hcrc;
} plug_hdr_t;
/* The API table: what a plugin may call in the firmware (docs/SD-MODE-DESIGN.md 10.2 "one versioned table"). The
 * firmware builds one, const, in its image (core/plugin.c plug_api_tab); a plugin reads it through its word
 * plugin_api (plugin/plug_unit.c), which the loader fills with the table's XIP address (relocation kind 2,
 * PLUG_REL_API). Entries are only ever added at the end; a changed entry bumps PLUG_API. Its size is in PLUG_ABI. */
#define PLUG_TAB_SINE 0u                  /* SINE[1024], int16 Q15 (felucca_tables.h: decoded into the pool at boot) */
#define PLUG_TAB_TANH 1u                  /* TANH_Q15[] (the same) */
#define PLUG_NTAB 2u                      /* the decoded tables the table lists (tab[]) */
typedef struct {
    uint32_t version;                     /* PLUG_API of the firmware */
    void *(*part_mem)(uint32_t part);     /* the part's engine memory (engines.c PART_MEM_AT): the bytes the engine's
                                           * descriptor asks for (engine_t part_bytes), zeroed when the part comes to it */
    const void *tab[PLUG_NTAB];           /* the tables the boot decodes into RAM, by PLUG_TAB_* (their addresses are the
                                           * firmware's: a plugin cannot link them) */
    void *trk;                            /* the tracks, core.h trk[NTRK] (track_t: the engines' part index is t - trk) */
    uint32_t (*rng)(void);                /* the main loop's random numbers (system/libc.c rng; never in a render) */
    /* (later, added at the end: the sample engines' card services, another agent's work) */
} plug_api_t;

/* the shapes a plugin shares with the firmware, in one word: the structs it reads and the descriptor it exports
 * change with the build's switches (a configuration's track_t is not another's), so the API number alone is not
 * enough. Evaluated where core.h is in scope (core/plugin.c, plugin/plug_unit.c) */
#ifndef PLUG_ABI
#define PLUG_ABI ((uint32_t)sizeof(track_t) << 20 ^ (uint32_t)sizeof(voice_t) << 12 ^ (uint32_t)sizeof(vmod_t) << 4 ^ \
                  (uint32_t)sizeof(engine_t) << 24 ^ (uint32_t)sizeof(preset_t) << 16 ^ (uint32_t)sizeof(param_desc_t) ^ \
                  (uint32_t)sizeof(plug_api_t) << 8)
#endif

/* A relocation word (the file, after the image): the kind in the top 4 bits, the image offset in the rest. The
 * loader adds the image's XIP address to the value each one names. */
#define PLUG_REL_OFF(w) ((w) & 0x0FFFFFFFu)
#define PLUG_REL_KIND(w) ((w) >> 28)
#define PLUG_REL_ABS32 0u                 /* a 32-bit word holding an address (tables of pointers, strings) */
#define PLUG_REL_MOV32 1u                 /* two instruction words: the address's low 16 bits in the first's top half,
                                           * its high 16 in the second's bottom half (pi32v2 R_PI32V2_MOV_ABS32,
                                           * measured on SID 2026-11-06: 6 of them, 67 ABS32) */
#define PLUG_REL_API 2u                   /* a 32-bit word set to the API table's XIP address (the unit's plugin_api;
                                           * the file's value is ignored) */
#define PLUG_REL_KMAX PLUG_REL_API        /* the last kind the loader knows */
#define PLUG_REL_SPAN(k) ((k) == PLUG_REL_MOV32 ? 8u : 4u)

/* plug_install's results (a slot number when it worked) */
#define PLUG_E_BAD (-1)                   /* not a plugin file, or one for another API, or malformed */
#define PLUG_E_FULL (-2)                  /* no run of free sectors holds it */
#define PLUG_E_SLOTS (-3)                 /* every engine slot taken */
#define PLUG_E_READ (-4)                  /* the reader failed (the card gone): nothing registered, the sectors free */
#define PLUG_E_FLASH (-5)                 /* an erase or a program failed */
#define PLUG_E_MEM (-6)                   /* its part memory is larger than a part's can be (no arena: the fixed size) */
