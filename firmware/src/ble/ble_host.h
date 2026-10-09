/* SPDX-License-Identifier: GPL-3.0-only */
/* Inside the host: L2CAP (ble_host.c: fixed channels, LE signalling, SMP, GAP start-up) and the ATT server with
 * its GATT database (ble_att.c). */
#ifndef BLE_HOST_H
#define BLE_HOST_H
#include <stdint.h>
#include "ble_cfg.h"

enum { L2CAP_CID_ATT = 4, L2CAP_CID_SIG = 5, L2CAP_CID_SMP = 6 };

/* the BLE-MIDI service UUID 03B80E5A-EDE8-4B33-A751-6CE34EC4C700, least significant octet first */
static const uint8_t BLE_UUID_MIDI_SVC[16] = {0x00, 0xC7, 0xC4, 0x4E, 0xE3, 0x6C, 0x51, 0xA7,
                                              0x33, 0x4B, 0xE8, 0xED, 0x5A, 0x0E, 0xB8, 0x03};

/* the frames ble_att.c may queue: a response must always find room, so notifications leave this much */
#define BLE_ATT_RESERVE (BLE_ATT_MTU_MAX + 8u)

/* ble_att.c */
BLE_API void ble_att_rx(const uint8_t *p, uint16_t n);  /* an ATT PDU from the client */
BLE_API void ble_att_reset(void);                       /* a new connection, or none */
BLE_API void ble_att_event(void);                       /* each connection event: indications, MIDI out */

/* ble_host.c: ask the central for BLE_CONN_MIN..MAX (L2CAP Connection Parameter Update Request), once per
 * connection, unless the interval in use is in that range already */
BLE_API void ble_sig_want_fast(void);
#if BLE_LL_ENC
/* the key the link layer may use (LTK least significant octet first, for Rand and EDIV) */
BLE_API void ble_host_set_key(const uint8_t rand[8], uint16_t ediv, const uint8_t ltk[16]);
#endif

/* ble_smp.c: an SMP PDU from the central; a new connection / none */
BLE_API void ble_smp_rx(const uint8_t *p, uint16_t n);
BLE_API void ble_smp_connected(void);
BLE_API void ble_smp_reset(void);
#if BLE_SMP_LEGACY
/* the short-term key while a pairing waits for the encryption (EDIV 0, Rand 0): 1 given, 0 none */
BLE_API int ble_smp_stk(const uint8_t rand[8], uint16_t ediv, uint8_t ltk[16]);
#endif

#endif
