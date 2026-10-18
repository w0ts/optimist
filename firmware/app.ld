/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Felucca memory map.
 * Code + constants execute in place from XIP (app.bin offset 0 = 0x02000120).
 * 0x01C00000..0x01C07F00 holds .ram_text (code that must run while the flash
 * is busy: the flash driver), then .ram_hot (the audio path, run from RAM so
 * the UI cannot evict it from the cache; core.h HOT). Both are copied there
 * by main.c. 0x01C07F00..0x01C08000 is crt0's STATUS (debug area).
 * RAM  : .ram_hot2 (more of the audio path, core.h HOT2: RAMTEXT is full), then .data/.bss (96 KiB)
 * POOL : big zero-initialised buffers (FX lines, canvases), section .pool
 * STACKS with 256-byte guard bands; NOINIT survives resets and UBOOT entry.
 * CACHE: 0x01F28000..0x01F2F000, data-cache ways as RAM (EXPERIMENTAL FELUCCA_CACHE_RAM, hal/fm1_cache.h).
 * 0x01C7FD50.. is reserved: boot_info, boot-state mailbox, vector table. */
MEMORY
{
    XIP    (rx)  : ORIGIN = 0x02000120, LENGTH = 0x8DFBC
    RAMTEXT (rwx): ORIGIN = 0x01C00000, LENGTH = 0x7F00
    RAM    (rwx) : ORIGIN = 0x01C08000, LENGTH = 96K
    POOL   (rw)  : ORIGIN = 0x01C20000, LENGTH = 0x54000
    NOINIT (rw)  : ORIGIN = 0x01C7C000, LENGTH = 0x3D50
    /* data-cache ways 0..6 as RAM: usable only with FELUCCA_CACHE_RAM and after its boot test passed
     * (hal/fm1_cache.h fm1_cache_ram_ok); otherwise they are the cache's own memory */
    CACHE  (rw)  : ORIGIN = 0x01F28000, LENGTH = 28K
}
SECTIONS
{
    .text : { KEEP(*(.text.crt0)) *(.text*) *(.rodata*) . = ALIGN(4); _etext = .; } > XIP
    .ram_text : { _rt_start = .; KEEP(*(.ram_text*)) . = ALIGN(4); _rt_end = .; } > RAMTEXT AT > XIP
    _rt_load = LOADADDR(.ram_text);
    .ram_hot : { _rh_start = .; KEEP(*(.ram_hot .ram_hot.*)) . = ALIGN(4); _rh_end = .; } > RAMTEXT AT > XIP
    _rh_load = LOADADDR(.ram_hot);
    .ram_hot2 : { _rh2_start = .; KEEP(*(.ram_hot2*)) . = ALIGN(4); _rh2_end = .; } > RAM AT > XIP
    _rh2_load = LOADADDR(.ram_hot2);
    .data : { . = ALIGN(4); _data_start = .; *(.data*) *(.sdata*) . = ALIGN(4); _data_end = .; } > RAM AT > XIP
    _data_load = LOADADDR(.data);
    .bss (NOLOAD) : { _bss_start = .; *(.bss*) *(.sbss*) *(COMMON) . = ALIGN(4); _bss_end = .; } > RAM
    /* the pool: first what main.c copies from flash at boot (.pool_init: the TAB_RAM tables), then the zeroed
     * buffers (.pool); both count as the pool (build.py: _pool_start.._pool_end) */
    .pool_init : { _pool_start = .; _pi_start = .; KEEP(*(.pool_init*)) . = ALIGN(4); _pi_end = .; } > POOL AT > XIP
    _pi_load = LOADADDR(.pool_init);
    .pool (NOLOAD) : { _pz_start = .; *(.pool*) . = ALIGN(4); _pool_end = .; } > POOL
    .noinit (NOLOAD) : { *(.noinit) *(.noinit.*) } > NOINIT     /* (.noinit.* last: a new one moves nothing) */
    /* cache RAM (EXPERIMENTAL, hal/fm1_cache.h): zeroed by its boot test, not by main.c. Only for an object whose
     * every user checks fm1_cache_ram_ok() first (without it, or when the test fails, it is not RAM); empty today:
     * the undo history takes what follows (undo.c) */
    .cache_ram (NOLOAD) : { _cr_start = .; *(.cache_ram .cache_ram.*) . = ALIGN(4); _cr_end = .; } > CACHE
    /* the undo history's ring (undo.c, FELUCCA_UNDO_HISTORY): the memory nothing is linked into, so it
     * grows and shrinks with the build. The pool after .pool up to its last 8 KiB (build.py keeps those
     * spare), then main RAM after .bss. Not a section; main.c does not clear it (undo.c needs no zeros).
     * hi < lo: none there. build.py checks the history gets >= 1 KiB. */
    /* the regions' sizes, for SYSTEM > INFO (ui/optimist/op_project.c sys_mem) */
    _xip_org = ORIGIN(XIP); _flash_len = LENGTH(XIP); _ram_org = ORIGIN(RAM); _ram_len = LENGTH(RAM);
    _pool_len = LENGTH(POOL); _rtxt_len = LENGTH(RAMTEXT);
    _undo_pool_lo = _pool_end;
    _undo_pool_hi = ORIGIN(POOL) + LENGTH(POOL) - 8192;
    _undo_ram_lo = _bss_end;
    _undo_ram_hi = ORIGIN(RAM) + LENGTH(RAM);
    _undo_cache_lo = _cr_end;                   /* with the cache RAM (its test passed): this replaces main RAM's part */
    _undo_cache_hi = ORIGIN(CACHE) + LENGTH(CACHE);
    _guard0 = 0x01C74000; _ustack_lo = 0x01C74100; _ustack_top = 0x01C7A000;
    _guard1 = 0x01C7A000; _sstack_lo = 0x01C7A100; _sstack_top = 0x01C7C000;
    /DISCARD/ : { *(.comment) *(.note*) }
}
