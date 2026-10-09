/* SPDX-License-Identifier: GPL-3.0-only */
/* One whole FM-1 BLE stack (the firmware's sources, BLE_API static as in its unity build) behind a fake baseband, for
 * tests/ble_f2f_test.c, which links two (or three) of these into one program and carries the packets between them:
 * FM-1 to FM-1 (docs/BLE-DEVICES-DESIGN.md P5, §9.3). Built once per unit with -DF2F_U=<name>: every function here is
 * <name>_<function> (tests/ble_f2f.h). The firmware's settings as built: BLE_CENTRAL brings LL encryption and legacy
 * pairing; BLE_MIDI_NEED_ENC as given (0 by default: no security on the MIDI characteristic). */
#include <stdint.h>
#include <string.h>
#define BLE_API static
#ifndef BLE_CENTRAL
#define BLE_CENTRAL 1
#endif
#include "../firmware/src/ble/ble_prim.c"
#include "../firmware/src/ble/ble_aes.c"
#include "../firmware/src/ble/ble_ll.c"
#include "../firmware/src/ble/ble_smp.c"
#include "../firmware/src/ble/ble_host.c"
#include "../firmware/src/ble/ble_att.c"
#include "../firmware/src/ble/ble_midi.c"
#include "../firmware/src/ble/ble_diag.c"
#include "../firmware/src/ble/ble_store.c"
#include "../firmware/src/ble/ble_scan.c"
#include "../firmware/src/ble/ble_gattc.c"
#include "../firmware/src/ble/ble_central.c"
#include "ble_f2f.h"

/* the fake baseband: what the stack asked for */
static struct {
    int adv_on, conn_on, init_on;
    uint8_t cind[36];
} hw;
static uint32_t now_us = 1000000, rng;

BLE_API void ble_hw_adv_start(const struct ble_hw_adv *a) { (void)a, hw.adv_on = 1; }
BLE_API void ble_hw_adv_stop(void) { hw.adv_on = 0; }
BLE_API void ble_hw_conn_start(const struct ble_hw_conn *c) { (void)c, hw.adv_on = 0, hw.conn_on = 1; }
BLE_API void ble_hw_conn_stop(void) { hw.conn_on = 0; }
BLE_API void ble_hw_scan_start(const struct ble_hw_scan *s) { (void)s; }
BLE_API void ble_hw_scan_stop(void) {}
BLE_API void ble_hw_init_start(const struct ble_hw_init *i)
{
    hw.adv_on = 0;
    hw.init_on = 1;
    memcpy(hw.cind, i->cind, 36);
}
BLE_API void ble_hw_init_stop(void) { hw.init_on = 0; }
BLE_API void ble_hw_conn_update(const struct ble_hw_conn_upd *u) { (void)u; }
BLE_API void ble_hw_chmap_update(const uint8_t chm[5], uint16_t instant) { (void)chm, (void)instant; }
BLE_API void ble_hw_set_lengths(uint8_t max_tx, uint8_t max_rx) { (void)max_tx, (void)max_rx; }
BLE_API void ble_hw_tx_kick(void) {}
BLE_API uint32_t ble_hw_time_us(void) { return now_us; }
BLE_API uint32_t ble_hw_diag_now(void) { return now_us; }
static uint8_t own_addr[6];
BLE_API uint8_t ble_hw_addr(uint8_t addr[6]) { memcpy(addr, own_addr, 6); return 1; }
BLE_API void ble_hw_rand(uint8_t *out, uint8_t n)
{
    while (n--) {
        rng ^= rng << 13, rng ^= rng >> 17, rng ^= rng << 5;
        *out++ = (uint8_t)rng;
    }
}

/* the firmware: MIDI out from a ring, MIDI in counted, keys recorded */
static uint32_t out_q[64], out_r, out_w, in_pkt[64], in_n, keys_n;
BLE_API int ble_app_midi_peek(uint32_t *pkt, uint32_t *t)
{
    if (out_r == out_w)
        return 0;
    *pkt = out_q[out_r % 64u];
    *t = 500;
    return 1;
}
BLE_API void ble_app_midi_pop(void) { out_r++; }
BLE_API void ble_app_midi_in(uint32_t pkt, uint16_t ts, uint16_t last) { (void)ts, (void)last, in_pkt[in_n++ % 64u] = pkt; }
BLE_API void ble_app_state(void) {}
BLE_API void ble_app_bond(const uint8_t rand[8], uint16_t ediv, const uint8_t ltk[16]) { (void)rand, (void)ediv, (void)ltk; }
BLE_API void ble_app_peer_id(const uint8_t irk[16], const uint8_t a[6], uint8_t r) { (void)irk, (void)a, (void)r; }
BLE_API void ble_app_central_keys(const struct ble_keys *k) { (void)k, keys_n++; }

/* ---- the unit's face (tests/ble_f2f.h) */
void F2F(init)(const uint8_t addr[6], uint32_t seed)
{
    memcpy(own_addr, addr, 6);
    rng = seed;
    ble_init(addr, 1);
    ble_enable(1);
}
int F2F(adv_on)(void) { return hw.adv_on; }
int F2F(connect)(const uint8_t peer[6])
{
    struct ble_peer p;
    memset(&p, 0, sizeof p);
    memcpy(p.addr, peer, 6);
    p.addr_rand = 1;
    return ble_central_connect(&p) && hw.init_on;
}
const uint8_t *F2F(cind)(void) { return hw.cind; }
int F2F(take_cind)(const uint8_t *cind) { return ble_ll_hw_connect_ind(cind, 36); }
void F2F(master_start)(void)
{
    hw.init_on = 0;
    ble_ll_hw_master_start();
    hw.conn_on = 1;
}
int F2F(conn_on)(void) { return hw.conn_on; }
uint8_t F2F(tx)(uint8_t *pdu) { return hw.conn_on ? ble_ll_hw_tx(pdu) : 0u; }
void F2F(acked)(void) { ble_ll_hw_tx_acked(); }
void F2F(rx)(const uint8_t *pdu, uint8_t n)
{
    if (hw.conn_on)
        ble_ll_hw_rx(pdu, n);
}
void F2F(event_end)(uint16_t counter)
{
    if (hw.conn_on)
        ble_ll_hw_event_end(counter, 1);
    now_us += 9u * 1250u;
}
int F2F(central_state)(void) { return ble_central_state(); }
int F2F(central_fail)(void) { return ble_central_fail(); }
uint32_t F2F(passkey)(void) { return ble_central_passkey(); }
int F2F(midi_ready)(void) { return ble_midi_ready(); }
int F2F(connected)(void) { return ble_connected(); }
int F2F(encrypted)(void) { return ble_ll_encrypted(); }
uint32_t F2F(pairings)(void) { return ble_dgc.si_pair_req; }
uint32_t F2F(mitm_pairings)(void) { return ble_dgc.si_mitm_req; }
uint32_t F2F(keys)(void) { return keys_n; }
void F2F(midi_out)(uint32_t pkt) { out_q[out_w++ % 64u] = pkt; }
uint32_t F2F(midi_in_n)(void) { return in_n; }
uint32_t F2F(midi_in)(uint32_t i) { return in_pkt[i % 64u]; }
