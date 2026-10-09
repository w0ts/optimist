/* SPDX-License-Identifier: GPL-3.0-only */
/* The host below ATT (Core Specification Vol 3):
 *   - GAP: the advertisement (flags, the BLE-MIDI service UUID) and the scan response (the name), Part C 11;
 *   - L2CAP on LE: basic frames over the fixed channels, reassembled from the link layer's fragments; the LE
 *     signalling channel: our Connection Parameter Update Request and its response, Command Reject for every
 *     request we do not take (Part A 4);
 *   - SMP: ble_smp.c ("Pairing Not Supported", or legacy Just Works with BLE_SMP_LEGACY); the key slot the link
 *     layer's encryption asks (BLE_LL_ENC).
 * Every ATT, signalling and SMP PDU in and out goes into blell's protocol ring (ble_diag.h ble_diag_pdu). */
#include "ble.h"
#include "ble_host.h"
#include "ble_ll.h"
#include "ble_util.h"
#include "ble_diag.h"

static struct {
    uint8_t rx[4 + BLE_ATT_MTU_MAX];           /* one L2CAP frame being reassembled */
    uint16_t have, need;                       /* octets in rx / of the frame (header included) */
    uint16_t skip;                             /* octets of a frame too big for rx still to come */
    uint8_t sig_id, fast;                      /* our last signalling identifier; 0 / 1 asked / 2 retried / 3 done */
} bhs;

/* the advertised name and the GAP Device Name: BLE_DEVICE_NAME, then " XXXX" = the last four hex digits of the device
 * address (its two least significant octets, as an address is written: "FM-1 A1B2" for ..:A1:B2; docs/
 * BLE-DEVICES-DESIGN.md §3.4) */
#define BLE_GAP_NAME_MAX 29u
static struct {
    char s[BLE_GAP_NAME_MAX];
    uint8_t n;
} bgap;

static void gap_name(const uint8_t addr[6])
{
    static const char base[] = BLE_DEVICE_NAME, HEX[] = "0123456789ABCDEF";
    uint32_t n = sizeof base - 1u;
    if (n > BLE_GAP_NAME_MAX - (BLE_NAME_HEX ? 5u : 0u))
        n = BLE_GAP_NAME_MAX - (BLE_NAME_HEX ? 5u : 0u);
    ble_cpy((uint8_t *)bgap.s, (const uint8_t *)base, n);
#if BLE_NAME_HEX
    bgap.s[n++] = ' ';
    bgap.s[n++] = HEX[addr[1] >> 4];
    bgap.s[n++] = HEX[addr[1] & 15u];
    bgap.s[n++] = HEX[addr[0] >> 4];
    bgap.s[n++] = HEX[addr[0] & 15u];
#else
    (void)addr, (void)HEX;
#endif
    bgap.n = (uint8_t)n;
}

BLE_API uint8_t ble_gap_name(const uint8_t **p)
{
    *p = (const uint8_t *)bgap.s;
    return bgap.n;
}

BLE_API void ble_init(const uint8_t addr[6], uint8_t addr_random)
{
    uint8_t ad[3 + 18], sr[31], n;
    ad[0] = 2;                                 /* flags: LE General Discoverable, BR/EDR not supported */
    ad[1] = 0x01;
    ad[2] = 0x06;
    ad[3] = 17;                                /* complete list of 128-bit service UUIDs: BLE-MIDI */
    ad[4] = 0x07;
    ble_cpy(ad + 5, BLE_UUID_MIDI_SVC, 16);
    gap_name(addr);
    n = bgap.n;
    sr[0] = (uint8_t)(n + 1u);
    sr[1] = 0x09;                              /* complete local name */
    ble_cpy(sr + 2, (const uint8_t *)bgap.s, n);
    ble_ll_init(addr, addr_random);
    ble_ll_set_adv_data(ad, sizeof ad, sr, (uint8_t)(n + 2u));
    ble_att_reset();
}

BLE_API void ble_enable(int on) { ble_ll_enable(on); }
BLE_API int ble_connected(void) { return ble_ll_connected(); }

/* ------------------------------------------------------------------------------------------- the link --- */

BLE_API void ble_host_connected(void)
{
    bhs.have = bhs.need = bhs.skip = 0;
    bhs.fast = 0;
    ble_att_reset();
    ble_smp_connected();
#if BLE_CENTRAL
    ble_central_connected();                   /* (as master: the GATT client, the bond's encryption) */
#endif
    ble_app_state();
}

BLE_API void ble_host_disconnected(uint8_t reason)
{
    (void)reason;
    bhs.have = bhs.need = bhs.skip = 0;
    ble_att_reset();
    ble_smp_reset();
#if BLE_CENTRAL
    ble_central_disconnected(reason);
#endif
    ble_app_state();
}

/* ------------------------------------------------------------------------------------- LE signalling --- */

static void sig_send(uint8_t code, uint8_t id, const uint8_t *d, uint16_t n)
{
    uint8_t p[4 + 8];
    p[0] = code;
    p[1] = id;
    ble_wr16(p + 2, n);
    ble_cpy(p + 4, d, n);
    ble_ll_send(L2CAP_CID_SIG, p, (uint16_t)(4u + n));
}

static void sig_conn_params(uint16_t lo, uint16_t hi)
{
    uint8_t d[8];
    ble_wr16(d, lo);
    ble_wr16(d + 2, hi);
    ble_wr16(d + 4, BLE_CONN_LATENCY);
    ble_wr16(d + 6, BLE_CONN_TIMEOUT);
    if (!++bhs.sig_id)
        bhs.sig_id = 1;
    sig_send(0x12, bhs.sig_id, d, 8);           /* Connection Parameter Update Request */
}

BLE_API void ble_sig_want_fast(void)
{
    uint16_t iv = ble_ll_interval();
#if BLE_CENTRAL
    if (ble_ll_central())
        return;                                /* (as master the interval is ours: BLE_CENTRAL_INTERVAL) */
#endif
    if (bhs.fast || !iv)
        return;
    bhs.fast = 3;
    if (iv < BLE_CONN_MIN || iv > BLE_CONN_MAX) {
        bhs.fast = 1;
        sig_conn_params(BLE_CONN_MIN, BLE_CONN_MAX);
    }
}

static void sig_rx(const uint8_t *p, uint16_t n)
{
    uint8_t code, id, d[2] = {0, 0};
    if (n < 4u || (uint32_t)ble_rd16(p + 2) + 4u > n || !p[1])
        return;
    code = p[0];
    id = p[1];
    switch (code) {
    case 0x13:                                 /* Connection Parameter Update Response */
        if (id != bhs.sig_id || n < 6u)
            return;
        if (ble_rd16(p + 4) && bhs.fast == 1 && BLE_CONN_RETRY_MIN > 0u) {
            bhs.fast = 2;                       /* rejected: ask once more, slower */
            sig_conn_params(BLE_CONN_RETRY_MIN, BLE_CONN_RETRY_MAX);
        } else
            bhs.fast = 3;
        return;
    case 0x01:                                 /* Command Reject (of ours) */
        if (id == bhs.sig_id)
            bhs.fast = 3;
        return;
    case 0x07: case 0x15: case 0x16: case 0x18: case 0x1A:   /* responses and indications: nothing to do */
        return;
#if BLE_CENTRAL
    case 0x12:                                 /* Connection Parameter Update Request: ours to answer as master */
        if (ble_ll_central()) {
            d[0] = ble_central_sig(p + 4, (uint16_t)(n - 4u));   /* the result: 0 accepted, 1 rejected */
            sig_send(0x13, id, d, 2);
        } else
            sig_send(0x01, id, d, 2);
        return;
#endif
    default:                                   /* anything else: Command Reject, "command not understood" */
        sig_send(0x01, id, d, 2);
        return;
    }
}

/* ------------------------------------------------------------------------------------------ L2CAP RX --- */

static void l2cap_frame(const uint8_t *f, uint16_t n)
{
    uint16_t cid = ble_rd16(f + 2);
    if (cid >= L2CAP_CID_ATT && cid <= L2CAP_CID_SMP)   /* (blell's protocol ring) */
        ble_diag_pdu((uint8_t)(cid - 3u), f + 4, (uint32_t)(n - 4u));
    if (cid == L2CAP_CID_ATT)
        ble_att_rx(f + 4, (uint16_t)(n - 4u));
    else if (cid == L2CAP_CID_SIG)
        sig_rx(f + 4, (uint16_t)(n - 4u));
    else if (cid == L2CAP_CID_SMP)
        ble_smp_rx(f + 4, (uint16_t)(n - 4u));
}

BLE_API void ble_host_rx(const uint8_t *p, uint8_t len, uint8_t start)
{
    uint32_t take;
    if (start) {
        bhs.have = bhs.need = bhs.skip = 0;       /* (an unfinished frame before it is dropped) */
        if (len < 2u)
            return;
        bhs.need = (uint16_t)(ble_rd16(p) + 4u);
        if (bhs.need > sizeof bhs.rx) {          /* too big for us: let it pass */
            bhs.skip = (uint16_t)(bhs.need - ble_min(len, bhs.need));
            bhs.need = 0;
            return;
        }
    } else if (bhs.skip) {
        bhs.skip = (uint16_t)(bhs.skip - ble_min(len, bhs.skip));
        return;
    } else if (!bhs.need)
        return;                                /* a continuation without a start */
    take = ble_min(len, (uint32_t)(bhs.need - bhs.have));
    ble_cpy(bhs.rx + bhs.have, p, take);
    bhs.have = (uint16_t)(bhs.have + take);
    if (bhs.have >= 4u && bhs.have == bhs.need) {
        bhs.need = 0;
        l2cap_frame(bhs.rx, bhs.have);
        bhs.have = 0;
    }
}

BLE_API void ble_host_event(void)
{
#if BLE_CENTRAL
    ble_central_event();                       /* (as master: the GATT client's and SMP's timeouts) */
#endif
    ble_att_event();
}

#if BLE_LL_ENC
/* one key slot: the bond SMP made (ble_smp.c), or the one the firmware kept across a power-off (ble_host_set_key at
 * start-up); in RAM here */
static struct {
    uint8_t valid, rand[8], ltk[16];
    uint16_t ediv;
} bhs_key;

BLE_API void ble_host_set_key(const uint8_t rand[8], uint16_t ediv, const uint8_t ltk[16])
{
    ble_cpy(bhs_key.rand, rand, 8);
    ble_cpy(bhs_key.ltk, ltk, 16);
    bhs_key.ediv = ediv;
    bhs_key.valid = 1;
}

#if !BLE_SMP_LEGACY
BLE_API void ble_host_encrypted(void) {}
#endif

BLE_API int ble_host_ltk(const uint8_t rand[8], uint16_t ediv, uint8_t ltk[16])
{
#if BLE_SMP_LEGACY
    if (ble_smp_stk(rand, ediv, ltk))           /* a pairing's short-term key first */
        return 1;
#endif
    if (!bhs_key.valid || ediv != bhs_key.ediv || !ble_eq(rand, bhs_key.rand, 8))
        return 0;
    ble_cpy(ltk, bhs_key.ltk, 16);
    return 1;
}
#endif
