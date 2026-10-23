/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of the shared radio's bring-up (firmware/src/ble/rf_share.h): BLE MIDI and Bluetooth audio on the one radio,
 * in either order. The radio's start-up and the LE baseband's init are counting stand-ins (the hardware's are
 * hal/fm1_ble.h's); the two sequences are the callers' as they are written:
 *   BLE      ble/ble_hw_wl82.c ble_hw_wl82_start: rfs_rf_up, then rfs_le_ble with the driver's window
 *   BR/EDR   bt/bt_inq.c bti_radio_up (rfs_is_up, else rfs_rf_up) then, with o_ble (the default), bti_le_up: rfs_le_br
 *            with its 64-byte stand-in window
 * Checks: each alone as before (one start-up, one LE init with its own window); BLE then BR/EDR: no second start-up, no
 * LE init over BLE's (its window and interrupt enables kept); BR/EDR then BLE: one start-up, BLE's window replaces the
 * stand-in; repeated runs of either side start nothing again. Whether the two engines then share the air well is not a
 * host question (docs/BT-CLASSIC.md §9.1: untested on an FM-1). */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static struct {
    uint32_t rf, bb, base, size;
} hw;
static void fm1_ble_rf_init(const uint8_t *x106, const uint8_t *x107, const uint8_t *x108, const uint8_t *x187)
{
    (void)x106, (void)x107, (void)x108, (void)x187;
    hw.rf++;
}
static void fm1_ble_bb_init(uint32_t base, uint32_t size)
{
    hw.bb++;
    hw.base = base, hw.size = size;
}
#include "../firmware/src/ble/rf_share.h"
#include "../firmware/src/ble/rf_share.h"              /* (twice, as the unity build may: the guard) */

#define BLE_BASE 0x40000u                               /* (the driver's window: any address, told apart) */
#define BLE_SIZE 1024u
#define BR_BASE 0x50000u
#define BR_SIZE 64u
static const uint8_t T[4] = {1, 2, 3, 4};

static uint8_t br_rf_up, br_le_up;                      /* (bt_inq.c bti_st.rf_up, bti.le_up) */
static void ble_start(void)                             /* ble_hw_wl82_start's radio part */
{
    (void)rfs_rf_up(T, T + 1, T + 2, T + 3);
    rfs_le_ble(BLE_BASE, BLE_SIZE);
}
static void br_prepare(void)                            /* bti_prepare's radio part: bti_radio_up, then bti_le_up */
{
    if (!br_rf_up) {
        if (!rfs_is_up())
            (void)rfs_rf_up(T, T + 1, T + 2, T + 3);
        br_rf_up = 1;
    }
    if (!br_le_up) {
        (void)rfs_le_br(BR_BASE, BR_SIZE);
        br_le_up = 1;
    }
}
static void boot(void)
{
    memset(&hw, 0, sizeof hw);
    rfs = 0;
    br_rf_up = br_le_up = 0;
}

static int fails;
static void check(const char *what, int ok)
{
    printf("%-118s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

int main(void)
{
    boot();
    ble_start();
    check("BLE alone: one radio start-up, one LE init with BLE's window (as before)", hw.rf == 1u && hw.bb == 1u && hw.base == BLE_BASE);
    boot();
    br_prepare();
    br_prepare();
    check("BR/EDR alone: one radio start-up, one LE init with its stand-in window (as before), none again on the next run",
          hw.rf == 1u && hw.bb == 1u && hw.base == BR_BASE && hw.size == BR_SIZE);
    boot();
    ble_start();
    br_prepare();
    check("BLE up, then BR/EDR: the radio not started again (BLE's link not retuned under it)", hw.rf == 1u && br_rf_up);
    check("... and no LE init over BLE's: its window and interrupt enables kept", hw.bb == 1u && hw.base == BLE_BASE && hw.size == BLE_SIZE);
    br_prepare();
    check("... a second BR/EDR run (a page after the scan): still nothing again", hw.rf == 1u && hw.bb == 1u && hw.base == BLE_BASE);
    boot();
    br_prepare();
    ble_start();
    check("BR/EDR up, then BLE: the radio not started again (the audio link not retuned under it)", hw.rf == 1u);
    check("... BLE's LE init runs: its window replaces BR/EDR's stand-in (no link in that one)", hw.bb == 2u && hw.base == BLE_BASE && hw.size == BLE_SIZE);
    br_prepare();
    check("... BR/EDR again after BLE: its LE init stays done (BLE's window kept)", hw.bb == 2u && hw.base == BLE_BASE);
    boot();
    check("rfs_le_br with BLE's window set: refused (0), rfs_rf_up a second time: 0",
          (rfs_le_ble(BLE_BASE, BLE_SIZE), rfs_le_br(BR_BASE, BR_SIZE) == 0) && rfs_rf_up(T, T, T, T) == 1 && rfs_rf_up(T, T, T, T) == 0);
    printf(fails ? "bt_coex test FAILED (%d)\n" : "bt_coex test passed\n", fails);
    return fails != 0;
}
