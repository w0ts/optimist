/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Serial console on the CDC-ACM function (FELUCCA_CDC=1): read-only
 * diagnostics. Runs in the main loop (cdc_task); usb_poll moves the bytes.
 * The baud rate is ignored. Nothing here writes memory or flash; `uboot`
 * does what the SysEx soft key does. */
#define CON_LINE 64u
#include "console_parse.h"                   /* con_word, con_num, con_run_parse (host-tested: tests/console_parse_test.c) */

static struct {
    char line[CON_LINE];
    uint32_t len;
    uint8_t dtr_seen, stalled;   /* stalled: the host stopped reading, drop the rest of this reply */
} con;

static void con_putc(char c)
{
    uint32_t t0 = fm1_ms;
    while (co_w - co_r >= CO_N) {                      /* full: wait for usb_poll, briefly */
        if (!cdc.dtr || con.stalled || fm1_ms - t0 > 20u) {
            con.stalled = 1;
            return;
        }
        fm1_wdt_feed();
    }
    cdc_out[co_w % CO_N] = (uint8_t)c;
    RING_PUBLISH();
    co_w++;
}

static void con_puts(const char *s)
{
    while (*s)
        con_putc(*s++);
}

static void con_hex(uint32_t v, uint32_t digits)
{
    while (digits--)
        con_putc("0123456789ABCDEF"[(v >> (digits * 4u)) & 15u]);
}

static void con_dec(int32_t v)
{
    char b[12];
    uint32_t n = 0, u = v < 0 ? (uint32_t)-v : (uint32_t)v;
    if (v < 0)
        con_putc('-');
    do
        b[n++] = (char)('0' + u % 10u);
    while ((u /= 10u) != 0 && n < sizeof b);
    while (n)
        con_putc(b[--n]);
}

static void con_kv(const char *k, int32_t v)            /* "key value\r\n" */
{
    con_puts(k);
    con_putc(' ');
    con_dec(v);
    con_puts("\r\n");
}

static void con_kx(const char *k, uint32_t v)
{
    con_puts(k);
    con_puts(" 0x");
    con_hex(v, 8);
    con_puts("\r\n");
}

static void con_memr(const char *p)
{
    int ok, ok2;
    uint32_t a = con_num(&p, &ok), n = con_num(&p, &ok2), i;
    if (!ok) {
        con_puts("usage: memr ADDR [LEN<=256]\r\n");
        return;
    }
    if (!ok2 || !n)
        n = 64;
    if (n > 256u)
        n = 256;
    if (!fm1_mem_readable(a, n)) {
        con_puts("only RAM 01C00000-01C80000 and XIP 02000000-02100000\r\n");
        return;
    }
    for (i = 0; i < n; i++) {
        if (i % 16u == 0) {
            con_hex(a + i, 8);
            con_putc(':');
        }
        con_putc(' ');
        con_hex(fm1_peek8(a + i), 2);
        if (i % 16u == 15u || i + 1u == n)
            con_puts("\r\n");
    }
}

#if FELUCCA_FLASH
static void con_flr(const char *p)                  /* flash read over SPI (no XIP decryption) */
{
    static uint8_t b[256];
    int ok, ok2;
    uint32_t a = con_num(&p, &ok), n = con_num(&p, &ok2), i;
    if (!ok || a < FL_APP_HI || a >= 0x100000u) {
        con_puts("usage: flr OFFSET [LEN<=256]   (the app end..0xFFFFF)\r\n");
        return;
    }
    if (!ok2 || !n)
        n = 64;
    if (n > 256u)
        n = 256;
    if (a + n > 0x100000u)
        n = 0x100000u - a;
    if (!flash_ok || st_read(a, b, n)) {
        con_puts("flash not available\r\n");
        return;
    }
    for (i = 0; i < n; i++) {
        if (i % 16u == 0) {
            con_hex(a + i, 6);
            con_putc(':');
        }
        con_putc(' ');
        con_hex(b[i], 2);
        if (i % 16u == 15u || i + 1u == n)
            con_puts("\r\n");
    }
}
#endif

#if FELUCCA_BLE
/* the radio's stored calibration (midi_ble.c, ble/ble_vm.c; docs/BLE-HW-FACTS.md §14, §15.4; docs/BLE-STACK.md §8.2): read only */
static void con_bytes(const char *k, const uint8_t *d, uint32_t n)   /* "key: XX XX ..", 16 a line */
{
    uint32_t i;
    con_puts(k);
    for (i = 0; i < n; i++) {
        if (i && i % 16u == 0)
            con_puts("\r\n    ");
        con_putc(' ');
        con_hex(d[i], 2);
    }
    con_puts("\r\n");
}

static void con_vm_rec(void *ctx, uint32_t off, uint32_t id, uint32_t len)
{
    (void)ctx;
    con_putc('@');
    con_hex(off, 6);
    con_puts(" id ");
    con_dec((int32_t)id);
    con_puts(" len ");
    con_dec((int32_t)len);
    con_puts("\r\n");
}

static void con_trims(const struct ble_rf_trims *t, uint8_t have)
{
    if (have & 1u)
        con_bytes("106:", t->x106, sizeof t->x106);
    if (have & 2u)
        con_bytes("107:", t->x107, sizeof t->x107);
    if (have & 4u)
        con_bytes("108:", t->x108, sizeof t->x108);
    if (have & 8u)
        con_bytes("187:", t->x187, sizeof t->x187);
}

#ifndef FELUCCA_WIFI_CON
#define FELUCCA_WIFI_CON 0                            /* the console's Wi-Fi test commands (OPTIMIST_WIFI=1; =2 builds without them) */
#endif
#ifndef FELUCCA_WIFI
#define FELUCCA_WIFI 0                                /* Wi-Fi (work in progress, docs/WIFI-LINK-HANDOVER.md): a test build
                                                       * sets it with OPTIMIST_WIFI=1 (tools/build.py); no builder item yet */
#endif
#if BLE_HW_WL82 && FELUCCA_WIFI
#include "../../hal/fm1_wifi.h"
#include "../wifi/wifi_crypto.c"                      /* (the station: used by wifijoin only, so far) */
#include "../wifi/wifi_sta.c"
#include "../wifi/wifi_ip.c"                          /* ARP, DHCP, IGMP, UDP; the early filter */
#include "../link/link.c"                             /* Ableton Link (the sync part) */

#define CON_WIFI_MAX 1024u                                /* the largest frame taken, with its 16-byte receive header: Link's datagrams are at most 512 B,
                                                           * DHCP and EAPOL under 700, a beacon about 600; a larger one is not for us and is dropped by the
                                                           * receive routine unread (hal/fm1_wifi.h fm1_wifi_rx: n > max). Was 2400, one whole frame (2.4 KB of RAM) */
static uint32_t con_wifi_buf[(CON_WIFI_MAX + 8u) / 4u + 1u];   /* (wifiscan, wifitx and wifijoin share it) */
#include "../wifi/wifi_link.c"                        /* wifilink: DHCP + Link after the join (the service runs from the main loop) */
#include "../wifi/wifi_conn.c"                        /* the connection manager: radio, scan, join, Link, a step a pass (the SYSTEM > WI-FI page) */
#if FELUCCA_WIFI_AP
#include "../wifi/wifi_ap.c"                          /* MODE AP: the authenticator, beacons, management answers */
#include "../wifi/wifi_ap_dhcp.c"                     /* .. its DHCP server */
#include "../wifi/wifi_apd.c"                         /* .. and the radio side: clients, keys, a step a pass */
#endif

#if FELUCCA_WIFI_CON
/* A console command that waits on the radio (a scan, a join) holds the main loop for seconds. The watchdog is fed, and the
 * dual-core starvation clock too: 1.5 s without a main-loop pass stops CPU1 for the rest of the boot (SYSTEM > INFO:
 * "CPU1 overload", system/main.c DUAL_STARVE_MS), which these commands did. The Wi-Fi page waits in no loop at all. */
static void con_wifi_alive(void)
{
    fm1_wdt_feed();
    felucca_dbg.loop_ms = fm1_ms ? fm1_ms : 1u;
}

/* wifiup: the Wi-Fi side brought up as the SDK does on a radio BT has started (usb_register): the radio started by our
 * start-up program if nothing has (BLUETOOTH and BT audio off: as Bluetooth audio starts it, bt_inq.c, CPU1 held), the
 * Ralink STA driver's MAC settings (fm1_wifi_sta_init), the coexistence controller's tables (wlc_main: fm1_wifi_wlc_setup,
 * which sets both RF paths); then wifiscan / wifitx / wifijoin. Off again only by a restart */
static void con_wifiup(void)
{
    int r = wifi_radio_up();
    if (r < 0) {
        con_puts("no stored trims (bletrim)\r\n");
        return;
    }
    con_puts(r == 1 ? "radio started (no BT)\r\n" : "radio up already (BT)\r\n");
    /* try23's PA-gain + 0x14008 writes REGRESSED TX (handshake went 10/10 fail vs try19's intermittent success at the same
     * 3-4 m); reverted. The default pa_mcs_gain table is likely a placeholder the device trim overrides, and/or 0x14008
     * disturbed the PHY. Proper TX-gain needs the device's own pa_trim via wf_rf_init - and a reliable signal measurement
     * to tune against, which we lack. fm1_wifi_pa_gain_init() etc. remain in the HAL for when that's set up. */
    con_kx("wl_en", fm1_wifi_rd(FM1_WL_EN));
    con_kx("rx_filter", fm1_wifi_mac_rd(FM1_WIFI_RX_FILTER));
    con_kx("mac_sys_ctrl", fm1_wifi_mac_rd(0x1004u));
    con_kv("channel", (int32_t)fm1_wifi_channel());
}
/* wifiscan: the networks around, from their beacons (and probe responses): channels 1-13, 150 ms each (a beacon is
 * every 102 ms), the RX filter open meanwhile; each new BSSID listed with its channel (the beacon's DS parameter),
 * the RSSI byte of the receive header and its SSID. Needs the Wi-Fi side up (wifiup). Listens only */
struct con_bss { uint8_t bssid[6], ch; int8_t rssi; char ssid[33]; };
static void con_wifi_beacon(const uint8_t *f, uint32_t n, struct con_bss *t, uint32_t *nt, uint32_t ch)
{
    const uint8_t *h = f + FM1_WIFI_RXWI, *e;
    uint32_t len = fm1_wifi_rx_mpdu_len(f), i;
    struct con_bss b;
    if (len + FM1_WIFI_RXWI > n || len < 36u || (h[0] != 0x80u && h[0] != 0x50u))
        return;                                           /* (beacon 0x80, probe response 0x50) */
    for (i = 0; i < 6u; i++)
        b.bssid[i] = h[16u + i];
    for (i = 0; i < *nt; i++)
        if (!memcmp(t[i].bssid, b.bssid, 6))
            return;
    b.ch = (uint8_t)ch;
    b.rssi = (int8_t)fm1_wifi_rx_rssi(f);
    b.ssid[0] = 0;
    e = h + len;
    for (h += 36u; h + 2u <= e && h + 2u + h[1] <= e; h += 2u + h[1]) {
        if (h[0] == 0u && h[1] <= 32u) {                  /* SSID */
            for (i = 0; i < h[1]; i++)
                b.ssid[i] = (h[2 + i] >= 32u && h[2 + i] < 127u) ? (char)h[2 + i] : '.';
            b.ssid[h[1]] = 0;
        } else if (h[0] == 3u && h[1] == 1u) {           /* DS parameter: the AP's own channel */
            b.ch = h[2];
        }
    }
    if (*nt < 8u)
        t[(*nt)++] = b;
}
static void con_wifiscan(void)
{
    uint32_t *buf = con_wifi_buf;
    static struct con_bss t[8];
    uint32_t nt = 0, ch, frames = 0, i, filt, ch0, t0;
    if (!rfs_is_up() || !(fm1_wifi_rd(FM1_WL_EN) & FM1_WL_EN_WIFI)) {
        con_puts("Wi-Fi not up: wifiup first\r\n");
        return;
    }
    filt = fm1_wifi_mac_rd(FM1_WIFI_RX_FILTER);
    ch0 = fm1_wifi_channel();
    fm1_wifi_mac_wr(FM1_WIFI_RX_FILTER, FM1_WIFI_RX_FILTER_ALL);
    for (ch = 1; ch <= 13u; ch++) {
        fm1_wifi_set_channel(ch);
        t0 = fm1_ticks();
        while (fm1_ticks() - t0 < 150000u * FM1_TICKS_PER_US) {
            int32_t n = fm1_wifi_rx(buf, CON_WIFI_MAX);
            con_wifi_alive();
            if (n < 0)
                continue;                                 /* (a frame too long for the buffer: skipped) */
            if (n > 0) {
                frames++;
                con_wifi_beacon((const uint8_t *)buf + 4u, (uint32_t)n, t, &nt, ch);
            }
        }
    }
    fm1_wifi_set_channel(ch0);
    fm1_wifi_mac_wr(FM1_WIFI_RX_FILTER, filt);
    con_kv("frames", (int32_t)frames);
    for (i = 0; i < nt; i++) {
        uint32_t k;
        con_puts("bss ");
        for (k = 0; k < 6u; k++) {
            con_hex(t[i].bssid[k], 2);
            con_putc(k < 5u ? ':' : ' ');
        }
        con_puts("ch ");
        con_hex(t[i].ch, 2);
        con_puts(" rssi ");
        con_hex((uint8_t)t[i].rssi, 2);
        con_puts(" \"");
        con_puts(t[i].ssid);
        con_puts("\"\r\n");
    }
}
/* wifidata: after wifijoin, 3 s of the data frames on the joined channel (the station RX filter left as wifijoin set it):
 * each one's frame control, its protected bit and 16 bytes after the 802.11 header (24, or 26 with QoS). Frames whose
 * payload starts with LLC/SNAP AA AA 03 came in decrypted: the MAC's CCMP and the installed keys work. Listens only */
static void con_wifidata(void)
{
    uint32_t *buf = con_wifi_buf, t0, i, n_data = 0, n_snap = 0, n_shown = 0;
    if (!rfs_is_up() || !(fm1_wifi_rd(FM1_WL_EN) & FM1_WL_EN_WIFI)) {
        con_puts("Wi-Fi not up: wifiup and wifijoin first\r\n");
        return;
    }
    t0 = fm1_ticks();
    while (fm1_ticks() - t0 < 3000000u * FM1_TICKS_PER_US) {
        int32_t n = fm1_wifi_rx(buf, CON_WIFI_MAX);
        const uint8_t *f = (const uint8_t *)buf + 4u, *h = f + FM1_WIFI_RXWI, *p;
        uint32_t hl;
        con_wifi_alive();
        if (n <= 0 || (h[0] & 0x0Cu) != 0x08u)
            continue;                                     /* (data frames only) */
        n_data++;
        hl = (h[0] & 0x80u) ? 26u : 24u;                  /* (QoS data: 2 more bytes) */
        p = h + hl;
        if (p[0] == 0xAAu && p[1] == 0xAAu && p[2] == 0x03u)
            n_snap++;
        if (n_shown++ < 6u) {
            con_hex(h[0], 2);
            con_hex(h[1], 2);
            con_puts((h[1] & 0x40u) ? " prot " : " open ");
            for (i = 0; i < 16u; i++)
                con_hex(p[i], 2);
            con_puts("\r\n");
        }
    }
    con_kv("data_frames", (int32_t)n_data);
    con_kv("llc_snap", (int32_t)n_snap);
}

/* wifipow [N]: read, or set, the TX power byte (byte 2 of 0x1405C) the stock firmware leaves at 6. For sweeping the
 * handshake's link margin without reflashing: set it, then wifijoin again and watch the AP-side RSSI / 4-way progress.
 * Preserved across wifijoin (set_channel only rewrites byte 0), so a value set here stays for the next join. */
static void con_wifipow(const char *p)
{
    int ok;
    uint32_t v;
    if (!rfs_is_up() || !(fm1_wifi_rd(FM1_WL_EN) & FM1_WL_EN_WIFI)) {
        con_puts("Wi-Fi not up: wifiup first\r\n");
        return;
    }
    v = con_num(&p, &ok);
    if (ok)
        fm1_wifi_set_power(v);
    con_kv("power", (int32_t)fm1_wifi_power());
    con_kx("chan_reg", fm1_wifi_rd(FM1_WIFI_CHAN));
}

/* wifiwcid [N]: read, or set, the TXWI WirelessCliID used for software-CCMP TX. 0 = the no-key entry (HW cipher engine
 * bypassed, our software-encrypted frame injected raw). Sweep 0/1/255 if the AP still rejects, to find the pass-through. */
static void con_wifiwcid(const char *p)
{
    int ok;
    uint32_t v = con_num(&p, &ok);
    if (ok)
        fm1_wifi_tx_wcid = v;
    con_kv("tx_wcid", (int32_t)fm1_wifi_tx_wcid);
}

/* wifiswc [N]: 1 = CCMP in software in wl_send (pair with a keyless wifiwcid), 0 = the MAC encrypts (default) */
static void con_wifiswc(const char *p)
{
    int ok;
    uint32_t v = con_num(&p, &ok);
    if (ok)
        fm1_wifi_sw_ccmp = v ? 1u : 0u;
    con_kv("sw_ccmp", (int32_t)fm1_wifi_sw_ccmp);
}

/* wifikey: read back the Ralink key-table / WCID registers after a join, to prove the writes actually land on the wl30
 * core. The MAC CSRs (0x1xxx -> MMIO 0x31xxx) are known to work; the key tables live higher (0x4020 key, 0x6804 attr,
 * 0x6008 IV -> MMIO 0x34020 / 0x36804 / 0x36008) and may be outside the decoded window. If these read back as 0 (not
 * what wifi_sta_install_hw_keys wrote), the HW never got the key and TX encryption can't work regardless of the values. */
static void con_wifikey(const char *p)
{
    (void)p;
    con_kx("wcid1_rx_lo_0x1808", fm1_wifi_mac_rd(0x1808u));
    con_kx("key_tk0_0x4020", fm1_wifi_mac_rd(0x4020u));
    con_kx("key_tk1_0x4024", fm1_wifi_mac_rd(0x4024u));
    con_kx("wcid1_attr_0x6804", fm1_wifi_mac_rd(0x6804u));
    con_kx("wcid1_iv_0x6008", fm1_wifi_mac_rd(0x6008u));
    con_kx("wcid1_eiv_0x600C", fm1_wifi_mac_rd(0x600Cu));
}

/* wifidma: can the frame DMA address the pool? Write a pool address and a main-RAM address to the TX pointer register
 * (no transfer starts: the length register 0x30F14 is the kick) and read each back. High bits dropped on the pool
 * address = the DMA wraps at 128 KiB onto RAMTEXT, which is why every IN_POOL TX buffer went out as garbage. */
static void con_wifidma(const char *p)
{
    (void)p;
    if (!rfs_is_up() || !(fm1_wifi_rd(FM1_WL_EN) & FM1_WL_EN_WIFI)) {
        con_puts("Wi-Fi not up: wifiup first\r\n");
        return;
    }
    fm1_wifi_wr(FM1_WIFI_TX_ADDR, 0x01C20004u);
    con_kx("wrote_pool_0x01C20004_read", fm1_wifi_rd(FM1_WIFI_TX_ADDR));
    fm1_wifi_wr(FM1_WIFI_TX_ADDR, 0x01C08004u);
    con_kx("wrote_ram__0x01C08004_read", fm1_wifi_rd(FM1_WIFI_TX_ADDR));
    fm1_wifi_wr(FM1_WIFI_TX_ADDR, 0x01C7C004u);
    con_kx("wrote_hi___0x01C7C004_read", fm1_wifi_rd(FM1_WIFI_TX_ADDR));
}

/* wifiscan's engine for the networks code: channels 1-13, ms each, every beacon and probe response into t (wn_scan_add:
 * one row a BSSID, strongest first). The AP's own channel is the DS parameter (wn_parse_beacon), not the one it was
 * heard on: wifijoin used to take the listening channel, and a join to a neighbour channel timed out */
static struct wn_scan con_scan IN_POOL;                 /* (main loop only, no DMA: the pool is fine) */
static void con_wifi_scan_all(struct wn_scan *t, uint32_t ms)
{
    uint32_t *buf = con_wifi_buf, ch, t0;
    wn_scan_clear(t);
    fm1_wifi_mac_wr(FM1_WIFI_RX_FILTER, FM1_WIFI_RX_FILTER_ALL);
    for (ch = 1; ch <= 13u; ch++) {
        fm1_wifi_set_channel(ch);
        t0 = fm1_ticks();
        while (fm1_ticks() - t0 < ms * 1000u * FM1_TICKS_PER_US) {
            int32_t n = fm1_wifi_rx(buf, CON_WIFI_MAX);
            const uint8_t *f = (const uint8_t *)buf + 4u;
            uint32_t mpdu;
            struct wn_ap ap;
            con_wifi_alive();
            if (n <= 0)
                continue;
            mpdu = fm1_wifi_rx_mpdu_len(f);
            if (mpdu + FM1_WIFI_RXWI <= (uint32_t)n &&
                wn_parse_beacon(f + FM1_WIFI_RXWI, mpdu, (uint8_t)ch, (int8_t)fm1_wifi_rx_rssi(f), &ap))
                wn_scan_add(t, &ap);
        }
    }
}

/* the join: scan, the strongest AP of the SSID (or the pinned BSSID), auth, assoc, the 4-way handshake. The credential
 * is the passphrase, or a saved network's PMK. A join that completes is saved (wn_put: SSID + PMK, never the password):
 * only a key a real handshake accepted ever lands in the list. 1 = keys installed (the link service is attached) */
static int con_wifi_connect(const char *target_ssid, uint32_t target_ssid_len, const char *pass, const uint8_t *pmk,
                            const uint8_t *pin_bssid)
{
    static uint32_t tx[(FM1_WIFI_TX_HDR + 256u) / 4u];
    uint8_t *frame_tx = (uint8_t *)tx + FM1_WIFI_TX_HDR;
    uint32_t *buf = con_wifi_buf;
    uint8_t target_bssid[6] = {0};
    uint32_t target_ch = 0, k;
    uint8_t sta_mac[6];
    struct wifi_sta sta;
    uint32_t t0, tx_len = 0, hs_rx = 0;
    int retry, ai = -1;

    if (wn_st.mode != WN_MODE_OFF) {
        con_puts("the Wi-Fi page's MODE is on: wifimode off first (the console join blocks the main loop)\r\n");
        return 0;
    }
    if (!rfs_is_up() || !(fm1_wifi_rd(FM1_WL_EN) & FM1_WL_EN_WIFI)) {
        con_puts("Wi-Fi not up: running wifiup\r\n");
        con_wifiup();
    }

    con_puts("searching for \"");
    con_puts(target_ssid);
    con_puts("\"...\r\n");

    con_wifi_scan_all(&con_scan, 150u);
    if (pin_bssid) {
        for (k = 0; k < con_scan.n; k++)
            if (!memcmp(con_scan.ap[k].bssid, pin_bssid, 6) && con_scan.ap[k].ssid_len == target_ssid_len &&
                !memcmp(con_scan.ap[k].ssid, target_ssid, target_ssid_len))
                ai = (int)k;
    } else {
        ai = wn_scan_best_for(&con_scan, (const uint8_t *)target_ssid, target_ssid_len);
    }
    if (ai < 0) {
        con_puts("SSID not found\r\n");
        return 0;
    }
    memcpy(target_bssid, con_scan.ap[ai].bssid, 6);
    target_ch = con_scan.ap[ai].ch;

    con_puts("found bss ");
    for (k = 0; k < 6u; k++) {
        con_hex(target_bssid[k], 2);
        con_putc(k < 5u ? ':' : ' ');
    }
    con_puts("ch ");
    con_hex(target_ch, 2);
    con_puts(" rssi ");
    con_dec((int32_t)con_scan.ap[ai].rssi);
    con_puts("\r\n");
    if (con_scan.ap[ai].sec != WN_SEC_PSK)
        con_puts("note: not WPA2-PSK with CCMP (the join will fail)\r\n");

    /* Obtain station MAC address and set up MAC hardware registers */
    wifi_sta_get_mac(sta_mac);
    wifi_sta_set_mac_regs(sta_mac, target_bssid, target_ch);

    wifi_sta_init(&sta, sta_mac, ble_hw_rand);
    if (pmk)
        wifi_sta_set_pmk(&sta, pmk, (const uint8_t *)target_ssid, target_ssid_len);
    else
        wifi_sta_set_creds(&sta, pass, (const uint8_t *)target_ssid, target_ssid_len);
    wifi_sta_set_ap(&sta, target_bssid);

    /* 1. Open System Authentication */
    sta.state = WIFI_STA_STATE_AUTH;
    con_puts("authenticating...");
    for (retry = 0; retry < 3 && sta.state == WIFI_STA_STATE_AUTH; retry++) {
        uint32_t flen = wifi_sta_build_auth(&sta, frame_tx);
        fm1_wifi_tx_prepare(tx, flen);
        fm1_wifi_tx(tx);

        t0 = fm1_ticks();
        while (fm1_ticks() - t0 < 300000u * FM1_TICKS_PER_US && sta.state == WIFI_STA_STATE_AUTH) {
            int32_t n = fm1_wifi_rx(buf, CON_WIFI_MAX);
            const uint8_t *f;
            uint32_t mpdu_len;
            con_wifi_alive();
            if (n <= 0)
                continue;
            f = (const uint8_t *)buf + 4u;
            mpdu_len = fm1_wifi_rx_mpdu_len(f);
            if (mpdu_len + FM1_WIFI_RXWI <= (uint32_t)n)
                wifi_sta_rx_frame(&sta, f + FM1_WIFI_RXWI, mpdu_len, frame_tx, &tx_len);
        }
    }

    if (sta.state != WIFI_STA_STATE_ASSOC) {
        con_puts(" auth timeout / failed\r\n");
        return 0;
    }
    con_puts(" ok\r\n");

    /* 2. Association Request */
    con_puts("associating...");
    for (retry = 0; retry < 3 && sta.state == WIFI_STA_STATE_ASSOC; retry++) {
        uint32_t flen = wifi_sta_build_assoc_req(&sta, frame_tx);
        fm1_wifi_tx_prepare(tx, flen);
        fm1_wifi_tx(tx);

        t0 = fm1_ticks();
        while (fm1_ticks() - t0 < 300000u * FM1_TICKS_PER_US && sta.state == WIFI_STA_STATE_ASSOC) {
            int32_t n = fm1_wifi_rx(buf, CON_WIFI_MAX);
            const uint8_t *f;
            uint32_t mpdu_len;
            con_wifi_alive();
            if (n <= 0)
                continue;
            f = (const uint8_t *)buf + 4u;
            mpdu_len = fm1_wifi_rx_mpdu_len(f);
            if (mpdu_len + FM1_WIFI_RXWI <= (uint32_t)n)
                wifi_sta_rx_frame(&sta, f + FM1_WIFI_RXWI, mpdu_len, frame_tx, &tx_len);
        }
    }

    if (sta.state != WIFI_STA_STATE_WAIT_M1) {
        con_puts(" assoc timeout / failed\r\n");
        return 0;
    }
    con_puts(" ok aid ");
    con_dec((int32_t)sta.aid);
    con_puts("\r\n");

    /* 3. 4-Way WPA2 Handshake */
    con_puts("handshake...");
    t0 = fm1_ticks();
    while (fm1_ticks() - t0 < 6000000u * FM1_TICKS_PER_US && sta.state != WIFI_STA_STATE_CONNECTED && sta.state != WIFI_STA_STATE_FAILED) {
        int32_t n = fm1_wifi_rx(buf, CON_WIFI_MAX);
        const uint8_t *f;
        uint32_t mpdu_len;
        con_wifi_alive();
        if (n <= 0)
            continue;
        f = (const uint8_t *)buf + 4u;
        mpdu_len = fm1_wifi_rx_mpdu_len(f);
        if (mpdu_len + FM1_WIFI_RXWI <= (uint32_t)n) {
            int res = wifi_sta_rx_frame(&sta, f + FM1_WIFI_RXWI, mpdu_len, frame_tx, &tx_len);
            hs_rx++;
            if (res == 1) {
                fm1_wifi_tx_prepare(tx, tx_len);
                fm1_wifi_tx(tx);
            }
        }
    }

    if (sta.state == WIFI_STA_STATE_CONNECTED) {
        con_puts(" ok, connected, keys installed\r\n");
        wl_attach(&sta);
        if (wn_put(&wn_st, (const uint8_t *)target_ssid, target_ssid_len, sta.pmk) == 0) {
            settings_later = 1;                            /* (written with the settings, once quiet) */
            con_puts("saved (SSID + key, not the password)\r\n");
        }
        return 1;
    }
    con_puts(" handshake failed / timeout\r\n");
    con_puts(sta.state == WIFI_STA_STATE_WAIT_M1 ? "  never saw M1 (the AP's first key frame)"
             : sta.state == WIFI_STA_STATE_WAIT_M3 ? "  M1 seen, M2 sent, M3 never came (weak uplink, or the AP never heard M2)"
             : sta.state == WIFI_STA_STATE_FAILED ? "  a key frame failed its check (wrong password?)" : "  state ?");
    con_puts(", frames in the window: ");
    con_dec((int32_t)hs_rx);
    con_puts("\r\n");
    return 0;
}

/* wifijoin <ssid> [passphrase] [bssid]: the passphrase, then the join; [bssid] pins one AP of the SSID */
static void con_wifijoin(const char *p)
{
    char ssid[33] = {0}, pass[65] = {0}, bssid_arg[24] = {0};
    uint8_t pin[6] = {0};
    uint32_t k, n = 0;
    int nib = 0;
    if (!con_str(&p, ssid, sizeof ssid)) {
        con_puts("usage: wifijoin <ssid> [passphrase] [bssid]\r\n");
        return;
    }
    con_str(&p, pass, sizeof pass);
    if (con_str(&p, bssid_arg, sizeof bssid_arg)) {         /* any hex, separators ignored */
        for (k = 0; bssid_arg[k] && nib < 12; k++) {
            char c2 = bssid_arg[k];
            int v2 = (c2 >= '0' && c2 <= '9') ? c2 - '0'
                   : (c2 >= 'a' && c2 <= 'f') ? c2 - 'a' + 10
                   : (c2 >= 'A' && c2 <= 'F') ? c2 - 'A' + 10 : -1;
            if (v2 < 0)
                continue;
            if (nib & 1)
                pin[nib / 2] |= (uint8_t)v2;
            else
                pin[nib / 2] = (uint8_t)(v2 << 4);
            nib++;
        }
    }
    while (ssid[n])
        n++;
    con_wifi_connect(ssid, n, pass, 0, nib == 12 ? pin : 0);
    memset(pass, 0, sizeof pass);
}

/* wifinets: the saved networks (never the keys) */
static void con_wifinets(const char *p)
{
    uint32_t i;
    (void)p;
    con_kv("mode", (int32_t)wn_st.mode);
    for (i = 0; i < WN_SLOTS; i++) {
        char nm[33];
        if (wn_st.n[i].used != 1u)
            continue;
        memcpy(nm, wn_st.n[i].ssid, wn_st.n[i].ssid_len);
        nm[wn_st.n[i].ssid_len] = 0;
        con_dec((int32_t)i + 1);
        con_puts(" ");
        con_puts(nm);
        con_puts("  (key saved)\r\n");
    }
    if (!wn_count(&wn_st))
        con_puts("none saved: wifijoin <ssid> <passphrase> saves a network once it joined\r\n");
}

/* wifigo [n]: join saved network n (1 = the latest) with its saved key: no password */
static void con_wifigo(const char *p)
{
    int ok;
    uint32_t n = con_num(&p, &ok);
    char nm[33];
    uint32_t slot = ok && n >= 1u ? n - 1u : 0u;
    if (slot >= WN_SLOTS || wn_st.n[slot].used != 1u) {
        con_puts("no such saved network (wifinets)\r\n");
        return;
    }
    memcpy(nm, wn_st.n[slot].ssid, wn_st.n[slot].ssid_len);
    nm[wn_st.n[slot].ssid_len] = 0;
    con_wifi_connect(nm, wn_st.n[slot].ssid_len, 0, wn_st.n[slot].pmk, 0);
}

/* wififorget <n>: saved network n and its key are deleted */
static void con_wififorget(const char *p)
{
    int ok;
    uint32_t n = con_num(&p, &ok);
    if (!ok || n < 1u || n > WN_SLOTS || wn_st.n[n - 1u].used != 1u) {
        con_puts("usage: wififorget <n> (wifinets)\r\n");
        return;
    }
    wn_forget(&wn_st, n - 1u);
    settings_later = 1;
    con_puts("forgotten\r\n");
}


/* wifiman [scan on|off] [join N [password]] [drop]: the connection manager (wifi_conn.c) from the console, to test it before
 * the page: state, the APs it has heard (N), a scan on or off, a join (a saved network needs no password), let go */
static void con_wifiman(const char *p)
{
    static const char *const ST[] = {"OFF", "RADIO", "IDLE", "SCAN", "PMK", "AUTH", "ASSOC", "HS", "UP", "FAIL", "LINK", "PRE"};
    static const char *const FL[] = {"-", "NO CAL", "NO AP", "AUTH", "ASSOC", "HANDSHAKE", "WRONG PASSWORD"};
    uint32_t i;
    int ok;
    if (con_word(&p, "scan")) {
        wc.scan_want = (uint8_t)con_word(&p, "on");
        if (wc.scan_want)
            wc_scan_clear();
    } else if (con_word(&p, "drop")) {
        wc_disconnect();
    } else if (con_word(&p, "join")) {
        uint32_t n = con_num(&p, &ok);
        char pass[65] = {0};
        int sv;
        if (!ok || n < 1u || n > wc_tab.n) {
            con_puts("usage: wifiman join <n> [password] (the AP's number in the list)\r\n");
            return;
        }
        con_str(&p, pass, sizeof pass);
        sv = wn_find(&wn_st, wc_tab.ap[n - 1u].ssid, wc_tab.ap[n - 1u].ssid_len);
        if (pass[0])
            wc_join(&wc_tab.ap[n - 1u], 0, pass);
        else if (sv >= 0)
            wc_join(&wc_tab.ap[n - 1u], wn_st.n[sv].pmk, 0);
        else
            con_puts("not a saved network: give its password\r\n");
        memset(pass, 0, sizeof pass);
    }
    con_puts("state ");
    con_puts(ST[wc.st % 12u]);
    con_puts(wc.st == WC_FAIL ? " " : "");
    con_puts(wc.st == WC_FAIL ? FL[wc.fail % 7u] : "");
    con_puts("\r\n");
    con_kv("mode", (int32_t)wn_st.mode);
    con_kv("scan_want", wc.scan_want);
    con_kv("sweeps", (int32_t)wc.sweeps);
    con_kv("join_attempt", wc.attempt);
    con_kv("join_frames", (int32_t)wc.d_n);
    con_kv("join_mgmt_to_us", (int32_t)wc.d_mg);
    con_kx("last_ap_mgmt", wc.d_sub);
    con_kv("last_status", (int32_t)wc.d_stat);
    if (wc.st == WC_UP)
        con_wl_ip("ip", wl.ip.ip);
    for (i = 0; i < wc_tab.n; i++) {
        char nm[33];
        const struct wn_ap *a = &wc_tab.ap[i];
        memcpy(nm, a->ssid, a->ssid_len);
        nm[a->ssid_len] = 0;
        con_dec((int32_t)i + 1);
        con_puts(" ");
        con_hex(a->bssid[4], 2);
        con_hex(a->bssid[5], 2);
        con_puts(" ch");
        con_dec((int32_t)a->ch);
        con_puts(" ");
        con_dec((int32_t)a->rssi);
        con_puts(a->sec == WN_SEC_PSK ? " psk " : a->sec == WN_SEC_OPEN ? " open " : " other ");
        con_puts(wn_find(&wn_st, a->ssid, a->ssid_len) >= 0 ? "SAVED " : "");
        con_puts(nm);
        con_puts("\r\n");
    }
}

/* wifimode [off|man|auto]: read or set, as the Bluetooth pages: OFF the radio stays off, MAN joins when told, AUTO joins the
 * best saved network heard (start-up and whenever none is joined: the page and the service, next) */
static void con_wifimode(const char *p)
{
    if (con_word(&p, "off"))
        wn_st.mode = WN_MODE_OFF, settings_later = 1;
    else if (con_word(&p, "man"))
        wn_st.mode = WN_MODE_MAN, settings_later = 1;
    else if (con_word(&p, "auto"))
        wn_st.mode = WN_MODE_AUTO, settings_later = 1;
    con_puts(wn_st.mode == WN_MODE_AUTO ? "mode AUTO\r\n" : wn_st.mode == WN_MODE_MAN ? "mode MAN\r\n" : "mode OFF\r\n");
}
#if FELUCCA_WIFI_AP
/* wifiap <ssid> <passphrase> [channel] [mc]: MODE AP from the console (needs wifimode off); `wifiap off` stops it; no argument
 * prints the state, the clients and the counters. mc: 0 group frames as unicast to each client (default), N the group key at WCID N */
static void con_wifiap(const char *p)
{
    static const char *const ST[] = {"OFF", "KEY", "RADIO", "UP", "FAIL"};
    char ssid[33] = {0}, pass[65] = {0};
    uint32_t i, n = 0, ch = 6u, mc = 0u;
    int ok;
    if (con_word(&p, "off")) {
        wa_stop();
        con_puts("AP off\r\n");
        return;
    }
    if (con_str(&p, ssid, sizeof ssid)) {
        con_str(&p, pass, sizeof pass);
        i = con_num(&p, &ok);
        if (ok)
            ch = i;
        i = con_num(&p, &ok);
        if (ok)
            mc = i;
        while (ssid[n])
            n++;
        if (wa_start((const uint8_t *)ssid, n, pass, ch, mc) != 0)
            con_puts("not started: wifimode off first; a name, a passphrase of 8 to 63 characters, channel 1..13\r\n");
        memset(pass, 0, sizeof pass);
    }
    con_puts("AP ");
    con_puts(ST[wa.st]);
    con_puts("\r\n");
    con_kv("channel", (int32_t)wa.ch);
    con_kv("mc", (int32_t)wa.mc);
    for (i = 0; i < WIFI_AP_CLIENTS; i++) {
        if (wa.ap.clients[i].state == WIFI_AP_CL_NONE)
            continue;
        con_dec((int32_t)i + 1);
        con_puts(" client state ");
        con_dec((int32_t)wa.ap.clients[i].state);
        con_puts(" hs ");
        con_dec((int32_t)wa.hs[i].state);
        con_puts(wa.keyed[i] ? " KEYED " : " ");
        con_hex(((uint32_t)wa.ap.clients[i].mac[0] << 16) | ((uint32_t)wa.ap.clients[i].mac[1] << 8) | wa.ap.clients[i].mac[2], 6);
        con_hex(((uint32_t)wa.ap.clients[i].mac[3] << 16) | ((uint32_t)wa.ap.clients[i].mac[4] << 8) | wa.ap.clients[i].mac[5], 6);
        con_puts("\r\n");
    }
    con_kv("beacons", (int32_t)wa.n.beacon);
    con_kv("mgmt", (int32_t)wa.n.mgmt);
    con_kv("probes", (int32_t)wa.n.probe);
    con_kv("probe_answers", (int32_t)wa.n.probe_tx);
    con_kv("auth", (int32_t)wa.n.auth);
    con_kv("assoc", (int32_t)wa.n.assoc);
    con_kv("eapol", (int32_t)wa.n.eapol);
    con_kv("m2", (int32_t)wa.n.m2);
    con_kv("m4", (int32_t)wa.n.m4);
    con_kv("bad_key_frames", (int32_t)wa.n.bad);
    con_kv("data_to_us", (int32_t)wa.n.data);
    con_kv("data_decrypted", (int32_t)wa.n.dec);
    con_kv("data_still_protected", (int32_t)wa.n.prot);
    con_kv("dhcp_replies", (int32_t)wa.n.dhcp);
    con_kv("peers", (int32_t)lk_peers(&wl_lk));
    con_kv("link_rx", (int32_t)wa.n.link_rx);          /* UDP datagrams to Link's port from a client (multicast or not) */
    con_kv("link_unicast_rx", (int32_t)wa.n.link_uc);
    con_kv("link_copies_tx", (int32_t)wa.n.mc_tx);     /* group frames sent, one copy a client */
    con_kv("strays_told", (int32_t)wa.n.stray);        /* deauths to stations that sent data and were not ours */
    con_kv("tx", (int32_t)wa.n.tx);
    con_kv("tx_err", (int32_t)wa.n.tx_err);
}
#endif
#endif /* FELUCCA_WIFI_CON */
#endif

static void con_blevm(void)                         /* stock V15's VM where the firmware finds it (ble_vm.h's candidates) */
{
    struct ble_vm_info in;
    struct ble_rf_trims t;
    uint8_t m[4];
    uint32_t a, i, size;
    int ok;
    for (i = 0; (a = ble_vm_cand(i, &size)) != 0; i++) {   /* every candidate's first word, in the order tried */
        con_puts("area ");
        con_hex(a, 6);
        con_putc(':');
        if (ble_vm_rd(0, a, m, 4)) {
            con_puts(" flash not available\r\n");
            return;
        }
        con_putc(' ');
        con_hex((uint32_t)m[0] << 24 | (uint32_t)m[1] << 16 | (uint32_t)m[2] << 8 | m[3], 8);
        con_puts("\r\n");
    }
    ok = ble_vm_scan(ble_vm_rd, 0, &in, &t, con_vm_rec, 0);
    if (!in.area) {
        con_puts("no VM (no candidate with 55AAAA55 and a valid first record)\r\n");
        return;
    }
    con_kx("live", in.area);                        /* the candidate used */
    con_kx("size", in.size);
    con_kx("log_end", in.end);                      /* (the emulator's V15 compacts its 8 KiB areas past 60 %, HW §14.4) */
    con_kv("records", in.nrec);
    con_kx("have", in.have);                        /* bits: 106, 107, 108, 187 */
    con_kx("wrong_len", in.wrong_len);
    con_kv("crc187", in.ok187);
    con_kv("read_err", in.read_err);
    con_kv("complete", ok);
    con_trims(&t, in.have);
}

#if FELUCCA_FLASH
/* every candidate area of the VM (ble_vm.h: 0x0E8000 and 0x0E7000, 4 KiB each, then the emulator's 0x093000-0x096FFF,
 * 24 KiB in all), raw, in flr's line format: tools/ble_vm.py decodes a log of it (or of 'flr' reads in a build without
 * BLE). About 85 KB of text: the main loop (the panel, not the audio) waits while the host reads it. */
static int con_dump_flash(uint32_t base, uint32_t size)   /* -> 0 done, -1 stopped (no flash, or the host stalled) */
{
    static uint8_t b[256];
    uint32_t a, i;
    for (a = base; a < base + size; a += sizeof b) {
        if (!flash_ok || st_read(a, b, sizeof b)) {
            con_puts("flash not available\r\n");
            return -1;
        }
        for (i = 0; i < sizeof b; i++) {
            if (i % 16u == 0) {
                con_hex(a + i, 6);
                con_putc(':');
            }
            con_putc(' ');
            con_hex(b[i], 2);
            if (i % 16u == 15u)
                con_puts("\r\n");
        }
        if (con.stalled)
            return -1;
    }
    return 0;
}

static void con_blevmdump(void)
{
    uint32_t k, size, base;
    for (k = 0; (base = ble_vm_cand(k, &size)) != 0; k++)
        if (con_dump_flash(base, size))
            return;
    con_puts("end\r\n");
}
#endif

static void con_bletrim(void)                       /* what the radio uses, Optimist's copy, what rf_init did */
{
    static const char *const SRC[3] = {"none (the radio stays off)", "VM", "Optimist's copy"};
    struct ble_rf_trims t;
    con_puts("source ");
    con_puts(SRC[ble_rf_src % 3u]);
    con_puts("\r\n");
    con_kx("vm_area", ble_vm_seen.area);
    con_kx("vm_have", ble_vm_seen.have);
    con_kv("vm_crc187", ble_vm_seen.ok187);
    con_kv("copy_ok", ble_rf_copy_ok(ble_rf_kept));
    con_kv("bluetooth_on", ble_on);
    con_kv("radio_started", ble_up);
    con_kv("boot_failed", (int32_t)bootguard.failed);   /* > 0: ON saved, this boot left the radio off (ble_boot_radio) */
    con_bytes("copy_raw:", ble_rf_kept, sizeof ble_rf_kept);    /* (the 100 bytes as kept: mark, data, CRC) */
    if (ble_rf_copy_ok(ble_rf_kept)) {
        con_kx("copy_from", ble_rf_copy_area(ble_rf_kept[0]));   /* the VM area it was taken from */
        ble_rf_copy_get(ble_rf_kept, &t);
        con_trims(&t, BLE_VM_ALL);
    }
#if BLE_HW_WL82
    con_puts("tables ");
    con_puts(BLE_RF_SHA256);
    con_puts("\r\n");
    con_kv("rf_ran", fm1_ble_rf_stat.ran);
    con_kv("rf_ops", (int32_t)fm1_ble_rf_stat.ops);
    con_kv("rf_trims", (int32_t)fm1_ble_rf_stat.trims);
    con_kv("rf_lut_words", (int32_t)fm1_ble_rf_stat.lut_words);
    con_kv("rf_delay_us", (int32_t)fm1_ble_rf_stat.delay_us);
    con_kv("rf_skipped", (int32_t)fm1_ble_rf_stat.skipped);
    con_kv("rf_bbp_timeouts", (int32_t)fm1_ble_rf_stat.bbp_timeouts);
    con_kv("rf_spi_timeouts", (int32_t)fm1_ble_rf_stat.spi_timeouts);
    con_kx("rf_bad_op", fm1_ble_rf_stat.bad_op);
    con_kv("vco_found", fm1_ble_rf_stat.scan_found);
    con_kv("vco_band", fm1_ble_rf_stat.scan_band);
    con_kv("vco_steps", fm1_ble_rf_stat.scan_steps);
    con_kx("vco_result", fm1_ble_rf_stat.scan_result);
    con_kv("rf_section", fm1_ble_rf_stat.section);           /* the §16.1 group last entered (15: done) */
    con_kx("rf_sections", fm1_ble_rf_stat.sections);
    con_kv("rf_burst", fm1_ble_rf_stat.burst);               /* writes of the burst group (5); 0: op by op */
#endif
}
#endif

#if FELUCCA_BT_AUDIO
#if !FELUCCA_BT_DIAG                   /* the Classic stack silent: its log lines and printers left out (BT_DIAG puts
                                        * them back, with btinq / btpage / btrx / bttrace) */
#define con_puts(s) ((void)sizeof(s))           /* (sizeof: the argument named, never run) */
#define con_putc(c) ((void)sizeof(c))
#define con_dec(v) ((void)sizeof(v))
#define con_hex(v, d) ((void)sizeof(v), (void)sizeof(d))
#endif
#include "../bt/bt_inq.c"              /* EXPERIMENTAL: the Bluetooth Classic stack (BT_AUDIO; btinq, btpage) */
#if !FELUCCA_BT_DIAG
#undef con_puts
#undef con_putc
#undef con_dec
#undef con_hex
#endif
#endif

#if FELUCCA_BLE && BLE_DIAG       /* FELUCCA_BLE_DIAG (builder item BLE_DIAG): without it no blell, no counters, no rings */
#include "../ble/ble_diag.c"           /* blell: the link layer's counters (ble/ble_diag.h) */

/* blell: link-layer diagnostics, RAM only (docs/BLE-STACK.md §11); 'blell clear' zeroes the counters; 'blell regs'
 * adds the engine's registers and columns (op 2 reads: on request only, never from the start path or an interrupt) */
static void con_blell(const char *p)
{
    struct ble_diag_regs r;
    int regs;
    r.valid = 0;
    if (con_word(&p, "clear")) {
        if (ble_up)
            fm1_ble_irqs_hold(1);
        ble_diag_clear();
        memset(&ble_mdg, 0, sizeof ble_mdg);       /* (TIMER5's fields: a count in flight may survive the clear) */
        if (ble_up)
            fm1_ble_irqs_hold(0);
        con_puts("cleared\r\n");
        return;
    }
    regs = con_word(&p, "regs");
    (void)regs;                                     /* (a build without the WL82 driver: no registers) */
    if (!ble_up)
        con_puts("radio not started (counters only)\r\n");
#if BLE_HW_WL82
    if (ble_up && regs) {                                   /* the engine's registers, the BLE interrupts held a moment */
        fm1_ble_irqs_hold(1);
        ble_hw_diag_regs(&r);
        fm1_ble_irqs_hold(0);
    }
#endif
    ble_diag_print(con_puts, &r);
    con_kv("mi_bluetooth_on", ble_on);              /* the app's side of BLE-MIDI in (midi_ble.c ble_mdg) */
    con_kv("mi_route", ble_midi_route);
    con_kv("mi_off_dropped", ble_mdg.off);
    con_kv("mi_not_channel", ble_mdg.not_chan);
    con_kv("mi_pushed", ble_mdg.pushed);
    con_kv("mi_ring_overflow", ble_mdg.overflow);
    con_kv("mi_drained", ble_mdg.drained);
    con_kv("mi_drain_full", ble_mdg.drain_full);
    con_kv("mi_drained_on", ble_mdg.drained_on);
    con_kv("mi_ring_w", bmi_w);
    con_kv("mi_ring_r", bmi_r);
#if FELUCCA_MIDI_INCLK
    con_kv("mi_in_clock", bp_set[BPS_MIN]);         /* MIDI IN = CLOCK: the router drops note ons (seq_midi.c) */
#endif
    con_kv("mi_in_q_w", mi_w);                      /* midi_in_q, all sources: w - r = waiting for events_block */
    con_kv("mi_in_q_r", mi_r);
}
#endif

/* the clock registers as the SPL left them (hal/fm1_clock.h): sys_div clk_con0..3, pll pll_con0/1 pll2_con0/1 */
static void con_clock_regs(void)
{
    uint32_t i;
    con_puts("clk");
    for (i = 0; i < 9u; i++) {
        con_putc(' ');
        con_hex(fm1_clk_reg(i), 8);
    }
    con_puts("\r\n");
}

#if FELUCCA_DUAL && FELUCCA_DUALCFG
/* the core rails (hal/fm1_supply.h): as found before CPU1's start, right after the raise, and now (status and boot) */
static void con_supply(void)
{
    uint32_t ie = fm1_icfg() & 0x200u, r, g;
    fm1_irq_off();                                      /* (P33 access: not reentrant) */
    r = fm1_supply_now();
    g = fm1_supply_regs();
    if (ie)
        fm1_irq_on();
    con_kx("cpu1_supply_was", dual.supply);             /* VDC14 << 4 | SYSVDD before the raise (0x3B: 3, 11; 0: none) */
    con_kx("cpu1_supply_set", dual.supply_set);         /* read back right after it */
    con_kx("cpu1_supply_err", dual.supply_err);         /* dcdc: 1 P33 timeout, 2 regulator bits, 4 a level (0: right) */
    con_kx("cpu1_supply_regs", dual.supply_regs);       /* P3_ANA_CON0 | CON5 << 8 | CON6 << 16 | CON9 << 24 after it */
    con_kx("rails_now", r);                             /* VDC14 << 4 | SYSVDD as they are */
    con_kx("p33_regs_now", g);                          /* CON0 bit 2 LDO13, bit 3 DCDC13 (the 1.4 V rail's regulator) */
    con_kv("p33_timeouts", (int32_t)fm1_p33_timeouts);
    con_kv("cpu1_supply_ms", (int32_t)dual.supply_ms);  /* TIMER4 ms when the rails were raised (0: not this boot) */
    con_kv("t4_cpu1_start_ms", (int32_t)dual_start_t4 - 1);   /* TIMER4 ms (-1: not): CPU1 started, */
    con_kv("t4_audio_init_ms", (int32_t)dual_audio_t4 - 1);   /* audio_init, */
    con_kv("t4_cpu1_job1_ms", (int32_t)dual_job1_t4 - 1);     /* CPU1's first job posted */
    {
        uint32_t ie = fm1_icfg() & 0x200u, v;
        fm1_irq_off();
        v = fm1_p33_read(FM1_P3_VLVD);
        if (ie)
            fm1_irq_on();
        con_kx("p33_vlvd", v);                          /* P3_VLVD_CON (stockorder: 0x79, 2.6 V on) */
    }
    con_kv("cpu1_parked", dual.parked);
#if FELUCCA_BLE && FELUCCA_DUAL >= 2
    con_kv("cpu1_ok_ms", (int32_t)dual_ok_t4 - 1);      /* TIMER4 ms when CPU1 was first seen working (-1: not) */
    con_kv("ble_ms_radio", (int32_t)ble_ms_radio - 1);  /* TIMER4 ms when the radio's start-up program began (-1: not) */
    con_kv("ble_ms_running", (int32_t)ble_ms_running - 1);   /* .. when BLUETOOTH ran (advertising) */
    con_kv("ble_deferred", ble_deferred);
#endif
}
#endif

static void con_status(void)
{
    const engine_t *e = ENGINES[TSEL->eng_req % NENGINES];
    con_puts("felucca ");
    con_puts(FELUCCA_VERSION);
    con_puts("\r\n");
    con_kv("uptime_ms", (int32_t)fm1_ms);
    con_kv("cpu_pct", (int32_t)(song.cpu_q8 * 100u / 256u));
    con_kv("cpu_khz", (int32_t)cpu_khz);                /* measured at boot (hal/fm1_clock.h) */
    con_clock_regs();
    con_kv("audio_max_us", (int32_t)felucca_dbg.max_us);
    con_kv("voices_shed", (int32_t)shed_count);
    con_kv("voices_given_up", (int32_t)voice_kills);
#if FELUCCA_CPU_GUARD
    con_kv("cpu_guard", (int32_t)cg.level);
    con_kv("cpu_guard_steps", (int32_t)cg.steps);
    con_kv("cpu_guard_shed", (int32_t)cg.sheds);
    con_kv("cpu_guard_early", (int32_t)cg.early);
    con_kv("cpu_guard_avg_pct", (int32_t)(cg.avg * 100u / 256u));   /* (the load over ~8 halves) */
    con_kv("cpu_guard_step_pct", (int32_t)(cg.smeas * 100u / 256u));   /* (the half that last stepped it) */
    con_kv("cpu_guard_rev_gain_pct", (int32_t)(cg.gain[CG_L_REVERB] * 100u / 256u));
#endif
    con_kv("audio_late", (int32_t)felucca_dbg.late);
    con_kx("audio_halves", felucca_dbg.halves);        /* (hex, wraps: the logger takes the differences) */
    con_kx("audio_us_sum", felucca_dbg.us_sum);         /* the halves' render us summed: d(sum) / d(halves) = the mean */
#if FELUCCA_DUAL && FELUCCA_DUALCFG
    {   /* corex2's per-core counters (the SDK's cpu_effic_calc: IF / RD / WR stalls?, TL clocks), low words, hex */
        static const char *const K[8] = {"c0_if", "c0_rd", "c0_wr", "c0_tl", "c1_if", "c1_rd", "c1_wr", "c1_tl"};
        uint32_t i;
        for (i = 0; i < 8u; i++)
            con_kx(K[i], fm1_pcnt(i));
    }
#endif
    con_kv("audio_max_pct", (int32_t)felucca_dbg.pk_all);   /* system/overload.h: INFO's MAX, % of a half */
    con_kv("audio_max5_pct", (int32_t)felucca_dbg.pk_5s);   /* INFO's MAX 5S */
    con_kv("cpu1_max5_pct", (int32_t)felucca_dbg.pk1_5s);   /* CPU1's jobs, MAX 5S */
#if FELUCCA_FX_REVERB
    con_kv("rev_state", (int32_t)rgd.st);               /* (fx.c RG_*: 0 on, 1 fading out, 2 clearing, 3 off) */
    con_kv("rev_live", (int32_t)rev_live);
    con_kv("rev_ret", (int32_t)song.g[G_RRET]);
#endif
#if FELUCCA_DUAL
    con_kv("cpu1_up", dual.up);
    con_kv("cpu1_why", dual.why);
    con_kv("cpu1_jobs", (int32_t)dual.jobs);
    con_kv("cpu1_wait_max_us", (int32_t)(dual.wait_max / FM1_TICKS_PER_US));
    con_kv("cpu1_mode", dual_mode);                     /* 2ND CORE: 0 AUTO, 1 OFF (system/dual_mode.h) */
    con_kv("cpu1_bad", (int32_t)fm1_dual_mb.bad);       /* requests it refused (misreads): 0 on a healthy unit */
    con_kv("cpu1_canary", (int32_t)fm1_dual_mb.canary); /* 1 + canary words read wrong at boot (1: none) */
    con_kv("cpu1_rewakes", (int32_t)dual.rewakes);      /* wakes sent again for a job not picked up (dual.c dual_join) */
    con_kv("cpu1_posts", (int32_t)dual.posts);          /* jobs posted */
    con_kv("cpu1_fpause_n", (int32_t)dual_fp_n);        /* flash operations CPU1 was parked / paused for (dual.c) */
    con_kv("cpu1_fpause_nostop", (int32_t)dual_fp_nostop);   /* ... whose stopped bit never showed (200 us) */
    con_kv("cpu1_fpause_noack", (int32_t)dual_fp_noack);     /* stock's park: CPU1 never said parked (200 us) */
    con_kv("cpu1_park_to", (int32_t)fm1_dual_mb.park_to);    /* ... CPU1's waits for the cache idle that ran out */
#if FELUCCA_DUALCFG
    con_kx("isr_timer5_at", fm1_vec_word(FM1_IRQ_TIMER5));   /* the TIMER5 entry attached (RAM 0x01C..., XIP 0x020...) */
#endif
#if FELUCCA_DUALCFG
    {   /* RAM read errors (hal/fm1_dual.h fm1_flip): per core, the count, the words read, the first one's address and XOR */
        uint32_t c;
        static const char *const K[2][4] = {{"flip0_n", "flip0_checks", "flip0_addr", "flip0_xor"},
                                            {"flip1_n", "flip1_checks", "flip1_addr", "flip1_xor"}};
        for (c = 0; c < 2u; c++) {
            con_kv(K[c][0], (int32_t)fm1_flip[c].n);
            con_kv(K[c][1], (int32_t)fm1_flip[c].checks);
            con_kx(K[c][2], fm1_flip[c].addr);
            con_kx(K[c][3], fm1_flip[c].got ^ fm1_flip[c].want);
        }
        con_kv("vec_flips", (int32_t)fm1_vec_flips);    /* TIMER5's vector read back wrong (main loop) */
        con_kx("vec_got", fm1_vec_got);
    }
    {
        uint32_t i, m = 0;
        for (i = 0; i < 8u; i++)                        /* the last 8 masks, oldest first, a hex digit each (bit 0..2 a */
            m = m << 4 | (dual.mring[(dual.posts + i) & 7u] & 15u);   /* part, 3 the drums) */
        con_kx("cpu1_masks", m);
        for (i = 0; i < DUAL_JOBS && i < DUAL_JOBS_MAX; i++) {
            static const char *const K[DUAL_JOBS_MAX] = {"cpu1_first_j0", "cpu1_first_j1", "cpu1_first_j2", "cpu1_first_j3",
                                                         "cpu1_first_j4", "cpu1_first_j5", "cpu1_first_j6", "cpu1_first_j7"};
            con_kv(K[i], (int32_t)dual.first_post[i]);  /* 1 + the post CPU1 first got job i (3 = NPART: the drums) */
        }
    }
#endif
    con_kv("cpu1_stage", (int32_t)fm1_dual_mb.stage);   /* how far CPU1 got (dual_mode.h C1S_*) */
    con_kv("cpu1_beat", (int32_t)fm1_dual_mb.count);    /* its spin loops / returns from idle */
    con_kv("cpu1_wakes", (int32_t)fm1_dual_mb.wakes);   /* its wake handler's entries (asleep) */
    con_kv("cpu1_stopped", dual.stop.stopped);          /* put down (any path, dual.c dual_down): */
    con_kv("cpu1_down_ms", (int32_t)dual.down_ms);      /* fm1_ms then (0: before the clock's first tick) */
    con_kv("cpu1_down_c1_ms", (int32_t)dual.stop.c1_ms);   /* ms since CPU1 was started */
    con_kv("cpu1_down_job", (int32_t)dual.stop.job);    /* jobs joined before (0: the first) */
    con_kv("cpu1_down_mask", (int32_t)dual.down_mask);
    con_kv("cpu1_down_wait_us", (int32_t)dual.stop.wait_us);
    con_kv("cpu1_down_in_job", dual.stop.in_job);
    con_kv("cpu1_down_took", dual.stop.took);
    con_kv("cpu1_down_at", dual.stop.at);
    con_kv("cpu1_down_beat", (int32_t)dual.down_beat);
    con_kv("cpu1_down_pend", dual.down_pend);           /* its wake (soft interrupt 124) still pending then */
    con_kx("cpu1_icfg", fm1_dual_mb.icfg);              /* CPU1's icfg as it slept (bit 31: read) */
    con_kv("cpu1_exc", (int32_t)fm1_dual_exc.count);    /* exceptions about CPU1 (hal/fm1_dual.h fm1_dual_exc_c) */
    con_kv("cpu1_exc_cpu", (int32_t)fm1_dual_exc.cpu);
    con_kx("cpu1_exc_dbg", fm1_dual_exc.dbg);
    con_kx("cpu1_exc_emu", fm1_dual_exc.emu);
    con_kx("cpu1_exc_pc", fm1_dual_exc.pc);
    con_kx("cpu1_exc_rets", fm1_dual_exc.rets);
    con_kx("cpu1_exc_c1con", fm1_dual_exc.c1con);
    con_kx("cpu1_exc_c1etm0", fm1_dual_exc.c1etm[0]);   /* CPU1's last branches then */
    con_kx("cpu1_exc_c1etm1", fm1_dual_exc.c1etm[1]);
    con_kx("cpu1_exc_c1etm2", fm1_dual_exc.c1etm[2]);
    con_kx("cpu1_exc_c1etm3", fm1_dual_exc.c1etm[3]);
    con_kx("cpu1_c1con", fm1_dual_c1con());
    con_kv("cpu1_stack_small_used", (int32_t)fm1_dual_stack_used(0));  /* bytes of 1024 (the idle one: sp spinning, usp asleep) */
    con_kv("cpu1_stack_big_used", (int32_t)fm1_dual_stack_used(1));    /* bytes of 5120 (the jobs' one) */                   /* (what it reads back now) */
#if FELUCCA_DUALCFG
    con_supply();
#endif
#endif
#if FELUCCA_STOCK_SUPPLY && !FELUCCA_DUALCFG
    con_kx("supply_was", supply.was);                   /* VDC14 << 4 | SYSVDD as the boot loader left them (0x3B: 3, 11) */
    con_kx("supply_now", supply.now);                   /* after the stock start-up (0x7F: 7, 15) */
    con_kx("supply_err", supply.err);                   /* 0: read back right; 1 P33 timeout, 2 regulator bits, 4 a level, 8 VLVD */
    con_kx("supply_regs", supply.regs);                 /* P3_ANA_CON0 | CON5 << 8 | CON6 << 16 | CON9 << 24 (CON0 bit 3: DC-DC) */
#endif
    con_kv("track", (int32_t)song.sel + 1);
    con_kv("batt_raw", song.batt_raw);
    con_puts("engine ");
    con_puts(e->name);
    con_puts("\r\n");
    con_puts("preset ");
    con_puts(TSEL->preset < e->npresets ? preset_name(TSEL->eng_req % NENGINES, TSEL->preset) : "-");
    con_puts("\r\n");
    con_kv("bpm", song.g[G_BPM]);
    con_kv("playing", song.playing);
    con_kv("boots", (int32_t)felucca_dbg.boots);
    con_kv("usb_resets", (int32_t)usb.resets);
    con_kv("usb_sof", (int32_t)usb.sof_seen);
    con_kv("usb_suspends", (int32_t)usb.suspends);
    con_kv("usb_retries", (int32_t)usb.retries);
    con_kv("usb_frame", (int32_t)usb.frame);
    con_kv("usb_frame_stalls", (int32_t)usb.frame_stalls);
    con_kv("usb_max_gap_polls", (int32_t)usb.max_gap);
    con_kv("midi_rx_pkts", (int32_t)usb.rx_pkts);
    con_kv("midi_tx_pkts", (int32_t)usb.tx_pkts);
#if FELUCCA_FLASH
    con_kv("flash", flash_ok);
    con_kv("flash_irqoff_us", (int32_t)(FM1_IRQOFF_TICKS(fm1_irqoff_max) / FM1_TICKS_PER_US));   /* INTERNALS' FLASH IRQS OFF */
    con_kv("flash_irqoff_kind", (int32_t)FM1_IRQOFF_KIND(fm1_irqoff_max));       /* FM1_FLOP_*: 0 other, 1 read, 2 write, 3 erase */
    con_kv("flash_erase_windows_max", (int32_t)fl_erase_windows_max);   /* (hal/fm1_flash_slice.h) */
    con_kv("flash_erase_held", (int32_t)fl_erase_held); /* erases a window waited out (no suspend) */
    con_kv("flash_erase_nosus", (int32_t)fl_erase_nosus);   /* suspends without a SUS bit */
    con_kv("flash_erase_redo", (int32_t)fl_erase_redo); /* erased again (not blank after the windows) */
#endif
}

/* `keys`: how often the keys are read, since the last `keys` (the maxima cleared): scan ticks and frames a second (10 kHz and 909 expected),
 * the longest gap between two ticks, the debounce blips, the longest main-loop gap. Run it in the modes where the 16 keys feel dead */
static void con_keys(void)
{
    static uint32_t last_ms, last_ticks, last_frames;
    uint32_t ms = fm1_ms, dt = ms - last_ms, t = fm1_in.ticks, f = fm1_in.frames, g;
    if (!dt)
        dt = 1;
    fm1_irq_off();
    g = fm1_in.gap_max;
    fm1_in.gap_max = 0;
    fm1_irq_on();
    con_kv("keys_ticks_per_s", (int32_t)((t - last_ticks) * 1000u / dt));
    con_kv("keys_frames_per_s", (int32_t)((f - last_frames) * 1000u / dt));
    con_kv("keys_tick_gap_max_us", (int32_t)(g / FM1_TICKS_PER_US));
    con_kv("keys_ui_gap_max_ms", (int32_t)fm1_in.ui_gap_max);
    con_kv("keys_press_blips", (int32_t)fm1_in.press_blips);
    con_kv("keys_release_blips", (int32_t)fm1_in.release_blips);
    {   /* the knobs' detents counted since power-on: tap step keys with no hand on any knob; a count that moves is a phantom turn
         * (a knob turned while a step key is down cancels its tap: op_input.c) */
        uint32_t e;
        for (e = 0; e < FM1_NENC; e++) {
            static const char *const N[FM1_NENC] = {"keys_knob0", "keys_knob1", "keys_knob2", "keys_knob3", "keys_knob4", "keys_knob5", "keys_knob6"};
            con_kv(N[e], (int32_t)fm1_in.enc_count[e]);
        }
    }
    fm1_in.ui_gap_max = 0;
    last_ms = ms, last_ticks = t, last_frames = f;
}

/* `keylog`: the last 32 key / button edges, oldest first: id (0..13 buttons, 14..40 note keys = note + 14), P down / R up, the time
 * since the one before in microseconds. Tap ONE step key once, then run it: a second P a few hundred us after the first is the scan */
static void con_keylog(void)
{
    uint32_t n = fm1_klog_n, i, from = n > FM1_KLOG_N ? n - FM1_KLOG_N : 0u, prev = 0;
    for (i = from; i < n; i++) {
        uint32_t k = i % FM1_KLOG_N, id = fm1_klog_id[k] & 0x7Fu;
        con_kv(fm1_klog_id[k] & 0x80u ? "R" : "P", (int32_t)id);
        con_kv("  dt_us", i == from ? 0 : (int32_t)((fm1_klog_t[k] - prev) / FM1_TICKS_PER_US));
        prev = fm1_klog_t[k];
    }
}

static void con_dbg(void)
{
    const uint32_t *w = (const uint32_t *)&felucca_dbg;
    static const char *const NAMES[] = {"magic", "halves", "max_us", "nested", "in_audio", "late",
                                        "timer_irqs", "ui_frames", "last_us", "cpu_q8", "boots",
                                        "stage", "page", "home", "prev_stage", "prev_page",
                                        "prev_home", "prev_rst", "prev_frames", "alive_ms", "c1",
                                        "prev_alive_ms", "prev_c1", "crash_seen", "crash_new", "boot_failed",
                                        "boot_pending"};
    uint32_t i;
    for (i = 0; i < sizeof NAMES / sizeof NAMES[0] && i < sizeof felucca_dbg / 4u; i++)
        con_kx(NAMES[i], w[i]);
#if FELUCCA_BLE && BLE_HW_WL82
    /* the BLE breadcrumb before the last reset (hal/fm1_ble_rf.h, docs/BLE-STACK.md §11, "The BLE breadcrumb"): 0xB1 SS GG RR, SS the
     * BLUETOOTH ON step, GG rf_ops / 256 and RR the rf_init group; then the BLE interrupts taken since that ON; then
     * rf_init's op, its index in the group and the last register access begun */
    con_kx("prev_ble", fm1_ble_bc.prev);
    con_kx("prev_ble_irqs", fm1_ble_bc.prev_irqs);
    con_kx("ble_step", fm1_ble_bc.now);
    con_kx("ble_irqs", fm1_ble_bc.irqs);
    con_kv("prev_ble_op", (int32_t)fm1_ble_bc.prev_op);      /* rf_ops, the op within its group, the last */
    con_kv("prev_ble_gop", (int32_t)fm1_ble_bc.prev_gop);    /* access begun (hal/fm1_ble_rf.h) */
    con_kx("prev_ble_addr", fm1_ble_bc.prev_addr);
    con_kx("prev_ble_stop", fm1_ble_bc.prev_stop);       /* the last link stop: 5D waiting, 5E done (path, us) */
    con_kx("ble_stop", fm1_ble_bc.stop);
#endif
}

#if FELUCCA_DUAL
/* the last exception's CPU1 state and bus master (hal/fm1_dual.h fm1_dual_snap; the crash report's or a hold's) */
static void con_snap(void)
{
    static const char *const R[16] = {"c1_r0", "c1_r1", "c1_r2", "c1_r3", "c1_r4", "c1_r5", "c1_r6", "c1_r7", "c1_r8",
                                      "c1_r9", "c1_r10", "c1_r11", "c1_r12", "c1_r13", "c1_r14", "c1_r15"};
    uint32_t i;
    if (fm1_c1_snap.magic != FM1_C1_SNAP_MAGIC) {
        con_puts("no cpu1 snapshot\r\n");
        return;
    }
    con_kx("snap_dbg", fm1_c1_snap.dbg);                 /* DBG_MSG: 4 axi_rd_inv 5 axi_wr_inv 16..18 c1_if/rd/wr_bus_inv 19..21 c0_ */
    con_kx("snap_axi_rd_id", fm1_c1_snap.axi_rd_id);     /* the bus master of an axi_rd_inv */
    con_kx("snap_axi_wr_id", fm1_c1_snap.axi_wr_id);
    con_kx("snap_c1_wr_window", fm1_c1_snap.c1_wr_num);   /* bit 3: CPU0's RAM code, bit 4: its stacks / .noinit / vectors */
    con_kx("c1_pc", fm1_c1_snap.pcrs);                   /* where CPU1 was (PCRS) */
    con_kx("c1_reti", fm1_c1_snap.reti);
    con_kx("c1_rets", fm1_c1_snap.rets);
    con_kx("c1_psr", fm1_c1_snap.psr);
    con_kx("c1_icfg", fm1_c1_snap.icfg);
    con_kx("c1_usp", fm1_c1_snap.usp);
    con_kx("c1_ssp", fm1_c1_snap.ssp);
    con_kx("c1_sp", fm1_c1_snap.sp);
    con_kx("c1_emu", fm1_c1_snap.emu);
    for (i = 0; i < 16u; i++)
        con_kx(R[i], fm1_c1_snap.r[i]);
    con_kx("snap_ble_step", fm1_c1_snap.ble[0]);         /* 0xB1 SS GG RR: BLUETOOTH ON step, rf ops / 256, rf_init group */
    con_kv("snap_ble_irqs", (int32_t)fm1_c1_snap.ble[1]);   /* BLE interrupts taken since ON */
    con_kv("snap_ble_op", (int32_t)fm1_c1_snap.ble[2]);  /* rf_init's op, its index in the group, the last register */
    con_kv("snap_ble_gop", (int32_t)fm1_c1_snap.ble[3]); /* access begun (hal/fm1_ble_rf.h) */
    con_kx("snap_ble_addr", fm1_c1_snap.ble[4]);
    con_kv("snap_ms", (int32_t)(fm1_c1_snap.ticks / (1000u * FM1_TICKS_PER_US)));   /* TIMER4 then, ms */
    con_kv("snap_boot", (int32_t)fm1_c1_snap.boot);     /* the boot it is from (status boots: this one) */
    con_kv("now_ms", (int32_t)(fm1_ticks() / (1000u * FM1_TICKS_PER_US)));
}
#endif

static void con_crash(void)
{
    if (fm1_crash.magic != FM1_CRASH_MAGIC) {
        con_puts("no crash record\r\n");
#if FELUCCA_DUAL
        con_snap();                                     /* (a hold of CPU1 leaves one without a crash) */
#endif
        return;
    }
    con_kv("count", (int32_t)fm1_crash.count);
    con_kx("vec", fm1_crash.vec);
    con_kx("pc", fm1_crash.pc);
    con_kx("rets", fm1_crash.rets);
    con_kx("emu", fm1_crash.emu);
    con_kx("dbg", fm1_crash.dbg);                       /* DBG_MSG: which guard / bus error (hal/fm1_guard.h) */
    con_kx("icfg", fm1_crash.icfg);
    con_kx("etm0", fm1_crash.etm[0]);                   /* the last branches before it */
    con_kx("etm1", fm1_crash.etm[1]);
    con_kx("etm2", fm1_crash.etm[2]);
    con_kx("etm3", fm1_crash.etm[3]);
    con_kx("sp", fm1_crash.sp);
    con_kx("psr", fm1_crash.psr);
    con_kv("uptime_ms", (int32_t)fm1_crash.uptime_ms);
    con_kv("early", (int32_t)fm1_crash.early);
#if FELUCCA_DUAL
    con_snap();
#endif
}

/* how this boot started and how the one before ended (2026-10-15, the CPU1 investigation: tools fm1_console.py boot,
 * todo1014/dualidle/cpu1_log.py): the reset source, the boot guard as found, the breadcrumbs the reset kept */
#if FELUCCA_DUAL && FELUCCA_DUALCFG
static void con_dualcfg_show(void);
#endif
static void con_boot(void)
{
    con_kx("p3_rst", fm1_boot.p3_rst);                  /* bit0 power-on, 1 VDDIO low, 2 WDT, 3 VCM, 4 long press, 5 1.2 V, 6 soft */
    con_kx("rst_src", fm1_boot.rst_src);
    con_kx("wdt_con", fm1_boot.wdt_con);
    con_kv("boot_failed", (int32_t)felucca_dbg.boot_failed);   /* warm resets within 30 s counted (bootguard.h) */
    con_kv("boot_pending", (int32_t)felucca_dbg.boot_pending);
    con_kv("bootguard_failed_now", (int32_t)bootguard.failed);   /* (0 once this boot ran 30 s) */
    con_kv("boots", (int32_t)felucca_dbg.boots);
    con_kv("prev_alive_ms", (int32_t)felucca_dbg.prev_alive_ms);   /* how long the boot before ran (TIMER5's ms) */
    con_kx("prev_stage", felucca_dbg.prev_stage);       /* where it was: 0x41.. a start-up step, 1..9 the main loop */
    con_kv("prev_frames", (int32_t)felucca_dbg.prev_frames);
    con_kv("prev_c1_stage", (int32_t)felucca_dbg.prev_c1);   /* CPU1's stage then (dual_mode.h C1S_*) */
    con_kv("prev_crash", (int32_t)felucca_dbg.crash_new);   /* 1: it ended in the crash report (crash) */
    con_kv("prev_loop_gap_max_ms", (int32_t)felucca_dbg.prev_loop_gap_max);   /* the main loop's longest gap then */
    con_kv("prev_starve_ms", (int32_t)felucca_dbg.prev_starve_ms);   /* when it starved (no pass for 1.5 s), 0: never */
    con_kx("prev_where", felucca_dbg.prev_where);       /* bit 0 in dual_join, bit 1 in the audio render (then) */
    con_kv("prev_in_audio", (int32_t)felucca_dbg.prev_in_audio);
    con_kv("prev_cpu_pct", (int32_t)(felucca_dbg.prev_cpu_q8 * 100u / 256u));   /* (smoothed: jumpy, not the load) */
    con_kv("prev_audio_max_pct", (int32_t)felucca_dbg.prev_pk_all);   /* the longest half since that boot, % of a half */
    con_kv("prev_audio_max5_pct", (int32_t)felucca_dbg.prev_pk_5s);  /* the longest of its last ~5 s */
    con_kv("prev_cpu1_max5_pct", (int32_t)felucca_dbg.prev_pk1_5s);  /* CPU1's jobs' longest half, its last ~5 s */
    con_kv("prev_drops", (int32_t)felucca_dbg.prev_drops);  /* late halves then (INFO's DROPS) */
    con_kv("prev_late", (int32_t)felucca_dbg.prev_late);
    con_kv("loop_gap_max_ms", (int32_t)felucca_dbg.loop_gap_max);   /* this boot so far */
    con_kx("prev_audio_stage", fm1_astage_prev);        /* core.h ASTAGE: where the audio interrupt was (0x7F: out of it) */
    con_kx("prev_loop_id", fm1_aloop_prev);             /* core.h LOOP_GUARD: a loop that hit its bound (0: none) */
    con_kx("loop_id", fm1_aloop);                       /* (this boot so far) */
    con_kv("ms", (int32_t)fm1_ms);
#if FELUCCA_CACHE_RAM
    /* hal/fm1_cache.h: 1 ready, 2 never idle, 3 masks not taken, 4 memory test failed (then restored); the registers
     * as the boot loader left them (CACHE_CON bit 9: the cache on for external RAM) and after */
    con_kv("cache_ram", (int32_t)fm1_cache.status);
    con_kx("cache_con_boot", fm1_cache.con);
    con_kx("dcache_way_boot", fm1_cache.dway0);
    con_kx("icache_way_boot", fm1_cache.iway0);
    con_kx("dcache_way", fm1_cache.dway);
    con_kx("icache_way", fm1_cache.iway);
    con_kx("cache_ram_bad_at", fm1_cache.bad_at);
#endif
#if FELUCCA_DUAL
    con_kv("cpu1_why", dual.why);
    con_kv("cpu1_stage", (int32_t)fm1_dual_mb.stage);
    con_kv("cpu1_start_ms", (int32_t)(dual.t_start / (1000u * FM1_TICKS_PER_US)));   /* TIMER4 ms: CPU1 started */
    con_kv("early_crashes", (int32_t)felucca_dbg.boot_failed);   /* bootguard.failed as this boot found it (AFTER CRASH) */
#if FELUCCA_DUALCFG
    con_kv("afterhold", (int32_t)!dualcfg.noafter);    /* 1: a boot after an early crash keeps CPU1 off (dualcfg afterhold) */
    con_supply();
#else
    /* the start-up order (stock's: rails, CPU1, the radio, the audio, CPU1's first job), TIMER4 ms (-1: not) */
    con_kv("t4_cpu1_start_ms", (int32_t)dual_start_t4 - 1);
#if FELUCCA_BLE
    con_kv("ble_ms_radio", (int32_t)ble_ms_radio - 1);
    con_kv("ble_ms_running", (int32_t)ble_ms_running - 1);
#endif
    con_kv("t4_audio_init_ms", (int32_t)dual_audio_t4 - 1);
    con_kv("t4_cpu1_job1_ms", (int32_t)dual_job1_t4 - 1);
    con_kv("cpu1_fpause_n", (int32_t)dual_fp_n);        /* flash operations CPU1 was parked and paused for (stock's) */
    con_kv("cpu1_fpause_noack", (int32_t)dual_fp_noack);
#if FELUCCA_STOCK_SUPPLY
    con_kx("supply_now", supply.now);
    con_kx("supply_err", supply.err);
    con_kx("supply_regs", supply.regs);
#endif
#endif
#if FELUCCA_DUALCFG
    con_dualcfg_show();
#endif
#endif
#if FELUCCA_BLE && BLE_HW_WL82
    {
        static const char *const K[14] = {"ble_ms_none", "ble_ms_set_on", "ble_ms_stack_init", "ble_ms_rf_init",
                                          "ble_ms_bb_init", "ble_ms_started", "ble_ms_enable", "ble_ms_link_stop",
                                          "ble_ms_link_open", "ble_ms_adv_prog", "ble_ms_adv_started", "ble_ms_irqs_on",
                                          "ble_ms_running", "ble_ms_set_off"};
        uint32_t i;
        for (i = 1; i < 14u; i++)
            con_kv(K[i], (int32_t)fm1_ble_step_at[i] - 1);   /* TIMER4 ms the step was first reached (-1: never) */
    }
#endif
}

#if FELUCCA_DUAL && FELUCCA_DUALCFG
static uint8_t con_reboot_req;                          /* `reboot`: main.c resets, as an intentional reset */
/* the second-core test image (system/dual.c dualcfg): `dualcfg show | reset | start boot|after|never | late MS |
 * park on|off | idle sleep|spin | hold MS | poll N | boost on|off | ble on|off | policy ble-exclusive|none`. Saved with the settings record at once,
 * applied at the next boot (`reboot`, or a power-on) */
static void con_dualcfg_show(void)
{
    static const char *const ST[3] = {"boot", "after", "never"};
    con_puts("dualcfg start ");
    con_puts(ST[dualcfg.start % 3u]);
    con_kv("\r\ndualcfg late", dualcfg.late_ms);
    con_kv("dualcfg park", dualcfg.park);
    con_puts(dualcfg.sleep ? "dualcfg idle sleep\r\n" : "dualcfg idle spin\r\n");
    con_kv("dualcfg hold", dualcfg.hold_ms);
    con_kv("dualcfg poll", (int32_t)dualcfg.poll);
    con_kv("dualcfg boost", dualcfg.boost & 1u);
    con_puts(dualcfg.boost & 2u ? "dualcfg dcdc on\r\n" : "dualcfg dcdc off\r\n");
    con_puts(dualcfg.early ? "dualcfg supply early on\r\n" : "dualcfg supply early off\r\n");
    con_kv("dualcfg rfquiet", dualcfg.rfquiet);
    con_kv("dualcfg release", dualcfg.rel_ms);
    con_kv("dualcfg bledelay", dualcfg.ble_delay);
    con_kv("dualcfg stockorder", dualcfg.stockorder);
    con_kv("dualcfg ble", dualcfg.ble);
    con_puts(dualcfg.policy ? "dualcfg policy ble-exclusive\r\n" : "dualcfg policy none\r\n");
    con_puts(dualcfg.flat ? "dualcfg nest off\r\n" : "dualcfg nest on\r\n");
    con_kv("dualcfg c1guard", dualcfg.c1guard);
    con_puts(dualcfg.insoff ? "dualcfg ins off\r\n" : "dualcfg ins on\r\n");
    con_kx("dualcfg jobs", dualcfg.jobs ? dualcfg.jobs : 0xFFu);
    con_puts(dualcfg.isrram ? "dualcfg isr ram\r\n" : "dualcfg isr xip\r\n");
    con_kv("dualcfg fpause", dualcfg.fpause);
    con_puts(dualcfg.noafter ? "dualcfg afterhold off\r\n" : "dualcfg afterhold on\r\n");
    con_puts(dualcfg.spinv == FM1_SPIN_XIP ? "dualcfg spin xip\r\n" : dualcfg.spinv == FM1_SPIN_NOPS ? "dualcfg spin nops\r\n"
                                                                                                  : "dualcfg spin ram\r\n");
    con_kv("dualcfg pcnt", dualcfg.pcnt);
    con_kx("dualcfg rails", dualcfg.rails);           /* VDC14 << 4 | SYSVDD targets (0: none) */
    con_puts("(the next boot; this boot: ");
    con_puts(DUAL_SLEEPS_NOW ? "sleep" : "spin");
    con_puts(")\r\n");
}
static int con_onoff(const char **p, uint8_t *v)
{
    if (con_word(p, "on"))
        *v = 1;
    else if (con_word(p, "off"))
        *v = 0;
    else
        return 0;
    return 1;
}
static void con_dualcfg(const char *p)
{
    dualcfg_t c = dualcfg;
    int ok = 1, n;
    uint32_t v;
    if (!*p || con_word(&p, "show")) {
        con_dualcfg_show();
        return;
    }
    if (con_word(&p, "reset"))
        dualcfg_defaults(&c);
    else if (con_word(&p, "start"))
        ok = con_word(&p, "boot") ? (c.start = DCS_BOOT, 1) : con_word(&p, "after") ? (c.start = DCS_AFTER, 1)
           : con_word(&p, "never") ? (c.start = DCS_NEVER, 1) : 0;
    else if (con_word(&p, "idle"))
        ok = con_word(&p, "sleep") ? (c.sleep = 1, 1) : con_word(&p, "spin") ? (c.sleep = 0, 1) : 0;
    else if (con_word(&p, "park"))
        ok = con_onoff(&p, &c.park);
    else if (con_word(&p, "boost")) {                  /* off | on (= jangada: 5 / 15) | stock (7 / 15, the fields only) */
        uint8_t on;
        if (con_word(&p, "stock"))
            ok = 1, c.boost = (uint8_t)(c.boost & 2u), c.rails = 0x7Fu;
        else if (con_word(&p, "jangada"))
            ok = 1, c.boost = (uint8_t)((c.boost & 2u) | 1u), c.rails = 0;
        else if ((ok = con_onoff(&p, &on)) != 0)
            c.boost = on ? (uint8_t)((c.boost & 2u) | 1u) : 0u, c.rails = 0;   /* (off: no rail raised, no dcdc) */
    } else if (con_word(&p, "supply")) {               /* supply early on|off: the rails raised right after the settings load */
        ok = con_word(&p, "early") && con_onoff(&p, &c.early);
    } else if (con_word(&p, "rfquiet")) {
        ok = con_onoff(&p, &c.rfquiet);
    } else if (con_word(&p, "release")) {              /* release MS: a parked CPU1 started MS after the radio program */
        v = con_num(&p, &n);
        ok = n && v <= 30000u;
        if (ok)
            c.rel_ms = (uint16_t)v;
    } else if (con_word(&p, "bledelay")) {             /* bledelay MS: the BLE radio MS after CPU1 is up (0: as before) */
        v = con_num(&p, &n);
        ok = n && v <= 30000u;
        if (ok)
            c.ble_delay = (uint16_t)v;
    } else if (con_word(&p, "dcdc")) {                 /* on: the stock supply start-up (hal/fm1_supply.h), read back */
        uint8_t on;
        ok = con_onoff(&p, &on);
        if (ok)
            c.boost = (uint8_t)((c.boost & 1u) | (on ? 2u : 0u));
    }
    else if (con_word(&p, "ble"))
        ok = con_onoff(&p, &c.ble);
    else if (con_word(&p, "isr"))
        ok = con_word(&p, "ram") ? (c.isrram = 1, 1) : con_word(&p, "xip") ? (c.isrram = 0, 1) : 0;
    else if (con_word(&p, "fpause"))                   /* on: C1_CON pause; stock: CPU1 parked in RAM first, then paused */
        ok = con_word(&p, "stock") ? (c.fpause = 2u, 1) : con_onoff(&p, &c.fpause);
    else if (con_word(&p, "stockorder"))
        ok = con_onoff(&p, &c.stockorder);
    else if (con_word(&p, "spin"))
        ok = con_word(&p, "ram") ? (c.spinv = FM1_SPIN_RAM, 1) : con_word(&p, "xip") ? (c.spinv = FM1_SPIN_XIP, 1)
           : con_word(&p, "nops") ? (c.spinv = FM1_SPIN_NOPS, 1) : 0;
    else if (con_word(&p, "boostlvl")) {
        v = con_num(&p, &n);
        ok = n && v < FM1_BOOST_LEVELS;
        if (ok)
            c.rails = (uint8_t)fm1_boost_level(v), c.boost &= 2u;
    } else if (con_word(&p, "rails")) {             /* rails V S: VDC14 0 or 3..7, SYSVDD 0 or 11..15 (0: left) */
        uint32_t s;
        int n2;
        v = con_num(&p, &n);
        s = con_num(&p, &n2);
        ok = n && n2 && (v == 0u || (v >= 3u && v <= 7u)) && (s == 0u || (s >= 11u && s <= 15u));
        if (ok)
            c.rails = (uint8_t)(v << 4 | s), c.boost &= 2u;
    } else if (con_word(&p, "pcnt"))
        ok = con_onoff(&p, &c.pcnt);
    else if (con_word(&p, "afterhold")) {
        uint8_t on;
        ok = con_onoff(&p, &on);
        if (ok)
            c.noafter = (uint8_t)!on;
    }
    else if (con_word(&p, "jobs")) {
        v = con_num(&p, &n);
        ok = n && v >= 1u && v <= 0xFFu;                /* (0x80: none, CPU1 runs with no job: no job has bit 7) */
        if (ok)
            c.jobs = (uint8_t)v;
    } else if (con_word(&p, "c1guard"))
        ok = con_onoff(&p, &c.c1guard);
    else if (con_word(&p, "ins")) {
        uint8_t on;
        ok = con_onoff(&p, &on);
        if (ok)
            c.insoff = (uint8_t)!on;
    }
    else if (con_word(&p, "nest")) {
        uint8_t on;
        ok = con_onoff(&p, &on);
        if (ok)
            c.flat = (uint8_t)!on;
    } else if (con_word(&p, "policy"))
        ok = con_word(&p, "ble-exclusive") ? (c.policy = 1, 1) : con_word(&p, "none") ? (c.policy = 0, 1) : 0;
    else if (con_word(&p, "late")) {
        v = con_num(&p, &n);
        ok = n && v <= 60000u;
        if (ok)
            c.late_ms = (uint16_t)v;
    } else if (con_word(&p, "hold")) {
        v = con_num(&p, &n);
        ok = n && v <= 60000u;
        if (ok)
            c.hold_ms = (uint16_t)v;
    } else if (con_word(&p, "poll")) {
        v = con_num(&p, &n);
        ok = n && v >= 1u && v <= 1000000u;
        if (ok)
            c.poll = v;
    } else
        ok = 0;
    if (!ok) {
        con_puts("? dualcfg show | reset | start boot|after|never | late MS | park on|off | idle sleep|spin | hold MS"
                 " | poll N | boost on|off | ble on|off | policy ble-exclusive|none | nest on|off | c1guard on|off | ins on|off | jobs MASK"
                 " | isr ram|xip | fpause on|off | afterhold on|off | spin ram|xip|nops | pcnt on|off | boostlvl 0..6 | rails V S"
                 " | boost stock|jangada | dcdc on|off | supply early on|off | rfquiet on|off | release MS"
                 " | bledelay MS | fpause stock | stockorder on|off\r\n");
        return;
    }
    c.magic = DUALCFG_MAGIC;
    dualcfg = c;
    settings_save();                                    /* (kept with the settings, applied at the next boot) */
    con_dualcfg_show();
}
#endif

static void con_params(void)
{
    uint32_t i;
    for (i = 0; i < P_COUNT; i++) {
        con_dec((int32_t)i);
        con_putc('=');
        con_dec(TSEL->p[i]);
        con_puts(i % 8u == 7u || i + 1u == P_COUNT ? "\r\n" : " ");
    }
}

/* tabbench: the read speed of main RAM against the pool on the FM-1 (TAB_IN_POOL, docs/MEMORY-MAP.md section 2; the
 * emulator has no memory timing). The same loop (a sine lookup's stride, 2 KiB, int16 reads) over SINE where this
 * build put it, a main RAM range (trk, read only) and a pool range (st_buf, read only); the best of 8 runs of 4,096
 * reads each, in tenths of a ns a read (timer 4, 24 MHz: ~4 ns steps over the run). Interrupts stay on (the audio
 * runs): repeat it, compare the figures with each other, not with a datasheet. CPU0, the loop's code from flash.
 * The flash lines read the app's first 2 / 16 KiB (constants are read the same way): with CACHE_RAM the data cache
 * keeps one 4 KiB way, so a 16 KiB figure worse with it than without means the flash's data goes through the data
 * cache (and the audio's flash tables would pay); the same figure, that it does not (hal/fm1_cache.h) */
extern const int16_t _xip_org[];                     /* (app.ld: the app's first byte in flash) */
static uint32_t con_tab_run(const int16_t *t, uint32_t mask, int32_t *sum)   /* (the sum is printed: every read kept) */
{
    uint32_t r, i, k, t0, d, best = 0xFFFFFFFFu;
    for (r = 0; r < 8u; r++) {
        int32_t a = 0;
        t0 = fm1_ticks();
        for (i = 0, k = 0; i < 4096u; i++, k = (k + 37u) & mask)
            a += t[k];
        d = fm1_ticks() - t0;
        *sum += a;
        if (d < best)
            best = d;
    }
    return best * 10000u / (FM1_TICKS_PER_US * 4096u);
}
static void con_tab_line(const char *name, const void *at, uint32_t mask, int32_t *sum)
{
    con_puts(name);
    con_hex((uint32_t)(uintptr_t)at, 8);
    con_puts("  ");
    con_dec((int32_t)con_tab_run((const int16_t *)at, mask, sum));
    con_puts(" (0.1 ns a read)\r\n");
}
static void con_tabbench(void)
{
    int32_t sum = 0;
    con_tab_line("SINE      ", SINE, 1023u, &sum);
    con_tab_line("main RAM  ", &trk[0], 1023u, &sum);
#if FELUCCA_FLASH
    con_tab_line("pool      ", st_buf, 1023u, &sum);  /* (storage.c: IN_POOL) */
#endif
#if FELUCCA_CACHE_RAM
    if (fm1_cache_ram_ok())                           /* (the undo history's: read only) */
        con_tab_line("cache RAM ", (const void *)(uintptr_t)FM1_CR_LO, 1023u, &sum);
#endif
    con_tab_line("flash 2K  ", _xip_org, 1023u, &sum);
    con_tab_line("flash 16K ", _xip_org, 8191u, &sum);
    con_puts("(checksum ");
    con_dec(sum);
    con_puts(")\r\n");
}

#include "console_mem.c"                              /* membench (a bench build only: FELUCCA_MEMBENCH) */
#include "console_run.c"                              /* play pause rec proj heavy wdtreset (scripted tests) */

#if FELUCCA_BT_AUDIO && FELUCCA_DUAL >= 2
/* `cpu1park`: CPU1 parked and started again exactly as the radio's start-up does (system/dual.c dual_radio_hold /
 * dual_radio_release), with no radio in between: whether the restart itself comes back (2026-10-20: CPU1 "NO START"
 * after a radio start-up, the restart never measured). Run it with CPU1 up (a fresh boot, before any Bluetooth) */
static void con_cpu1park(void)
{
    uint32_t t0;
    con_kv("cpu1_up_before", dual.up);
    con_kv("cpu1_why_before", dual.why);
    dual_radio_hold();
    con_kv("cpu1_parked", dual.parked);
    t0 = fm1_ms;
    while (fm1_ms - t0 < 50u)
        ;
    dual_radio_release();
    con_kv("cpu1_up_after", dual.up);
    con_kv("cpu1_why_after", dual.why);
}
#endif
#if FELUCCA_LAYOUT == 1
static void con_far(void)                            /* the far probe (system/far_probe.c, docs/SD-MODE-DESIGN.md) */
{
    static const char *const R[] = {"ok: code and data past 0x93000 work", "the constant reads wrong (not called)",
                                    "the call returned wrong"};
    uint32_t r = far_check();
    con_puts("far probe ");
    con_puts(R[r]);
    con_puts("\r\n");
    con_bytes("read", (const uint8_t *)far_seen, sizeof far_seen);
    con_kx("at", (uint32_t)(uintptr_t)far_magic);
}
#endif

#if FELUCCA_SDCARD
#include "console_sd.c"                              /* sd: the SD card (storage/sd/sdcard.c) */
#endif
#if FELUCCA_OVERLAY
static void con_ovl(void)                            /* the RAMTEXT overlays (core/overlay.c): the slots, the copies */
{
    static const char *const WHERE[] = {"slots", "ram", "xip", "-"};
    uint32_t k, e;
    con_kv("ovl_bad", rto_st.bad);
    con_kv("slot_bytes", (int32_t)rto_slot_len);
    for (k = 0; k < NPART; k++) {
        con_puts("slot ");
        con_dec((int32_t)k + 1);
        con_puts(" ");
        con_puts(rto_eng[k] && rto_eng[k] <= NENGINES ? ENGINES[rto_eng[k] - 1u]->name : "-");
        con_puts("\r\n");
    }
    for (e = 0; e < NENGINES; e++) {
        uint32_t w = rto_src[e] ? 0u : rto_res[e] ? ((const void *)rto_res[e] >= (const void *)_xip_org ? 2u : 1u) : 3u;
        if (w == 3u && ENGINES[e]->render)
            continue;                                /* (an engine with no overlay code) */
        con_puts(ENGINES[e]->name);
        con_puts(" ");
        con_puts(WHERE[w]);
        if (!w) {
            con_puts(" ");
            con_dec(rto_len[e]);
            con_puts(" B");
        }
        con_puts("\r\n");
    }
    con_kv("loads", (int32_t)rto_st.loads);
    con_kv("copy_us_last", (int32_t)rto_st.us_last);
    con_kv("copy_us_max", (int32_t)rto_st.us_max);
    con_kv("copy_bytes_max", (int32_t)rto_st.bytes_max);
}
#endif

static void con_exec(const char *p)
{
    if (con_run(p))
        return;
    if (con_word(&p, "help") || con_word(&p, "?"))
        con_puts("play  pause  stop  rec on|off  proj load N  heavy [NOTES]  wdtreset  "
                 "status  dbg  crash  boot  "
#if FELUCCA_DUAL && FELUCCA_DUALCFG
                 "dualcfg  reboot  "
#endif
                 "params  tabbench  memr ADDR [LEN]  flr OFF [LEN]"
#if FELUCCA_MEMBENCH && defined(__PI32V2__)
                 "  membench"
#endif
#if FELUCCA_BLE
                 "  blevm  blevmdump  bletrim"
#if BLE_HW_WL82 && FELUCCA_WIFI && FELUCCA_WIFI_CON
                 "  wifiup  wifiscan  wifijoin  wifidata  wifilink  wifibig [N]  wifipow [N]  wifiwcid [N]  wifiswc [N]  wifikey  wifidma  wifinets  wifigo [N]  wififorget N  wifimode [off|man|auto]  wifiman [scan on|off | join N [pw] | drop]"
#endif
#if BLE_DIAG
                 "  blell [clear|regs]"
#endif
#endif
#if FELUCCA_BT_AUDIO
#if FELUCCA_BT_DIAG
                 "  btclk [run]  btinq|btscan [SECONDS] [even|odd|all agc=N win=N frq=N pll=N sweep|nosweep pwr=N prio=N]|stop"
                 "  btpage ADDR [SECONDS] [OPTIONS]|pll=N|agc=N|follow=0|1|stop  bttrace [N]  btrx [N]"
#endif
#if FELUCCA_DUAL >= 2
                 "  cpu1park"
#endif
#endif
#if FELUCCA_LAYOUT == 1
                 "  far"
#endif
#if FELUCCA_SDCARD
                 "  sd [mode duplex|dirrx|bitbang | dma on|off | raw SECTOR | ls [DIR] | cat FILE | bench [FILE] | write FILE]"
#endif
#if FELUCCA_OVERLAY
                 "  ovl"
#endif
                 "  keys  keylog  uboot yes\r\n");
    else if (con_word(&p, "status"))
        con_status();
    else if (con_word(&p, "dbg"))
        con_dbg();
    else if (con_word(&p, "keys"))
        con_keys();
    else if (con_word(&p, "keylog"))
        con_keylog();
    else if (con_word(&p, "crash"))
        con_crash();
    else if (con_word(&p, "boot"))
        con_boot();
#if FELUCCA_DUAL && FELUCCA_DUALCFG
    else if (con_word(&p, "dualcfg"))
        con_dualcfg(p);
    else if (con_word(&p, "reboot")) {
        con_puts("rebooting\r\n");
        con_reboot_req = 1;                            /* (main loop: an intentional reset, not a failed boot) */
    }
#endif
    else if (con_word(&p, "params"))
        con_params();
    else if (con_word(&p, "tabbench"))
        con_tabbench();
#if FELUCCA_LAYOUT == 1
    else if (con_word(&p, "far"))
        con_far();
#endif
#if FELUCCA_SDCARD
    else if (con_word(&p, "sd"))
        con_sd(p);
#endif
#if FELUCCA_OVERLAY
    else if (con_word(&p, "ovl"))
        con_ovl();
#endif
#if FELUCCA_MEMBENCH && defined(__PI32V2__)
    else if (con_word(&p, "membench"))
        con_membench();
#endif
    else if (con_word(&p, "memr"))
        con_memr(p);
#if FELUCCA_FLASH
    else if (con_word(&p, "flr"))
        con_flr(p);
#endif
#if FELUCCA_BLE
    else if (con_word(&p, "blevmdump"))
#if FELUCCA_FLASH
        con_blevmdump();
#else
        con_puts("flash not available\r\n");
#endif
    else if (con_word(&p, "blevm"))
        con_blevm();
    else if (con_word(&p, "bletrim"))
        con_bletrim();
#if BLE_HW_WL82 && FELUCCA_WIFI && FELUCCA_WIFI_CON
    else if (con_word(&p, "wifiup"))
        con_wifiup();
    else if (con_word(&p, "wifilink"))
        con_wifilink(p);
    else if (con_word(&p, "wifiscan"))
        con_wifiscan();
    else if (con_word(&p, "wifidata"))
        con_wifidata();
    else if (con_word(&p, "wifijoin"))
        con_wifijoin(p);
    else if (con_word(&p, "wifibig"))
        con_wifibig(p);
    else if (con_word(&p, "wifipow"))
        con_wifipow(p);
    else if (con_word(&p, "wifiwcid"))
        con_wifiwcid(p);
    else if (con_word(&p, "wifiswc"))
        con_wifiswc(p);
    else if (con_word(&p, "wifikey"))
        con_wifikey(p);
    else if (con_word(&p, "wifidma"))
        con_wifidma(p);
    else if (con_word(&p, "wifinets"))
        con_wifinets(p);
    else if (con_word(&p, "wifigo"))
        con_wifigo(p);
    else if (con_word(&p, "wififorget"))
        con_wififorget(p);
    else if (con_word(&p, "wifimode"))
        con_wifimode(p);
    else if (con_word(&p, "wifiman"))
        con_wifiman(p);
#if FELUCCA_WIFI_AP
    else if (con_word(&p, "wifiap"))
        con_wifiap(p);
#endif
#endif
#if BLE_DIAG
    else if (con_word(&p, "blell"))
        con_blell(p);
#endif
#endif
#if FELUCCA_BT_AUDIO
#if FELUCCA_DUAL >= 2
    else if (con_word(&p, "cpu1park"))
        con_cpu1park();
#endif
#if FELUCCA_BT_DIAG
    else if (con_word(&p, "btclk"))
        con_btclk(p);
    else if (con_word(&p, "btinq"))
        con_btrun(p, BTI_M_INQ);
    else if (con_word(&p, "btscan"))
        con_btrun(p, BTI_M_SCAN);
    else if (con_word(&p, "bttrace"))
        con_bttrace(p);
    else if (con_word(&p, "btrx"))
        con_btrx(p);
    else if (con_word(&p, "btpage"))
        con_btpage(p);
#endif
#endif
    else if (con_word(&p, "uboot")) {
        if (con_word(&p, "yes")) {
            con_puts("entering UBOOT\r\n");
            usb.uboot_req = 1;                         /* main loop: same path as the SysEx key */
        } else {
            con_puts("type 'uboot yes'\r\n");
        }
    } else if (*p)
        con_puts("? (help)\r\n");
}

static void cdc_task(void)                              /* main loop */
{
    if (cdc.dtr && !con.dtr_seen) {
        con_puts("\r\nOptimist (Felucca) ");
        con_puts(FELUCCA_VERSION);
        con_puts(" console - 'help'\r\n> ");
    }
    con.dtr_seen = cdc.dtr;
    while (ci_r != ci_w) {
        char c;
        RING_PUBLISH();                                /* usb_poll (producer) can preempt us */
        c = (char)cdc_in[ci_r % CI_N];
        RING_PUBLISH();
        ci_r++;
        if (c == '\r' || c == '\n') {
            if (c == '\n' && con.len == 0)
                continue;                              /* the LF of a CR LF */
            con_puts("\r\n");
            con.line[con.len] = 0;
            con.stalled = 0;
            con_exec(con.line);
            con.len = 0;
            con_puts("> ");
        } else if (c == 0x7F || c == 0x08) {
            if (con.len) {
                con.len--;
                con_puts("\b \b");
            }
        } else if (c >= ' ' && c < 0x7F && con.len + 1u < CON_LINE) {
            con.line[con.len++] = c;
            con_putc(c);
        }
    }
}
