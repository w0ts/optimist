/* SPDX-License-Identifier: GPL-3.0-only */
/* FM-1 to FM-1 (docs/BLE-DEVICES-DESIGN.md P5, §9.3): two whole copies of our BLE stack (tests/ble_f2f_unit.c, one
 * object each) connected through a fake air that carries every PDU (all acknowledged): unit a is the FM-1 that picks
 * the other in DEVICES (central), unit b another FM-1 as it ships (BLE_MIDI_NEED_ENC=0), unit c one built with
 * BLE_MIDI_NEED_ENC=1 (its MIDI characteristic behind encryption: Insufficient Authentication until then).
 *   b   connect, discover, subscribe, MIDI both ways: no SMP at all, no encryption, no passkey (no dialog, no PIN);
 *   c   the same with a Just Works pairing on its Insufficient Authentication: silent (no passkey, no MITM asked),
 *       encrypted, ready, MIDI both ways. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "ble_f2f.h"

F2F_DECLARE(a)
F2F_DECLARE(b)
F2F_DECLARE(c)

static int fails;
static void check(const char *what, int ok)
{
    printf("%-100s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

/* the air between the central unit a and a peripheral unit (b or c) */
struct unit {
    uint8_t (*tx)(uint8_t *);
    void (*acked)(void);
    void (*rx)(const uint8_t *, uint8_t);
    void (*event_end)(uint16_t);
    int (*conn_on)(void);
};
static const struct unit A = {a_tx, a_acked, a_rx, a_event_end, a_conn_on};
static const struct unit B = {b_tx, b_acked, b_rx, b_event_end, b_conn_on};
static const struct unit C = {c_tx, c_acked, c_rx, c_event_end, c_conn_on};
static uint16_t evt;
static uint32_t smp_pdus, enc_reqs, passkeys;

/* what crossed the air in the clear: SMP (L2CAP CID 6, a start fragment) and LL_ENC_REQ */
static void sniff(const uint8_t *pdu, uint8_t n, int encrypted)
{
    if (encrypted || n < 3u)
        return;
    if ((pdu[0] & 3u) == 2u && n >= 6u && pdu[4] == 6u && pdu[5] == 0u)
        smp_pdus++;
    if ((pdu[0] & 3u) == 3u && pdu[2] == 0x03u)
        enc_reqs++;
}

static void event(const struct unit *p, int (*enc)(void))
{
    uint8_t pdu[2 + 255], n;
    int k;
    for (k = 0; k < 32 && A.conn_on() && p->conn_on() && (n = A.tx(pdu)) != 0; k++) {
        sniff(pdu, n, enc());
        p->rx(pdu, n);
        A.acked();
    }
    for (k = 0; k < 32 && A.conn_on() && p->conn_on() && (n = p->tx(pdu)) != 0; k++) {
        sniff(pdu, n, enc());
        A.rx(pdu, n);
        p->acked();
    }
    A.event_end(evt);
    p->event_end(evt);
    evt++;
    passkeys += a_passkey() != 0xFFFFFFFFu;
}

static void events(const struct unit *p, int (*enc)(void), int n)
{
    while (n-- > 0)
        event(p, enc);
}

static const uint8_t ADDR_A[6] = {0xA1, 0xB2, 0x33, 0x44, 0x55, 0xC6};   /* random static, as FM-1s use */
static const uint8_t ADDR_B[6] = {0x0B, 0x0B, 0x33, 0x44, 0x55, 0xC7};
static const uint8_t ADDR_C[6] = {0x0C, 0x0C, 0x33, 0x44, 0x55, 0xC8};

static int link_up(const uint8_t *addr, int (*take)(const uint8_t *))
{
    if (!a_connect(addr))
        return 0;
    if (!take(a_cind()))
        return 0;
    a_master_start();
    evt = 0;
    return a_connected();
}

static void test_plain(void)
{
    uint32_t i;
    smp_pdus = enc_reqs = passkeys = 0;
    check("FM-1 b (as shipped) advertises; FM-1 a picks it: its CONNECT_IND taken by b, a the master",
          b_adv_on() && link_up(ADDR_B, b_take_cind) && b_connected());
    events(&B, a_encrypted, 60);
    check("a <- b: MTU, the BLE-MIDI service, the characteristic, the CCCD: ready on both sides",
          a_central_state() == 3 && a_midi_ready() && b_midi_ready());
    check("a <- b: no SMP on the air, no LL_ENC_REQ, no passkey: no pairing, no dialog, no PIN",
          smp_pdus == 0 && enc_reqs == 0 && passkeys == 0 && a_pairings() == 0 && !a_encrypted() && a_keys() == 0);
    a_midi_out(0x09u | 0x90u << 8 | 0x40u << 16 | 0x50u << 24);
    b_midi_out(0x08u | 0x80u << 8 | 0x3Cu << 16 | 0x00u << 24);
    events(&B, a_encrypted, 6);
    i = b_midi_in_n();
    check("MIDI a -> b (a Write Command): note on 40 50", i == 1 && b_midi_in(0) == (0x09u | 0x90u << 8 | 0x40u << 16 |
                                                                                       0x50u << 24));
    check("MIDI b -> a (a notification): note off 3C", a_midi_in_n() == 1 &&
          (a_midi_in(0) & 0xFFFFFFu) == (0x08u | 0x80u << 8 | 0x3Cu << 16));
}

static void test_need_enc(void)
{
    smp_pdus = enc_reqs = passkeys = 0;
    check("FM-1 c (BLE_MIDI_NEED_ENC=1) picked by a: connected", c_adv_on() && link_up(ADDR_C, c_take_cind));
    events(&C, a_encrypted, 120);
    check("a <- c: its Insufficient Authentication -> Just Works (no MITM asked), encrypted, ready",
          a_pairings() == 1 && a_mitm_pairings() == 0 && a_encrypted() && a_central_state() == 3 && a_midi_ready());
    check("a <- c: no passkey ever shown on a (silent: no dialog, no PIN), the bond's keys to a's firmware",
          passkeys == 0 && smp_pdus >= 2 && a_keys() == 1);
    a_midi_out(0x09u | 0x90u << 8 | 0x41u << 16 | 0x51u << 24);
    events(&C, a_encrypted, 6);
    check("MIDI a -> c over the encrypted link", c_midi_in_n() == 1 && (c_midi_in(0) >> 16 & 0xFFu) == 0x41u);
}

int main(void)
{
    a_init(ADDR_A, 0x2468ACE1u);
    b_init(ADDR_B, 0x13579BDFu);
    c_init(ADDR_C, 0x0F1E2D3Cu);
    test_plain();
    a_init(ADDR_A, 0x2468ACE1u);                  /* (a fresh a: the next link) */
    test_need_enc();
    printf("%s\n", fails ? "BLE FM-1 to FM-1: FAILED" : "BLE FM-1 to FM-1: all passed");
    return fails != 0;
}
