/* SPDX-License-Identifier: GPL-3.0-only */
/* BLE MIDI (FELUCCA_BLE, EXPERIMENTAL; route C, our own stack in ../../ble/, docs/BLE-STACK.md) into and out of the
 * MIDI router, the way USB and TRS MIDI do it (usb.c, midi_uart.c):
 *   in   the stack decodes a central's BLE-MIDI writes, in the BLE interrupts, into USB-MIDI event packets. They wait
 *        in ble_in_q (one producer: the BLE interrupts) until the TIMER5 tick moves them into midi_in_q, whose
 *        producers all run in TIMER5; seq.c's events_block then plays them as it plays USB's. Channel messages only,
 *        as stock does (BLE in -> the synth): clock and transport from BLE are left out (clock_sync.c follows USB
 *        and TRS). Their time: the arrival less how much earlier than the packet's newest one their timestamp is.
 *   out  midi_out_event (the audio ISR: the FM-1's own notes, the stream USB gets) queues each event with its
 *        millisecond in ble_out_q while a central listens; the stack packs them into notifications at the next
 *        connection event, with real BLE-MIDI timestamps.
 *   ble_midi_route  bit 0 in, bit 1 out; both by default, as stock. Nothing is bridged between USB / TRS and BLE.
 * Until a baseband driver exists (docs/BLE-HW-FACTS.md), ble/ble_hw_stub.c stands in and nothing is ever sent. */
#include "../../ble/ble_stack.c"

#define BMQ 64u
enum { BLE_ROUTE_IN = 1, BLE_ROUTE_OUT = 2 };
enum { MSRC_BLE = 3 };                          /* (only channel messages are queued: clock_sync.c never sees it) */
static uint8_t ble_midi_route = BLE_ROUTE_IN | BLE_ROUTE_OUT;
static uint32_t ble_in_q[BMQ], ble_in_t[BMQ], ble_out_q[BMQ], ble_out_ms[BMQ];
static volatile uint32_t bmi_w, bmi_r, bmo_w, bmo_r;
static volatile uint8_t ble_out_on;            /* a central has the MIDI notifications on */

BLE_API void ble_app_state(void) { ble_out_on = (uint8_t)ble_midi_ready(); }

BLE_API int ble_app_midi_peek(uint32_t *pkt, uint32_t *t_ms)
{
    if (bmo_r == bmo_w)
        return 0;
    RING_PUBLISH();                             /* the slot read after the index (the audio ISR fills it) */
    *pkt = ble_out_q[bmo_r % BMQ];
    *t_ms = ble_out_ms[bmo_r % BMQ];
    return 1;
}

BLE_API void ble_app_midi_pop(void)
{
    RING_PUBLISH();
    bmo_r++;
}

BLE_API void ble_app_midi_in(uint32_t pkt, uint16_t ts, uint16_t last_ts)
{
    uint32_t cin = pkt & 15u;
    if (!(ble_midi_route & BLE_ROUTE_IN) || cin < 8u || cin > 0xEu || bmi_w - bmi_r >= BMQ)
        return;
    ble_in_q[bmi_w % BMQ] = pkt;
    ble_in_t[bmi_w % BMQ] = SYNC_NOW() - ((uint32_t)(uint16_t)(last_ts - ts) & 0x1FFFu) * 1000u * FM1_TICKS_PER_US;
    RING_PUBLISH();
    bmi_w++;
}

static void ble_midi_out(uint32_t pkt)          /* the audio ISR, from usb.c's midi_out_event */
{
    if (!ble_out_on || !(ble_midi_route & BLE_ROUTE_OUT) || bmo_w - bmo_r >= BMQ)
        return;
    ble_out_q[bmo_w % BMQ] = pkt;
    ble_out_ms[bmo_w % BMQ] = fm1_ms;
    RING_PUBLISH();
    bmo_w++;
}

static void ble_midi_poll(void)                 /* the TIMER5 ISR, 2 kHz: BLE in -> the router */
{
    while (bmi_r != bmi_w) {
        RING_PUBLISH();
        if (!midi_in_enqueue(ble_in_q[bmi_r % BMQ], MSRC_BLE, ble_in_t[bmi_r % BMQ]))
            return;                             /* the router's ring is full: the next tick */
        RING_PUBLISH();
        bmi_r++;
    }
}

static void ble_midi_init(void)                 /* at boot, before the BLE interrupts are on */
{
    uint8_t a[6], rnd = ble_hw_addr(a);
    ble_init(a, rnd);
    ble_enable(1);                              /* as stock: BLE on from every boot */
}
