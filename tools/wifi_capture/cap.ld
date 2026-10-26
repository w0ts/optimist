/* SPDX-License-Identifier: GPL-3.0-only */
/* The capture image (tools/wifi_capture/README.md): code and constants in XIP, .data copied to RAM by cap_cstart,
 * .bss zeroed; the RAM range of firmware/app.ld. */
ENTRY(_start)
MEMORY
{
    XIP (rx)  : ORIGIN = 0x02000120, LENGTH = 0x8DFBC
    RAM (rwx) : ORIGIN = 0x01C08000, LENGTH = 0x74000
}
SECTIONS
{
    .text : { KEEP(*(.text.crt0)) *(.text*) *(.rf.text*) *(.rodata*) *(.srodata*) . = ALIGN(4); _etext = .; } > XIP
    .data : { _data_start = .; *(.data*) *(.sdata*) . = ALIGN(4); _data_end = .; } > RAM AT > XIP
    _data_load = LOADADDR(.data);
    .bss (NOLOAD) : { _bss_start = .; *(.bss*) *(.sbss*) *(COMMON) . = ALIGN(4); _bss_end = .; } > RAM
    .stack (NOLOAD) : { . = ALIGN(8); . += 0x4000; _ustack_top = .; . += 0x1000; _sstack_top = .; } > RAM
}
