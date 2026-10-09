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
static const char *ble_dev_msg;                  /* the list's status line for a moment (a pick's answer) */
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

static void ble_dev_say(const char *m)
{
    ble_dev_msg = m;
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

/* connecting out: 1 connecting to a pick, 2 searching for LAST, 0 neither */
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

/* YES on a row */
static void ble_dev_pick(uint32_t row)
{
    int last;
    uint8_t near[16];
    uint32_t n_near, n = ble_dev_rows(&last, near, &n_near);
    if (row >= n)
        return;
    if (row == 0) {                              /* NONE: visible, no auto-connect (a connected Mac stays) */
#if BLE_CENTRAL
        ble_connect_none();                      /* (our central link and the search end) */
#endif
        ble_store_select(&ble_store, BLE_SEL_NONE);
        ble_store_changed();
        ble_dev_say("NONE: STAY VISIBLE");
        return;
    }
    if ((int)row == last) {                      /* LAST: the choice; connected to now when the list hears it, else
                                                  * searched for once the list closes (ble_connect.c) */
        if (!ble_on)
            ble_midi_set(1);                     /* (YES with BLUETOOTH OFF switches it ON: decided, §6.1 #8) */
        ble_store_select(&ble_store, BLE_SEL_LAST);
        ble_store_changed();
#if BLE_CENTRAL
        {
            uint32_t i;
            for (i = 0; i < BLE_SCAN_N; i++)     /* (heard now: its nearby entry, left out of the rows) */
                if (ble_found.e[i].used && ble_found.e[i].midi &&
                    rc_is_last(ble_found.e[i].addr, ble_found.e[i].addr_rand)) {
                    ble_connect_pick(&ble_found.e[i]);
                    return;
                }
        }
        ble_dev_say("LAST: SEARCHING WHEN CLOSED");
#else
        ble_dev_say("LAST: KEPT");
#endif
        return;
    }
#if BLE_CENTRAL
    ble_connect_pick(&ble_found.e[near[row - 1u - (last >= 0 ? 1u : 0u)]]);
#endif
}

static void ble_dev_forget(void)                 /* FORGET LAST: the entry and its keys go, NONE is picked */
{
#if BLE_CENTRAL
    ble_connect_none();                          /* (a link to it ends) */
#endif
    ble_store_forget(&ble_store);
    ble_store_changed();
    ble_dev_say("FORGOTTEN");
}
