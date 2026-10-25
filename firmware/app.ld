/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Felucca memory map.
 * Code + constants execute in place from XIP (app.bin offset 0 = 0x02000120).
 * 0x01C00000..0x01C00100 is crt0's STATUS (debug area).
 * SRAM 0x01C00100..0x01C74000 is one packed region (the FM-1 runs code and data at one speed in all of it, measured
 * 2026-10-25: docs/MEMORY-MAP.md 2.3), in this order, each right after the one before:
 *   .ram_text  code that must run while the flash is busy (the flash driver)
 *   .ram_hot   the audio path, run from RAM so the UI cannot evict it from the cache (core.h HOT)
 *   .ram_hot2  more of the audio path (core.h HOT2; one run of RAM code with the two above: one guard window)
 *   .data .bss
 *   .pool_init .pool  big buffers (FX lines, canvases): IN_POOL
 *   the undo history's ring: what is left, less an 8 KiB spare
 * The RAM code and the copies are written by main.c at boot.
 * STACKS with 256-byte guard bands; NOINIT survives resets and UBOOT entry.
 * CACHE: 0x01F28000..0x01F2F000, data-cache ways as RAM (EXPERIMENTAL FELUCCA_CACHE_RAM, hal/fm1_cache.h).
 * 0x01C7FD50.. is reserved: boot_info, boot-state mailbox, vector table.
 * The update loader runs at 0x01C0A800 (loader.ld, 80 KiB): an update overwrites that part of SRAM. */
MEMORY
{
    XIP    (rx)  : ORIGIN = 0x02000120, LENGTH = 0x8DFBC
    LOAD   (r)   : ORIGIN = 0x10000000, LENGTH = 0x100000   /* the plain load copies of the RAM sections: a dummy
                                                            * region, never in the image (the LZB stream at _lz_load
                                                            * holds them: build.py), so they take no room in the slot */
    SRAM   (rwx) : ORIGIN = 0x01C00100, LENGTH = 0x73F00    /* up to _guard0 (the stacks) */
    NOINIT (rw)  : ORIGIN = 0x01C7C000, LENGTH = 0x3D50
    /* data-cache ways 0..6 as RAM: usable only with FELUCCA_CACHE_RAM and after its boot test passed
     * (hal/fm1_cache.h fm1_cache_ram_ok); otherwise they are the cache's own memory */
    CACHE  (rw)  : ORIGIN = 0x01F28000, LENGTH = 28K
}
SECTIONS
{
    .text : { KEEP(*(.text.crt0)) *(.text*) *(.rodata*) . = ALIGN(4); _etext = .; } > XIP
    .ram_text : { _rt_start = .; KEEP(*(.ram_text*)) . = ALIGN(4); _rt_end = .; } > SRAM AT > LOAD
    _rt_load = LOADADDR(.ram_text);
    _lz_load = _etext;                      /* the LZB stream of every copy (below): right after .text (the copies'
                                             * own load addresses are in LOAD: a real link needs no slot room for them) */
    .ram_hot : { _rh_start = .; KEEP(*(.ram_hot .ram_hot.*)) . = ALIGN(4); _rh_end = .; } > SRAM AT > LOAD
    _rh_load = LOADADDR(.ram_hot);
    .ram_hot2 : { _rh2_start = .; KEEP(*(.ram_hot2*)) . = ALIGN(4); _rh2_end = .; } > SRAM AT > LOAD
    _rh2_load = LOADADDR(.ram_hot2);
    .data : { . = ALIGN(4); _data_start = .; *(.data*) *(.sdata*) . = ALIGN(4); _data_end = .; } > SRAM AT > LOAD
    _data_load = LOADADDR(.data);           /* (the plain layout's; the image: _lz_load's stream) */
    .bss (NOLOAD) : { _bss_start = .; *(.bss*) *(.sbss*) *(COMMON) . = ALIGN(4); _bss_end = .; } > SRAM
    /* the pool: first what main.c copies from flash at boot (.pool_init: the TAB_RAM tables), then the zeroed
     * buffers (.pool); both count as the pool (build.py: _pool_start.._pool_end). The LOADADDRs are the
     * linker's plain layout; the image holds instead one LZB stream from _lz_load (build.py, tools/lzb.py: .data, then
     * .pool_init, .ram_text, .ram_hot, .ram_hot2, each a block), decoded in that order by main.c lzb_expand at boot:
     * 52 KB of RAM code and tables take 44 KB of flash (2026-10-23; .data alone was run-length coded, 2026-10-22) */
    .pool_init : { . = ALIGN(4); _pool_start = .; _pi_start = .; KEEP(*(.pool_init*)) . = ALIGN(4); _pi_end = .; } > SRAM AT > LOAD
    _pi_load = LOADADDR(.pool_init);
    .pool (NOLOAD) : { _pz_start = .; *(.pool*) . = ALIGN(4); _pool_end = .; } > SRAM
    .noinit (NOLOAD) : { *(.noinit) *(.noinit.*) } > NOINIT     /* (.noinit.* last: a new one moves nothing) */
    /* cache RAM (EXPERIMENTAL, hal/fm1_cache.h): zeroed by its boot test, not by main.c. Only for an object whose
     * every user checks fm1_cache_ram_ok() first (without it, or when the test fails, it is not RAM); empty today:
     * the undo history takes what follows (undo.c) */
    .cache_ram (NOLOAD) : { _cr_start = .; *(.cache_ram .cache_ram.*) . = ALIGN(4); _cr_end = .; } > CACHE
    /* the undo history's ring (undo.c, FELUCCA_UNDO_HISTORY): the memory nothing is linked into, so it
     * grows and shrinks with the build: SRAM after .pool up to its last 8 KiB (build.py keeps those spare).
     * Not a section; main.c does not clear it (undo.c needs no zeros). Its second part (undo.c's main RAM part)
     * is empty: everything is one run now; with the cache RAM (its test passed) the cache ways are that part.
     * hi <= lo: none there. build.py checks the history gets >= 1 KiB. */
    _xip_org = ORIGIN(XIP); _flash_len = LENGTH(XIP); _sram_org = ORIGIN(SRAM); _sram_len = LENGTH(SRAM);
    _undo_pool_lo = _pool_end;
    _undo_pool_hi = ORIGIN(SRAM) + LENGTH(SRAM) - 8192;
    _undo_ram_lo = _pool_end;
    _undo_ram_hi = _pool_end;
    _undo_cache_lo = _cr_end;                   /* with the cache RAM (its test passed): undo.c's second part */
    _undo_cache_hi = ORIGIN(CACHE) + LENGTH(CACHE);
    _guard0 = 0x01C74000; _ustack_lo = 0x01C74100; _ustack_top = 0x01C7A000;
    _guard1 = 0x01C7A000; _sstack_lo = 0x01C7A100; _sstack_top = 0x01C7C000;
    ASSERT(ORIGIN(SRAM) + LENGTH(SRAM) == _guard0, "app.ld: SRAM must end at the stacks' first guard band")
    /DISCARD/ : { *(.comment) *(.note*) }
}
