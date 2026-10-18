/* SPDX-License-Identifier: GPL-3.0-only */
/* The simulator's interrupt controller (firmware/hal/fm1_irq.h): cli / sti set the global enable (sim_core.h sim_ie),
 * a handler attached to TIMER5 or ALNK0 is called by the scheduler (sim.c), the crash record is the same struct. */
#pragma once
#include <stdint.h>
#include "sim_core.h"

enum { FM1_IRQ_EXCEPTION = 1, FM1_IRQ_ALNK0 = 11, FM1_IRQ_SPI1 = 16, FM1_IRQ_UART1 = 20,
       FM1_IRQ_SARADC = 24, FM1_IRQ_SPI2 = 37, FM1_IRQ_TIMER5 = 63, FM1_IRQ_SOFT0 = 120 };

static uint8_t sim_icfg8;                         /* icfg bit 8 (fm1_irq_enable_all) */
static inline uint32_t fm1_icfg(void) { return (sim_icfg8 ? 0x100u : 0u) | (sim_ie ? 0x200u : 0u); }
static inline void fm1_icfg_set(uint32_t v)
{
    sim_icfg8 = (v & 0x100u) != 0;
    if (v & 0x200u)
        sim_irq_on();
    else
        sim_ie = 0;
}
static inline void fm1_irq_off(void) { sim_ie = 0; }
static inline void fm1_irq_on(void) { sim_irq_on(); }
static void sim_idle(void);                       /* sim.c: the clock to the next event */
static inline void fm1_idle(void) { sim_idle(); }

#define FM1_CRASH_MAGIC 0x43525348u          /* "CRSH" */
typedef struct {
    uint32_t magic, count, vec, pc, rets, emu, dbg, sp, psr, icfg, uptime_ms;
    uint32_t etm[4];
    uint32_t early;                          /* consecutive crashes < 3 s after boot */
} fm1_crash_t;
fm1_crash_t fm1_crash;

static void fm1_fault(const fm1_crash_t *c);   /* application: report, then reset */

static void fm1_irq_init(void) { sim_ie = 0; }
static void fm1_irq_attach(uint32_t n, void (*h)(void), uint32_t prio) { (void)prio; sim_attach(n, h); }
static void fm1_irq_mask(uint32_t n) { sim_hold(n, 1); }
static inline void fm1_irq_hold(uint32_t n, int hold) { sim_hold(n, hold); }
static void fm1_irq_enable_all(void)
{
    sim_icfg8 = 1;
    fm1_irq_on();
}
