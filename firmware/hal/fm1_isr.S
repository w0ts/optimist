/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* IRQ entry wrappers: save what a C call may clobber, call the C body,
 * restore, csync, rti. clang does not emit the csync needed before rti,
 * so ISRs are not written in C directly. */
	.macro FM1_ISR name, body
	.section .text.\name,"ax",@progbits
	.globl \name
\name:
	[--sp] = {psr, rets, reti}
	[--sp] = {r3-r0}
	call \body
	{r3-r0} = [sp++]
	{psr, rets, reti} = [sp++]
	csync
	rti
	.endm

	FM1_ISR isr_timer5, fm1_timer5_irq
	FM1_ISR isr_alnk0, fm1_alnk0_irq

/* the same two entries in RAM (.ram_hot), their C bodies called through a register (RAM to the flash: a direct call does not
 * reach): the second-core test image's `dualcfg isr ram` attaches these (2026-10-15: TIMER5 dispatched to its XIP stub + 2
 * with one address bit wrong, three times, with CPU1 running: the cache / XIP fetch of the stub is suspect) */
	.macro FM1_ISR_RAM name, body
	.section .ram_hot.\name,"ax",@progbits
	.globl \name
\name:
	[--sp] = {psr, rets, reti}
	[--sp] = {r3-r0}
	r3 = \body
	call r3
	{r3-r0} = [sp++]
	{psr, rets, reti} = [sp++]
	csync
	rti
	.endm

	FM1_ISR_RAM isr_timer5_ram, fm1_timer5_irq
	FM1_ISR_RAM isr_alnk0_ram, fm1_alnk0_irq
