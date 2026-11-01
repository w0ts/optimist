/* SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0 */
/* The BR/EDR table's offsets and the frame control block (docs/BT-CLASSIC.md §3, §4): data only, no register, no
 * assembly, so the host tests can include it (tests/bt_ltx_test.c); fm1_bredr.h includes it. */
#pragma once
#include <stdint.h>
#include "fm1_cc.h"

/* the table (BR §3): offsets from its base */
#define FM1_BR_TAB_SIZE 4196u
#define FM1_BR_OFF_FRQ 32u                    /* frq_table[0] */
#define FM1_BR_OFF_FRAME 368u                 /* frame n at 368 + 180 n (below 0x800) */
#define FM1_BR_OFF_ESCO 3808u
#define FM1_BR_OFF_FHS 3936u                  /* the 20-byte FHS RX buffer */
#define FM1_BR_OFF_EIR 3956u                  /* the 240-byte EIR RX buffer */

/* the frame control block (BR §4), 180 bytes, read and written by the engine's DMA */
struct fm1_br_frame {
    volatile uint16_t bdaddr[3], bch_lo, bch_mid;
    volatile uint8_t bch_hi, cx, miscntl, format, rxtog, txtog;
    volatile uint16_t wincntl, rxmaxbuf;
    volatile uint8_t txpwr, txfreq, rxrssi, rxfreq;
    volatile uint16_t txptr[2], rxptr[2], linkcntl[2], txphdr[2], rxstat[2], rxphdr[2];
    volatile uint16_t clk_off_lo, clk_off_hi, rx_clkn_lo, rx_clkn_hi, bitoff, rxbit;
    volatile uint16_t cryptk[8], escorxstat[2], lmprxptr[2], aclprio, escoprio, bufcntl;
    volatile uint8_t txbufcntl[2], rxbufcntl[2], rxbufstat[2];
    volatile uint16_t tws[5];
    volatile uint8_t twssta[2];
    volatile uint32_t txpwer, rxgain0, txset, rxset, pll_comp, mdm_set;
    volatile uint16_t rssi[4];
    volatile uint32_t anl_out;
    volatile uint8_t tws_copy[36];
};
_Static_assert(sizeof(struct fm1_br_frame) == 180u && __builtin_offsetof(struct fm1_br_frame, linkcntl) == 32u &&
                   __builtin_offsetof(struct fm1_br_frame, clk_off_lo) == 48u &&
                   __builtin_offsetof(struct fm1_br_frame, aclprio) == 84u &&
                   __builtin_offsetof(struct fm1_br_frame, txbufcntl) == 90u &&
                   __builtin_offsetof(struct fm1_br_frame, txpwer) == 108u &&
                   __builtin_offsetof(struct fm1_br_frame, rssi) == 132u,
               "fm1_br_frame: BR §4's offsets");

FM1_INLINE struct fm1_br_frame *fm1_br_frame(uint8_t *table, uint32_t n)
{
    return (struct fm1_br_frame *)(void *)(table + FM1_BR_OFF_FRAME + 180u * n);
}

FM1_INLINE void fm1_br_frame_clear(struct fm1_br_frame *f)
{
    volatile uint8_t *b = (volatile uint8_t *)(void *)f;
    uint32_t i;
    for (i = 0; i < sizeof *f; i++)
        b[i] = 0;
}
