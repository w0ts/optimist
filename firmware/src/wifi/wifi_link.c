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
    uint8_t ap;                                        /* MODE AP: the service runs as the access point (wifi_apd.c), no station */
    uint32_t k;                                        /* the next MIDI clock tick: 24 a beat of the session */
    uint32_t t_ref, rem;                               /* TIMER4 at the last wl_now; its ticks since the last whole us */
    int64_t us_ref;                                    /* the microsecond clock at t_ref - rem */
    struct wifi_sta sta;                               /* the joined station (its keys are installed in the MAC) */
    struct wifi_ip ip;
    uint8_t own_playing;                                /* the FM-1's own transport, last pushed to Link */
    int32_t own_mpb;                                    /* .. and its tempo, last pushed to Link (us a beat) */
    uint32_t tx_pn;                                     /* CCMP TX packet number (software encryption); first frame = 1 */
    uint32_t n_rx, n_data, n_uc, n_uc_snap, n_prot, n_kept, n_eapol, n_tx, n_tx_err, n_ticks, n_restart, n_catchup;
} wl IN_POOL;
static struct lk_node wl_lk IN_POOL;
/* NOT IN_POOL: the frame DMA reads main RAM (0x01C08000..) fine - the handshake's and wifitx's buffers live there and
 * their frames reach the AP - but every TX buffer placed in the pool (0x01C20000.., 128 KiB above the SRAM base) went
 * out as garbage: the DMA address likely wraps at 128 KiB and fetched RAMTEXT instead (try25, docs/MEMORY-MAP.md §2) */
#define wl_txb wifi_txb                                 /* the one transmit buffer (wifi_ip.c): every send is from the main loop and waits for the DMA */
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

#if FELUCCA_WIFI_AP
static void wa_send(uint32_t len);                     /* wifi_apd.c: the frame out at the right key, to one client or each */
static void wa_rx(const uint8_t *f, uint32_t len, int64_t now);
static uint32_t wa_udp_srv(struct wifi_ip *wip, const uint8_t *src_mac, const uint8_t *msg, uint32_t n, uint8_t *tx_buf);
#endif
static void wl_send(uint32_t len)                      /* what wl_txb's frame holds, out */
{
#if FELUCCA_WIFI_AP
    if (wl.ap) {
        wa_send(len);
        return;
    }
#endif
#if FELUCCA_WIFI_SWCCMP
    uint8_t *f = (uint8_t *)wl_txb + FM1_WIFI_TX_HDR;
    if (fm1_wifi_sw_ccmp && len >= 24u && f[0] == 0x08u && (f[1] & 0x40u)) {   /* software CCMP (off by default: the MAC encrypts at WCID 1) */
        uint8_t pn[6];
        wl.tx_pn++;
        pn[0] = (uint8_t)wl.tx_pn;
        pn[1] = (uint8_t)(wl.tx_pn >> 8);
        pn[2] = (uint8_t)(wl.tx_pn >> 16);
        pn[3] = (uint8_t)(wl.tx_pn >> 24);
        pn[4] = 0;
        pn[5] = 0;
        len = wc_ccmp_encrypt(wl.sta.ptk.tk, wl.sta.mac, pn, 0, f, 24u, len - 24u);
    }
#endif
    fm1_wifi_tx_prepare(wl_txb, len);
    if (fm1_wifi_tx(wl_txb) == 0)
        wl.n_tx++;
    else
        wl.n_tx_err++;
}

static int wl_q_push(uint32_t st, uint32_t t)          /* 0: the ring is full, the message was NOT queued (retry next pass) */
{
    if (wl_qw - wl_qr >= WL_Q)
        return 0;
    wl_qp[wl_qw % WL_Q] = 0x0Fu | st << 8;             /* (CIN F: one system real-time byte) */
    wl_qt[wl_qw % WL_Q] = t;
    RING_PUBLISH();
    wl_qw++;
    return 1;
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
        if (t < now - 200000) {                        /* late: the main loop stalled, or the session moved */
            if (play && wl.started && lk_beat_at(&wl_lk, now) - ub < 4 * LK_UBEAT) {
                t = now;                               /* a bar or less behind: the missed ticks now, so the sequencer's
                                                        * position catches up with the session instead of staying behind */
                wl.n_catchup++;
            } else {
                wl.feed = 0;                           /* further than a bar (or stopped): start again from the session beat */
                return;
            }
        }
        t_ticks = wl_ticks_of(t);
        if (play && !wl.started && wl.k % 96u == 0u) { /* the next bar line (4 beats: the quantum) */
            if (!wl_q_push(0xFAu, t_ticks))
                break;                                 /* (ring full: this tick and its START next pass) */
            wl.started = 1;
        } else if (!play && wl.started) {
            if (!wl_q_push(0xFCu, t_ticks))
                break;
            wl.started = 0;
        }
        if (!wl_q_push(0xF8u, t_ticks))
            break;                                     /* ring full: keep k, the tick goes out next pass (never dropped) */
        wl.n_ticks++;
        wl.k++;
    }
}

static void wl_push_own(int64_t now)                   /* the FM-1's own PLAY/STOP and tempo, to the Link session */
{
    int32_t mpb;
    if (!wl.ip.ip || lk_peers(&wl_lk) == 0 || sy.src == MSRC_LINK)
        return;                                        /* (no session to tell yet, or Link itself drives us: no echo) */
    mpb = 60000000 / (song.g[G_BPM] > 20 ? song.g[G_BPM] : 20);
    if (mpb != wl.own_mpb) {
        lk_set_tempo(&wl_lk, mpb, now);
        wl.own_mpb = mpb;
    }
    if (song.playing != wl.own_playing) {
        lk_set_playing(&wl_lk, song.playing, lk_beat_at(&wl_lk, now), now);
        wl.own_playing = song.playing;
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
    for (i = 0; i < (wl.ap ? 24u : 6u); i++) {         /* (a few frames a pass: the rest wait in the MAC; the AP answers probes and joins among the neighbours' traffic: more) */
        int32_t n = fm1_wifi_rx(con_wifi_buf, CON_WIFI_MAX);
        const uint8_t *f = (const uint8_t *)con_wifi_buf + 4u;
        uint32_t mpdu;
        if (n <= 0)
            break;
        now = wl_now();                                /* (the arrival: as near the DMA as the main loop gets) */
        mpdu = fm1_wifi_rx_mpdu_len(f);
        if (mpdu + FM1_WIFI_RXWI <= (uint32_t)n) {
#if FELUCCA_WIFI_AP
            if (wl.ap)
                wa_rx(f + FM1_WIFI_RXWI, mpdu, now);
            else
#endif
                wl_rx(f + FM1_WIFI_RXWI, mpdu, now);
        }
    }
    now = wl_now();
    tx_len = wifi_ip_poll(&wl.ip, now, (uint8_t *)wl_txb + FM1_WIFI_TX_HDR);
    if (tx_len)
        wl_send(tx_len);
    lk_poll(&wl_lk, now);
    wl_clock(now);
    wl_push_own(now);
}

static uint32_t wl_peers(void) { return wl.on ? lk_peers(&wl_lk) : 0u; }   /* the Link peers heard (the page, the web tab) */
static void wl_attach(const struct wifi_sta *s)        /* wifijoin ended connected: keep the station for the service */
{
    wl.on = 0;
    wl.sta = *s;
    memset(wl.sta.pmk, 0, sizeof wl.sta.pmk);          /* (the PTK and GTK are all the service needs) */
    wl.joined = 1;
}

#if FELUCCA_WIFI_CON
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
#endif

/* wifilink: after wifijoin, DHCP + Link (the service runs from the main loop from here on); each call prints the state:
 * peers, tempo, the clock followed (following 4 = LINK), the receive counters. `wifilink off` says goodbye to the session. */
static void wl_start(void)                             /* the station is joined: DHCP and Link, from the main loop's wl_poll */
{
    int64_t now;
    int32_t mpb;
    uint32_t seed = 0;
    struct lk_cfg c;
    wl.t_ref = fm1_ticks();
    wl.rem = 0;
    wl.us_ref = 1000000;
    now = wl_now();
    ble_hw_rand((uint8_t *)&seed, 4);
    wl.ap = 0;
    wifi_ip_init(&wl.ip, wl.sta.mac, wl.sta.bssid, ble_hw_rand);
    wl.ip.send = wl_send;
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
    wl.n_tx = wl.n_tx_err = wl.n_ticks = wl.n_restart = wl.n_catchup = 0;
    wl.feed = wl.started = 0;
    wl.own_playing = song.playing;                 /* (the state at link-on: only later changes are pushed) */
    wl.own_mpb = mpb;
    wl.tx_pn = 0;                                  /* first software-CCMP frame gets PN 1 (fresh key = fresh AP replay counter) */
    wl_qr = wl_qw = 0;
    wl_send(wifi_ip_dhcp_start(&wl.ip, (uint8_t *)wl_txb + FM1_WIFI_TX_HDR));
    wl.ip.dhcp_t0 = now;
    wl.on = 1;
}
#if FELUCCA_WIFI_AP
static void wl_start_ap(const uint8_t mac[6], uint32_t ip)   /* MODE AP: Link at the AP's own address (no DHCP: the clients ask us) */
{
    int64_t now;
    int32_t mpb;
    uint32_t seed = 0;
    struct lk_cfg c;
    wl.t_ref = fm1_ticks();
    wl.rem = 0;
    wl.us_ref = 1000000;
    now = wl_now();
    ble_hw_rand((uint8_t *)&seed, 4);
    wl.ap = 1;
    wifi_ip_ap_init(&wl.ip, mac, ip, ble_hw_rand);
    wl.ip.protect = 1;
    wl.ip.send = wl_send;
    wl.ip.udp_srv = wa_udp_srv;
    c.ip = 0;
    c.port = WIFI_IP_LINK_PORT;
    c.send = wifi_ip_link_send;
    c.ctx = &wl.ip;
    mpb = 60000000 / (song.g[G_BPM] > 20 ? song.g[G_BPM] : 20);
    lk_init(&wl_lk, &c, seed, mpb);
    wifi_ip_set_link(&wl.ip, &wl_lk, 0);
    lk_enable(&wl_lk, 1, now);
    wl.n_rx = wl.n_data = wl.n_uc = wl.n_uc_snap = wl.n_prot = wl.n_kept = wl.n_eapol = 0;
    wl.n_tx = wl.n_tx_err = wl.n_ticks = wl.n_restart = wl.n_catchup = 0;
    wl.feed = wl.started = 0;
    wl.own_playing = song.playing;
    wl.own_mpb = mpb;
    wl.tx_pn = 0;
    wl_qr = wl_qw = 0;
    wl.on = 1;
}
#endif
static void wl_stop(void)                              /* Link and the service off (the station stays joined as far as the AP knows) */
{
    if (wl.on) {
        lk_enable(&wl_lk, 0, wl_now());
        wl.on = wl.feed = wl.started = 0;
    }
}

#if FELUCCA_WIFI_CON
static void con_wifilink(const char *p)
{
    int32_t mpb;
    if (!wl.joined) {
        con_puts("not joined: wifiup, wifijoin <ssid> <passphrase> first\r\n");
        return;
    }
    if (con_word(&p, "off")) {
        wl_stop();
        con_puts("link off\r\n");
        return;
    }
    if (!wl.on) {
        wl_start();
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
    con_kv("clock_catchups", (int32_t)wl.n_catchup);
}

/* wifibig: try10 (real) hardware result (docs/WIFI-LINK-HANDOVER.md) left the size-vs-content question open - three
 * different CCMP buffer layouts for DHCP's ~250-320 byte protected frame all produced the identical ~6-byte garbage
 * on air, while every short protected/unprotected frame tried (EAPOL, IGMP) has worked. This sends one protected
 * broadcast UDP datagram, same size class as a DHCP Discover but with trivial known content (an incrementing byte
 * pattern, not DHCP's option structure), to a port nobody listens on (9, discard). A capture of this frame settles
 * the open question directly: identical truncation again implicates frame size / the TX DMA-queue path (not DHCP's
 * specific content or offset math); an intact frame on the wire would instead point back at something DHCP-specific
 * in build_dhcp_msg. Needs `wifilink` run at least once first (wl.ip must be initialized with protect=1) */
static void con_wifibig(const char *p)
{
    uint8_t payload[236];                               /* ~300 B total frame once headers/CCMP overhead are added,
                                                          * matching a DHCP Discover's on-air size */
    uint32_t i, len, n = sizeof payload, v;
    int ok;
    if (!wl.on) {
        con_puts("not linked: wifilink first\r\n");
        return;
    }
    v = con_num(&p, &ok);
    if (ok && v > 0u && v <= sizeof payload)
        n = v;
    for (i = 0; i < n; i++)
        payload[i] = (uint8_t)i;
    len = wifi_ip_send_udp(&wl.ip, 0xFFFFFFFFu, 9u, payload, n, (uint8_t *)wl_txb + FM1_WIFI_TX_HDR);
    wl_send(len);
    con_kv("payload", (int32_t)n);
    con_kv("frame_len", (int32_t)len);
    con_kv("tx", (int32_t)wl.n_tx);
    con_kv("tx_err", (int32_t)wl.n_tx_err);
}
#endif /* FELUCCA_WIFI_CON */
