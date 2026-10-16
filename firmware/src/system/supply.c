/* SPDX-License-Identifier: GPL-3.0-only */
/* FELUCCA_STOCK_SUPPLY (the builder's STOCK_SUPPLY, EXPERIMENTAL, on by default with the second core): the FM-1's own supply
 * start-up, at the top of the boot (system/main.c, IRQs off, before the second core, the audio and the BLE radio): the stock
 * M-VAVE application's sequence (hal/fm1_supply.h fm1_supply_stock: the 1.4 V rail from the LDO to the DC-DC, VDC14 7 =
 * 1.60 V, SYSVDD 15 = 1.38 V last, then the low-voltage detector at 2.6 V), read back. A wrong read-back keeps the second core off (dual_mode.h SUPPLY); the rails
 * are never lowered. SYSTEM > INTERNALS shows the rails (op_project.c SUPPLY), the console's status too (supply_*). */
#ifndef FELUCCA_STOCK_SUPPLY
#define FELUCCA_STOCK_SUPPLY 0
#endif
#if FELUCCA_STOCK_SUPPLY
#include "fm1_supply.h"
static struct {
    uint8_t was, now;                              /* VDC14 << 4 | SYSVDD: as the boot loader left them, after */
    uint8_t err;                                   /* FM1_SUP_* (0: read back right) */
    uint8_t done;                                  /* 1 once applied this boot */
    uint32_t regs;                                 /* P3_ANA_CON0 | CON5 << 8 | CON6 << 16 | CON9 << 24 after */
} supply;

static void supply_boot(void)
{
    supply.was = (uint8_t)fm1_supply_now();
    supply.err = (uint8_t)fm1_supply_stock();
    if (!supply.err)
        supply.err = (uint8_t)fm1_lvr_stock();     /* stock's 2.6 V low-voltage detector after the rails (V15 app_core) */
    supply.now = (uint8_t)fm1_supply_now();
    supply.regs = fm1_supply_regs();
    supply.done = 1;
}

/* the rails in plain words (SYSTEM > INTERNALS): "1.38 V  1.60 V", and the 1.4 V rail's regulator */
static inline uint32_t supply_sysvdd_mv(uint32_t code) { return 930u + 30u * (code & 15u); }
static inline uint32_t supply_vdc14_mv(uint32_t code) { return 1250u + 50u * (code & 7u); }
static inline int supply_on_dcdc(uint32_t regs) { return (regs & 0x0Cu) == 0x08u; }
#endif
