/* SPDX-License-Identifier: GPL-3.0-only */
/* How deep the stacks have gone since boot (the user, 2026-10-28: SYSTEM > INFO's RAM line, "amber when under 1 KB of stack
 * is left"). Both builds' linker scripts (firmware/app.ld; the release's fm1base/device/app.ld and app_wifi.ld) put CPU0's two
 * stacks right under the reset-kept record (.noinit at 0x01C7C000), each over a 256 B guard band (hal/fm1_guard.h):
 *   MAIN  the main loop's (crt0: sp = _ustack_top): _ustack_lo 0x01C74100 .. _ustack_top 0x01C7A000, 24,320 B
 *   IRQ   the interrupts' (crt0: ssp = _sstack_top): _sstack_lo 0x01C7A100 .. _sstack_top 0x01C7C000, 7,936 B
 * (a crash's sp 0x01C7BFFC is the interrupts' stack one word in: an interrupt's first push.) CPU1's jobs' stack (DUAL builds)
 * is marked by its own start and read through STK_C1_USED: hal/fm1_dual.h fm1_c1_stack's upper 5 KiB here; the release's
 * felucca-bsp fm1_cpu1_ustack (fm1.toml cpu2_stack, less its lowest 256 B: the guard), plat_cpu2_stack (fm1_compat.h).
 *
 * At boot (stk_boot: main.c fm1_main; the release: opt_core_init), the interrupts off, every word below what is live gets
 * STK_MARK: in the stack sp is in, up to STK_SLACK words under sp (the painter's own frame); when that is the main stack,
 * the interrupts' too, all but its top STK_SLACK words (no interrupt runs while they are off). sp anywhere else (a task's
 * stack): nothing painted, nothing shown. A word no longer the mark has been written since: the lowest one is the deepest
 * that stack went. The main loop reads it (stk_left: from the bottom up to the first written word), never an interrupt. */
#ifndef STACKMARK_H
#define STACKMARK_H
#define STK_MARK 0x4B545350u                            /* "PSTK" */
#define STK_SLACK 64u                                   /* words left unpainted under sp (and at the interrupts' top) */
#define STK_LOW 1024u                                   /* bytes left under which INFO's RAM line is amber */
enum { STK_MAIN, STK_IRQ, STK_C1, STK_N };
static struct {
    uint32_t *lo[2], *top[2];                           /* CPU0's stacks as painted (lo 0: not painted) */
} stk;
static void stk_fill(uint32_t *lo, uint32_t *hi)
{
    volatile uint32_t *p = lo;                          /* (a plain loop, never a library call under sp) */
    while (p < hi)
        *p++ = STK_MARK;
}
/* the paint, sp the stack pointer live now (the interrupts off); the main stack ulo..utop, the interrupts' slo..stop */
static void stk_paint(uint32_t *ulo, uint32_t *utop, uint32_t *slo, uint32_t *stop, uintptr_t sp)
{
    uint32_t *p = (uint32_t *)(sp & ~(uintptr_t)3u);
    stk.lo[STK_MAIN] = stk.lo[STK_IRQ] = 0;
    if (p >= ulo + STK_SLACK && p <= utop) {
        stk_fill(ulo, p - STK_SLACK);
        stk_fill(slo, stop - STK_SLACK);
        stk.lo[STK_MAIN] = ulo, stk.top[STK_MAIN] = utop;
        stk.lo[STK_IRQ] = slo, stk.top[STK_IRQ] = stop;
    } else if (p >= slo + STK_SLACK && p <= stop) {
        stk_fill(slo, p - STK_SLACK);
        stk.lo[STK_IRQ] = slo, stk.top[STK_IRQ] = stop;
    }
}
/* bytes of the mark from lo up to the first word written (top - lo: never written) */
static uint32_t stk_unused(const uint32_t *lo, const uint32_t *top)
{
    const volatile uint32_t *p = lo;
    while (p < top && *p == STK_MARK)
        p++;
    return (uint32_t)(p - lo) * 4u;
}

/* the second core's jobs' stack: bytes used (0: not started, not in the build) and its size */
#ifndef STK_C1_USED
#if FELUCCA_DUAL && defined(__PI32V2__)
#define STK_C1_USED() fm1_dual_stack_used(1u)
#define STK_C1_BYTES ((FM1_C1_STACK_WORDS - FM1_C1_SMALL) * 4u)
#else
#define STK_C1_USED() 0u
#define STK_C1_BYTES 0u
#endif
#endif

#ifdef __PI32V2__
#include "../../hal/fm1_stack.h"                    /* (the linker scripts' stack symbols, the stack pointer) */
static void stk_boot(void)
{
    IRQ_SAVE(ie);
    stk_paint(fm1_stk_ulo, fm1_stk_utop, fm1_stk_slo, fm1_stk_stop, fm1_sp());
    IRQ_RESTORE(ie);
}
/* the SRAM nothing is linked into: the pool's end to the stacks' guard band (the undo history's room, UNDO_HISTORY) */
static uint32_t stk_sram_free(void) { return (uint32_t)(fm1_stk_guard0 - fm1_stk_pool_end); }
#else
/* the host (tests, the simulator): stand-ins of the device's sizes, nothing live in them; the tests write into them */
#define STK_HOST_MAIN (24320u / 4u)
#define STK_HOST_IRQ (7936u / 4u)
static uint32_t stk_host_main[STK_HOST_MAIN], stk_host_irq[STK_HOST_IRQ], stk_host_free;
static void stk_boot(void)
{
    stk_paint(stk_host_main, stk_host_main + STK_HOST_MAIN, stk_host_irq, stk_host_irq + STK_HOST_IRQ,
              (uintptr_t)(stk_host_main + STK_HOST_MAIN));
}
static uint32_t stk_sram_free(void) { return stk_host_free; }
#endif

/* stack k's bytes never used since boot, its size in *size; -1: not measured (not painted, CPU1 off or not built) */
static int32_t stk_left(uint32_t k, uint32_t *size)
{
    if (k == STK_C1) {
        uint32_t used = STK_C1_USED(), n = STK_C1_BYTES;
        *size = n;
        return used && n ? (int32_t)(n > used ? n - used : 0u) : -1;
    }
    if (k >= STK_C1 || !stk.lo[k])
        return *size = 0, -1;
    *size = (uint32_t)(stk.top[k] - stk.lo[k]) * 4u;
    return (int32_t)stk_unused(stk.lo[k], stk.top[k]);
}
/* the consoles' lines (io/console.c, the release's ui/opt_console.h "dbg"): each stack's bottom (CPU0's), size and bytes never
 * used since boot (-1: not measured) */
static void stk_report(void (*kv)(const char *, int32_t), void (*kx)(const char *, uint32_t))
{
    static const char *const K[STK_N][3] = {{"stack_main_lo", "stack_main_size", "stack_main_left"},
                                            {"stack_irq_lo", "stack_irq_size", "stack_irq_left"},
                                            {"stack_cpu1_lo", "stack_cpu1_size", "stack_cpu1_left"}};
    uint32_t k, size;
    for (k = 0; k < STK_N; k++) {
        int32_t l = stk_left(k, &size);
        if (k < STK_C1)
            kx(K[k][0], (uint32_t)(uintptr_t)stk.lo[k]);
        kv(K[k][1], (int32_t)size);
        kv(K[k][2], l);
    }
}
/* the least left of every stack measured, -1 none; *which: that stack (STK_*) */
static int32_t stk_least(uint32_t *which)
{
    uint32_t k, size;
    int32_t m = -1, l;
    *which = STK_MAIN;
    for (k = 0; k < STK_N; k++)
        if ((l = stk_left(k, &size)) >= 0 && (m < 0 || l < m))
            m = l, *which = k;
    return m;
}
/* the least left of every stack ever, -1 none; *which: that stack. The release: fm1-base's plat_stack_least_ever (its
 * own paint, kept across restarts, power-offs and crashes: its device/stacks.c, dbgstats.c), the room since boot when
 * it knows none; Optimist's own build: since boot (stk_least) */
static int32_t stk_least_ever(uint32_t *which)
{
#ifdef PLAT_STATS_APP
    _Static_assert(STK_MAIN == PLAT_STK_MAIN && STK_IRQ == PLAT_STK_IRQ && STK_C1 == PLAT_STK_C1,
                   "stackmark.h: the stacks in plat.h's order");
    uint32_t k;
    int32_t m = -1, l;
    *which = STK_MAIN;
    for (k = 0; k < STK_N; k++)
        if ((l = plat_stack_least_ever((int)k)) >= 0 && (m < 0 || l < m))
            m = l, *which = k;
    if (m >= 0)
        return m;
#endif
    return stk_least(which);
}
/* STACK LOW: a stack has been within STK_LOW bytes of its bottom (ever, as stk_least_ever counts): INFO's RAM line
 * amber, and what a RAM bar may show */
static int stk_low(void)
{
    uint32_t w;
    int32_t l = stk_least_ever(&w);
    return l >= 0 && (uint32_t)l < STK_LOW;
}
#endif
