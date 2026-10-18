/* SPDX-License-Identifier: GPL-3.0-only */
/* The simulator's machine: the state the host HAL headers (firmware/sim/hal/fm1_*.h) share with the platform
 * (firmware/sim/sim.c). One compilation unit (sim.c includes felucca.c), so everything here is static; the functions
 * declared here are defined in sim.c, after the firmware.
 *
 * Time: sim_t counts TIMER4 ticks (24 MHz) since power-on, 64 bit. Code does not take simulated time to run: each
 * fm1_ticks() read moves the clock one tick (so a spin on the timer ends), fm1_delay_* moves it by the delay, and the
 * scheduler (sim.c sim_run) moves it from one event to the next while the main loop would sleep. Events: the 10 kHz
 * TIMER5 tick and the audio DMA's half-buffer switch; their interrupt handlers run when the clock passes them, the
 * interrupts are on (cli / sti: sim_ie) and no handler runs already (no nesting). */
#pragma once
#include <stdint.h>

#define SIM_TICKS_PER_S 24000000u
#define SIM_FLASH_SIZE 0x100000u                  /* the FM-1's 1 MiB SPI NOR (hal/fm1_flash_map.h: key_mac ends it) */

static uint64_t sim_t;                            /* TIMER4 ticks since power-on */
static uint64_t sim_next = ~(uint64_t)0;          /* the earliest event due */
static uint8_t sim_ie;                            /* the global interrupt enable (icfg bit 9: cli / sti) */
static uint8_t sim_in_isr;                        /* a handler runs (no nesting in the simulator) */
static uint8_t sim_flash_img[SIM_FLASH_SIZE] __attribute__((aligned(16)));   /* the flash, as a RAM image */

static void sim_events(void);                     /* the events due at sim_t, their handlers when they may run */
static void sim_advance(uint64_t ticks);          /* the clock forward, every event on the way */
static void sim_irq_on(void);                     /* sti: a pending handler runs now */
static void sim_attach(uint32_t n, void (*h)(void));
static void sim_hold(uint32_t n, int hold);       /* one source held off in the interrupt controller */
static void sim_stop(uint32_t why) __attribute__((noreturn));   /* reboot / UBOOT / fatal: back to the export */
static void sim_flash_init(void);                 /* the image erased (0xFF), once */

enum { SIM_ST_REBOOT = 1u, SIM_ST_UBOOT = 2u, SIM_ST_FATAL = 4u };

static inline __attribute__((always_inline)) uint32_t sim_tick(void)
{
    if (++sim_t >= sim_next)
        sim_events();
    return (uint32_t)sim_t;
}
