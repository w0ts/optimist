/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of the WL82 baseband driver (firmware/src/ble/ble_hw_wl82.c) with the whole stack, against a fake engine
 * that behaves as the FM-1 measured (blell3, 9a90c7d, 2026-10-08), not as the fact sheet guessed:
 *   - advertising: the CONNECT_IND lands in the buffer RXTOG then points to, RXTOG moves past it, RXBUFnCNTL stays 0;
 *   - connection: the engine fills RXTOG's buffer, sets its RXBUFnCNTL bit0, moves RXTOG; it sends TXTOG's buffer
 *     while its TXBUFnCNTL bit0 = 1 (both are 1 after conn_start: two empty PDUs) and CLEARS bit0 when done;
 *   - the slot clock (columns 0 / 14) steps back 267 slots now and then, as the FM-1's did.
 * Checks: the CONNECT_IND found by the RXTOG rule; our LL_VERSION_IND and LL_PERIPHERAL_FEATURE_REQ go out; the link
 * lives past 60 s (the 40 s procedure timeout no longer fires from a clock step); no column 0 / 14 read from the
 * ISRs; the 40 s timeout still fires at 40 s when the central never answers; TIMER4 wrapping in the middle; and the
 * fact sheet's TX direction (the engine sets bit0 back to 1) still works (the emulator's model). */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#include "../firmware/src/ble/ble_stack.c"
#include "../firmware/src/ble/ble_diag.c"

static int fails;
static void check(const char *what, int ok)
{
    printf("%-100s %s\n", what, ok ? "ok" : "FAIL");
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

static struct {
    int clears;                         /* 1: the FM-1 (the engine clears TXBUFnCNTL bit0); 0: the fact sheet */
    int answer_feat;                    /* the central answers our PERIPHERAL_FEATURE_REQ */
    uint8_t q[8][32], qlen[8];          /* the central's control PDUs to send (opcode + data) */
    int qn, sn;
    int ver_rx, feat_req_rx, data_rx, empties;
} cen;

static void cen_ctrl(const uint8_t *p, int n)
{
    memcpy(cen.q[cen.qn], p, (size_t)n);
    cen.qlen[cen.qn++] = (uint8_t)n;
}

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
    ble_wl82_rx_irq();
}

/* one connection event: the central's packet, our answer, the event IRQ */
static void event(uint32_t e)
{
    uint32_t b = CB->rxtog & 1u, t;
    uint8_t llid = 1, n = 0;
    uint8_t *p = bb.rx[b].buf + HW_SWHDR;
    if (cen.qn) {
        llid = 3;
        n = cen.qlen[0];
        memcpy(p, cen.q[0], n);
        memmove(cen.q, cen.q + 1, sizeof cen.q[0] * 7);
        memmove(cen.qlen, cen.qlen + 1, 7);
        cen.qn--;
    }
    CB->rxdhdr[b] = (uint16_t)(n << 8 | (uint32_t)cen.sn << 3 | llid);
    CB->rxstat[b] = 0x9401u;
    CB->rxbufcntl[b] |= 1u;
    CB->rxtog ^= 1u;
    cen.sn ^= 1;
    ble_wl82_rx_irq();
    t = CB->txtog & 1u;                                                 /* the engine answers */
    if ((CB->txbufcntl[t] & 1u) == (cen.clears ? 1u : 0u)) {
        uint16_t h = CB->txdhdr[t];
        const uint8_t *q = bb.tx[t].buf + HW_SWHDR;
        if ((h & 3u) == 3u && h >> 8) {
            if (q[0] == LL_VERSION_IND)
                cen.ver_rx++;
            if (q[0] == LL_PERIPHERAL_FEATURE_REQ) {
                static const uint8_t rsp[9] = {LL_FEATURE_RSP, 0x01};
                cen.feat_req_rx++;
                if (cen.answer_feat)
                    cen_ctrl(rsp, 9);
            }
        } else if (h >> 8)
            cen.data_rx++;
        else
            cen.empties++;
        CB->txbufcntl[t] ^= 1u;                                         /* done: cleared (FM-1) or set (sheet) */
        CB->txtog ^= 1u;
    }
    fk.slots += 2u * IV;
    if (e % 97u == 96u)
        fk.slots -= 267u;                                               /* the FM-1's backward step */
    fk.ticks += TICKS_PER_EVT;
    fk.col3 = e + 1u;
    ble_wl82_event_irq();
}

static void run(int clears, int answer, uint32_t ticks0, uint32_t events, const char *name)
{
    static const uint8_t addr[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0xC6};
    static const uint8_t ver[6] = {LL_VERSION_IND, 0x0C, 0x0F, 0x00, 0x00, 0x01};
    struct ble_rf_trims tr;
    uint32_t e, t_conn;
    char what[160];
    memset(&cen, 0, sizeof cen);
    memset(&fk, 0, sizeof fk);
    memset(&tr, 0, sizeof tr);
    ble_diag_clear();
    fk.ticks = ticks0;
    fk.tick_per_read = FM1_TICKS_PER_US;
    cen.clears = clears;
    cen.answer_feat = answer;
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
    for (e = 0; e < events && ble_ll_connected(); e++)
        event(e);
    snprintf(what, sizeof what, "%s: TXBUFnCNTL direction learnt (%u), our VERSION_IND once, our FEATURE_REQ", name,
             (unsigned)ble_dg.tx_pol);
    check(what, ble_dg.tx_pol == (clears ? BTP_CLEARS : BTP_SHEET) && cen.ver_rx == 1 && cen.feat_req_rx == 1 &&
                    ble_dg.tx_queued >= 2 && ble_dg.tx_acked >= 2 && ble_dg.ctl_tx_n >= 2);
    snprintf(what, sizeof what, "%s: no column 0 / 14 (slot clock) read from the ISRs over %u events", name,
             (unsigned)e);
    check(what, fk.col_reads[0] == 0 && fk.col_reads[14] == 0);
    snprintf(what, sizeof what, "%s: RX in a connection: CNTL bit0, RXTOG past it (%u / %u)", name,
             (unsigned)ble_dg.rxc_tog_past, (unsigned)ble_dg.rxc_tog_at);
    check(what, ble_dg.rx_desync == 0 && ble_dg.rxc_tog_past >= e - 1u && ble_dg.rxc_tog_at == 0);
    if (answer) {
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
    run(1, 1, 1000u, 2100u, "FM-1 engine");
    run(1, 1, 0xFFFFFFFFu - 10u * 24000000u, 2100u, "FM-1 engine, TIMER4 wraps 10 s in");
    run(1, 0, 5000u, 2100u, "FM-1 engine, the central silent");
    run(0, 1, 1000u, 300u, "fact sheet engine (the emulator's model)");
    printf("%s\n", fails ? "BLE driver: FAILED" : "BLE driver: all passed");
    return fails != 0;
}
