/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of the WL82 baseband driver (firmware/src/ble/ble_hw_wl82.c) with the whole stack, against a fake engine
 * that behaves as the FM-1 measured (blell3 9a90c7d, blell4 5008663, blell6 0475aa5, blell8 cbb94d1):
 *   - advertising: the CONNECT_IND lands in the buffer RXTOG then points to, RXTOG moves past it, RXBUFnCNTL stays 0;
 *     advertising leaves TXTOG bit0 = 1;
 *   - connection RX: the engine fills RXTOG's buffer, sets its RXBUFnCNTL bit0, moves RXTOG; the received header in
 *     RXDHDRn carries the central's NESN (bit2) and SN (bit3), Core layout;
 *   - connection TX, the model blell8 fits (docs/BLE-HW-FACTS.md §8.1): acknowledgement and flow control as the Core
 *     spec (Vol 6 Part B 4.5.9): the engine keeps transmitSeqNum / nextExpectedSeqNum; the SN of a TX buffer is its
 *     TXDHDRn bit2, fixed per buffer (§7 step 17 gives the two buffers opposite bits; no software and no engine write
 *     ever changed it: blell8 dhdr0 0907 / dhdr1 0903 through 65 moves), so the engine sends the buffer whose bit2 is
 *     its transmitSeqNum: a new PDU from the other buffer after every acknowledgement (ping-pong), the same buffer
 *     again until acknowledged. TXBUFnCNTL bit0 = 1: loaded. The engine clears it on the buffer it sends (blell4,
 *     blell6: set-up's empty PDU and the VERSION_IND) and SETS it again on the buffer whose PDU the central
 *     acknowledged (blell6 txsnap 4: TXBUF1CNTL 01 the event after the VERSION_IND was acknowledged, no software
 *     write; blell8: the buffer cbb94d1 moved a PDU away from read 01 again within 4 events). A "loaded" bit on a
 *     buffer the engine reaches sends whatever is in it as a new PDU;
 *   - the Mac (blell8): VERSION_IND, FEATURE_REQ two events later, LL_LENGTH_REQ once it has our FEATURE_RSP, then
 *     ATT: Exchange MTU, Read By Group Type, the MIDI CCCD write; it terminates (0x13) 249 events (7.5 s) after a
 *     LENGTH_REQ nobody answered, as it did 7.47 s after blell8's;
 *   - the slot clock (columns 0 / 14) steps back 267 slots now and then, as the FM-1's did.
 * Against this engine cbb94d1's driver (ack = TXBUFnCNTL bit0 cleared, a PDU not taken in 2 events moved to the other
 * buffer) fails as on the FM-1: our FEATURE_RSP is moved back and forth (tx_moved, tx_force_free), the Mac's LENGTH_REQ
 * is never answered and the Mac terminates. Checks, with the fix (acknowledgement by the central's NESN): our
 * VERSION_IND, FEATURE_RSP, LENGTH_RSP, the ATT responses and a MIDI notification go out once each, in order, within
 * a few events of being asked; no PDU of ours is sent twice as new; the link lives past 60 s; with a central that waits,
 * our PERIPHERAL_FEATURE_REQ goes out; a stale bit appearing mid-connection is freed before the engine reaches it; no
 * column 0 / 14 read from the ISRs; the 40 s timeout still fires when the central never answers; TIMER4 wrapping in
 * the middle; packet loss (central and peripheral side); and the fact sheet's TX direction (the engine sets bit0 back
 * to 1, TXTOG moves: the emulator's model) still works. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#include "../firmware/src/ble/ble_stack.c"
#include "../firmware/src/ble/ble_diag.c"

static int fails;
static void check(const char *what, int ok)
{
    printf("%-118s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

static struct {
    int on;                             /* the app has a MIDI event for us */
    uint32_t popped;
} app;

static int ble_app_midi_peek(uint32_t *pkt, uint32_t *t)
{
    if (!app.on)
        return 0;
    *pkt = 0x7F3C9009u;                 /* note on, C4, 127 (USB-MIDI CIN 9) */
    *t = 100;
    return 1;
}
static void ble_app_midi_pop(void)
{
    app.on = 0;
    app.popped++;
}
static void ble_app_midi_in(uint32_t pkt, uint16_t ts, uint16_t last)
{
    (void)pkt;
    (void)ts;
    (void)last;
}
static void ble_app_state(void) {}

#define IV 24u                          /* the Mac's CONNECT_IND: 30 ms, timeout 72 (720 ms), hop 13, sca 1 */
#define TICKS_PER_EVT (IV * 1250u * FM1_TICKS_PER_US)
#define RESTALE_EVT 30u                 /* the "stale" scenario: a stale bit on a free buffer here */
#define MAC_LEN_WAIT 249u               /* the Mac's patience with its LENGTH_REQ (blell8: 7.47 s) */
#define MIDI_EVT 120u                   /* the app's note */

enum { C_WAITS, C_MAC, C_SILENT };      /* the central: answers our FEATURE_REQ / asks first (blell8) / never answers */

static struct {
    int clears;                         /* 1: the FM-1; 0: the fact sheet (the emulator's model) */
    int kind;                           /* C_* */
    int restale;                        /* a stale "loaded" bit on a free buffer at RESTALE_EVT */
    int loss;                           /* 1: every 7th packet of the central and every 11th of ours lost */
    uint8_t q[8][32], qlen[8], qllid[8];  /* the central's PDUs to send */
    int qn;
    uint8_t cur[32], cur_len, cur_llid; /* the PDU in flight (until acknowledged) */
    int cur_ok, sn, nesn, sheet_sn;
    int ver_rx, feat_req_rx, feat_rsp_rx, len_rsp_rx, len_req_rx, mtu_rsp_rx, group_rsp_rx, write_rsp_rx, notif_rx;
    int data_rx, empties, dups, junk_rx, order_bad;
    uint32_t e, len_req_evt, group_evt, notif_evt, feat_req_evt, mtu_req_evt, write_req_evt;
    int len_req_out, terminated;
} cen;

static struct {                         /* the fake FM-1 engine's TX state */
    int tsn, nesn, sent_any, retx, data_sent;
    uint8_t last[40];                   /* the PDU last sent (header 2 + payload), for a retransmission */
    uint32_t sends[2], rearms;
} eng;

static void cen_pdu(uint8_t llid, const uint8_t *p, int n)
{
    memcpy(cen.q[cen.qn], p, (size_t)n);
    cen.qlen[cen.qn] = (uint8_t)n;
    cen.qllid[cen.qn++] = llid;
}
static void cen_ctrl(const uint8_t *p, int n) { cen_pdu(3, p, n); }

static void cind(void)                  /* the CONNECT_IND, as the FM-1 stored it */
{
    uint32_t b = CB->rxtog & 1u;
    uint8_t *p = bb.rx[b].buf + HW_SWHDR;
    static const uint8_t init_a[6] = {1, 2, 3, 4, 5, 0x46};
    memcpy(p, init_a, 6);
    memcpy(p + 6, drv.adv.adv + 2, 6);
    p[12] = 0xDB, p[13] = 0xAC, p[14] = 0x9A, p[15] = 0xAF;           /* AA AF9AACDB */
    p[16] = 0xA4, p[17] = 0xEF, p[18] = 0x2D;                          /* CRC init */
    p[19] = 3;                                                          /* WinSize */
    p[20] = 22, p[21] = 0;                                              /* WinOffset */
    p[22] = IV, p[23] = 0;
    p[24] = 0, p[25] = 0;
    p[26] = 72, p[27] = 0;
    p[28] = 0xFF, p[29] = 0xFF, p[30] = 0xFF, p[31] = 0xFF, p[32] = 0x1F;
    p[33] = (uint8_t)(13u | 1u << 5);
    CB->rxahdr[b] = 0x2285u;                                            /* blell3 rxsnap: ahdr 2285, dhdr 2200 */
    CB->rxdhdr[b] = 0x2200u;
    CB->rxstat[b] = 0x9401u;
    CB->rxtog ^= 1u;                                                    /* moved past; RXBUFnCNTL stays 0 */
    CB->txtog = 1u;                                                     /* advertising left TXTOG on buffer 1 (blell4) */
    ble_wl82_rx_irq();
}

/* the central takes a new PDU of ours (header h, payload q) */
static void cen_rx(uint16_t h, const uint8_t *q)
{
    uint8_t n = (uint8_t)(h >> 8);
    if (!n) {
        cen.empties++;
        return;
    }
    if ((h & 3u) == 3u) {
        switch (q[0]) {
        case LL_VERSION_IND:
            cen.ver_rx++;
            break;
        case LL_FEATURE_RSP:
            cen.feat_rsp_rx++;
            if (cen.kind == C_MAC && !cen.len_req_out) {                /* the Mac: DLE next (blell8) */
                static const uint8_t req[9] = {LL_LENGTH_REQ, 251, 0, 0x48, 0x08, 251, 0, 0x48, 0x08};
                cen_ctrl(req, 9);
                cen.len_req_out = 1;
            }
            break;
        case LL_LENGTH_RSP:
            cen.len_rsp_rx++;
            if (!cen.feat_rsp_rx)
                cen.order_bad++;                                        /* (before our FEATURE_RSP) */
            if (cen.kind == C_MAC) {
                static const uint8_t mtu[7] = {3, 0, 4, 0, 0x02, 0x05, 0x01};   /* Exchange MTU, 517 */
                cen_pdu(2, mtu, 7);
                cen.mtu_req_evt = cen.e;
            }
            break;
        case LL_LENGTH_REQ: {
            static const uint8_t rsp[9] = {LL_LENGTH_RSP, 251, 0, 0x48, 0x08, 251, 0, 0x48, 0x08};
            cen.len_req_rx++;
            cen_ctrl(rsp, 9);
            break;
        }
        case LL_PERIPHERAL_FEATURE_REQ: {
            static const uint8_t rsp[9] = {LL_FEATURE_RSP, 0x01};
            cen.feat_req_rx++;
            if (cen.kind == C_WAITS)
                cen_ctrl(rsp, 9);
            break;
        }
        default:
            break;
        }
        return;
    }
    cen.data_rx++;
    if ((h & 3u) == 2u && q[2] == 4u && q[3] == 0u) {                  /* L2CAP, the ATT channel */
        if (q[4] == 0x03u) {
            static const uint8_t grp[11] = {7, 0, 4, 0, 0x10, 0x01, 0x00, 0xFF, 0xFF, 0x00, 0x28};
            cen.mtu_rsp_rx++;
            cen_pdu(2, grp, 11);                                        /* Read By Group Type, primary services */
        }
        if (q[4] == 0x11u) {
            static const uint8_t wr[9] = {5, 0, 4, 0, 0x12, 15, 0, 1, 0};   /* Write Request: the MIDI CCCD = 1 */
            cen.group_rsp_rx++;
            cen.group_evt = cen.e;
            cen_pdu(2, wr, 9);
            cen.write_req_evt = cen.e;
        }
        if (q[4] == 0x13u)
            cen.write_rsp_rx++;
        if (q[4] == 0x1Bu && q[5] == 14u) {
            cen.notif_rx++;
            cen.notif_evt = cen.e;
        }
    }
}

/* a stale "loaded" bit on the buffer the driver does not use right now, with a header the central must never see */
static void restale(void)
{
    uint32_t b;
    for (b = 0; b < 2u; b++)
        if (!(CB->txbufcntl[b] & 1u) && (CB->txdhdr[b] >> 2 & 1u) != (uint32_t)eng.tsn) {
            bb.tx[b].buf[HW_SWHDR] = 0xEE;
            CB->txdhdr[b] = (uint16_t)((CB->txdhdr[b] & 4u) | 1u << 8 | 3u);   /* a 1-octet control PDU 0xEE */
            CB->txbufcntl[b] |= 1u;
            return;
        }
}

/* the FM-1 engine, blell8's fit (see the top): one connection event. The central's packet first (its NESN
 * acknowledges our last, its SN is new or a repeat), then the engine's answer, then the central reads it */
static void eng_fm1_event(uint32_t rb, int c_lost, int p_lost)
{
    uint8_t llid = cen.cur_ok ? cen.cur_llid : 1u, n = cen.cur_ok ? cen.cur_len : 0u;
    uint32_t b;
    uint16_t h;
    const uint8_t *q;
    if (c_lost)
        return;                                                         /* the engine heard nothing: no answer */
    if (cen.nesn != eng.tsn) {                                          /* our last acknowledged */
        b = (CB->txdhdr[0] >> 2 & 1u) == (uint32_t)eng.tsn ? 0u : 1u;
        if (!(CB->txbufcntl[b] & 1u))
            eng.rearms++;
        CB->txbufcntl[b] |= 1u;                                         /* blell6 txsnap 4, blell8: set on the ack */
        eng.tsn ^= 1;
        eng.retx = 0;
    } else
        eng.retx = eng.sent_any;
    if (cen.sn == eng.nesn) {                                           /* new: into RXTOG's buffer */
        uint8_t *p = bb.rx[rb].buf + HW_SWHDR;
        eng.nesn ^= 1;
        memcpy(p, cen.cur, n);
        CB->rxdhdr[rb] = (uint16_t)(n << 8 | (uint32_t)cen.sn << 3 | (uint32_t)cen.nesn << 2 | llid);
        CB->rxstat[rb] = 0x9401u;
        CB->rxbufcntl[rb] |= 1u;
        CB->rxtog ^= 1u;
    }                                                                   /* (a repeat: dropped by the engine, model) */
    b = (CB->txdhdr[0] >> 2 & 1u) == (uint32_t)eng.tsn ? 0u : 1u;       /* SN fixed per buffer: the ping-pong */
    if (!eng.retx) {
        if (CB->txbufcntl[b] & 1u) {
            eng.last[0] = (uint8_t)(CB->txdhdr[b] & 3u);
            eng.last[1] = (uint8_t)(CB->txdhdr[b] >> 8);
            memcpy(eng.last + 2, bb.tx[b].buf + HW_SWHDR, eng.last[1] < 38u ? eng.last[1] : 38u);
            if (!eng.data_sent || !cen.clears)                          /* cleared on the buffer it sends: the */
                CB->txbufcntl[b] &= (uint8_t)~1u;                       /* first data PDU only (blell4/6/8: the */
            if (eng.last[1])                                            /* VERSION_IND; blell8: never FEATURE_RSP) */
                eng.data_sent = 1;
            eng.sends[b]++;
        } else
            eng.last[0] = 1, eng.last[1] = 0;                           /* nothing loaded: an empty PDU of its own */
        if (!eng.sent_any)
            CB->txtog |= 6u;                                            /* 7 after the first packet (blell4) */
        eng.sent_any = 1;
    }
    if (p_lost)
        return;
    h = (uint16_t)(eng.last[1] << 8 | (uint32_t)eng.tsn << 3 | (uint32_t)eng.nesn << 2 | eng.last[0]);
    q = eng.last + 2;
    if ((h >> 3 & 1u) == (uint32_t)cen.nesn) {                          /* the central: new from us */
        cen.nesn ^= 1;
        if (eng.last[1] == 1u && q[0] == 0xEEu)
            cen.junk_rx++;
        cen_rx(h, q);
    } else if (eng.last[1])
        cen.dups++;                                                     /* (a retransmission: the central drops it) */
    if ((h >> 2 & 1u) != (uint32_t)cen.sn) {                            /* ours acknowledged its packet */
        cen.sn ^= 1;
        cen.cur_ok = 0;
    }
}

/* the fact sheet's engine (the emulator's model): bit0 = 0 loaded, set back to 1 when done, TXTOG moves */
static void eng_sheet_tx(void)
{
    uint32_t t = CB->txtog & 1u;
    if (CB->txbufcntl[t] & 1u)
        return;
    cen_rx(CB->txdhdr[t], bb.tx[t].buf + HW_SWHDR);
    CB->txbufcntl[t] |= 1u;
    CB->txtog ^= 1u;
}

static void cen_next(void)              /* the central's next PDU, once the last one was acknowledged */
{
    if (cen.cur_ok || !cen.qn)
        return;
    memcpy(cen.cur, cen.q[0], cen.qlen[0]);
    cen.cur_len = cen.qlen[0];
    cen.cur_llid = cen.qllid[0];
    cen.cur_ok = 1;
    memmove(cen.q, cen.q + 1, sizeof cen.q[0] * 7);
    memmove(cen.qlen, cen.qlen + 1, 7);
    memmove(cen.qllid, cen.qllid + 1, 7);
    cen.qn--;
}

/* one connection event: the central's packet, the engine's answer, the RX IRQ, the event IRQ */
static void event(uint32_t e)
{
    uint32_t b = CB->rxtog & 1u;
    cen.e = e;
    if (cen.kind == C_MAC && cen.len_req_out == 1 && !cen.len_rsp_rx && !cen.terminated) {
        if (!cen.len_req_evt)
            cen.len_req_evt = e;
        else if (e - cen.len_req_evt >= MAC_LEN_WAIT) {                /* blell8: TERMINATE_IND, 0x13 */
            static const uint8_t term[2] = {LL_TERMINATE_IND, 0x13};
            cen_ctrl(term, 2);
            cen.terminated = 1;
        }
    }
    if (cen.kind == C_MAC && e == MIDI_EVT)
        app.on = 1;
    cen_next();
    if (cen.clears)
        eng_fm1_event(b, cen.loss && e % 7u == 3u, cen.loss && e % 11u == 5u);
    else {
        uint8_t llid = cen.cur_ok ? cen.cur_llid : 1u, n = cen.cur_ok ? cen.cur_len : 0u;
        memcpy(bb.rx[b].buf + HW_SWHDR, cen.cur, n);
        CB->rxdhdr[b] = (uint16_t)(n << 8 | (uint32_t)cen.sheet_sn << 3 | llid);
        CB->rxstat[b] = 0x9401u;
        CB->rxbufcntl[b] |= 1u;
        CB->rxtog ^= 1u;
        cen.sheet_sn ^= 1;
        cen.cur_ok = 0;
    }
    ble_wl82_rx_irq();
    if (!cen.clears)
        eng_sheet_tx();
    if (cen.restale && e == RESTALE_EVT)
        restale();                                                      /* after the RX IRQ, before the event IRQ */
    fk.slots += 2u * IV;
    if (e % 97u == 96u)
        fk.slots -= 267u;                                               /* the FM-1's backward step */
    fk.ticks += TICKS_PER_EVT;
    fk.col3 = e + 1u;
    ble_wl82_event_irq();
}

static void run(int clears, int kind, int restale_on, int loss, uint32_t ticks0, uint32_t events, const char *name)
{
    static const uint8_t addr[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0xC6};
    static const uint8_t ver[6] = {LL_VERSION_IND, 0x0C, 0x0F, 0x00, 0x00, 0x01};
    static const uint8_t feat[9] = {LL_FEATURE_REQ, 0x3F};              /* the Mac's: DLE among them */
    struct ble_rf_trims tr;
    uint32_t e, t_conn;
    char what[220];
    memset(&cen, 0, sizeof cen);
    memset(&eng, 0, sizeof eng);
    memset(&app, 0, sizeof app);
    memset(&fk, 0, sizeof fk);
    memset(&tr, 0, sizeof tr);
    ble_diag_clear();
    fk.ticks = ticks0;
    fk.tick_per_read = FM1_TICKS_PER_US;
    cen.clears = clears;
    cen.kind = kind;
    cen.restale = restale_on;
    cen.loss = loss;
    ble_hw_wl82_start(&tr);
    ble_init(addr, 1);
    ble_enable(1);
    fk.ticks += 50000u * FM1_TICKS_PER_US;
    cind();
    snprintf(what, sizeof what, "%s: CONNECT_IND taken, found in the buffer RXTOG moved past (CNTL 0)", name);
    check(what, ble_ll_connected() && ble_dg.cind_ok == 1 && ble_dg.rxf_tog_prev == 1 && ble_dg.rxf_cntl == 0);
    t_conn = fk.ticks;
    memset(fk.col_reads, 0, sizeof fk.col_reads);
    cen_ctrl(ver, 6);
    if (kind == C_MAC) {                                                /* blell8: FEATURE_REQ two events later */
        cen_pdu(1, feat, 0);
        cen_ctrl(feat, 9);
    }
    for (e = 0; e < events && ble_ll_connected(); e++)
        event(e);
    snprintf(what, sizeof what, "%s: TXBUFnCNTL direction learnt (%u), our VERSION_IND once", name,
             (unsigned)ble_dg.tx_pol);
    check(what, ble_dg.tx_pol == (clears ? BTP_CLEARS : BTP_SHEET) && cen.ver_rx == 1);
    if (kind == C_MAC) {
        snprintf(what, sizeof what, "%s: FEATURE_RSP %d, LENGTH_RSP %d (Mac's LENGTH_REQ in event %u), MTU / group / "
                 "write responses %d / %d / %d", name, cen.feat_rsp_rx, cen.len_rsp_rx, (unsigned)cen.len_req_evt,
                 cen.mtu_rsp_rx, cen.group_rsp_rx, cen.write_rsp_rx);
        check(what, cen.feat_rsp_rx == 1 && cen.len_rsp_rx == 1 && cen.mtu_rsp_rx == 1 && cen.group_rsp_rx == 1 &&
                        cen.write_rsp_rx == 1 && cen.order_bad == 0 && ble_dg.tx_queued >= 7 &&
                        ble_dg.tx_acked >= 7);
        snprintf(what, sizeof what, "%s: a MIDI notification out (%d, event %u, asked in %u)", name, cen.notif_rx,
                 (unsigned)cen.notif_evt, MIDI_EVT);
        check(what, cen.notif_rx == 1 && cen.notif_evt >= MIDI_EVT && cen.notif_evt <= MIDI_EVT + (loss ? 8u : 4u));
        if (clears) {
            snprintf(what, sizeof what, "%s: the LENGTH_RSP within %u events of the LENGTH_REQ", name,
                     (unsigned)(cen.mtu_req_evt - cen.len_req_evt));
            check(what, cen.mtu_req_evt >= cen.len_req_evt && cen.mtu_req_evt - cen.len_req_evt <= (loss ? 8u : 4u));
        }
    } else {
        snprintf(what, sizeof what, "%s: our PERIPHERAL_FEATURE_REQ out (%d)", name, cen.feat_req_rx);
        check(what, cen.feat_req_rx == 1 && ble_dg.tx_queued >= 2 && ble_dg.tx_acked >= 2 && ble_dg.ctl_tx_n >= 2);
    }
    if (clears) {
        snprintf(what, sizeof what, "%s: ack by NESN (%u acked, %u by CNTL as well), no PDU of ours new twice, no junk "
                 "(re-armed bits %u, freed %u)", name, (unsigned)ble_dg.tx_acked, (unsigned)ble_dg.tx_cntl_clr,
                 (unsigned)eng.rearms, (unsigned)ble_dg.tx_rearm_clr);
        check(what, cen.ver_rx == 1 && cen.feat_rsp_rx <= 1 && cen.len_rsp_rx <= 1 && cen.mtu_rsp_rx <= 1 &&
                        cen.group_rsp_rx <= 1 && cen.notif_rx <= 1 && cen.junk_rx == 0 && ble_dg.tx_moved == 0 &&
                        ble_dg.tx_force_free == 0 && ble_dg.tx_stale_clr == 1);
    }
    snprintf(what, sizeof what, "%s: no column 0 / 14 (slot clock) read from the ISRs over %u events", name,
             (unsigned)e);
    check(what, fk.col_reads[0] == 0 && fk.col_reads[14] == 0);
    if (!loss) {
        snprintf(what, sizeof what, "%s: RX in a connection: CNTL bit0, RXTOG past it (%u / %u)", name,
                 (unsigned)ble_dg.rxc_tog_past, (unsigned)ble_dg.rxc_tog_at);
        check(what, ble_dg.rx_desync == 0 && ble_dg.rxc_tog_past >= e - 1u && ble_dg.rxc_tog_at == 0);
    }
    if (kind != C_SILENT) {
        snprintf(what, sizeof what, "%s: the link still up after %u s (procedure timeout 40 s, supervision 720 ms)",
                 name, (unsigned)((fk.ticks - t_conn) / 24000000u));
        check(what, ble_ll_connected() && ble_dg.closes == 0 && e == events && !cen.terminated);
    } else {
        uint32_t s = (fk.ticks - t_conn) / 24000u;
        snprintf(what, sizeof what, "%s: no FEATURE_RSP: LL response timeout 0x22 at 40 s (%u ms after connect)", name,
                 (unsigned)s);
        check(what, !ble_ll_connected() && ble_dg.closes == 1 && ble_dg.close_reason == BLE_ERR_LL_RSP_TIMEOUT &&
                        s >= 40000u && s <= 40400u && ble_dg.close_since_start_us >= 40000000u &&
                        ble_dg.close_since_start_us <= 40400000u);
    }
    ble_enable(0);
}

int main(void)
{
    run(1, C_MAC, 0, 0, 1000u, 2100u, "FM-1 engine, a Mac (blell8)");
    run(1, C_MAC, 1, 0, 1000u, 2100u, "FM-1 engine, a Mac, a stale bit mid-connection");
    run(1, C_MAC, 0, 1, 1000u, 2100u, "FM-1 engine, a Mac, packets lost both ways");
    run(1, C_WAITS, 0, 0, 1000u, 2100u, "FM-1 engine, a central that waits");
    run(1, C_MAC, 0, 0, 0xFFFFFFFFu - 10u * 24000000u, 2100u, "FM-1 engine, TIMER4 wraps 10 s in");
    run(1, C_SILENT, 0, 0, 5000u, 2100u, "FM-1 engine, the central silent");
    run(0, C_WAITS, 0, 0, 1000u, 300u, "fact sheet engine (the emulator's model)");
    run(0, C_MAC, 0, 0, 1000u, 300u, "fact sheet engine, a Mac");
    printf("%s\n", fails ? "BLE driver: FAILED" : "BLE driver: all passed");
    return fails != 0;
}
