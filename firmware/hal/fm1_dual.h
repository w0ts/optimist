/* SPDX-License-Identifier: GPL-3.0-only */
/* FM-1 second core (CPU1) bring-up and the shared mailbox. EXPERIMENTAL: validated
 * in the emulator only, never on a device (docs/DUAL-CORE.md).
 *
 * Start sequence: the one the stock M-VAVE application uses (its code at XIP
 * 0x02059a2e, disassembled with the vendor objdump):
 *   [0x01C7FFF8] = CPU1 entry      (the SPL RAM handoff vector; = vector slot 126, SOFT6)
 *   [0x10008]   |= 8               (kept until CPU1 answered, then restored; meaning unknown)
 *   C1_CON      |= 8, &= ~2        (run, out of reset)
 *   wait for CPU1 to write an acknowledgement
 * Stop (AC79 SDK cpu/wl82/debug.c, halting the other core): C1_CON &= ~8, |= 2.
 *
 * The entry (fm1_dual.S) sets CPU1's stacks and returns (rti) into C code, as
 * the stock entry does. CPU1 runs with its interrupts off, except one: with FELUCCA_DUAL_IDLE it sleeps
 * (idle) between jobs and CPU0 wakes it with soft interrupt 124 (SOFT4, the SDK's CPU1 IPI), whose
 * handler runs the job (fm1_dual_wake_*, below; dual.c).
 *
 * Flash rule: while CPU0 programs or erases the flash (XIP off), CPU1 must not
 * fetch from XIP. CPU1 runs XIP code only between a job's request and its done
 * flag, and both happen inside CPU0's audio interrupt; the rest of the time it
 * spins in fm1_dual_idle_ram (below, .ram_text, no calls, no constants). */
#pragma once
#include <stdint.h>
#include "fm1_flash.h"            /* RAMFN, FL_FAR, fl_csync */
#include "fm1_time.h"

#define FM1_C1_CON   (*(volatile uint32_t *)0x1EEE004u)   /* corex2 C1_CON: b1 reset/halt, b3 run */
#define FM1_SYSCLK1  (*(volatile uint32_t *)0x10008u)     /* stock: |= 8 around the CPU1 start */
#define FM1_C1_VEC   (*(volatile uint32_t *)0x01C7FFF8u)  /* CPU1 handoff vector */

static inline __attribute__((always_inline)) uint32_t fm1_cnum(void)                     /* this core's number: 0 or 1 */
{
    uint32_t id;
    __asm__ volatile("%0 = cnum" : "=r"(id));
    return id;
}
static inline __attribute__((always_inline)) void fm1_dual_sync(void) { __asm__ volatile("csync" ::: "memory"); }

/* shared state; every field is written by one core only (noted), read by the other */
typedef struct {
    volatile uint32_t req;        /* CPU0: job sequence number (a new value = a new job) */
    volatile uint32_t done;       /* CPU1: the last job finished (= req: idle) */
    volatile uint32_t arg;        /* CPU0: the job's argument (which parts) */
    volatile uint32_t hello;      /* CPU1: FM1_DUAL_HELLO once it runs C code */
    volatile uint32_t count;      /* CPU1: stage 1, incremented while idle */
    volatile uint32_t ping, pong; /* CPU0 / CPU1: pong follows ping while idle (liveness) */
    volatile uint32_t fault;      /* CPU1: its exception vector + 1 after a fault */
} fm1_dual_mb_t;
#define FM1_DUAL_HELLO 0x43505531u                        /* "CPU1" */
static fm1_dual_mb_t fm1_dual_mb;

/* CPU1 between jobs, from RAM: publish `done`, then wait for a request. COUNT != 0:
 * stage 1, count the idle loops. Returns the new request. */
RAMFN static uint32_t fm1_dual_idle_ram(uint32_t done, uint32_t count)
{
    fm1_dual_mb_t *mb = &fm1_dual_mb;
    uint32_t r;
    fl_csync();                                           /* the job's stores before `done` */
    mb->done = done;
    fl_csync();
    for (;;) {
        r = mb->req;
        if (r != done)
            break;
        if (mb->ping != mb->pong)
            mb->pong = mb->ping;
        if (count)
            mb->count++;
    }
    fl_csync();                                           /* the request's data after `req` */
    return r;
}

/* CPU1 after a fault (fm1_irq.h fm1_fault_c): report and stay in RAM, off the flash */
RAMFN static void fm1_dual_dead_ram(uint32_t vec)
{
    fm1_dual_mb.fault = vec + 1u;
    fl_csync();
    for (;;)
        ;
}

/* CPU1's stacks and its entry: like the stock entry, set the stack pointers, put the C entry into
 * reti, rti. Which of usp / ssp / sp CPU1's C code runs on after the rti is not known: each gets
 * its own 2 KiB (usp: the first, ssp: the second, sp: the third). */
#define FM1_C1_STACK_WORDS 1536u                         /* 6 KiB */
uint32_t fm1_c1_stack[FM1_C1_STACK_WORDS] __attribute__((section(".pool"), aligned(8), used));
void fm1_cpu1_main(void);                                 /* the C entry (src/dual.c), never returns */
__asm__(".section .text.fm1_cpu1,\"ax\",@progbits\n"
        "\t.globl fm1_cpu1_entry\n"
        "fm1_cpu1_entry:\n"
        "\tusp = fm1_c1_stack + 2048\n"
        "\tssp = fm1_c1_stack + 4096\n"
        "\tsp = fm1_c1_stack + 6144\n"
        "\treti = fm1_cpu1_main\n"
        "\trti\n"
        "\t.previous\n");
extern void fm1_cpu1_entry(void);

static void fm1_dual_cpu1_fault(uint32_t vec)            /* fm1_irq.h fm1_fault_c, on CPU1 */
{
    FL_FAR(fm1_dual_dead_ram)(vec);
}

/* start CPU1 and wait up to TIMEOUT_US for its hello; 1 = running. On a timeout CPU1 is put back
 * into reset (and stays there). Call with the vectors still writable (before fm1_guard_lock_top). */
static int fm1_dual_start(uint32_t timeout_us)
{
    uint32_t sys = FM1_SYSCLK1, t0;
    int ok;
    fm1_dual_mb.hello = 0;
    fm1_dual_mb.fault = 0;
    fm1_dual_sync();
    FM1_C1_VEC = (uint32_t)(uintptr_t)fm1_cpu1_entry;
    FM1_SYSCLK1 = sys | 8u;
    FM1_C1_CON |= 8u;
    FM1_C1_CON &= ~2u;
    t0 = fm1_ticks();
    while (fm1_dual_mb.hello != FM1_DUAL_HELLO && fm1_ticks() - t0 < timeout_us * FM1_TICKS_PER_US)
        ;
    ok = fm1_dual_mb.hello == FM1_DUAL_HELLO;
    FM1_SYSCLK1 = sys;
    if (!ok) {
        FM1_C1_CON &= ~8u;
        FM1_C1_CON |= 2u;
    }
    return ok;
}

/* ---- CPU1 asleep between jobs (FELUCCA_DUAL_IDLE). Soft interrupts 120..127 share one latch (ILAT,
 * fm1_irq.h); each core has its own interrupt configuration bank, CPU1's at +0x200 (as the emulator
 * models the stock firmware; not verified on a device). CPU0 never enables 124 in its bank. */
#define FM1_IRQ_C1_WAKE 124u                                 /* SOFT4 */
#define FM1_ICFG_C1(n) (*(volatile uint32_t *)(0x1EEF300u + 4u * ((n) >> 3)))
#define FM1_ILAT_SET_ (*(volatile uint32_t *)0x1EEF1A0u)     /* (= fm1_irq.h FM1_ILAT_SET / _CLR) */
#define FM1_ILAT_CLR_ (*(volatile uint32_t *)0x1EEF1A4u)
#define FM1_VEC_ ((volatile uint32_t *)0x01C7FE00u)
/* CPU0, before CPU1 starts (vectors writable): CPU1's bank all off but the wake, at priority 1 */
static void fm1_dual_wake_setup(void (*isr)(void))
{
    uint32_t i;
    for (i = 0; i < 16u; i++)
        *(volatile uint32_t *)(0x1EEF300u + 4u * i) = 0;
    FM1_ILAT_CLR_ = 1u << (FM1_IRQ_C1_WAKE - 120u);
    FM1_VEC_[FM1_IRQ_C1_WAKE] = (uint32_t)(uintptr_t)isr;
    FM1_ICFG_C1(FM1_IRQ_C1_WAKE) = ((1u << 1) | 1u) << ((FM1_IRQ_C1_WAKE & 7u) * 4u);
}
/* CPU0: a job is posted (after req) */
static inline __attribute__((always_inline)) void fm1_dual_wake(void)
{
    FM1_ILAT_SET_ = 1u << (FM1_IRQ_C1_WAKE - 120u);
}
/* CPU1, in the handler, before it looks at req: a later wake stays latched (no lost wake) */
static inline __attribute__((always_inline)) void fm1_dual_wake_ack(void)
{
    FM1_ILAT_CLR_ = 1u << (FM1_IRQ_C1_WAKE - 120u);
    __asm__ volatile("csync" ::: "memory");
}
/* CPU1: its interrupts on (icfg bit 8, sti), then asleep in RAM for good: every job runs in the
 * handler, which returns to the idle.
 * A core woken from `idle` runs 4 more issue slots before the interrupt enters (FM-1_996, measured
 * on CPU0 with TIMER3; the emulator models it). A bare `idle; goto idle` reaches the next `idle`
 * inside that window and goes back to sleep without ever taking the interrupt: CPU1 never ran its
 * first job, CPU0 waited DUAL_JOIN_US for it (the first audio half late, 6.5 ms at boot) and put
 * CPU1 into reset for good. Four csyncs after the `idle` cover the window (tools/build.py checks
 * that no `idle` is reached again within 4 instructions of one). */
RAMFN static void fm1_dual_sleep_ram(void)
{
    uint32_t v;
    __asm__ volatile("%0 = icfg" : "=r"(v));
    __asm__ volatile("icfg = %0" ::"r"(v | 0x100u) : "memory");
    __asm__ volatile("csync\n\tsti" ::: "memory");
    for (;;)
        __asm__ volatile("idle\n\tcsync\n\tcsync\n\tcsync\n\tcsync" ::: "memory");
}
/* the handler's entry, in RAM with the code it calls (core.h HOT: .ram_hot): as fm1_isr.S */
void fm1_cpu1_wake(void);                                 /* the C body (src/dual.c) */
__asm__(".section .ram_hot.isr_c1_wake,\"ax\",@progbits\n"
        "\t.globl isr_c1_wake\n"
        "isr_c1_wake:\n"
        "\t[--sp] = {psr, rets, reti}\n"
        "\t[--sp] = {r3-r0}\n"
        "\tcall fm1_cpu1_wake\n"
        "\t{r3-r0} = [sp++]\n"
        "\t{psr, rets, reti} = [sp++]\n"
        "\tcsync\n"
        "\trti\n"
        "\t.previous\n");
extern void isr_c1_wake(void);

/* hold CPU1 in reset (a timeout, a fault): it stops wherever it is */
static void fm1_dual_halt(void)
{
    FM1_C1_CON &= ~8u;
    FM1_C1_CON |= 2u;
    fm1_dual_sync();
}
