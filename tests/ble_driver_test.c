/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of the WL82 baseband driver (firmware/src/ble/ble_hw_wl82.c) with the whole stack, against a fake engine
 * that behaves as the FM-1 measured (blell3 9a90c7d, blell4 5008663, blell6 0475aa5), not as the fact sheet guessed:
 *   - advertising: the CONNECT_IND lands in the buffer RXTOG then points to, RXTOG moves past it, RXBUFnCNTL stays 0;
 *     advertising leaves TXTOG bit0 = 1;
 *   - connection RX: the engine fills RXTOG's buffer, sets its RXBUFnCNTL bit0, moves RXTOG;
 *   - connection TX (blell4, blell6): the engine sends only from the buffer set-up's empty PDU left (TXTOG bit0 at the
 *     first anchor, buffer 1) while its TXBUFnCNTL bit0 = 1, and CLEARS bit0 of that buffer only, as it sends it;
 *     buffer 0 is never sent, bit0 = 1 or not, though TXTOG reads 7 (first packet), 5 (first data PDU), 2 (bit0 = 0)
 *     the event after, E three events later. conn_start writes bit0 = 1 on both buffers; the event after the first
 *     data PDU the engine sets buffer 1's bit0 again (blell6 txsnap 4) and sits on it (empty PDUs of its own) like on a
 *     stale bit, which stops being one when software clears its bit or writes a new header into it. With the Mac's
 *     FEATURE_REQ two events after its VERSION_IND (blell6), 0475aa5's driver puts the FEATURE_RSP into buffer 0 and
 *     it never leaves: tx_queued 3, tx_acked 1, tx_force_free 1, as on the FM-1;
 *   - the slot clock (columns 0 / 14) steps back 267 slots now and then, as the FM-1's did.
 * Checks: the CONNECT_IND found by the RXTOG rule; with a Mac-like central (VERSION_IND then FEATURE_REQ, DLE) our
 * VERSION_IND, FEATURE_RSP, LENGTH_REQ and the ATT responses (Exchange MTU, Read By Group Type) all go out and the link
 * lives past 60 s; with a central that waits, our PERIPHERAL_FEATURE_REQ goes out; a stale bit appearing mid-
 * connection is forced free (tx_force_free); a PDU in the buffer the engine does not send is moved (tx_moved) within
 * a few events; no column 0 / 14 read from the ISRs; the 40 s timeout still fires when
 * the central never answers; TIMER4 wrapping in the middle; and the fact sheet's TX direction (the engine sets bit0
 * back to 1) still works (the emulator's model). */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#include "../firmware/src/ble/ble_stack.c"
#include "../firmware/src/ble/ble_diag.c"

static int fails;
static void check(const char *what, int ok)
{
    printf("%-110s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

static int ble_app_midi_peek(uint32_t *pkt, uint32_t *t)
{
    (void)pkt;
    (void)t;
    return 0;
}
static void ble_app_midi_pop(void) {}
static void ble_app_midi_in(uint32_t pkt, uint16_t ts, uint16_t last)
{
    (void)pkt;
    (void)ts;
    (void)last;
}
static void ble_app_state(void) {}

#define IV 24u                          /* the Mac's CONNECT_IND: 30 ms, timeout 72 (720 ms), hop 13, sca 1 */
#define TICKS_PER_EVT (IV * 1250u * FM1_TICKS_PER_US)
#define RESTALE_EVT 30u                 /* the "stale" scenario: a stale bit on TXTOG's buffer here */

enum { C_WAITS, C_MAC, C_SILENT };      /* the central: answers our FEATURE_REQ / asks first (blell4) / never answers */

static struct {
    int clears;                         /* 1: the FM-1 (the engine clears TXBUFnCNTL bit0); 0: the fact sheet */
    int kind;                           /* C_* */
    int restale;                        /* a stale "loaded" bit on TXTOG's free buffer at RESTALE_EVT */
    uint8_t q[8][32], qlen[8], qllid[8];  /* the central's PDUs to send */
    int qn, sn;
    int ver_rx, feat_req_rx, feat_rsp_rx, len_req_rx, mtu_rsp_rx, group_rsp_rx, data_rx, empties;
    uint32_t e, group_evt;              /* the event now; the one the Read By Group Type response went out in */
} cen;

static struct {                         /* the fake engine's TX state (FM-1) */
    int stale[2], sent_any, s, after_data, rearms;
    uint16_t stale_hdr[2];
    uint32_t stale_events;              /* events the engine sat on a stale buffer */
    uint32_t never_sent_events;         /* events the other buffer sat loaded (never taken: blell6) */
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

static void eng_stale(uint32_t b)
{
    eng.stale[b] = 1;
    eng.stale_hdr[b] = CB->txdhdr[b];
}

/* the central reads the PDU in TX buffer t */
static void cen_rx(uint32_t t)
{
    uint16_t h = CB->txdhdr[t];
    const uint8_t *q = bb.tx[t].buf + HW_SWHDR;
    if (!(h >> 8)) {
        cen.empties++;
        return;
    }
    if ((h & 3u) == 3u) {
        if (q[0] == LL_VERSION_IND)
            cen.ver_rx++;
        if (q[0] == LL_FEATURE_RSP)
            cen.feat_rsp_rx++;
        if (q[0] == LL_LENGTH_REQ) {
            static const uint8_t rsp[9] = {LL_LENGTH_RSP, 251, 0, 0x48, 0x08, 251, 0, 0x48, 0x08};
            cen.len_req_rx++;
            cen_ctrl(rsp, 9);
        }
        if (q[0] == LL_PERIPHERAL_FEATURE_REQ) {
            static const uint8_t rsp[9] = {LL_FEATURE_RSP, 0x01};
            cen.feat_req_rx++;
            if (cen.kind == C_WAITS)
                cen_ctrl(rsp, 9);
        }
        return;
    }
    cen.data_rx++;
    if ((h & 3u) == 2u && q[2] == 4u && q[3] == 0u) {                  /* L2CAP, the ATT channel */
        if (q[4] == 0x03u)
            cen.mtu_rsp_rx++;
        if (q[4] == 0x11u) {
            cen.group_rsp_rx++;
            cen.group_evt = cen.e;
        }
    }
}

/* the FM-1 engine's TX in one event (blell6, 0475aa5): it sends only from the buffer it sent the set-up empty PDU
 * from (TXTOG bit0 at the first anchor, buffer 1), clears that buffer's TXBUFnCNTL bit0 as it sends it, and never sends
 * buffer 0, whatever TXTOG reads: TXTOG goes 7 (first packet), 5 (first data PDU sent), 2 (the event after: bit0 = 0),
 * E three events later, and the FEATURE_RSP loaded into buffer 0 with bit0 = 1 never left. The event after the first
 * data PDU the engine sets bit0 of the buffer it sent from back to 1 (blell6 txsnap 4: TXBUF1CNTL 01 that no software
 * wrote) and then sits on it as on a stale bit (sends empty PDUs) until software frees it or writes a new header:
 * 0475aa5 force-freed it at evt 4 (txsnap 6). */
static void eng_fm1_tx(void)
{
    uint32_t s;
    if (!eng.sent_any)
        eng.s = (int)(CB->txtog & 1u);
    s = (uint32_t)eng.s;
    if (eng.after_data) {                                              /* TXTOG's measured sequence */
        eng.after_data++;
        if (eng.after_data == 2)
            CB->txtog = 2u;
        else if (eng.after_data == 5)
            CB->txtog = 0xEu;
    }
    if (eng.after_data == 2 && !(CB->txbufcntl[s] & 1u)) {
        CB->txbufcntl[s] |= 1u;                                         /* the unexplained re-arm (txsnap 4), then */
        eng_stale(s);                                                   /* held: blell6 force-freed it at evt 4 */
        eng.rearms++;
    }
    if (CB->txbufcntl[s ^ 1u] & 1u)
        eng.never_sent_events++;                                        /* buffer 0 loaded, never taken */
    if (eng.stale[s] && (!(CB->txbufcntl[s] & 1u) || CB->txdhdr[s] != eng.stale_hdr[s]))
        eng.stale[s] = 0;                                               /* software cleared it or loaded a PDU */
    if (eng.stale[s]) {
        eng.stale_events++;
        cen.empties++;
        return;
    }
    if (!(CB->txbufcntl[s] & 1u)) {
        cen.empties++;                                                  /* nothing loaded: an empty PDU of its own */
        return;
    }
    cen_rx(s);
    CB->txbufcntl[s] &= (uint8_t)~1u;                                   /* cleared: only the buffer it sent */
    if (!eng.sent_any)
        CB->txtog |= 6u;                                                /* 7 after the first packet */
    eng.sent_any = 1;
    if (CB->txdhdr[s] >> 8 && !eng.after_data) {
        CB->txtog &= (uint16_t)~2u;                                     /* 5 after the first data PDU */
        eng.after_data = 1;
    }
}

/* the fact sheet's engine (the emulator's model): bit0 = 0 loaded, set back to 1 when done, TXTOG moves */
static void eng_sheet_tx(void)
{
    uint32_t t = CB->txtog & 1u;
    if (CB->txbufcntl[t] & 1u)
        return;
    cen_rx(t);
    CB->txbufcntl[t] |= 1u;
    CB->txtog ^= 1u;
}

/* one connection event: the central's packet, the engine's answer, the RX IRQ, the event IRQ */
static void event(uint32_t e)
{
    uint32_t b = CB->rxtog & 1u;
    uint8_t llid = 1, n = 0;
    uint8_t *p = bb.rx[b].buf + HW_SWHDR;
    cen.e = e;
    if (cen.kind == C_MAC && e == 20u) {
        static const uint8_t mtu[7] = {3, 0, 4, 0, 0x02, 0x05, 0x01};   /* Exchange MTU, 517 */
        cen_pdu(2, mtu, 7);
    }
    if (cen.kind == C_MAC && e == 40u) {
        static const uint8_t grp[11] = {7, 0, 4, 0, 0x10, 0x01, 0x00, 0xFF, 0xFF, 0x00, 0x28};
        cen_pdu(2, grp, 11);                                            /* Read By Group Type, primary services */
    }
    if (cen.restale && e == RESTALE_EVT) {
        uint32_t t = (uint32_t)eng.s;
        if (!(CB->txbufcntl[t] & 1u) && drv.tx_n == 0) {                /* an unexplained stale bit on the free one */
            CB->txdhdr[t] = (uint16_t)((CB->txdhdr[t] & 4u) | 1u);
            CB->txbufcntl[t] |= 1u;
            eng_stale(t);
        }
    }
    if (cen.qn) {
        llid = cen.qllid[0];
        n = cen.qlen[0];
        memcpy(p, cen.q[0], n);
        memmove(cen.q, cen.q + 1, sizeof cen.q[0] * 7);
        memmove(cen.qlen, cen.qlen + 1, 7);
        memmove(cen.qllid, cen.qllid + 1, 7);
        cen.qn--;
    }
    CB->rxdhdr[b] = (uint16_t)(n << 8 | (uint32_t)cen.sn << 3 | llid);
    CB->rxstat[b] = 0x9401u;
    CB->rxbufcntl[b] |= 1u;
    CB->rxtog ^= 1u;
    cen.sn ^= 1;
    if (cen.clears)
        eng_fm1_tx();                                                   /* the answer goes before the RX IRQ */
    ble_wl82_rx_irq();
    if (!cen.clears)
        eng_sheet_tx();
    fk.slots += 2u * IV;
    if (e % 97u == 96u)
        fk.slots -= 267u;                                               /* the FM-1's backward step */
    fk.ticks += TICKS_PER_EVT;
    fk.col3 = e + 1u;
    ble_wl82_event_irq();
}

static void run(int clears, int kind, int restale, uint32_t ticks0, uint32_t events, const char *name)
{
    static const uint8_t addr[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0xC6};
    static const uint8_t ver[6] = {LL_VERSION_IND, 0x0C, 0x0F, 0x00, 0x00, 0x01};
    static const uint8_t feat[9] = {LL_FEATURE_REQ, 0x3F};              /* the Mac's: DLE among them */
    struct ble_rf_trims tr;
    uint32_t e, t_conn;
    char what[200];
    memset(&cen, 0, sizeof cen);
    memset(&eng, 0, sizeof eng);
    memset(&fk, 0, sizeof fk);
    memset(&tr, 0, sizeof tr);
    ble_diag_clear();
    fk.ticks = ticks0;
    fk.tick_per_read = FM1_TICKS_PER_US;
    cen.clears = clears;
    cen.kind = kind;
    cen.restale = restale;
    ble_hw_wl82_start(&tr);
    ble_init(addr, 1);
    ble_enable(1);
    fk.ticks += 50000u * FM1_TICKS_PER_US;
    cind();
    snprintf(what, sizeof what, "%s: CONNECT_IND taken, found in the buffer RXTOG moved past (CNTL 0)", name);
    check(what, ble_ll_connected() && ble_dg.cind_ok == 1 && ble_dg.rxf_tog_prev == 1 && ble_dg.rxf_cntl == 0);
    if (clears)
        eng_stale((CB->txtog & 1u) ^ 1u);                               /* conn_start's other empty PDU */
    t_conn = fk.ticks;
    memset(fk.col_reads, 0, sizeof fk.col_reads);
    cen_ctrl(ver, 6);
    if (kind == C_MAC) {                                                /* blell6: FEATURE_REQ two events later */
        cen_pdu(1, feat, 0);
        cen_ctrl(feat, 9);
    }
    for (e = 0; e < events && ble_ll_connected(); e++)
        event(e);
    snprintf(what, sizeof what, "%s: TXBUFnCNTL direction learnt (%u), our VERSION_IND once", name,
             (unsigned)ble_dg.tx_pol);
    check(what, ble_dg.tx_pol == (clears ? BTP_CLEARS : BTP_SHEET) && cen.ver_rx == 1);
    if (kind == C_MAC) {
        snprintf(what, sizeof what, "%s: FEATURE_RSP %d, our LENGTH_REQ %d, ATT MTU / group responses %d / %d", name,
                 cen.feat_rsp_rx, cen.len_req_rx, cen.mtu_rsp_rx, cen.group_rsp_rx);
        check(what, cen.feat_rsp_rx == 1 && cen.len_req_rx == 1 && cen.mtu_rsp_rx == 1 && cen.group_rsp_rx == 1 &&
                        ble_dg.tx_queued >= 5 && ble_dg.tx_acked >= 5);
    } else {
        snprintf(what, sizeof what, "%s: our PERIPHERAL_FEATURE_REQ out (%d)", name, cen.feat_req_rx);
        check(what, cen.feat_req_rx == 1 && ble_dg.tx_queued >= 2 && ble_dg.tx_acked >= 2 && ble_dg.ctl_tx_n >= 2);
    }
    if (clears) {
        uint32_t k = restale ? 2u : 1u;                                 /* the re-armed bit (blell6), the injected one */
        snprintf(what, sizeof what, "%s: conn_start's stale TX bit freed (stale %u), re-armed ones forced free (%u)",
                 name, (unsigned)ble_dg.tx_stale_clr, (unsigned)ble_dg.tx_force_free);
        check(what, ble_dg.tx_stale_clr == 1 && ble_dg.tx_force_free == k && eng.stale_events > 0);
        snprintf(what, sizeof what, "%s: PDUs moved off the buffer the engine never sends (%u), waited there %u events",
                 name, (unsigned)ble_dg.tx_moved, (unsigned)eng.never_sent_events);
        check(what, ble_dg.tx_moved == k && eng.never_sent_events <= 6u * k);
        if (kind == C_MAC) {                                            /* asked in event 40 */
            snprintf(what, sizeof what, "%s: the Read By Group Type response out in event %u (asked in 40)", name,
                     (unsigned)cen.group_evt);
            check(what, cen.group_evt >= 41u && cen.group_evt <= (restale ? 45u : 42u));
        }
    }
    snprintf(what, sizeof what, "%s: no column 0 / 14 (slot clock) read from the ISRs over %u events", name,
             (unsigned)e);
    check(what, fk.col_reads[0] == 0 && fk.col_reads[14] == 0);
    snprintf(what, sizeof what, "%s: RX in a connection: CNTL bit0, RXTOG past it (%u / %u)", name,
             (unsigned)ble_dg.rxc_tog_past, (unsigned)ble_dg.rxc_tog_at);
    check(what, ble_dg.rx_desync == 0 && ble_dg.rxc_tog_past >= e - 1u && ble_dg.rxc_tog_at == 0);
    if (kind != C_SILENT) {
        snprintf(what, sizeof what, "%s: the link still up after %u s (procedure timeout 40 s, supervision 720 ms)",
                 name, (unsigned)((fk.ticks - t_conn) / 24000000u));
        check(what, ble_ll_connected() && ble_dg.closes == 0 && e == events);
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
    run(1, C_MAC, 0, 1000u, 2100u, "FM-1 engine, a Mac (blell6)");
    run(1, C_MAC, 1, 1000u, 2100u, "FM-1 engine, a Mac, a stale bit mid-connection");
    run(1, C_WAITS, 0, 1000u, 2100u, "FM-1 engine, a central that waits");
    run(1, C_MAC, 0, 0xFFFFFFFFu - 10u * 24000000u, 2100u, "FM-1 engine, TIMER4 wraps 10 s in");
    run(1, C_SILENT, 0, 5000u, 2100u, "FM-1 engine, the central silent");
    run(0, C_WAITS, 0, 1000u, 300u, "fact sheet engine (the emulator's model)");
    run(0, C_MAC, 0, 1000u, 300u, "fact sheet engine, a Mac");
    printf("%s\n", fails ? "BLE driver: FAILED" : "BLE driver: all passed");
    return fails != 0;
}
