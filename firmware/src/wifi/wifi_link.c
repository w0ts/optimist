/* SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0 */
/* The Wi-Fi service that runs Ableton Link on the FM-1 (docs/LINK-DESIGN.md, docs/WIFI-LINK-HANDOVER.md). Included by
 * io/console.c after wifi_sta.c, wifi_ip.c and link.c (a test build: OPTIMIST_WIFI=1). Started by the console's
 * `wifilink` after a `wifijoin`; wl_poll runs once a main-loop pass, wl_midi_poll in the 2 kHz TIMER5 tick (main.c).
 *
 * Main loop (wl_poll): the RX DMA, the early filter, the IP layer (ARP, DHCP, IGMP, UDP into Link's lk_rx), group
 * rekeys, Link's timers, and the MIDI clock Link implies. Link's session becomes a MIDI clock for clock_sync.c: 24 F8 a
 * beat at the exact time Link puts each tick (never the arrival of anything), FA at the next bar line when the session
 * plays, FC when it stops; they go through a small ring to the TIMER5 tick, which queues them as MSRC_LINK input. The
 * FM-1 follows Link's tempo and start / stop; it does not send its own PLAY or tempo changes to Link (yet).
 *
 * Time: a microsecond clock from TIMER4 (24 MHz) kept in wl.us_ref / wl.t_ref / wl.rem: no 64-bit division. */

#define WL_Q 16u                                       /* the tick ring (the main loop writes, the TIMER5 tick reads) */
#define WL_AHEAD_US 25000                              /* ticks are queued this far before their time */

static struct {
    uint8_t joined, on, feed, started;                 /* wifijoin done; the service runs; ticks flow; FA sent */
    uint8_t gtk_seen;
    uint32_t k;                                        /* the next MIDI clock tick: 24 a beat of the session */
    uint32_t t_ref, rem;                               /* TIMER4 at the last wl_now; its ticks since the last whole us */
    int64_t us_ref;                                    /* the microsecond clock at t_ref - rem */
    struct wifi_sta sta;                               /* the joined station (its keys are installed in the MAC) */
    struct wifi_ip ip;
    uint32_t n_rx, n_data, n_uc, n_uc_snap, n_prot, n_kept, n_eapol, n_tx, n_tx_err, n_ticks, n_restart;
} wl IN_POOL;
static struct lk_node wl_lk IN_POOL;
static uint32_t wl_txb[(FM1_WIFI_TX_HDR + 576u) / 4u] IN_POOL;
static uint32_t wl_qp[WL_Q] IN_POOL, wl_qt[WL_Q] IN_POOL;
static volatile uint32_t wl_qw, wl_qr;

static int64_t wl_now(void)                            /* host microseconds, from TIMER4 (called at least every 100 s) */
{
    uint32_t t = fm1_ticks();
    wl.rem += t - wl.t_ref;
    wl.t_ref = t;
    wl.us_ref += wl.rem / FM1_TICKS_PER_US;
    wl.rem %= FM1_TICKS_PER_US;
    return wl.us_ref;
}

static uint32_t wl_ticks_of(int64_t us)                /* TIMER4 ticks of a host time within a minute of now */
{
    return wl.t_ref - wl.rem + (uint32_t)(int32_t)(us - wl.us_ref) * FM1_TICKS_PER_US;
}

static void wl_send(uint32_t len)                      /* what wl_txb's frame holds, out */
{
    fm1_wifi_tx_prepare(wl_txb, len);
    if (fm1_wifi_tx(wl_txb) == 0)
        wl.n_tx++;
    else
        wl.n_tx_err++;
}

static void wl_q_push(uint32_t st, uint32_t t)
{
    if (wl_qw - wl_qr >= WL_Q)
        return;
    wl_qp[wl_qw % WL_Q] = 0x0Fu | st << 8;             /* (CIN F: one system real-time byte) */
    wl_qt[wl_qw % WL_Q] = t;
    RING_PUBLISH();
    wl_qw++;
}

static void wl_midi_poll(void)                         /* the TIMER5 tick: a queued message whose time has come -> clock_sync */
{
    while (wl_qr != wl_qw && (int32_t)(fm1_ticks() - wl_qt[wl_qr % WL_Q]) >= 0) {
        if (!midi_in_enqueue(wl_qp[wl_qr % WL_Q], MSRC_LINK, wl_qt[wl_qr % WL_Q]))
            return;                                    /* (the router's ring is full: the next tick) */
        RING_PUBLISH();
        wl_qr++;
    }
}

static void wl_clock(int64_t now)                      /* Link's session as MIDI clock (see the top of the file) */
{
    int on = wl.ip.ip && lk_peers(&wl_lk) > 0;
    if (!on) {
        if (wl.started)
            wl_q_push(0xFCu, wl_ticks_of(now));
        wl.feed = wl.started = 0;
        return;
    }
    if (!wl.feed) {                                    /* the next tick at or after now */
        int64_t ub = lk_beat_at(&wl_lk, now);
        if (ub < 0)
            return;
        wl.k = sy_div((uint64_t)ub * 24u, LK_UBEAT) + 1u;
        wl.feed = 1;
        wl.started = 0;
        wl.n_restart++;
    }
    for (;;) {
        uint32_t m = wl.k / 3u, r = wl.k - m * 3u, t_ticks;
        int64_t ub = (int64_t)m * 125000 + (r == 0u ? 0 : r == 1u ? 41667 : 83333), t = lk_time_at(&wl_lk, ub);
        int play = lk_playing(&wl_lk);
        if (t > now + WL_AHEAD_US)
            break;
        if (t < now - 200000) {                        /* the main loop stalled (or the session moved): start again */
            wl.feed = 0;
            return;
        }
        t_ticks = wl_ticks_of(t);
        if (play && !wl.started && wl.k % 96u == 0u) { /* the next bar line (4 beats: the quantum) */
            wl_q_push(0xFAu, t_ticks);
            wl.started = 1;
        } else if (!play && wl.started) {
            wl_q_push(0xFCu, t_ticks);
            wl.started = 0;
        }
        wl_q_push(0xF8u, t_ticks);
        wl.n_ticks++;
        wl.k++;
    }
}

static void wl_rx(const uint8_t *f, uint32_t len, int64_t now)   /* one 802.11 frame the AP sent (RXWI stripped) */
{
    uint32_t tx_len = 0, hl;
    uint8_t *txf = (uint8_t *)wl_txb + FM1_WIFI_TX_HDR;
    int r;
    wl.n_rx++;
    if (len < 32u || (f[0] & 0x0Cu) != 0x08u)
        return;                                        /* (data frames only) */
    wl.n_data++;
    hl = (f[0] & 0x80u) ? 26u : 24u;
    if (len < hl + 8u)
        return;
    if (!memcmp(f + 4, wl.sta.mac, 6)) {               /* sent to us alone: the pairwise key decrypted it, or did not */
        wl.n_uc++;
        if (f[1] & 0x40u)
            wl.n_prot++;
        if (f[hl] == 0xAAu && f[hl + 1u] == 0xAAu && f[hl + 2u] == 0x03u)
            wl.n_uc_snap++;
    }
    if (!wifi_ip_early_filter(f, len, wl.sta.mac, wl.ip.ip, wl.ip.link_port))
        return;
    wl.n_kept++;
    if (f[hl + 6u] == 0x88u && f[hl + 7u] == 0x8Eu) {  /* EAPOL: a group rekey */
        wl.n_eapol++;
        r = wifi_sta_rx_frame(&wl.sta, f, len, txf, &tx_len);
        if (r == 1)
            wl_send(tx_len);
        return;
    }
    r = wifi_ip_rx_frame(&wl.ip, f, len, now, txf, &tx_len);
    if (r == 1)
        wl_send(tx_len);
}

static void wl_poll(void)                              /* once a main-loop pass */
{
    uint32_t i, tx_len;
    int64_t now;
    if (!wl.on)
        return;
    for (i = 0; i < 6u; i++) {                         /* (a few frames a pass: the rest wait in the MAC) */
        int32_t n = fm1_wifi_rx(con_wifi_buf, CON_WIFI_MAX);
        const uint8_t *f = (const uint8_t *)con_wifi_buf + 4u;
        uint32_t mpdu;
        if (n <= 0)
            break;
        now = wl_now();                                /* (the arrival: as near the DMA as the main loop gets) */
        mpdu = fm1_wifi_rx_mpdu_len(f);
        if (mpdu + FM1_WIFI_RXWI <= (uint32_t)n)
            wl_rx(f + FM1_WIFI_RXWI, mpdu, now);
    }
    now = wl_now();
    tx_len = wifi_ip_poll(&wl.ip, now, (uint8_t *)wl_txb + FM1_WIFI_TX_HDR);
    if (tx_len)
        wl_send(tx_len);
    lk_poll(&wl_lk, now);
    wl_clock(now);
}

static void wl_attach(const struct wifi_sta *s)        /* wifijoin ended connected: keep the station for the service */
{
    wl.on = 0;
    wl.sta = *s;
    memset(wl.sta.pmk, 0, sizeof wl.sta.pmk);          /* (the PTK and GTK are all the service needs) */
    wl.joined = 1;
}

static void con_wl_ip(const char *name, uint32_t ip)
{
    con_puts(name);
    con_putc(' ');
    con_dec((int32_t)(ip >> 24));
    con_putc('.');
    con_dec((int32_t)(ip >> 16 & 255u));
    con_putc('.');
    con_dec((int32_t)(ip >> 8 & 255u));
    con_putc('.');
    con_dec((int32_t)(ip & 255u));
    con_puts("\r\n");
}

/* wifilink: after wifijoin, DHCP + Link (the service runs from the main loop from here on); each call prints the state:
 * peers, tempo, the clock followed (following 4 = LINK), the receive counters. `wifilink off` says goodbye to the session. */
static void con_wifilink(const char *p)
{
    int64_t now;
    int32_t mpb;
    if (!wl.joined) {
        con_puts("not joined: wifiup, wifijoin <ssid> <passphrase> first\r\n");
        return;
    }
    if (con_word(&p, "off")) {
        if (wl.on) {
            lk_enable(&wl_lk, 0, wl_now());
            wl.on = wl.feed = wl.started = 0;
        }
        con_puts("link off\r\n");
        return;
    }
    if (!wl.on) {
        uint32_t seed = 0;
        struct lk_cfg c;
        wl.t_ref = fm1_ticks();
        wl.rem = 0;
        wl.us_ref = 1000000;
        now = wl_now();
        ble_hw_rand((uint8_t *)&seed, 4);
        wifi_ip_init(&wl.ip, wl.sta.mac, wl.sta.bssid, ble_hw_rand);
        wl.ip.protect = 1;                             /* (keys are in: the MAC encrypts what carries the Protected bit) */
        c.ip = 0;
        c.port = WIFI_IP_LINK_PORT;
        c.send = wifi_ip_link_send;
        c.ctx = &wl.ip;
        mpb = 60000000 / (song.g[G_BPM] > 20 ? song.g[G_BPM] : 20);
        lk_init(&wl_lk, &c, seed, mpb);
        wifi_ip_set_link(&wl.ip, &wl_lk, 0);
        lk_enable(&wl_lk, 1, now);
        wl.n_rx = wl.n_data = wl.n_uc = wl.n_uc_snap = wl.n_prot = wl.n_kept = wl.n_eapol = 0;
        wl.n_tx = wl.n_tx_err = wl.n_ticks = wl.n_restart = 0;
        wl.feed = wl.started = 0;
        wl_qr = wl_qw = 0;
        wl_send(wifi_ip_dhcp_start(&wl.ip, (uint8_t *)wl_txb + FM1_WIFI_TX_HDR));
        wl.ip.dhcp_t0 = now;
        wl.on = 1;
        con_puts("link on: DHCP sent\r\n");
    }
    con_kv("dhcp", (int32_t)wl.ip.dhcp_state);            /* 1 discovering, 2 requesting, 3 bound, 4 failed */
    con_wl_ip("ip", wl.ip.ip);
    con_wl_ip("gateway", wl.ip.gw);
    con_kv("peers", (int32_t)lk_peers(&wl_lk));
    mpb = lk_mpb(&wl_lk);
    con_kv("bpm_x10", mpb > 0 ? (int32_t)(600000000 / mpb) : 0);
    con_kv("playing", lk_playing(&wl_lk));
    con_kv("following", (int32_t)sy.src);              /* the clock followed: 4 = LINK (SYNC AUTO or LINK) */
    con_kv("sync", song.g[G_SYNC]);
    con_kv("rx", (int32_t)wl.n_rx);
    con_kv("data", (int32_t)wl.n_data);
    con_kv("unicast_to_us", (int32_t)wl.n_uc);
    con_kv("unicast_decrypted", (int32_t)wl.n_uc_snap);   /* of those: LLC/SNAP came out (the pairwise key works) */
    con_kv("unicast_still_protected", (int32_t)wl.n_prot);
    con_kv("kept_by_filter", (int32_t)wl.n_kept);
    con_kv("eapol", (int32_t)wl.n_eapol);
    con_kv("tx", (int32_t)wl.n_tx);
    con_kv("tx_err", (int32_t)wl.n_tx_err);
    con_kv("clock_ticks", (int32_t)wl.n_ticks);
    con_kv("clock_restarts", (int32_t)wl.n_restart);
}
