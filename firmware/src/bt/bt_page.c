/* SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0 */
/* EXPERIMENTAL Bluetooth Classic probe, stage 2 (BT_AUDIO): `btpage ADDR [SECONDS] [OPTIONS]` pages a device as the
 * master (docs/BT-CLASSIC.md §8.3: the ID train with the device's access code, its ID answer, our FHS, its
 * second ID, our POLL), keeps the link up with the engine's POLL / NULL and runs the minimal LMP of bt_lmp.c: our
 * features, version and host connection requests, the answers to what the device asks, setup complete. Every step is
 * printed as a `btpage:` line. Included by bt_inq.c (its frame, rings, options and printing). No vendor code. */
#include "bt_lmp.c"
#if FELUCCA_BT_AFH
#include "bt_afh.c"                                /* the channels the device fails on out of the hopping (LMP_set_AFH) */
#endif

#define BP_OFF_FHS 4252u                           /* our FHS for the page (18 B) */
#define BP_OFF_TX0 4272u                           /* the two LMP TX payloads (17 B each) */
#define BP_OFF_TX1 4292u
#define BP_OFF_TXB0 4480u                          /* the two DH5 TX payloads (339 B each, after the table: BTI_TAB_ALLOC) */
#define BP_OFF_TXB1 (4480u + 344u)
#define BP_TXB 339u                                /* a DH5 payload (BR §5.5) */
#define BP_MQB 332u                                /* the longest audio packet queued: 17 B of L2CAP and media headers and
                                                    * BA_MFRAMES SBC frames at BA_BITPOOL (bt_acl.c asserts the sum; a
                                                    * lower agreed bitpool makes shorter ones); 7 B a slot less than a DH5 */
#define BP_MQ 40u                                 /* the audio packets queued (~350 ms of the stream at 3 SBC frames a
                                                    * packet; 4 = 23 ms overflowed on every radio hiccup: audio_drop
                                                    * 11 %, heard as crackles, 2026-10-21; 16 = 140 ms overflowed on the
                                                    * XM4's 100-150 ms refusals of our DH5s while it kept answering,
                                                    * 2026-10-23: a phone's sink buffer rides those out, so does this) */
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
    uint32_t rs_sum, rs_n, rs_min, rs_max;         /* this second's RSSI1 (BR §4) at each reception: the gain set at the
                                                    * page stays for the link (bti_agc_follow runs in bti_page_rx only) */
    uint8_t agc_follow;                            /* `btpage follow=1`: bti_agc_follow at each reception of the link */
    uint32_t rb_sum, rb_n, rb_min, rb_max;         /* this second's rxbit (BR §4) of the good receptions: where in our
                                                    * window the device's answers land (timing drift? the XM4, 2026-10-21) */
    uint32_t g10, g50, g200, gmax;                 /* this second's gaps between good receptions over 10 / 50 / 200 ms and
                                                    * the longest, ms: the XM4 silent in bursts, or losing packets at
                                                    * random? (logs/drop-2026-10-21.log: last_good_ms 50-250 while bad) */
    uint16_t ch_good[80], ch_sec;                  /* good receptions per RF channel (rxfreq & 0x7F; [79] out of range)
                                                    * since the last assessment (every BP_CH_SEC s, bp_sec_tick): the
                                                    * channels the device fails on leave the hopping (bt_afh.c); the
                                                    * console's channel line */
    uint8_t feat[8];                               /* the device's features (its LMP_features_res, page 0; byte 4 bit 0:
                                                    * AFH capable slave) */
    uint8_t afh_map[10], afh_on, afh_pending;      /* the hop set (79 bits, all at a new link), AFH wanted, a switch
                                                    * announced (LMP_set_AFH) and due at afh_at (CLKN slots, bp_alarm) */
    uint32_t afh_at, afh_switches;
} bp;
#define BP_CH_SEC 30u                              /* the channel assessment's window, s (~2 answers a channel a second) */
#define BP_AFH_AHEAD 320u                          /* the switch this many slots (200 ms) after its announcement */
/* the audio packets (bt_acl.c ba_media_send, from the audio interrupt): whole L2CAP frames, each one DH5 (or DH3 / DH1
 * when shorter), never cut; the main loop's bp_tx_service sends them after the LMP and L2CAP signalling */
static struct {
    uint8_t d[BP_MQ][BP_MQB];
    uint16_t n[BP_MQ];
    volatile uint32_t w, r;
} bp_mq IN_POOL;
static int bp_media_queue(const uint8_t *frame, uint32_t len)   /* 1 queued; 0 dropped whole (full, too long, no 5 slots) */
{
    uint32_t i, k;
    if ((bp.slots5 != 2u && bp.slots5 != 4u) || len > (bp.slots5 == 2u ? BP_MQB : 183u) || bp_mq.w - bp_mq.r >= BP_MQ) {
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
static void bp_queue_w(const uint8_t *pdu, uint32_t len, const char *why);
/* (why: the console's line, with a console; without one its string need not be in flash: 162 B on mots.config) */
#define bp_queue(pdu, len, why) bp_queue_w((pdu), (len), FELUCCA_CDC ? (why) : (const char *)0)
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
    if (bta_mem_of(bp.remote) && bta_mem_of(bp.remote)->bonded)   /* (remembered and bonded: its key, no pairing: */
        btp_bond(&bp_ssp, bta_mem_of(bp.remote)->lk);  /* bt_acl.c ba_poll authenticates; bta_mem.c) */
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
    f->miscntl = (uint8_t)((f->miscntl & 0x38u) | BP_LT_ADDR);   /* (hop table 0, the basic one: AFH comes later, bp_alarm) */
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
    if (!FELUCCA_BT_DIAG)
        return;
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

static void bp_good_note(const struct fm1_br_frame *f)   /* IRQ 40: a good reception's RF channel (and, BT_DIAG, its gap) */
{
    uint32_t c = f->rxfreq & 0x7Fu;
    if (c > 78u)
        c = 79u;
    if (bp.ch_good[c] < 0xFFFFu)
        bp.ch_good[c]++;
    if (FELUCCA_BT_DIAG) {
        uint32_t d = fm1_ms - bp.last_good_ms;
        if (d > 10u)
            bp.g10++;
        if (d > 50u)
            bp.g50++;
        if (d > 200u)
            bp.g200++;
        if (d > bp.gmax)
            bp.gmax = d;
    }
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
        if (FELUCCA_BT_DIAG && llid && !(hdr & 4u))
            bp.flow0++;
        bp.type_n[type]++;
        if ((stat >> 8) == 0x8Fu) {
            bp.good++;
            bp_good_note(f);
            bp.last_good_ms = fm1_ms;
            if (FELUCCA_BT_DIAG) {
                uint32_t b = f->rxbit & 0x3FFu;
                bp.rb_sum += b;
                bp.rb_n++;
                if (b < bp.rb_min)
                    bp.rb_min = b;
                if (b > bp.rb_max)
                    bp.rb_max = b;
            }
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
            bp_good_note(f);
            bp.last_good_ms = fm1_ms;
        }
    }
    if (FELUCCA_BT_DIAG) {
        uint32_t r = f->rssi[1];
        bp.rs_sum += r;
        bp.rs_n++;
        if (r < bp.rs_min)
            bp.rs_min = r;
        if (r > bp.rs_max)
            bp.rs_max = r;
        if (bp.agc_follow)
            bti_agc_follow(f);
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
#if FELUCCA_BT_AFH
    if (bp.afh_pending) {                          /* the hop set announced (LMP_set_AFH) switches at its instant: the
                                                    * alarm lands on it (Part C 4.1.4: the slave switches there too) */
        uint32_t left = (bp.afh_at - now) & BTI_CLK;
        if (left == 0 || left > (BTI_CLK >> 1)) {
            struct fm1_br_frame *f = fm1_br_frame(bti_tab, 0);
            f->miscntl = (uint8_t)((f->miscntl & 0x3Fu) | (bp.afh_on ? 1u << 6 : 0u));   /* hop table 1 / basic (BR §4) */
            f->format = (uint8_t)((f->format & 0xBFu) | (bp.afh_on ? 0x40u : 0u));
            bp.afh_pending = 0;
            bp.afh_switches++;
        } else if (left < 18u) {
            bti_alarm_arm(left);
            return 1;
        }
    }
#endif
    bti_alarm_arm(18u);
    return 1;
}

static void bp_queue_w(const uint8_t *pdu, uint32_t len, const char *why)
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
/* E0 in the engine (docs/BT-CLASSIC.md 9, [I]: not yet seen working): step 1 Kc' (bt_ssp.c: reduced below 16 octets) into cryptk, RX
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
    if (op == BTL_FEATURES_RES && r->len >= 9u)
        for (i = 0; i < 8u; i++)
            bp.feat[i] = r->data[1u + i];          /* (kept: AFH only to an AFH capable slave, bp_afh_assess) */
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

#if FELUCCA_CDC && FELUCCA_BT_DIAG
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
#if FELUCCA_BT_DIAG
        bti_kv(" flow0=", bp.flow0);
#if FELUCCA_BT_AFH
        {
            uint32_t used = 0;
            for (i = 0; i < 79u; i++)
                used += bt_afh_used(bp.afh_map, i);
            bti_kv(" afh=", used);                 /* (the hop set's channels of 79; switches so far; a switch due) */
            bti_kv("/", bp.afh_switches);
            con_puts(bp.afh_pending ? "p" : "");
        }
#endif
        con_puts(" pll=");
        con_dec(bti.pll_now);
        {
            uint32_t sum, n, lo, hi;
            fm1_br_irqs_hold(1);
            sum = bp.rs_sum, n = bp.rs_n, lo = bp.rs_min, hi = bp.rs_max;
            bp.rs_sum = bp.rs_n = bp.rs_max = 0;
            bp.rs_min = 0xFFFFFFFFu;
            fm1_br_irqs_hold(0);
            bti_kv(" agc=", bti.agc);
            con_puts(bp.agc_follow ? "f" : "");
            if (n) {
                bti_kv(" rssi=", lo);
                bti_kv("/", sum / n);
                bti_kv("/", hi);
            }
            fm1_br_irqs_hold(1);
            sum = bp.rb_sum, n = bp.rb_n, lo = bp.rb_min, hi = bp.rb_max;
            bp.rb_sum = bp.rb_n = bp.rb_max = 0;
            bp.rb_min = 0xFFFFFFFFu;
            fm1_br_irqs_hold(0);
            if (n) {
                bti_kv(" rxbit=", lo);
                bti_kv("/", (sum * 10u / n) / 10u);
                bti_kv(".", (sum * 10u / n) % 10u);
                bti_kv("/", hi);
            }
            fm1_br_irqs_hold(1);
            sum = bp.g10, n = bp.g50, lo = bp.g200, hi = bp.gmax;
            bp.g10 = bp.g50 = bp.g200 = bp.gmax = 0;
            fm1_br_irqs_hold(0);
            bti_kv(" gaps>10/50/200=", sum);   /* (this second's gaps between good receptions; gmax the longest, ms) */
            bti_kv("/", n);
            bti_kv("/", lo);
            bti_kv(" gmax=", hi);
        }
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
#endif
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

#endif
#if FELUCCA_BT_AFH
static void bp_afh_send(uint32_t on)               /* the hop set to the engine's AFH table and map, LMP_set_AFH to the
                                                    * device with the switch's instant (bp_alarm switches there) */
{
    uint8_t out[BTL_MAX + 8], all[10];
    uint32_t words[3], i, n, at;
    const uint8_t *map = bp.afh_map;
    if (!on) {
        for (i = 0; i < 9u; i++)
            all[i] = 0xFFu;
        all[9] = 0x7Fu;
        map = all;
    }
    n = bt_afh_table(map, bti_tab + FM1_BR_OFF_FRQ + 80u, words);   /* frq_table[1] (BR §3) */
    fm1_br_afh_map(0, words);                      /* (map 0 goes with hop table 1: btctrler.a bdhw_set_afh) */
    (void)n;
    at = (bti_clkn() + BP_AFH_AHEAD) & BTI_CLK & ~1u;   /* an even slot: a master-to-slave one (CLK[1] = 0) */
    bp.afh_at = at;
    bp.afh_on = (uint8_t)on;
    bp.afh_pending = 1;
    bp_queue(out, btl_set_afh(out, at, on, map), on ? "the channels it fails on out of the hopping" : "AFH off");
}
static void bp_afh_assess(const uint16_t ch[80])  /* every BP_CH_SEC s of the link (bp_sec_tick): the channels the device
                                                    * failed on leave the hop set (bt_afh.c), the device told */
{
    if (!bp.afh_on || bp.afh_pending || !(bp.feat[4] & 1u) || !bp.setup_in || !bp.setup_out)
        return;                                    /* (AFH only once set up, to an AFH capable slave, one switch at a time) */
    if (bt_afh_classify(ch, bp.afh_map))
        bp_afh_send(1);
}
#endif
static void bp_sec_tick(void)                      /* the main loop, once a second in the page / connection modes */
{
    uint16_t ch[80];
    uint32_t i;
    if (bti.mode != BTI_M_CONN || ++bp.ch_sec < BP_CH_SEC)
        return;
    bp.ch_sec = 0;
    fm1_br_irqs_hold(1);
    for (i = 0; i < 79u; i++) {                    /* by RF channel: rxfreq is the hop index (0-39 the even channels,
                                                    * 40-78 the odd; frq_table[0], BR §7) */
        ch[i < 40u ? 2u * i : 2u * (i - 40u) + 1u] = bp.ch_good[i];
        bp.ch_good[i] = 0;
    }
    ch[79] = bp.ch_good[79];
    bp.ch_good[79] = 0;
    fm1_br_irqs_hold(0);
#if FELUCCA_CDC && FELUCCA_BT_DIAG
    con_puts("btpage: ch{");                       /* the answers per RF channel 0..78, then the out-of-range */
    for (i = 0; i < 79u; i++) {
        con_dec(ch[i]);
        con_putc(' ');
    }
    bti_kv("| ", ch[79]);
    con_puts("}\r\n");
#endif
#if FELUCCA_BT_AFH
    bp_afh_assess(ch);
#else
    (void)ch;
#endif
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

#if FELUCCA_CDC && FELUCCA_BT_DIAG
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

#endif
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
    bp.mq_drop = bp.mq_sent = bp.mq_hi = bp.mq_busy = bp.flow0 = bp.rs_sum = bp.rs_n = bp.rs_max = 0;
    bp.rs_min = 0xFFFFFFFFu;
    bp.rb_sum = bp.rb_n = bp.rb_max = 0;
    bp.rb_min = 0xFFFFFFFFu;
    bp.g10 = bp.g50 = bp.g200 = bp.gmax = 0;
    bp.ch_sec = 0;
    for (i = 0; i < 80u; i++)
        bp.ch_good[i] = 0;
    for (i = 0; i < 8u; i++)
        bp.feat[i] = 0;
    for (i = 0; i < 9u; i++)
        bp.afh_map[i] = 0xFFu;                     /* (a new link hops over all 79: the device's holes measured again) */
    bp.afh_map[9] = 0x7Fu;
    bp.afh_on = FELUCCA_BT_AFH && !FELUCCA_BT_DIAG;   /* (a diagnostics build switches only when told: btpage afh=) */
    bp.afh_pending = 0;
    bp.afh_switches = 0;
    bp_mq.r = bp_mq.w;                            /* (a new link: no audio queued) */
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

#if FELUCCA_CDC && FELUCCA_BT_DIAG
static void con_btpage(const char *p)
{
    uint8_t a[6];
    uint32_t s = 10u;
#if FELUCCA_BT_DIAG
    int32_t v;
    uint32_t i;
#endif
    if (con_word(&p, "stop")) {
        bp_leave();
        return;
    }
#if FELUCCA_BT_DIAG
    if (bti_opt_num(&p, "pll", &v)) {             /* the carrier offset now, in a running link (the sweep's pick stays
                                                    * for the whole link, bti_page_rx: does it drift? the XM4, 2026-10-21) */
        con_puts("btpage: pll ");
        con_dec(bti.pll_now);
        bti_pll(v < -1000 ? -1000 : v > 1000 ? 1000 : v);
        con_puts(" -> ");
        con_dec(bti.pll_now);
        con_puts("\r\n");
        return;
    }
    if (bti_opt_num(&p, "agc", &v)) {             /* the receive gain now, fixed (0..15), in a running link */
        struct fm1_br_frame *f = fm1_br_frame(bti_tab, 0);
        bti_kv("btpage: agc ", bti.agc);
        bti.agc = (uint8_t)(v < 0 ? 0 : v > 15 ? 15 : v);
        bp.agc_follow = 0;
        f->rxgain0 = bti.agc;
        bti_kv(" -> ", bti.agc);
        con_puts("\r\n");
        return;
    }
    if (bti_opt_num(&p, "follow", &v)) {          /* the gain following RSSI1 at each reception of the link (as paging) */
        bp.agc_follow = v != 0;
        con_puts(bp.agc_follow ? "btpage: agc follows\r\n" : "btpage: agc fixed\r\n");
        return;
    }
#if FELUCCA_BT_AFH
    if (bti_opt_num(&p, "afh", &v)) {             /* AFH in the running link (a diagnostics build switches only when
                                                    * told, bp_start): 0 off (all 79 again), 1 on (the map as assessed,
                                                    * the switch at once), 3 the map reset to all 79 then on (the
                                                    * mechanics alone: the hopping stays the basic one), 6 hop table 1
                                                    * selected alone with all 79 (no LMP: the same test without the device) */
        if (bti.mode != BTI_M_CONN || bp.afh_pending) {
            con_puts("btpage: afh: no link, or a switch under way\r\n");
            return;
        }
        if (v == 3) {
            for (i = 0; i < 9u; i++)
                bp.afh_map[i] = 0xFFu;
            bp.afh_map[9] = 0x7Fu;
        }
        if (v == 6) {
            uint8_t all[10];
            uint32_t words[3];
            struct fm1_br_frame *fr = fm1_br_frame(bti_tab, 0);
            for (i = 0; i < 9u; i++)
                all[i] = 0xFFu;
            all[9] = 0x7Fu;
            (void)bt_afh_table(all, bti_tab + FM1_BR_OFF_FRQ + 80u, words);
            fm1_br_afh_map(0, words);
            fr->miscntl = (uint8_t)((fr->miscntl & 0x3Fu) | 1u << 6);
            fr->format = (uint8_t)(fr->format | 0x40u);
        } else if (v == 0)
            bp_afh_send(0);
        else {
            bp.afh_on = 1;
            bp_afh_send(1);
        }
        bti_kv("btpage: afh ", (uint32_t)v);
        bti_kv(" at slot ", bp.afh_at);
        con_puts("\r\n");
        return;
    }
#endif
#endif
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
#endif
