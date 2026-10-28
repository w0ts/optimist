/* SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0 */
/* WPA2-PSK Station (IEEE 802.11i / 802.11-2020 §12.7): PTK derivation, 4-way handshake
 * state machine (EAPOL-Key M1-M4), GTK unwrapping, 802.11 Auth/Assoc framing, LLC/SNAP
 * EAPOL encapsulation, MAC address discovery, and MAC key table installation. */
#ifndef WIFI_STA_H
#define WIFI_STA_H
#include <stdint.h>
#include "wifi_crypto.h"

#define WIFI_STA_SSID_MAX 32u
#define WIFI_STA_EAPOL_MAX 256u

enum wifi_sta_state {
    WIFI_STA_STATE_IDLE = 0,
    WIFI_STA_STATE_AUTH,
    WIFI_STA_STATE_ASSOC,
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
    uint16_t aid;                          /* Assigned Association ID */
    uint16_t tx_seq;                       /* Transmit sequence counter */
    void (*rand)(uint8_t *out, uint8_t n); /* random bytes for the SNonce (firmware: ble_hw_rand) */
};

void wifi_sta_init(struct wifi_sta *sta, const uint8_t sta_mac[6], void (*rand)(uint8_t *out, uint8_t n));
void wifi_sta_set_creds(struct wifi_sta *sta, const char *pass, const uint8_t *ssid, uint32_t ssid_len);
void wifi_sta_set_pmk(struct wifi_sta *sta, const uint8_t pmk[32], const uint8_t *ssid, uint32_t ssid_len);   /* a saved key */
void wifi_sta_set_ap(struct wifi_sta *sta, const uint8_t bssid[6]);
void wifi_sta_derive_ptk(const uint8_t pmk[32], const uint8_t spa[6], const uint8_t aa[6],
                         const uint8_t snonce[32], const uint8_t anonce[32], uint8_t ptk[48]);

/* Feed a received EAPOL-Key packet. If a response packet (M2 or M4) is produced, returns 1
 * and writes length into tx_len; returns 0 if consumed with no response, -1 on error. */
int wifi_sta_rx_eapol(struct wifi_sta *sta, const uint8_t *pkt, uint32_t len, uint8_t *tx_buf, uint32_t *tx_len);

/* Install derived PTK and GTK into RT3070 MAC hardware tables (0x1800, 0x4000, 0x6000, 0x6800, 0x6C00, 0x7000). */
void wifi_sta_install_hw_keys(const struct wifi_sta *sta);
void wifi_sta_clear_hw_keys(void);                 /* WCID 1 back to no cipher, no key (before a join, on leaving) */
uint32_t wifi_sta_build_deauth(struct wifi_sta *sta, uint8_t *tx_buf);   /* leaving: reason 3 */

/* 802.11 Authentication Frame: builds Open System Auth (Seq 1) frame. Returns frame byte count. */
uint32_t wifi_sta_build_auth(struct wifi_sta *sta, uint8_t *tx_buf);

/* Parses 802.11 Auth Response from AP (Seq 2, Status 0). Returns 0 on success, -1 on error. */
int wifi_sta_parse_auth_resp(const struct wifi_sta *sta, const uint8_t *frame, uint32_t len);

/* 802.11 Association Request Frame: builds Assoc Req with SSID, Rates, Ext Rates, RSN IE. Returns frame byte count. */
uint32_t wifi_sta_build_assoc_req(struct wifi_sta *sta, uint8_t *tx_buf);

/* Parses 802.11 Assoc Response from AP (Status 0, records AID). Returns 0 on success, -1 on error. */
int wifi_sta_parse_assoc_resp(struct wifi_sta *sta, const uint8_t *frame, uint32_t len);

/* Encapsulates an EAPOL packet in an 802.11 Data frame with LLC/SNAP header (AA-AA-03-00-00-00-88-8E).
 * Returns frame byte count. */
uint32_t wifi_sta_encap_eapol(struct wifi_sta *sta, const uint8_t *eapol, uint32_t eapol_len, uint8_t *tx_buf);

/* Processes an incoming 802.11 frame (Auth Resp, Assoc Resp, or EAPOL Data). If an EAPOL response
 * (M2/M4 wrapped in 802.11 Data + LLC/SNAP) is produced, returns 1 and sets tx_len; returns 0 if consumed, -1 on error. */
int wifi_sta_rx_frame(struct wifi_sta *sta, const uint8_t *frame, uint32_t len, uint8_t *tx_buf, uint32_t *tx_len);

/* Retrieves Station MAC address from VM record 102 (BTIF 0x0E9000), applies locally-administered bit (bit 1 of byte 0).
 * Falls back to 02:11:22:33:44:55 if absent. Returns 1 if from VM, 0 if fallback. */
int wifi_sta_get_mac(uint8_t mac[6]);

/* Programs Station MAC (0x1008/0x100C), Target BSSID (0x1010/0x1014), and Radio Channel (0x1405C). */
void wifi_sta_set_mac_regs(const uint8_t sta_mac[6], const uint8_t bssid[6], uint32_t channel);

#endif
