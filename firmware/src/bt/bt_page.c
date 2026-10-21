/* SPDX-License-Identifier: GPL-3.0-only */
/* EXPERIMENTAL Bluetooth Classic probe, stage 2 (BT_AUDIO): `btpage ADDR [SECONDS] [OPTIONS]` pages a device as the
 * master (docs/BT-CLASSIC.md §8.3: the ID train with the device's access code, its ID answer, our FHS, its
 * second ID, our POLL), keeps the link up with the engine's POLL / NULL and runs the minimal LMP of bt_lmp.c: our
 * features, version and host connection requests, the answers to what the device asks, setup complete. Every step is
 * printed as a `btpage:` line. Included by bt_inq.c (its frame, rings, options and printing). No vendor code. */
#include "bt_lmp.c"

#define BP_OFF_FHS 4252u                           /* our FHS for the page (18 B) */
#define BP_OFF_TX0 4272u                           /* the two LMP TX payloads (17 B each) */
#define BP_OFF_TX1 4292u
#define BP_OFF_TXB0 4480u                          /* the two DH5 TX payloads (339 B each, after the table: BTI_TAB_ALLOC) */
#define BP_OFF_TXB1 (4480u + 344u)
#define BP_TXB 339u                                /* a DH5 payload (BR §5.5) */
#define BP_MQ 16u                                  /* the audio packets queued (~140 ms of the stream at 3 SBC frames a
                                                    * packet; 4 = 23 ms overflowed on every radio hiccup: audio_drop
                                                    * 11 %, heard as crackles, 2026-10-21) */
#define BP_OFF_RX0 1088u                           /* the connection's two RX buffers: the table's link_rx_bulk (BR §3) */
#define BP_OFF_RX1 (1088u + 680u)
#define BP_LT_ADDR 1u
#define BP_TXQ 32u
#define BP_COD 0x28041Cu                           /* our class of device (the FHS): services Capturing (bit 19, an audio
                                                    * source, as BlueZ sets it) and Audio (21); major Audio/Video, minor
                                                    * Portable Audio. Before: 0x240404, Rendering + a wearable headset:
                                                    * the FM-1 told the AirPods it was a headset (2026-10-20) */
#define BP_SUPERVISION_MS 4000u                    /* no good packet for this long: the link is called lost */

static struct {
    uint8_t remote[6], rbch[5], obch[5];
    uint8_t sub, have_est, setup_out, setup_in, stopping;
    int32_t est;                                   /* the device's clock offset estimate, slots */
    uint32_t page_ids, fhs_acked, conn_ms, last_good_ms, good, bad, nobuf, acl, lmp_in, lmp_out;
    uint32_t type_n[16];
    uint8_t txq[BP_TXQ][BTL_MAX], txl[BP_TXQ], txllid[BP_TXQ];
    uint32_t txw, txr, stop_ms;
    uint8_t slots5;                                /* the multi-slot request: 0 not asked, 1 asked for 5, 2 granted 5, 3
                                                    * asked for 3 (5 refused), 4 granted 3, 5 refused both */
    uint32_t mq_drop, mq_sent;                     /* audio packets dropped (the queue full), sent */
    uint32_t mq_hi, mq_busy;                       /* the queue's deepest this second; audio waiting with both TX
                                                    * buffers still held by the engine (its retransmissions) */
    uint8_t name_done;                             /* the device's name read whole (or refused): the pairing may start */
    uint16_t st_v[6];                              /* this second's rxstat values (TYPE masked off) and their counts: */
    uint32_t st_n[6], st_more, flow0;              /* which of the unknown bits (§5.4, U6) move while the stream backs
                                                    * up (the XM4 crackles, 2026-10-21); flow0: ACL payloads FLOW 0 */
} bp;
/* the audio packets (bt_acl.c ba_media_send, from the audio interrupt): whole L2CAP frames, each one DH5 (or DH3 / DH1
 * when shorter), never cut; the main loop's bp_tx_service sends them after the LMP and L2CAP signalling */
static struct {
    uint8_t d[BP_MQ][BP_TXB];
    uint16_t n[BP_MQ];
    volatile uint32_t w, r;
} bp_mq IN_POOL;
static int bp_media_queue(const uint8_t *frame, uint32_t len)   /* 1 queued; 0 dropped whole (full, too long, no 5 slots) */
{
    uint32_t i, k;
    if ((bp.slots5 != 2u && bp.slots5 != 4u) || len > (bp.slots5 == 2u ? BP_TXB : 183u) || bp_mq.w - bp_mq.r >= BP_MQ) {
        bp.mq_drop++;
        return 0;
    }
    k = bp_mq.w % BP_MQ;
    for (i = 0; i < len; i++)
        bp_mq.d[k][i] = frame[i];
    bp_mq.n[k] = (uint16_t)len;
    bp_mq.w++;
    if (bp_mq.w - bp_mq.r > bp.mq_hi)
        bp.mq_hi = bp_mq.w - bp_mq.r;
    return 1;
}
#if FELUCCA_BT_AUDIO
/* pairing and encryption on the link (bt_ssp.c, SSP Just Works, the device starts it): a fresh state each connection, its
 * PDUs through bp_queue; the randomness for the key and the nonces from the link's own noise (bp_noise: each reception's
 * status, clock and timing) and the clocks, through SHA-256: no hardware random source is known on the FM-1, and Just
 * Works has no protection against an active attacker anyway */
#include "bt_sha256.c"
#include "bt_safer.c"                                /* (SAFER+, E1 / E3, the SSP f1-f3 and P-192 through micro-ecc) */
#include "bt_ssp.c"
static struct btp bp_ssp;
static uint32_t bp_noise, bp_rng_n;
static int bp_rng(uint8_t *d, unsigned n)
{
    while (n) {
        struct bt_sha256 c;
        uint8_t h[32];
        uint32_t w[4] = {bp_noise, bti_clkn(), fm1_ms, bp_rng_n++}, k;
        bt_sha256_init(&c);
        bt_sha256_add(&c, (const uint8_t *)w, sizeof w);
        bt_sha256_done(&c, h);
        for (k = 0; k < 32u && n; k++, n--)
            *d++ = h[k];
        bp_noise = bp_noise * 31u + h[0];
    }
    return 1;
}
static void bp_queue(const uint8_t *pdu, uint32_t len, const char *why);
static void bp_ssp_send(void *ctx, const uint8_t *pdu, uint32_t len)
{
    (void)ctx;
    bp_queue(pdu, len, "pairing");
}
#endif

static const char *const bp_type_name[16] = {"NULL", "POLL", "FHS", "DM1", "DH1", "HV1", "HV2", "HV3",
                                             "DV", "AUX1", "DM3", "DH3", "EV4", "EV5", "DM5", "DH5"};

static void bp_clkoff(struct fm1_br_frame *f, int32_t v)   /* BR §4: 28 bits, sign at 27 */
{
    uint32_t u = v < 0 ? ((uint32_t)v | 0xF8000000u) : ((uint32_t)v & 0x07FFFFFFu);
    f->clk_off_lo = (uint16_t)u;
    f->clk_off_hi = (uint16_t)(u >> 16);
}

static void bp_addr_set(struct fm1_br_frame *f, const uint8_t a[6], const uint8_t bch[5])
{
    f->bdaddr[0] = (uint16_t)(a[0] | a[1] << 8);
    f->bdaddr[1] = (uint16_t)(a[2] | a[3] << 8);
    f->bdaddr[2] = (uint16_t)(a[4] | a[5] << 8);
    f->bch_lo = (uint16_t)(bch[0] | bch[1] << 8);
    f->bch_mid = (uint16_t)(bch[2] | bch[3] << 8);
    f->bch_hi = bch[4];
}

/* the exchange entries open on the slots of one parity (BR §3.1): in the page, the master's slots of the device's clock
 * (the estimate's parity: probe 3b, an odd estimate was answered only on the odd entries); in the piconet, our own */
static void bp_xchg(uint32_t parity)
{
    uint32_t i;
    for (i = 0; i < 16u; i++)
        fm1_br_xchg(bti_tab, i, FM1_BR_OFF_FRAME | ((i & 1u) == parity ? 0u : 1u << 12));
}

static void bp_program(void)                       /* BR §8.3 step 6 (the library's page resume) */
{
    struct fm1_br_frame *f = fm1_br_frame(bti_tab, 0);
    uint32_t tx = f->txtog & 1u;
    bp_clkoff(f, bp.est);
    bp_addr_set(f, bp.remote, bp.rbch);
    f->miscntl = 0;
    f->wincntl = bti.o_win ? bti.o_win : 80u;
    f->txptr[tx] = BP_OFF_FHS;
    f->txptr[tx ^ 1u] = BP_OFF_FHS;
    f->format = (uint8_t)(4u | bti.train << 7);
    f->linkcntl[tx] = 0x4002u;                     /* FHS */
    f->linkcntl[tx ^ 1u] = 0x4001u;                /* POLL */
    f->txbufcntl[0] &= (uint8_t)~1u;
    f->txbufcntl[1] &= (uint8_t)~1u;
    f->txphdr[0] = 2u;
    f->txphdr[1] = 2u;
    bp_xchg((uint32_t)bp.est & 1u);
    bp.sub = 0;
}

static void bp_event(uint32_t kind, uint32_t a, uint32_t stat, const uint8_t *data, uint32_t len)
{
    struct fm1_br_frame *f = fm1_br_frame(bti_tab, 0);
    uint32_t clkn = f->rx_clkn_lo | (uint32_t)f->rx_clkn_hi << 16;
    uint8_t one = (uint8_t)a;
    bti_push(kind, a, stat, clkn, data ? data : &one, data ? len : 1u);
}

static void bp_conn_enter(void)                    /* BR §8.3 sub-state 2 -> the connection (master) */
{
    struct fm1_br_frame *f = fm1_br_frame(bti_tab, 0);
    uint32_t i;
    f->bufcntl = (uint16_t)((f->bufcntl & ~0x0F00u & ~0x20u) | 0x100u | 3u);
    f->txbufcntl[0] |= 1u;                         /* both empty: the engine polls */
    f->txbufcntl[1] |= 1u;
    f->rxptr[0] = BP_OFF_RX0;
    f->lmprxptr[0] = BP_OFF_RX0;
    f->rxptr[1] = BP_OFF_RX1;
    f->lmprxptr[1] = BP_OFF_RX1;
    for (i = 0; i < 2u; i++) {
        f->rxbufcntl[i] &= (uint8_t)~1u;
        f->linkcntl[i] |= 1u << 14;
    }
    bti.mode = BTI_M_CONN;
#if FELUCCA_BT_AUDIO
    for (i = 0; i < 2u; i++)
        f->linkcntl[i] &= (uint16_t)~(1u << 7 | 1u << 15);   /* (a new link: no encryption until it is started) */
    uECC_set_rng(bp_rng);
    btp_begin(&bp_ssp, bti.own, bp.remote, bp_rng, bp_ssp_send, 0);
    if (bta.bonded && bta.have_last && ble_eq(bp.remote, bta.last, 6))
        btp_bond(&bp_ssp, bta.lk);                 /* (bonded: its key, no pairing: bt_acl.c ba_poll authenticates) */
#endif
    bp.conn_ms = fm1_ms;
    bp.last_good_ms = fm1_ms;
    bp_event(BTI_K_CONN, 0, 0, 0, 0);
}

static void bp_page_rx(void)                       /* BR §8.3 "On IRQ 40 RX" (the page frame; only TYPE 0 counts) */
{
    struct fm1_br_frame *f = fm1_br_frame(bti_tab, 0);
    uint32_t idx = (f->rxtog & 1u) ^ 1u, tx = f->txtog & 1u, stat = f->rxstat[idx], pt, ff;
    uint32_t clkn = f->rx_clkn_lo | (uint32_t)f->rx_clkn_hi << 16;
    if (clkn == bti.last_rx_clkn)
        return;
    bti.last_rx_clkn = clkn;
    bti_count(bti_st.rs_key, bti_st.rs_n, &bti_st.rs_other, stat);
    bti_rxlog(idx, stat);
    if ((stat >> 8) == 0x8Fu)
        bti_agc_follow(f);
    if ((stat & 15u) != 0)
        return;
    bti.o_sweep = 0;                               /* (an answer: the carrier offset that got it stays) */
    if (bp.sub == 0) {                             /* the device's ID: send our FHS (master response) */
        pt = 2u, ff = 6u;
        bp.page_ids++;
        bti_alarm_arm(16u);
    } else if (bp.sub == 1) {                      /* its second ID (our FHS acknowledged): the piconet, POLL */
        pt = 1u, ff = 2u;
        bp.fhs_acked++;
        /* the piconet's clock and slots after the FHS (pcon=N, an experiment: with an exact odd estimate, probe 4c's
         * 0 / even got the FHS acknowledged 108 times and the POLL answered never; 4d's estimate / its parity the same):
         * bit0 = the frame keeps the estimate (else 0, the library's), bit1 = the entries keep its parity (else even) */
        bp_clkoff(f, bti.o_pcon == 4u ? bti.o_pclk : bti.o_pcon & 1u ? bp.est : 0);   /* (pclk=N: N slots) */
        bp_addr_set(f, bti.own, bp.obch);
        bp_xchg(bti.o_ppar <= 1u ? bti.o_ppar : bti.o_pcon & 2u ? (uint32_t)bp.est & 1u : 0u);
        f->rxtog = 0;
        f->rxstat[0] |= 0x90u;
        f->rxstat[1] |= 0x90u;
        bti_alarm_arm(32u);
    } else {
        pt = 1u, ff = 2u;
    }
    bp.sub++;
    f->miscntl = (uint8_t)((f->miscntl & ~7u) | BP_LT_ADDR);
    f->format = (uint8_t)(bti.train << 7 | ff);
    f->linkcntl[tx] = (uint16_t)(pt | 0x4010u);
    f->linkcntl[tx ^ 1u] = (uint16_t)(pt | 0x4000u);
    f->txbufcntl[tx] &= (uint8_t)~1u;
    {
        uint8_t ev[3] = {bp.sub, (uint8_t)(f->rxbit & 0xFFu), (uint8_t)((f->rxbit >> 8) & 3u)};
        bp_event(BTI_K_PSTEP, bp.sub, stat, ev, 3);
    }
    if (bp.sub > 2u)
        bp_conn_enter();
}

static void bp_tx_media(struct fm1_br_frame *f);
static void bp_stat_note(uint32_t stat)            /* IRQ 40: one rxstat into this second's table (bp_status) */
{
    uint32_t v = stat & 0xFFF0u, k;
    for (k = 0; k < 6u; k++) {
        if (bp.st_n[k] && bp.st_v[k] == v) {
            bp.st_n[k]++;
            return;
        }
        if (!bp.st_n[k]) {
            bp.st_v[k] = (uint16_t)v;
            bp.st_n[k] = 1;
            return;
        }
    }
    bp.st_more++;
}

static void bp_conn_rx(void)                       /* a packet in the connection: both buffers, the filled first */
{
    struct fm1_br_frame *f = fm1_br_frame(bti_tab, 0);
    uint32_t k, i, any = 0;
    for (k = 0; k < 2u; k++) {
        uint32_t stat, hdr, type, llid, len;
        i = ((f->rxtog & 1u) ^ 1u) ^ k;
        if (!(f->rxbufcntl[i] & 1u))
            continue;
        any = 1;
        stat = f->rxstat[i], hdr = f->rxphdr[i], type = stat & 15u, llid = hdr & 3u, len = (hdr >> 3) & 0x3FFu;
#if FELUCCA_BT_AUDIO
        bp_noise = bp_noise * 0x9E3779B1u + (stat ^ (uint32_t)f->rx_clkn_lo << 8 ^ f->rxbit);   /* (the randomness: bp_rng) */
#endif
        bti_rxlog(i, stat);
        bp_stat_note(stat);
        if (llid && !(hdr & 4u))
            bp.flow0++;
        bp.type_n[type]++;
        if ((stat >> 8) == 0x8Fu) {
            bp.good++;
            bp.last_good_ms = fm1_ms;
            if (llid == 3u && len >= 1u && len <= BTL_MAX)
                bp_event(BTI_K_LMP, i, stat, bti_tab + (i ? BP_OFF_RX1 : BP_OFF_RX0), len);
            else if (len && llid) {
                bp.acl++;
                bp_event(BTI_K_ACL, llid, stat, bti_tab + (i ? BP_OFF_RX1 : BP_OFF_RX0),
                         len > sizeof bti_ring[0].data ? (uint32_t)sizeof bti_ring[0].data : len);
            }
        } else {
            bp.bad++;
        }
        f->rxbufcntl[i] &= (uint8_t)~1u;            /* re-armed (BR §5.5) */
        f->linkcntl[i] |= 1u << 14;
    }
    if (!any) {                                    /* (a NULL / POLL may fill no buffer: its status still counts) */
        uint32_t stat = f->rxstat[(f->rxtog & 1u) ^ 1u];
        bp.nobuf++;
        bp_stat_note(stat);
        bp.type_n[stat & 15u]++;
        if ((stat >> 8) == 0x8Fu) {
            bp.good++;
            bp.last_good_ms = fm1_ms;
        }
    }
    bp_tx_media(f);                                /* (the stream's next packets at once, not at the main loop's pace) */
}

static void bp_rx(void)                            /* IRQ 40 RX in the page or connection modes */
{
    if (bti.mode == BTI_M_CONN)
        bp_conn_rx();
    else
        bp_page_rx();
}

static int bp_alarm(uint32_t now)                  /* the alarm in the page / connection modes -> 1 handled */
{
    if (bti.mode == BTI_M_PAGE) {
        if (((now - bti.train_clkn) & BTI_CLK) >= 2048u) {   /* the A/B train every 1.28 s (BR §8.3) */
            bti.train_clkn = now;
            bti.train ^= 1u;
            bti_st.trains++;
        }
        bp_program();                              /* (again every 18 slots: a response that went nowhere is over) */
    }
    if (((bti.end_clkn - now) & BTI_CLK) > (BTI_CLK >> 1) && bti.mode == BTI_M_PAGE) {
        bti_run_stop();
        return 1;
    }
    bti_alarm_arm(18u);
    return 1;
}

static void bp_queue(const uint8_t *pdu, uint32_t len, const char *why)
{
    uint32_t i;
    uint32_t tid = 0;
    if (bp.txw - bp.txr >= BP_TXQ || len == 0 || len > BTL_MAX)
        return;
    for (i = 0; i < len; i++)
        bp.txq[bp.txw % BP_TXQ][i] = pdu[i];
    bp.txl[bp.txw % BP_TXQ] = (uint8_t)len;
    bp.txllid[bp.txw % BP_TXQ] = 3u;
    bp.txw++;
    con_puts("btpage: LMP out ");
    con_puts(btl_name(btl_opcode(pdu, len, &tid)));
    bti_kv(" tid=", tid);
    if (why) {
        con_puts(" (");
        con_puts(why);
        con_puts(")");
    }
    con_puts("\r\n");
}

/* an audio packet into every empty TX buffer (BR §5.5), DH5 / DH3 / DH1 by its length: IRQ 40 itself (bp_conn_rx, each
 * reception: the stream needs 172 DH5 a second and the main loop runs ~65 times, measured 2026-10-20 on the WH-1000XM4:
 * audio_sent 2,008, dropped 3,527 in 30 s), or the main loop with IRQ 40 held. Never ahead of the LMP / L2CAP queue */
static void bp_tx_media(struct fm1_br_frame *f)
{
    uint32_t i, k, n, pass;
    for (pass = 0; pass < 2u && bp_mq.r != bp_mq.w && bp.txr == bp.txw; pass++) {
        i = f->txtog & 1u;
        if (!(f->txbufcntl[i] & 1u))
            i ^= 1u;
        if (!(f->txbufcntl[i] & 1u)) {
            bp.mq_busy++;
            return;                                /* (both buffers full: the engine has two to send) */
        }
        {
            uint32_t q = bp_mq.r % BP_MQ, t;
            n = bp_mq.n[q];
            for (k = 0; k < n; k++)
                bti_tab[(i ? BP_OFF_TXB1 : BP_OFF_TXB0) + k] = bp_mq.d[q][k];
            t = n > 183u ? 15u : n > 27u ? 11u : 4u;   /* DH5, DH3, DH1 (BR §5.2) */
            f->txptr[i] = (uint16_t)(i ? BP_OFF_TXB1 : BP_OFF_TXB0);
            f->txphdr[i] = (uint16_t)(n << 3 | 1u << 2 | 2u);   /* LLID 2: an L2CAP start, whole; FLOW 1 */
            f->linkcntl[i] = (uint16_t)((f->linkcntl[i] & 0x80B0u) | t | 0x4000u);
            f->txbufcntl[i] &= (uint8_t)~1u;
            bp_mq.r++;
            bp.mq_sent++;
        }
    }
}
static void bp_tx_service(void)                    /* the main loop: a PDU into an empty TX buffer (BR §5.5): the LMP /
                                                    * L2CAP queue first, else an audio packet */
{
    struct fm1_br_frame *f = fm1_br_frame(bti_tab, 0);
    uint32_t i, k, n;
    if (bti.mode != BTI_M_CONN)
        return;
    if (bp.txr == bp.txw) {
        if (bp_mq.r == bp_mq.w)
            return;
        fm1_br_irqs_hold(1);
        bp_tx_media(f);
        fm1_br_irqs_hold(0);
        return;
    }
    fm1_br_irqs_hold(1);
    i = f->txtog & 1u;
    if (!(f->txbufcntl[i] & 1u))
        i ^= 1u;
    if (f->txbufcntl[i] & 1u) {
        n = bp.txl[bp.txr % BP_TXQ];
        for (k = 0; k < n; k++)
            bti_tab[(i ? BP_OFF_TX1 : BP_OFF_TX0) + k] = bp.txq[bp.txr % BP_TXQ][k];
        f->txptr[i] = (uint16_t)(i ? BP_OFF_TX1 : BP_OFF_TX0);
        f->txphdr[i] = (uint16_t)(n << 3 | 1u << 2 | bp.txllid[bp.txr % BP_TXQ]);   /* LLID 3 LMP, 2 / 1 L2CAP; FLOW 1 */
        f->linkcntl[i] = (uint16_t)((f->linkcntl[i] & 0x80B0u) | 3u | 0x4000u);   /* DM1 */
        f->txbufcntl[i] &= (uint8_t)~1u;
        if (bp.txllid[bp.txr % BP_TXQ] == 3u)
            bp.lmp_out++;
        bp.txr++;
    }
    fm1_br_irqs_hold(0);
}

#if FELUCCA_BT_AUDIO
/* E0 in the engine (docs/BT-CLASSIC.md 9, [I]: not yet seen working): step 1 Kc' (= Kc: key size 16) into cryptk, RX
 * decryption (linkcntl bit 15) on both buffers; step 2 TX encryption (bit 7) too. The key's octet order in cryptk is the
 * air order, low octet first in each half-word: a guess the hardware test settles */
static void bp_e0(uint32_t step)
{
    struct fm1_br_frame *f = fm1_br_frame(bti_tab, 0);
    uint32_t i;
    fm1_br_irqs_hold(1);
    if (step == 1u)
        for (i = 0; i < 8u; i++)
            f->cryptk[i] = (uint16_t)(bp_ssp.kc[2u * i] | (uint32_t)bp_ssp.kc[2u * i + 1u] << 8);
    for (i = 0; i < 2u; i++)
        f->linkcntl[i] |= (uint16_t)(step == 1u ? 1u << 15 : 1u << 7);
    fm1_br_irqs_hold(0);
    con_puts(step == 1u ? "btpage: E0 key loaded, RX decryption on\r\n" : "btpage: TX encryption on: the link is encrypted\r\n");
}
#endif
static void bp_lmp_in(const struct bti_rx *r)
{
    uint8_t out[BTL_MAX + 8];
    uint32_t tid = 0, op = btl_opcode(r->data, r->len, &tid), n, i;
    bp.lmp_in++;
    con_puts("btpage: LMP in  ");
    con_puts(btl_name(op));
    bti_kv(" op=", op);
    bti_kv(" tid=", tid);
    con_puts(" [");
    for (i = 0; i < r->len; i++) {
        con_hex(r->data[i], 2);
        con_putc(i + 1u < r->len ? ' ' : ']');
    }
    con_puts("\r\n");
    if (op == BTL_ACCEPTED && r->len > 1u && r->data[1] == BTL_HOST_CONN_REQ && !bp.setup_out) {
        bp.setup_out = 1;
        n = btl_setup_complete(out);
        bp_queue(out, n, "the device took the connection");
    } else if (op == BTL_NOT_ACCEPTED && r->len > 2u) {
        con_puts("btpage: not accepted: op ");
        con_dec(r->data[1]);
        con_puts(" reason 0x");
        con_hex(r->data[2], 2);
        con_puts("\r\n");
        if (r->data[1] == BTL_NAME_REQ)
            bp.name_done = 1;                      /* (its name refused: as good as read, the pairing may start) */
        if (r->data[1] == BTL_MAX_SLOT_REQ && bp.slots5 == 1u) {   /* (5 refused: 3; a Raspberry Pi refused 5 with 0x34,
                                                    * Reserved Slot Violation, 2026-10-20) */
            out[0] = (uint8_t)(BTL_MAX_SLOT_REQ << 1 | 0u);
            out[1] = 3;
            bp.slots5 = 3;
            bp_queue(out, 2, "3-slot packets then");
        } else if (r->data[1] == BTL_MAX_SLOT_REQ && bp.slots5 == 3u) {
            bp.slots5 = 5;
            con_puts("btpage: no multi-slot packets: the audio cannot go (DM1 is too slow for SBC)\r\n");
        }
    } else if (op == BTL_ACCEPTED && r->len > 1u && r->data[1] == BTL_MAX_SLOT_REQ && (bp.slots5 == 1u || bp.slots5 == 3u)) {
        bp.slots5 = bp.slots5 == 1u ? 2u : 4u;
        con_puts(bp.slots5 == 2u ? "btpage: 5-slot packets granted: the audio in DH5, two SBC frames each\r\n"
                                 : "btpage: 3-slot packets granted: the audio in DH3, one SBC frame each\r\n");
    } else if (op == BTL_NAME_RES && r->len >= 3u) {   /* a fragment of its name (offset, whole length, 14 bytes) */
        uint32_t off = r->data[1], whole = r->data[2];
        if (off + 14u < whole && off + 14u < 248u) {
            uint8_t nr[2] = {(uint8_t)(BTL_NAME_REQ << 1 | 0u), (uint8_t)(off + 14u)};
            bp_queue(nr, 2, "the rest of its name");
        } else {
            bp.name_done = 1;
        }
    } else if (op == BTL_SETUP_COMPLETE) {
        bp.setup_in = 1;
        con_puts("btpage: the device sent setup_complete");
        con_puts(bp.setup_out ? ": the link is set up (LMP)\r\n" : "\r\n");
    } else if (op == BTL_DETACH) {
        con_puts("btpage: the device detached, reason 0x");
        con_hex(r->len > 1u ? r->data[1] : 0, 2);
        con_puts("\r\n");
        bp.stopping = 1;
        bp.stop_ms = fm1_ms;
    }
#if FELUCCA_BT_AUDIO
    {
        uint32_t was = bp_ssp.st;
        if (btp_in(&bp_ssp, r->data, r->len)) {
            if (bp_ssp.st == BTP_STARTING && was != BTP_STARTING)
                bp_e0(1);                               /* (we sent start_encryption_req: Kc' in, RX decryption on) */
            else if (bp_ssp.st == BTP_ENCRYPTED && was == BTP_STARTING)
                bp_e0(2);                               /* (it accepted: TX encryption on too) */
            return;
        }
    }
#endif
    n = btl_answer(r->data, r->len, "OPTIMIST BT", out);
    if (n)
        bp_queue(out, n, "answer");
    if (bp.setup_in && bp.setup_out && !bp.slots5) {   /* the link set up: 5-slot packets for the audio (Part C 4.1.10) */
        out[0] = (uint8_t)(BTL_MAX_SLOT_REQ << 1 | 0u);
        out[1] = 5;
        bp.slots5 = 1;
        bp_queue(out, 2, "5-slot packets for the audio");
        /* then, as BlueZ and Bluedroid do before they pair: its extended features (page 1) and its name, whole
         * (bt_acl.c waits for the name before the pairing; the AirPods had only ever met stacks that ask) */
        out[0] = (uint8_t)(BTL_ESCAPE << 1 | 0u), out[1] = BTL_X_FEATURES_REQ, out[2] = 1, out[3] = 1;
        for (n = 0; n < 8u; n++)
            out[4 + n] = (uint8_t)(n == 0u ? BTL_FEAT1_SSP_HOST : 0u);
        bp_queue(out, 12, "its extended features, page 1");
        out[0] = (uint8_t)(BTL_NAME_REQ << 1 | 0u), out[1] = 0;
        bp_queue(out, 2, "its name");
    }
}

static void bp_leave(void);                        /* (below: bt_acl.c lets a failed set-up go) */
#include "bt_acl.c"                                /* stage 3: L2CAP and SDP over the link */

static void bp_print_rx(const struct bti_rx *r)   /* bti_print_rx's page / connection kinds */
{
    if (r->kind == BTI_K_PSTEP) {
        static const char *const what[4] = {"", "the device's ID heard: our FHS goes out",
                                            "its second ID: our FHS acknowledged, POLL in the piconet",
                                            "its answer to the POLL"};
        con_puts("btpage: step ");
        con_dec(r->data[0]);
        con_puts(": ");
        con_puts(what[r->data[0] < 4u ? r->data[0] : 3u]);
        con_puts(" stat=0x");
        con_hex(r->stat, 4);
        con_puts(" clkn=0x");
        con_hex(r->clkn, 7);
        con_puts(" pll=");
        con_dec(bti.pll_now);
        bti_kv(" rxbit=", r->data[1] | (uint32_t)r->data[2] << 8);
        bti_kv(" rssi1=", r->rssi1);
        con_puts("\r\n");
    } else if (r->kind == BTI_K_CONN) {
        uint8_t out[BTL_MAX + 8];
        uint32_t n;
        con_puts("btpage: CONNECTED (master, LT_ADDR 1): the link runs on the engine's POLL / NULL\r\n");
        n = btl_features_req(out);
        bp_queue(out, n, 0);
        n = btl_version_req(out);
        bp_queue(out, n, 0);
        n = btl_host_conn_req(out);
        bp_queue(out, n, 0);
    } else if (r->kind == BTI_K_LMP) {
        bp_lmp_in(r);
    } else if (r->kind == BTI_K_ACL) {
        ba_in(r);
    }
}

static void bp_print_sec(void)
{
    uint32_t i;
    con_puts("btpage: 1s ");
    if (bti.mode == BTI_M_PAGE) {
        bti_kv("paging sub=", bp.sub);
        bti_kv(" ids=", bp.page_ids);
        bti_kv(" fhs_acked=", bp.fhs_acked);
        bti_kv(" rx=", bti_st.rx_irqs);
        con_puts(" pll=");
        con_dec(bti.pll_now);
        con_puts(" train=");
        con_putc(bti.train ? 'B' : 'A');
    } else {
        bti_kv("connected ", (fm1_ms - bp.conn_ms) / 1000u);
        bti_kv(" s good=", bp.good);
        bti_kv(" bad=", bp.bad);
        bti_kv(" nobuf=", bp.nobuf);
        bti_kv(" acl=", bp.acl);
        bti_kv(" lmp in=", bp.lmp_in);
        bti_kv(" out=", bp.lmp_out);
        bti_kv(" q=", bp.txw - bp.txr);
        bti_kv(" ring_drop=", bti_st.dropped);     /* (received, acknowledged, lost before the main loop read them) */
        bti_kv(" last_good_ms=", fm1_ms - bp.last_good_ms);
        if (bp.mq_sent || bp.mq_drop) {
            bti_kv(" audio_sent=", bp.mq_sent);                /* (DH5 packets of the stream; dropped whole: the queue full) */
            bti_kv(" audio_drop=", bp.mq_drop);
            bti_kv(" mq_hi=", bp.mq_hi);                       /* (this second's deepest queue, of BP_MQ) */
            bti_kv(" mq_busy=", bp.mq_busy);
            bp.mq_hi = 0;
        }
        bti_kv(" flow0=", bp.flow0);
        {
            uint16_t sv[6];
            uint32_t sn[6], more;
            fm1_br_irqs_hold(1);                   /* (a copy, then cleared: IRQ 40 is held only for that) */
            for (i = 0; i < 6u; i++) {
                sv[i] = bp.st_v[i], sn[i] = bp.st_n[i];
                bp.st_n[i] = 0;
            }
            more = bp.st_more;
            bp.st_more = 0;
            fm1_br_irqs_hold(0);
            con_puts(" st{");                      /* (this second: rxstat & 0xFFF0 : count) */
            for (i = 0; i < 6u && sn[i]; i++) {
                con_hex(sv[i], 4);
                bti_kv(":", sn[i]);
                con_putc(' ');
            }
            if (more)
                bti_kv("more:", more);
            con_puts("}");
        }
        con_puts(" types{");
        for (i = 0; i < 16u; i++)
            if (bp.type_n[i]) {
                con_puts(bp_type_name[i]);
                bti_kv(":", bp.type_n[i]);
                con_putc(' ');
            }
        con_puts("}");
    }
    con_puts("\r\n");
}

static void bp_poll(void)                          /* the main loop, page / connection modes */
{
    bp_tx_service();
    ba_poll();
    if (bti.mode == BTI_M_CONN && bti_st.phase == BTI_INQ_RUN) {
        if ((int32_t)(fm1_ms - bp.last_good_ms) > (int32_t)BP_SUPERVISION_MS) {   /* signed: IRQ 40 can store a
            * last_good_ms newer than the fm1_ms read here (unsigned, that was 0xFFFFFFFF: a healthy link cut, the user
            * 2026-10-21 "sometimes the BT disconnects", logs/drop-2026-10-21.log: lost at 150 s, last_good_ms=1) */
            con_puts("btpage: no good packet for 4 s: the link is lost\r\n");
            fm1_br_irqs_hold(1);
            bti_run_stop();
            fm1_br_irqs_hold(0);
        } else if (bp.stopping && fm1_ms - bp.stop_ms > 300u) {
            fm1_br_irqs_hold(1);
            bti_run_stop();
            fm1_br_irqs_hold(0);
        }
    }
}

static int bp_parse_addr(const char **p, uint8_t a[6])   /* "AA:BB:CC:DD:EE:FF" (NAP:UAP:LAP, as printed) */
{
    const char *s = *p;
    uint32_t i, k, v;
    while (*s == ' ')
        s++;
    for (i = 0; i < 6u; i++) {
        v = 0;
        for (k = 0; k < 2u; k++, s++) {
            char c = *s;
            if (c >= '0' && c <= '9')
                v = v << 4 | (uint32_t)(c - '0');
            else if (c >= 'a' && c <= 'f')
                v = v << 4 | (uint32_t)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
                v = v << 4 | (uint32_t)(c - 'A' + 10);
            else
                return 0;
        }
        a[5u - i] = (uint8_t)v;
        if (i < 5u && *s++ != ':')
            return 0;
    }
    *p = s;
    return 1;
}

static int bp_start(const uint8_t addr[6], uint32_t slots)
{
    struct fm1_br_frame *f = fm1_br_frame(bti_tab, 0);
    struct bti_fhs fh;
    uint32_t i, lap;
    int32_t est;
    if (!bti_prepare(BTI_M_PAGE))
        return 0;
    for (i = 0; i < 6u; i++)
        bp.remote[i] = addr[i];
    lap = addr[0] | (uint32_t)addr[1] << 8 | (uint32_t)addr[2] << 16;
    bti_parity(lap, bp.rbch);
    bti_own_addr();
    bti_parity(bti.own[0] | (uint32_t)bti.own[1] << 8 | (uint32_t)bti.own[2] << 16, bp.obch);
    bp.have_est = (uint8_t)(!bti.o_noclk && bti_seen_clk(addr, &est));
    /* the estimate + 1 (padj=N adds more): the inquiry hears the FHS in our odd RX slot and the FHS clock lacks CLK1,
     * so remote CLK - our CLKN comes out one short (probe 4g: +1 connected at once; +0, +2, -2, +4 got the FHS through
     * but never the POLL answered); the exchange entries follow its parity (bp_xchg) */
    bp.est = bp.have_est ? est + 1 + bti.o_padj : bti.o_padj;                /* the exact estimate; the exchange entries follow its parity (bp_xchg) */
    for (i = 0; i < 6u; i++)
        fh.addr[i] = bti.own[i];
    fh.eir = 0, fh.sr = 1, fh.cod = BP_COD, fh.lt_addr = BP_LT_ADDR, fh.clk = 0, fh.psm = 0;
    bti_fhs_build(&fh, bti_tab + BP_OFF_FHS);
    f->lmprxptr[0] = FM1_BR_OFF_FHS;
    f->lmprxptr[1] = FM1_BR_OFF_FHS;
    f->rxptr[0] = FM1_BR_OFF_FHS;
    f->rxptr[1] = FM1_BR_OFF_FHS;
    bp.page_ids = bp.fhs_acked = bp.good = bp.bad = bp.nobuf = bp.acl = bp.lmp_in = bp.lmp_out = 0;
    bp.setup_out = bp.setup_in = bp.stopping = 0;
    bp.slots5 = 0;
    bp.name_done = 0;
    bp.mq_drop = bp.mq_sent = bp.mq_hi = bp.mq_busy = bp.flow0 = 0;
    bp_mq.r = bp_mq.w;                             /* (a new link: no audio queued) */
    ba_reset();
    bp.txw = bp.txr = 0;
    for (i = 0; i < 16u; i++)
        bp.type_n[i] = 0;
    bp_program();
    bti_go(slots);
    fm1_br_irqs_hold(1);
    bp_xchg((uint32_t)bp.est & 1u);                /* (bti_go opened the even ones) */
    fm1_br_irqs_hold(0);
    return 1;
}

/* the link let go as the spec has it: LMP_detach (0x13, the user ended it), the run stopped 300 ms after (bp_poll),
 * so the device knows at once and can be connected to again (cut without it, it waits for its supervision timeout,
 * 20 s on headphones, and refuses meanwhile: the user, 2026-10-20, "it does not disconnect correctly"); no link up
 * (a page): stopped at once. The console's `btpage stop` and the page's SAVE (bt_audio.c bta_hangup) alike */
static void bp_leave(void)
{
    if (bti.mode == BTI_M_CONN && bti_st.phase == BTI_INQ_RUN) {
        uint8_t out[4];
        if (!bp.stopping)
            bp_queue(out, btl_detach(out, 0x13u), "let go");
        bp.stopping = 1;
        bp.stop_ms = fm1_ms;
    } else {
        fm1_br_irqs_hold(1);
        bti_run_stop();
        fm1_br_irqs_hold(0);
    }
}

static void con_btpage(const char *p)
{
    uint8_t a[6];
    uint32_t s = 10u;
    if (con_word(&p, "stop")) {
        bp_leave();
        return;
    }
    if (!bp_parse_addr(&p, a)) {
        con_puts("btpage AA:BB:CC:DD:EE:FF [SECONDS] [OPTIONS] | stop\r\n");
        return;
    }
    bti_opts(p, &s);
    if (!bp_start(a, s * 1600u)) {
        con_puts("btpage: not started (busy, or no RF trims: 'btclk')\r\n");
        return;
    }
    con_puts("btpage: paging ");
    bti_addr(a);
    bti_kv(" for ", s * 1000u);
    con_puts(" ms (the link then stays up until 'btpage stop' or lost) as ");
    bti_addr(bti.own);
    if (bp.have_est) {
        con_puts(", clock offset from the last inquiry ");
        con_dec(bp.est);
    } else {
        con_puts(", no clock estimate (not in the last inquiry): both trains");
    }
    bti_opts_print();
    con_puts("\r\n");
}
