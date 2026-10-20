/* SPDX-License-Identifier: GPL-3.0-only */
/* SYSTEM > Bluetooth, one pattern for MIDI and AUDIO (the user, 2026-10-19: "audio and midi should just follow the same
 * pattern, same ui, same logic"). Included by op_project.c after the MODE line (ble_ui_mode).
 *   MODE    OFF, MIDI MAN, MIDI AUTO, AUDIO MAN, AUDIO AUTO. MAN: only the user connects. AUTO: the same, and the last
 *           device is connected to at boot and when the mode is chosen. A link lost is not made again in either.
 *   SAVE    whatever line the cursor is on: connected or connecting, it lets go; else the last device, connected to;
 *           none, the scan (the devices list). On the list SAVE connects to the row (it becomes the last device).
 *   SAVE held  forgets the last device. HOME goes home, as anywhere.
 * MIDI advertises whenever no link is up (ble/ble_ll.c). Each action says what it did (ui_say). The mode and the AUDIO
 * last device are kept in the settings record (storage/persist_rec.c bt_rec). */
#define BT_TRY_MS 30000u                                /* a connect not up in this long: NO ANSWER, given up */
#define BT_REC_MARK 0x31525442u                         /* "BTR1" */
#if FELUCCA_BT_AUDIO
#define BT_AUDIO_BUILT 1
#else
#define BT_AUDIO_BUILT 0                                /* (an AUDIO mode in the record: OFF in this build) */
#endif
static uint8_t sys_dirty;                               /* (op_project.c: a setting changed, saved when SYSTEM is left) */
static struct {
    uint8_t want;                                       /* a connect under way (the user's, or AUTO's at boot) */
    uint8_t boot;                                       /* the saved mode applied */
    uint8_t rec_mode;                                   /* the mode as the settings record gave it (0xFF: none) */
    uint32_t t0;                                        /* fm1_ms: the connect's start */
} bt = {0, 0, 0xFFu, 0};

static int bt_audio(void) { return ble_ui_mode >= 3u; }
static int bt_auto(void) { return ble_ui_mode == 2u || ble_ui_mode == 4u; }

static int bt_has_last(void)
{
#if FELUCCA_BT_AUDIO
    if (bt_audio())
        return bta.have_last;
#endif
#if BLE_CENTRAL
    return ble_store_has_last(&ble_store);
#else
    return 0;
#endif
}
static void bt_last_name(char nm[BLE_NAME_MAX + 1u])
{
    nm[0] = 0;
#if FELUCCA_BT_AUDIO
    if (bt_audio()) {
        if (bta.have_last)
            str_cpy(nm, bta.last_name, BLE_NAME_MAX + 1u);
        return;
    }
#endif
#if BLE_CENTRAL
    if (ble_store_has_last(&ble_store))
        ble_store_name(&ble_store, nm);
#endif
}
static int bt_linked(void)                              /* a link is up */
{
#if FELUCCA_BT_AUDIO
    if (bt_audio()) {
        uint32_t a = bta_state();
        return a >= BTA_S_LINKED && a <= BTA_S_STREAMING;
    }
#endif
    return ble_ui_mode && ble_on && ble_connected();
}
static int bt_connecting(void)
{
#if FELUCCA_BT_AUDIO
    if (bt_audio())
        return bt.want || bta_state() == BTA_S_PAGING;
#endif
    return bt.want || (ble_ui_mode && ble_seeking() != 0);
}
#if BLE_CENTRAL
static void bt_midi_choice(uint8_t sel)                 /* LAST: the stack searches for it (ble_connect.c); NONE: it does not */
{
    if (ble_store.sel != sel)
        ble_store_select(&ble_store, sel);
}
#endif
static void bt_stop(void)                               /* the link, or the connect, ends; the last device kept */
{
    bt.want = 0;
#if FELUCCA_BT_AUDIO
    if (bt_audio()) {
        bta_hangup();
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
static void bt_go(void)                                 /* connect to the last device */
{
    bt.want = 1;
    bt.t0 = fm1_ms;
#if FELUCCA_BT_AUDIO
    if (bt_audio()) {
        (void)bta_page(bta.last);
        return;
    }
#endif
#if BLE_CENTRAL
    ble_connect_rearm();                                /* (a failure kept: the user acted) */
    bt_midi_choice(BLE_SEL_LAST);
#endif
}
static void bt_scan(int on)                             /* the devices list's scan */
{
#if FELUCCA_BT_AUDIO
    if (bt_audio()) {
        bta_scan(on);
        return;
    }
#endif
    ble_devices_open(on);
}

/* the devices list's rows: the devices heard (MIDI: LAST first, from ble_devices.c, its NONE row left out; AUDIO: the
 * inquiry's answers) */
static uint32_t bt_rows(void)
{
#if FELUCCA_BT_AUDIO
    if (bt_audio())
        return bta.n;
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
static void bt_row(uint32_t i, char nm[BLE_NAME_MAX + 1u], const char **tag, int32_t *bars)
{
    *tag = "";
    *bars = -1;
#if FELUCCA_BT_AUDIO
    if (bt_audio()) {
        str_cpy(nm, bta_dev_name(i), BLE_NAME_MAX + 1u);
        if (bta.have_last && i < bta.n && ble_eq(bta.d[i].addr, bta.last, 6))
            *tag = bt_linked() ? "CONNECTED" : bta_state() == BTA_S_PAGING ? "CONNECTING" : "LAST";
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
static void bt_pick(uint32_t i)                         /* SAVE on row i: connect to it */
{
    char nm[BLE_NAME_MAX + 1u];
    const char *tag;
    int32_t bars;
    if (i >= bt_rows())
        return;
#if FELUCCA_BT_AUDIO
    if (bt_audio()) {
        bt_row(i, nm, &tag, &bars);
        bta_pick(i);
    } else
#endif
    {
        struct ble_dev_key k;
        int r;
        if (bdv.key_ok && (i == bdv.key_row || (i < bdv.key_row && i + 1u == bt_rows())))
            k = bdv.key;                                /* (the row the user saw: that device, not that place; a shrunk
                                                         * list clamps the row) */
        else
            ble_dev_key_of(i + 1u, &k);
        r = ble_dev_key_row(&k);
        if (r < 1) {
            ui_message("DEVICE GONE");
            return;
        }
        bt_row((uint32_t)r - 1u, nm, &tag, &bars);
        ble_dev_pick((uint32_t)r);                      /* (LAST: the search for it; a nearby device: connected to, it
                                                         * becomes LAST once up) */
    }
    bt.want = 1;
    bt.t0 = fm1_ms;
    sys_dirty = 1;
    ui_say("CONNECTING ", nm);
}

/* SAVE on the Bluetooth page, any line */
static void bt_save(void)
{
    char nm[BLE_NAME_MAX + 1u];
    bt_last_name(nm);
    if (!ble_ui_mode) {
        ui_message("BLUETOOTH OFF");
        return;
    }
    if (bt_linked() || bt_connecting()) {
        bt_stop();
        ui_say("DISCONNECTED ", nm);
        return;
    }
    if (bt_has_last()) {
        bt_go();
        ui_say("CONNECTING ", nm);
        return;
    }
    dev_open();                                         /* (nothing remembered: the devices around, to pick one) */
}
static void bt_forget(void)                             /* SAVE held */
{
    char nm[BLE_NAME_MAX + 1u];
    if (!bt_has_last()) {
        ui_message("NO LAST DEVICE");
        return;
    }
    bt_last_name(nm);
    bt_stop();
#if FELUCCA_BT_AUDIO
    if (bt_audio()) {
        bta.have_last = 0;
        bta.bonded = 0;                                 /* (its key goes too) */
        memset(bta.lk, 0, sizeof bta.lk);
        bta.key_new = 1;
    } else
#endif
    {
#if BLE_CENTRAL
        ble_dev_forget();                               /* (its keys go too) */
#endif
    }
    sys_dirty = 1;
    ui_say("FORGOTTEN ", nm);
}

/* a mode chosen (MODE's knob, or the record at boot) */
static void bt_enter(uint32_t m)
{
    bt.want = 0;
    ble_ui_mode = (uint8_t)m;
#if FELUCCA_BT_AUDIO
    if (m >= 3u)
        bta_start(m == 3u ? BTA_MAN : BTA_AUTO);        /* (BLE MIDI off: one engine on the radio at a time) */
    else
        bta_stop();
#endif
    if (m == 1u || m == 2u) {
        ble_midi_set(1);
#if BLE_CENTRAL
        bt_midi_choice(BLE_SEL_NONE);
#endif
    } else if (m == 0u)
        ble_midi_set(0);
    if (bt_auto() && bt_has_last())
        bt_go();
}

/* main loop (op_input.c ui_input): the saved mode at boot; a connect given up after BT_TRY_MS; MIDI's search only while
 * a connect is wanted (MAN and AUTO alike: no link made again by itself) */
static void bt_poll(void)
{
#if FELUCCA_BT_AUDIO
    if (bta.key_new) {                                  /* (our address made, a device bonded or forgotten: kept) */
        bta.key_new = 0;
        sys_dirty = 1;
        settings_later = 1;
    }
    if (bt_audio()) {                                   /* (the link ended by itself: where it had got to) */
        const char *w = bta_lost();
        if (w) {
            ui_message(w);
            bt.want = 0;
        }
    }
#endif
    if (!bt.boot) {
        if (fm1_ms < 1500u)
            return;                                     /* (the radio and the settings up first) */
        bt.boot = 1;
        if (bt.rec_mode <= 4u && (bt.rec_mode >= 3u ? BT_AUDIO_BUILT : bt.rec_mode == 0u || ble_on))
            bt_enter(bt.rec_mode);
        else                                            /* (no record yet: as the MODE line read before it) */
#if BLE_CENTRAL
            bt_enter(!ble_on ? 0u : bt_has_last() && ble_store.sel == BLE_SEL_LAST ? 2u : 1u);
#else
            bt_enter(ble_on ? 1u : 0u);
#endif
    }
    if (!bt.want)
        goto idle;
    if (bt_linked()) {
        bt.want = 0;
        goto idle;
    }
    if (fm1_ms - bt.t0 >= BT_TRY_MS) {
        char nm[BLE_NAME_MAX + 1u];
        bt_last_name(nm);
        bt_stop();
        ui_say("NO ANSWER ", nm);
        return;
    }
#if FELUCCA_BT_AUDIO
    if (bt_audio()) {
        if (bta_state() == BTA_S_REFUSED) {             /* (it answered and said no: not asked again) */
            char nm[BLE_NAME_MAX + 1u];
            bt_last_name(nm);
            bt_stop();
            ui_say("REFUSED ", nm);
        } else if (bta_state() != BTA_S_PAGING && bta.have_last)
            (void)bta_page(bta.last);                   /* (a page is ~5 s: again until BT_TRY_MS) */
        return;
    }
#endif
#if BLE_CENTRAL
    {
        char st[40];
        if (ble_connect_status(st, sizeof st) == RCS_BAD)
            bt.want = 0;                                /* (the stack gave up and says why: FAILED: <why>) */
    }
#endif
    return;
idle:
#if BLE_CENTRAL
    if (!bt_audio() && ble_seeking() != 1)
        bt_midi_choice(BLE_SEL_NONE);                   /* (no search for LAST once up or let go: a link lost stays lost) */
#endif
    return;
}

#if FELUCCA_BT_AUDIO
#include "op_volgest.h"
/* SHIFT + the volume knob (system/main.c master_pot, a reading at a time): 15 % of its travel switches the output
 * between Bluetooth audio and the FM-1's own, while audio streams (op_volgest.h); 1: the volume holds */
static struct vgest bt_vg;
static int bt_vol_hold(int32_t knob)
{
    int t, held = vgest_step(&bt_vg, btn_held(B_OCTDN), knob, &t);
    if (t) {
        if (!bt_audio() || bta_state() != BTA_S_STREAMING) {
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

/* the settings record's bytes (storage/persist_rec.c): the mark, the mode, the AUDIO last device */
#define BT_REC_SIZE 32u
static void bt_rec_out(uint8_t r[BT_REC_SIZE])
{
    uint32_t mk = BT_REC_MARK;
    memset(r, 0, BT_REC_SIZE);
    memcpy(r, &mk, 4);
    r[4] = ble_ui_mode;
#if FELUCCA_BT_AUDIO
    r[5] = bta.have_last;
    memcpy(r + 6, bta.last, 6);
    str_cpy((char *)r + 12, bta.last_name, 17);
#endif
}
static void bt_rec_in(const uint8_t r[BT_REC_SIZE])
{
    uint32_t mk;
    memcpy(&mk, r, 4);
    if (mk != BT_REC_MARK)
        return;
    bt.rec_mode = r[4];
#if FELUCCA_BT_AUDIO
    bta.have_last = (uint8_t)(r[5] == 1u);
    memcpy(bta.last, r + 6, 6);
    str_cpy(bta.last_name, (const char *)r + 12, sizeof bta.last_name);
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
    r[4] = bta.own_set, r[5] = bta.bonded;
    memcpy(r + 6, bta.own, 6);
    memcpy(r + 12, bta.lk, 16);
}
static void bt_key_in(const uint8_t r[32])
{
    uint32_t mk;
    memcpy(&mk, r, 4);
    if (mk != BT_KEY_MARK)
        return;
    bta.own_set = (uint8_t)(r[4] == 1u), bta.bonded = (uint8_t)(r[5] == 1u);
    memcpy(bta.own, r + 6, 6);
    memcpy(bta.lk, r + 12, 16);
}
#endif

/* the devices list's status line (op_project.c dev_draw): MIDI ble_devices.c's; AUDIO the scan's */
static uint32_t bt_list_line(char *b, uint32_t room)
{
#if FELUCCA_BT_AUDIO
    if (bt_audio()) {
        uint32_t a = bta_state();
        str_cpy(b, a == BTA_S_SCANNING ? "SCANNING ..." : bta.n ? "SAVE CONNECTS, HOLD SAVE FORGETS" : "NONE FOUND", room);
        return a == BTA_S_SCANNING || bta.n ? BDL_PLAIN : BDL_WARN;
    }
#endif
    {
        int last;
        uint32_t nn;
        uint8_t near[BLE_SCAN_N];
        (void)ble_dev_rows(&last, near, &nn);
        return ble_dev_line(b, room, nn);
    }
}
static uint32_t bt_list_line_name_at(void)
{
#if FELUCCA_BT_AUDIO
    if (bt_audio())
        return BLE_LINE_NO_NAME;
#endif
    return ble_dev_line_name_at();
}
static uint32_t bt_list_sig(void)                       /* what the list shows, as one number (a redraw when it changes) */
{
#if FELUCCA_BT_AUDIO
    if (bt_audio()) {
        uint32_t i, sig = bta.n * 7u + bta_state() * 131u + bta.have_last;
        for (i = 0; i < bta.n; i++)
            sig = hs(sig, bta.d[i].name);
        return sig;
    }
#endif
    return ble_dev_sig();
}
