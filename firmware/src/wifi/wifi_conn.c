/* SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0 */
/* The Wi-Fi connection manager (docs/WIFI-UI-DESIGN.md): the radio, the scan, the join and Link, one small step a main-loop
 * pass, never a loop that waits. Included by io/console.c after wifi_link.c; wc_poll runs beside wl_poll (system/main.c).
 * The SYSTEM > WI-FI page (ui/optimist/op_wifi.c) drives it and shows it; the console's wifijoin / wifigo are the blocking
 * test tools beside it (they hold the main loop for seconds: wc_enable(0) first).
 *
 *   MODE OFF   nothing runs; the radio, once started, stays up (it goes off only by a restart).
 *   MODE MAN   the radio is up; the page scans while it is on screen; SAVE on a row joins that AP.
 *   MODE AUTO  as MAN, and the best saved network heard is joined, at start-up and whenever none is joined.
 *
 * States: OFF, RADIO (starting it), IDLE (up, doing nothing), SCAN (channel after channel, 150 ms each, into the table),
 * PMK (the password's key, 256 of the 8192 rounds a pass), AUTH, ASSOC, HS (the 4-way, 6 s), UP (joined: wl_poll runs
 * DHCP and Link), FAIL (why: wc.fail, until the next try). A join that completes is saved (wn_put: SSID + key). */
#define WC_CH_MS 150u                                   /* one channel's listening in a scan */
#define WC_STEP_MS 400u                                 /* auth / assoc: the request again after this */
#define WC_STEP_TRIES 6u                                /* .. up to this many sends (the uplink is weak: a long frame is lost often) */
#define WC_ATTEMPTS 3u                                  /* the whole join, from AUTH, this many times before FAIL */
#define WC_HS_MS 6000u                                  /* the 4-way's whole window */
#define WC_PRE_MS 150u                                  /* after the deauth: the AP drops its record of us */
#define WC_LINK_MS 300u                                 /* M4 sent -> DHCP: the AP needs this to take the key into use */
#define WC_RETRY_MS 20000u                              /* AUTO: another round after a failure or nothing found */
enum { WC_OFF, WC_RADIO, WC_IDLE, WC_SCAN, WC_PMK, WC_AUTH, WC_ASSOC, WC_HS, WC_UP, WC_FAIL, WC_LINK, WC_PRE };   /* PRE: the
                                                         * deauth sent, the AP given a moment before AUTH */   /* LINK: joined, a
                                                         * moment before DHCP (the AP opens its port after M4) */
enum { WCF_NONE, WCF_NOCAL, WCF_NOAP, WCF_AUTH, WCF_ASSOC, WCF_HS, WCF_KEY };   /* why a join failed: WCF_KEY = a key frame failed
                                                         * its check (the password is wrong) */
static struct {
    uint8_t st, fail, ch, tries;
    uint8_t scan_want;                                  /* the page asks for a scan (it is on screen, nothing joined) */
    uint8_t saved_join;                                 /* the join in hand uses a saved key (no password was typed) */
    uint8_t joined_slot;                                /* 1 + the saved slot joined (0: none), for the page */
    uint8_t attempt;                                    /* the join's attempt (1..WC_ATTEMPTS) */
    uint8_t fail_new;                                   /* a failure the page has not said yet (it says it once) */
    uint8_t d_sub, d_stat;                              /* the last frame from the AP in a join: its first byte, and an auth /
                                                         * assoc response's status code (diagnostics: wifiman) */
    uint16_t d_n, d_mg;                                 /* frames handled in this join; of them management frames from the AP */
    uint32_t t0, t_retry, sweeps;                       /* fm1_ms of this state's start / the last failure; scan sweeps done */
    struct wn_ap ap;                                    /* the AP being joined */
    struct wifi_sta sta;
    struct wc_pmk_job job;
    uint8_t pmk[32];
} wc IN_POOL;
/* a request by SSID (the web editor, the page's typed password): scan, the strongest AP of it, join, save on success.
 * pass[0] == 0 and a saved network of that name: its key is used. One at a time; the password is wiped once the key job has it */
static struct {
    uint8_t on, sweeps_left, ssid_len;
    uint8_t ssid[WN_SSID_MAX];
    char pass[64];
} wc_req IN_POOL;
static struct wn_scan wc_tab IN_POOL;                   /* the APs heard (main loop only, no DMA: the pool is fine) */
#define wc_txb wifi_txb                                 /* a frame out: the one transmit buffer (wifi_ip.c: main RAM, the DMA reads it) */

/* the radio and the MAC for Wi-Fi: 1 started here (no Bluetooth), 2 already up (BT started it), -1 no stored trims */
static int wifi_radio_up(void)
{
    struct ble_rf_trims t;
    int r = 2;
    if (!ble_up && !rfs_is_up()) {
        if (!ble_rf_copy_ok(ble_rf_kept))
            return -1;
        ble_rf_copy_get(ble_rf_kept, &t);
#if FELUCCA_DUAL >= 2
        dual_radio_hold();
#endif
        (void)rfs_rf_up(t.x106, t.x107, t.x108, t.x187);
#if FELUCCA_DUAL >= 2
        dual_radio_release();
#endif
        r = 1;
    }
    fm1_wifi_sta_settings();
    fm1_wifi_wlc_setup();
    return r;
}
static int wc_radio_is_up(void) { return rfs_is_up() && (fm1_wifi_rd(FM1_WL_EN) & FM1_WL_EN_WIFI); }

static int wc_mode_off(void) { return wn_st.mode == WN_MODE_OFF || wn_st.mode == WN_MODE_AP || wn_st.mode == WN_MODE_AP_MAN; }   /* (MODE AP: the radio is the access point's, wifi_apd.c; the manager rests) */
static void wc_to(uint32_t st) { wc.st = (uint8_t)st; wc.t0 = fm1_ms; }
static void wc_fail(uint32_t why) { wc.fail = (uint8_t)why; wc.fail_new = 1; wc_to(WC_FAIL); wc.t_retry = fm1_ms; }
static void wc_send(uint32_t len)                       /* wc_txb's frame (the station's own builders wrote it), out */
{
    fm1_wifi_tx_prepare(wc_txb, len);
    (void)fm1_wifi_tx(wc_txb);
}
static uint8_t *wc_frame(void) { return (uint8_t *)wc_txb + FM1_WIFI_TX_HDR; }

/* the received frames waiting, each given to f(frame, mpdu_len, rssi): up to max a pass (a join needs its one answer through
 * whatever else the channel carries: 24; the scan does not mind losing some) */
static void wc_rx(void (*f)(const uint8_t *, uint32_t, int8_t), uint32_t max)
{
    uint32_t i;
    for (i = 0; i < max; i++) {
        int32_t n = fm1_wifi_rx(con_wifi_buf, CON_WIFI_MAX);
        const uint8_t *fr = (const uint8_t *)con_wifi_buf + 4u;
        uint32_t mpdu;
        if (n <= 0)
            return;
        mpdu = fm1_wifi_rx_mpdu_len(fr);
        if (mpdu + FM1_WIFI_RXWI <= (uint32_t)n)
            f(fr + FM1_WIFI_RXWI, mpdu, (int8_t)fm1_wifi_rx_rssi(fr));
    }
}
static void wc_on_scan(const uint8_t *f, uint32_t len, int8_t rssi)
{
    struct wn_ap ap;
    if (wn_parse_beacon(f, len, wc.ch, rssi, &ap))
        wn_scan_add(&wc_tab, &ap);
}
static void wc_on_join(const uint8_t *f, uint32_t len, int8_t rssi)
{
    uint32_t tx_len = 0;
    (void)rssi;
    if (wc.sta.state == WIFI_STA_STATE_CONNECTED)
        return;                                         /* (joined: what follows, the AP's group key frame, is wl_poll's) */
    if (len >= 30u && (f[0] & 0x0Cu) == 0u && !memcmp(f + 4, wc.sta.mac, 6) && !memcmp(f + 10, wc.ap.bssid, 6)) {   /* a
                                                         * management frame from our AP TO US (the AP also answers other clients):
                                                         * kept for the diagnostics */
        wc.d_sub = f[0];
        if (f[0] == 0xB0u)
            wc.d_stat = f[28];                          /* auth response: status */
        else if (f[0] == 0x10u && len >= 28u)
            wc.d_stat = f[26];                          /* assoc response: status */
        wc.d_mg++;
    }
    wc.d_n++;
    if (wifi_sta_rx_frame(&wc.sta, f, len, wc_frame(), &tx_len) == 1 && tx_len)
        wc_send(tx_len);                                /* (M2, M4: the station built them) */
}

/* ---- the page's and AUTO's requests */
static void wc_leave(void)                              /* joined (UP, LINK): tell the AP (deauth, reason 3), the MAC's key cleared first so
                                                         * the frame goes out plain; then Link and the service stop */
{
    if (wc.st == WC_UP || wc.st == WC_LINK) {
        wifi_sta_clear_hw_keys();
        wc_send(wifi_sta_build_deauth(&wc.sta, wc_frame()));
    }
    wl_stop();
    wl.joined = 0;
    wc.joined_slot = 0;
    memset(&wc_req, 0, sizeof wc_req);                  /* (a request still waiting for its AP: and its password: dropped) */
}
static void wc_enable(int on)                           /* MODE OFF / MAN / AUTO: the radio on or the manager off */
{
    if (!on) {
        wc_leave();
        wc.scan_want = 0;
        wc_to(WC_OFF);
        return;
    }
    if (wc.st == WC_OFF || wc.st == WC_FAIL)
        wc_to(WC_RADIO);
}
static void wc_disconnect(void)                         /* UP: let go (the AP told); the scan can run again */
{
    wc_leave();
    wc_to(WC_IDLE);
}
static void wc_scan_clear(void) { wn_scan_clear(&wc_tab); wc.sweeps = 0; }
/* join the AP: a saved key (pmk), or a password to derive the key from (pass; copied, then wiped by the job) */
static void wc_join(const struct wn_ap *ap, const uint8_t *pmk, const char *pass)
{
    if (wc.st == WC_OFF || wc.st == WC_RADIO)
        return;
    wc_leave();                                         /* (one AP at a time: the old one told) */
    wifi_sta_clear_hw_keys();                           /* (the MAC as at boot: no key left from the last join) */
    wc.ap = *ap;
    wc.d_sub = wc.d_stat = 0;
    wc.d_n = wc.d_mg = 0;
    wc.attempt = 1;
    if (pmk) {
        memcpy(wc.pmk, pmk, 32);
        wc.saved_join = 1;
        wc_to(WC_PMK);                                  /* (no rounds to do: the next pass goes on) */
        wc.job.done = 1;
    } else {
        wc.saved_join = 0;
        wc_pmk_begin(&wc.job, pass, ap->ssid, ap->ssid_len);
        wc_to(WC_PMK);
    }
}
static void wc_start_auth(void);
static void wc_retry_or_fail(uint32_t why)           /* the join again from AUTH (a new SNonce) while attempts are left */
{
    if (wc.attempt < WC_ATTEMPTS) {
        wc.attempt++;
        wc_start_auth();
        return;
    }
    wc_fail(why);
}
static void wc_start_auth(void)
{
    uint8_t mac[6];
    wifi_sta_get_mac(mac);
    wifi_sta_set_mac_regs(mac, wc.ap.bssid, wc.ap.ch);
    wifi_sta_init(&wc.sta, mac, ble_hw_rand);
    wifi_sta_set_pmk(&wc.sta, wc.pmk, wc.ap.ssid, wc.ap.ssid_len);
    wifi_sta_set_ap(&wc.sta, wc.ap.bssid);
    wc.sta.state = WIFI_STA_STATE_AUTH;
    wc.tries = 0;
    wc_to(WC_PRE);
    wc_send(wifi_sta_build_deauth(&wc.sta, wc_frame()));   /* (an association left from before, a reboot: gone) */
}
static void wc_drop_frame(const uint8_t *f, uint32_t len, int8_t rssi) { (void)f, (void)len, (void)rssi; }


/* ask for a join by name; 0 accepted, -1 the manager is off (MODE OFF) or the name is bad, -2 no password and not a saved network,
 * -3 the radio is not up yet */
static int wc_request(const uint8_t *ssid, uint32_t len, const char *pass)
{
    uint32_t i;
    if (wc_mode_off() || len < 1u || len > WN_SSID_MAX)
        return -1;
    if (wc.st == WC_OFF || wc.st == WC_RADIO)
        return -3;                                      /* (the radio is not up yet: a scan would listen to nothing; ask again) */
    if ((!pass || !pass[0]) && wn_find(&wn_st, ssid, len) < 0)
        return -2;
    wc_leave();                                         /* (one network at a time: the old one told, any older request dropped) */
    memset(&wc_req, 0, sizeof wc_req);
    memcpy(wc_req.ssid, ssid, len);
    wc_req.ssid_len = (uint8_t)len;
    if (pass)
        for (i = 0; pass[i] && i < 63u; i++)
            wc_req.pass[i] = pass[i];
    wc_req.on = 1;
    wc_req.sweeps_left = 3u;                            /* sweeps to find it before NO AP */
    wc.joined_slot = 0;
    wc_scan_clear();
    wc.ch = 1;
    fm1_wifi_mac_wr(FM1_WIFI_RX_FILTER, FM1_WIFI_RX_FILTER_ALL);
    fm1_wifi_set_channel(wc.ch);
    wc_to(WC_SCAN);
    return 0;
}

/* AUTO: the strongest AP of the most recently used saved network that was heard (WPA2-PSK only), or -1 */
static int wc_auto_pick(int *slot)
{
    uint32_t i;
    for (i = 0; i < WN_SLOTS; i++) {
        int a;
        if (wn_st.n[i].used != 1u)
            continue;
        a = wn_scan_best_for(&wc_tab, wn_st.n[i].ssid, wn_st.n[i].ssid_len);
        if (a >= 0 && wc_tab.ap[a].sec == WN_SEC_PSK) {
            *slot = (int)i;
            return a;
        }
    }
    return -1;
}

static void wc_poll(void)                               /* once a main-loop pass: one step */
{
    uint32_t now = fm1_ms;
    if (wc_mode_off() && wc.st != WC_OFF && wc.st != WC_RADIO) {   /* MODE OFF, in any state: everything stops */
        wc_enable(0);
        return;
    }
    switch (wc.st) {
    case WC_OFF:
        if (!wc_mode_off())
            wc_to(WC_RADIO);                            /* (the saved MODE: at start-up, and when the page sets it) */
        break;
    case WC_RADIO:
        if (wc_mode_off()) {
            wc_to(WC_OFF);
            break;
        }
        if (now < 3000u)
            break;                                      /* (the start-up's own work first: the radio is started once, not at 0 s) */
        if (wifi_radio_up() < 0) {
            wc_fail(WCF_NOCAL);
            break;
        }
        wc_scan_clear();
        wc_to(WC_IDLE);
        break;
    case WC_IDLE:
    case WC_FAIL:
        if (wc_mode_off()) {
            wc_enable(0);
            break;
        }
        if (wc.st == WC_FAIL && now - wc.t_retry < WC_RETRY_MS && wn_st.mode == WN_MODE_AUTO && !wc.scan_want)
            break;
        if (wc.st == WC_FAIL && wc.fail == WCF_NOCAL)
            break;                                      /* (no radio calibration stored: nothing to scan with) */
        if (wc.scan_want || (wn_st.mode == WN_MODE_AUTO && wn_count(&wn_st) && now - wc.t_retry >= WC_RETRY_MS)) {
            wc.ch = 1;
            fm1_wifi_mac_wr(FM1_WIFI_RX_FILTER, FM1_WIFI_RX_FILTER_ALL);
            fm1_wifi_set_channel(wc.ch);
            wc_to(WC_SCAN);
        }
        break;
    case WC_SCAN: {
        int slot = 0, a;
        wc_rx(wc_on_scan, 8u);
        if (now - wc.t0 < WC_CH_MS)
            break;
        if (wc.ch < 13u) {
            wc.ch++;
            fm1_wifi_set_channel(wc.ch);
            wc.t0 = now;
            break;
        }
        wc.sweeps++;                                    /* a sweep done */
        wc.t_retry = now;
        if (wc_req.on) {                                /* a request by name: its strongest AP, or another sweep, or NO AP */
            a = wn_scan_best_for(&wc_tab, wc_req.ssid, wc_req.ssid_len);
            if (a >= 0) {
                int sv = wn_find(&wn_st, wc_req.ssid, wc_req.ssid_len);
                wc_req.on = 0;
                if (!wc_req.pass[0] && sv < 0) {        /* (forgotten while it was being looked for) */
                    memset(&wc_req, 0, sizeof wc_req);
                    wc_fail(WCF_NOAP);
                    break;
                }
                {
                    char pw[64];                        /* (a copy: wc_join's wc_leave wipes wc_req, and the key job takes this one) */
                    memcpy(pw, wc_req.pass, sizeof pw);
                    memset(&wc_req, 0, sizeof wc_req);
                    if (pw[0])
                        wc_join(&wc_tab.ap[a], 0, pw);
                    else
                        wc_join(&wc_tab.ap[a], wn_st.n[sv].pmk, 0);
                    memset(pw, 0, sizeof pw);           /* (the key job has copied the password: none is kept) */
                }
            } else if (--wc_req.sweeps_left == 0u) {
                memset(&wc_req, 0, sizeof wc_req);
                wc_fail(WCF_NOAP);
            } else {
                wc.ch = 1;
                fm1_wifi_set_channel(wc.ch);
                wc.t0 = now;
            }
            break;
        }
        if (wn_st.mode == WN_MODE_AUTO && !wl.joined && (a = wc_auto_pick(&slot)) >= 0) {
            wc_join(&wc_tab.ap[a], wn_st.n[slot].pmk, 0);
            break;
        }
        if (wc.scan_want) {                             /* the page is on screen: another sweep, the table kept */
            wc.ch = 1;
            fm1_wifi_set_channel(wc.ch);
            wc.t0 = now;
        } else {
            wc_to(WC_IDLE);
        }
        break;
    }
    case WC_PMK:
        if (wc_pmk_step(&wc.job, 256u, wc.pmk))
            wc_start_auth();
        break;
    case WC_PRE:
        wc_rx(wc_drop_frame, 24u);                      /* (whatever the AP still sends from the old association) */
        if (now - wc.t0 >= WC_PRE_MS) {
            wc_to(WC_AUTH);
            wc_send(wifi_sta_build_auth(&wc.sta, wc_frame()));
            wc.tries = 1;
        }
        break;
    case WC_AUTH:
        wc_rx(wc_on_join, 24u);
        if (wc.sta.state == WIFI_STA_STATE_ASSOC) {
            wc_to(WC_ASSOC);
            wc_send(wifi_sta_build_assoc_req(&wc.sta, wc_frame()));
            wc.tries = 1;
        } else if (now - wc.t0 >= WC_STEP_MS) {
            if (wc.tries >= WC_STEP_TRIES) {
                wc_retry_or_fail(WCF_AUTH);
            } else {
                wc.tries++;
                wc.t0 = now;
                wc_send(wifi_sta_build_auth(&wc.sta, wc_frame()));
            }
        }
        break;
    case WC_ASSOC:
        wc_rx(wc_on_join, 24u);
        if (wc.sta.state == WIFI_STA_STATE_WAIT_M1) {
            wc_to(WC_HS);
        } else if (now - wc.t0 >= WC_STEP_MS) {
            if (wc.tries >= WC_STEP_TRIES) {
                wc_retry_or_fail(WCF_ASSOC);
            } else {
                wc.tries++;
                wc.t0 = now;
                wc_send(wifi_sta_build_assoc_req(&wc.sta, wc_frame()));
            }
        }
        break;
    case WC_HS:
        wc_rx(wc_on_join, 24u);
        if (wc.sta.state == WIFI_STA_STATE_CONNECTED) {
            int s;
            wl_attach(&wc.sta);
            if (wn_put(&wn_st, wc.ap.ssid, wc.ap.ssid_len, wc.sta.pmk) == 0) {
                settings_later = 1;
                s = wn_find(&wn_st, wc.ap.ssid, wc.ap.ssid_len);
                wc.joined_slot = (uint8_t)(s + 1);
            }
            wc_to(WC_LINK);
        } else if (wc.sta.state == WIFI_STA_STATE_FAILED) {
            wc_retry_or_fail(WCF_KEY);                  /* (a key frame failed its check: a frame corrupted on the weak link as often as a wrong
                                                         * password: retried, and called WRONG PASSWORD only when every attempt ends so) */
        } else if (now - wc.t0 >= WC_HS_MS) {
            wc_retry_or_fail(WCF_HS);
        }
        break;
    case WC_LINK:
        wc_rx(wc_on_join, 24u);                         /* (nothing is done with them: the station is connected) */
        if (now - wc.t0 >= WC_LINK_MS) {
            wl_start();                                 /* DHCP, and Link once there is an address */
            wc_to(WC_UP);
        }
        break;
    case WC_UP:
        break;                                          /* (wl_poll serves the link) */
    default:
        break;
    }
}
