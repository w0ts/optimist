/* SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0 */
/* The Wi-Fi station on the host (firmware/src/wifi/): the WPA2 crypto against the published vectors.
 *   SHA-1 (FIPS 180-4 "abc"), HMAC-SHA1 (RFC 2202 case 1), the PMK (IEEE 802.11 PBKDF2 example "password" / "IEEE"),
 *   AES-128 both ways (FIPS-197 C.1), AES key unwrap (RFC 3394 §4.1)
 * Exit status: the number of failed checks. */
#include <stdio.h>
#include <string.h>
#include "../firmware/src/wifi/wifi_crypto.c"
#include "../firmware/src/wifi/wifi_sta.c"

static int fails, checks;
static void hex(const char *s, uint8_t *out)
{
    while (s[0] && s[1]) {
        unsigned v;
        sscanf(s, "%2x", &v);
        *out++ = (uint8_t)v;
        s += 2;
    }
}
static void check(const char *what, const uint8_t *got, const char *want_hex, unsigned n)
{
    uint8_t w[64];
    hex(want_hex, w);
    checks++;
    if (memcmp(got, w, n)) {
        unsigned i;
        fails++;
        printf("FAIL %s: ", what);
        for (i = 0; i < n; i++)
            printf("%02x", got[i]);
        printf("\n");
    }
}

/* Helper for mock AP: RFC 3394 Key Wrap */
static void mock_aes_wrap(const uint8_t kek[16], const uint8_t *in, uint32_t n, uint8_t *out)
{
    uint8_t a[8], b[16];
    uint32_t blocks = n / 8u, i, j, k;
    for (i = 0; i < 8u; i++)
        a[i] = 0xA6u;
    for (i = 0; i < blocks * 8u; i++)
        out[8u + i] = in[i];
    for (j = 0; j < 6u; j++) {
        for (i = 1; i <= blocks; i++) {
            uint32_t t = blocks * j + i;
            for (k = 0; k < 8u; k++)
                b[k] = a[k];
            for (k = 0; k < 8u; k++)
                b[8 + k] = out[8u * i + k];
            wc_aes128_encrypt(kek, b, b);
            for (k = 0; k < 8u; k++)
                a[k] = b[k];
            a[7] ^= (uint8_t)t;
            a[6] ^= (uint8_t)(t >> 8);
            a[5] ^= (uint8_t)(t >> 16);
            a[4] ^= (uint8_t)(t >> 24);
            for (k = 0; k < 8u; k++)
                out[8u * i + k] = b[8 + k];
        }
    }
    for (i = 0; i < 8u; i++)
        out[i] = a[i];
}

static void t_crypto(void)
{
    struct wc_sha1 s;
    uint8_t d[64], k[32], in[32], wrap_out[32];
    wc_sha1_init(&s);
    wc_sha1_add(&s, (const uint8_t *)"abc", 3);
    wc_sha1_done(&s, d);
    check("sha1 abc", d, "a9993e364706816aba3e25717850c26c9cd0d89d", 20);
    memset(k, 0x0b, 20);
    wc_hmac_sha1(k, 20, (const uint8_t *)"Hi There", 8, d);
    check("hmac rfc2202-1", d, "b617318655057264e28bc0b6fb378c8ef146be00", 20);
    wc_pmk("password", (const uint8_t *)"IEEE", 4, d);
    check("pmk password/IEEE", d, "f42c6fc52df0ebef9ebb4b90b38a5f902e83fe1b135a70e23aed762e9710a12e", 32);
    hex("000102030405060708090a0b0c0d0e0f", k);
    hex("00112233445566778899aabbccddeeff", in);
    wc_aes128_encrypt(k, in, d);
    check("aes enc fips197", d, "69c4e0d86a7b0430d8cdb78070b4c55a", 16);
    wc_aes128_decrypt(k, d, d);
    check("aes dec fips197", d, "00112233445566778899aabbccddeeff", 16);

    mock_aes_wrap(k, in, 16, wrap_out);
    check("wrap rfc3394", wrap_out, "1fa68b0a8112b447aef34bd8fb5a7b829d3e862371d2cfe5", 24);

    hex("1fa68b0a8112b447aef34bd8fb5a7b829d3e862371d2cfe5", in);
    checks++;
    if (!wc_aes_unwrap(k, in, 24, d)) {
        fails++;
        printf("FAIL unwrap integrity\n");
    }
    check("unwrap rfc3394", d, "00112233445566778899aabbccddeeff", 16);
}

static void test_rand(uint8_t *out, uint8_t n)      /* (a test source: the firmware passes the radio's, ble_hw_rand) */
{
    static uint32_t x = 0x9E3779B9u;
    while (n--) {
        x ^= x << 13, x ^= x >> 17, x ^= x << 5;
        *out++ = (uint8_t)x;
    }
}

/* Hardware MAC table write capture */
static struct {
    uint32_t r, v;
} mac_writes[64];
static uint32_t mac_write_count;

static void test_mac_wr(uint32_t r, uint32_t v)
{
    if (mac_write_count < 64) {
        mac_writes[mac_write_count].r = r;
        mac_writes[mac_write_count].v = v;
        mac_write_count++;
    }
}

static uint32_t get_mac_reg(uint32_t r)
{
    uint32_t i;
    for (i = 0; i < mac_write_count; i++)
        if (mac_writes[i].r == r)
            return mac_writes[i].v;
    return 0xFFFFFFFFu;
}

static void t_ptk_derivation(void)
{
    uint8_t pmk[32], spa[6], aa[6], snonce[32], anonce[32], ptk[48];
    /* Standard test vector */
    hex("0bd15797054a5e30d09c8877d000864942909f1505363000cd61b05d678a30ef", pmk);
    hex("000b6b0d952b", spa);
    hex("00146c7e4080", aa);
    hex("69d1da42e2cd1e86093d56f685de7da839070ec7419e1fa0a5ad7755ba90bc1b", snonce);
    hex("09f72c2121e7d084ee0b938c53835cae9231f24d26279f72e90c8a66699a7b55", anonce);

    wifi_sta_derive_ptk(pmk, spa, aa, snonce, anonce, ptk);
    /* Verify KCK (first 16), KEK (next 16), TK (next 16) */
    check("ptk kck", ptk, "8fd534464a886e64aeeefa96c0b5e156", 16);
    check("ptk kek", ptk + 16, "bbd2a5755525a0a9cb3e3565b2c58366", 16);
    check("ptk tk", ptk + 32, "832fec9a315cb545b9ca9bd000c6b48d", 16);
}

static void t_handshake_flow(void)
{
    struct wifi_sta sta;
    uint8_t sta_mac[6], ap_bssid[6], anonce[32], gtk[16];
    uint8_t m1[128], m2[128], m3[256], m4[128];
    uint32_t m2_len = 0, m4_len = 0;
    uint8_t gtk_kde[32], enc_kde[40];
    uint8_t ap_ptk[48];
    uint8_t ap_mic[16];
    int res;

    wifi_sta_set_test_mac_wr(test_mac_wr);
    mac_write_count = 0;

    hex("021122334455", sta_mac);
    hex("00146c7e4080", ap_bssid);
    hex("09f72c2121e7d084ee0b938c53835cae9231f24d26279f72e90c8a66699a7b55", anonce);
    hex("11223344556677889900aabbccddeeff", gtk);

    wifi_sta_init(&sta, sta_mac, test_rand);
    wifi_sta_set_creds(&sta, "secretpass123", (const uint8_t *)"TestNetwork", 11);
    wifi_sta_set_ap(&sta, ap_bssid);

    checks++;
    if (sta.state != WIFI_STA_STATE_WAIT_M1)
        fails++;

    /* --- AP creates Message 1 --- */
    memset(m1, 0, 99);
    m1[0] = 0x01;                       /* 802.1X */
    m1[1] = 0x03;                       /* EAPOL-Key */
    write_u16_be(m1 + 2, 95);           /* Body length */
    m1[4] = 0x02;                       /* RSN */
    write_u16_be(m1 + 5, 0x008Au);      /* Version 2, Pairwise, Key ACK */
    write_u16_be(m1 + 7, 16u);          /* Key length */
    write_u16_be(m1 + 9, 0);            /* Replay counter = 1 */
    m1[16] = 1;
    memcpy(m1 + 17, anonce, 32);        /* ANonce */

    /* STA processes M1 -> generates M2 */
    res = wifi_sta_rx_eapol(&sta, m1, 99, m2, &m2_len);
    checks++;
    if (res != 1 || m2_len != 121 || sta.state != WIFI_STA_STATE_WAIT_M3) {
        fails++;
        printf("FAIL M1 handling: res=%d, len=%u, state=%d\n", res, m2_len, sta.state);
    }

    /* AP validates M2: derive PTK using STA's SNonce from M2 */
    wifi_sta_derive_ptk(sta.pmk, sta_mac, ap_bssid, m2 + 17, anonce, ap_ptk);
    compute_eapol_mic(ap_ptk, m2, 121, ap_mic);
    checks++;
    if (memcmp(m2 + 81, ap_mic, 16) != 0) {
        fails++;
        printf("FAIL M2 MIC validation by AP\n");
    }

    /* --- AP creates Message 3 --- */
    memset(m3, 0, 256);
    m3[0] = 0x01;
    m3[1] = 0x03;
    m3[4] = 0x02;
    /* Key Info: Version 2, Pairwise, Install, Key ACK, Key MIC, Secure, Encrypted Key Data */
    write_u16_be(m3 + 5, 0x13CAu);
    write_u16_be(m3 + 7, 16u);
    write_u16_be(m3 + 9, 0);            /* Replay counter = 2 */
    m3[16] = 2;
    memcpy(m3 + 17, anonce, 32);

    /* Construct GTK KDE (24 bytes): Tag 0xDD, len 22, OUI 00-0F-AC, type 0x01, KeyId 1, res, GTK */
    gtk_kde[0] = 0xDD;
    gtk_kde[1] = 22;
    gtk_kde[2] = 0x00; gtk_kde[3] = 0x0F; gtk_kde[4] = 0xAC; gtk_kde[5] = 0x01;
    gtk_kde[6] = 0x01;                  /* Key ID 1 */
    gtk_kde[7] = 0x00;                  /* Reserved */
    memcpy(gtk_kde + 8, gtk, 16);

    /* Encrypt GTK KDE with KEK using AES Key Wrap: 24 bytes in -> 32 bytes out */
    mock_aes_wrap(ap_ptk + 16, gtk_kde, 24, enc_kde);
    write_u16_be(m3 + 97, 32u);
    memcpy(m3 + 99, enc_kde, 32);
    write_u16_be(m3 + 2, 95u + 32u);    /* Body length = 127 */
    compute_eapol_mic(ap_ptk, m3, 131, m3 + 81);

    /* STA processes M3 -> generates M4, installs keys (8 bytes of padding after the frame: not part of the MIC) */
    res = wifi_sta_rx_eapol(&sta, m3, 139, m4, &m4_len);
    checks++;
    if (res != 1 || m4_len != 99 || sta.state != WIFI_STA_STATE_CONNECTED) {
        fails++;
        printf("FAIL M3 handling: res=%d, len=%u, state=%d\n", res, m4_len, sta.state);
    }

    /* Verify GTK unwrapped correctly by STA */
    check("sta gtk", sta.gtk, "11223344556677889900aabbccddeeff", 16);
    checks++;
    if (sta.gtk_id != 1)
        fails++;

    /* AP validates M4 */
    compute_eapol_mic(ap_ptk, m4, 99, ap_mic);
    checks++;
    if (memcmp(m4 + 81, ap_mic, 16) != 0) {
        fails++;
        printf("FAIL M4 MIC validation by AP\n");
    }

    /* Verify hardware MAC register writes for key installation */
    checks++;
    if (get_mac_reg(0x6804u) != 0x04u) {        /* WCID 1 Attribute = AES-CCMP */
        fails++;
        printf("FAIL HW WCID attribute reg\n");
    }
    checks++;
    if (get_mac_reg(0x7000u) != (4u << 4)) {    /* Shared key mode = AES-CCMP for key 1 */
        fails++;
        printf("FAIL HW Shared key mode reg\n");
    }
}

static void t_negative_cases(void)
{
    struct wifi_sta sta;
    uint8_t sta_mac[6], ap_bssid[6], anonce[32], gtk[16];
    uint8_t m1[128], m2[128], m3[256], m4[128];
    uint32_t m2_len = 0, m4_len = 0;
    uint8_t gtk_kde[32], enc_kde[40];
    uint8_t ap_ptk[48];

    hex("021122334455", sta_mac);
    hex("00146c7e4080", ap_bssid);
    hex("09f72c2121e7d084ee0b938c53835cae9231f24d26279f72e90c8a66699a7b55", anonce);
    hex("11223344556677889900aabbccddeeff", gtk);

    /* Test 1: Corrupted MIC in M3 */
    wifi_sta_init(&sta, sta_mac, test_rand);
    wifi_sta_set_creds(&sta, "secretpass123", (const uint8_t *)"TestNetwork", 11);
    wifi_sta_set_ap(&sta, ap_bssid);

    memset(m1, 0, 99);
    m1[0] = 0x01; m1[1] = 0x03;
    write_u16_be(m1 + 2, 95);
    m1[4] = 0x02;
    write_u16_be(m1 + 5, 0x008Au);
    write_u16_be(m1 + 7, 16u);
    m1[16] = 1;
    memcpy(m1 + 17, anonce, 32);

    wifi_sta_rx_eapol(&sta, m1, 99, m2, &m2_len);
    wifi_sta_derive_ptk(sta.pmk, sta_mac, ap_bssid, m2 + 17, anonce, ap_ptk);

    memset(m3, 0, 256);
    m3[0] = 0x01; m3[1] = 0x03; m3[4] = 0x02;
    write_u16_be(m3 + 5, 0x13CAu);
    write_u16_be(m3 + 7, 16u);
    m3[16] = 2;
    memcpy(m3 + 17, anonce, 32);

    gtk_kde[0] = 0xDD; gtk_kde[1] = 22;
    gtk_kde[2] = 0x00; gtk_kde[3] = 0x0F; gtk_kde[4] = 0xAC; gtk_kde[5] = 0x01;
    gtk_kde[6] = 0x01; gtk_kde[7] = 0x00;
    memcpy(gtk_kde + 8, gtk, 16);

    mock_aes_wrap(ap_ptk + 16, gtk_kde, 24, enc_kde);
    write_u16_be(m3 + 97, 32u);
    memcpy(m3 + 99, enc_kde, 32);
    write_u16_be(m3 + 2, 95u + 32u);
    compute_eapol_mic(ap_ptk, m3, 131, m3 + 81);

    /* Corrupt one byte of MIC */
    m3[85] ^= 0xFF;
    int res = wifi_sta_rx_eapol(&sta, m3, 131, m4, &m4_len);
    checks++;
    if (res != -1 || sta.state != WIFI_STA_STATE_FAILED) {
        fails++;
        printf("FAIL Corrupted MIC should fail\n");
    }

    /* Test 2: a replayed M3 (its counter not above M1's) is refused */
    wifi_sta_init(&sta, sta_mac, test_rand);
    wifi_sta_set_creds(&sta, "secretpass123", (const uint8_t *)"TestNetwork", 11);
    wifi_sta_set_ap(&sta, ap_bssid);
    m1[16] = 5;
    wifi_sta_rx_eapol(&sta, m1, 99, m2, &m2_len);
    wifi_sta_derive_ptk(sta.pmk, sta_mac, ap_bssid, m2 + 17, anonce, ap_ptk);
    m3[16] = 5;
    compute_eapol_mic(ap_ptk, m3, 131, m3 + 81);
    res = wifi_sta_rx_eapol(&sta, m3, 131, m4, &m4_len);
    checks++;
    if (res != -1 || sta.state != WIFI_STA_STATE_WAIT_M3) {
        fails++;
        printf("FAIL replayed M3 should be refused (res=%d state=%d)\n", res, sta.state);
    }

    /* Test 3: key data longer than the frame is refused before any unwrap */
    m3[16] = 6;
    write_u16_be(m3 + 97, 200u);
    compute_eapol_mic(ap_ptk, m3, 131, m3 + 81);
    res = wifi_sta_rx_eapol(&sta, m3, 131, m4, &m4_len);
    checks++;
    if (res != -1) {
        fails++;
        printf("FAIL key data past the frame should be refused\n");
    }
}

static void t_mgmt_framing(void)
{
    struct wifi_sta sta;
    uint8_t sta_mac[6], ap_bssid[6];
    uint8_t auth_buf[64], auth_resp[64];
    uint8_t assoc_buf[128], assoc_resp[64];
    uint32_t len;
    int res;

    hex("021122334455", sta_mac);
    hex("00146c7e4080", ap_bssid);

    wifi_sta_init(&sta, sta_mac, test_rand);
    wifi_sta_set_creds(&sta, "secretpass123", (const uint8_t *)"TestNetwork", 11);
    wifi_sta_set_ap(&sta, ap_bssid);

    /* 1. Build Authentication Frame */
    len = wifi_sta_build_auth(&sta, auth_buf);
    checks++;
    if (len != 30u || auth_buf[0] != 0xB0u || auth_buf[1] != 0x00u) {
        fails++;
        printf("FAIL build auth frame\n");
    }
    checks++;
    if (memcmp(auth_buf + 4, ap_bssid, 6) != 0 || memcmp(auth_buf + 10, sta_mac, 6) != 0) {
        fails++;
        printf("FAIL auth addresses\n");
    }
    checks++;
    if (auth_buf[24] != 0 || auth_buf[25] != 0 || auth_buf[26] != 1 || auth_buf[27] != 0) {
        fails++;
        printf("FAIL auth fixed fields (alg 0, seq 1)\n");
    }

    /* 2. Parse Authentication Response */
    memset(auth_resp, 0, 30);
    auth_resp[0] = 0xB0;
    memcpy(auth_resp + 4, sta_mac, 6);              /* DA */
    memcpy(auth_resp + 10, ap_bssid, 6);            /* SA */
    memcpy(auth_resp + 16, ap_bssid, 6);            /* BSSID */
    auth_resp[24] = 0; auth_resp[25] = 0;           /* Alg 0 */
    auth_resp[26] = 2; auth_resp[27] = 0;           /* Seq 2 */
    auth_resp[28] = 0; auth_resp[29] = 0;           /* Status 0 (Success) */

    res = wifi_sta_parse_auth_resp(&sta, auth_resp, 30);
    checks++;
    if (res != 0) {
        fails++;
        printf("FAIL parse valid auth response\n");
    }

    /* Negative: bad status code */
    auth_resp[28] = 1;
    res = wifi_sta_parse_auth_resp(&sta, auth_resp, 30);
    checks++;
    if (res != -1) {
        fails++;
        printf("FAIL parse auth response with error status should fail\n");
    }
    auth_resp[28] = 0;

    /* 3. Build Association Request */
    len = wifi_sta_build_assoc_req(&sta, assoc_buf);
    checks++;
    if (len < 64u || assoc_buf[0] != 0x00u || assoc_buf[1] != 0x00u) {
        fails++;
        printf("FAIL build assoc request frame\n");
    }
    checks++;
    if (memcmp(assoc_buf + 4, ap_bssid, 6) != 0 || memcmp(assoc_buf + 10, sta_mac, 6) != 0) {
        fails++;
        printf("FAIL assoc addresses\n");
    }
    /* Verify SSID tag (Tag 0, len 11, "TestNetwork") */
    checks++;
    if (assoc_buf[28] != 0x00u || assoc_buf[29] != 11u || memcmp(assoc_buf + 30, "TestNetwork", 11) != 0) {
        fails++;
        printf("FAIL assoc SSID tag\n");
    }

    /* 4. Parse Association Response */
    memset(assoc_resp, 0, 30);
    assoc_resp[0] = 0x10;                           /* Assoc Resp */
    memcpy(assoc_resp + 4, sta_mac, 6);             /* DA */
    memcpy(assoc_resp + 10, ap_bssid, 6);           /* SA */
    memcpy(assoc_resp + 16, ap_bssid, 6);           /* BSSID */
    assoc_resp[26] = 0; assoc_resp[27] = 0;         /* Status 0 */
    assoc_resp[28] = 42; assoc_resp[29] = 0xC0;     /* AID 42 */

    res = wifi_sta_parse_assoc_resp(&sta, assoc_resp, 30);
    checks++;
    if (res != 0 || sta.aid != 42) {
        fails++;
        printf("FAIL parse valid assoc response (aid=%u)\n", sta.aid);
    }
}

static void t_eapol_encap_and_full_join(void)
{
    struct wifi_sta sta;
    uint8_t sta_mac[6], ap_bssid[6], anonce[32], gtk[16];
    uint8_t frame_in[300], frame_out[300];
    uint8_t m1[128], m3[256];
    uint32_t out_len = 0;
    uint8_t gtk_kde[32], enc_kde[40];
    uint8_t ap_ptk[48];
    int res;

    wifi_sta_set_test_mac_wr(test_mac_wr);
    mac_write_count = 0;

    hex("021122334455", sta_mac);
    hex("00146c7e4080", ap_bssid);
    hex("09f72c2121e7d084ee0b938c53835cae9231f24d26279f72e90c8a66699a7b55", anonce);
    hex("11223344556677889900aabbccddeeff", gtk);

    wifi_sta_init(&sta, sta_mac, test_rand);
    wifi_sta_set_creds(&sta, "secretpass123", (const uint8_t *)"TestNetwork", 11);
    wifi_sta_set_ap(&sta, ap_bssid);

    /* --- Phase 1: Authentication --- */
    sta.state = WIFI_STA_STATE_AUTH;
    memset(frame_in, 0, 30);
    frame_in[0] = 0xB0;
    memcpy(frame_in + 4, sta_mac, 6);
    memcpy(frame_in + 10, ap_bssid, 6);
    memcpy(frame_in + 16, ap_bssid, 6);
    frame_in[26] = 2;                               /* Seq 2 */

    res = wifi_sta_rx_frame(&sta, frame_in, 30, frame_out, &out_len);
    checks++;
    if (res != 0 || sta.state != WIFI_STA_STATE_ASSOC) {
        fails++;
        printf("FAIL full join: auth step\n");
    }

    /* --- Phase 2: Association --- */
    memset(frame_in, 0, 30);
    frame_in[0] = 0x10;
    memcpy(frame_in + 4, sta_mac, 6);
    memcpy(frame_in + 10, ap_bssid, 6);
    memcpy(frame_in + 16, ap_bssid, 6);
    frame_in[28] = 7; frame_in[29] = 0xC0;          /* AID 7 */

    res = wifi_sta_rx_frame(&sta, frame_in, 30, frame_out, &out_len);
    checks++;
    if (res != 0 || sta.state != WIFI_STA_STATE_WAIT_M1 || sta.aid != 7) {
        fails++;
        printf("FAIL full join: assoc step\n");
    }

    /* --- Phase 3: Handshake M1 in 802.11 Data frame + LLC/SNAP --- */
    memset(m1, 0, 99);
    m1[0] = 0x01; m1[1] = 0x03;
    write_u16_be(m1 + 2, 95);
    m1[4] = 0x02;
    write_u16_be(m1 + 5, 0x008Au);
    write_u16_be(m1 + 7, 16u);
    m1[16] = 1;
    memcpy(m1 + 17, anonce, 32);

    /* Wrap M1 in 802.11 Data Frame */
    memset(frame_in, 0, 32);
    frame_in[0] = 0x08; frame_in[1] = 0x02;         /* FromDS=1 */
    memcpy(frame_in + 4, sta_mac, 6);               /* DA */
    memcpy(frame_in + 10, ap_bssid, 6);             /* SA */
    memcpy(frame_in + 16, ap_bssid, 6);             /* BSSID */
    /* LLC/SNAP */
    frame_in[24] = 0xAA; frame_in[25] = 0xAA; frame_in[26] = 0x03;
    frame_in[30] = 0x88; frame_in[31] = 0x8E;
    memcpy(frame_in + 32, m1, 99);

    res = wifi_sta_rx_frame(&sta, frame_in, 32 + 99, frame_out, &out_len);
    checks++;
    if (res != 1 || out_len != 32 + 121 || sta.state != WIFI_STA_STATE_WAIT_M3) {
        fails++;
        printf("FAIL full join: M1->M2 over-the-air frame (res=%d, len=%u, state=%d)\n", res, out_len, sta.state);
    }
    /* Verify LLC/SNAP on M2 */
    checks++;
    if (frame_out[24] != 0xAA || frame_out[30] != 0x88 || frame_out[31] != 0x8E) {
        fails++;
        printf("FAIL M2 LLC/SNAP encapsulation\n");
    }

    /* AP derives PTK from M2 payload */
    wifi_sta_derive_ptk(sta.pmk, sta_mac, ap_bssid, frame_out + 32 + 17, anonce, ap_ptk);

    /* --- Phase 4: Handshake M3 in 802.11 Data frame + LLC/SNAP --- */
    memset(m3, 0, 256);
    m3[0] = 0x01; m3[1] = 0x03; m3[4] = 0x02;
    write_u16_be(m3 + 5, 0x13CAu);
    write_u16_be(m3 + 7, 16u);
    m3[16] = 2;
    memcpy(m3 + 17, anonce, 32);

    gtk_kde[0] = 0xDD; gtk_kde[1] = 22;
    gtk_kde[2] = 0x00; gtk_kde[3] = 0x0F; gtk_kde[4] = 0xAC; gtk_kde[5] = 0x01;
    gtk_kde[6] = 0x01; gtk_kde[7] = 0x00;
    memcpy(gtk_kde + 8, gtk, 16);

    mock_aes_wrap(ap_ptk + 16, gtk_kde, 24, enc_kde);
    write_u16_be(m3 + 97, 32u);
    memcpy(m3 + 99, enc_kde, 32);
    write_u16_be(m3 + 2, 95u + 32u);
    compute_eapol_mic(ap_ptk, m3, 131, m3 + 81);

    memset(frame_in, 0, 32);
    frame_in[0] = 0x08; frame_in[1] = 0x02;
    memcpy(frame_in + 4, sta_mac, 6);
    memcpy(frame_in + 10, ap_bssid, 6);
    memcpy(frame_in + 16, ap_bssid, 6);
    frame_in[24] = 0xAA; frame_in[25] = 0xAA; frame_in[26] = 0x03;
    frame_in[30] = 0x88; frame_in[31] = 0x8E;
    memcpy(frame_in + 32, m3, 131);

    res = wifi_sta_rx_frame(&sta, frame_in, 32 + 131, frame_out, &out_len);
    checks++;
    if (res != 1 || out_len != 32 + 99 || sta.state != WIFI_STA_STATE_CONNECTED) {
        fails++;
        printf("FAIL full join: M3->M4 over-the-air frame (res=%d, len=%u, state=%d)\n", res, out_len, sta.state);
    }
}

static uint8_t mock_vm_data[10];
static int mock_vm_available = 1;
static int mock_st_read(uint32_t off, void *dst, uint32_t n)
{
    if (!mock_vm_available || off != 0x0E9000u)
        return -1;
    memcpy(dst, mock_vm_data, n);
    return 0;
}

static void t_mac_address_and_regs(void)
{
    uint8_t mac[6];
    uint8_t sta_mac[6], ap_bssid[6];
    int res;

    wifi_sta_set_test_mac_wr(test_mac_wr);
    mac_write_count = 0;
    wifi_sta_set_test_st_read(mock_st_read);

    /* Test 1: Valid VM record 102 (ID 0x66) with BT MAC 10:20:30:40:50:60 */
    mock_vm_available = 1;
    uint8_t raw_mac[6] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60};
    uint32_t crc = ble_crc16_xmodem(0, raw_mac, 6);
    mock_vm_data[0] = (uint8_t)crc;
    mock_vm_data[1] = 0x66;                         /* Record 102 */
    mock_vm_data[2] = 0x60;                         /* len 6, type 0 */
    mock_vm_data[3] = 0x00;
    memcpy(mock_vm_data + 4, raw_mac, 6);

    res = wifi_sta_get_mac(mac);
    checks++;
    if (res != 1) {
        fails++;
        printf("FAIL get mac from VM\n");
    }
    checks++;
    /* Local bit 0x02 set: 0x10 | 0x02 = 0x12 */
    if (mac[0] != 0x12 || memcmp(mac + 1, raw_mac + 1, 5) != 0) {
        fails++;
        printf("FAIL mac locally administered bit\n");
    }

    /* Test 2: Fallback when VM is absent */
    mock_vm_available = 0;
    res = wifi_sta_get_mac(mac);
    checks++;
    if (res != 0 || mac[0] != 0x02 || mac[1] != 0x11 || mac[2] != 0x22) {
        fails++;
        printf("FAIL fallback mac\n");
    }

    /* Test 3: MAC registers programming */
    hex("021122334455", sta_mac);
    hex("00146c7e4080", ap_bssid);
    wifi_sta_set_mac_regs(sta_mac, ap_bssid, 6);

    checks++;
    if (get_mac_reg(0x1008u) != 0x33221102u) {
        fails++;
        printf("FAIL STA MAC DW0 reg\n");
    }
    checks++;
    if (get_mac_reg(0x100Cu) != 0x00FF5544u) {
        fails++;
        printf("FAIL STA MAC DW1 reg\n");
    }
    checks++;
    if (get_mac_reg(0x1010u) != 0x7E6C1400u) {
        fails++;
        printf("FAIL BSSID DW0 reg\n");
    }
    checks++;
    if (get_mac_reg(0x1014u) != 0x00008040u) {
        fails++;
        printf("FAIL BSSID DW1 reg\n");
    }
}

int main(void)
{
    t_crypto();
    t_ptk_derivation();
    t_handshake_flow();
    t_negative_cases();
    t_mgmt_framing();
    t_eapol_encap_and_full_join();
    t_mac_address_and_regs();
    printf("wifi_sta_test: %d checks, %d failed\n", checks, fails);
    return fails;
}
