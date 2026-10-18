/* SPDX-License-Identifier: GPL-3.0-only */
/* The simulator's TIMER5 (firmware/hal/fm1_timer.h): the 10 kHz tick is an event of the scheduler (sim.c). */
#pragma once
#include <stdint.h>
#include "fm1_cc.h"
#include "fm1_irq.h"

static void sim_timer5_start(void);               /* sim.c: the tick's events from now on */

FM1_INLINE void fm1_timer5_start(void (*isr)(void), uint32_t prio)
{
    fm1_irq_attach(FM1_IRQ_TIMER5, isr, prio);
    sim_timer5_start();
}

FM1_INLINE void fm1_timer5_ack(void) {}
