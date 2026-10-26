/* SPDX-License-Identifier: GPL-3.0-only */
/* WPA2-PSK Station (IEEE 802.11i / 802.11-2020 §12.7): PTK derivation, 4-way handshake
 * state machine (EAPOL-Key M1-M4), GTK unwrapping, and MAC key table installation. */
#include "wifi_sta.h"

#if defined(__PI32V2__) || defined(FM1_FIRMWARE)
/* (the firmware's own C library, system/libc.c: no <string.h> in the toolchain's freestanding build) */
void *memcpy(void *d, const void *s, unsigned n);
void *memset(void *d, int c, unsigned n);
int memcmp(const void *a, const void *b, unsigned n);
#include "hal/fm1_wifi.h"
#define STA_MAC_WR(r, v) fm1_wifi_mac_wr((r), (v))
#else
#include <string.h>
/* Host stub / hook for testing */
static void (*sta_test_mac_wr_fn)(uint32_t r, uint32_t v) = 0;
void wifi_sta_set_test_mac_wr(void (*fn)(uint32_t r, uint32_t v)) { sta_test_mac_wr_fn = fn; }
#define STA_MAC_WR(r, v) do { if (sta_test_mac_wr_fn) sta_test_mac_wr_fn((r), (v)); } while (0)
#endif

/* Standard WPA2-PSK RSN Information Element for Station (22 bytes) */
static const uint8_t sta_rsn_ie[22] = {
    0x30, 0x14,                                     /* Tag 0x30 (RSN), length 20 */
    0x01, 0x00,                                     /* Version 1 */
    0x00, 0x0F, 0xAC, 0x04,                         /* Group cipher: AES-CCMP */
    0x01, 0x00,                                     /* Pairwise cipher count: 1 */
    0x00, 0x0F, 0xAC, 0x04,                         /* Pairwise cipher: AES-CCMP */
    0x01, 0x00,                                     /* AKM suite count: 1 */
    0x00, 0x0F, 0xAC, 0x02,                         /* AKM: PSK (00-0F-AC-2) */
    0x00, 0x00                                      /* RSN capabilities */
};

void wifi_sta_init(struct wifi_sta *sta, const uint8_t sta_mac[6], void (*rand)(uint8_t *out, uint8_t n))
{
    memset(sta, 0, sizeof(*sta));
    if (sta_mac)
        memcpy(sta->mac, sta_mac, 6);
    sta->state = WIFI_STA_STATE_IDLE;
    sta->rand = rand;
}

void wifi_sta_set_creds(struct wifi_sta *sta, const char *pass, const uint8_t *ssid, uint32_t ssid_len)
{
    if (ssid_len > WIFI_STA_SSID_MAX)
        ssid_len = WIFI_STA_SSID_MAX;
    memcpy(sta->ssid, ssid, ssid_len);
    sta->ssid_len = ssid_len;
    wc_pmk(pass, ssid, ssid_len, sta->pmk);
}

void wifi_sta_set_ap(struct wifi_sta *sta, const uint8_t bssid[6])
{
    memcpy(sta->bssid, bssid, 6);
    sta->state = WIFI_STA_STATE_WAIT_M1;
}

void wifi_sta_derive_ptk(const uint8_t pmk[32], const uint8_t spa[6], const uint8_t aa[6],
                         const uint8_t snonce[32], const uint8_t anonce[32], uint8_t ptk[48])
{
    uint8_t data[76];
    const uint8_t *min_mac, *max_mac;
    const uint8_t *min_nonce, *max_nonce;

    if (memcmp(spa, aa, 6) < 0) {
        min_mac = spa;
        max_mac = aa;
    } else {
        min_mac = aa;
        max_mac = spa;
    }

    if (memcmp(snonce, anonce, 32) < 0) {
        min_nonce = snonce;
        max_nonce = anonce;
    } else {
        min_nonce = anonce;
        max_nonce = snonce;
    }

    memcpy(data, min_mac, 6);
    memcpy(data + 6, max_mac, 6);
    memcpy(data + 12, min_nonce, 32);
    memcpy(data + 44, max_nonce, 32);

    wc_prf(pmk, 32, "Pairwise key expansion", data, 76, ptk, 48);
}

/* the SNonce: from the caller's random source (the firmware: the radio's, ble_hw_rand), never a predictable sequence */
static void gen_snonce(struct wifi_sta *sta, uint8_t nonce[32])
{
    sta->rand(nonce, 32);
}

static uint64_t read_u64_be(const uint8_t *p)
{
    uint64_t v = 0;
    uint32_t i;
    for (i = 0; i < 8u; i++)
        v = (v << 8) | p[i];
    return v;
}

static void write_u16_be(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void compute_eapol_mic(const uint8_t kck[16], const uint8_t *pkt, uint32_t len, uint8_t mic[16])
{
    uint8_t tmp[WIFI_STA_EAPOL_MAX];
    uint8_t digest[20];
    if (len > sizeof(tmp))
        return;
    memcpy(tmp, pkt, len);
    memset(tmp + 81, 0, 16);
    wc_hmac_sha1(kck, 16, tmp, len, digest);
    memcpy(mic, digest, 16);
}

void wifi_sta_install_hw_keys(const struct wifi_sta *sta)
{
    uint32_t i, w;

    /* 1. Program AP BSSID into WCID entry 1 (0x1808) */
    w = (uint32_t)sta->bssid[0] | ((uint32_t)sta->bssid[1] << 8) |
        ((uint32_t)sta->bssid[2] << 16) | ((uint32_t)sta->bssid[3] << 24);
    STA_MAC_WR(0x1808u, w);
    w = (uint32_t)sta->bssid[4] | ((uint32_t)sta->bssid[5] << 8);
    STA_MAC_WR(0x180Cu, w);

    /* 2. Program Pairwise Key (TK) into Pairwise Key Table entry 1 (0x4020) */
    for (i = 0; i < 4u; i++) {
        w = (uint32_t)sta->ptk.tk[4 * i] | ((uint32_t)sta->ptk.tk[4 * i + 1] << 8) |
            ((uint32_t)sta->ptk.tk[4 * i + 2] << 16) | ((uint32_t)sta->ptk.tk[4 * i + 3] << 24);
        STA_MAC_WR(0x4020u + 4u * i, w);
    }
    /* Clear MIC keys for CCMP */
    for (i = 4; i < 8u; i++)
        STA_MAC_WR(0x4020u + 4u * i, 0);

    /* 3. Program Group Key (GTK) into Shared Key Table (0x6C00 + gtk_id * 32) */
    if (sta->gtk_id < 4u) {
        uint32_t base = 0x6C00u + (uint32_t)sta->gtk_id * 32u;
        for (i = 0; i < 4u; i++) {
            w = (uint32_t)sta->gtk[4 * i] | ((uint32_t)sta->gtk[4 * i + 1] << 8) |
                ((uint32_t)sta->gtk[4 * i + 2] << 16) | ((uint32_t)sta->gtk[4 * i + 3] << 24);
            STA_MAC_WR(base + 4u * i, w);
        }
        for (i = 4; i < 8u; i++)
            STA_MAC_WR(base + 4u * i, 0);

        /* Set Shared Key Mode (0x7000): cipher 4 (AES-CCMP) for this key */
        w = 4u << ((uint32_t)sta->gtk_id * 4u);
        STA_MAC_WR(0x7000u, w);
    }

    /* 4. Configure WCID Attribute Table (0x6800 + 1 * 4 = 0x6804): cipher 4 (AES-CCMP) */
    STA_MAC_WR(0x6804u, 0x04u);

    /* 5. Clear IV/EIV for WCID 1 (0x6008) */
    STA_MAC_WR(0x6008u, 0);
    STA_MAC_WR(0x600Cu, 0);
}

int wifi_sta_rx_eapol(struct wifi_sta *sta, const uint8_t *pkt, uint32_t len, uint8_t *tx_buf, uint32_t *tx_len)
{
    uint16_t body_len, key_info, key_data_len;
    uint8_t desc_type;
    uint64_t replay;

    if (!sta || !pkt || len < 99u || !tx_buf || !tx_len)
        return -1;

    /* Check EAPOL Type = 3 (EAPOL-Key) */
    if (pkt[1] != 0x03u)
        return -1;

    body_len = ((uint16_t)pkt[2] << 8) | pkt[3];
    if ((uint32_t)body_len + 4u > len)
        return -1;

    desc_type = pkt[4];
    if (desc_type != 0x02u)                         /* 2 = RSN (802.11i) */
        return -1;

    len = 4u + body_len;                            /* (the EAPOL frame itself: any padding after it is not MIC'd) */
    key_info = ((uint16_t)pkt[5] << 8) | pkt[6];
    replay = read_u64_be(pkt + 9);
    key_data_len = ((uint16_t)pkt[97] << 8) | pkt[98];
    if (99u + key_data_len > len)                   /* (key data past the frame: refused) */
        return -1;

    /* Must be Pairwise key */
    if (!(key_info & (1u << 3)))
        return -1;

    /* Message 1 from AP: Key ACK=1, MIC=0 */
    if ((key_info & (1u << 7)) && !(key_info & (1u << 8))) {
        sta->replay_cnt = replay;
        memcpy(sta->anonce, pkt + 17, 32);

        /* Generate SNonce and derive PTK */
        gen_snonce(sta, sta->snonce);
        wifi_sta_derive_ptk(sta->pmk, sta->mac, sta->bssid, sta->snonce, sta->anonce, (uint8_t *)&sta->ptk);

        /* Build Message 2 */
        memset(tx_buf, 0, 121);
        tx_buf[0] = 0x01;                           /* 802.1X-2001 */
        tx_buf[1] = 0x03;                           /* EAPOL-Key */
        write_u16_be(tx_buf + 2, 95u + sizeof(sta_rsn_ie)); /* Body length */
        tx_buf[4] = 0x02;                           /* RSN Key Descriptor */
        /* Key Info: Version 2 (HMAC-SHA1/AES), Key Type Pairwise, Key MIC */
        write_u16_be(tx_buf + 5, 0x010Au);
        write_u16_be(tx_buf + 7, 16u);              /* Key Length */
        memcpy(tx_buf + 9, pkt + 9, 8);             /* Replay counter */
        memcpy(tx_buf + 17, sta->snonce, 32);       /* SNonce */
        /* Key Data: Station RSN IE */
        write_u16_be(tx_buf + 97, sizeof(sta_rsn_ie));
        memcpy(tx_buf + 99, sta_rsn_ie, sizeof(sta_rsn_ie));

        /* Compute MIC */
        compute_eapol_mic(sta->ptk.kck, tx_buf, 121, tx_buf + 81);

        *tx_len = 121;
        sta->state = WIFI_STA_STATE_WAIT_M3;
        return 1;
    }

    /* Message 3 from AP: Key ACK=1, MIC=1 */
    if ((key_info & (1u << 7)) && (key_info & (1u << 8))) {
        uint8_t check_buf[WIFI_STA_EAPOL_MAX];
        uint8_t expected_mic[16];
        uint8_t unwrapped[128];
        uint32_t i;

        if (sta->state != WIFI_STA_STATE_WAIT_M3)
            return -1;
        if (replay <= sta->replay_cnt)                  /* (a replayed or old M3) */
            return -1;

        if (len > sizeof(check_buf))
            return -1;

        /* Verify MIC: zero out MIC field and compute HMAC-SHA1 */
        memcpy(check_buf, pkt, len);
        memset(check_buf + 81, 0, 16);
        compute_eapol_mic(sta->ptk.kck, check_buf, len, expected_mic);

        if (memcmp(pkt + 81, expected_mic, 16) != 0) {
            sta->state = WIFI_STA_STATE_FAILED;
            return -1;
        }

        /* Check ANonce matches */
        if (memcmp(pkt + 17, sta->anonce, 32) != 0) {
            sta->state = WIFI_STA_STATE_FAILED;
            return -1;
        }

        /* If Key Data is encrypted, unwrap GTK */
        if ((key_info & (1u << 12)) && key_data_len >= 24u) {
            if (key_data_len > sizeof unwrapped + 8u || key_data_len % 8u) {
                sta->state = WIFI_STA_STATE_FAILED;
                return -1;
            }
            if (!wc_aes_unwrap(sta->ptk.kek, pkt + 99, key_data_len, unwrapped)) {
                sta->state = WIFI_STA_STATE_FAILED;
                return -1;
            }

            /* Search for GTK KDE (0xDD, len, 00-0F-AC, type 0x01) */
            uint32_t ulen = key_data_len - 8u;
            for (i = 0; i + 6u < ulen;) {
                uint8_t tag = unwrapped[i];
                uint8_t tlen = unwrapped[i + 1];
                if (i + 2u + tlen > ulen)
                    break;
                if (tag == 0xDDu && tlen >= 6u &&
                    unwrapped[i + 2] == 0x00 && unwrapped[i + 3] == 0x0F &&
                    unwrapped[i + 4] == 0xAC && unwrapped[i + 5] == 0x01) {
                    sta->gtk_id = unwrapped[i + 6] & 0x03u;
                    if (tlen >= 22u)
                        memcpy(sta->gtk, unwrapped + i + 8, 16);
                    break;
                }
                i += 2u + tlen;
            }
        }

        sta->replay_cnt = replay;

        /* Build Message 4 */
        memset(tx_buf, 0, 99);
        tx_buf[0] = 0x01;                           /* 802.1X-2001 */
        tx_buf[1] = 0x03;                           /* EAPOL-Key */
        write_u16_be(tx_buf + 2, 95u);              /* Body length */
        tx_buf[4] = 0x02;                           /* RSN Key Descriptor */
        /* Key Info: Version 2, Key Type Pairwise, Key MIC, Secure */
        write_u16_be(tx_buf + 5, 0x030Au);
        write_u16_be(tx_buf + 7, 16u);              /* Key Length */
        memcpy(tx_buf + 9, pkt + 9, 8);             /* Replay counter */
        write_u16_be(tx_buf + 97, 0);               /* Key Data Length 0 */

        /* Compute MIC */
        compute_eapol_mic(sta->ptk.kck, tx_buf, 99, tx_buf + 81);

        *tx_len = 99;

        /* Install hardware keys */
        wifi_sta_install_hw_keys(sta);
        sta->state = WIFI_STA_STATE_CONNECTED;
        return 1;
    }

    return 0;
}
