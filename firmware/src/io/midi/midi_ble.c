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
 *   ble_on          HOME > SYSTEM > BLUETOOTH (ui_menu.c ble_midi_set): ON by default, kept in the settings word (bit 21,
 *                   inverted: settings_word.c). OFF: not advertising, a central terminated, no note left sounding.
 * The baseband driver is ble/ble_hw_wl82.c (hal/fm1_ble.h; FELUCCA_BLE_STUB=1: the stand-in, nothing is sent). */
#include "../../ble/ble_stack.c"
#include "../../ble/ble_vm.c"                   /* the radio's stored calibration: the VM read, the copy */

#define BMQ 64u
enum { BLE_ROUTE_IN = 1, BLE_ROUTE_OUT = 2 };
enum { MSRC_BLE = 3 };                          /* (only channel messages are queued: clock_sync.c never sees it) */
static uint8_t ble_midi_route = BLE_ROUTE_IN | BLE_ROUTE_OUT;
static uint32_t ble_in_q[BMQ], ble_in_t[BMQ], ble_out_q[BMQ], ble_out_ms[BMQ];
static volatile uint32_t bmi_w, bmi_r, bmo_w, bmo_r;
static volatile uint8_t ble_out_on;            /* a central has the MIDI notifications on */
static uint8_t ble_on = 1;                     /* HOME > BLUETOOTH: the radio is on (0: never advertises) */
static uint8_t ble_held[16][16];               /* the notes a central has on (bit per note, per channel), TIMER5's */
static volatile uint8_t ble_release;           /* the link is gone / off: end those notes (ble_midi_poll) */
static uint8_t ble_was_conn;

BLE_API void ble_app_state(void)               /* the BLE interrupts, or boot / the menu with them held */
{
    uint8_t c = (uint8_t)ble_connected();
    ble_out_on = (uint8_t)ble_midi_ready();
    if (!ble_out_on)
        bmo_r = bmo_w;                          /* nothing waits for a central that does not listen */
    if (ble_was_conn && !c)
        ble_release = 1;                        /* its notes are not ended by a note-off that will never come */
    ble_was_conn = c;
}

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
    if (!ble_on || !(ble_midi_route & BLE_ROUTE_IN) || cin < 8u || cin > 0xEu || bmi_w - bmi_r >= BMQ)
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

static void ble_note_seen(uint32_t pkt)         /* the notes a central has on, to end them if it is gone */
{
    uint32_t st = (pkt >> 8) & 0xF0u, ch = (pkt >> 8) & 15u, n = (pkt >> 16) & 0x7Fu;
    if (st == 0x90u && ((pkt >> 24) & 0x7Fu))
        ble_held[ch][n >> 3] |= (uint8_t)(1u << (n & 7u));
    else if (st == 0x80u || st == 0x90u)
        ble_held[ch][n >> 3] &= (uint8_t)~(1u << (n & 7u));
}

static void ble_notes_end(void)                 /* a note-off for each note a central left on (the next tick if full) */
{
    uint32_t ch, n, left = 0;
    for (ch = 0; ch < 16u; ch++)
        for (n = 0; n < 128u; n++) {
            if (!(ble_held[ch][n >> 3] & (1u << (n & 7u))))
                continue;
            if (midi_in_enqueue(8u | (0x80u | ch) << 8 | n << 16, MSRC_BLE, SYNC_NOW()))
                ble_held[ch][n >> 3] &= (uint8_t)~(1u << (n & 7u));
            else
                left = 1;
        }
    if (!left)
        ble_release = 0;
}

static void ble_midi_poll(void)                 /* the TIMER5 ISR, 2 kHz: BLE in -> the router */
{
    while (bmi_r != bmi_w) {
        RING_PUBLISH();
        if (!midi_in_enqueue(ble_in_q[bmi_r % BMQ], MSRC_BLE, ble_in_t[bmi_r % BMQ]))
            return;                             /* the router's ring is full: the next tick */
        ble_note_seen(ble_in_q[bmi_r % BMQ]);
        RING_PUBLISH();
        bmi_r++;
    }
    if (ble_release)
        ble_notes_end();                        /* (after what the central had sent: those are in the router now) */
}

/* The device address: VM id 104 (a provisioned public address) when the driver finds one, else a random static
 * address made once and kept with the settings (project.c persist_t), so a central sees the same device after every
 * power-on. [6] = BLE_ADDR_KEPT marks a kept one. */
#define BLE_ADDR_KEPT 0xA5u
static uint8_t ble_addr_kept[8];
static uint8_t settings_later;                  /* (ui/panel.c: saved with the settings once quiet, project.c) */

static uint8_t ble_midi_addr(uint8_t a[6])
{
    uint32_t i;
    uint8_t rnd = ble_hw_addr(a);
    if (!rnd)
        return 0;
    if (ble_addr_kept[6] == BLE_ADDR_KEPT && (ble_addr_kept[5] >> 6) == 3u) {
        for (i = 0; i < 6u; i++)
            a[i] = ble_addr_kept[i];
    } else {
        for (i = 0; i < 6u; i++)
            ble_addr_kept[i] = a[i];
        ble_addr_kept[6] = BLE_ADDR_KEPT;
        settings_later = 1;                     /* (not now: the BLE / audio interrupts are not on yet) */
    }
    return 1;
}

/* The radio's stored calibration (ble/ble_vm.c; docs/BLE-HW-FACTS.md §14, §15.4): stock V15's VM read in place at
 * every boot (never written), the four RF records kept in a copy with the settings (project.c persist_t.ble_rf),
 * and the precedence VM -> copy -> none. With none the radio is never started: BLUETOOTH shows NO RF CAL, the
 * console's 'bletrim' says why, and nothing is transmitted uncalibrated. */
static uint8_t ble_rf_kept[BLE_RF_COPY_SIZE];   /* the copy (persist_t.ble_rf, project.c) */
static struct ble_vm_info ble_vm_seen;          /* what this boot's VM scan found (console 'bletrim') */
static uint8_t ble_rf_src;                      /* BLE_RF_NONE / _FROM_VM / _FROM_COPY */
#if !defined(FELUCCA_FLASH) || FELUCCA_FLASH
static uint8_t flash_ok;                        /* (felucca.c: the JEDEC id matched, persist_boot) */
static int st_read(uint32_t off, void *dst, uint32_t n);
static int ble_vm_rd(void *ctx, uint32_t off, uint8_t *dst, uint32_t n)   /* SPI reads, no XIP decryption */
{
    (void)ctx;
    return flash_ok ? st_read(off, dst, n) : -1;
}
#else
static int ble_vm_rd(void *ctx, uint32_t off, uint8_t *dst, uint32_t n)   /* (a build without flash: no VM) */
{
    (void)ctx, (void)off, (void)dst, (void)n;
    return -1;
}
#endif
static int ble_radio_ok(void) { return !BLE_HW_WL82 || ble_rf_src != BLE_RF_NONE; }

static void ble_midi_init(void)                 /* at boot, after the audio and USB, before the interrupts are on */
{
    uint8_t a[6], rnd;
    struct ble_rf_trims vm, use;
    int complete, save;
    complete = ble_vm_scan(ble_vm_rd, 0, &ble_vm_seen, &vm, 0, 0);   /* (the settings are read: persist_boot) */
    ble_rf_src = (uint8_t)ble_rf_choose(complete, &vm, ble_vm_seen.area, ble_rf_kept, &use, &save);
    if (save)
        settings_later = 1;                     /* the copy kept with the settings, once quiet (project.c) */
    rnd = ble_midi_addr(a);
    ble_init(a, rnd);
    if (!ble_radio_ok())
        return;                                 /* no stored trims: the radio is never started (§15.4 step 3) */
#if BLE_HW_WL82
    ble_hw_wl82_init(&use);                     /* the radio and the baseband (ble/ble_hw_wl82.c) */
#endif
    ble_enable(ble_on);                         /* as stock, on from every boot, unless HOME > BLUETOOTH says OFF */
}

/* HOME > SYSTEM > BLUETOOTH (ui_menu.c), main loop. ON: advertise again. OFF: stop advertising, or ask a connected
 * central to go (LL_TERMINATE_IND; it is gone once that is acknowledged and nothing advertises after it), and end
 * what it left sounding. With no link the baseband's interrupts are off (ble_hw_adv_stop, fm1_ble_link_stop). */
static void ble_midi_set(uint8_t on)
{
    on = on ? 1u : 0u;
    if (on == ble_on)
        return;
    if (!ble_radio_ok()) {                      /* (no stored trims: only the setting changes, the radio stays off) */
        ble_on = on;
        return;
    }
    fm1_ble_irqs_hold(1);
    ble_on = on;
    ble_enable(on);
    if (!on)
        ble_release = 1;                        /* (and again once the link is closed: ble_app_state) */
    fm1_ble_irqs_hold(0);
}
