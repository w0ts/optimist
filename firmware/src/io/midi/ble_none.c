/* SPDX-License-Identifier: GPL-3.0-only */
/* A build with Bluetooth Classic audio and no BLE MIDI (BT_AUDIO without BLE, the user 2026-10-20: the Bluetooth page
 * "should adapt and show only what is baked in the firmware"): SYSTEM > Bluetooth (ui/optimist/op_bt.c, op_project.c)
 * is built with either, and its MIDI side reads these instead of midi_ble.c and io/midi/ble_devices.c: the radio
 * never on for MIDI, no device, no list. The page offers no MIDI mode in such a build (op_project.c BT_MODES), so
 * none of this is reached but the read-outs. Included by felucca.c in place of midi_ble.c. */
#include "../../ble/ble_util.h"                         /* (ble_eq, ble_cpy: the page and bt_audio.c compare addresses) */
#define BLE_CENTRAL 0                                   /* (no scanner, no LAST: midi_ble.c defines it from the build) */
#define BLE_SCAN_N 8u                                   /* (ble/ble_scan.h's sizes: the page's buffers) */
#define BLE_NAME_MAX 16u
#define BLE_LINE_NO_NAME 0xFFu
enum { BDL_PLAIN, BDL_GOOD, BDL_WARN, BDL_BAD };
struct ble_dev_key {
    uint8_t kind, rnd, addr[6];
};
static const uint8_t ble_on = 0, ble_up = 0;            /* (never on for MIDI) */
static uint32_t bmi_w, bmo_w;                           /* (no MIDI in or out: ACTIVITY's counters stay) */
static int ble_connected(void) { return 0; }
static int ble_radio_ok(void) { return 1; }
static void ble_midi_set(uint8_t on) { (void)on; }      /* (BR/EDR's: bt_audio.c hands the radio over, nothing to stop) */
static int ble_seeking(void) { return 0; }
static void ble_devices_open(int on) { (void)on; }
static uint32_t ble_dev_status(void) { return 0; }
static uint32_t ble_dev_rows(int *last, uint8_t *near, uint32_t *n_near)
{
    (void)near;
    *last = -1;
    *n_near = 0;
    return 0;
}
static void ble_dev_row(uint32_t r, int last, const uint8_t *near, char *nm, const char **tag, int *chosen, uint32_t *bars)
{
    (void)r, (void)last, (void)near;
    if (nm)
        nm[0] = 0;
    *tag = "";
    if (chosen)
        *chosen = 0;
    *bars = 0;
}
static uint32_t ble_dev_sig(void) { return 0; }
static uint32_t ble_dev_line(char *st, uint32_t room, uint32_t n_near)
{
    (void)room, (void)n_near;
    st[0] = 0;
    return BDL_PLAIN;
}
static uint32_t ble_dev_line_name_at(void) { return BLE_LINE_NO_NAME; }
static void ble_dev_key_of(uint32_t row, struct ble_dev_key *k)
{
    (void)row;
    k->kind = 3u;                                       /* (nothing) */
}
static int ble_dev_key_row(const struct ble_dev_key *k) { (void)k; return -1; }
static void ble_dev_pick(uint32_t row) { (void)row; }
static void ble_dev_forget(void) {}
