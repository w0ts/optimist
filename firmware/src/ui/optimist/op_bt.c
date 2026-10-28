/* SPDX-License-Identifier: GPL-3.0-only */
/* SYSTEM > BT MIDI and SYSTEM > BT AUDIO: two pages, one pattern (the user, 2026-10-19: "audio and midi should just follow
 * the same pattern, same ui, same logic"; 2026-10-23: both at the same time, a page each). Included by op_project.c.
 * Every function takes the side, a: BT_MIDI (BLE MIDI, builder item BLE) or BT_AUD (Bluetooth audio, BT_AUDIO); each side
 * has its own MODE, its own connect under way (want, its timer) and its own devices list. A page is there only when its
 * item is built.
 *   MODE    OFF, MAN, AUTO. MAN: only the user connects. AUTO: the same, and the last device is connected to at boot and
 *           when the mode is chosen. A link lost is not made again in either.
 *   the page's devices (the user, 2026-10-24: "we scan when not connected and on the screen. we show the remembered devices in
 *           one color and the new ones in another color. save connect/disconnect and forget (long press)"; "the knob k2 to
 *           scroll thru the list"): while the page is on screen with no link up and no connect under way (that side's, MODE
 *           MAN or AUTO) the FM-1 scans, and the page's two middle lines give way to the devices (bt_pg_*; BT AUDIO always, BT
 *           MIDI with BLE_CENTRAL): the remembered ones first (LAST, KNOWN), then the new ones heard, four rows, the remembered
 *           in C_OK (green), the new in C_WARN (amber), the text only, the cursor a bar at the left edge. DEVICE's knob (K2)
 *           moves the cursor, stopping at the ends. The scan stops with a link or a connect (either side's: one radio, a scan
 *           waits while the other side connects, rf_share.h) and with the page left; BT MIDI's advertising waits while it
 *           scans. Without BLE_CENTRAL BT MIDI is as it was (no scan on the page; SAVE opens the overlay list, dev_open).
 *   SAVE    whatever line of the page the cursor is on: connected or connecting, it lets go; else the list's row under the
 *           cursor, connected to (it becomes the last device; nothing heard: NONE FOUND).
 *   SAVE held  the cursor row's remembered device forgotten (a new one: NOT REMEMBERED; AUDIO remembers 4, bt/bta_mem.c, and
 *           AUTO pages them in turn); linked or connecting, that side's last device. HOME goes home, as anywhere.
 * MIDI advertises whenever no link is up (ble/ble_ll.c). Each action says what it did (ui_say). Both modes and the AUDIO
 * last device are kept in the settings record (storage/persist_rec.c bt_rec; bt_rec_out below). The radio is shared:
 * ble/rf_share.h, docs/BT-CLASSIC.md §9.1. */
#define BT_TRY_MS 30000u                                /* a connect not up in this long: NO ANSWER, given up */
#define BT_T_SH 4u                                      /* the connect's start kept in 16 ms steps (16 bits: 17 min round) */
#define BT_REC_MARK 0x31525442u                         /* "BTR1" */
#define BT_REC_MODES 29u                                /* the record's byte with both modes (bit 7 set: written by this) */
#define BT_REC_OK 0x80u
#if FELUCCA_BT_AUDIO
#define BT_AUDIO_BUILT 1
#else
#define BT_AUDIO_BUILT 0                                /* (an AUDIO mode in the record: OFF in this build) */
#endif
enum { BT_OFF, BT_MAN, BT_AUTO };
static uint8_t sys_dirty;                               /* (op_project.c: a setting changed, saved when SYSTEM is left) */
static struct {
    uint8_t mode;                                       /* bits 0-1 BT MIDI's MODE, bits 2-3 BT AUDIO's (BT_OFF ...) */
    uint8_t want;                                       /* bit a: a connect under way on side a (the user's, or AUTO's) */
    uint8_t boot;                                       /* the saved modes applied */
    uint8_t rec;                                        /* the modes as the settings record gave them: BT_REC_OK | mode's
                                                         * bits; 0: none */
    uint8_t scan;                                       /* bit a: side a's page scan runs (bt_pg_sync) */
    uint8_t pcur, pfirst;                               /* the page's devices: the cursor row, the first row shown */
    uint16_t t0[2];                                     /* fm1_ms >> BT_T_SH: side a's connect started */
} bt;

static uint32_t bt_mode(uint32_t a) { return (bt.mode >> (2u * a)) & 3u; }
static void bt_mode_put(uint32_t a, uint32_t m) { bt.mode = (uint8_t)((bt.mode & ~(3u << (2u * a))) | (m & 3u) << (2u * a)); }
static int bt_wants(uint32_t a) { return (bt.want >> a) & 1u; }
static void bt_want(uint32_t a, int on)
{
    bt.want = (uint8_t)(on ? bt.want | 1u << a : bt.want & ~(1u << a));
    if (on)
        bt.t0[a & 1u] = (uint16_t)(fm1_ms >> BT_T_SH);
}
static uint32_t bt_waited(uint32_t a) { return (uint32_t)(uint16_t)((uint16_t)(fm1_ms >> BT_T_SH) - bt.t0[a & 1u]) << BT_T_SH; }

static int bt_has_last(uint32_t a)
{
#if FELUCCA_BT_AUDIO
    if (a == BT_AUD)
        return bta.nmem != 0u;
#endif
#if BLE_CENTRAL
    if (a == BT_MIDI)
        return ble_store_has_last(&ble_store);
#endif
    return 0;
}
static void bt_last_name(uint32_t a, char nm[BLE_NAME_MAX + 1u])
{
    nm[0] = 0;
#if FELUCCA_BT_AUDIO
    if (a == BT_AUD) {
        if (bta.nmem)
            str_cpy(nm, bta.mem[0].name, BLE_NAME_MAX + 1u);
        return;
    }
#endif
#if BLE_CENTRAL
    if (ble_store_has_last(&ble_store))
        ble_store_name(&ble_store, nm);
#endif
}
static int bt_linked(uint32_t a)                        /* side a's link is up */
{
#if FELUCCA_BT_AUDIO
    if (a == BT_AUD) {
        uint32_t s = bta_state();
        return s >= BTA_S_LINKED && s <= BTA_S_STREAMING;
    }
#endif
    return a == BT_MIDI && bt_mode(BT_MIDI) && ble_on && ble_connected();
}
static int bt_connecting(uint32_t a)
{
#if FELUCCA_BT_AUDIO
    if (a == BT_AUD)
        return bt_wants(a) || bta_state() == BTA_S_PAGING;
#endif
    return bt_wants(a) || (a == BT_MIDI && bt_mode(BT_MIDI) && ble_seeking() != 0);
}
#if BLE_CENTRAL
static void bt_midi_choice(uint8_t sel)                 /* LAST: the stack searches for it (ble_connect.c); NONE: it does not */
{
    if (ble_store.sel != sel)
        ble_store_select(&ble_store, sel);
}
#endif
static void bt_stop(uint32_t a)                         /* side a's link, or its connect, ends; the last device kept */
{
    bt_want(a, 0);
#if FELUCCA_BT_AUDIO
    if (a == BT_AUD) {
        bta_hangup();
        bta_settle(0);                                  /* (AUTO's round left: the last device first again) */
        return;
    }
#endif
#if BLE_CENTRAL
    bt_midi_choice(BLE_SEL_NONE);
    if (ble_seeking() || ble_connect_last_up() || ble_ll_central()) {
        ble_connect_none();
        return;
    }
#endif
    if (ble_on && ble_connected()) {                    /* (a computer or phone linked to us: the radio again, it advertises) */
        ble_midi_set(0);
        ble_midi_set(1);
    }
}
static void bt_go(uint32_t a)                           /* side a: connect to its last device */
{
    bt_want(a, 1);
#if FELUCCA_BT_AUDIO
    if (a == BT_AUD) {
        (void)bta_page(bta.mem[0].addr);
        return;
    }
#endif
#if BLE_CENTRAL
    ble_connect_rearm();                                /* (a failure kept: the user acted) */
    bt_midi_choice(BLE_SEL_LAST);
#endif
}
static void bt_scan(uint32_t a, int on)                 /* side a's devices list: its scan */
{
#if FELUCCA_BT_AUDIO
    if (a == BT_AUD) {
        bta_scan(on);
        return;
    }
#endif
    ble_devices_open(on);
}

/* side a's devices list's rows: the devices heard (MIDI: LAST first, from ble_devices.c, its NONE row left out; AUDIO: the
 * inquiry's answers) */
static uint32_t bt_rows(uint32_t a)
{
#if FELUCCA_BT_AUDIO
    if (a == BT_AUD)
        return bta_rows();                              /* (the remembered devices first, then the scan's others) */
#endif
    {
        int last;
        uint32_t nn, n;
        uint8_t near[BLE_SCAN_N];
        n = ble_dev_rows(&last, near, &nn);
        return n ? n - 1u : 0u;
    }
}
/* row i: its name, its tag (LAST, CONNECTED, CONNECTING ...), its signal (-1: none known) */
static void bt_row(uint32_t a, uint32_t i, char nm[BLE_NAME_MAX + 1u], const char **tag, int32_t *bars)
{
    *tag = "";
    *bars = -1;
#if FELUCCA_BT_AUDIO
    if (a == BT_AUD) {
        int r = bta_row(i);
        if (r < 0) {                                    /* (found by the scan, not remembered) */
            str_cpy(nm, bta_dev_name((uint32_t)(-1 - r)), BLE_NAME_MAX + 1u);
            return;
        }
        str_cpy(nm, bta.mem[r].name, BLE_NAME_MAX + 1u);
        if (r == bta.page_i && (bt_linked(a) || bta_state() == BTA_S_PAGING))
            *tag = bt_linked(a) ? "CONNECTED" : "CONNECTING";   /* (the one paged: AUTO's round included) */
        else
            *tag = r == 0 ? "LAST" : "KNOWN";
        return;
    }
#endif
    {
        int last, chosen;
        uint32_t nn, b;
        uint8_t near[BLE_SCAN_N];
        (void)ble_dev_rows(&last, near, &nn);
        ble_dev_row(i + 1u, last, near, nm, tag, &chosen, &b);
        *bars = (int32_t)b;
    }
}

/* ---- the page's own devices (the user, 2026-10-24: "we scan when not connected and on the screen. we show the remembered
 * devices in one color and the new ones in another color. save connect/disconnect and forget (long press)"; "the knob k2 to
 * scroll thru the list"): while side a's page is on screen with no link up and no connect under way, the FM-1 scans and the
 * page's middle is the devices list (the remembered ones first, from bta_mem.c / ble_devices.c, then the new ones heard);
 * DEVICE's knob (K2) moves the cursor, SAVE connects to the row, SAVE held forgets a remembered one. MIDI's list is the
 * central's (BLE_CENTRAL); without it the page is as it was. The overlay list (dev_open) stays for MIDI without a central. */
static uint32_t bt_page(void);                          /* op_global.c: 0 not a Bluetooth page, else 1 + its side */
static int bt_pg_side(uint32_t a)                       /* side a has the page list in this build */
{
#if FELUCCA_BT_AUDIO
    if (a == BT_AUD)
        return 1;
#endif
#if BLE_CENTRAL
    if (a == BT_MIDI)
        return 1;
#endif
    (void)a;
    return 0;
}
static int bt_pg_on(uint32_t a)                         /* the page list is what side a's page shows now */
{
    return bt_pg_side(a) && bt_mode(a) && !BDV_OPEN && !bt_linked(a) && !bt_connecting(a);
}
static uint32_t bt_pg_shown(void)                       /* the Bluetooth page on screen shows its list: 1 + its side, else 0 */
{
    uint32_t p = bt_page();
    return p && bt_pg_on(p - 1u) ? p : 0u;
}
static void bt_pg_sync(void)                            /* the scans follow the pages: on screen, not linked, nothing connecting */
{
    uint32_t a, pg = bt_page();
    for (a = 0; a < 2u; a++) {
        int want = pg == 1u + a && bt_pg_on(a) && !bt_connecting(a ^ 1u), has = (bt.scan >> a) & 1;
        if (want == has) {
#if FELUCCA_BT_AUDIO
            if (want && a == BT_AUD && bta_state() != BTA_S_SCANNING)
                bta_scan(2);                            /* (the inquiry ended: again, the list kept) */
#endif
            continue;
        }
        bt.scan = (uint8_t)(want ? bt.scan | 1u << a : bt.scan & ~(1u << a));
        if (want)
            bt.pcur = bt.pfirst = 0, bdv.key_ok = 0;
        bt_scan(a, want);
    }
}
static uint32_t bt_pg_cur(uint32_t a)                   /* the cursor row, kept inside the list (it shrinks and grows) */
{
    uint32_t n = bt_rows(a);
    if (bt.pcur >= n)
        bt.pcur = (uint8_t)(n ? n - 1u : 0u);
    return bt.pcur;
}
static int bt_pg_known(uint32_t a, uint32_t i)          /* row i is a remembered device (else a new one, heard) */
{
#if FELUCCA_BT_AUDIO
    if (a == BT_AUD)
        return bta_row(i) >= 0;
#endif
#if BLE_CENTRAL
    {
        struct ble_dev_key k;
        ble_dev_key_of(i + 1u, &k);
        return k.kind == 1u || k.kind == 4u;
    }
#else
    (void)a, (void)i;
    return 0;
#endif
}
static int bt_pg_turn(uint32_t a, int32_t s)            /* side a's DEVICE knob: the cursor through the list, stopping at the ends -> 1 */
{
    uint32_t n, c;
    if (!bt_pg_on(a))
        return 0;
    if (s != OP_RESET) {
        n = bt_rows(a);
        c = bt_pg_cur(a);
        c = s < 0 ? (c > (uint32_t)-s ? c - (uint32_t)-s : 0u) : c + (uint32_t)s;
        bt.pcur = (uint8_t)(c >= n ? (n ? n - 1u : 0u) : c);
        bdv.key_ok = 0;                                 /* (the row now: the key of the one drawn highlighted is retaken) */
    }
    return 1;
}
/* the list as drawn between y0 and y1, a row each (colour is the only mark: the remembered devices C_OK, green, the new ones
 * C_WARN, amber; both fixed status colours, whatever the palette; no text on a fill, the cursor row is marked by a bar at the
 * left edge in col) */
#define BT_PG_ROWS 4u
static void bt_pg_paint(uint32_t a, int32_t y0, int32_t y1, uint16_t col)
{
    uint32_t n = bt_rows(a), cur = bt_pg_cur(a), i;
    int32_t rh = (y1 - y0) / (int32_t)BT_PG_ROWS;
    if (cur < bt.pfirst)
        bt.pfirst = (uint8_t)cur;
    else if (cur >= bt.pfirst + BT_PG_ROWS)
        bt.pfirst = (uint8_t)(cur - (BT_PG_ROWS - 1u));
    if (bt.pfirst + BT_PG_ROWS > n)
        bt.pfirst = (uint8_t)(n > BT_PG_ROWS ? n - BT_PG_ROWS : 0u);
    for (i = bt.pfirst; i < n && i < bt.pfirst + BT_PG_ROWS; i++) {
        char nm[BLE_NAME_MAX + 1u], b[BLE_NAME_MAX + 4u];
        const char *tag;
        int32_t bars, y = y0 + (int32_t)(i - bt.pfirst) * rh, right = 232, room;
        uint16_t ink = bt_pg_known(a, i) ? C_OK : C_WARN;
        bt_row(a, i, nm, &tag, &bars);
        if (i == cur)
            cv_rect(0, y + 2, 3, rh - 4, col);
        if (bars >= 0) {
            dev_bars(right - 16, y + (rh - 16) / 2, (uint32_t)bars, 0);
            right -= 22;
        }
        if (tag[0]) {
            op_case(b, tag, sizeof b);
            right -= text_ws(b);
            cv_texts(right, y + (rh - (int32_t)FONT_S.h) / 2, b, ink);
            right -= 8;
        }
        room = right - 8;
        cv_text(8, y + (rh - (int32_t)font_big()->h) / 2, font_big(), cut(b, nm, room > 16 ? (uint32_t)room / 16u : 1u), ink);
    }
    if (n > BT_PG_ROWS) {                               /* where the window is: a thin bar at the right edge */
        int32_t h = rh * (int32_t)BT_PG_ROWS, th = h * (int32_t)BT_PG_ROWS / (int32_t)n;
        cv_rect(238, y0, 2, h, C_LINE);
        cv_rect(238, y0 + (h - th) * (int32_t)bt.pfirst / (int32_t)(n - BT_PG_ROWS), 2, th, C_GRAY);
    }
}
/* what the list shows, as one number (op_gldraw.c gl_draw: the band is drawn again when it changes), and the key of the row
 * drawn highlighted (MIDI: SAVE picks that device, wherever the list moved it) */
static uint32_t bt_pg_sig(uint32_t a)
{
    uint32_t n = bt_rows(a), cur = bt_pg_cur(a), i, sig = hu(hu(0x7B5Du + n * 131u, cur), bt.pfirst);
    char nm[BLE_NAME_MAX + 1u];
    const char *tag;
    int32_t bars;
    for (i = 0; i < n; i++) {
        bt_row(a, i, nm, &tag, &bars);
        sig = hu(hs(hs(sig, nm), tag), (uint32_t)(bars + 2) * 2u + (uint32_t)bt_pg_known(a, i));
    }
#if BLE_CENTRAL
    if (a == BT_MIDI && cur < n) {
        ble_dev_key_of(cur + 1u, &bdv.key);
        bdv.key_row = (uint8_t)cur;
        bdv.key_ok = 1;
    }
#endif
    return sig;
}
static int bt_midi_row(uint32_t i)                      /* MIDI's list row i: the ble_devices.c row of the device it is (by what it
                                                         * is, not where: a shrunk list clamps the row) -> -1: gone */
{
    struct ble_dev_key k;
    if (bdv.key_ok && (i == bdv.key_row || (i < bdv.key_row && i + 1u == bt_rows(BT_MIDI))))
        k = bdv.key;                                    /* (the row the user saw: that device, not that place) */
    else
        ble_dev_key_of(i + 1u, &k);
    return ble_dev_key_row(&k);
}
static void bt_pick(uint32_t a, uint32_t i)             /* SAVE on side a's row i: connect to it */
{
    char nm[BLE_NAME_MAX + 1u];
    const char *tag;
    int32_t bars;
    if (i >= bt_rows(a))
        return;
#if FELUCCA_BT_AUDIO
    if (a == BT_AUD) {
        bt_row(a, i, nm, &tag, &bars);
        bta_pick_row(i);                                /* (it becomes the last device, its key kept) */
    } else
#endif
    {
        int r = bt_midi_row(i);
        if (r < 1) {
            ui_message("DEVICE GONE");
            return;
        }
        bt_row(a, (uint32_t)r - 1u, nm, &tag, &bars);
        ble_dev_pick((uint32_t)r);                      /* (LAST: the search for it; a nearby device: connected to, it
                                                         * becomes LAST once up) */
    }
    bt_want(a, 1);
    sys_dirty = 1;
    ui_say("CONNECTING ", nm);
}

/* SAVE on side a's page, any line */
static void bt_save(uint32_t a)
{
    char nm[BLE_NAME_MAX + 1u];
    bt_last_name(a, nm);
    if (!bt_mode(a)) {
        ui_message(a == BT_AUD ? "BT AUDIO OFF" : "BT MIDI OFF");
        return;
    }
    if (bt_linked(a) || bt_connecting(a)) {
        bt_stop(a);
        ui_say("DISCONNECTED ", nm);
        return;
    }
    if (bt_pg_on(a)) {                                  /* the page's list: the row under the cursor, remembered or new */
        if (!bt_rows(a)) {
            ui_message("NONE FOUND");
            return;
        }
        bt_pick(a, bt_pg_cur(a));
        bt_pg_sync();                                   /* (connecting: the scan stops now) */
        return;
    }
    if (bt_has_last(a)) {
        bt_go(a);
        ui_say("CONNECTING ", nm);
        return;
    }
    dev_open(a);                                        /* (MIDI without the central: nothing remembered, the devices list) */
}
static int bt_pg_forget(uint32_t a)                     /* SAVE held on the page's list: the cursor row's remembered device -> 1 */
{
    char nm[BLE_NAME_MAX + 1u];
    const char *tag;
    int32_t bars;
    uint32_t i = bt_pg_cur(a);
    if (!bt_pg_on(a))
        return 0;
    if (i >= bt_rows(a)) {
        ui_message("NONE FOUND");
        return 1;
    }
    if (!bt_pg_known(a, i)) {                           /* (a new device: nothing to forget) */
        ui_message("NOT REMEMBERED");
        return 1;
    }
    bt_row(a, i, nm, &tag, &bars);
#if FELUCCA_BT_AUDIO
    if (a == BT_AUD)
        bta_forget((uint32_t)bta_row(i));               /* (its key goes too; the next one becomes the last) */
    else
#endif
    {
#if BLE_CENTRAL
        struct ble_dev_key k;
        int r = bt_midi_row(i);
        if (r < 1) {
            ui_message("DEVICE GONE");
            return 1;
        }
        ble_dev_key_of((uint32_t)r, &k);
        if (k.kind == 1u)
            ble_dev_forget();                           /* (LAST: its keys go, the next remembered one becomes LAST) */
        else
            (void)ble_dev_forget_row((uint32_t)r);
#endif
    }
    bdv.key_ok = 0;
    sys_dirty = 1;
    ui_say("FORGOTTEN ", nm);
    return 1;
}
static void bt_forget(uint32_t a)                       /* SAVE held */
{
    char nm[BLE_NAME_MAX + 1u];
    if (bt_pg_forget(a))
        return;
    if (!bt_has_last(a)) {
        ui_message("NO LAST DEVICE");
        return;
    }
#if FELUCCA_BT_AUDIO
    if (a == BT_AUD) {                                  /* the list open: the cursor's remembered device; else the last */
        int k = bta_row(dev_listing() ? ui.row[SCR_SYSTEM] : 0u);
        if (k < 0) {
            ui_message("NOT REMEMBERED");
            return;
        }
        str_cpy(nm, bta.mem[k].name, sizeof nm);
        if (k == bta.page_i)
            bt_stop(a);                                 /* (the one linked or paged: its link or connect ends) */
        bta_forget((uint32_t)k);                        /* (its key goes too; the next one becomes the last) */
        sys_dirty = 1;
        ui_say("FORGOTTEN ", nm);
        return;
    }
#endif
#if BLE_CENTRAL
    if (dev_listing()) {                                /* the list open on a remembered device other than LAST: that one */
        const char *tag;
        int32_t bars;
        bt_row(a, ui.row[SCR_SYSTEM], nm, &tag, &bars);
        if (ble_dev_forget_row(ui.row[SCR_SYSTEM] + 1u)) {   /* (MIDI's rows: the list's after its NONE) */
            sys_dirty = 1;
            ui_say("FORGOTTEN ", nm);
            return;
        }
    }
#endif
    bt_last_name(a, nm);
    bt_stop(a);
#if BLE_CENTRAL
    ble_dev_forget();                                   /* (its keys go too; the next remembered one becomes LAST) */
#endif
    sys_dirty = 1;
    ui_say("FORGOTTEN ", nm);
}

/* side a's mode chosen (MODE's knob, or the record at boot); the other side is left as it is */
static void bt_enter(uint32_t a, uint32_t m)
{
    bt_want(a, 0);
    bt_mode_put(a, m);
    if (a == BT_AUD) {
#if FELUCCA_BT_AUDIO
        if (m)
            bta_start(m == BT_MAN ? BTA_MAN : BTA_AUTO);
        else
            bta_stop();
#endif
    } else if (m) {
        ble_midi_set(1);
#if BLE_CENTRAL
        bt_midi_choice(BLE_SEL_NONE);
#endif
    } else
        ble_midi_set(0);
    if (bt_mode(a) == BT_AUTO && bt_has_last(a))
        bt_go(a);
}

/* the saved modes at boot (bt_poll, once the radio and the settings are up): the record's, else as the MODE line read
 * before there was one (the settings word's BLUETOOTH bit: MIDI) */
static void bt_boot(void)
{
    uint32_t mm, am = 0;
#if BLE_CENTRAL
    mm = !ble_on ? BT_OFF : bt_has_last(BT_MIDI) && ble_store.sel == BLE_SEL_LAST ? BT_AUTO : BT_MAN;
#else
    mm = ble_on ? BT_MAN : BT_OFF;
#endif
    if (bt.rec & BT_REC_OK) {
        uint32_t r = bt.rec & 3u;
        if (r <= BT_AUTO && (r == BT_OFF || ble_on))    /* (MIDI on in the record, the radio not: as the word says) */
            mm = r;
        am = BT_AUDIO_BUILT && ((bt.rec >> 2) & 3u) <= BT_AUTO ? (bt.rec >> 2) & 3u : BT_OFF;
    }
    bt_enter(BT_MIDI, mm);
    bt_enter(BT_AUD, am);
}

/* main loop, side a: a connect given up after BT_TRY_MS; MIDI's search only while a connect is wanted (MAN and AUTO
 * alike: no link made again by itself) */
static void bt_poll_side(uint32_t a)
{
    char nm[BLE_NAME_MAX + 1u];
    if (bt_wants(a) && bt_linked(a)) {
        bt_want(a, 0);
#if FELUCCA_BT_AUDIO
        if (a == BT_AUD)
            bta_settle(1);                              /* (the device that answered first in the list now) */
#endif
    }
    if (!bt_wants(a)) {
#if BLE_CENTRAL
        if (a == BT_MIDI && ble_seeking() != 1)
            bt_midi_choice(BLE_SEL_NONE);               /* (no search for LAST once up or let go: a link lost stays lost) */
#endif
        return;
    }
    if (bt_waited(a) >= BT_TRY_MS) {
#if FELUCCA_BT_AUDIO
        if (a == BT_AUD && bt_mode(a) == BT_AUTO && bta.nmem > 1u) {   /* (AUTO went round the remembered devices) */
            bt_stop(a);
            ui_message("NO DEVICE ANSWERED");
            return;
        }
#endif
        bt_last_name(a, nm);
        bt_stop(a);
        ui_say("NO ANSWER ", nm);
        return;
    }
#if FELUCCA_BT_AUDIO
    if (a == BT_AUD) {
        if (bta_state() == BTA_S_REFUSED) {             /* (it answered and said no: not asked again) */
            bt_last_name(a, nm);
            bt_stop(a);
            ui_say(bta_gave_up() ? "TIMED OUT " : "REFUSED ", nm);   /* (the 20 s limit is ours: bt_acl.c) */
        } else if (bta_state() != BTA_S_PAGING && bta.nmem) {
            if (bt_mode(a) == BT_AUTO && bta.tried)
                (void)bta_next();                       /* (AUTO: the next remembered device, the last one first) */
            (void)bta_page(bta.mem[bta.page_i].addr);   /* (a page is ~5 s: again until BT_TRY_MS) */
        }
        return;
    }
#endif
#if BLE_CENTRAL
    if (ble_connect_status(nm, sizeof nm) == RCS_BAD)
        bt_want(a, 0);                                  /* (the stack gave up and says why: FAILED: <why>) */
#endif
}
/* main loop (op_input.c ui_input): the saved modes at boot, then each side */
static void bt_poll(void)
{
#if FELUCCA_BT_AUDIO
    if (bta.key_new) {                                  /* (our address made, a device bonded or forgotten: kept) */
        bta.key_new = 0;
        sys_dirty = 1;
        settings_later = 1;
    }
    if (bt_mode(BT_AUD) && bta_rejoin())                /* (paired, let go, connecting again with the key: bt_acl.c) */
        bt_want(BT_AUD, 1);
    if (bt_mode(BT_AUD)) {                              /* (the link ended by itself: where it had got to) */
        const char *w = bta_lost();
        if (w) {
            ui_message(w);
            bt_want(BT_AUD, 0);
        }
    }
#if FELUCCA_BLE
    ble_sig_audio(bta_state() == BTA_S_STREAMING);      /* (BLE's interval slower meanwhile: the audio's air, ble_host.c) */
#endif
#endif
    if (!bt.boot) {
        if (fm1_ms < 1500u)
            return;                                     /* (the radio and the settings up first) */
        bt.boot = 1;
        bt_boot();
    }
#if FELUCCA_BLE
    bt_poll_side(BT_MIDI);
#endif
#if FELUCCA_BT_AUDIO
    bt_poll_side(BT_AUD);
#endif
    bt_pg_sync();                                       /* (the page's scan: on while its page is shown with no link) */
}

#if FELUCCA_BT_AUDIO
#include "op_volgest.h"
/* SHIFT2 + the volume knob (system/main.c master_pot, a reading at a time): 15 % of its travel switches the output
 * between Bluetooth audio and the FM-1's own, while audio streams (op_volgest.h); 1: the volume holds. BT AUDIO's side
 * only: BT MIDI's link is not touched. SHIFT2, not SHIFT: SHIFT + the knob is the USB input's VOLUME (op_mixer.c
 * ut_vol_hold; the user, 2026-10-28) */
static struct vgest bt_vg;
static int bt_vol_hold(int32_t knob)
{
    int t, held = vgest_step(&bt_vg, btn_held(B_OCTUP) && !btn_held(B_OCTDN), knob, &t);
    if (t) {
        char nm[BLE_NAME_MAX + 1u];
        int on = bt_mode(BT_AUD) != BT_OFF;
        if (on && bta_state() != BTA_S_STREAMING && bt_has_last(BT_AUD)) {   /* (not streaming: the last device, connected
                                                         * to; its stream plays there once up. The user, 2026-10-20: "in
                                                         * manual mode, shift + volume should connect to the last device") */
            if (!bt_connecting(BT_AUD) && !bt_linked(BT_AUD))
                bt_go(BT_AUD);
            bt_last_name(BT_AUD, nm);
            ui_say("CONNECTING ", nm);
        } else if (!on || bta_state() != BTA_S_STREAMING) {
            ui_message("NO BT AUDIO DEVICE");
        } else {
            int to_bt = !bta_out_bt();
            bta_output(to_bt);
            ui_message(to_bt ? "OUTPUT: BLUETOOTH" : "OUTPUT: INTERNAL");
        }
    }
    return held;
}
#endif

/* the settings record's bytes (storage/persist_rec.c): the mark, the old single MODE (r[4]: OFF, MIDI MAN, MIDI AUTO,
 * AUDIO MAN, AUDIO AUTO, as a firmware from before the two pages reads it: AUDIO's when AUDIO is on), the AUDIO last
 * device, and both modes (r[BT_REC_MODES], BT_REC_OK set) */
#define BT_REC_SIZE 32u
static void bt_rec_out(uint8_t r[BT_REC_SIZE])
{
    uint32_t mk = BT_REC_MARK, am = bt_mode(BT_AUD);
    memset(r, 0, BT_REC_SIZE);
    memcpy(r, &mk, 4);
    r[4] = (uint8_t)(am ? 2u + am : bt_mode(BT_MIDI));
#if FELUCCA_BT_AUDIO
    r[5] = (uint8_t)(bta.nmem != 0u);
    memcpy(r + 6, bta.mem[0].addr, 6);
    str_cpy((char *)r + 12, bta.mem[0].name, 17);
#endif
    r[BT_REC_MODES] = (uint8_t)(BT_REC_OK | bt.mode);
}
static void bt_rec_in(const uint8_t r[BT_REC_SIZE])
{
    uint32_t mk, old = r[4];
    memcpy(&mk, r, 4);
    if (mk != BT_REC_MARK)
        return;
    if (r[BT_REC_MODES] & BT_REC_OK)
        bt.rec = (uint8_t)(r[BT_REC_MODES] & (BT_REC_OK | 0x0Fu));
    else if (old <= 2u || (old <= 4u && BT_AUDIO_BUILT))   /* (a record from before the two pages: one mode, the other
                                                         * OFF; an AUDIO mode in a build without it: as no record) */
        bt.rec = (uint8_t)(BT_REC_OK | (old >= 3u ? (old - 2u) << 2 : old));
#if FELUCCA_BT_AUDIO
    bta.nmem = (uint8_t)(r[5] == 1u);
    memcpy(bta.mem[0].addr, r + 6, 6);
    str_cpy(bta.mem[0].name, (const char *)r + 12, sizeof bta.mem[0].name);
#endif
}

#if FELUCCA_BT_AUDIO
/* the key record's bytes (storage/persist_rec.c bt_key): the mark, our address, the bond */
#define BT_KEY_MARK 0x314B5442u                         /* "BTK1" */
static void bt_key_out(uint8_t r[32])
{
    uint32_t mk = BT_KEY_MARK;
    memset(r, 0, 32);
    memcpy(r, &mk, 4);
    r[4] = bta.own_set, r[5] = bta.mem[0].bonded;
    memcpy(r + 6, bta.own, 6);
    memcpy(r + 12, bta.mem[0].lk, 16);
}
static void bt_key_in(const uint8_t r[32])
{
    uint32_t mk;
    memcpy(&mk, r, 4);
    if (mk != BT_KEY_MARK)
        return;
    bta.own_set = (uint8_t)(r[4] == 1u), bta.mem[0].bonded = (uint8_t)(r[5] == 1u);
    memcpy(bta.own, r + 6, 6);
    memcpy(bta.mem[0].lk, r + 12, 16);
}
/* the other remembered devices' record (storage/persist_rec.c bt_kept, 2026-10-24): bta.mem[1..3] as they are, 40 bytes
 * each (address, bonded, link key, name), the most recently used first; a name of "" ends them (a record from before: 0) */
#define BT_KEPT_SIZE (40u * (BTA_MEMS - 1u))
_Static_assert(sizeof(struct bta_mem) == 40u, "a remembered device: 40 bytes, no padding");
static void bt_kept_out(uint8_t r[BT_KEPT_SIZE])
{
    memset(r, 0, BT_KEPT_SIZE);
    if (bta.nmem > 1u)
        memcpy(r, &bta.mem[1], 40u * (bta.nmem - 1u));
}
static void bt_kept_in(const uint8_t r[BT_KEPT_SIZE])   /* after bt_rec_in and bt_key_in: none without a last device */
{
    bta.page_i = 0;
    for (; bta.nmem && bta.nmem < BTA_MEMS && r[23]; r += 40)   /* (r[23]: the name's first byte) */
        memcpy(&bta.mem[bta.nmem++], r, 40);
}
#endif

#if BT_OVL
/* side a's devices list's status line (op_project.c dev_draw): MIDI ble_devices.c's; AUDIO the scan's */
static uint32_t bt_list_line(uint32_t a, char *b, uint32_t room)
{
#if FELUCCA_BT_AUDIO
    if (a == BT_AUD) {
        uint32_t s = bta_state();
        str_cpy(b, s == BTA_S_SCANNING ? "SCANNING ..." : bta_rows() ? "SAVE CONNECTS, HOLD FORGETS" : "NONE FOUND", room);
        return s == BTA_S_SCANNING || bta_rows() ? BDL_PLAIN : BDL_WARN;
    }
#endif
    {
        int last;
        uint32_t nn;
        uint8_t near[BLE_SCAN_N];
        (void)a;
        (void)ble_dev_rows(&last, near, &nn);
        return ble_dev_line(b, room, nn);
    }
}
static uint32_t bt_list_line_name_at(uint32_t a)
{
#if FELUCCA_BT_AUDIO
    if (a == BT_AUD)
        return BLE_LINE_NO_NAME;
#endif
    (void)a;
    return ble_dev_line_name_at();
}
static uint32_t bt_list_sig(uint32_t a)                 /* what the list shows, as one number (a redraw when it changes) */
{
#if FELUCCA_BT_AUDIO
    if (a == BT_AUD) {
        uint32_t i, sig = bta.n * 7u + bta_state() * 131u + bta.nmem * 17u + bta.page_i;
        for (i = 0; i < bta.n; i++)
            sig = hs(sig, bta.d[i].name);
        for (i = 0; i < bta.nmem; i++)
            sig = hs(sig, bta.mem[i].name);
        return sig;
    }
#endif
    (void)a;
    return ble_dev_sig();
}
#endif
