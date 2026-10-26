/* SPDX-License-Identifier: GPL-3.0-only */
/* WPA2-PSK Station (IEEE 802.11i / 802.11-2020 §12.7): PTK derivation, 4-way handshake
 * state machine (EAPOL-Key M1-M4), GTK unwrapping, and MAC key table installation. */
#ifndef WIFI_STA_H
#define WIFI_STA_H
#include <stdint.h>
#include "wifi_crypto.h"

#define WIFI_STA_SSID_MAX 32u
#define WIFI_STA_EAPOL_MAX 256u

enum wifi_sta_state {
    WIFI_STA_STATE_IDLE = 0,
    WIFI_STA_STATE_WAIT_M1,
    WIFI_STA_STATE_WAIT_M3,
    WIFI_STA_STATE_CONNECTED,
    WIFI_STA_STATE_FAILED,
};

struct wifi_sta_ptk {
    uint8_t kck[16];                       /* Key Confirmation Key (EAPOL MIC) */
    uint8_t kek[16];                       /* Key Encryption Key (AES unwrap) */
    uint8_t tk[16];                        /* Temporal Key (CCMP pairwise key) */
};

struct wifi_sta {
    enum wifi_sta_state state;
    uint8_t mac[6];                        /* Station MAC address (SPA) */
    uint8_t bssid[6];                      /* AP BSSID / MAC address (AA) */
    uint8_t ssid[WIFI_STA_SSID_MAX];
    uint32_t ssid_len;
    uint8_t pmk[32];                       /* Pairwise Master Key */
    uint8_t snonce[32];                    /* Station Nonce */
    uint8_t anonce[32];                    /* AP Nonce */
    struct wifi_sta_ptk ptk;               /* Derived PTK */
    uint8_t gtk[16];                       /* Group Temporal Key */
    uint8_t gtk_id;                        /* GTK Key ID (1..3) */
    uint64_t replay_cnt;                   /* Last seen replay counter */
    void (*rand)(uint8_t *out, uint8_t n); /* random bytes for the SNonce (firmware: ble_hw_rand) */
};

void wifi_sta_init(struct wifi_sta *sta, const uint8_t sta_mac[6], void (*rand)(uint8_t *out, uint8_t n));
void wifi_sta_set_creds(struct wifi_sta *sta, const char *pass, const uint8_t *ssid, uint32_t ssid_len);
void wifi_sta_set_ap(struct wifi_sta *sta, const uint8_t bssid[6]);
void wifi_sta_derive_ptk(const uint8_t pmk[32], const uint8_t spa[6], const uint8_t aa[6],
                         const uint8_t snonce[32], const uint8_t anonce[32], uint8_t ptk[48]);

/* Feed a received EAPOL-Key packet. If a response packet (M2 or M4) is produced, returns 1
 * and writes length into tx_len; returns 0 if consumed with no response, -1 on error. */
int wifi_sta_rx_eapol(struct wifi_sta *sta, const uint8_t *pkt, uint32_t len, uint8_t *tx_buf, uint32_t *tx_len);

/* Install derived PTK and GTK into RT3070 MAC hardware tables (0x1800, 0x4000, 0x6000, 0x6800, 0x6C00, 0x7000). */
void wifi_sta_install_hw_keys(const struct wifi_sta *sta);

#endif
