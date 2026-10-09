/* SPDX-License-Identifier: GPL-3.0-only */
/* BLE stack configuration (route C, docs/BLE-STACK.md). Every value can be set with -D; the defaults are the
 * FM-1 build's. Sizes are in bytes; times in the Bluetooth units named.
 *
 * Written from the Bluetooth Core Specification (v5.x, LE 1M only) and the MIDI Association's
 * "Specification for MIDI over Bluetooth Low Energy" 1.0. No vendor code. */
#ifndef BLE_CFG_H
#define BLE_CFG_H

#ifndef BLE_API
#define BLE_API                          /* the stack's functions: extern; static in the firmware's unity build */
#endif

/* ---- GAP */
#ifndef BLE_DEVICE_NAME
#define BLE_DEVICE_NAME "FM-1_BLE"       /* stock V15's name (BLE-MIDI-FEASIBILITY.md 9.2); at most 29 characters */
#endif
#ifndef BLE_APPEARANCE
#define BLE_APPEARANCE 0x0000u           /* GAP Appearance: 0 = unknown (no category is claimed) */
#endif
#ifndef BLE_ADV_INTERVAL
#define BLE_ADV_INTERVAL 160u            /* x 0.625 ms = 100 ms (stock: 0xA0) */
#endif
#ifndef BLE_ADV_CHANNELS
#define BLE_ADV_CHANNELS 7u              /* bit 0: channel 37, bit 1: 38, bit 2: 39 */
#endif

/* ---- connection parameters the peripheral asks for once the MIDI notifications are on (stock: {6, 9, 0, 100},
 * then {12, 12, 0, 100} once). Interval x 1.25 ms, timeout x 10 ms. Also the GAP Peripheral Preferred
 * Connection Parameters characteristic. */
#ifndef BLE_CONN_MIN
#define BLE_CONN_MIN 6u
#endif
#ifndef BLE_CONN_MAX
#define BLE_CONN_MAX 9u
#endif
#ifndef BLE_CONN_LATENCY
#define BLE_CONN_LATENCY 0u
#endif
#ifndef BLE_CONN_TIMEOUT
#define BLE_CONN_TIMEOUT 100u
#endif
#ifndef BLE_CONN_RETRY_MIN
#define BLE_CONN_RETRY_MIN 12u           /* the second request after a reject (0 = no second request) */
#endif
#ifndef BLE_CONN_RETRY_MAX
#define BLE_CONN_RETRY_MAX 12u
#endif

/* ---- link layer */
#ifndef BLE_LL_MAX_OCTETS
#define BLE_LL_MAX_OCTETS 251u           /* largest LL payload sent or received (27..251; 27 = no data length ext.) */
#endif
#ifndef BLE_LL_TX_RING
#define BLE_LL_TX_RING 1024u             /* queued L2CAP frames (bytes, a power of two) */
#endif
#ifndef BLE_LL_CTRL_Q
#define BLE_LL_CTRL_Q 4u                 /* queued LL control PDUs */
#endif
#ifndef BLE_LL_VERSION
#define BLE_LL_VERSION 9u                /* VersNr: 9 = Core 5.0 */
#endif
#ifndef BLE_LL_COMPANY
#define BLE_LL_COMPANY 0xFFFFu           /* CompId: 0xFFFF, the value for no assigned company identifier */
#endif
#ifndef BLE_LL_SUBVERSION
#define BLE_LL_SUBVERSION 0x0001u
#endif
#ifndef BLE_LL_PERIPH_FEAT
#define BLE_LL_PERIPH_FEAT 1             /* start the feature exchange ourselves when the central has not */
#endif
#ifndef BLE_LL_ENC
#define BLE_LL_ENC 0                     /* LL encryption (needs keys: a pairing method in ble_smp.c); 0 = refused */
#endif

/* ---- security (ble_smp.c) */
#ifndef BLE_SMP_LEGACY
#define BLE_SMP_LEGACY 0                 /* LE legacy pairing, Just Works, with bonding (needs BLE_LL_ENC); 0: a Pairing
                                          * Request gets Pairing Failed, Pairing Not Supported */
#endif
#ifndef BLE_SMP_SEC_REQ
#define BLE_SMP_SEC_REQ 0                /* send an SMP Security Request (bonding) when a connection starts, as stock V15
                                          * is reported to; Apple's Accessory Design Guidelines (58.10) advise against */
#endif
#ifndef BLE_MIDI_NEED_ENC
#define BLE_MIDI_NEED_ENC 0              /* the MIDI I/O value and its CCCD need an encrypted link: Insufficient
                                          * Authentication (0x05) until then, the way Apple's guidelines (58.10) ask a
                                          * peripheral to start pairing */
#endif
#if BLE_SMP_LEGACY && !BLE_LL_ENC
#error "BLE_SMP_LEGACY needs BLE_LL_ENC=1"
#endif
#if (BLE_SMP_SEC_REQ || BLE_MIDI_NEED_ENC) && !BLE_SMP_LEGACY
#error "BLE_SMP_SEC_REQ / BLE_MIDI_NEED_ENC need BLE_SMP_LEGACY=1"
#endif

/* ---- host */
#ifndef BLE_ATT_MTU_MAX
#define BLE_ATT_MTU_MAX 247u             /* our ATT receive MTU: 247 fills one 251-byte LL PDU (23..517) */
#endif
#ifndef BLE_SC_ON_SUBSCRIBE
#define BLE_SC_ON_SUBSCRIBE 0            /* 1: indicate Service Changed (0x0001..0xFFFF) whenever a client subscribes to
                                          * it. Off: on the FM-1 (blell10-12, 2026-10-09) the Mac confirmed that
                                          * indication and then never discovered the MIDI characteristic; the database
                                          * never changes while we run, so the indication is never due (§5) */
#endif
#ifndef BLE_MIDI_RUNNING_STATUS
#define BLE_MIDI_RUNNING_STATUS 1        /* the encoder drops a repeated status byte inside a packet */
#endif
#ifndef BLE_MIDI_PER_EVENT
#define BLE_MIDI_PER_EVENT 2u            /* notifications queued per connection event at most */
#endif

#if BLE_LL_MAX_OCTETS < 27u || BLE_LL_MAX_OCTETS > 251u
#error "BLE_LL_MAX_OCTETS: 27..251"
#endif
#if BLE_ATT_MTU_MAX < 23u || BLE_ATT_MTU_MAX > 517u
#error "BLE_ATT_MTU_MAX: 23..517"
#endif
#if (BLE_LL_TX_RING & (BLE_LL_TX_RING - 1u)) || BLE_LL_TX_RING < 2u * (BLE_ATT_MTU_MAX + 4u + 2u)
#error "BLE_LL_TX_RING: a power of two, room for two full ATT frames"
#endif

#endif
