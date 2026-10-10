/* SPDX-License-Identifier: GPL-3.0-only */
/* The radio's stand-ins for the host UI tests (tests/ui_pages_test.c, tests/ui_optimist_test.c): the state midi_ble.c
 * keeps, and firmware/src/io/midi/ble_devices.c (the DEVICES list both UIs show) with its plain parts (the device store,
 * the scanner's table) over a stand-in link layer: ble_ll_scan records what is wanted, the reports come from the test
 * (ble_fake_rep). Included where the radio's calls are declared: after the helpers, before the UI. */
#ifndef TEST_BLE_UI_STUBS_H
#define TEST_BLE_UI_STUBS_H
#if FELUCCA_BLE
static uint8_t ble_on, ble_link;               /* (the radio, firmware/src/io/midi/midi_ble.c: HOME > BLUETOOTH) */
static uint32_t ble_sets;
static uint8_t ble_up = 1;                       /* (the radio started this boot: midi_ble.c) */
static int ble_connected(void) { return ble_link; }
static int ble_radio_ok(void) { return 1; }       /* (the stored RF trims found: midi_ble.c) */
static void ble_midi_out(uint32_t pkt) { (void)pkt; }
static void ble_midi_set(uint8_t on) { on = on ? 1u : 0u; if (on != ble_on) { ble_on = on; ble_sets++; } }
/* the DEVICES list (firmware/src/io/midi/ble_devices.c) with its plain parts (the device store, the scanner's table)
 * and a stand-in link layer: ble_ll_scan records what is wanted, the reports come from the test (ble_fake_rep) */
#define BLE_API static
#ifndef BLE_CENTRAL
#define BLE_CENTRAL 1
#endif
#include "../firmware/src/ble/ble_store.c"
#if BLE_CENTRAL
#include "../firmware/src/ble/ble_scan.c"
#endif
static struct { uint8_t want, scanning; uint8_t q[8][39], qlen[8]; uint16_t qrssi[8]; uint32_t w, r, holds; } blefk;
static void fm1_ble_irqs_hold(int hold) { blefk.holds += hold != 0; }
static void ble_ll_scan(int on) { blefk.want = (uint8_t)(on != 0); blefk.scanning = (uint8_t)(on && ble_on && !ble_link); }
static int ble_ll_scanning(void) { return blefk.scanning; }
static uint8_t ble_ll_scan_take(uint8_t *pdu, uint16_t *rssi)
{
    uint32_t i;
    uint8_t n;
    if (blefk.r == blefk.w)
        return 0;
    i = blefk.r++ % 8u;
    n = blefk.qlen[i];
    memcpy(pdu, blefk.q[i], n);
    *rssi = blefk.qrssi[i];
    return n;
}
#if BLE_CENTRAL
/* a stand-in for the stack's central role (ble/ble_central.c): the test moves its state */
#include "../firmware/src/ble/ble.h"
#include "../firmware/src/ble/ble_diag.h"
static struct {
    int connects, cancels;
    struct ble_peer p;
    uint8_t st, fail, initiating, central, pairing, code, prompted, mitm;
    uint32_t passkey;                            /* (BLE_NO_PASSKEY: none shown) */
} cenfk = {.passkey = BLE_NO_PASSKEY};
static int ble_central_connect(const struct ble_peer *p)
{
    cenfk.connects++;
    cenfk.p = *p;
    cenfk.st = BLE_CS_CONNECTING;
    cenfk.fail = BLE_CF_NONE;
    cenfk.initiating = 1;
    return 1;
}
static void ble_central_cancel(void)
{
    cenfk.cancels++;
    cenfk.initiating = cenfk.central = 0;
    cenfk.st = BLE_CS_IDLE;
}
static uint8_t ble_central_state(void) { return cenfk.st; }
static uint8_t ble_central_fail(void) { return cenfk.fail; }
static uint8_t ble_central_code(void) { return cenfk.code; }
static int ble_central_prompted(void) { return cenfk.prompted; }
static int ble_central_mitm(void) { return cenfk.mitm; }
static int ble_central_pairing(void) { return cenfk.pairing; }
static uint32_t ble_central_passkey(void) { return cenfk.passkey; }
static int ble_ll_central(void) { return cenfk.central; }
static int ble_ll_initiating(void) { return cenfk.initiating; }
static int ble_rpa_resolve(const uint8_t irk[16], const uint8_t a[6]) { return irk[0] == 0x5A && a[0] == 0x77; }
#endif
#include "../firmware/src/io/midi/ble_devices.c"

/* a report as the link layer hands it over (ble_ll_scan_take): header, AdvA, then AD structures */
static void ble_fake_rep(uint8_t type, uint8_t a0, int midi, const char *name, uint16_t rssi)
{
    static const uint8_t U[16] = {0x00, 0xC7, 0xC4, 0x4E, 0xE3, 0x6C, 0x51, 0xA7,
                                  0x33, 0x4B, 0xE8, 0xED, 0x5A, 0x0E, 0xB8, 0x03};
    uint32_t i = blefk.w % 8u, n = 8;
    uint8_t *p = blefk.q[i];
    p[0] = (uint8_t)(type | 0x40u);                  /* TxAdd: random */
    p[2] = a0, p[3] = 0x11, p[4] = 0x22, p[5] = 0x33, p[6] = 0x44, p[7] = 0xC5;
    if (type == 0) {
        p[n++] = 2, p[n++] = 0x01, p[n++] = 0x06;
    }
    if (midi) {
        p[n++] = 17, p[n++] = 0x07;
        memcpy(p + n, U, 16), n += 16;
    }
    if (name) {
        uint32_t l = (uint32_t)strlen(name);
        p[n++] = (uint8_t)(l + 1u), p[n++] = 0x09;
        memcpy(p + n, name, l), n += l;
    }
    p[1] = (uint8_t)(n - 2u);
    blefk.qlen[i] = (uint8_t)n;
    blefk.qrssi[i] = rssi;
    blefk.w++;
}

#if BLE_CENTRAL
/* the DEVICES status area's line from connecting out (ble_connect.c ble_connect_status), "" when it has nothing */
static const char *dev_status(void)
{
    static char t[40];
    t[0] = 0;
    ble_connect_status(t, sizeof t);
    return t;
}

/* the link the stand-in made goes: why (ble_central_fail) */
static void cen_gone(uint8_t why)
{
    cenfk.st = BLE_CS_IDLE, cenfk.fail = why, cenfk.central = cenfk.initiating = cenfk.pairing = 0, ble_link = 0;
    cenfk.passkey = BLE_NO_PASSKEY;
}
#endif
#endif
#endif
