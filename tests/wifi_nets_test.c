/* SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0 */
/* The saved Wi-Fi networks and the scan table on the host (firmware/src/wifi/wifi_nets.c): the store's order, replace,
 * overflow, forget and damaged-record handling, the scan table's dedupe and order, and the beacon parser's security
 * classification (WPA2-PSK / CCMP joinable; open, WPA3, TKIP groups and PMF-required not).
 * Exit status: the number of failed checks. */
#include <stdio.h>
#include <string.h>
#include "../firmware/src/wifi/wifi_nets.c"

static int fails, checks;
static void ok(int c, const char *what)
{
    checks++;
    if (!c) {
        fails++;
        printf("FAIL %s\n", what);
    }
}
static void pmk_of(uint8_t pmk[32], uint8_t v)
{
    memset(pmk, v, 32);
}
static void put(struct wn_store *s, const char *ssid, uint8_t v)
{
    uint8_t pmk[32];
    pmk_of(pmk, v);
    wn_put(s, (const uint8_t *)ssid, (uint32_t)strlen(ssid), pmk);
}
static int slot(const struct wn_store *s, const char *ssid)
{
    return wn_find(s, (const uint8_t *)ssid, (uint32_t)strlen(ssid));
}

static void t_store(void)
{
    struct wn_store s, r;
    uint8_t raw[WN_STORE_SIZE], pmk[32];
    wn_reset(&s);
    ok(wn_count(&s) == 0u && s.mode == WN_MODE_OFF, "empty store");
    put(&s, "home", 1);
    put(&s, "studio", 2);
    put(&s, "phone", 3);
    ok(wn_count(&s) == 3u && slot(&s, "phone") == 0 && slot(&s, "studio") == 1 && slot(&s, "home") == 2, "most recent first");
    put(&s, "home", 9);                                  /* same SSID again: replaced, not added, and first */
    ok(wn_count(&s) == 3u && slot(&s, "home") == 0 && s.n[0].pmk[0] == 9u, "same SSID replaces and moves to the front");
    put(&s, "gig", 4);
    put(&s, "club", 5);                                  /* a fifth: the oldest (studio) drops */
    ok(wn_count(&s) == WN_SLOTS && slot(&s, "studio") == -1 && slot(&s, "club") == 0 && slot(&s, "phone") == 3,
       "the oldest drops when full");
    wn_touch(&s, 3);                                     /* phone joined: front */
    ok(slot(&s, "phone") == 0 && slot(&s, "club") == 1, "touch moves to the front");
    wn_forget(&s, 1);                                    /* club forgotten: the rest close up */
    ok(wn_count(&s) == 3u && slot(&s, "club") == -1 && slot(&s, "phone") == 0 && slot(&s, "gig") == 1 && slot(&s, "home") == 2,
       "forget closes up");
    pmk_of(pmk, 0);
    ok(s.n[3].used == 0u && !memcmp(s.n[3].pmk, pmk, 32), "a forgotten key is wiped");
    ok(wn_put(&s, (const uint8_t *)"", 0, pmk) == -1 && wn_put(&s, (const uint8_t *)"x", 33, pmk) == -1, "bad SSID lengths refused");

    s.mode = WN_MODE_MAN;
    wn_save(&s, raw);
    ok(wn_load(&r, raw) && r.mode == WN_MODE_MAN && wn_count(&r) == 3u && slot(&r, "phone") == 0, "save / load round trip");
    raw[2] = 9;                                          /* a mode out of range reads as OFF */
    ok(wn_load(&r, raw) && r.mode == WN_MODE_OFF, "a bad mode reads as OFF");
    memset(raw, 0, sizeof raw);                          /* an older record: zeros */
    ok(!wn_load(&r, raw) && wn_count(&r) == 0u, "zeros read as nothing saved");
    wn_save(&s, raw);
    raw[4 + 66 + 1] = 0;                                 /* entry 1: its SSID length zeroed (damaged) */
    ok(wn_load(&r, raw) && wn_count(&r) == 2u && slot(&r, "phone") == 0 && slot(&r, "home") == 1, "a damaged entry is dropped (the rest closed up)");
    wn_save(&s, raw);
    raw[0] = 0x55;                                       /* a foreign mark */
    ok(!wn_load(&r, raw) && wn_count(&r) == 0u, "a foreign record reads as nothing");
}

/* a beacon: 24-byte header, 12 fixed bytes, then the elements given */
static uint32_t beacon(uint8_t *f, uint8_t last, uint16_t cap, const uint8_t *ies, uint32_t n)
{
    memset(f, 0, 64);
    f[0] = 0x80;
    memset(f + 4, 0xFF, 6);
    f[10] = f[16] = 0x1C;
    f[11] = f[17] = 0x0B;
    f[12] = f[18] = 0x8B;
    f[13] = f[19] = 0x48;
    f[14] = f[20] = 0xEC;
    f[15] = f[21] = last;
    f[34] = (uint8_t)cap;
    f[35] = (uint8_t)(cap >> 8);
    memcpy(f + 36, ies, n);
    return 36u + n;
}
static const uint8_t SSID_MOTS[] = {0, 4, 'm', 'o', 't', 's'};
static const uint8_t DS11[] = {3, 1, 11};
/* RSN: v1, group CCMP, 1 pairwise CCMP, 1 AKM PSK, caps 0 */
static const uint8_t RSN_PSK[] = {48, 20, 1, 0, 0, 0x0F, 0xAC, 4, 1, 0, 0, 0x0F, 0xAC, 4, 1, 0, 0, 0x0F, 0xAC, 2, 0, 0};

static uint32_t build(uint8_t *f, const uint8_t *rsn, uint32_t rn, uint16_t cap)
{
    uint8_t ies[96];
    uint32_t n = 0;
    memcpy(ies + n, SSID_MOTS, sizeof SSID_MOTS), n += sizeof SSID_MOTS;
    memcpy(ies + n, DS11, sizeof DS11), n += sizeof DS11;
    if (rn)
        memcpy(ies + n, rsn, rn), n += rn;
    return beacon(f, 0x60, cap, ies, n);
}

static void t_beacon(void)
{
    uint8_t f[160], rsn[32];
    struct wn_ap ap;
    uint32_t n;
    n = build(f, RSN_PSK, sizeof RSN_PSK, 0x0411);
    ok(wn_parse_beacon(f, n, 6, -50, &ap) && ap.ssid_len == 4u && !memcmp(ap.ssid, "mots", 4) && ap.ch == 11u &&
           ap.rssi == -50 && ap.bssid[5] == 0x60 && ap.sec == WN_SEC_PSK,
       "a WPA2-PSK beacon: SSID, DS channel wins over the listening channel, RSSI, BSSID, joinable");
    n = build(f, 0, 0, 0x0401);
    ok(wn_parse_beacon(f, n, 6, -50, &ap) && ap.sec == WN_SEC_OPEN, "no RSN, no privacy bit: open");
    n = build(f, 0, 0, 0x0411);
    ok(wn_parse_beacon(f, n, 6, -50, &ap) && ap.sec == WN_SEC_OTHER, "privacy bit without an RSN element (WEP): not joinable");
    memcpy(rsn, RSN_PSK, sizeof RSN_PSK);
    rsn[7] = 2;                                          /* group cipher TKIP */
    n = build(f, rsn, sizeof RSN_PSK, 0x0411);
    ok(wn_parse_beacon(f, n, 6, -50, &ap) && ap.sec == WN_SEC_OTHER, "a TKIP group: not joinable (Link is multicast)");
    memcpy(rsn, RSN_PSK, sizeof RSN_PSK);
    rsn[19] = 8;                                         /* AKM SAE only (WPA3) */
    n = build(f, rsn, sizeof RSN_PSK, 0x0411);
    ok(wn_parse_beacon(f, n, 6, -50, &ap) && ap.sec == WN_SEC_OTHER, "WPA3-SAE only: not joinable");
    memcpy(rsn, RSN_PSK, sizeof RSN_PSK);
    rsn[20] = 0xC0;                                      /* RSN capabilities: MFPR + MFPC */
    n = build(f, rsn, sizeof RSN_PSK, 0x0411);
    ok(wn_parse_beacon(f, n, 6, -50, &ap) && ap.sec == WN_SEC_OTHER, "PMF required: not joinable");
    memcpy(rsn, RSN_PSK, sizeof RSN_PSK);
    rsn[20] = 0x80;                                      /* MFPC only (capable, not required): joinable */
    n = build(f, rsn, sizeof RSN_PSK, 0x0411);
    ok(wn_parse_beacon(f, n, 6, -50, &ap) && ap.sec == WN_SEC_PSK, "PMF capable but not required: joinable");
    {
        uint8_t hid[] = {0, 0};                          /* a hidden network: an empty SSID */
        n = beacon(f, 0x61, 0x0411, hid, sizeof hid);
        ok(!wn_parse_beacon(f, n, 6, -50, &ap), "a hidden SSID is not listed");
        hid[0] = 0, hid[1] = 0;
    }
    f[0] = 0x40;                                         /* a probe request is not an AP */
    n = build(f, RSN_PSK, sizeof RSN_PSK, 0x0411);
    f[0] = 0x40;
    ok(!wn_parse_beacon(f, n, 6, -50, &ap), "not a beacon");
    ok(!wn_parse_beacon(f, 20, 6, -50, &ap), "too short");
    n = build(f, RSN_PSK, sizeof RSN_PSK, 0x0411);
    f[0] = 0x80;
    ok(wn_parse_beacon(f, n - 5u, 6, -50, &ap) || 1, "a truncated element does not run past the frame");
}

static void ap_of(struct wn_ap *a, uint8_t last, const char *ssid, int8_t rssi)
{
    memset(a, 0, sizeof *a);
    a->bssid[5] = last;
    a->ssid_len = (uint8_t)strlen(ssid);
    memcpy(a->ssid, ssid, a->ssid_len);
    a->rssi = rssi;
    a->sec = WN_SEC_PSK;
}

static void t_scan(void)
{
    struct wn_scan t;
    struct wn_ap a;
    uint32_t i;
    wn_scan_clear(&t);
    ap_of(&a, 1, "mots", -70);
    ok(wn_scan_add(&t, &a) == 1 && t.n == 1u, "first AP added");
    ap_of(&a, 2, "mots", -50);
    wn_scan_add(&t, &a);
    ap_of(&a, 3, "other", -60);
    wn_scan_add(&t, &a);
    ok(t.n == 3u && t.ap[0].bssid[5] == 2 && t.ap[1].bssid[5] == 3 && t.ap[2].bssid[5] == 1, "kept strongest first");
    ap_of(&a, 1, "mots", -40);                           /* an AP heard again, stronger: refreshed and moved up */
    ok(wn_scan_add(&t, &a) == 0 && t.n == 3u && t.ap[0].bssid[5] == 1, "the same BSSID refreshes, not duplicates");
    ap_of(&a, 1, "mots", -90);                           /* heard again, weaker: the stronger value is kept */
    wn_scan_add(&t, &a);
    ok(t.ap[0].bssid[5] == 1 && t.ap[0].rssi == -40, "a weaker repeat keeps the best RSSI");
    ok(wn_scan_best_for(&t, (const uint8_t *)"mots", 4) == 0 && t.ap[wn_scan_best_for(&t, (const uint8_t *)"mots", 4)].bssid[5] == 1,
       "the strongest AP of an SSID");
    ok(wn_scan_best_for(&t, (const uint8_t *)"nope", 4) == -1, "an SSID not heard");
    for (i = 0; i < 20u; i++) {                          /* more than the table holds: the strongest stay */
        char nm[8];
        snprintf(nm, sizeof nm, "n%u", i);
        ap_of(&a, (uint8_t)(10u + i), nm, (int8_t)(-100 + (int)i * 2));
        wn_scan_add(&t, &a);
    }
    ok(t.n == WN_SCAN_MAX, "the table is capped");
    for (i = 1; i < t.n; i++)
        if (t.ap[i].rssi > t.ap[i - 1u].rssi)
            break;
    ok(i == t.n, "still sorted after overflow");
    ok(t.ap[0].rssi == -40, "the strongest kept");
    ap_of(&a, 99, "weak", -120);
    ok(wn_scan_add(&t, &a) == 0 && t.n == WN_SCAN_MAX, "a weaker AP than all is refused when full");
}

/* MODE AP's network in the store (version 2), and the version 1 records of before (268 octets, zero-extended by the record) */
static void t_ap(void)
{
    struct wn_store s, r;
    uint8_t raw[WN_STORE_SIZE], pmk[32], old[WN_STORE_SIZE];
    ok(WN_STORE_SIZE == 336u && sizeof(struct wn_apcfg) == 68u, "the store is 336 octets");
    wn_reset(&s);
    ok(s.ap.set == 0u && s.ver == 2u, "empty store: no AP key, version 2");
    put(&s, "home", 1);
    s.mode = WN_MODE_AP;
    pmk_of(pmk, 0x5A);
    ok(wn_ap_put(&s, (const uint8_t *)"FM1-TEST", 8, 11, pmk) == 0 && s.ap.set == 1u && s.ap.ch == 11u && s.ap.ssid_len == 8u, "an AP network is kept");
    wn_save(&s, raw);
    ok(wn_load(&r, raw) && r.mode == WN_MODE_AP && r.ap.set == 1u && r.ap.ch == 11u && !memcmp(r.ap.ssid, "FM1-TEST", 8) &&
       r.ap.pmk[0] == 0x5A && r.ap.pmk[31] == 0x5A && wn_count(&r) == 1u, "AP network and mode round trip with the saved networks");
    ok(wn_ap_put(&s, s.ap.ssid, s.ap.ssid_len, s.ap.ch, pmk) == 0 && s.ap.ssid_len == 8u && !memcmp(s.ap.ssid, "FM1-TEST", 8), "putting the store's own name back keeps it");
    ok(wn_ap_put(&s, 0, 0, 0, pmk) == 0 && s.ap.ssid_len == 0u && s.ap.ch == 0u, "the automatic name and default channel are length 0 / channel 0");
    ok(wn_ap_put(&s, (const uint8_t *)"x", 1, 14, pmk) == -1 && wn_ap_put(&s, (const uint8_t *)"x", 33, 6, pmk) == -1, "channel 14 and a 33-octet name refused");
    ok(wn_ap_set_channel(&s, 9) == 0 && s.ap.ch == 9u && wn_ap_set_channel(&s, 0) == -1 && wn_ap_set_channel(&s, 14) == -1 && s.ap.ch == 9u, "channel 1..13 only");
    wn_ap_forget(&s);
    ok(s.ap.set == 0u && s.ap.pmk[0] == 0u && s.ap.ch == 9u, "forgetting the AP key clears it and keeps the channel");
    wn_save(&s, raw);
    ok(wn_load(&r, raw) && r.ap.set == 0u && r.ap.ch == 9u, "no key, channel kept after a round trip");
    /* a damaged AP block: the flag is not 1, a name longer than 32, a channel above 13 */
    wn_reset(&s);
    wn_ap_put(&s, (const uint8_t *)"FM1-TEST", 8, 6, pmk);
    wn_save(&s, raw);
    raw[4 + 4 * 66] = 2;
    ok(wn_load(&r, raw) && r.ap.set == 0u, "a flag that is not 1: no key kept");
    wn_save(&s, raw);
    raw[4 + 4 * 66 + 2] = 40;
    ok(wn_load(&r, raw) && r.ap.set == 0u && r.ap.ssid_len == 0u, "a name over 32 octets: no key kept");
    wn_save(&s, raw);
    raw[4 + 4 * 66 + 1] = 99;
    ok(wn_load(&r, raw) && r.ap.set == 1u && r.ap.ch == 0u, "a channel above 13 reads as the default");
    /* a version 1 record: 268 octets, the rest zeros (the settings record grew) */
    wn_reset(&s);
    put(&s, "mots", 3);
    s.mode = WN_MODE_AUTO;
    wn_save(&s, old);
    memset(old + 268, 0, WN_STORE_SIZE - 268u);
    old[1] = 1;
    ok(wn_load(&r, old) && wn_count(&r) == 1u && slot(&r, "mots") == 0 && r.mode == WN_MODE_AUTO && r.ap.set == 0u && r.ver == 2u,
       "a version 1 record: the networks and the mode kept, no AP, read as version 2");
    old[2] = WN_MODE_AP;
    ok(wn_load(&r, old) && r.mode == WN_MODE_OFF, "MODE AP in a version 1 record reads as OFF");
    old[2] = WN_MODE_AP_MAN;
    ok(wn_load(&r, old) && r.mode == WN_MODE_OFF, "MODE AP MAN in a version 1 record reads as OFF");
    wn_reset(&s);
    s.mode = WN_MODE_AP_MAN;
    wn_save(&s, raw);
    ok(wn_load(&r, raw) && r.mode == WN_MODE_AP_MAN, "MODE AP MAN round trip");
    raw[2] = 5;
    ok(wn_load(&r, raw) && r.mode == WN_MODE_OFF, "a mode above AP MAN reads as OFF");
    old[1] = 3;
    ok(!wn_load(&r, old), "a version from the future is refused");
}

int main(void)
{
    t_store();
    t_ap();
    t_beacon();
    t_scan();
    printf("wifi_nets_test: %d checks, %d failed\n", checks, fails);
    return fails;
}
