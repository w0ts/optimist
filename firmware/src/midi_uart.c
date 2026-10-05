/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Modifications Copyright (C) 2026 Kerem Kilic (Ellic Studio): Melodee (github.com/keremimo/melodee,
 * e459da5): clock and transport queued with the notes, the ring read by content */
/* MIDI IN on the TRS jack: PH8 -> input channel 1 -> UART1 RX, 31250 baud,
 * RX DMA into a 128-byte ring, polled from the TIMER5 ISR (no UART IRQ).
 * Built with FELUCCA_UART=1 (the default). Channel messages go into midi_in_q next to
 * USB, as USB-MIDI packets (cable 0, CIN = status >> 4), so seq.c routes them by
 * channel as it does USB; clock, start / continue / stop and song position are queued
 * in order with the notes, timestamped (clock_sync.c follows them). Other realtime,
 * the rest of system common and SysEx are dropped.
 *
 * The ring is read by content, not by the DMA count (Melodee): every slot the parser has
 * not yet been given holds UM_EMPTY, and the reader takes bytes until it meets
 * one. The count (HRXCNT, latched every poll rather than on RPND / OTPND as the
 * SDK does) once missed a byte on an FM-1 while the byte itself was in the
 * ring: the reader stayed one byte behind until reboot, so each message ended
 * only with the next byte. Following the content cannot drift.
 *
 * Time: the poll sees the bytes that landed since the last one (up to 0.5 ms, ~1.5 bytes); byte k of
 * the n found is taken to have landed (n - 1 - k) byte times before the newest, and that one half a
 * poll period ago (the mean): within +-0.25 ms of its real arrival. */
#include "../hal/fm1_uart.h"   /* registers; relative, so the host tests find it too */

#define UM_RING 128u
#define UM_EMPTY 0xFDu         /* undefined MIDI realtime byte: never sent, marks unwritten slots */
#define UM_BYTE_T (320u * 24u) /* a byte at 31250 baud (10 bits), TIMER4 ticks */
static volatile uint8_t um_ring[UM_RING] __attribute__((aligned(16)));
static struct {
    uint32_t rd, drops, msgs;
    volatile uint32_t bytes;
    uint8_t st, need, got, d0, sysex;
} um;

static void uart_midi_init(void)                   /* before timer5_start(): PORTH RMW */
{
    uint32_t i;
    for (i = 0; i < UM_RING; i++)
        um_ring[i] = UM_EMPTY;
    fm1_uart1_midi_init(um_ring, UM_RING);
}

static uint32_t um_len(uint32_t s)                 /* data bytes of a channel status / F2 */
{
    return (s & 0xE0u) == 0xC0u ? 1u : 2u;         /* Cx Dx: 1, else 2 (F2: 2) */
}

static void um_put(uint32_t pkt, uint32_t t)
{
    if (midi_in_enqueue(pkt, MSRC_TRS, t))
        um.msgs++;
    else
        um.drops++;
}

static void um_byte(uint32_t b, uint32_t t)
{
    if (b >= 0xF8u) {
        if (b == 0xF8u || (b >= 0xFAu && b <= 0xFCu))
            um_put(0x0Fu | b << 8, t);
        return;                                    /* realtime leaves running status intact */
    }
    if (b & 0x80u) {
        um.sysex = b == 0xF0u;
        um.st = b < 0xF0u || b == 0xF2u ? (uint8_t)b : 0;   /* other system common / SysEx cancel it */
        um.need = (uint8_t)um_len(b);
        um.got = 0;
        return;
    }
    if (um.sysex || !um.st)
        return;
    if (um.need == 2u && !um.got) {
        um.d0 = (uint8_t)b;
        um.got = 1;
        return;
    }
    {
        uint32_t d1 = um.need == 2u ? um.d0 : b, d2 = um.need == 2u ? b : 0u;
        uint32_t cin = um.st == 0xF2u ? 3u : um.st >> 4;
        um.got = 0;
        um_put(cin | (uint32_t)um.st << 8 | d1 << 16 | d2 << 24, t);
        if (um.st == 0xF2u)
            um.st = 0;                             /* (no running status for system common) */
    }
}

/* the bytes the DMA has written since the last call; a received UM_EMPTY (line
 * noise) is passed over once the byte after it has landed */
static void um_drain(uint32_t now)
{
    uint32_t n, avail = 0, rd = um.rd;
    while (avail < UM_RING && (um_ring[rd] != UM_EMPTY || um_ring[(rd + 1u) & (UM_RING - 1u)] != UM_EMPTY)) {
        avail++;
        rd = (rd + 1u) & (UM_RING - 1u);
    }
    for (n = 0; n < avail; n++) {
        uint32_t b = um_ring[um.rd];
        um_ring[um.rd] = UM_EMPTY;
        um.rd = (um.rd + 1u) & (UM_RING - 1u);
        um.bytes++;
        um_byte(b, now - SYNC_POLL_HALF - (avail - 1u - n) * UM_BYTE_T);   /* (UM_EMPTY: ignored) */
    }
}

static void uart_midi_poll(void)                   /* TIMER5 ISR, same context as usb_poll */
{
    fm1_uart1_rx_take();                           /* acknowledges the pendings; the count is not used */
    um_drain(SYNC_NOW());
}
