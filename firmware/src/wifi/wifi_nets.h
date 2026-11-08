/* SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0 */
/* The Wi-Fi networks the FM-1 knows: up to WN_SLOTS saved (SSID + the WPA2 pairwise master key), and the table of the
 * access points heard in a scan. Pure data and parsing, no radio: the same code runs in tests/wifi_nets_test.c.
 *
 * Saved networks keep the PMK, not the passphrase: PBKDF2-HMAC-SHA1(passphrase, SSID, 4096) is all a WPA2-PSK join
 * needs, so the plain password never sits in flash and a join skips the derivation. The PMK is as good as the password
 * for that one network, so it is kept in the settings record only (storage/persist_rec.c persist_t.wifi_nets, the
 * CRC-checked settings object) and never shown. Most recently used first. */
#ifndef WIFI_NETS_H
#define WIFI_NETS_H
#include <stdint.h>

#define WN_SLOTS 4u
#define WN_GROUPS 9u                     /* JANK groups 1..WN_GROUPS */
#define WN_SSID_MAX 32u
#define WN_MARK 0xA7u                    /* a valid store */
#define WN_VER 2u                        /* 1: the saved networks only (a record of 268 octets, read as the first part of this) */
#define WN_STORE_SIZE 336u               /* header 4 + 4 entries x 66 + the access point's 68 */
enum { WN_MODE_OFF = 0, WN_MODE_AUTO = 1, WN_MODE_MAN = 2, WN_MODE_AP = 3, WN_MODE_AP_MAN = 4, WN_MODE_JANK = 5, WN_MODE_JANK_HOST = 6, WN_MODE_JANK_AUTO = 7 };   /* JANK_AUTO: JANK that also becomes the host when none of its group is heard (the trial's stack only) */   /* JANK_HOST: AP AUTO under the automatic name FM1-xxxx and its default password only (never a typed one: so JANK finds and joins it), Link kept on (the user, 2026-11-03) */   /* JANK: AUTO, and an Optimist access point (FM1-xxxx) is joined with its default password when no saved network is heard (the trial's stack only) */   /* as the Bluetooth pages: OFF the radio stays off;
                                                                  * MAN it joins when told (the page's SAVE); AUTO the best saved network
                                                                  * heard, at start-up and when none is joined; AP the FM-1 is the
                                                                  * access point (wifi/wifi_apd.c), at power-on and
                                                                  * kept up ("AP AUTO"); AP_MAN the same, but only started
                                                                  * by SAVE on the page (or the web editor) and off at power-on */

struct wn_net {                          /* 66 octets */
    uint8_t used, ssid_len;
    uint8_t ssid[WN_SSID_MAX];
    uint8_t pmk[32];
};
struct wn_apcfg {                        /* MODE AP's network: the name, the channel, the WPA2 key of that name (never the password) */
    uint8_t set;                         /* 1: a key is kept (no access point is run without one) */
    uint8_t ch;                          /* 1..13; 0: the default (6) */
    uint8_t ssid_len;                    /* 0: the automatic name (FM1- and the last four digits of the radio's address) */
    uint8_t grp;                         /* the JANK group, 1..9 (0: 1; the trial's stack: FM1-xxxx-N for 2..9) */
    uint8_t ssid[WN_SSID_MAX];
    uint8_t pmk[32];
};
struct wn_store {
    uint8_t mark, ver, mode, rsv;
    struct wn_net n[WN_SLOTS];
    struct wn_apcfg ap;
};
_Static_assert(sizeof(struct wn_net) == 66u && sizeof(struct wn_apcfg) == 68u && sizeof(struct wn_store) == WN_STORE_SIZE,
               "the Wi-Fi store: 336 octets");

void wn_reset(struct wn_store *s);
/* from the settings record's octets (zeros: an older record) -> 1 something valid was there; damaged entries dropped */
int wn_load(struct wn_store *s, const uint8_t raw[WN_STORE_SIZE]);
void wn_save(const struct wn_store *s, uint8_t raw[WN_STORE_SIZE]);
uint32_t wn_count(const struct wn_store *s);
int wn_find(const struct wn_store *s, const uint8_t *ssid, uint32_t len);          /* slot, or -1 */
/* save (or replace: same SSID) a network at the front; the oldest drops when all slots are used. 0, or -1 for a bad SSID */
int wn_put(struct wn_store *s, const uint8_t *ssid, uint32_t len, const uint8_t pmk[32]);
void wn_touch(struct wn_store *s, uint32_t slot);                                   /* joined: to the front */
void wn_forget(struct wn_store *s, uint32_t slot);                                  /* its entry and key go; the rest close up */
/* MODE AP: the name (len 0: the automatic one), the channel (0: the default) and the key of that name. 0, or -1 for a bad name / channel */
int wn_ap_put(struct wn_store *s, const uint8_t *ssid, uint32_t len, uint32_t ch, const uint8_t pmk[32]);
void wn_ap_forget(struct wn_store *s);                                              /* the key goes (the name and channel stay) */
int wn_ap_set_channel(struct wn_store *s, uint32_t ch);                             /* 1..13, else -1 */

/* ---- the access points heard */
#define WN_SCAN_MAX 12u
enum { WN_SEC_OTHER = 0, WN_SEC_PSK = 1, WN_SEC_OPEN = 2 };   /* PSK: WPA2-PSK, pairwise and group CCMP, PMF not required */
struct wn_ap {
    uint8_t bssid[6];
    uint8_t ch, ssid_len, sec;
    int8_t rssi;                         /* the receive header's RSSI byte (dBm, signed) */
    uint8_t ssid[WN_SSID_MAX];
};
struct wn_scan {
    uint8_t n;
    struct wn_ap ap[WN_SCAN_MAX];
};
void wn_scan_clear(struct wn_scan *t);
/* a beacon / probe response (802.11 frame, RXWI already stripped; len bytes) -> 1 and *out filled; 0: not one, or no
 * SSID (a hidden network) or too short. ch: the channel the radio listened on (the DS parameter, when there, wins) */
int wn_parse_beacon(const uint8_t *f, uint32_t len, uint8_t ch, int8_t rssi, struct wn_ap *out);
/* add or refresh by BSSID (the stronger RSSI kept); the table keeps the strongest WN_SCAN_MAX. 1 if a new BSSID */
int wn_scan_add(struct wn_scan *t, const struct wn_ap *ap);
/* the table's rows, strongest first (the table itself is kept sorted) */
int wn_scan_best_for(const struct wn_scan *t, const uint8_t *ssid, uint32_t len);   /* strongest AP of that SSID, or -1 */
#endif
