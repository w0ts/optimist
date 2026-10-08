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

#endif
