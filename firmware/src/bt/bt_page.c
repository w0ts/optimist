/* SPDX-License-Identifier: GPL-3.0-only */
/* EXPERIMENTAL Bluetooth Classic probe, stage 2 (BT_INQ_PROBE): `btpage ADDR [SECONDS] [OPTIONS]` pages a device as the
 * master (docs/BT-CLASSIC-HW-FACTS.md §8.3: the ID train with the device's access code, its ID answer, our FHS, its
 * second ID, our POLL), keeps the link up with the engine's POLL / NULL and runs the minimal LMP of bt_lmp.c: our
 * features, version and host connection requests, the answers to what the device asks, setup complete. Every step is
 * printed as a `btpage:` line. Included by bt_inq.c (its frame, rings, options and printing). No vendor code. */
#include "bt_lmp.c"

#define BP_OFF_FHS 4252u                           /* our FHS for the page (18 B) */
#define BP_OFF_TX0 4272u                           /* the two LMP TX payloads (17 B each) */
#define BP_OFF_TX1 4292u
#define BP_OFF_RX0 1088u                           /* the connection's two RX buffers: the table's link_rx_bulk (BR §3) */
#define BP_OFF_RX1 (1088u + 680u)
#define BP_LT_ADDR 1u
#define BP_TXQ 32u
#define BP_SUPERVISION_MS 4000u                    /* no good packet for this long: the link is called lost */

static struct {
    uint8_t remote[6], rbch[5], obch[5];
    uint8_t sub, have_est, setup_out, setup_in, stopping;
    int32_t est;                                   /* the device's clock offset estimate, slots */
    uint32_t page_ids, fhs_acked, conn_ms, last_good_ms, good, bad, nobuf, acl, lmp_in, lmp_out;
    uint32_t type_n[16];
    uint8_t txq[BP_TXQ][BTL_MAX], txl[BP_TXQ], txllid[BP_TXQ];
    uint32_t txw, txr, stop_ms;
} bp;

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
        bti_rxlog(i, stat);
        bp.type_n[type]++;
        if ((stat >> 8) == 0x8Fu) {
            bp.good++;
            bp.last_good_ms = fm1_ms;
            if (llid == 3u && len >= 1u && len <= BTL_MAX)
                bp_event(BTI_K_LMP, i, stat, bti_tab + (i ? BP_OFF_RX1 : BP_OFF_RX0), len);
            else if (len && llid) {
                bp.acl++;
                bp_event(BTI_K_ACL, llid, stat, bti_tab + (i ? BP_OFF_RX1 : BP_OFF_RX0), len > 240u ? 240u : len);
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
        bp.type_n[stat & 15u]++;
        if ((stat >> 8) == 0x8Fu) {
            bp.good++;
            bp.last_good_ms = fm1_ms;
        }
    }
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

static void bp_tx_service(void)                    /* the main loop: one LMP PDU into an empty TX buffer (BR §5.5) */
{
    struct fm1_br_frame *f = fm1_br_frame(bti_tab, 0);
    uint32_t i, k, n;
    if (bti.mode != BTI_M_CONN || bp.txr == bp.txw)
        return;
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
    n = btl_answer(r->data, r->len, "OPTIMIST BT", out);
    if (n)
        bp_queue(out, n, "answer");
}

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
        bti_kv(" last_good_ms=", fm1_ms - bp.last_good_ms);
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
        if (fm1_ms - bp.last_good_ms > BP_SUPERVISION_MS) {
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
    fh.eir = 0, fh.sr = 1, fh.cod = 0x240404u, fh.lt_addr = BP_LT_ADDR, fh.clk = 0, fh.psm = 0;
    bti_fhs_build(&fh, bti_tab + BP_OFF_FHS);
    f->lmprxptr[0] = FM1_BR_OFF_FHS;
    f->lmprxptr[1] = FM1_BR_OFF_FHS;
    f->rxptr[0] = FM1_BR_OFF_FHS;
    f->rxptr[1] = FM1_BR_OFF_FHS;
    bp.page_ids = bp.fhs_acked = bp.good = bp.bad = bp.nobuf = bp.acl = bp.lmp_in = bp.lmp_out = 0;
    bp.setup_out = bp.setup_in = bp.stopping = 0;
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

static void con_btpage(const char *p)
{
    uint8_t a[6];
    uint32_t s = 10u;
    if (con_word(&p, "stop")) {
        if (bti.mode == BTI_M_CONN && bti_st.phase == BTI_INQ_RUN) {
            uint8_t out[4];
            bp_queue(out, btl_detach(out, 0x13u), "stop");
            bp.stopping = 1;
            bp.stop_ms = fm1_ms;
        } else {
            fm1_br_irqs_hold(1);
            bti_run_stop();
            fm1_br_irqs_hold(0);
        }
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
