/* SPDX-License-Identifier: GPL-3.0-only */
/* CPU0's stacks as the linker scripts place them (firmware/app.ld; the release's fm1base/device/app.ld and app_wifi.ld),
 * for system/stackmark.h: the symbols under names of their own (main.c, hal/fm1_guard.h, the release's opt_core.c
 * declare some of them with other types), and the stack pointer now. */
#ifndef FM1_STACK_H
#define FM1_STACK_H
#include <stdint.h>

extern uint32_t fm1_stk_ulo[] __asm__("_ustack_lo"), fm1_stk_utop[] __asm__("_ustack_top");
extern uint32_t fm1_stk_slo[] __asm__("_sstack_lo"), fm1_stk_stop[] __asm__("_sstack_top");
extern uint8_t fm1_stk_pool_end[] __asm__("_pool_end"), fm1_stk_guard0[] __asm__("_guard0");

static inline uint32_t fm1_sp(void)
{
    uint32_t sp;
    __asm__ volatile("%0 = sp" : "=r"(sp));
    return sp;
}
#endif
