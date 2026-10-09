/* SPDX-License-Identifier: GPL-3.0-only */
/* The BLE-MIDI peripheral as the firmware sees it (route C: our own stack, docs/BLE-STACK.md).
 *
 *   ble_init(addr, random)     once at start, before the driver's interrupts are on
 *   ble_enable(on)             advertise as BLE_DEVICE_NAME with the BLE-MIDI service / stop and disconnect
 *   ble_midi_ready()           a central is connected and has turned the MIDI notifications on
 *
 * The firmware gives the stack its MIDI through four functions (io/midi/midi_ble.c, or a test's fakes). All
 * are called from the BLE interrupts' context (ble_hw.h); they must only touch single-producer rings. */
#ifndef BLE_H
#define BLE_H
#include <stdint.h>
#include "ble_cfg.h"

BLE_API void ble_init(const uint8_t addr[6], uint8_t addr_random);
BLE_API void ble_enable(int on);
BLE_API int ble_midi_ready(void);
BLE_API int ble_connected(void);

/* ---- the firmware implements these ---- */
/* the oldest MIDI event waiting to go out (a USB-MIDI event packet and the millisecond it happened): 1, or 0
 * when there is none. The stack takes it with ble_app_midi_pop() once it is in a notification. */
BLE_API int ble_app_midi_peek(uint32_t *pkt, uint32_t *t_ms);
BLE_API void ble_app_midi_pop(void);
/* one message received (a USB-MIDI event packet), with its 13-bit timestamp and the last timestamp of the
 * BLE packet it came in (the newest: the difference is how long before the packet's end it happened) */
BLE_API void ble_app_midi_in(uint32_t pkt, uint16_t ts, uint16_t last_ts);
/* a central connected (1) or left (0); its MIDI notifications on or off (ble_midi_ready()) */
BLE_API void ble_app_state(void);
#if BLE_SMP_LEGACY
/* a central bonded: its key (Rand, EDIV, LTK least significant octet first) to keep across power-offs and give back
 * with ble_host_set_key (ble_host.h) after ble_init. BLE interrupts' context: copy it and save it later */
BLE_API void ble_app_bond(const uint8_t rand[8], uint16_t ediv, const uint8_t ltk[16]);
/* the bonded peer's identity (SMP Identity Information + Identity Address Information): its IRK and identity address
 * (least significant octet first; addr_rand 1 random static, 0 public). BLE interrupts' context: copy it */
BLE_API void ble_app_peer_id(const uint8_t irk[16], const uint8_t addr[6], uint8_t addr_rand);
#endif

#if BLE_CENTRAL
/* ---- connecting out as central (BLE_CENTRAL, docs/BLE-DEVICES-DESIGN.md P3 / P4; ble_central.c) */
struct ble_peer {
    uint8_t addr[6], addr_rand;          /* its AdvA as heard now (least significant octet first; an RPA stays one) */
    uint8_t bonded;                      /* ltk / rand / ediv are its bond: the link is encrypted with them */
    uint8_t ltk[16], rand[8];
    uint16_t ediv;
};
/* the keys a pairing as initiator took from the peripheral (Core Vol 3 Part H 3.6) */
#define BLE_KEYS_LTK 1u                  /* ltk / rand / ediv: the bond (Encryption Information, Master Identification) */
#define BLE_KEYS_ID 2u                   /* irk / id / id_rand: its identity (Identity Information + Address) */
struct ble_keys {
    uint8_t has;                         /* BLE_KEYS_* */
    uint8_t ltk[16], rand[8];
    uint16_t ediv;
    uint8_t irk[16], id[6], id_rand;
};
enum { BLE_CS_IDLE, BLE_CS_CONNECTING, BLE_CS_SETUP, BLE_CS_READY };   /* ble_central_state */
enum { BLE_CF_NONE, BLE_CF_LOST, BLE_CF_NO_MIDI, BLE_CF_PAIRING, BLE_CF_AUTH, BLE_CF_GATT };   /* ble_central_fail */
/* initiate to p (advertising or scanning stop: one link) -> 1 started, 0 not now (off, or a link is up). From there:
 * connected as master, the BLE-MIDI characteristic found and subscribed (pairing or encrypting with p's bond when the
 * peripheral asks, or wants it), MIDI both ways: BLE_CS_READY. Called like ble_enable (the BLE interrupts held) */
BLE_API int ble_central_connect(const struct ble_peer *p);
BLE_API void ble_central_cancel(void);   /* stop initiating, or leave our central link (a peripheral link stays) */
BLE_API uint8_t ble_central_state(void);
BLE_API uint8_t ble_central_fail(void);  /* why the last connection out ended before / after READY (BLE_CF_*) */
BLE_API uint8_t ble_central_code(void);  /* ... its code: the LL reason, the ATT error or the SMP reason */
/* 1: addr is a resolvable private address made from irk (ah, Core Vol 3 Part H 2.2.2; least significant first) */
BLE_API int ble_rpa_resolve(const uint8_t irk[16], const uint8_t addr[6]);
/* the firmware implements: a pairing as initiator ended with these keys of the peripheral (BLE interrupts' context:
 * copy them) */
BLE_API void ble_app_central_keys(const struct ble_keys *k);
#endif

#endif
