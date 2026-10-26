/* SPDX-License-Identifier: GPL-3.0-only */
/* The capture image's entry (tools/wifi_capture/README.md): the same hand-over as firmware/crt0.S (the SPL maps the
 * image at XIP 0x02000120 and jumps to its first byte; `goto +4; rts` first), then interrupts off, our stacks, C. */
	.section .text.crt0,"ax",@progbits
	.globl _start
_start:
	goto 1f
	rts
1:	cli
	sp = _ustack_top
	ssp = _sstack_top
	call cap_cstart
	goto .
