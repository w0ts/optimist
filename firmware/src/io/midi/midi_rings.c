/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The MIDI rings between the inputs, the sequencer and the outputs (out of io/usb/usb.c as they were, so the
 * sequencer builds without the USB driver; usb.c includes this file, felucca.c before usb.c):
 *   in         midi_in_q: 4-byte USB-MIDI event packets, each with its source (MSRC_*) and arrival time (SYNC_NOW).
 *              Producers: usb.c (usb_poll / usb_rx_peek), midi_uart.c, midi_ble.c, all in the TIMER5 ISR;
 *              consumer: the audio ISR (seq.c events_block).
 *   out        midi_out_q: the FM-1's own events (midi_out_event, from the audio ISR); consumer: usb.c ep1_tx in the
 *              TIMER5 ISR, which outranks it. The same events go to BLE (midi_ble.c ble_midi_out).
 *   clock out  rt_out_*: CLOCK OUT's packets with the time each is due (clock_out.c, FELUCCA_MIDI_CLKOUT); consumer:
 *              usb.c (usb_rx_peek, ep1_tx).
 * A slot is stored, then published (RING_PUBLISH), then its index moves. The outputs take events only while
 * midi_out_open() (usb.c: a host configured the device; a build without usb.c defines its own) */
#ifndef MIDI_RINGS_C
#define MIDI_RINGS_C

/* MIDI rings: 4-byte USB-MIDI event packets */
#define MQ 64u
static uint32_t midi_in_q[MQ], midi_out_q[MQ];
static volatile uint32_t mi_w, mi_r, mo_w, mo_r;
/* The USB rescue (system/recovery.c) has no audio ISR and no events_block: nothing drains the input ring. With
 * midi_in_unread set, channel and clock events are dropped as they arrive and no EP1 packet is held for room, so
 * a DAW's clock or notes cannot fill the ring and NAK the installer's SysEx for ever (Jangada 0835cc4) */
static uint8_t midi_in_unread;
static int midi_out_open(void);                 /* the outputs take events (usb.c: usb.config) */

/* MIDI in, timestamped (after Melodee, Kerem Kilic: clock and transport queued in order with the notes,
 * with their source and arrival time). Time: TIMER4 ticks (SYNC_NOW), so clock_sync.c can follow 24 PPQN
 * to a fraction of a millisecond. Producers: usb_poll and uart_midi_poll, both in the TIMER5 ISR */
#ifndef SYNC_NOW
#define SYNC_NOW() fm1_ticks()
#endif
#define SYNC_PEEK_HALF (50u * 24u)                      /* half the 10 kHz TIMER5 tick, in TIMER4 ticks */
enum { MSRC_USB = 1, MSRC_TRS = 2, MSRC_BLE = 3 };      /* = G_SYNC USB / TRS; BLE: io/midi/midi_ble.c (SYNC_BLE) */
#ifndef OVL_LOST
#define OVL_LOST(k) ((void)0)            /* (a unit without the overload monitor, system/overload.h: the driver tests) */
#endif
static uint32_t midi_in_t[MQ];
static uint8_t midi_in_src[MQ];
static int midi_in_enqueue(uint32_t pkt, uint32_t src, uint32_t t)
{
    if (midi_in_unread || mi_w - mi_r >= MQ)
        return 0;
    midi_in_q[mi_w % MQ] = pkt;
    midi_in_t[mi_w % MQ] = t;
    midi_in_src[mi_w % MQ] = (uint8_t)src;
    RING_PUBLISH();
    mi_w++;
    return 1;
}
/* malformed (SLOOP 2.3, after Felucca 1.0): a channel message whose status is not its CIN's, or a data byte with
 * bit 7 (the second only for the 3-byte ones), is ignored, not played as some other note; a song position too */
static int midi_rx_bad(uint32_t pkt)
{
    uint32_t cin = pkt & 15u, st = (pkt >> 8) & 0xFFu;
    if (cin >= 8u && cin <= 0xEu && ((st >> 4) != cin || (pkt & 0x800000u) ||
                                     (cin != 0xCu && cin != 0xDu && (pkt & 0x80000000u))))
        return 1;
    return cin == 3u && st == 0xF2u && (pkt & 0x80800000u);
}
/* what the input ring takes: channel messages, clock / start / continue / stop (CIN F), song position (CIN 3, F2) */
static int midi_rx_wanted(uint32_t pkt)
{
    uint32_t cin = pkt & 15u, st = (pkt >> 8) & 0xFFu;
    return (cin >= 8u && cin <= 0xEu) || (cin == 0xFu && (st == 0xF8u || (st >= 0xFAu && st <= 0xFCu))) ||
           (cin == 3u && st == 0xF2u);
}

/* MIDI clock out (io/midi/clock_out.c, FELUCCA_MIDI_CLKOUT): USB-MIDI packets (F8, FA, FB, FC: CIN F; F2: CIN 3),
 * each with the TIMER4 time its audio leaves; sent once that time has come, ahead of anything else on the IN
 * endpoint: by usb_rx_peek within a 10 kHz tick (+-0.05 ms), or by the poll. Producer: the audio ISR (events_block);
 * consumer: the TIMER5 ISR, which outranks it. A slot set to 0 was taken back (rt_out_drop_after): skipped */
#if defined(FELUCCA_MIDI_CLKOUT) && FELUCCA_MIDI_CLKOUT && !defined(FELUCCA_LOADER)
#define USB_RT_OUT 1
#define RTQ 16u
static uint32_t rt_out_pkt[RTQ], rt_out_due[RTQ];
static volatile uint32_t rt_w, rt_r;
static void rt_out_push(uint32_t due, uint32_t pkt)
{
    if (midi_out_open() && rt_w - rt_r < RTQ) {
        rt_out_pkt[rt_w % RTQ] = pkt;
        rt_out_due[rt_w % RTQ] = due;
        RING_PUBLISH();
        rt_w++;
    }
}
/* the F8s queued and not yet sent that are due after t: taken back (a start: no stopped clock tick just before
 * the downbeat's). The consumer may take one while this runs: it is then sent, as it would have been */
static void rt_out_drop_after(uint32_t t, uint32_t pkt)
{
    uint32_t i;
    for (i = rt_r; i != rt_w; i++)
        if (rt_out_pkt[i % RTQ] == pkt && (int32_t)(rt_out_due[i % RTQ] - t) > 0)
            rt_out_pkt[i % RTQ] = 0;
}
static int rt_out_ready(uint32_t now)                  /* the oldest is due (within half a 10 kHz tick) */
{
    while (rt_r != rt_w && !rt_out_pkt[rt_r % RTQ])
        rt_r++;                                         /* (taken back) */
    return rt_r != rt_w && (int32_t)(now - rt_out_due[rt_r % RTQ] + SYNC_PEEK_HALF) >= 0;
}
#else
#define USB_RT_OUT 0
#define rt_out_ready(now) 0
#endif

#if FELUCCA_BLE
static void ble_midi_out(uint32_t pkt);             /* (io/midi/midi_ble.c) */
#endif
static void midi_out_event(uint32_t pkt)            /* from the audio ISR */
{
#if FELUCCA_BLE
    ble_midi_out(pkt);                              /* the same events to a BLE central that listens */
#endif
    if (midi_out_open() && mo_w - mo_r < MQ) {
        midi_out_q[mo_w % MQ] = pkt;
        RING_PUBLISH();
        mo_w++;
    }
}

#endif /* MIDI_RINGS_C */
