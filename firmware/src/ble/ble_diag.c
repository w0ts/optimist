/* SPDX-License-Identifier: GPL-3.0-only */
/* The console's `blell` (docs/BLE-STACK.md §12.7): ble_diag.h's counters, the link layer's state and, on the FM-1, the
 * engine's registers, one "key value" line each in the console's style. Included after ble_ll.c (it reads bll). */
#include "ble_diag.h"

typedef void (*ble_diag_put)(const char *s);

static void bd_hex(ble_diag_put put, uint32_t v, uint32_t digits)
{
    char b[9];
    uint32_t i;
    for (i = 0; i < digits && i < 8u; i++)
        b[i] = "0123456789ABCDEF"[(v >> ((digits - 1u - i) * 4u)) & 15u];
    b[i] = 0;
    put(b);
}

static void bd_dec(ble_diag_put put, uint32_t v)
{
    char b[11];
    uint32_t n = 10;
    b[10] = 0;
    do
        b[--n] = (char)('0' + v % 10u);
    while ((v /= 10u) != 0 && n);
    put(b + n);
}

static void bd_kv(ble_diag_put put, const char *k, uint32_t v)
{
    put(k);
    put(" ");
    bd_dec(put, v);
    put("\r\n");
}

static void bd_kx(ble_diag_put put, const char *k, uint32_t v, uint32_t digits)
{
    put(k);
    put(" ");
    bd_hex(put, v, digits);
    put("\r\n");
}

/* the last n (at most BLE_DIAG_LAST) of a ring of opcodes, oldest first */
static void bd_ops(ble_diag_put put, const char *k, const uint8_t *ring, uint32_t total)
{
    uint32_t i, n = total < BLE_DIAG_LAST ? total : BLE_DIAG_LAST;
    bd_kv(put, k, total);
    put(k);
    put("_last");
    for (i = total - n; i != total; i++) {
        put(" ");
        bd_hex(put, ring[i & (BLE_DIAG_LAST - 1u)], 2);
    }
    put("\r\n");
}

static const char *const BD_EV[BDE_COUNT] = {
    "-", "enable", "adv_start", "adv_stop", "adv_drop", "cind_rx", "cind_ok", "cind_rej", "conn_set", "first_evt",
    "first_rx", "rx_bad", "rx_desync", "c3_zero", "ctl_rx", "ctl_tx", "instant", "close", "busy"};
static const char *const BD_LL[3] = {"off", "adv", "conn"};
static const char *const BD_HW[3] = {"off", "adv", "conn"};

static void bd_regs(ble_diag_put put, const struct ble_diag_regs *r)
{
    static const char *const COL[BLE_DIAG_COLS] = {"col0", "col1", "col2", "col3", "col4", "col5", "col6", "col14",
                                                   "col15"};
    uint32_t i;
    put("hw_state ");
    put(BD_HW[r->hw_state % 3u]);
    put("\r\n");
    for (i = 0; i < BLE_DIAG_COLS; i++)
        bd_kx(put, COL[i], r->col[i], 4);
    bd_kv(put, "col2_state", r->col[2] >> 12 & 7u);       /* HW §2.5: 2 advertising, 7 peripheral */
    bd_kx(put, "clock", r->clock, 6);
    bd_kx(put, "ien", r->ien, 4);
    bd_kx(put, "ipnd", r->ipnd, 4);
    bd_kx(put, "g2en", r->g2en, 4);
    bd_kx(put, "g2pnd", r->g2pnd, 4);
    bd_kx(put, "bbstat", r->stat, 4);
    bd_kx(put, "txtog", r->txtog, 4);
    bd_kx(put, "rxtog", r->rxtog, 4);
    bd_kv(put, "rx_next", r->rx_next);
    bd_kv(put, "rx_sn", r->rx_sn);
    bd_kv(put, "tx_n", r->tx_n);
    bd_kv(put, "win_wide", r->win_wide);
    bd_kx(put, "txbufcntl", (uint32_t)r->txbufcntl[0] << 8 | r->txbufcntl[1], 4);
    bd_kx(put, "rxbufcntl", (uint32_t)r->rxbufcntl[0] << 8 | r->rxbufcntl[1], 4);
    bd_kx(put, "rxstat", (uint32_t)r->rxstat[0] << 16 | r->rxstat[1], 8);
    bd_kx(put, "rxahdr", (uint32_t)r->rxahdr[0] << 16 | r->rxahdr[1], 8);
    bd_kx(put, "rxdhdr", (uint32_t)r->rxdhdr[0] << 16 | r->rxdhdr[1], 8);
    bd_kx(put, "txdhdr", (uint32_t)r->txdhdr[0] << 16 | r->txdhdr[1], 8);
    bd_kx(put, "intframe", r->intframe, 4);
    bd_kx(put, "format", r->format, 4);
    bd_kx(put, "optcntl", r->optcntl, 4);
    bd_kv(put, "evtcount", r->evtcount);
    bd_kv(put, "wincntl0", r->wincntl0);
}

static void bd_cind(ble_diag_put put, const struct ble_diag *d)
{
    uint32_t chm = (uint32_t)d->cind_chm[0] | (uint32_t)d->cind_chm[1] << 8 | (uint32_t)d->cind_chm[2] << 16 |
                   (uint32_t)d->cind_chm[3] << 24;
    bd_kv(put, "cind_rx", d->cind_rx);
    bd_kv(put, "cind_ok", d->cind_ok);
    bd_kv(put, "cind_rej", d->cind_rej);
    bd_kv(put, "cind_rej_why", d->cind_rej_why);
    bd_kx(put, "cind_hdr", d->cind_hdr, 2);
    bd_kx(put, "cind_aa", d->cind_aa, 8);
    bd_kx(put, "cind_crc", d->cind_crc, 6);
    bd_kv(put, "cind_win_size", d->cind_win_size);
    bd_kv(put, "cind_win_off", d->cind_win_off);
    bd_kv(put, "cind_interval", d->cind_interval);
    bd_kv(put, "cind_latency", d->cind_latency);
    bd_kv(put, "cind_timeout", d->cind_timeout);
    put("cind_chm ");
    bd_hex(put, d->cind_chm[4], 2);
    bd_hex(put, chm, 8);
    put("\r\n");
    bd_kv(put, "cind_hop", d->cind_hop);
    bd_kv(put, "cind_sca", d->cind_sca);
    bd_kv(put, "cind_isr_us", d->cind_isr_us);
    bd_kv(put, "cind_slot_irq", d->cind_slot_irq);
    bd_kv(put, "cind_slot_set", d->cind_slot_set);
    bd_kv(put, "first_evt", d->first_evt);
    bd_kv(put, "first_rx_us", d->first_rx_us);
    bd_kv(put, "first_rx_evt", d->first_rx_evt);
}

static void bd_conn(ble_diag_put put, const struct ble_diag *d)
{
    bd_kv(put, "evt_irqs", d->evt_irqs);
    bd_kv(put, "rx_irqs", d->rx_irqs);
    bd_kv(put, "conn_events", d->conn_events);
    bd_kv(put, "c3_zero", d->c3_zero);
    bd_kv(put, "evt_same", d->evt_same);
    bd_kv(put, "last_evt", d->last_evt);
    bd_kv(put, "rx_good", d->rx_good);
    bd_kv(put, "rx_crc_bad", d->rx_crc_bad);
    bd_kx(put, "rx_bad_stat", d->rx_bad_stat, 4);
    bd_kv(put, "rx_repeat", d->rx_repeat);
    bd_kv(put, "rx_empty", d->rx_empty);
    bd_kv(put, "rx_nothing", d->rx_nothing);
    bd_kv(put, "rx_desync", d->rx_desync);
    bd_kv(put, "tx_queued", d->tx_queued);
    bd_kv(put, "tx_acked", d->tx_acked);
    bd_kv(put, "tx_none", d->tx_none);
    bd_kv(put, "clk_step_max", d->clk_step_max);
    bd_ops(put, "ctl_rx", d->ctl_rx, d->ctl_rx_n);
    bd_ops(put, "ctl_tx", d->ctl_tx, d->ctl_tx_n);
    bd_ops(put, "att_rx", d->att_rx, d->att_rx_n);
    bd_kv(put, "closes", d->closes);
    bd_kx(put, "close_reason", d->close_reason, 2);
    bd_kv(put, "close_by", d->close_by);
    bd_kv(put, "close_evt", d->close_evt);
    bd_kv(put, "close_since_rx_us", d->close_since_rx_us);
    bd_kv(put, "close_since_start_us", d->close_since_start_us);
    bd_kv(put, "sup_timeouts", d->sup_timeouts);
    bd_kv(put, "estab_fails", d->estab_fails);
    bd_kv(put, "peer_terms", d->peer_terms);
}

#if defined(BLE_HW_WL82) && BLE_HW_WL82
static const uint8_t BLE_DIAG_COL[BLE_DIAG_COLS] = {0, 1, 2, 3, 4, 5, 6, 14, 15};   /* the columns read (op 2) */
/* the engine's side (the WL82 driver, ble_hw_wl82.c, in the same unity build): column reads (op 2), the interrupt
 * registers, the control block words the link runs on. Main loop, the BLE interrupts held, only after
 * ble_hw_wl82_start. */
static void ble_hw_diag_regs(struct ble_diag_regs *r)
{
    uint32_t i, w[5];
    r->valid = 1;
    r->hw_state = drv.state;
    r->rx_next = drv.rx_next;
    r->rx_sn = drv.rx_sn;
    r->tx_n = drv.tx_n;
    r->win_wide = drv.win_wide;
    for (i = 0; i < BLE_DIAG_COLS; i++)
        r->col[i] = (uint16_t)fm1_ble_col_rd(HW_LINK, BLE_DIAG_COL[i]);
    r->clock = fm1_ble_clock(HW_LINK);
    fm1_ble_irq_regs(w);
    r->ien = w[0];
    r->ipnd = w[1];
    r->g2en = w[2];
    r->g2pnd = w[3];
    r->stat = w[4];
    r->txtog = CB->txtog;
    r->rxtog = CB->rxtog;
    r->intframe = CB->intframe;
    r->format = CB->format;
    r->optcntl = CB->optcntl;
    r->evtcount = CB->evtcount;
    r->wincntl0 = CB->wincntl[0];
    for (i = 0; i < 2u; i++) {
        r->rxstat[i] = CB->rxstat[i];
        r->rxahdr[i] = CB->rxahdr[i];
        r->rxdhdr[i] = CB->rxdhdr[i];
        r->txdhdr[i] = CB->txdhdr[i];
        r->txbufcntl[i] = CB->txbufcntl[i];
        r->rxbufcntl[i] = CB->rxbufcntl[i];
    }
}
#endif

/* everything, in the order docs/BLE-STACK.md §12.7 lists it; r: the engine's registers (r->valid 0: none) */
static void ble_diag_print(ble_diag_put put, const struct ble_diag_regs *r)
{
    const struct ble_diag *d = &ble_dg;
    uint32_t i, n;
    put("ll_state ");
    put(BD_LL[bll.state % 3u]);
    put("\r\n");
    bd_kv(put, "ll_enabled", bll.enabled);
    bd_kv(put, "ll_established", bll.state == LL_CONN && bll.established);
    bd_kv(put, "ll_interval", bll.state == LL_CONN ? bll.interval : 0u);
    bd_kv(put, "ll_lproc", bll.lproc);
    bd_kv(put, "ll_rproc", bll.rproc);
    bd_kv(put, "now_us", d->now_us);
    if (r && r->valid)
        bd_regs(put, r);
    bd_kv(put, "adv_starts", d->adv_starts);
    bd_kv(put, "adv_events", d->adv_events);
    bd_kx(put, "adv_col2", d->adv_col2, 4);
    bd_kx(put, "adv_col14", d->adv_col14, 4);
    bd_kx(put, "adv_col15", d->adv_col15, 4);
    bd_kv(put, "adv_rx", d->adv_rx);
    bd_kv(put, "scan_req", d->scan_req);
    bd_kv(put, "adv_drop", d->adv_drop);
    bd_kx(put, "adv_drop_stat", d->adv_drop_stat, 4);
    bd_kx(put, "adv_drop_hdr", d->adv_drop_hdr, 4);
    bd_kv(put, "busy_max", d->busy_max);
    bd_kv(put, "busy_timeouts", d->busy_timeouts);
    bd_cind(put, d);
    bd_conn(put, d);
    n = d->ev_n < BLE_DIAG_RING ? d->ev_n : BLE_DIAG_RING;
    bd_kv(put, "events", d->ev_n);
    for (i = d->ev_n - n; i != d->ev_n; i++) {       /* "ev T_US NAME ARG", oldest first */
        uint32_t k = i & (BLE_DIAG_RING - 1u);
        put("ev ");
        bd_dec(put, d->ev[k].t_us);
        put(" ");
        put(d->ev[k].code < BDE_COUNT ? BD_EV[d->ev[k].code] : "?");
        put(" ");
        bd_hex(put, d->ev[k].arg, 4);
        put("\r\n");
    }
}

/* `blell clear` (the BLE interrupts held): the counters and the ring from zero; the clock goes on */
static void ble_diag_clear(void)
{
    uint8_t *p = (uint8_t *)&ble_dg;
    uint32_t i;
    for (i = 0; i < sizeof ble_dg; i++)
        p[i] = 0;
    ble_dg.magic = BLE_DIAG_MAGIC;
    ble_dg.first_rx_evt = ble_dg.first_evt = 0xFFFFu;
}
