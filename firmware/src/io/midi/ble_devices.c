/* SPDX-License-Identifier: GPL-3.0-only */
/* HOME > SYSTEM > BLUETOOTH > DEVICES: the one list (docs/BLE-DEVICES-DESIGN.md §2.3, the user's rulings of 2026-10-09):
 *   NONE    stay visible as a peripheral, connect to nothing by ourselves (today's behaviour);
 *   LAST    the one remembered device: one the FM-1 itself connected to, as a central, and reconnects to while
 *           BLUETOOTH is ON (only when there is one). A Mac or phone connecting to the FM-1 is accepted as always and
 *           never becomes LAST;
 *   nearby  the BLE-MIDI devices a scan finds (BLE_CENTRAL), scanning only while this list is open.
 * Picking a nearby device connects to it (ble_connect.c): ready, it becomes LAST and the choice. LAST chosen: the FM-1
 * searches for it by itself while BLUETOOTH is ON and the list is closed. NONE: our central link and the search end.
 * The choice and LAST live in the device store
 * (ble/ble_store.h) at the end of the settings record (storage/project.c persist_t.ble_dev), saved with the settings.
 *
 * Main-loop code (the menu, ble_devices_poll). The stack's calls go through the BLE interrupts' hold, as
 * ble_midi_set's do. Included by midi_ble.c, and by tests/ui_pages_test.c with stand-ins for the radio. */

static struct ble_dev_store ble_store;           /* LAST and the choice, decoded at start-up (ble_devices_boot) */
static uint8_t ble_dev_kept[BLE_DEV_STORE_SIZE]; /* its octets in the settings record (project.c persist_t.ble_dev) */
static uint8_t ble_devs_open;                    /* the DEVICES list is on screen: scan (BLE_CENTRAL) */
/* the status line's tones: plain (dim), good, a notice (BLUETOOTH IS OFF, BUSY), a failure; and "no device name in it" */
enum { BDL_PLAIN, BDL_GOOD, BDL_WARN, BDL_BAD };
#define BLE_LINE_NO_NAME 0xFFu
static const char *ble_dev_msg;                  /* the list's status line for a moment (a pick's answer) */
static uint8_t ble_dev_msg_tone;
static uint8_t ble_line_nm = BLE_LINE_NO_NAME;   /* ble_dev_line: where a device's name starts in the line */
static uint32_t ble_dev_msg_ms;
#define BLE_DEV_MSG_MS 3000u
#if BLE_CENTRAL
static struct ble_scan_tab ble_found;            /* the nearby devices (ble/ble_scan.c), this main loop's */
static uint32_t ble_age_ms;
#endif

static void ble_devices_boot(void)               /* at start-up, after the settings are read (persist_boot) */
{
    ble_store_load(&ble_store, ble_dev_kept);
}

static void ble_dev_say(const char *m, uint32_t tone)
{
    ble_dev_msg = m;
    ble_dev_msg_tone = (uint8_t)tone;
    ble_dev_msg_ms = fm1_ms;
}

static const char *ble_dev_message(void)         /* the status line's message while it lasts, else 0 */
{
    return ble_dev_msg && fm1_ms - ble_dev_msg_ms < BLE_DEV_MSG_MS ? ble_dev_msg : 0;
}

static void ble_store_changed(void)              /* saved with the settings (the menu's close, or once quiet) */
{
    ble_store_save(&ble_store, ble_dev_kept);
    settings_later = 1;
}

/* the radio scans while DEVICES is open and BLUETOOTH is ON (the link layer keeps "scan wanted" across a connection
 * and an OFF / ON: ble_ll_scan) */
static void ble_scan_want(int on)
{
#if BLE_CENTRAL
    if (!ble_up)
        return;
    fm1_ble_irqs_hold(1);
    ble_ll_scan(on);
    fm1_ble_irqs_hold(0);
#else
    (void)on;
#endif
}

static void ble_devices_open(int on)             /* the menu: DEVICES opened (1) or left (0) */
{
    on = on ? 1 : 0;
    if (on == ble_devs_open)
        return;
    ble_devs_open = (uint8_t)on;
#if BLE_CENTRAL
    if (on)
        ble_scan_clear(&ble_found);              /* a fresh list each time it opens */
#endif
    ble_scan_want(on);
}

#if BLE_CENTRAL
static uint8_t ble_connect_phase(void);
#endif

/* connecting out: 1 connecting to a pick, 2 searching for LAST, 0 neither (RC_LINK, RC_HELD: ble_connect_ui) */
static int ble_seeking(void)
{
#if BLE_CENTRAL
    uint8_t ph = ble_connect_phase();
    if (!ble_up || ble_connected())
        return 0;
    return ph == 4u ? 1 : ph >= 1u && ph <= 3u ? 2 : 0;   /* (RC_PICK; RC_WAIT, RC_SCAN, RC_TRY) */
#else
    return 0;
#endif
}

static int ble_scanning(void)
{
#if BLE_CENTRAL
    return ble_up && ble_ll_scanning();
#else
    return 0;
#endif
}

#if BLE_CENTRAL
#include "ble_connect.c"                         /* connecting out: a pick, LAST and its search */
#endif

/* main loop: the scan's reports into the list (and to the search for LAST), the list aged; connecting out */
static void ble_devices_poll(void)
{
#if BLE_CENTRAL
    uint8_t pdu[2 + 37], n;
    uint16_t rssi;
    uint32_t k;
    if (!ble_up)
        return;
    for (k = 0; k < 16u && (n = ble_ll_scan_take(pdu, &rssi)) != 0; k++) {
        ble_scan_add(&ble_found, pdu, n, rssi, fm1_ms);
        rc_report(pdu, n);
    }
    if (fm1_ms - ble_age_ms >= 1000u) {
        ble_age_ms = fm1_ms;
        ble_scan_age(&ble_found, fm1_ms);
    }
    ble_connect_poll();
#endif
}

/* the list's rows: 0 NONE, then LAST (when there is one), then the nearby devices -> how many; *last: LAST's row or -1;
 * near[]: the nearby rows' indexes into ble_found.e */
static uint32_t ble_dev_rows(int *last, uint8_t *near, uint32_t *n_near)
{
    uint32_t n = 1;
    *last = -1;
    *n_near = 0;
    if (ble_store_has_last(&ble_store))
        *last = (int)n++;
#if BLE_CENTRAL
    {
        uint32_t i, k = 0, m = ble_scan_list(&ble_found, near);
        for (i = 0; i < m; i++)                   /* (LAST heard nearby: its own row, not a second one) */
            if (!rc_is_last(ble_found.e[near[i]].addr, ble_found.e[near[i]].addr_rand))
                near[k++] = near[i];
        *n_near = k;
    }
    n += *n_near;
#else
    (void)near;
#endif
    return n;
}

/* which device a row is, by what it is rather than where: the list moves under the cursor (a device ages out above it),
 * so a UI keeps the key of the row it drew highlighted and picks by that (ble_dev_pick_key) */
struct ble_dev_key {
    uint8_t kind;                                /* 0 NONE, 1 LAST, 2 a nearby device (addr, rnd), 3 nothing */
    uint8_t rnd, addr[6];
};
static void ble_dev_key_of(uint32_t row, struct ble_dev_key *k)
{
    int last;
    uint8_t near[16];
    uint32_t n_near, n = ble_dev_rows(&last, near, &n_near);
    k->kind = 3u;
    k->rnd = 0;
    memset(k->addr, 0, 6);
    if (row >= n)
        return;
    k->kind = row == 0u ? 0u : (int)row == last ? 1u : 2u;
#if BLE_CENTRAL
    if (k->kind == 2u) {
        const struct ble_found *e = &ble_found.e[near[row - 1u - (last >= 0 ? 1u : 0u)]];
        k->rnd = e->addr_rand;
        memcpy(k->addr, e->addr, 6);
    }
#endif
}
static int ble_dev_key_row(const struct ble_dev_key *k)   /* the row of that device now, else -1 */
{
    uint32_t r, n;
    struct ble_dev_key c;
    int last;
    uint8_t near[16];
    uint32_t n_near;
    if (k->kind == 3u)
        return -1;
    n = ble_dev_rows(&last, near, &n_near);
    for (r = 0; r < n; r++) {
        ble_dev_key_of(r, &c);
        if (c.kind == k->kind && (k->kind != 2u || (c.rnd == k->rnd && !memcmp(c.addr, k->addr, 6))))
            return (int)r;
    }
#if BLE_CENTRAL
    if (k->kind == 2u && last >= 0 && rc_is_last(k->addr, k->rnd))
        return last;                             /* (it became LAST: nearby leaves it out, the LAST row is it) */
#endif
    return -1;
}

/* YES on a row */
static void ble_dev_pick(uint32_t row)
{
    int last;
    uint8_t near[16];
    uint32_t n_near, n = ble_dev_rows(&last, near, &n_near);
    if (row >= n) {
        ble_dev_say("DEVICE GONE", BDL_WARN);
        return;
    }
    if (row == 0) {                              /* NONE: visible, no auto-connect (a connected Mac stays) */
#if BLE_CENTRAL
        ble_connect_none();                      /* (our central link and the search end) */
#endif
        ble_store_select(&ble_store, BLE_SEL_NONE);
        ble_store_changed();
        ble_dev_say(ble_on ? "NONE: STAY VISIBLE" : "NONE CHOSEN, BLUETOOTH OFF", ble_on ? BDL_GOOD : BDL_WARN);
        return;
    }
    if ((int)row == last) {                      /* LAST: the choice; connected to now when the list hears it, else
                                                  * searched for once the list closes (ble_connect.c) */
        if (!ble_on)
            ble_midi_set(1);                     /* (YES with BLUETOOTH OFF switches it ON: decided, §6.1 #8) */
        ble_store_select(&ble_store, BLE_SEL_LAST);
        ble_store_changed();
#if BLE_CENTRAL
        ble_connect_rearm();                     /* (a failure kept: the user acted) */
        {
            uint32_t i;
            for (i = 0; i < BLE_SCAN_N; i++)     /* (heard now: its nearby entry, left out of the rows) */
                if (ble_found.e[i].used && ble_found.e[i].midi &&
                    rc_is_last(ble_found.e[i].addr, ble_found.e[i].addr_rand)) {
                    ble_connect_pick(&ble_found.e[i]);
                    return;
                }
        }
        ble_dev_say("LAST: SEARCHING WHEN CLOSED", BDL_GOOD);
#else
        ble_dev_say("LAST: KEPT", BDL_GOOD);
#endif
        return;
    }
#if BLE_CENTRAL
    ble_connect_pick(&ble_found.e[near[row - 1u - (last >= 0 ? 1u : 0u)]]);
#endif
}

/* YES on the row the UI showed highlighted (k, its key): that device wherever it is now; gone, nothing is picked */
static void ble_dev_pick_key(const struct ble_dev_key *k)
{
    int r = ble_dev_key_row(k);
    if (r < 0)
        ble_dev_say("DEVICE GONE", BDL_WARN);
    else
        ble_dev_pick((uint32_t)r);
}

static void ble_dev_forget(void)                 /* FORGET LAST: the entry and its keys go, NONE is picked */
{
#if BLE_CENTRAL
    ble_connect_none();                          /* (a link to it ends) */
#endif
    ble_store_forget(&ble_store);
    ble_store_changed();
    ble_dev_say("FORGOTTEN", BDL_GOOD);
}

/* ---- what the list shows: the pieces both UIs draw (ui/sloop/ui_menu.c, ui/optimist/op_project.c), in capitals; each
 * UI draws them its own way (and sets its case). */
static int ble_radio_ok(void);                   /* (midi_ble.c: the stored RF trims were found) */

/* what the radio does: 0 off (or ON but not started this boot), 1 advertising, 2 a link is up (either role; ours once
 * ready), 3 no stored RF trims (the radio never starts), 4 scanning (the list open), 5 connecting (to a pick, or our link
 * setting up), 6 searching for LAST, 7 pairing, 8 the last attempt failed (kept until the user acts) */
static uint32_t ble_dev_status(void)
{
    int s = ble_seeking();
    uint32_t u = 0;
#if BLE_CENTRAL
    u = ble_on && ble_up ? ble_connect_ui() : 0u;
#endif
    return !ble_radio_ok() ? 3u : !ble_on || !ble_up ? 0u : u == 2u ? 7u : u == 1u ? 5u : ble_connected() ? 2u :
           s ? (uint32_t)(4 + s) : u == 3u ? 8u : ble_scanning() ? 4u : 1u;
}

#if BLE_CENTRAL
static int ble_dev_found_at(const uint8_t a[6], uint8_t rnd)   /* the nearby entry with this address, else -1 */
{
    uint32_t i;
    for (i = 0; i < BLE_SCAN_N; i++)
        if (ble_found.e[i].used && ble_found.e[i].midi && ble_found.e[i].addr_rand == rnd &&
            !memcmp(ble_found.e[i].addr, a, 6))
            return (int)i;
    return -1;
}
#endif

/* row r (ble_dev_rows): its text (BLE_NAME_MAX + 1 octets, a device's name as advertised; NONE's is "NONE (VISIBLE)"; nm
 * and chosen may be 0 when the caller wants only the tag and the bars), its tag (LAST; CONNECTED on LAST when our
 * link to it is up; CONNECTING / PAIRING on the nearby row being connected to; else ""), whether it is the choice (NONE
 * or LAST), and its signal bars 0..3 (0: not heard in this scan) */
static void ble_dev_row(uint32_t r, int last, const uint8_t *near, char *nm, const char **tag, int *chosen,
                        uint32_t *bars)
{
    char sink[BLE_NAME_MAX + 1u];
    int ignored;
    if (!nm)
        nm = sink;
    if (!chosen)
        chosen = &ignored;
    *tag = "";
    *chosen = 0;
    *bars = 0;
    if (r == 0) {
        str_cpy(nm, "NONE (VISIBLE)", BLE_NAME_MAX + 1u);
        *chosen = ble_store.sel == BLE_SEL_NONE;
        return;
    }
    if ((int)r == last) {
        ble_store_name(&ble_store, nm);
        *tag = "LAST";
#if BLE_CENTRAL
        {
            int f;
            if (ble_connect_last_up())
                *tag = "CONNECTED";           /* (our link to it is up) */
            f = ble_dev_found_at(ble_store.dev.addr, ble_store.dev.info & BLE_DEV_RANDOM);
            *bars = f >= 0 ? ble_scan_bars(&ble_found, (uint32_t)f) : 0u;
        }
#endif
        *chosen = ble_store.sel == BLE_SEL_LAST;
        return;
    }
#if BLE_CENTRAL
    {
        const struct ble_found *e = &ble_found.e[near[r - 1u - (last >= 0 ? 1u : 0u)]];
        if (e->name[0])
            str_cpy(nm, e->name, BLE_NAME_MAX + 1u);
        else
            ble_addr_text(e->addr, nm);
        *bars = ble_scan_bars(&ble_found, (uint32_t)(e - ble_found.e));
        if (ble_connect_on(e))
            *tag = ble_dev_status() == 7u ? "PAIRING" : "CONNECTING";
    }
#else
    (void)near;
    nm[0] = 0;
#endif
}

/* what the list shows, as one number: a UI redraws when it changes (the rows' bars, the status line, the messages) */
static uint32_t ble_dev_sig(void)
{
    uint32_t sig = ble_store.sel * 13u + (uint32_t)ble_store_has_last(&ble_store) * 17u + ble_dev_status() * 19u +
                   (uint32_t)ble_on * 29u;
    const char *m = ble_dev_message();
    for (; m && *m; m++)
        sig = sig * 31u + (uint8_t)*m;
#if BLE_CENTRAL
    {
        uint32_t i;
        char t[40] = {0};
        sig = sig * 31u + ble_found.gen + ble_connect_phase() * 37u + (uint32_t)ble_connect_last_up() * 41u;
        for (i = 0; i < BLE_SCAN_N; i++)       /* (the bars move with the hits / RSSI) */
            sig = sig * 31u + ble_scan_bars(&ble_found, i);
        sig = sig * 31u + ble_connect_status(t, sizeof t) + ble_connect_passkey();
        for (m = t; *m; m++)                   /* (the status area's line) */
            sig = sig * 31u + (uint8_t)*m;
    }
#endif
    return sig;
}

/* the status line, in capitals: a pick's answer, BLUETOOTH IS OFF, connecting out (CONNECTING / PAIRING / the code's
 * prompt / CONNECTED <name> / FAILED: <why>, a failure kept until the user acts), searching for LAST, CONNECTED: NO SCAN,
 * SCANNING n FOUND, VISIBLE. -> BDL_*: plain (dim), good, a notice, a failure. A device's name in it is as advertised:
 * ble_dev_line_name_at() is where it starts (BLE_LINE_NO_NAME: none), so a UI can set the case of the words before it and
 * leave the name alone, and cut the name rather than the words when the line is too long */
static uint32_t ble_dev_line(char *st, uint32_t room, uint32_t n_near)
{
    const char *m = ble_dev_message();
    uint32_t tone = BDL_PLAIN;
#if BLE_CENTRAL
    uint32_t k;
    char nm[BLE_NAME_MAX + 1u];
#endif
    ble_line_nm = BLE_LINE_NO_NAME;
    if (m) {
        str_cpy(st, m, room);
        return ble_dev_msg_tone;
    }
    if (!ble_on) {
        str_cpy(st, "BLUETOOTH IS OFF", room);
        return BDL_WARN;
    }
#if BLE_CENTRAL
    if ((k = ble_connect_status(st, room)) != RCS_NONE) {       /* connecting out: under way, CONNECTED <name>, FAILED:
                                                                 * <why> (kept until the user acts) */
        ble_line_nm = rc_nm_at;
        return k == RCS_GOOD || ble_connect_passkey() != BLE_NO_PASSKEY ? BDL_GOOD : k == RCS_BAD ? BDL_BAD : BDL_PLAIN;
    }
    if (ble_connect_last_up()) {                                /* our link: to LAST (a pick that became LAST) */
        str_cpy(st, "CONNECTED ", room);
        ble_line_nm = 10u;
        ble_store_name(&ble_store, nm);
        str_cpy(st + str_len(st), nm, room - str_len(st));
        return BDL_GOOD;
    }
    if (ble_seeking()) {
        str_cpy(st, ble_seeking() == 1 ? "CONNECTING ..." : "SEARCHING FOR LAST", room);
        return BDL_PLAIN;
    }
#endif
    if (ble_connected()) {
        str_cpy(st, "CONNECTED: NO SCAN", room);
        tone = BDL_GOOD;
    } else if (ble_scanning()) {
        str_cpy(st, "SCANNING ", room);
        fmt_int(st + str_len(st), (int32_t)n_near);
        str_cpy(st + str_len(st), " FOUND", room - str_len(st));
    } else
        str_cpy(st, BLE_CENTRAL ? "VISIBLE" : "VISIBLE (NO SCAN BUILT IN)", room);
    return tone;
}

static uint32_t ble_dev_line_name_at(void) { return ble_line_nm; }
