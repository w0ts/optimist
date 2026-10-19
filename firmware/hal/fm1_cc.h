/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FM1_INLINE: register helpers that must inline into their caller, so the
 * SFR access sequence (and its timing, in ISRs and IRQ-off sections) is the
 * one written at the call site; -Os would otherwise out-line a helper with
 * several callers. RAM-resident code (.ram_text) may only use these, never
 * a called helper (build.py checks .ram_text for calls). */
#pragma once

#define FM1_INLINE static inline __attribute__((always_inline))

/* the core's synchronisation instructions (pi32v2): csync, ssync. A host build (the tests, with the host's compiler)
 * has neither: a compiler barrier stands in (gcc emits unused static helpers at -O0, and its assembler refused them) */
#if defined(__PI32V2__)
#define FM1_CSYNC() __asm__ volatile("csync" ::: "memory")
#define FM1_SSYNC() __asm__ volatile("ssync" ::: "memory")
#else
#define FM1_CSYNC() __asm__ volatile("" ::: "memory")
#define FM1_SSYNC() __asm__ volatile("" ::: "memory")
#endif
