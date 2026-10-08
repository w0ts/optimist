/* SPDX-License-Identifier: GPL-3.0-only */
/* The BLE stack as part of the firmware's unity build (felucca.c, FELUCCA_BLE=1): every stack function is static
 * here (BLE_API), so what this build never calls is left out (the app is linked without --gc-sections).
 * A baseband driver built as a unit of its own would need BLE_API empty instead. */
#define BLE_API static
#include "ble_prim.c"
#if BLE_LL_ENC
#include "ble_aes.c"
#endif
#include "ble_midi.c"
#include "ble_ll.c"
#include "ble_host.c"
#include "ble_att.c"
#ifndef FELUCCA_BLE_HW
#include "ble_hw_stub.c"            /* no baseband driver yet: a stand-in that never advertises */
#endif
