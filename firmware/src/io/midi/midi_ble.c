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
 *   ble_on          HOME > SYSTEM > BLUETOOTH (ui_menu.c ble_midi_set): OFF by default, kept in the settings word (bit 21,
 *                   1 = ON: settings_word.c; a fresh unit, an older word or SLOOP's reads OFF). OFF from boot: the radio
 *                   is never started (no rf_init, no BLE / RF register written), so a radio start-up that hangs cannot
 *                   stop the FM-1 booting; it starts at boot only when ON was saved, else when the menu switches it ON.
 *                   OFF after that: not advertising, a central terminated, no note left sounding.
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
static uint8_t ble_on;                         /* HOME > BLUETOOTH: the radio is on (0, the default: never advertises) */
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

/* blell's BLE-MIDI in, the app's side (console.c prints it after ble_dg's mi_*): each field has one writer, the BLE
 * interrupts (off .. overflow) or the TIMER5 tick (drained .. drained_on); 'blell clear' zeroes them */
static struct {
    uint32_t off, not_chan, pushed, overflow;   /* BLUETOOTH OFF / route without IN; not a channel message; queued; full */
    uint32_t drained, drain_full, drained_on;   /* into midi_in_q; midi_in_q full (retried); of them note ons */
} ble_mdg;

BLE_API void ble_app_midi_in(uint32_t pkt, uint16_t ts, uint16_t last_ts)
{
    uint32_t cin = pkt & 15u;
    if (!ble_on || !(ble_midi_route & BLE_ROUTE_IN)) {
        ble_mdg.off++;
        return;
    }
    if (cin < 8u || cin > 0xEu) {
        ble_mdg.not_chan++;
        return;
    }
    if (bmi_w - bmi_r >= BMQ) {
        ble_mdg.overflow++;
        return;
    }
    ble_mdg.pushed++;
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
        if (!midi_in_enqueue(ble_in_q[bmi_r % BMQ], MSRC_BLE, ble_in_t[bmi_r % BMQ])) {
            ble_mdg.drain_full++;
            return;                             /* the router's ring is full: the next tick */
        }
        ble_mdg.drained++;
        if (((ble_in_q[bmi_r % BMQ] >> 8) & 0xF0u) == 0x90u && ((ble_in_q[bmi_r % BMQ] >> 24) & 0x7Fu))
            ble_mdg.drained_on++;
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

/* The bond (BLE_SMP_LEGACY, ble/ble_smp.c): one central's key, kept with the settings (project.c persist_t.ble_bond,
 * never in the VM area) so a Mac that paired once finds the FM-1 bonded after a power-off. [0] BLE_BOND_KEPT marks it;
 * EDIV [2..3], Rand [4..11], LTK [12..27], least significant octet first. The BLE interrupts write it (ble_app_bond);
 * the main loop asks for a settings save (ble_bond_poll). Builds without SMP keep the field zero. */
#define BLE_BOND_KEPT 0xB5u
#define BLE_BOND_SIZE 28u
static uint8_t ble_bond_kept[BLE_BOND_SIZE];
#if BLE_SMP_LEGACY
static volatile uint8_t ble_bond_new;

BLE_API void ble_app_bond(const uint8_t rand[8], uint16_t ediv, const uint8_t ltk[16])
{
    uint32_t i;
    ble_bond_kept[0] = 0;
    ble_bond_kept[2] = (uint8_t)ediv;
    ble_bond_kept[3] = (uint8_t)(ediv >> 8);
    for (i = 0; i < 8u; i++)
        ble_bond_kept[4 + i] = rand[i];
    for (i = 0; i < 16u; i++)
        ble_bond_kept[12 + i] = ltk[i];
    ble_bond_kept[0] = BLE_BOND_KEPT;
    ble_bond_new = 1;
}

static void ble_bond_poll(void)                 /* main loop: a new bond goes out with the settings, once quiet */
{
    if (!ble_bond_new)
        return;
    ble_bond_new = 0;
    settings_later = 1;
}

static void ble_bond_restore(void)              /* after ble_init: the kept bond is the link layer's key again */
{
    if (ble_bond_kept[0] == BLE_BOND_KEPT)
        ble_host_set_key(ble_bond_kept + 4, (uint16_t)(ble_bond_kept[2] | ble_bond_kept[3] << 8), ble_bond_kept + 12);
}
#else
static void ble_bond_poll(void) {}
static void ble_bond_restore(void) {}
#endif

/* The radio's stored calibration (ble/ble_vm.c; docs/BLE-HW-FACTS.md §14, §15.4): stock V15's VM read in place at
 * every boot (never written by BLE; where it is and what else writes near it: docs/BLE-STACK.md §12.2, §12.8), the four RF records kept in a copy with the settings (project.c persist_t.ble_rf),
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

static uint8_t ble_up;                          /* the radio and the stack started (once per boot: ble_radio_start) */

#if BLE_HW_WL82
#define BLE_STEP(s) fm1_ble_step(FM1_BLE_STEP_##s)   /* the breadcrumb a watchdog reset keeps (hal/fm1_ble_rf.h) */
#else
#define BLE_STEP(s) ((void)0)
#endif

/* the radio, the baseband and the stack, the first time BLUETOOTH is ON, with the BLE interrupts masked -> 1 started,
 * 0 no stored trims (the radio is never started, §15.4 step 3) */
static int ble_radio_start(void)
{
    uint8_t a[6], rnd;
    struct ble_rf_trims use;
    if (ble_up)
        return 1;
    if (!ble_radio_ok())
        return 0;
#if BLE_HW_WL82
    fm1_ble_crumb_irqs = 0;
#endif
    BLE_STEP(STACK_INIT);
    rnd = ble_midi_addr(a);
    ble_init(a, rnd);
    ble_bond_restore();
#if BLE_HW_WL82
    ble_rf_copy_get(ble_rf_kept, &use);         /* (the VM's set: ble_rf_choose made the copy from it) */
    ble_hw_wl82_start(&use);                    /* the radio and the baseband (ble/ble_hw_wl82.c) */
#else
    (void)use;
#endif
    BLE_STEP(STARTED);
    ble_up = 1;
    return 1;
}

static void ble_midi_init(void)                 /* at boot, after the audio and USB, before the interrupts are on */
{
    struct ble_rf_trims vm, use;
    int complete, save;
    complete = ble_vm_scan(ble_vm_rd, 0, &ble_vm_seen, &vm, 0, 0);   /* (flash reads only; the settings are read) */
    ble_rf_src = (uint8_t)ble_rf_choose(complete, &vm, ble_vm_seen.area, ble_rf_kept, &use, &save);
    if (save)
        settings_later = 1;                     /* the copy kept with the settings, once quiet (project.c) */
#if BLE_HW_WL82
    ble_hw_wl82_attach();                       /* the two vectors, masked: no BLE / RF register written */
#endif
    if (!ble_boot_radio(ble_on, bootguard.failed, ble_radio_ok()) || !ble_radio_start())
        return;                                 /* OFF (the default); or the boot guard counts a failed start-up (a
                                                 * warm reset in a boot's first 30 s: the watchdog's, after a radio
                                                 * start-up that hung; system/bootguard.h), so this boot leaves the
                                                 * radio off, ON stays saved and the menu can switch it OFF; or no
                                                 * stored trims: the radio stays off (ble/ble_vm.c ble_boot_radio) */
    BLE_STEP(ENABLE);
    ble_enable(1);                              /* ON saved: advertising from boot */
    BLE_STEP(IRQS_ON);
    fm1_ble_irqs_hold(0);
    BLE_STEP(RUNNING);
}

/* HOME > SYSTEM > BLUETOOTH (ui_menu.c), main loop. ON: advertise again. OFF: stop advertising, or ask a connected
 * central to go (LL_TERMINATE_IND; it is gone once that is acknowledged and nothing advertises after it), and end
 * what it left sounding. With no link the baseband's interrupts are off (ble_hw_adv_stop, fm1_ble_link_stop). */
static void ble_midi_set(uint8_t on)
{
    on = on ? 1u : 0u;
    if (on == ble_on)
        return;
    ble_on = on;
    if (on)
        BLE_STEP(SET_ON);
    if (!ble_up && (!on || !ble_radio_start()))
        return;                                 /* never started and OFF, or no stored trims: only the setting */
    fm1_ble_irqs_hold(1);
    BLE_STEP(ENABLE);
    ble_enable(on);
    if (!on)
        ble_release = 1;                        /* (and again once the link is closed: ble_app_state) */
    BLE_STEP(IRQS_ON);
    fm1_ble_irqs_hold(0);
    if (on)
        BLE_STEP(RUNNING);                      /* (the main loop after it: IRQS_ON -> RUNNING, then the count of */
    else                                        /*  BLE interrupts, fm1_ble_crumb_irqs) */
        BLE_STEP(SET_OFF);
}
