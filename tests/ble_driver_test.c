/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of the WL82 baseband driver (firmware/src/ble/ble_hw_wl82.c) with the whole stack, against a fake engine
 * that follows the TX contract of docs/BLE-HW-FACTS.md §8.2 and the RX the FM-1 measured (§8.1):
 *   - advertising: the CONNECT_IND lands in the buffer RXTOG then points to, RXTOG moves past it, RXBUFnCNTL stays 0;
 *     advertising leaves TXTOG bit0 = 1;
 *   - connection RX: the engine fills RXTOG's buffer, sets its RXBUFnCNTL bit0, moves RXTOG; RXDHDRn carries the
 *     central's NESN (bit2) and SN (bit3), Core layout; a repeated SN is dropped by the engine;
 *   - connection TX (§8.2): TXBUFnCNTL bit0 = 1 is "empty", 0 "handed to the engine". The engine transmits only a
 *     buffer with bit0 = 0, the TXTOG buffer first (else the other one, TXTOG moving to it); with neither, an empty PDU
 *     of its own. It keeps SN / NESN itself (Core Vol 6 Part B 4.5.9), retransmits its copy of an unacknowledged PDU,
 *     and when the central acknowledges a PDU from a buffer it sets that buffer's bit0 = 1 and moves TXTOG bit0 to the
 *     other buffer. Optionally the FM-1's first-event quirk (§8.1 blell4, §8.2 point 2): at its first transmission
 *     the engine clears bit0 of the TXTOG buffer with nothing loaded and sends set-up's empty PDU from it;
 *   - the Mac: VERSION_IND, FEATURE_REQ two events later, LL_LENGTH_REQ once it has our FEATURE_RSP, then ATT: Exchange
 *     MTU, Read By Group Type, the MIDI CCCD write; it terminates (0x13) 249 events (7.5 s) after a LENGTH_REQ nobody
 *     answered, as it did after blell8's;
 *   - the slot clock (columns 0 / 14) steps back 267 slots now and then, as the FM-1's did.
 * The fake also watches the driver: after set-up it must never write bit0 = 1, never write TXTOG, never change TXDHDR
 * bit2, never touch the header or payload of a buffer the engine holds (MD excepted), and the event interrupt must
 * never load a buffer. Checks: the whole exchange, once each and in order, the link past 60 s, with packet loss both
 * ways, with a central that waits for our PERIPHERAL_FEATURE_REQ, TIMER4 wrapping, the 40 s timeout when the central
 * never answers, and that the previous drivers' polarity (bit0 = 1 written on a loaded buffer, 5008663 .. 51792b7)
 * stalls against this engine as it did on the FM-1. */
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
#define MAC_LEN_WAIT 249u               /* the Mac's patience with its LENGTH_REQ (blell8: 7.47 s) */
#define MIDI_EVT 120u                   /* the app's note */

enum { C_WAITS, C_MAC, C_SILENT };      /* the central: answers our FEATURE_REQ / asks first (blell8) / never answers */
enum { O_QUIRK = 1, O_LOSS = 2, O_OLDPOL = 4 };   /* run options */

static struct {
    int kind, opt;                      /* C_*, O_* */
    uint8_t q[8][32], qlen[8], qllid[8];  /* the central's PDUs to send */
    int qn;
    uint8_t cur[32], cur_len, cur_llid; /* the PDU in flight (until acknowledged) */
    int cur_ok, sn, nesn;
    int ver_rx, feat_req_rx, feat_rsp_rx, len_rsp_rx, len_req_rx, mtu_rsp_rx, group_rsp_rx, write_rsp_rx, notif_rx;
    int data_rx, empties, dups, order_bad;
    uint32_t e, len_req_evt, notif_evt, mtu_req_evt;
    int len_req_out, terminated;
} cen;

static struct {                         /* the fake engine's TX state */
    int tsn, nesn, sent_any;
    int last_b;                         /* the buffer the PDU in flight came from; -1: an empty PDU of the engine's */
    int in_flight;                      /* a PDU sent and not yet acknowledged (retransmitted from eng.last) */
    uint8_t last[40];                   /* that PDU (header 2 + payload) */
    uint32_t sends[2];                  /* data PDUs sent from each buffer (not counting retransmissions) */
    /* the watch on the driver */
    uint8_t cntl[2];                    /* TXBUFnCNTL as the engine last left it */
    uint16_t tog, dhdr[2];
    uint8_t pay[2][32];
    uint32_t sw_set1, sw_tog, sw_bit2, sw_touch, evt_load, old_writes;
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
            cen_pdu(2, wr, 9);
        }
        if (q[4] == 0x13u)
            cen.write_rsp_rx++;
        if (q[4] == 0x1Bu && q[5] == 14u) {
            cen.notif_rx++;
            cen.notif_evt = cen.e;
        }
    }
}

/* the engine's view of the TX registers, for the watch */
static void eng_save(void)
{
    uint32_t b;
    for (b = 0; b < 2u; b++) {
        eng.cntl[b] = CB->txbufcntl[b];
        eng.dhdr[b] = CB->txdhdr[b];
        memcpy(eng.pay[b], bb.tx[b].buf + HW_SWHDR, sizeof eng.pay[b]);
    }
    eng.tog = CB->txtog;
}

/* what the driver did since eng_save (evt: in the event interrupt) */
static void eng_watch(int evt)
{
    uint32_t b;
    if (drv.state != HW_CONN)
        return;                                                         /* (closed: advertising rewrites the buffers) */
    if (CB->txtog != eng.tog)
        eng.sw_tog++;
    for (b = 0; b < 2u; b++) {
        if ((CB->txbufcntl[b] & 1u) && !(eng.cntl[b] & 1u))
            eng.sw_set1++;
        if ((CB->txdhdr[b] ^ eng.dhdr[b]) & 4u)
            eng.sw_bit2++;
        if (!(eng.cntl[b] & 1u) && (((CB->txdhdr[b] ^ eng.dhdr[b]) & ~8u) ||
                                    memcmp(eng.pay[b], bb.tx[b].buf + HW_SWHDR, sizeof eng.pay[b])))
            eng.sw_touch++;
        if (evt && (eng.cntl[b] & 1u) && !(CB->txbufcntl[b] & 1u))
            eng.evt_load++;
    }
}

/* the previous drivers (5008663 .. 51792b7): a loaded buffer marked with bit0 = 1 ("loaded" in their polarity) */
static void old_polarity(void)
{
    uint32_t b;
    for (b = 0; b < 2u; b++)
        if (drv.tx_rec[b] && !(CB->txbufcntl[b] & 1u)) {
            CB->txbufcntl[b] |= 1u;
            eng.old_writes++;
        }
}

/* the engine's packet T_IFS after the central's: its copy again while unacknowledged, else a buffer with bit0 = 0
 * (TXTOG's first), else an empty PDU of its own */
static void eng_send(void)
{
    uint32_t t = CB->txtog & 1u, b;
    if (eng.in_flight)
        return;                                                         /* (eng.last is resent) */
    if ((cen.opt & O_QUIRK) && !eng.sent_any && (CB->txbufcntl[t] & 1u))
        CB->txbufcntl[t] &= (uint8_t)~1u;                               /* §8.1: set-up's empty PDU, bit0 cleared */
    b = !(CB->txbufcntl[t] & 1u) ? t : !(CB->txbufcntl[t ^ 1u] & 1u) ? t ^ 1u : 2u;
    if (b < 2u) {
        if (b != t)
            CB->txtog = (uint16_t)((CB->txtog & ~1u) | b);
        eng.last[0] = (uint8_t)((CB->txdhdr[b] & 3u) | (CB->txdhdr[b] & 8u) << 1);
        eng.last[1] = (uint8_t)(CB->txdhdr[b] >> 8);
        memcpy(eng.last + 2, bb.tx[b].buf + HW_SWHDR, eng.last[1] < 38u ? eng.last[1] : 38u);
        eng.last_b = (int)b;
        eng.sends[b] += eng.last[1] != 0;               /* (data PDUs only) */
    } else {
        eng.last[0] = 1, eng.last[1] = 0;
        eng.last_b = -1;
    }
    eng.in_flight = 1;
    if (!eng.sent_any)
        CB->txtog |= 6u;                                                /* 7 after the first packet (blell4) */
    eng.sent_any = 1;
}

/* one connection event: the central's packet (its NESN acknowledges our last, its SN is new or a repeat), the
 * engine's answer, the central reads it */
static void eng_event(uint32_t rb, int c_lost, int p_lost)
{
    uint8_t llid = cen.cur_ok ? cen.cur_llid : 1u, n = cen.cur_ok ? cen.cur_len : 0u;
    uint16_t h;
    const uint8_t *q;
    if (c_lost)
        return;                                                         /* the engine heard nothing: no answer */
    if (eng.in_flight && cen.nesn != eng.tsn) {                         /* our last acknowledged */
        if (eng.last_b >= 0) {
            CB->txbufcntl[eng.last_b] |= 1u;                            /* finished: empty again */
            CB->txtog = (uint16_t)((CB->txtog & ~1u) | (uint32_t)(eng.last_b ^ 1));
        }
        eng.tsn ^= 1;
        eng.in_flight = 0;
    }
    if (cen.sn == eng.nesn) {                                           /* new: into RXTOG's buffer */
        uint8_t *p = bb.rx[rb].buf + HW_SWHDR;
        eng.nesn ^= 1;
        memcpy(p, cen.cur, n);
        CB->rxdhdr[rb] = (uint16_t)(n << 8 | (uint32_t)cen.sn << 3 | (uint32_t)cen.nesn << 2 | llid);
        CB->rxstat[rb] = 0x9401u;
        CB->rxbufcntl[rb] |= 1u;
        CB->rxtog ^= 1u;
    }                                                                   /* (a repeat: dropped by the engine) */
    eng_send();
    if (p_lost)
        return;
    h = (uint16_t)(eng.last[1] << 8 | (uint32_t)eng.tsn << 3 | (uint32_t)eng.nesn << 2 | (eng.last[0] & 0x13u));
    q = eng.last + 2;
    if ((h >> 3 & 1u) == (uint32_t)cen.nesn) {                          /* the central: new from us */
        cen.nesn ^= 1;
        cen_rx(h, q);
    } else if (eng.last[1])
        cen.dups++;                                                     /* (a retransmission: the central drops it) */
    if ((h >> 2 & 1u) != (uint32_t)cen.sn) {                            /* ours acknowledged its packet */
        cen.sn ^= 1;
        cen.cur_ok = 0;
    }
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
    int loss = (cen.opt & O_LOSS) != 0;
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
    eng_event(b, loss && e % 7u == 3u, loss && e % 11u == 5u);
    eng_save();
    ble_wl82_rx_irq();
    eng_watch(0);
    if (cen.opt & O_OLDPOL)
        old_polarity();
    fk.slots += 2u * IV;
    if (e % 97u == 96u)
        fk.slots -= 267u;                                               /* the FM-1's backward step */
    fk.ticks += TICKS_PER_EVT;
    fk.col3 = e + 1u;
    eng_save();
    ble_wl82_event_irq();
    eng_watch(1);
}

static void run(int kind, int opt, uint32_t ticks0, uint32_t events, const char *name)
{
    static const uint8_t addr[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0xC6};
    static const uint8_t ver[6] = {LL_VERSION_IND, 0x0C, 0x0F, 0x00, 0x00, 0x01};
    static const uint8_t feat[9] = {LL_FEATURE_REQ, 0x3F};              /* the Mac's: DLE among them */
    struct ble_rf_trims tr;
    uint32_t e, t_conn;
    int loss = (opt & O_LOSS) != 0;
    char what[220];
    memset(&cen, 0, sizeof cen);
    memset(&eng, 0, sizeof eng);
    memset(&app, 0, sizeof app);
    memset(&fk, 0, sizeof fk);
    memset(&tr, 0, sizeof tr);
    ble_diag_clear();
    fk.ticks = ticks0;
    fk.tick_per_read = FM1_TICKS_PER_US;
    cen.kind = kind;
    cen.opt = opt;
    ble_hw_wl82_start(&tr);
    ble_init(addr, 1);
    ble_enable(1);
    fk.ticks += 50000u * FM1_TICKS_PER_US;
    cind();
    snprintf(what, sizeof what, "%s: CONNECT_IND taken, found in the buffer RXTOG moved past (CNTL 0)", name);
    check(what, ble_ll_connected() && ble_dg.cind_ok == 1 && ble_dg.rxf_tog_prev == 1 && ble_dg.rxf_cntl == 0);
    snprintf(what, sizeof what, "%s: set-up: both TXBUFnCNTL bit0 = 1, TXDHDR %04X / %04X (TXTOG's bit2 0), none "
             "recorded", name, CB->txdhdr[0], CB->txdhdr[1]);
    check(what, (CB->txbufcntl[0] & 1u) && (CB->txbufcntl[1] & 1u) && CB->txdhdr[1] == 0x0001u && CB->txdhdr[0] == 0x0005u &&
                    !CB->txahdr[0] && !CB->txahdr[1] && !drv.tx_rec[0] && !drv.tx_rec[1]);
    t_conn = fk.ticks;
    memset(fk.col_reads, 0, sizeof fk.col_reads);
    cen_ctrl(ver, 6);
    if (kind == C_MAC) {                                                /* blell8: FEATURE_REQ two events later */
        cen_pdu(1, feat, 0);
        cen_ctrl(feat, 9);
    }
    for (e = 0; e < events && ble_ll_connected(); e++)
        event(e);
    if (opt & O_OLDPOL) {
        snprintf(what, sizeof what, "%s: stalls: %u bit0 = 1 writes on loaded buffers, VERSION_IND %d, FEATURE_RSP %d, "
                 "data PDUs sent %u", name, (unsigned)eng.old_writes, cen.ver_rx, cen.feat_rsp_rx,
                 (unsigned)(eng.sends[0] + eng.sends[1]));
        check(what, eng.old_writes > 0 && cen.ver_rx == 0 && cen.feat_rsp_rx == 0 && cen.len_rsp_rx == 0 &&
                        eng.sends[0] + eng.sends[1] == 0);
        ble_enable(0);
        return;
    }
    snprintf(what, sizeof what, "%s: our VERSION_IND once; queued %u, acked %u, ack within %u events", name,
             (unsigned)ble_dg.tx_queued, (unsigned)ble_dg.tx_acked, (unsigned)ble_dg.tx_ack_evt_max);
    check(what, cen.ver_rx == 1 && ble_dg.tx_acked + 1u >= ble_dg.tx_queued && ble_dg.tx_acked <= ble_dg.tx_queued &&
                    ble_dg.tx_ack_evt_max <= (loss ? 8u : 4u));
    snprintf(what, sizeof what, "%s: the driver kept the contract (bit0=1 %u, TXTOG %u, bit2 %u, held touched %u, "
             "event IRQ loads %u)", name, (unsigned)eng.sw_set1, (unsigned)eng.sw_tog, (unsigned)eng.sw_bit2,
             (unsigned)eng.sw_touch, (unsigned)eng.evt_load);
    check(what, !eng.sw_set1 && !eng.sw_tog && !eng.sw_bit2 && !eng.sw_touch && !eng.evt_load);
    snprintf(what, sizeof what, "%s: txsnaps: first a load (b %u, snap %u), the TXTOG / bit0 values kept; held %u",
             name, ble_dg.txs_first.b, ble_dg.txs_first.snap, (unsigned)ble_dg.tx_eng_held);
    check(what, ble_dg.txs_n >= 2u && ble_dg.txs_first.what == BTX_LOAD &&
                    ((opt & O_QUIRK) ? ble_dg.tx_eng_held >= 1u : ble_dg.tx_eng_held == 0u));
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
        snprintf(what, sizeof what, "%s: the LENGTH_RSP within %u events of the LENGTH_REQ", name,
                 (unsigned)(cen.mtu_req_evt - cen.len_req_evt));
        check(what, cen.mtu_req_evt >= cen.len_req_evt && cen.mtu_req_evt - cen.len_req_evt <= (loss ? 8u : 4u));
    } else if (kind == C_WAITS) {
        snprintf(what, sizeof what, "%s: our PERIPHERAL_FEATURE_REQ out (%d)", name, cen.feat_req_rx);
        check(what, cen.feat_req_rx == 1 && ble_dg.tx_queued >= 2 && ble_dg.tx_acked >= 2 && ble_dg.ctl_tx_n >= 2);
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
        check(what, ble_ll_connected() && ble_dg.closes == 0 && e == events && !cen.terminated &&
                        (fk.ticks - t_conn) / 24000000u >= 60u);
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
    run(C_MAC, O_QUIRK, 1000u, 2100u, "§8.2 engine (FM-1 first-event quirk), a Mac");
    run(C_MAC, 0, 1000u, 2100u, "§8.2 engine (the emulator's model), a Mac");
    run(C_MAC, O_QUIRK | O_LOSS, 1000u, 2100u, "§8.2 engine, a Mac, packets lost both ways");
    run(C_WAITS, O_QUIRK, 1000u, 2100u, "§8.2 engine, a central that waits");
    run(C_MAC, O_QUIRK, 0xFFFFFFFFu - 10u * 24000000u, 2100u, "§8.2 engine, TIMER4 wraps 10 s in");
    run(C_SILENT, O_QUIRK, 5000u, 2100u, "§8.2 engine, the central silent");
    run(C_MAC, O_QUIRK | O_OLDPOL, 1000u, 300u, "§8.2 engine, the previous drivers' polarity (bit0 = 1 = loaded)");
    printf("%s\n", fails ? "BLE driver: FAILED" : "BLE driver: all passed");
    return fails != 0;
}
