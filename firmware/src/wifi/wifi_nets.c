/* SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0 */
/* The saved Wi-Fi networks and the scan table (wifi_nets.h). Pure: no radio, no flash. */
#include "wifi_nets.h"

#if defined(__PI32V2__) || defined(FM1_FIRMWARE)
void *memcpy(void *d, const void *s, unsigned n);
void *memset(void *d, int c, unsigned n);
int memcmp(const void *a, const void *b, unsigned n);
#else
#include <string.h>
#endif

void wn_reset(struct wn_store *s)
{
    memset(s, 0, sizeof *s);
    s->mark = WN_MARK;
    s->ver = WN_VER;
}

static int wn_entry_ok(const struct wn_net *e)
{
    return e->used == 1u && e->ssid_len >= 1u && e->ssid_len <= WN_SSID_MAX;
}

int wn_load(struct wn_store *s, const uint8_t raw[WN_STORE_SIZE])
{
    uint32_t i, j = 0;
    struct wn_store t;
    wn_reset(s);
    memcpy(&t, raw, sizeof t);
    if (t.mark != WN_MARK || t.ver < 1u || t.ver > WN_VER)
        return 0;
    s->mode = t.mode <= WN_MODE_MAN || ((t.mode == WN_MODE_AP || t.mode == WN_MODE_AP_MAN || t.mode == WN_MODE_JANK || t.mode == WN_MODE_JANK_HOST || t.mode == WN_MODE_JANK_AUTO) && t.ver >= 2u) ? t.mode : WN_MODE_OFF;
    for (i = 0; i < WN_SLOTS; i++)
        if (wn_entry_ok(&t.n[i]))
            s->n[j++] = t.n[i];                         /* (damaged ones dropped, the rest closed up in order) */
    if (t.ver >= 2u && t.ap.set == 1u && t.ap.ssid_len <= WN_SSID_MAX) {
        s->ap = t.ap;                                   /* (a name longer than 32 or a flag that is not 1: no key kept) */
        if (s->ap.ch > 13u)
            s->ap.ch = 0;
    } else if (t.ver >= 2u && t.ap.ch >= 1u && t.ap.ch <= 13u && t.ap.ssid_len <= WN_SSID_MAX) {
        s->ap.ch = t.ap.ch;                             /* (no key, but the channel chosen is kept) */
        s->ap.ssid_len = t.ap.ssid_len;
        memcpy(s->ap.ssid, t.ap.ssid, t.ap.ssid_len);
    }
    s->ap.grp = t.ver >= 2u && t.ap.grp >= 1u && t.ap.grp <= WN_GROUPS ? t.ap.grp : 0u;   /* (the JANK group, kept whatever else the access point's record holds) */
    return 1;
}

void wn_save(const struct wn_store *s, uint8_t raw[WN_STORE_SIZE])
{
    memcpy(raw, s, sizeof *s);
}

uint32_t wn_count(const struct wn_store *s)
{
    uint32_t i, n = 0;
    for (i = 0; i < WN_SLOTS; i++)
        n += s->n[i].used == 1u;
    return n;
}

int wn_find(const struct wn_store *s, const uint8_t *ssid, uint32_t len)
{
    uint32_t i;
    for (i = 0; i < WN_SLOTS; i++)
        if (s->n[i].used == 1u && s->n[i].ssid_len == len && !memcmp(s->n[i].ssid, ssid, len))
            return (int)i;
    return -1;
}

static void wn_front(struct wn_store *s, uint32_t slot)   /* slot to the front, the ones before it one place back */
{
    struct wn_net e = s->n[slot];
    uint32_t i;
    for (i = slot; i > 0; i--)
        s->n[i] = s->n[i - 1u];
    s->n[0] = e;
}

int wn_put(struct wn_store *s, const uint8_t *ssid, uint32_t len, const uint8_t pmk[32])
{
    int at;
    if (len < 1u || len > WN_SSID_MAX)
        return -1;
    at = wn_find(s, ssid, len);
    if (at < 0)
        at = (int)WN_SLOTS - 1;                         /* a new one takes the last slot (the oldest, or a free one) ... */
    memset(&s->n[at], 0, sizeof s->n[at]);
    s->n[at].used = 1u;
    s->n[at].ssid_len = (uint8_t)len;
    memcpy(s->n[at].ssid, ssid, len);
    memcpy(s->n[at].pmk, pmk, 32);
    wn_front(s, (uint32_t)at);                          /* ... and goes to the front */
    return 0;
}

void wn_touch(struct wn_store *s, uint32_t slot)
{
    if (slot < WN_SLOTS && s->n[slot].used == 1u)
        wn_front(s, slot);
}

void wn_forget(struct wn_store *s, uint32_t slot)
{
    uint32_t i;
    if (slot >= WN_SLOTS)
        return;
    for (i = slot; i + 1u < WN_SLOTS; i++)
        s->n[i] = s->n[i + 1u];
    memset(&s->n[WN_SLOTS - 1u], 0, sizeof s->n[0]);
}

int wn_ap_put(struct wn_store *s, const uint8_t *ssid, uint32_t len, uint32_t ch, const uint8_t pmk[32])
{
    struct wn_apcfg t;                                  /* (the name may be the store's own: built aside, then put in place) */
    if (len > WN_SSID_MAX || ch > 13u || (len && !ssid))
        return -1;
    memset(&t, 0, sizeof t);
    t.set = 1u;
    t.ch = (uint8_t)ch;
    t.ssid_len = (uint8_t)len;
    if (len)
        memcpy(t.ssid, ssid, len);
    memcpy(t.pmk, pmk, 32);
    t.grp = s->ap.grp;
    s->ap = t;
    return 0;
}

void wn_ap_forget(struct wn_store *s)
{
    memset(s->ap.pmk, 0, sizeof s->ap.pmk);
    s->ap.set = 0;
}

int wn_ap_set_channel(struct wn_store *s, uint32_t ch)   /* 0: AUTO (the quietest at each start), else 1..13 */
{
    if (ch > 13u)
        return -1;
    s->ap.ch = (uint8_t)ch;
    return 0;
}

/* ---- the scan table */
void wn_scan_clear(struct wn_scan *t)
{
    memset(t, 0, sizeof *t);
}

static uint32_t wn_u16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }

/* an RSN element's body (id 48) -> WN_SEC_PSK when it is WPA2-PSK with CCMP pairwise and group and PMF not required */
static int wn_rsn_psk(const uint8_t *p, uint32_t n)
{
    static const uint8_t CCMP[4] = {0x00, 0x0F, 0xAC, 0x04}, PSK[4] = {0x00, 0x0F, 0xAC, 0x02};
    uint32_t i, cnt, ok_pw = 0, ok_akm = 0, o = 2;
    if (n < 8u || wn_u16(p) != 1u)                      /* version 1, group suite */
        return 0;
    if (memcmp(p + 2, CCMP, 4))                         /* the group cipher must be CCMP: Link is multicast */
        return 0;
    o = 6;
    cnt = wn_u16(p + o);
    o += 2;
    if (o + 4u * cnt + 2u > n)
        return 0;
    for (i = 0; i < cnt; i++)
        ok_pw |= !memcmp(p + o + 4u * i, CCMP, 4);
    o += 4u * cnt;
    cnt = wn_u16(p + o);
    o += 2;
    if (o + 4u * cnt > n)
        return 0;
    for (i = 0; i < cnt; i++)
        ok_akm |= !memcmp(p + o + 4u * i, PSK, 4);
    o += 4u * cnt;
    if (o + 2u <= n && (wn_u16(p + o) & 0x40u))         /* RSN capabilities: management frame protection required */
        return 0;
    return ok_pw && ok_akm;
}

int wn_parse_beacon(const uint8_t *f, uint32_t len, uint8_t ch, int8_t rssi, struct wn_ap *out)
{
    uint32_t i = 36, cap;
    const uint8_t *e;
    int have_ssid = 0, rsn = 0;
    if (len < 38u || (f[0] != 0x80u && f[0] != 0x50u))  /* beacon, probe response */
        return 0;
    memset(out, 0, sizeof *out);
    memcpy(out->bssid, f + 16, 6);
    out->ch = ch;
    out->rssi = rssi;
    cap = wn_u16(f + 34);
    e = f;
    while (i + 2u <= len && i + 2u + e[i + 1u] <= len) {
        uint32_t id = e[i], l = e[i + 1u];
        const uint8_t *b = e + i + 2u;
        if (id == 0u && l <= WN_SSID_MAX) {
            uint32_t k;
            for (k = 0; k < l && b[k] == 0u; k++)
                ;
            if (l && k < l) {                           /* (a hidden network sends an empty or all-zero SSID) */
                memcpy(out->ssid, b, l);
                out->ssid_len = (uint8_t)l;
                have_ssid = 1;
            }
        } else if (id == 3u && l == 1u && b[0] >= 1u && b[0] <= 14u) {
            out->ch = b[0];                             /* DS parameter: the AP's own channel beats where we heard it */
        } else if (id == 48u) {
            rsn = wn_rsn_psk(b, l);
            out->sec = rsn ? WN_SEC_PSK : WN_SEC_OTHER;
        }
        i += 2u + l;
    }
    if (!have_ssid)
        return 0;
    if (out->sec == WN_SEC_OTHER && !rsn && !(cap & 0x10u))
        out->sec = WN_SEC_OPEN;                         /* no RSN element and no privacy bit: open */
    return 1;
}

int wn_scan_add(struct wn_scan *t, const struct wn_ap *ap)
{
    uint32_t i, at;
    int added = 0;
    for (i = 0; i < t->n && memcmp(t->ap[i].bssid, ap->bssid, 6); i++)
        ;
    if (i < t->n) {                                     /* known: refresh, keep the stronger RSSI */
        int8_t r = t->ap[i].rssi > ap->rssi ? t->ap[i].rssi : ap->rssi;
        t->ap[i] = *ap;
        t->ap[i].rssi = r;
        at = i;
    } else if (t->n < WN_SCAN_MAX) {
        t->ap[t->n] = *ap;
        at = t->n++;
        added = 1;
    } else if (ap->rssi > t->ap[WN_SCAN_MAX - 1u].rssi) {   /* full: the weakest makes room (the table is sorted) */
        t->ap[WN_SCAN_MAX - 1u] = *ap;
        at = WN_SCAN_MAX - 1u;
        added = 1;
    } else {
        return 0;
    }
    while (at > 0u && t->ap[at].rssi > t->ap[at - 1u].rssi) {   /* keep it strongest first */
        struct wn_ap x = t->ap[at];
        t->ap[at] = t->ap[at - 1u];
        t->ap[at - 1u] = x;
        at--;
    }
    return added;
}

int wn_scan_best_for(const struct wn_scan *t, const uint8_t *ssid, uint32_t len)
{
    uint32_t i;
    for (i = 0; i < t->n; i++)                          /* (sorted: the first match is the strongest) */
        if (t->ap[i].ssid_len == len && !memcmp(t->ap[i].ssid, ssid, len))
            return (int)i;
    return -1;
}
