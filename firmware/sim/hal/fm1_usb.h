/* SPDX-License-Identifier: GPL-3.0-only */
/* The simulator's USB0 (firmware/hal/fm1_usb.h): a model of the SIE registers src/io/usb/usb.c reads and writes,
 * enough for its MIDI paths (EP1 OUT / IN) with a host that has configured the device (sim.c sets usb.config and
 * runs ep1_config, as a SET_CONFIGURATION). EP1 OUT packets come from the page (sim_midi_in), EP1 IN packets go to
 * it (sim_midi_out); no bus reset, no EP0 traffic, every IN packet taken at once. sim.c sim_sie_rd / sim_sie_wr. */
#pragma once
#include <stdint.h>
#include "fm1_cc.h"

static struct {
    uint8_t on;                                   /* attached (USB_NRST) */
    uint8_t rd;                                   /* the last SIE read's data */
    uint8_t *rxbuf[4];                            /* EPn OUT DMA buffers */
} sim_usb;

static uint32_t sim_sie_rd(uint32_t r);           /* sim.c */
static void sim_sie_wr(uint32_t r, uint32_t v);
static void sim_usb_tx(uint32_t ep, const uint8_t *p, uint32_t n);

FM1_INLINE void fm1_usb_reset(void) { sim_usb.on = 0; }
FM1_INLINE void fm1_usb_attach(void *ep0)
{
    uint32_t n;
    for (n = 1; n <= 3u; n++)
        sim_usb.rxbuf[n] = (uint8_t *)ep0;
    sim_usb.on = 1;
}
FM1_INLINE void fm1_usb_off(void) { sim_usb.on = 0; }

FM1_INLINE uint32_t fm1_usb_sie_on(void) { return sim_usb.on ? 4u : 0u; }
FM1_INLINE void fm1_usb_sie_wr_start(uint32_t r, uint32_t v) { sim_sie_wr(r & 0xFFu, v & 0xFFu); }
FM1_INLINE void fm1_usb_sie_rd_start(uint32_t r) { sim_usb.rd = (uint8_t)sim_sie_rd(r & 0xFFu); }
FM1_INLINE uint32_t fm1_usb_sie_done(void) { return 0x8000u; }
FM1_INLINE uint32_t fm1_usb_sie_data(void) { return sim_usb.rd; }

FM1_INLINE void fm1_usb_ep0_buf(void *p) { (void)p; }
FM1_INLINE void fm1_usb_ep_txbuf(uint32_t ep, void *p) { (void)ep; (void)p; }
FM1_INLINE void fm1_usb_ep_rxbuf(uint32_t ep, void *p) { sim_usb.rxbuf[ep & 3u] = (uint8_t *)p; }
FM1_INLINE void fm1_usb_ep0_send(void *p, uint32_t n) { (void)p; (void)n; }
FM1_INLINE void fm1_usb_ep_send(uint32_t ep, void *p, uint32_t n) { sim_usb_tx(ep, (const uint8_t *)p, n); }
FM1_INLINE void fm1_usb_rx_sync(void) {}
FM1_INLINE void fm1_usb_ep_enable(uint32_t eps) { (void)eps; }
FM1_INLINE uint32_t fm1_usb_sof_take(void) { return sim_usb.on; }
