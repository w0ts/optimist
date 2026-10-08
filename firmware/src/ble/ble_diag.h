/* SPDX-License-Identifier: GPL-3.0-only */
/* Link-layer diagnostics for the console's `blell` (docs/BLE-STACK.md §12.7): plain counters, the last CONNECT_IND,
 * the last control and ATT opcodes, and a ring of the last 32 timestamped events. RAM only, a few hundred bytes.
 * Written only from the BLE interrupts' context (ble_hw.h: the driver's ISRs, or the main loop with them held), so
 * nothing here needs a lock; the console reads it (a value may be one event stale) and `blell clear` zeroes it with
 * the BLE interrupts held. Recording never changes what the stack does. */
#ifndef BLE_DIAG_H
#define BLE_DIAG_H
#include <stdint.h>
#include "ble_cfg.h"

#define BLE_DIAG_MAGIC 0x4C454C42u      /* "BLEL": tests/ble_emu_test.py finds the block in a RAM dump by it */
#define BLE_DIAG_RING 32u               /* events kept (a power of two) */
#define BLE_DIAG_LAST 8u                /* opcodes kept (a power of two) */

/* event codes of the ring (ble_diag.c prints their names) */
enum {
    BDE_NONE, BDE_ENABLE, BDE_ADV_START, BDE_ADV_STOP, BDE_ADV_DROP, BDE_CIND_RX, BDE_CIND_OK, BDE_CIND_REJ,
    BDE_CONN_SET, BDE_FIRST_EVT, BDE_FIRST_RX, BDE_RX_BAD, BDE_RX_DESYNC, BDE_C3_ZERO, BDE_CTRL_RX, BDE_CTRL_TX,
    BDE_INSTANT, BDE_CLOSE, BDE_BUSY, BDE_COUNT
};
/* why a CONNECT_IND was not taken (cind_rej_why) */
enum {
    BDR_OK, BDR_NOT_ADV, BDR_FORMAT, BDR_RXADD, BDR_ADVA, BDR_PARAMS, BDR_WINSIZE, BDR_WINOFF, BDR_HOP, BDR_CHM,
    BDR_AA
};
/* who closed a connection (close_by) */
enum { BDC_LOCAL, BDC_PEER, BDC_SUPERVISION, BDC_ESTABLISH, BDC_PROC_TIMEOUT, BDC_PROTOCOL, BDC_TERM_UNACKED };

struct ble_diag {
    uint32_t magic;
    /* advertising (driver) */
    uint32_t adv_starts, adv_events, adv_rx, scan_req, adv_drop;
    uint16_t adv_drop_stat, adv_drop_hdr;          /* the last dropped one: RXSTAT, RXAHDR */
    uint16_t adv_col2, adv_col14, adv_col15;       /* read back right after the last advertising start */
    uint16_t busy_max;                             /* the longest 0x28038 bit1 wait when a link stopped (polls) */
    uint32_t busy_timeouts;                        /* ... that ran out (the engine still busy) */
    /* CONNECT_IND (driver + link layer) */
    uint32_t cind_rx, cind_ok, cind_rej;
    uint8_t cind_rej_why, cind_hdr, cind_win_size, cind_hop, cind_sca, cind_chm[5];
    uint32_t cind_aa, cind_crc;
    uint16_t cind_win_off, cind_interval, cind_latency, cind_timeout;
    uint32_t cind_isr_us;                          /* RX IRQ entry -> state 7 written (us) */
    uint16_t cind_slot_irq, cind_slot_set;         /* the link clock (625 us slots, column 0) at both */
    uint32_t first_rx_us;                          /* state 7 written -> the first data PDU's RX IRQ (us) */
    uint16_t first_rx_evt, first_evt;              /* their event counters (0xFFFF: none yet) */
    /* the connection (driver) */
    uint32_t evt_irqs, rx_irqs, conn_events, c3_zero, evt_same;
    uint32_t rx_good, rx_crc_bad, rx_repeat, rx_empty, rx_nothing, rx_desync;
    uint16_t rx_bad_stat, last_evt;
    uint32_t tx_queued, tx_acked, tx_none;         /* tx_none: a free buffer, nothing queued (the engine sends empty) */
    uint32_t clk_step_max;                         /* the largest link clock step seen between two reads (slots) */
    /* the link layer */
    uint32_t ctl_rx_n, ctl_tx_n, att_rx_n;
    uint8_t ctl_rx[BLE_DIAG_LAST], ctl_tx[BLE_DIAG_LAST], att_rx[BLE_DIAG_LAST];
    uint32_t closes, sup_timeouts, estab_fails, peer_terms;
    uint8_t close_reason, close_by;
    uint16_t close_evt;                            /* the event counter at the last close */
    uint32_t close_since_rx_us, close_since_start_us;   /* now - the last RX, now - CONNECT_IND, at that close */
    /* the event ring */
    uint32_t now_us, ev_n;
    struct { uint32_t t_us; uint8_t code, pad; uint16_t arg; } ev[BLE_DIAG_RING];
};

static struct ble_diag ble_dg = {.magic = BLE_DIAG_MAGIC, .first_rx_evt = 0xFFFFu, .first_evt = 0xFFFFu};

/* the engine's side, read when `blell` prints (the WL82 driver's ble_hw_diag_regs, the BLE interrupts held) */
#define BLE_DIAG_COLS 9u                /* columns 0-6, 14, 15 (ble_diag.c) */
struct ble_diag_regs {
    uint8_t valid, hw_state, rx_next, rx_sn, tx_n, win_wide;
    uint16_t col[BLE_DIAG_COLS];
    uint32_t ien, ipnd, g2en, g2pnd, stat, clock;
    uint16_t txtog, rxtog, intframe, format, optcntl, evtcount, wincntl0, rxstat[2], rxahdr[2], rxdhdr[2], txdhdr[2];
    uint8_t txbufcntl[2], rxbufcntl[2];
};

/* the driver's diagnostic clock: microseconds since the radio was first started (ble_hw.h) */
BLE_API uint32_t ble_hw_diag_now(void);

static inline void ble_diag_ev(uint8_t code, uint32_t arg)
{
    uint32_t i = ble_dg.ev_n++ & (BLE_DIAG_RING - 1u);
    ble_dg.now_us = ble_hw_diag_now();
    ble_dg.ev[i].t_us = ble_dg.now_us;
    ble_dg.ev[i].code = code;
    ble_dg.ev[i].arg = (uint16_t)arg;
}

static inline void ble_diag_last(uint8_t *ring, uint32_t *n, uint8_t v)
{
    ring[(*n)++ & (BLE_DIAG_LAST - 1u)] = v;
}

#endif
