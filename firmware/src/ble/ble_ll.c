/* SPDX-License-Identifier: GPL-3.0-only */
/* The link layer, peripheral only, one connection, LE 1M (Core Specification Vol 6 Part B). The baseband engine
 * (ble_hw.h) does the per-event timing, hopping, acknowledgement and instants; this file does the rest:
 *   - advertising PDUs (ADV_IND, SCAN_RSP) and CONNECT_IND checks (4.4, 4.5.1, 2.3.3.1);
 *   - the TX queues (control PDUs first, then L2CAP frames cut to the data length in use) and RX hand-over;
 *   - the supervision timeout and the six-interval establishment rule (4.5.2), the 40 s procedure timeout (5.2);
 *   - the control procedures (5.1): connection update and channel map with instants, termination, feature
 *     exchange (also started by us), version exchange, ping, data length, PHY update (1M only), connection
 *     parameters request (accepted when valid), encryption start (BLE_LL_ENC, else refused); LL_UNKNOWN_RSP for
 *     the rest. */
#include "ble_ll.h"
#include "ble_hw.h"
#include "ble_util.h"
#include "ble_prim.h"
#if BLE_LL_ENC
#include "ble_aes.h"
#endif

#define LL_RING_MASK (BLE_LL_TX_RING - 1u)
#define LL_PROC_US 40000000u                       /* the LL response timeout: 40 s */
enum { LL_OFF, LL_ADV, LL_CONN };
/* procedures waiting on the central: ours (lproc) and the central's (rproc) */
enum { P_NONE, P_FEAT, P_LEN, P_TERM, P_UPD, P_PHY, P_ENC };

#define LL_OUR_FEAT ((BLE_LL_ENC ? BLE_FEAT_ENC | BLE_FEAT_PING : 0u) | BLE_FEAT_CONN_PARAM | BLE_FEAT_EXT_REJECT | \
                     BLE_FEAT_PERIPH_FEAT | (BLE_LL_MAX_OCTETS > 27u ? BLE_FEAT_DLE : 0u))

static struct {
    uint8_t state, enabled, addr_rand;
    uint8_t addr[6];
    uint8_t adv[2 + 37], adv_len, sr[2 + 37], sr_len;
    /* the connection */
    uint16_t interval, timeout, last_evt;
    uint8_t win_size, established, chm[5];
    uint16_t win_offset;
    uint32_t t_start, t_rx;
    uint8_t max_tx, max_rx;                        /* the data length in use (octets) */
    uint8_t peer_feat, feat_known, ver_sent, len_done;
    uint8_t upd_pending, chm_pending;
    struct ble_hw_conn_upd upd;
    uint8_t new_chm[5];
    uint16_t chm_instant;
    uint8_t lproc, rproc;
    uint32_t lproc_t, rproc_t;
    uint8_t term_rx, term_reason;
    uint8_t handed, acked, term_seq, term_out;     /* PDUs given to the engine / acknowledged; LL_TERMINATE_IND's
                                                    * number, and that it went out */
    /* TX: control PDUs, then L2CAP frames in a byte ring */
    uint8_t ctrl[BLE_LL_CTRL_Q][24], ctrl_len[BLE_LL_CTRL_Q], ctrl_rd, ctrl_n;
    uint8_t ring[BLE_LL_TX_RING];
    uint32_t wr, rd, off;                          /* free-running ring indexes; off: octets of the head frame sent */
#if BLE_LL_ENC
    uint8_t enc_rx, enc_tx, tx_paused;
    struct ble_ccm ccm_rx, ccm_tx;
    uint8_t rxbuf[BLE_LL_MAX_OCTETS + 4u];
#endif
} bll;

/* ------------------------------------------------------------------------------------------ advertising --- */

BLE_API void ble_ll_init(const uint8_t addr[6], uint8_t addr_random)
{
    ble_zero((uint8_t *)&bll, sizeof bll);
    ble_cpy(bll.addr, addr, 6);
    bll.addr_rand = addr_random ? 1u : 0u;
    ble_ll_set_adv_data(0, 0, 0, 0);
}

static uint8_t ll_adv_pdu(uint8_t *pdu, uint8_t type, const uint8_t *d, uint8_t n)
{
    if (n > 31u)
        n = 31u;
    pdu[0] = (uint8_t)(type | bll.addr_rand << 6);   /* TxAdd; ChSel 0: we only know Algorithm #1 */
    pdu[1] = (uint8_t)(6u + n);
    ble_cpy(pdu + 2, bll.addr, 6);
    ble_cpy(pdu + 8, d, n);
    return (uint8_t)(8u + n);
}

static void ll_adv_start(void)
{
    struct ble_hw_adv a;
    a.adv = bll.adv;
    a.adv_len = bll.adv_len;
    a.scan_rsp = bll.sr;
    a.scan_rsp_len = bll.sr_len;
    a.interval = BLE_ADV_INTERVAL;
    a.channels = BLE_ADV_CHANNELS;
    bll.state = LL_ADV;
    ble_hw_adv_start(&a);
}

BLE_API void ble_ll_set_adv_data(const uint8_t *ad, uint8_t ad_len, const uint8_t *sr, uint8_t sr_len)
{
    bll.adv_len = ll_adv_pdu(bll.adv, 0x0u, ad, ad_len);           /* ADV_IND */
    bll.sr_len = ll_adv_pdu(bll.sr, 0x4u, sr, sr_len);              /* SCAN_RSP */
    if (bll.state == LL_ADV)
        ll_adv_start();
}

BLE_API int ble_ll_connected(void) { return bll.state == LL_CONN; }
BLE_API uint16_t ble_ll_interval(void) { return bll.state == LL_CONN ? bll.interval : 0; }
BLE_API uint8_t ble_ll_peer_features(void) { return bll.feat_known ? bll.peer_feat : 0; }

static void ll_close(uint8_t reason)
{
    ble_hw_conn_stop();
    bll.state = LL_OFF;
    ble_host_disconnected(reason);
    if (bll.enabled)
        ll_adv_start();
}

BLE_API void ble_ll_enable(int on)
{
    bll.enabled = on ? 1u : 0u;
    if (on && bll.state == LL_OFF)
        ll_adv_start();
    else if (!on && bll.state == LL_ADV) {
        ble_hw_adv_stop();
        bll.state = LL_OFF;
    } else if (!on && bll.state == LL_CONN)
        ble_ll_disconnect(BLE_ERR_REMOTE_USER);
}

/* timeout x 10 ms must exceed (1 + latency) x interval x 1.25 ms x 2 */
static int ll_params_ok(uint32_t interval, uint32_t latency, uint32_t timeout)
{
    return interval >= 6u && interval <= 3200u && timeout >= 10u && timeout <= 3200u && latency <= 499u &&
           timeout * 4u > (1u + latency) * interval;
}

BLE_API int ble_ll_hw_connect_ind(const uint8_t *pdu, uint8_t len)
{
    const uint8_t *p = pdu + 2;
    struct ble_hw_conn c;
    struct ble_chmap m;
    if (bll.state != LL_ADV || len < 2u + 34u || (pdu[0] & 0x0Fu) != 0x5u || pdu[1] != 34u ||
        (pdu[0] >> 7) != bll.addr_rand || !ble_eq(p + 6, bll.addr, 6))
        return 0;
    c.aa = ble_rd32(p + 12);
    c.crc_init = ble_rd24(p + 16);
    c.win_size = p[19];
    c.win_offset = ble_rd16(p + 20);
    c.interval = ble_rd16(p + 22);
    c.latency = ble_rd16(p + 24);
    c.timeout = ble_rd16(p + 26);
    ble_cpy(c.chm, p + 28, 5);
    c.chm[4] &= 0x1Fu;
    c.hop = p[33] & 0x1Fu;
    c.sca = p[33] >> 5;
    if (!ll_params_ok(c.interval, c.latency, c.timeout) || c.win_size < 1u || c.win_size > 8u ||
        c.win_size >= c.interval || c.win_offset > c.interval || c.hop < 5u || c.hop > 16u || !ble_chmap_set(&m, c.chm) ||
        c.aa == BLE_ADV_AA)
        return 0;
    /* a new connection: everything after the advertising state starts at zero */
    ble_zero((uint8_t *)&bll.interval, (uint32_t)((uint8_t *)&bll.ring - (uint8_t *)&bll.interval));
    bll.wr = bll.rd = bll.off = 0;
#if BLE_LL_ENC
    bll.enc_rx = bll.enc_tx = bll.tx_paused = 0;
#endif
    ble_cpy(bll.chm, c.chm, 5);
    bll.interval = c.interval;
    bll.timeout = c.timeout;
    bll.win_size = c.win_size;
    bll.win_offset = c.win_offset;
    bll.last_evt = 0xFFFFu;                         /* the first event is counter 0 */
    bll.max_tx = bll.max_rx = 27u;
    c.latency = 0;                                 /* we listen at every event */
    bll.state = LL_CONN;
    bll.t_start = bll.t_rx = ble_hw_time_us();
    ble_hw_conn_start(&c);
    ble_host_connected();
    return 1;
}

/* ------------------------------------------------------------------------------------------- TX queues --- */

static int ll_ctrl(uint8_t op, const uint8_t *d, uint8_t n)
{
    uint8_t i;
    if (bll.ctrl_n >= BLE_LL_CTRL_Q)
        return 0;
    i = (uint8_t)((bll.ctrl_rd + bll.ctrl_n) % BLE_LL_CTRL_Q);
    bll.ctrl[i][0] = op;
    ble_cpy(bll.ctrl[i] + 1, d, n);
    bll.ctrl_len[i] = (uint8_t)(n + 1u);
    bll.ctrl_n++;
    ble_hw_tx_kick();
    return 1;
}

static void ll_unknown(uint8_t op) { ll_ctrl(LL_UNKNOWN_RSP, &op, 1); }

/* refuse the central's procedure op: LL_REJECT_EXT_IND when the central knows it, else LL_REJECT_IND */
static void ll_reject(uint8_t op, uint8_t err)
{
    uint8_t d[2];
    d[0] = op;
    d[1] = err;
    if (bll.feat_known && (bll.peer_feat & BLE_FEAT_EXT_REJECT))
        ll_ctrl(LL_REJECT_EXT_IND, d, 2);
    else
        ll_ctrl(LL_REJECT_IND, d + 1, 1);
}

BLE_API uint32_t ble_ll_tx_room(void)
{
    uint32_t used = bll.wr - bll.rd;
    if (bll.state != LL_CONN || used + 4u >= BLE_LL_TX_RING)
        return 0;
    return BLE_LL_TX_RING - used - 4u;
}

BLE_API int ble_ll_send(uint16_t cid, const uint8_t *p, uint16_t n)
{
    uint8_t h[4];
    uint32_t i;
    if (n > ble_ll_tx_room() || bll.lproc == P_TERM)
        return 0;
    ble_wr16(h, n);
    ble_wr16(h + 2, cid);
    for (i = 0; i < 4u; i++)
        bll.ring[(bll.wr + i) & LL_RING_MASK] = h[i];
    for (i = 0; i < n; i++)
        bll.ring[(bll.wr + 4u + i) & LL_RING_MASK] = p[i];
    bll.wr += 4u + n;
    ble_hw_tx_kick();
    return 1;
}

static uint32_t ll_head_len(void)                 /* the head frame's octets, header included */
{
    return 4u + (bll.ring[bll.rd & LL_RING_MASK] | (uint32_t)bll.ring[(bll.rd + 1u) & LL_RING_MASK] << 8);
}

static int ll_data_ready(void)
{
#if BLE_LL_ENC
    if (bll.tx_paused)
        return 0;
#endif
    return bll.rd != bll.wr;
}

BLE_API uint8_t ble_ll_hw_tx(uint8_t *pdu)
{
    uint32_t n, i, room = bll.max_tx;               /* (the data length counts the payload without the MIC) */
    if (bll.state != LL_CONN)
        return 0;
    if (bll.ctrl_n) {
        n = bll.ctrl_len[bll.ctrl_rd];
        pdu[0] = 3u;
        ble_cpy(pdu + 2, bll.ctrl[bll.ctrl_rd], n);
        if (pdu[2] == LL_TERMINATE_IND) {
            bll.term_seq = (uint8_t)(bll.handed + 1u);
            bll.term_out = 1;
        }
        bll.ctrl_rd = (uint8_t)((bll.ctrl_rd + 1u) % BLE_LL_CTRL_Q);
        bll.ctrl_n--;
    } else if (ll_data_ready()) {
        uint32_t total = ll_head_len();
        n = ble_min(total - bll.off, room);
        pdu[0] = bll.off ? 1u : 2u;
        for (i = 0; i < n; i++)
            pdu[2 + i] = bll.ring[(bll.rd + bll.off + i) & LL_RING_MASK];
        bll.off += n;
        if (bll.off == total) {
            bll.rd += total;
            bll.off = 0;
        }
    } else
        return 0;
    if (bll.ctrl_n || ll_data_ready())
        pdu[0] |= 0x10u;                           /* MD: more queued */
    pdu[1] = (uint8_t)n;
#if BLE_LL_ENC
    if (bll.enc_tx) {
        ble_ccm_encrypt(&bll.ccm_tx, pdu[0], pdu + 2, (uint8_t)n);
        pdu[1] = (uint8_t)(n + 4u);
    }
#endif
    bll.handed++;
    return (uint8_t)(2u + pdu[1]);
}

BLE_API void ble_ll_hw_tx_acked(void)
{
    if (bll.state != LL_CONN || bll.acked == bll.handed)
        return;
    bll.acked++;
    if (bll.term_out && bll.acked == bll.term_seq)
        ll_close(BLE_ERR_LOCAL_HOST);
}

BLE_API void ble_ll_disconnect(uint8_t reason)
{
    if (bll.state != LL_CONN || bll.lproc == P_TERM)
        return;
    bll.ctrl_n = 0;                                 /* (anything not yet sent goes) */
    bll.rd = bll.wr;
    bll.off = 0;
    bll.lproc = P_TERM;
    bll.lproc_t = ble_hw_time_us();
    ll_ctrl(LL_TERMINATE_IND, &reason, 1);
}

/* -------------------------------------------------------------------------------- control procedures --- */

static void ll_lengths(const uint8_t *d)          /* the central's MaxRxOctets, MaxRxTime, MaxTxOctets, MaxTxTime */
{
    uint32_t rx_o = ble_rd16(d), rx_t = ble_rd16(d + 2), tx_o = ble_rd16(d + 4), tx_t = ble_rd16(d + 6);
    uint32_t rx_by_t = rx_t >= 328u ? rx_t / 8u - 14u : 27u, tx_by_t = tx_t >= 328u ? tx_t / 8u - 14u : 27u;
    rx_o = ble_min(ble_min(rx_o < 27u ? 27u : rx_o, rx_by_t), BLE_LL_MAX_OCTETS);
    tx_o = ble_min(ble_min(tx_o < 27u ? 27u : tx_o, tx_by_t), BLE_LL_MAX_OCTETS);
    bll.max_tx = (uint8_t)rx_o;                     /* we send at most what the central receives */
    bll.max_rx = (uint8_t)tx_o;
    bll.len_done = 1;
    ble_hw_set_lengths(bll.max_tx, bll.max_rx);
}

static void ll_our_lengths(uint8_t *d)
{
    ble_wr16(d, BLE_LL_MAX_OCTETS);
    ble_wr16(d + 2, (BLE_LL_MAX_OCTETS + 14u) * 8u);
    ble_wr16(d + 4, BLE_LL_MAX_OCTETS);
    ble_wr16(d + 6, (BLE_LL_MAX_OCTETS + 14u) * 8u);
}

static void ll_features(uint8_t op, uint8_t peer)
{
    uint8_t d[8];
    ble_zero(d, 8);
    bll.peer_feat = peer;
    bll.feat_known = 1;
    d[0] = (uint8_t)(LL_OUR_FEAT & peer);          /* octet 0: the features both support */
    if (op)
        ll_ctrl(op, d, 8);
}

/* 1 if the instant is still ahead of (or at) the current event: (instant - counter) mod 65536 < 32767 */
static int ll_instant_ok(uint16_t instant)
{
    return (uint16_t)(instant - (uint16_t)(bll.last_evt + 1u)) < 32767u;
}

/* a control PDU from the central (p: opcode and CtrData, n octets) */
static void ll_rx_ctrl(const uint8_t *p, uint8_t n)
{
    static const uint8_t LEN[] = {12, 8, 2, 23, 13, 1, 1, 2, 9, 9, 1, 1, 6, 2, 9, 24, 24, 3, 1, 1, 9, 9, 3, 3, 5, 3};
    uint8_t op = p[0], d[24];
    if (!n)
        return;
    if (op >= sizeof LEN || n != LEN[op]) {        /* unknown, or not its length */
        ll_unknown(op);
        return;
    }
    switch (op) {
    case LL_CONNECTION_UPDATE_IND: {
        uint16_t instant = ble_rd16(p + 10);
        if (!ll_instant_ok(instant)) {
            ll_close(BLE_ERR_INSTANT_PASSED);
            return;
        }
        bll.upd.win_size = p[1];
        bll.upd.win_offset = ble_rd16(p + 2);
        bll.upd.interval = ble_rd16(p + 4);
        bll.upd.latency = 0;
        bll.upd.timeout = ble_rd16(p + 8);
        bll.upd.instant = instant;
        if (!ll_params_ok(bll.upd.interval, ble_rd16(p + 6), bll.upd.timeout)) {
            ll_close(BLE_ERR_INVALID_LL_PARAMS);
            return;
        }
        bll.upd_pending = 1;
        ble_hw_conn_update(&bll.upd);
        if (bll.rproc == P_UPD)
            bll.rproc = P_NONE;
        return;
    }
    case LL_CHANNEL_MAP_IND: {
        struct ble_chmap m;
        uint16_t instant = ble_rd16(p + 6);
        if (!ll_instant_ok(instant)) {
            ll_close(BLE_ERR_INSTANT_PASSED);
            return;
        }
        ble_cpy(bll.new_chm, p + 1, 5);
        bll.new_chm[4] &= 0x1Fu;
        if (!ble_chmap_set(&m, bll.new_chm)) {
            ll_close(BLE_ERR_INVALID_LL_PARAMS);
            return;
        }
        bll.chm_instant = instant;
        bll.chm_pending = 1;
        ble_hw_chmap_update(bll.new_chm, instant);
        return;
    }
    case LL_TERMINATE_IND:
        bll.term_rx = 1;                            /* closed after this event: the engine acknowledges it */
        bll.term_reason = p[1];
        return;
    case LL_FEATURE_REQ:
        ll_features(LL_FEATURE_RSP, p[1]);
        return;
    case LL_FEATURE_RSP:
        ll_features(0, p[1]);
        if (bll.lproc == P_FEAT)
            bll.lproc = P_NONE;
        return;
    case LL_VERSION_IND:
        if (!bll.ver_sent) {                        /* once per connection */
            d[0] = BLE_LL_VERSION;
            ble_wr16(d + 1, BLE_LL_COMPANY);
            ble_wr16(d + 3, BLE_LL_SUBVERSION);
            bll.ver_sent = ll_ctrl(LL_VERSION_IND, d, 5);
        }
        return;
    case LL_PING_REQ:
        ll_ctrl(LL_PING_RSP, d, 0);
        return;
    case LL_LENGTH_REQ:
        ll_lengths(p + 1);
        ll_our_lengths(d);
        ll_ctrl(LL_LENGTH_RSP, d, 8);
        return;
    case LL_LENGTH_RSP:
        ll_lengths(p + 1);
        if (bll.lproc == P_LEN)
            bll.lproc = P_NONE;
        return;
    case LL_PHY_REQ:
        d[0] = 1u;                                 /* LE 1M only, both ways */
        d[1] = 1u;
        ll_ctrl(LL_PHY_RSP, d, 2);
        bll.rproc = P_PHY;
        bll.rproc_t = ble_hw_time_us();
        return;
    case LL_PHY_UPDATE_IND:
        if (bll.rproc == P_PHY)
            bll.rproc = P_NONE;
        if ((p[1] | p[2]) & ~1u)                   /* anything but 1M (0: no change): we cannot follow */
            ll_close(BLE_ERR_UNSUPP_REMOTE);
        return;
    case LL_CONNECTION_PARAM_REQ:
        if (!ll_params_ok(ble_rd16(p + 3), ble_rd16(p + 5), ble_rd16(p + 7)) || ble_rd16(p + 1) > ble_rd16(p + 3) ||
            ble_rd16(p + 1) < 6u) {
            ll_reject(op, BLE_ERR_INVALID_LL_PARAMS);
            return;
        }
        ll_ctrl(LL_CONNECTION_PARAM_RSP, p + 1, 23);   /* accepted as asked */
        bll.rproc = P_UPD;
        bll.rproc_t = ble_hw_time_us();
        return;
    case LL_UNKNOWN_RSP:
    case LL_REJECT_IND:
    case LL_REJECT_EXT_IND:
        if (bll.lproc == P_FEAT || bll.lproc == P_LEN)
            bll.lproc = P_NONE;                     /* the central does not do it: carry on without */
        if (op == LL_UNKNOWN_RSP && p[1] == LL_PERIPHERAL_FEATURE_REQ)
            bll.feat_known = 1;
        if (op == LL_UNKNOWN_RSP && p[1] == LL_LENGTH_REQ)
            bll.len_done = 1;
        return;
#if BLE_LL_ENC
    case LL_ENC_REQ: {
        uint8_t ltk[16], skd[16], i;
        bll.tx_paused = 1;
        ble_hw_rand(d, 12);                        /* SKDs (8), IVs (4) */
        ll_ctrl(LL_ENC_RSP, d, 12);
        if (!ble_host_ltk(p + 1, ble_rd16(p + 9), ltk)) {
            bll.tx_paused = 0;
            ll_reject(op, BLE_ERR_PIN_KEY_MISSING);
            return;
        }
        for (i = 0; i < 8u; i++) {                 /* SKD = SKDs || SKDm, most significant octet first */
            skd[i] = d[7 - i];
            skd[8 + i] = p[11 + 7 - i];
        }
        for (i = 0; i < 16u; i++)
            bll.ccm_rx.key[i] = ltk[15 - i];
        ble_aes128(bll.ccm_rx.key, skd, bll.ccm_rx.key);
        ble_cpy(bll.ccm_rx.iv, p + 19, 4);          /* IV = IVm || IVs */
        ble_cpy(bll.ccm_rx.iv + 4, d + 8, 4);
        bll.ccm_rx.ctr = 0;
        bll.ccm_rx.ctr_hi = 0;
        bll.ccm_tx = bll.ccm_rx;
        bll.ccm_rx.dir = 1;
        bll.ccm_tx.dir = 0;
        ll_ctrl(LL_START_ENC_REQ, d, 0);
        bll.enc_rx = 1;
        bll.rproc = P_ENC;
        bll.rproc_t = ble_hw_time_us();
        return;
    }
    case LL_START_ENC_RSP:
        if (bll.rproc != P_ENC || !bll.enc_rx)
            return;
        bll.enc_tx = 1;
        ll_ctrl(LL_START_ENC_RSP, d, 0);
        bll.tx_paused = 0;
        bll.rproc = P_NONE;
        return;
#else
    case LL_ENC_REQ:
        ll_reject(op, BLE_ERR_UNSUPP_REMOTE);
        return;
#endif
    default:                                       /* peripheral-to-central PDUs, PAUSE_ENC, ... */
        ll_unknown(op);
        return;
    }
}

BLE_API void ble_ll_hw_rx(const uint8_t *pdu, uint8_t len)
{
    uint8_t n, llid;
    const uint8_t *p = pdu + 2;
    if (bll.state != LL_CONN || len < 2u || pdu[1] > len - 2u)
        return;
    n = pdu[1];
    llid = pdu[0] & 3u;
    bll.established = 1;
#if BLE_LL_ENC
    if (bll.enc_rx && n) {
        if (n > 4u + BLE_LL_MAX_OCTETS)
            return;
        ble_cpy(bll.rxbuf, p, n);
        if (!ble_ccm_decrypt(&bll.ccm_rx, pdu[0], bll.rxbuf, n)) {
            ll_close(BLE_ERR_MIC_FAILURE);
            return;
        }
        p = bll.rxbuf;
        n = (uint8_t)(n - 4u);
    }
#endif
    if (n > BLE_LL_MAX_OCTETS)
        return;
    if (llid == 3u)
        ll_rx_ctrl(p, n);
    else if (llid && n && bll.lproc != P_TERM)
        ble_host_rx(p, n, llid == 2u);
}

/* our own procedures, one at a time, once the link is up: the feature exchange, then the data length */
static void ll_start_procs(uint32_t now)
{
    uint8_t d[8];
    if (bll.lproc != P_NONE || !bll.established)
        return;
    if (!bll.feat_known && BLE_LL_PERIPH_FEAT && (uint16_t)(bll.last_evt + 1u) >= 6u) {
        ble_zero(d, 8);
        d[0] = (uint8_t)LL_OUR_FEAT;
        if (ll_ctrl(LL_PERIPHERAL_FEATURE_REQ, d, 8)) {
            bll.lproc = P_FEAT;
            bll.lproc_t = now;
        }
    } else if (BLE_LL_MAX_OCTETS > 27u && bll.feat_known && !bll.len_done && (bll.peer_feat & BLE_FEAT_DLE)) {
        ll_our_lengths(d);
        if (ll_ctrl(LL_LENGTH_REQ, d, 8)) {
            bll.lproc = P_LEN;
            bll.lproc_t = now;
        }
    }
}

BLE_API void ble_ll_hw_event_end(uint16_t counter, uint8_t rx_ok)
{
    uint32_t now = ble_hw_time_us();
    if (bll.state != LL_CONN)
        return;
    bll.last_evt = counter;
    if (rx_ok) {
        bll.t_rx = now;
        bll.established = 1;
    }
    if (bll.term_rx) {
        ll_close(bll.term_reason);
        return;
    }
    if (bll.upd_pending && (uint16_t)(counter - bll.upd.instant) < 0x8000u) {
        bll.upd_pending = 0;                        /* the instant's event is over: the new parameters hold */
        bll.interval = bll.upd.interval;
        bll.timeout = bll.upd.timeout;
        bll.t_rx = now;                             /* (the supervision timer starts again with them) */
    }
    if (bll.chm_pending && (uint16_t)(counter - bll.chm_instant) < 0x8000u) {
        bll.chm_pending = 0;
        ble_cpy(bll.chm, bll.new_chm, 5);
    }
    if (!bll.established) {                         /* six intervals after the transmit window, nothing heard */
        if (now - bll.t_start > (1u + bll.win_offset + bll.win_size + 6u * (uint32_t)bll.interval) * 1250u) {
            ll_close(BLE_ERR_CONN_FAILED);
            return;
        }
    } else if (now - bll.t_rx > (uint32_t)bll.timeout * 10000u) {
        ll_close(BLE_ERR_CONN_TIMEOUT);
        return;
    }
    if (bll.lproc == P_TERM && now - bll.lproc_t > (uint32_t)bll.timeout * 10000u) {
        ll_close(BLE_ERR_LOCAL_HOST);              /* our LL_TERMINATE_IND never acknowledged */
        return;
    }
    if ((bll.lproc != P_NONE && bll.lproc != P_TERM && now - bll.lproc_t > LL_PROC_US) ||
        (bll.rproc != P_NONE && now - bll.rproc_t > LL_PROC_US)) {
        ll_close(BLE_ERR_LL_RSP_TIMEOUT);
        return;
    }
    ll_start_procs(now);
    if (bll.lproc != P_TERM)
        ble_host_event();
}
