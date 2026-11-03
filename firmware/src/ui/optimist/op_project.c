/* SPDX-License-Identifier: GPL-3.0-only */
/* SYSTEM (docs/UI-OPTIMIST-DESIGN.md 4.7 GLOBAL and SYSTEM); PROJECT is op_prjflow.c (4.6 PROJECT). An action cell does
 * nothing when turned: its knob makes it the hot cell, YES does it, and what destroys asks first (op_state.c op_arm). */

#if FELUCCA_BT_UI
/* ---- BLE devices: SYSTEM > BLUETOOTH, YES on the BLE row opens the list (the same one SLOOP's menu has: NONE, LAST, the
 * BLE-MIDI devices nearby, io/midi/ble_devices.c; the rows, their tags, the status line and the connecting logic are
 * its, shared). While it is open SYSTEM's rows ARE the list: SELECT moves the cursor (it stops at the ends), SAVE picks
 * the row (NONE, LAST) or connects to it, HOME goes back (the scan stops, the BLUETOOTH row is where it was), SHIFT + a
 * button asks FORGET LAST (the modal; SAVE yes). The panel is drawn by dev_draw (op_draw.c calls it), the cards show the
 * row's action and its signal. The words are in sentence case, as the rest of the UI; a device's name is shown as it
 * advertises itself ("KeyStep 37", "MPC Live"), never recased. */
#define DEV_ROW_H 36                                    /* a row: the name in the large face (the user, 2026-10-19: "use a bigger font") */
#define DEV_TY ((DEV_ROW_H - 16) / 2)                   /* the small words' and the bars' top in a row (the tag, NONE's mark) */
#define BLE_ACT_MS 300u                                 /* SYSTEM > Bluetooth's MIDI IN / OUT lit this long after a message */
#define DEV_STATUS_H 24                                 /* the status line under the rows */
#define DEV_LINE_W 232                                  /* the status line's room: 4 px in from each side */
/* The overlay list (dev_open ... dev_draw) is built only where something can open it: BT MIDI without BLE_CENTRAL (op_bt.c
 * bt_save). With the central, or BT AUDIO, the Bluetooth pages list their devices themselves (op_bt.c bt_pg_*) and the overlay
 * is compiled out (2026-10-24: BT_OVL 0; its stand-ins below keep every caller as it was, folded away). */
#define BT_OVL (FELUCCA_BLE && !BLE_CENTRAL)
#if BT_OVL
#define BDV_OPEN (bdv.open != 0u)
#else
#define BDV_OPEN 0
#endif
static struct {
#if BT_OVL
    uint8_t open, sys_row;                              /* the list is open; SYSTEM's row to come back to */
    uint8_t first;                                      /* the first row shown: kept, it moves only to follow the cursor */
#endif
    uint8_t key_ok, key_row;                            /* the row drawn highlighted, and which device it is */
    struct ble_dev_key key;                             /* (YES picks that device, wherever the list moved it) */
} bdv;
static struct {
#if BT_OVL
    int last;                                           /* what dev_paint draws */
    uint8_t near[BLE_SCAN_N];
    uint32_t n, cur, first, shown, h, tone;
#endif
    uint16_t bar;
    union {
#if BT_OVL
        char st[40];                                    /* the status line (dev_draw, while the list is open) */
#endif
        struct {
            uint32_t i, o, ti, to;                      /* the Bluetooth page's MIDI line: midi_ble.c's ring counts seen, and when
                                                         * they moved (only while the list is closed: no RAM of its own) */
        } act;
    };
#if BT_OVL && BLE_CEN_PAIR
    uint32_t key;                                       /* the passkey (BLE_NO_PASSKEY: none) */
#endif
} bdp;
static void cv_tall(uint32_t y, uint32_t h, uint16_t bg, void (*fn)(void));   /* op_graph.c */
static uint32_t hs(uint32_t h, const char *s);          /* (signatures: op_draw.c) */
static uint32_t hu(uint32_t h, uint32_t v);
static const char *cut(char *b, const char *s, uint32_t n);
static const felucca_font_t *font_big(void);            /* op_graph.c */

enum { BT_MIDI, BT_AUD };                               /* the two Bluetooth pages' sides (op_bt.c): BT MIDI, BT AUDIO */
static void bt_scan(uint32_t a, int on);                /* op_bt.c */
static uint32_t bt_rows(uint32_t a);
static void bt_row(uint32_t a, uint32_t i, char nm[BLE_NAME_MAX + 1u], const char **tag, int32_t *bars);
static void bt_pick(uint32_t a, uint32_t i);
#if !BT_OVL                                             /* (nothing opens the overlay: its callers fold away) */
static int dev_listing(void) { return 0; }
static uint32_t dev_side(void) { return BT_MIDI; }
static uint32_t dev_rows(void) { return 0; }
static void dev_close(void) {}
static void dev_open(uint32_t a) { (void)a; }
static void dev_draw(uint32_t y, uint32_t h) { (void)y, (void)h; }
static void dev_cell(uint32_t r, uint32_t k, cell_t *c) { (void)r, (void)k, cell_clear(c); }
static int dev_yes(uint32_t r, uint32_t ok) { (void)r, (void)ok; return 0; }
#else
static uint32_t bt_list_line(uint32_t a, char *b, uint32_t room);
static uint32_t bt_list_line_name_at(uint32_t a);
static uint32_t bt_list_sig(uint32_t a);
static int dev_listing(void) { return bdv.open != 0u && ui.scr == SCR_SYSTEM; }
static uint32_t dev_side(void) { return bdv.open == 2u ? BT_AUD : BT_MIDI; }   /* (the list open is that page's) */
static uint32_t dev_rows(void) { return bt_rows(dev_side()); }
static void dev_open(uint32_t a)                        /* side a's devices list (bdv.open: 1 MIDI, 2 AUDIO) */
{
    bdv.open = (uint8_t)(a + 1u);
    bdv.first = 0;
    bdv.key_ok = 0;
    bdv.sys_row = ui.row[SCR_SYSTEM];
    ui.row[SCR_SYSTEM] = 0;                             /* (the cursor on NONE) */
    ui.hot = 0;
    ui.hot_lit = 0;
    op_disarm();
    bt_scan(a, 1);                                      /* (the scan starts) */
    ui.force = 1;
}
static void dev_close(void)
{
    if (!bdv.open)
        return;
    bt_scan(dev_side(), 0);                             /* (the scan stops, advertising goes on) */
    bdv.open = 0;
    bdp.act.i = bmi_w, bdp.act.o = bmo_w;               /* (the MIDI line's counters take the status line's place: dark) */
    bdp.act.ti = bdp.act.to = fm1_ms - BLE_ACT_MS;
    ui.row[SCR_SYSTEM] = bdv.sys_row;
    ui.hot = 0;
    ui.hot_lit = 0;
    op_disarm();
    ui.force = 1;
}
static void dev_cell(uint32_t r, uint32_t k, cell_t *c) { (void)r, (void)k, cell_clear(c); }   /* (no cards over the list) */
static int dev_yes(uint32_t r, uint32_t ok)             /* SAVE on a row: connect to it, back to the page */
{
    (void)ok;
    bt_pick(dev_side(), r);
    dev_close();
    return 1;
}
#endif
/* the signal of a nearby row: three bars rising, as many lit as the scan heard (ble_dev_row: relative, no RSSI gain
 * table yet); none lit: not heard in this scan */
static void dev_bars(int32_t x, int32_t y, uint32_t n, int on)
{
    int32_t k;
    for (k = 0; k < 3; k++)
        if ((uint32_t)k >= n && !on)                    /* (unlit: outlined on black) */
            cv_frame(x + k * 6, y + 16 - 4 * (k + 1), 4, 4 * (k + 1), C_LINE);
        else
            cv_rect(x + k * 6, y + 16 - 4 * (k + 1), 4, 4 * (k + 1), (uint32_t)k < n ? (on ? cur_ink(bdp.bar) : C_GRAY) : cur_trough(bdp.bar));
}
#if BT_OVL
static void dev_paint(void)                             /* the panel's band in its own coordinates (cv_tall) */
{
    uint32_t i, rows_h = bdp.h - DEV_STATUS_H;
    char nm[BLE_NAME_MAX + 4u], b[BLE_NAME_MAX + 4u];
    int32_t wide = bdp.n > bdp.shown ? 236 : 240;
#if BLE_CEN_PAIR
    if (bdp.key != BLE_NO_PASSKEY) {                    /* pairing with a code: the phone's user types it */
        char d[7];
        pk_digits(d, bdp.key);
        cv_text_mid((int32_t)bdp.h / 2 - 30, &FONT_S, bdp.st, C_HI);
        cv_text_mid((int32_t)bdp.h / 2 - 8, font_big(), d, C_WHITE);
        return;
    }
#endif
    for (i = bdp.first; i < bdp.n && i < bdp.first + bdp.shown; i++) {
        int32_t y = (int32_t)(i - bdp.first) * DEV_ROW_H, right = wide - 4, room, bars;
        int on = i == bdp.cur;
        const char *tag;
        bt_row(dev_side(), i, nm, &tag, &bars);
        if (on)
            cv_rect(0, y, wide, DEV_ROW_H - 1, cur_bg(bdp.bar));   /* the cursor row: the track's colour, its ink */
        if (bars >= 0 && (!tag[0] || str_len(tag) <= 4u)) {   /* (CONNECTED / CONNECTING / PAIRING take the bars' place) */
            dev_bars(wide - 22, y + DEV_TY, (uint32_t)bars, on);
            right = wide - 28;
        }
        if (tag[0]) {
            op_case(b, tag, sizeof b);
            right -= text_ws(b);
            cv_texts(right, y + DEV_TY, b, on ? cur_ink(bdp.bar) : C_GRAY);
            right -= 8;
        }
        room = right - 4;
        cv_text(4, y + 2, font_big(), cut(b, nm, room > 16 ? (uint32_t)room / 16u : 1u), on ? cur_ink(bdp.bar) : C_GRAY);
    }
    if (bdp.n > bdp.shown) {                            /* where the window is in the list */
        scroll_bar(0, (int32_t)rows_h, (int32_t)bdp.first, (int32_t)bdp.shown, (int32_t)bdp.n);
    }
    cv_rect(0, (int32_t)rows_h, 240, 1, C_LINE);
    cv_texts(4, (int32_t)rows_h + 5, bdp.st, bdp.tone == BDL_GOOD ? C_OK : bdp.tone == BDL_WARN ? C_WARN : bdp.tone == BDL_BAD ? C_ERR : C_DIM);
}
/* the status line as drawn: the words in sentence case, a device's name in it as advertised (from at, ble_dev_line_name_at),
 * cut at the end to the room (the name goes first, then the words) */
static void dev_line_text(char *out, uint32_t room, const char *line, uint32_t at)
{
    char words[sizeof bdp.st];
    uint32_t n = str_len(line);
    if (at > n)
        at = n;
    str_cpy(words, line, at + 1u);
    op_case(out, words, room);
    str_app(out, line + at, room);
    while (out[0] && text_ws(out) > DEV_LINE_W)
        out[str_len(out) - 1u] = 0;
}
/* the list in the panel y, h tall (op_draw.c, when dev_listing): lazy, redrawn when what it shows changes */
static void dev_draw(uint32_t y, uint32_t h)
{
    uint32_t sig;
    char b[sizeof bdp.st];
    bdp.n = dev_rows();
    if (ui.row[SCR_SYSTEM] >= bdp.n && bdp.n)
        ui.row[SCR_SYSTEM] = (uint8_t)(bdp.n - 1u);     /* (the list shrank) */
    bdp.cur = ui.row[SCR_SYSTEM];
    bdp.shown = (h - DEV_STATUS_H) / DEV_ROW_H;
    bdp.first = bdv.first;                              /* (kept: the window moves only when the cursor leaves it) */
    if (bdp.cur < bdp.first)
        bdp.first = bdp.cur;
    else if (bdp.cur >= bdp.first + bdp.shown)
        bdp.first = bdp.cur - (bdp.shown - 1u);
    if (bdp.first + bdp.shown > bdp.n)                  /* (the list shrank: no empty rows under the last one) */
        bdp.first = bdp.n > bdp.shown ? bdp.n - bdp.shown : 0u;
    bdv.first = (uint8_t)bdp.first;
    bdv.key_ok = 0;
    if (dev_side() == BT_MIDI && bdp.cur < bdp.n) {               /* (MIDI: what is highlighted, SAVE picks it by what it is) */
        ble_dev_key_of(bdp.cur + 1u, &bdv.key);
        bdv.key_row = (uint8_t)bdp.cur;
        bdv.key_ok = 1;
    }
    bdp.h = h;
    bdp.bar = trk_col(song.sel);
#if BLE_CEN_PAIR
    bdp.key = ble_connect_passkey();
#endif
    bdp.tone = bt_list_line(dev_side(), b, sizeof b);
    dev_line_text(bdp.st, sizeof bdp.st, b, bt_list_line_name_at(dev_side()));
    sig = hu(hs(hu(hu(hu(bt_list_sig(dev_side()) + bdv.open, bdp.cur), bdp.first), bdp.bar), bdp.st), h) | 1u;
    if (sig == ui.sig[2])
        return;
    ui.sig[2] = sig;
    cv_tall(y, h, C_BLACK, dev_paint);
}
#endif
/* ---- SYSTEM > Bluetooth and Devices, the lines (docs/UI-OPTIMIST-DESIGN.md 4.7 GLOBAL and SYSTEM): STATE in words, the DEVICE the
 * link is with, MIDI activity, RECONNECT (the list's NONE / LAST) and FORGET LAST. Read from ble_devices.c, nothing new kept
 * but the activity's two counters */
static int32_t sys_step(int32_t v, int32_t s, int32_t max);
#define bact bdp.act                                    /* (in the list's drawing state: the list and this line are never both shown) */
#include "op_bt.c"                                      /* the one pattern for both pages: SAVE, AUTO, the lists' rows */

static void ble_mode_sync(void)                         /* the radio switched elsewhere (a pick with it off, NO CAL): BT MIDI's
                                                         * MODE follows; else MODE is the choice (op_bt.c, kept in the record) */
{
    if (bt_mode(BT_MIDI) && !ble_on)
        bt_mode_put(BT_MIDI, BT_OFF);
    else if (!bt_mode(BT_MIDI) && ble_on)
        bt_mode_put(BT_MIDI, BT_MAN);
}
/* MODE on side a's page: OFF, MAN, AUTO (NO CAL: the radio's stored trims missing) */
static void bt_mode_cell(uint32_t a, cell_t *c)
{
    static const char *const M[3] = {"OFF", "MAN", "AUTO"};
    uint32_t m;
    if (a == BT_MIDI)
        ble_mode_sync();
    m = bt_mode(a);
    c->kind = CK_VAL;
    c->label = "MODE";
    c->big = 1;
    cell_val(c, !ble_radio_ok() && m ? "NO CAL" : M[m % 3u]);
    c->col = m && !ble_radio_ok() ? C_WARN : 0;
}
static void bt_mode_set(uint32_t a, int32_t s)          /* a detent a mode, stopping at the ends; YES round */
{
    if (a == BT_MIDI)
        ble_mode_sync();
    bt_enter(a, (uint32_t)sys_step((int32_t)bt_mode(a), s, BT_AUTO));
}

static const char *const BLE_ST[9] = {"OFF", "ADVERTISING", "CONNECTED", "NO CAL", "SCANNING", "CONNECTING", "SEARCHING",
                                      "PAIRING", "FAILED"};   /* (ble_dev_status's codes) */
static void ble_state_cell(cell_t *c)                   /* BT MIDI's STATE */
{
    uint32_t s = ble_dev_status();
    c->kind = CK_RO;
    c->label = "STATE";
    c->big = 1;
    cell_val(c, ble_on && !ble_up && s == 0u ? "STARTING" : BLE_ST[s % 9u]);
    c->col = s == 2u ? C_OK : s == 3u ? C_WARN : s == 8u ? C_ERR : 0;
}
/* BT MIDI's DEVICE: the device the link is with (or being made with, or searched for), its name as advertised; HOST, a
 * computer or phone that connected to the FM-1 (its name is not known); NONE */
static void ble_peer_cell(cell_t *c)
{
    char nm[40];
    const char *v = "NONE";
    c->kind = bt_pg_on(BT_MIDI) ? CK_VAL : CK_RO;       /* (a knob that moves the page's list: op_bt.c) */
    c->label = "DEVICE";
#if BLE_CENTRAL
    if (ble_on && ble_up && ble_connect_status(nm, sizeof nm) != RCS_NONE && rc_nm_at < str_len(nm))
        v = nm + rc_nm_at;                              /* (connecting / pairing / connected to a pick) */
    else
#endif
    if (bt_has_last(BT_MIDI))
        bt_last_name(BT_MIDI, nm), v = nm;
    else if (ble_on && ble_connected())
        v = "HOST";
    cell_val(c, v);
    c->big = str_len(c->val) <= 9u;                     /* (a longer name in the small face; 13 characters of it shown) */
    c->col = bt_linked(BT_MIDI) ? C_OK : 0;
}
static void ble_midi_cell(cell_t *c)                    /* BT MIDI's ACTIVITY: IN, OUT, lit a moment after a message */
{
    ble_mode_sync();
    c->kind = CK_RO;
    c->big = 1;
    c->label = "ACTIVITY";
    if (ble_on) {
        uint32_t in, out;
        if (bmi_w != bact.i)
            bact.i = bmi_w, bact.ti = fm1_ms;
        if (bmo_w != bact.o)
            bact.o = bmo_w, bact.to = fm1_ms;
        in = fm1_ms - bact.ti < BLE_ACT_MS;
        out = fm1_ms - bact.to < BLE_ACT_MS;
        cell_val(c, in && out ? "IN OUT" : in ? "IN" : out ? "OUT" : "--");
        c->col = in || out ? C_OK : 0;
    } else {
        cell_val(c, "--");
        c->col = 0;
    }
}
#if FELUCCA_BT_AUDIO
static const char *const BTA_ST[12] = {"OFF", "SCANNING", "NOT CONNECTED", "NOT CONNECTED", "CONNECTING", "SETTING UP",
                                       "SETTING UP", "CONNECTED", "REFUSED", "NOT CONNECTED", "NOT CONNECTED", "NOT CONNECTED"};
                                                        /* (bt_audio.c BTA_S_*, said as MIDI says them) */
static void bta_state_cell(cell_t *c)                   /* BT AUDIO's STATE: the Classic link's */
{
    uint32_t s = bta_state();
    c->kind = CK_RO;
    c->label = "STATE";
    c->big = 1;
    cell_val(c, bt_wants(BT_AUD) ? "CONNECTING" : s == BTA_S_STREAMING && !bta_out_bt() ? "INTERNAL OUT" : BTA_ST[s % 12u]);
    c->col = s == BTA_S_STREAMING ? C_OK : s == BTA_S_REFUSED ? C_ERR : 0;
}
static void bta_peer_cell(cell_t *c)                    /* BT AUDIO's DEVICE: the last device (the one connected or paged), NONE */
{
    char nm[BLE_NAME_MAX + 1u];
    c->kind = bt_pg_on(BT_AUD) ? CK_VAL : CK_RO;        /* (a knob that moves the page's list: op_bt.c) */
    c->label = "DEVICE";
    bt_last_name(BT_AUD, nm);
    cell_val(c, nm[0] ? nm : "NONE");
    c->big = str_len(c->val) <= 9u;
    c->col = bt_linked(BT_AUD) ? C_OK : 0;
}
static void bta_fmt_cell(cell_t *c)                     /* BT AUDIO's FORMAT */
{
    c->kind = CK_RO;
    c->big = 1;
    c->label = "FORMAT";
    cell_val(c, "SBC 44.1k");
    c->col = C_OK;
}
#endif
#if BLE_CENTRAL
static void ble_auto_cell(cell_t *c)                    /* RECONNECT: LAST's name when it is the choice, else OFF (NONE); a
                                                         * read-out while there is no LAST (nothing to choose) */
{
    c->label = "RECONNECT";
    if (!ble_store_has_last(&ble_store))
        c->kind = CK_RO;
    if (ble_store_has_last(&ble_store) && ble_store.sel == BLE_SEL_LAST) {
        char nm[BLE_NAME_MAX + 1u];
        ble_store_name(&ble_store, nm);
        cell_val(c, nm);
    } else
        cell_val(c, "OFF");
}
static void ble_auto_set(int32_t s)                     /* right LAST, left NONE, YES (0) the other: ble_devices.c's pick */
{
    uint32_t last = ble_store.sel == BLE_SEL_LAST, want = s > 0 ? 1u : s < 0 ? 0u : !last;
    if (want == last)
        return;
    if (want && !ble_store_has_last(&ble_store)) {
        ui_message("NO LAST DEVICE");
        return;
    }
    ble_dev_pick(want);                                 /* (row 1 is LAST when there is one) */
}
static int ble_forget_yes(uint32_t r, uint32_t k, uint32_t ok)   /* FORGET LAST: asked, naming it */
{
    char nm[BLE_NAME_MAX + 1u];
    if (!ble_store_has_last(&ble_store)) {
        ui_message("NO LAST DEVICE");
        return 1;
    }
    if (ok) {
        ble_dev_forget();
        return 1;
    }
    ble_store_name(&ble_store, nm);
    op_arm(SCR_SYSTEM, r, k, "FORGET", nm, 1);
    ui.arm_raw = 1;                                     /* (the name as advertised) */
    return 1;
}
#endif
#endif

#if WIFI_UI
/* SYSTEM > WI-FI and the password entry (op_wifi.c). That file drives the Wi-Fi manager (wifi/wifi_conn.c), whose state is
 * defined after the UI in the unity build, so it is included late (felucca.c, after io/console.c); the UI above it knows only
 * these. Without WIFI_UI each folds to nothing and the pages are as they were. */
static void wf_cell(uint32_t it, cell_t *c);            /* a WI-FI line: MODE, NETWORK, SIGNAL, STATE */
static void wf_mode_set(int32_t s);
static int wf_turn(int32_t s);                          /* NETWORK's knob: the list's cursor -> 1 when the list is shown */
static int wf_ch_turn(int32_t s);                       /* CHANNEL's knob in MODE AP -> 1 taken */
static int wf_grp_turn(int32_t s);                      /* GROUP's knob in the JAM modes -> 1 taken */
static int wf_st_turn(int32_t s);                       /* STATE's knob: the line's items, the cycling held 30 s -> 1 taken */
static void wf_save(void);                              /* SAVE tapped on the page */
static void wf_forget_do(void);                         /* the FORGET question answered YES */
static int wf_key(uint32_t b, int held);                /* op_input.c: SAVE tapped / held on the WI-FI page -> 1 taken */
static uint32_t wf_pg_shown(void);                      /* the page on screen shows its networks (the two middle lines give way) */
static void wf_pg_paint(int32_t y0, int32_t y1, uint16_t col);
static uint32_t wf_pg_sig(void);
static void wf_poll(void);                              /* every UI pass: the scan follows the page, a failure said once */
static int pw_on(void);                                 /* the password entry is open (NAME's gates hold: name_on) */
static void pw_frame(uint32_t pressed, uint32_t held);
static void pw_draw(void);
static void pw_title(char *t, uint32_t n);
static uint32_t pw_leds(uint32_t *d);
#else
#define wf_key(b, held) 0
#define wf_pg_shown() 0u
#define pw_on() 0
#endif

#include "../../system/dual_mode.h"                     /* 2ND CORE: DUALM_*, dual_why_name, and the INFO text helpers dual_cat / dual_cnum / dual_chex (LAST RESET: every UI=1 build; pure, no state) */
#include "../../system/stackmark.h"                     /* INFO's RAM line: how deep the stacks went (painted at boot: stk_boot) */
/* ---- SYSTEM: SLOOP's HOME-held menu as rows (ui/sloop/ui_menu.c) */
enum { SI_NONE, SI_TUNE, SI_RECQ, SI_PUNCH, SI_OUT, SI_SYNC, SI_CH1, SI_CH2,
       SI_CH3, SI_CHD, SI_USB, SI_CPU, SI_ABOUT, SI_BLE, SI_KCOL, SI_DBGST, SI_METERS, SI_CLKOUT, SI_CPU1, SI_INFO, SI_BUILD, SI_REPO,
       SI_DROPS, SI_GUARD, SI_UAINFO, SI_XRUN, SI_DIAG, SI_CREDITS, SI_BLEST, SI_BLEPEER, SI_BLEMIDI, SI_BLELIST, SI_BLEAUTO,
       SI_BLEFGT, SI_FRESO, SI_HPBASS, SI_BTA, SI_BTAPEER, SI_BTAFMT, SI_BTAST, SI_CLKOFS, SI_BTOFS, SI_LINK, SI_LINKOFS,
#if FELUCCA_SD_UI
       SI_SDCARD, SI_SDVERS, SI_SDSLACK, SI_SDINDEX,    /* SD CARD: the card, VERSIONS, SLACK, the sample index (sd_mode.c) */
       SI_SDPRE,                                        /* (SD_STREAM: the card samples' pre-buffer) */
       SI_SDBACK, SI_SDREST,                            /* SETTINGS: BACKUP to the card, RESTORE from it (sd_mode.c) */
#endif
       SI_WFMODE, SI_WFNET, SI_WFSIG, SI_WFST,           /* SYSTEM > WI-FI (op_wifi.c): MODE, NETWORK, SIGNAL, STATE */
       SI_RAM,                                          /* INFO: the SRAM free at link, the stacks' least room left (stackmark.h) */
       SI_UHMODE, SI_UHDEV,                             /* SYSTEM > USB (USB_HOST, the release): MODE DEVICE / HOST, DEVICE */
       SI_ARN,                                          /* INFO: the arena's longest move and interrupts-off window (us) */
       SI_RAMUSE,                                       /* INFO: the arena's use, parts / features / free (core/feat_mem.c) */
       SI_DUALT,                                        /* SYSTEM > CPU TIMES: the dual render's times (SYS_DEBUG, the second core) */
       SI_BTSTAT,                                       /* SYSTEM > BT STATS: the Bluetooth audio stream's counters (SYS_DEBUG) */
       SI_TOP,                                          /* SYSTEM > TOP: what takes the CPU and the RAM (SYS_TOP: op_top.c) */
       SI_UAS0 };                                       /* SI_UAS0 + s: USB AUDIO's line of source s (ua_tap.h UAS_*), last */
/* a core's load number (% of the audio budget) in its colour: green under 60, amber 60..85, red over 85 (the predictive guard's
 * ceiling, system/cpuguard.c CG_CEIL = 85 %: above it the guard sheds quality / voices); INFO's bars and ticks the same. */
#define LOAD_AMBER 60
#define LOAD_RED 85
static uint16_t load_col(int32_t pct) { return pct > LOAD_RED ? C_ERR : pct >= LOAD_AMBER ? C_WARN : C_OK; }
/* INFO's overload lines (system/overload.h, cpuguard.c): the CPU lines (a bar of the load now, the ticks of its 10 s and
 * boot peaks; the number is the load now), DROPS, UNDERRUNS,
 * GUARD; DROPS, UNDERRUNS, MAX and the voices shed counted from a baseline SHIFT + YES on INFO sets (op_global.c info_clear;
 * the counters themselves go on: the console still reads them since boot) */
#define DROPS_RED_MS 5000u                              /* DROPS red while a late half came within this long, else amber */
static uint32_t info_shed0;
/* UNDERRUNS: everything else lost since boot: USB audio's playback / capture under- and overruns and missed frames (USB audio
 * builds), MIDI in that found its ring full (USB, TRS, BLE) */
static uint32_t info_ua_xruns(void)
{
#if FELUCCA_USB_AUDIO
    return ua.play_underruns + ua.play_overruns + ua.cap_underruns + ua.cap_overruns + ua.missed_frames;
#else
    return 0;
#endif
}
static uint32_t info_xruns(void) { return info_ua_xruns() + ovl_midi_lost(); }
static void info_clear_counts(void)                     /* SHIFT + YES on INFO: DROPS, UNDERRUNS, the maxima, the voices shed from 0 */
{
    uint32_t r;
    ovl.drops0 = ovl.drops;
    ovl.xrun0 = info_xruns();
    for (r = 0; r < OVL_NRING; r++)
        ovl.r[r].amax = 0;
#if FELUCCA_CPU_GUARD && FELUCCA_SYS_DEBUG
    info_shed0 = cg.sheds;
#endif
}
/* a CPU line's form: its bar the load now (0..100), the bright tick at the last ~10 s's peak, the dim one at the peak since boot
 * (op_gldraw.c gl_info_bar; the user, 2026-10-28: "VU peak-hold style"), each in the load's colours there */
static void info_bar(cell_t *c, uint32_t ring, int32_t now)
{
    uint32_t p10 = ovl_win_pct(ring, fm1_ms, OVL_W10), pall = ovl_maxall_pct(ring);
    cell_gauge(c, 0, 0, 100, now < 100 ? now : 100);
    c->pkbar = 1;
    c->hold[0] = (int16_t)(p10 < 100u ? p10 : 100u);
    c->hold[1] = (int16_t)(pall < 100u ? pall : 100u);
}
#if FELUCCA_CPU_GUARD && FELUCCA_SYS_DEBUG
static const char *const CG_NAME[CG_TOP + 1] = {"OFF", "QUALITY", "UNISON", "NOTES", "REVERB"};
#endif
#define SI_IF(c, i) ((c) ? (uint8_t)(i) : (uint8_t)SI_NONE)
#if FELUCCA_USB_AUDIO
#define UA_IT(s) SI_IF((s) < UAS_RA, SI_UAS0 + (s))     /* USB AUDIO's line of source s, none past the last lane */
#define UA_RT(k) ((uint8_t)(SI_UAS0 + UAS_RA + (k)))   /* .. of RET A B C (k 0..2) */
_Static_assert(UAS_N <= 19u && UAS_RA <= 16u, "USB AUDIO: four rows of four sources, then the returns' row");
static int si_ua(uint32_t it) { return it >= SI_UAS0 && it < SI_UAS0 + UAS_N; }
#else
#define si_ua(it) 0
#endif
#if FELUCCA_SYS_DEBUG && DUAL_LOAD && DUAL_TIMES                 /* (DUAL_LOAD: every dual build, and the host UI test's CPU1 load build) */
#define OP_DTIMES 1                                     /* SYSTEM > CPU TIMES (sys_dt_cell): the dual render's times */
#else
#define OP_DTIMES 0
#endif
#if FELUCCA_SYS_DEBUG && FELUCCA_BT_AUDIO && defined(PLAT_BTA_STATS)   /* (fm1-base's plat_bta_stats: the release) */
#define OP_BTSTATS 1                                    /* SYSTEM > BT STATS (sys_bts_cell) */
#else
#define OP_BTSTATS 0
#endif
#ifndef FELUCCA_SYS_TOP
#define FELUCCA_SYS_TOP 0                               /* (core/registry.h's: the host tests include this file alone) */
#endif
#define OP_TOP FELUCCA_SYS_TOP                          /* SYSTEM > TOP (op_top.c): the CPU's jobs and the RAM's owners, largest first */
static const struct { const char *name; uint8_t it[4]; } SYS[] = {
    {"INFO", {SI_INFO, SI_NONE, SI_NONE, SI_NONE}},      /* first: one dense read-only page, not 4 lines (op_gldraw.c gl_info_draw): the clock, the
                                                         * load of each core, the memory (the user, 2026-10-14). The second core is always on:
                                                         * no 2ND CORE line; dual.c's guard still parks it and the page says why */
#if FELUCCA_SYS_DEBUG                                  /* (the debug pages: builder item SYS_DEBUG, the user 2026-10-17) */
    {"INTERNALS", {SI_DIAG, SI_NONE, SI_NONE, SI_NONE}},   /* INFO split in two (the user, 2026-10-15): what a musician reads on INFO,
                                                         * the internals here (the last reset, flash, the second core) */
#if FELUCCA_USB_AUDIO
    {"USB STATS", {SI_UAINFO, SI_NONE, SI_NONE, SI_NONE}},   /* the USB audio glitch counters */
#endif
#if OP_DTIMES
    {"CPU TIMES", {SI_DUALT, SI_NONE, SI_NONE, SI_NONE}},   /* the dual render's times per mix block, after USB STATS (2026-10-30: sys_dt_cell) */
#endif
#if OP_BTSTATS
    {"BT STATS", {SI_BTSTAT, SI_NONE, SI_NONE, SI_NONE}},   /* the Bluetooth audio stream's counters (2026-11-01: sys_bts_cell) */
#endif
#endif
#if FELUCCA_SD_UI
    {"SD CARD", {SI_SDCARD, SI_SDVERS, SI_SDSLACK, SI_SDINDEX}},   /* (the card's state, a project's versions kept, the room
                                                         * a file's run gets past its size, the sample index again) */
#if FELUCCA_SD_STREAM
    {"SD CARD", {SI_SDPRE, SI_NONE, SI_NONE, SI_NONE}},   /* (the card samples' pre-buffer, its KB beside it) */
#endif
    {"SETTINGS", {SI_SDBACK, SI_SDREST, SI_NONE, SI_NONE}},   /* (the device's settings to the card and back) */
#endif
#if FELUCCA_BLE                                         /* (Bluetooth, a page a side, each with its item: op_bt.c) */
    {"BT MIDI", {SI_BLE, SI_BLEPEER, SI_BLEMIDI, SI_BLEST}},   /* MODE, DEVICE, ACTIVITY, STATE */
#endif
#if FELUCCA_BT_AUDIO
    {"BT AUDIO", {SI_BTA, SI_BTAPEER, SI_BTAFMT, SI_BTAST}},   /* MODE, DEVICE, FORMAT, STATE */
#endif
#if WIFI_UI
    {"WI-FI", {SI_WFMODE, SI_WFNET, SI_WFSIG, SI_WFST}},   /* the same pattern as the Bluetooth pages (the user, 2026-10-27: "it should be very similar") */
#endif
    /* the screen and the sound in one section (the user, 2026-10-25: KNOB COLORS was on a page read as "audio", "group and
     * make it logical"): KNOB COLORS (the knob caps' colours; COLOR gone 2026-10-18: the palette is MONO, panel.c),
     * SPK LOWCUT and HPHN BASS (the output's correction by the headphone jack: fx/outcorr.c); FILT RESO went to GLOBAL's TUNE page
     * (the user, 2026-10-29: it is a project's, not the device's), so USB SERIAL joins this page (GL_JOIN) when it fits. DEBUG STATS is the last row, after HPHN BASS, where the platform keeps them
     * (DBG_STATS: the release; it had a page alone, SYSTEM > DEBUG, until 2026-10-29): General is then 4 lines, full, and USB SERIAL
     * keeps its own page (the join holds 4 lines at most) */
    {"GENERAL", {SI_KCOL, SI_HPBASS, DBG_STATS ? SI_DBGST : SI_NONE, SI_NONE}},
#if USBH_UI
    {"USB", {SI_UHMODE, SI_UHDEV, SI_NONE, SI_NONE}},   /* the port as a MIDI host or the computer's device (USB_HOST: the trial's
                                                         * ui/opt_usbh.h on fm1-base's; USB SERIAL below it, one section) */
#endif
#if FELUCCA_CDC
    {"USB", {SI_USB, SI_NONE, SI_NONE, SI_NONE}},       /* (USB SERIAL: on General's page when it fits, GL_JOIN) */
#endif
    {"METERS", {SI_METERS, SI_NONE, SI_NONE, SI_NONE}},   /* the load lines at the screen's foot: OFF CPUS MEMORY BOTH (core/meters.h, op_meters.c); on General's page when it fits (GL_JOIN) */
#if OP_TOP
    {"TOP", {SI_TOP, SI_NONE, SI_NONE, SI_NONE}},       /* after METERS, before USB AUDIO (the user, 2026-11-03: General then Top after Bluetooth and Wi-Fi; op_top.c) */
#endif
#if FELUCCA_USB_AUDIO
    /* where each source goes (the user, 2026-10-17: "each track, including master, can be assigned to none, 1, 2, 1+2, 3, 4,
     * 3+4", and the drum lanes; ua_tap.h): a line a source, MASTER T1 T2 T3 | DRUMS, DRUM BUS, DR1 DR2 | DR3..DR6 | DR7..DR10
     * | RET A B C (the FX returns, 2026-10-29: a row of their own, last, so the rows before keep their places); the rows one
     * section over their pages (op_global.c gl_sid: a row named as the one before it goes on with it) */
    {"USB AUDIO", {UA_IT(0), UA_IT(1), UA_IT(2), UA_IT(3)}},
    {"USB AUDIO", {UA_IT(4), UA_IT(5), UA_IT(6), UA_IT(7)}},
    {"USB AUDIO", {UA_IT(8), UA_IT(9), UA_IT(10), UA_IT(11)}},
    {"USB AUDIO", {UA_IT(12), UA_IT(13), UA_IT(14), UA_IT(15)}},
    {"USB AUDIO", {UA_RT(0), UA_RT(1), UA_RT(2), SI_NONE}},
#endif
    {"ABOUT", {SI_ABOUT, SI_BUILD, SI_REPO, SI_NONE}},
    {"CREDITS", {SI_CREDITS, SI_NONE, SI_NONE, SI_NONE}},   /* after ABOUT: one text page, the end titles (op_credits.c) */
    /* the global rows, last: GLOBAL's (GLO tapped, op_global.c), not SYSTEM's (the user, 2026-10-12: GLO "the page
     * where we have bpm, nudge, midi track settings etc"); defined here once, SYSTEM shows the rows above them */
    {"TUNE", {SI_TUNE, SI_RECQ, SI_IF(FELUCCA_TFLT_RESO && FELUCCA_TRK_FILT, SI_FRESO), SI_IF(FELUCCA_PUNCH_SW && !FELUCCA_REC_MODES, SI_PUNCH)}},   /* GLOBAL's first page (the user, 2026-11-01: the PLAY-held page
                            * has the rest): TUNE, QUANT (the record quantise, also on PLAY's REC rows), FILT RESO (SLOOP 2.8's FILTER RESO, every track's
                            * FILTER resonance, saved with the PROJECT: core.h G_TFRES) */
    {"MIDI", {SI_IF(FELUCCA_MIDI_CLOCK, SI_SYNC), SI_IF(FELUCCA_MIDI_CLKOUT, SI_CLKOUT),
              FELUCCA_LINK ? (uint8_t)SI_LINK : SI_IF(FELUCCA_MIDI_OUT, SI_OUT), FELUCCA_LINK ? SI_IF(FELUCCA_MIDI_OUT, SI_OUT) : (uint8_t)SI_NONE}},   /* (LINK, Link builds, between CLK OUT and MIDI OUT) */   /* (CLK IN, CLK OUT, MIDI OUT: no MIDI IN since 2026-10-14, the user: notes come in through the track channels,
                            * the clock through CLK IN; settings word bit 15 is free; CLKOUT: the clock sent, io/midi/clock_out.c) */
#if FELUCCA_MIDI_CLOCK
    {"MIDI", {SI_CLKOFS, SI_IF(SY_BT_OFS, SI_BTOFS), SI_IF(FELUCCA_LINK, SI_LINKOFS), SI_NONE}},   /* (following a clock: the steps that much earlier,
                            * a wired clock's / a Bluetooth (BLE) clock's: clock_sync.c sync_lead_t; the settings record) */
#endif
    {"CHANNELS", {SI_CH1, SI_CH2, SI_CH3, SI_CHD}},
};
#define NSYS (sizeof SYS / sizeof SYS[0])
#define NSYS_GLO (3u + FELUCCA_MIDI_CLOCK)              /* the global rows at SYS's end: TUNE, MIDI (and its offsets), CHANNELS */
#define NSYS_SETUP (NSYS - NSYS_GLO)                    /* SYSTEM's rows: the setup (SCREEN, AUDIO, BLUETOOTH ...) */
static uint8_t sys_dirty;                               /* a setting changed: saved when SYSTEM or GLOBAL is left */
static uint32_t sys_rows(void)
{
#if FELUCCA_BT_UI
    if (BDV_OPEN)
        return dev_rows();                              /* (the BLE devices list: its rows) */
#endif
    return NSYS_SETUP;
}
static void sys_name(uint32_t r, char *b)
{
#if FELUCCA_BT_UI
    if (BDV_OPEN) {
        str_cpy(b, "DEVICES", 12);                      /* (the list's header: "Devices 2/4") */
        return;
    }
#endif
    str_cpy(b, SYS[r % NSYS].name, 12);
}
static uint32_t sys_item(uint32_t r, uint32_t k) { return SYS[r % NSYS].it[k & 3u]; }

/* the settings with a descriptor (params.c GP, bp_set.c): the MIDI rows */
static const param_desc_t *sys_desc(uint32_t it, int16_t **vp)
{
    static const uint8_t BPS[] = {BPS_MOUT, 0, BPS_CH0, BPS_CH1, BPS_CH2, BPS_CHD};
    *vp = 0;
    if (it == SI_SYNC || it == SI_FRESO || it == SI_TUNE || it == SI_RECQ) {
        const uint32_t g = it == SI_SYNC ? G_SYNC : it == SI_FRESO ? G_TFRES : it == SI_TUNE ? G_TUNE : G_RECQ;
        *vp = &song.g[g];
        return &GP[g];
    }
#if FELUCCA_PUNCH_SW
    if (it == SI_PUNCH) {                               /* (without REC MODES the PLAY-held page has no REC 2: PUNCH is here) */
        *vp = &punch_sw;
        return &PUNCH_SW_D;
    }
#endif
    if (it >= SI_OUT && it <= SI_CHD)
        return bps_desc(BPS[it - SI_OUT], vp);
#if FELUCCA_MIDI_CLOCK
    if (it == SI_CLKOFS || it == SI_BTOFS || it == SI_LINKOFS) {   /* (clock_sync.c sync_ofs, ms) */
        uint32_t x = it == SI_BTOFS ? 1u : it == SI_LINKOFS ? 2u : 0u;
        *vp = &sync_ofs[x];
        return &SY_OFS_D[x];
    }
#endif
    return 0;
}
static int32_t sys_step(int32_t v, int32_t s, int32_t max)   /* a knob stops at the ends; YES (s 0) goes round */
{
    return s ? ui_clamp(v + (s > 0 ? 1 : -1), 0, max) : (v >= max ? 0 : v + 1);
}
#ifndef FELUCCA_COMMIT
#define FELUCCA_COMMIT ""
#endif
/* the battery, from the ADC (main.c reads ch3, the divider on PB1, smoothed into song.batt_raw, 0 = not read yet). The 3 bars are
 * SLOOP's own thresholds (ui/sloop/ui_draw.c batt_level: 531 / 561 / 591); with a USB host present the line says USB (whether that
 * shifts the reading is not measured).
 * The volts and the percentage are an ESTIMATE (the user, 2026-10-21: "convert the 600 ish value in volts"): the divider's ratio
 * and the ADC's reference are not documented here. Assumed: a 1 : 2 divider and a 3.3 V reference, 6.452 mV a count (600 = 3.87 V,
 * and SLOOP's bar thresholds then fall at 3.43 / 3.62 / 3.81 V, a Li-ion cell's own steps), the percentage the usual Li-ion curve.
 * Measure the cell at the pads against the count to correct BATT_UV_PER_COUNT. */
#define BATT_UV_PER_COUNT 6452
static uint32_t sys_batt_bars(void)
{
    return song.batt_raw >= 591 ? 3u : song.batt_raw >= 561 ? 2u : song.batt_raw >= 531 ? 1u : 0u;
}
static uint32_t sys_batt_mv(void) { return (uint32_t)song.batt_raw * BATT_UV_PER_COUNT / 1000u; }
static uint32_t sys_batt_pct(void)                      /* the cell's charge from its millivolts: the Li-ion curve, straight between its knots */
{
    static const uint16_t MV[7] = {3300, 3500, 3700, 3800, 3900, 4000, 4150};
    static const uint8_t PC[7] = {0, 8, 40, 60, 75, 88, 100};
    uint32_t mv = sys_batt_mv(), i;
    if (mv <= MV[0])
        return 0u;
    for (i = 1; i < 7u; i++)
        if (mv < MV[i])
            return PC[i - 1u] + (mv - MV[i - 1u]) * (uint32_t)(PC[i] - PC[i - 1u]) / (uint32_t)(MV[i] - MV[i - 1u]);
    return 100u;
}
/* the supply, for INFO's header (op_draw.c head_make): 0 on the cell, 1 5 V present (a charger or a USB host, and the chip's
 * charger not known to run: full, off, or an external one), 2 charging (the chip's own registers: 5 V on the supply pin, its
 * charger on and the full flag clear; main.c / fm1-base plat_battery_charging; read from the SDK's P33 map, not yet measured on
 * an FM-1). A USB host alone (usb.config) counts as 1: the registers are not needed to know power is there */
static uint32_t sys_batt_chg(void)
{
    return song.batt_chg ? song.batt_chg : usb.config && !usb.suspended ? 1u : 0u;
}
static uint16_t sys_batt_col(uint32_t pct)              /* the CPU bars' colours turned round: green, amber under 40 %, red under 20 % */
{
    return pct < 20u ? C_ERR : pct < 40u ? C_WARN : C_OK;
}
static void sys_ro(cell_t *c, const char *label)    /* a read-out line */
{
    c->label = label;
    c->kind = CK_RO;
}
/* INFO's RAM bar (the user, 2026-10-29: one memory bar, drawn as the CPU bars, its peaks as theirs): RAM, its used / total KB.
 * With an arena (core/arena.c: the SRAM the link leaves free, its whole room, and the cache RAM's: CACHE_RAM) the arena's
 * blocks in use the fill, the undo history's share (seq/undo.c: its least and what it borrows) a dimmer segment after
 * it, the rest free; the bright tick the last ~10 s's most in use, the dim one the most since boot (system/overload.h's
 * RAM ring, sampled with the CPU's).
 * Without: the SRAM in use at link of the SRAM (stk_sram_free: what is left), no ticks. The stacks: no bar, the label
 * amber and STACK LOW when one has had under STK_LOW left (system/stackmark.h stk_low: ever, the release's own record) */
#ifdef PLAT_TICKS_PER_US
#define INFO_SRAM 475136u                               /* (the release's SRAM to the stacks: fm1-base device/app.ld) */
#else
#define INFO_SRAM 466688u                               /* (Optimist's own: firmware/app.ld) */
#endif
#if PART_DROPS && FELUCCA_CACHE_RAM
#define INFO_CR fm1_cache_ram_free()                    /* (the cache RAM, its test passed: the undo history's second
                                                         * part, counted in the total and in its share; hal/fm1_cache.h) */
#else
#define INFO_CR 0u
#endif
/* the arena's fill, the one quantity of INFO's RAM bar and of the memory line over every page (op_draw.c ml_draw): the bytes in use,
 * the whole room, the undo history's share after the fill (the bar's dimmer segment) */
static void ram_fill(uint32_t *used, uint32_t *total, uint32_t *n)
{
#if PART_DROPS
    *total = ARN_BYTES + ARN_UNDO_KEEP + INFO_CR;
    *used = ARN_BYTES - arn_room();
#if FELUCCA_UNDO_HISTORY
    *n = undo_h.ready ? undo_h.size : ARN_UNDO_KEEP + INFO_CR;   /* (both its parts: the free SRAM's, the cache RAM's) */
#else
    *n = 0;
#endif
#else
    *total = INFO_SRAM;
    *used = *total > stk_sram_free() ? *total - stk_sram_free() : 0u;
    *n = 0;
#endif
}
static uint32_t info_ram_pct(void)                      /* the fill in percent, 0..100 (the memory line's) */
{
    uint32_t used, total, n;
    ram_fill(&used, &total, &n);
    return total ? (used * 100u / total < 100u ? used * 100u / total : 100u) : 0u;
}
static void info_ram(cell_t *c)
{
    uint32_t used, total, pct, n, shown;
    ram_fill(&used, &total, &n);
    sys_ro(c, "RAM");
    pct = info_ram_pct();
    cell_gauge(c, 0, 0, 100, (int32_t)pct);
    c->pkbar = 1;
    c->av = (int16_t)(total ? (used + n) * 100u / total < 100u ? (used + n) * 100u / total : 100u : 0u);
    shown = used / 1024u;
#if PART_DROPS
    {
        uint32_t k10 = ovl_win_max(OVL_RAM, fm1_ms, OVL_W10), kall = ovl.r[OVL_RAM].amax;
        c->hold[0] = (int16_t)(total ? k10 * 102400u / total : 0u);
        c->hold[1] = (int16_t)(total ? kall * 102400u / total : 0u);
    }
#endif
    fmt_int(c->val, (int32_t)shown);
    n = str_len(c->val);
    c->val[n] = '/';
    fmt_int(c->val + n + 1u, (int32_t)(total / 1024u));
    c->unit = "KB";
    if (stk_low()) {                                    /* (a stack within STK_LOW of its bottom, ever: stackmark.h) */
        c->col = C_WARN;
        c->danger = 1;                                  /* (its label amber too: op_gldraw.c gl_info_draw) */
        c->unit = "KB STACK LOW";
    }
}
static void sys_onoff(cell_t *c, const char *label, uint32_t on)   /* an ON / OFF line */
{
    cell_list(c, label, on ? "ON" : "OFF", 1, on != 0u);
}
#if FELUCCA_MIDI_CLKOUT
/* CLK OUT's list, in the order shown: OFF USB BLE BOTH (without BLE: OFF USB); the values are clock_out.c CLKO_* */
#if FELUCCA_BLE
static const uint8_t CKO_ORD[] = {CLKO_OFF, CLKO_USB, CLKO_BLE, CLKO_BOTH};
#else
static const uint8_t CKO_ORD[] = {CLKO_OFF, CLKO_USB};
#endif
#define CKO_N ((int32_t)(sizeof CKO_ORD / sizeof CKO_ORD[0]))
static int32_t cko_pos(void)
{
    int32_t i;
    for (i = 0; i < CKO_N; i++)
        if (CKO_ORD[i] == clko_on)
            return i;
    return clko_on ? 1 : 0;                              /* (BOTH without BLE: USB) */
}
#endif
static void sys_it_cell(uint32_t it, cell_t *c)     /* the cell of setting it */
{
    static const char *const HB[3] = {"OFF", "+3 dB", "+6 dB"};
    int16_t *vp;
    const param_desc_t *d;
    d = sys_desc(it, &vp);
    if (d) {
        cell_param(c, d, vp);
        if (it == SI_SYNC)
            c->label = "CLK IN";                        /* (the page reads as a set: CLK IN, CLK OUT, MIDI OUT; TEMPO's card says SYNC) */
        else if (it == SI_OUT)
            c->label = "MIDI OUT";
        return;
    }
    cell_clear(c);
    c->kind = CK_VAL;
#if FELUCCA_USB_AUDIO
    if (si_ua(it)) {                                    /* a source's channels: text only, no bar (the list walks it) */
        c->label = ua_src_name(it - SI_UAS0);
        cell_val(c, ua_dst_name(ua_dst[it - SI_UAS0]));
        return;
    }
#endif
    switch (it) {
    case SI_HPBASS:
        cell_list(c, "HPHN BASS", HB[hp_bass % 3u], 2, (int32_t)(hp_bass % 3u));     /* (headphones in the jack) */
        break;
#if FELUCCA_CDC
    case SI_USB:
        sys_onoff(c, "SERIAL", usb_serial);
        c->col = usb_serial == usb_cdc_on ? 0 : C_WARN;  /* (amber: a restart applies it) */
        break;
#endif
#if FELUCCA_BT_UI
    case SI_BLE:
        bt_mode_cell(BT_MIDI, c);
        break;
#if FELUCCA_BT_AUDIO
    case SI_BTA:
        bt_mode_cell(BT_AUD, c);
        break;
    case SI_BTAST:
        bta_state_cell(c);
        break;
    case SI_BTAPEER:
        bta_peer_cell(c);
        break;
    case SI_BTAFMT:
        bta_fmt_cell(c);
        break;
#endif
    case SI_BLEST:
        ble_state_cell(c);
        break;
    case SI_BLEPEER:
        ble_peer_cell(c);
        break;
    case SI_BLEMIDI:
        ble_midi_cell(c);
        break;
    case SI_BLELIST:
        cell_act(c, 0, "DEVICES", "SCAN", 0);         /* (YES: the list, the scan while it is open) */
        break;
#if BLE_CENTRAL
    case SI_BLEAUTO:
        ble_auto_cell(c);
        break;
    case SI_BLEFGT:                                     /* FORGET while there is a LAST, else NONE, a read-out */
        if (ble_store_has_last(&ble_store))
            cell_act(c, 0, "LAST DEVICE", "FORGET", 1);
        else
            c->kind = CK_RO, c->label = "LAST DEVICE", cell_val(c, "NONE");
        break;
#endif
#endif
#if WIFI_UI
    case SI_WFMODE:
    case SI_WFNET:
    case SI_WFSIG:
    case SI_WFST:
        wf_cell(it, c);
        break;
#endif
#if USBH_UI
    case SI_UHMODE:                                     /* the port: the computer's device, or the host of a USB-MIDI device:
                                                         * then nothing for the computer (no USB MIDI, audio, editor) */
        c->label = "MODE";
        cell_val(c, usbh_mode() ? "HOST, NO PC" : "DEVICE");
        c->big = !usbh_mode();
        c->col = usbh_mode() ? C_WARN : 0;
        break;
    case SI_UHDEV: {                                    /* what is plugged in: its name, CONNECTED, NONE, NO MIDI, LOW SPEED */
        uint32_t ok;
        c->kind = CK_RO;
        c->label = "DEVICE";
        cell_val(c, usbh_device(&ok));
        c->big = str_len(c->val) <= 9u;
        c->col = ok ? C_OK : str_eq(c->val, "NO MIDI") || str_eq(c->val, "LOW SPEED") ? C_WARN : 0;
        break;
    }
#endif
    case SI_CPU: {                                      /* (DUAL: its wait for CPU1 counts in it); (over 100: late) */
        int32_t now = (int32_t)(song.cpu_q8 * 100u / 256u), pct = now;
        const char *nm = DUAL_LOAD ? "CPU0" : FELUCCA_SYS_DEBUG ? "LOAD" : "CPU LOAD";   /* (DUAL: CPU0 beside CPU1, on the
                                                         * player's INFO too: the user, 2026-10-18) */
        c->label = nm;
        c->kind = CK_RO;
        fmt_int(c->val, pct);
        c->col = load_col(pct);
        c->unit = "%";
        info_bar(c, OVL_CPU0, now);
        break;
    }
#if PART_DROPS
    case SI_ARN: {                                      /* the arena's moves (core/arena.c arn_probe): the longest, start to
                                                         * swap, and the longest interrupts-off window of one (us; 0/0: none) */
        uint32_t n;
        sys_ro(c, "ARENA MOVE");
        fmt_int(c->val, (int32_t)(arn_probe.max_us > 99999u ? 99999u : arn_probe.max_us));
        n = str_len(c->val);
        c->val[n] = '/';
        fmt_int(c->val + n + 1u, (int32_t)(arn_probe.off_us > 9999u ? 9999u : arn_probe.off_us));
        c->unit = "us";
        break;
    }
#endif
    case SI_RAM:
        info_ram(c);
        break;
#if FMEM_ANY
    case SI_RAMUSE: {                                   /* where the arena's RAM is: "P61 R18 F22" KB (parts and effects, the
                                                         * features' blocks: the radios', free), the memory line's split */
        uint32_t fr = arn_room(), fe = fmem_held(), pa = ARN_BYTES - fr > fe ? ARN_BYTES - fr - fe : 0u, n;
        sys_ro(c, "RAM USE");
        c->val[0] = 'P';
        fmt_int(c->val + 1, (int32_t)(pa / 1024u));
        n = str_len(c->val);
        c->val[n++] = ' ', c->val[n++] = 'R';
        fmt_int(c->val + n, (int32_t)fmem_kb(fe));
        n = str_len(c->val);
        c->val[n++] = ' ', c->val[n++] = 'F';
        fmt_int(c->val + n, (int32_t)(fr / 1024u));
        c->unit = "KB";
        break;
    }
#endif
#if FELUCCA_SYS_DEBUG
    case SI_DROPS: {                                    /* late halves (gaps in the sound) since boot or the last clear */
        uint32_t n = ovl_drops();
        sys_ro(c, "DROPS");
        fmt_int(c->val, (int32_t)n);
        c->col = !n ? C_OK : ovl_recent(fm1_ms, DROPS_RED_MS) ? C_ERR : C_WARN;
        c->unit = "";
        cell_gauge(c, 0, 0, 1, n != 0u);
        break;
    }
#endif
    case SI_XRUN: {                                     /* UNDERRUNS: USB audio's and MIDI in's losses, the total and where; without
                                                         * the debug pages the drop-outs too (no DROPS line then) */
        static char u[16];
        uint32_t nu = info_ua_xruns(), mi = ovl_midi_lost(), dr = FELUCCA_SYS_DEBUG ? 0u : ovl_drops(), n = nu + mi - ovl.xrun0 + dr;
        sys_ro(c, "UNDERRUNS");
        fmt_int(c->val, (int32_t)n);
        u[0] = 0;
        if (n && dr && ovl_recent(fm1_ms, DROPS_RED_MS)) {   /* (a drop-out just now: red, as the header's mark) */
            c->unit = "";
            c->col = C_ERR;
            cell_gauge(c, 0, 0, 1, 1);
            break;
        }
        if (n) {                                        /* (where, since boot: "USB3 MIDI1") */
            if (nu) {
                str_cpy(u, " USB", sizeof u);
                fmt_app(u, (int32_t)(nu < 9999u ? nu : 9999u));
            }
            if (mi) {
                str_app(u, " MIDI", sizeof u);
                fmt_app(u, (int32_t)(mi < 9999u ? mi : 9999u));
            }
        }
        c->unit = u;
        c->col = !n ? C_OK : fm1_ms - ovl.xrun_ms < DROPS_RED_MS ? C_ERR : C_WARN;
        cell_gauge(c, 0, 0, 1, n != 0u);
        break;
    }
#if FELUCCA_CPU_GUARD && FELUCCA_SYS_DEBUG
    case SI_GUARD: {                                    /* what the CPU guard eases now, and the voices it shed (cpuguard.c) */
        uint32_t n = cg.sheds - info_shed0, l = cg.level <= CG_TOP ? cg.level : CG_TOP;
        sys_ro(c, "GUARD");
        cell_val(c, CG_NAME[l]);
        if (cg.vcap) {                                  /* (reacting from NOTES on: the voices capped) */
            str_app(c->val, " MAX", sizeof c->val);
            fmt_app(c->val, cg.vcap);
        }
        if (n && str_len(c->val) + 5u < sizeof c->val) {
            str_app(c->val, " -", sizeof c->val);
            fmt_app(c->val, (int32_t)(n < 999u ? n : 999u));
        }
        c->unit = n ? "V" : "";
        c->col = !l ? C_OK : l < CG_L_NOTES ? C_WARN : C_ERR;   /* (voices shed or the reverb off: red) */
        cell_gauge(c, 0, 0, CG_TOP, (int32_t)l);
        break;
    }
#endif
#if FELUCCA_SYS_DEBUG || DUAL_LOAD
    case SI_CPU1: {                                     /* the second core: ON and its share of each audio half (dual_load.h), OFF and why, or not in the build */
#if DUAL_LOAD
        int32_t pct = dual_c1_pct();
        c->kind = CK_RO;
        if (pct < 0) {                                  /* (down: CPU0 renders everything, dual.c; why it is down, else NO REASON) */
            c->label = "2ND CORE OFF";
            cell_val(c, dual.why ? dual_why_name(dual.why) : "NO REASON");
            c->unit = "";
            c->col = C_WARN;
            cell_gauge(c, 0, 0, 100, 0);
        } else {
            int32_t now = pct;
            c->label = FELUCCA_SYS_DEBUG ? "2ND CORE ON" : "CPU1";
            fmt_int(c->val, pct);
            c->unit = "%";
            c->col = load_col(pct);
            info_bar(c, OVL_CPU1, now);
        }
#else
        sys_ro(c, "2ND CORE");
        cell_val(c, "NOT IN BUILD");
        c->unit = "";
        c->col = C_DIM;
        cell_gauge(c, 0, 0, 100, 0);
#endif
        break;
    }
#endif
    case SI_INFO:                                       /* the info page's one line (op_gldraw.c draws the page itself) */
        sys_ro(c, "INFO");
        break;
    case SI_DIAG:                                       /* the INTERNALS page's one line (op_gldraw.c draws the page itself) */
        sys_ro(c, "INTERNALS");
        break;
    case SI_UAINFO:                                     /* the USB STATS page's one line */
        sys_ro(c, "USB STATS");
        break;
    case SI_DUALT:                                      /* the CPU TIMES page's one line (op_gldraw.c draws the page itself) */
        sys_ro(c, "CPU TIMES");
        break;
    case SI_BTSTAT:                                     /* the BT STATS page's one line (op_gldraw.c draws the page itself) */
        sys_ro(c, "BT STATS");
    case SI_TOP:                                        /* the TOP page's one line (op_gldraw.c draws the page, op_top.c its lines) */
        sys_ro(c, "TOP");
        break;
    case SI_CREDITS:                                    /* the credits page's one line (op_credits.c gl_cred_draw draws the page) */
        sys_ro(c, "CREDITS");
        break;
    case SI_BUILD:                                      /* the build's date and time (the compiler's) */
        sys_ro(c, "BUILD");
        cell_val(c, __DATE__);
        c->unit = __TIME__;
        break;
    case SI_REPO:                                       /* the source, as text: the link right-aligned in the unit */
        c->label = " ";                                 /* (blank: the link takes the line's width, the label must not be empty for the help) */
        c->kind = CK_RO;
        c->unit = "github.com/w0ts/optimist";
        break;
    case SI_KCOL:                                       /* the knob caps' colours on dials, labels, cards (core/knobcol.h) */
        sys_onoff(c, "KNOB COLORS", knob_colors);       /* (the settings list has the room: op_gldraw.c) */
        break;
    case SI_DBGST:                                      /* the platform's stats in flash every 5 minutes (core/dbgstats.h) */
        sys_onoff(c, "DEBUG STATS", dbg_stats);
        break;
    case SI_METERS: {                                   /* thin load lines on the screen's last rows (op_meters.c; kept in the settings record, rsv_pj[2]) */
        static const char *const MN[MTR_N] = {"OFF", "CPUS", "MEMORY", "BOTH"};
        cell_list(c, "METERS", MN[meters_mode % MTR_N], MTR_N - 1, meters_mode);
        break;
    }
#if FELUCCA_MIDI_CLKOUT
    case SI_CLKOUT:                                     /* MIDI clock out (io/midi/clock_out.c) */
        {
            static const char *const CKN[4] = {"OFF", "BOTH", "USB", "BLE"};   /* (by CLKO_*) */
            cell_list(c, "CLK OUT", CKN[clko_on & 3u], CKO_N - 1, cko_pos());
        }
        c->col = clko_blocked() ? C_WARN : 0;            /* (amber: on, but the port chosen is the clock followed, or no BLE central listens: nothing sent) */
        break;
#endif
#if FELUCCA_LINK
    case SI_LINK: {
        static const char *const LKN[3] = {"OFF", "ON", "ON+S/S"};   /* (by LINK_*) */
        cell_list(c, "LINK", link_eff() != link_mode ? "ON (JAM)" : LKN[link_mode % 3u], LINK_ONSS, link_mode);   /* (MODE JAM forces ON: said, the stored value kept) */
        break;
    }
#endif
#if FELUCCA_SD_UI
    case SI_SDCARD: {                                   /* the card: READY and its size, else what is wrong (YES: mounted again) */
        static const char *const ST[4] = {"NONE", "NOT FAT", "PREPARING", "READY"};
        static char sz[8];
        uint32_t st = sdm_state(), mb = sd_card_mb();
        sys_ro(c, "CARD");
        cell_val(c, ST[st & 3u]);
        c->col = st == 3u ? C_OK : C_WARN;
        if (st >= 2u) {                                 /* (its capacity: the CSD's) */
            fmt_int(sz, (int32_t)(mb >= 1024u ? mb >> 10 : mb));
            str_app(sz, mb >= 1024u ? " GB" : " MB", sizeof sz);
            c->unit = sz;
        }
        break;
    }
    case SI_SDVERS:
        c->label = "VERSIONS";
        fmt_int(c->val, (int32_t)sdm_keep());
        cell_gauge(c, 0, 0, 16, (int32_t)sdm_keep());
        break;
    case SI_SDSLACK:
        c->label = "SLACK";
        fmt_int(c->val, (int32_t)sdm_scale());
        c->unit = "%";
        cell_gauge(c, 0, 0, 400, (int32_t)sdm_scale());
        break;
    case SI_SDINDEX:
        cell_act(c, 0, "SAMPLES", "INDEX", 0);
        break;
    case SI_SDBACK:
        cell_act(c, 0, "BACKUP", "TO CARD", 0);
        break;
    case SI_SDREST:
        cell_act(c, 0, "RESTORE", "FROM CARD", 1);
        break;
    case SI_SDPRE: {                                    /* each card sample's head in RAM, ms, and what the project's take */
        static char kb[10];
        uint32_t ms = sdm_pre_ms();
        c->label = "PRE-BUFFER";
        fmt_int(c->val, (int32_t)ms);
        fmt_int(kb, (int32_t)sdm_pre_kb());
        str_app(kb, " KB", sizeof kb);
        c->unit = kb;
        cell_gauge(c, 1, 0, 3, ms <= 25u ? 0 : ms <= 50u ? 1 : ms <= 100u ? 2 : 3);
        break;
    }
#endif
    case SI_ABOUT: {                                    /* a read-out: the version itself (YES still says it whole) */
        const char *v = FELUCCA_VERSION;
        sys_ro(c, "VERSION");
        cell_val(c, v + (str_len(v) > 9u && !memcmp(v, "OPTIMIST ", 9) ? 9u : 0u));  /* "OPTIMIST 0.1": 0.1 */
        c->unit = FELUCCA_COMMIT;                       /* (the commit it was built from, "" without) */
        break;
    }
    default:
        c->kind = CK_NONE;
        break;
    }
}
static void sys_cell(uint32_t r, uint32_t k, cell_t *c)
{
#if FELUCCA_BT_UI
    if (BDV_OPEN) {
        dev_cell(r, k, c);
        return;
    }
#endif
    sys_it_cell(sys_item(r, k), c);
}
static uint32_t fm1_irqoff_max;                         /* (hal/fm1_flash.h's counter, included after the UI in felucca.c: a tentative definition, the same object;
                                                         * bits 0-27 the ticks, 28-29 the operation: 1 read, 2 write, 3 erase) */
#if FELUCCA_SYS_DEBUG                                   /* (the debug pages' read-outs, to INFO_RST's end: builder item SYS_DEBUG) */
static void info_num(cell_t *c, uint32_t v, const char *unit)   /* a read-out's value: v (capped at 9999999) and its unit */
{
    fmt_int(c->val, (int32_t)(v > 9999999u ? 9999999u : v));
    c->unit = unit;
}
#if FELUCCA_USB_AUDIO
/* SYSTEM's USB STATS page (the glitch counters, 2026-10-14; plain words since 2026-10-15, the user: "easier-to-read text"):
 * counters since boot, read only. Playback underruns / overruns, the USB frames missed, bad packets, the packets received, the
 * rate the FM-1 asks of the computer (its feedback, Hz), the ring's lowest fill (frames of 1024, target 512), the longest gap
 * between two USB polls (us) and how many were over 0.9 ms */
#define INFO_UA 11u
static void sys_ua_cell(uint32_t i, cell_t *c)
{
    static const char *const L[INFO_UA] = {"USB UNDERRUNS", "USB OVERRUNS", "MISSED FRAMES", "BAD PACKETS", "PACKETS IN",
                                           "RATE ASKED", "LOWEST BUFFER", "LONGEST GAP", "GAPS > 0.9 MS",
                                           "REC UNDERRUNS", "REC OVERRUNS"};   /* (the recording's: INFO's UNDERRUNS counts them too) */
    static const uint8_t WARN[INFO_UA] = {1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1};   /* (a counter of trouble: amber once not 0) */
    uint32_t v;
    cell_clear(c);
    c->kind = CK_RO;
    c->label = L[i % INFO_UA];
    switch (i % INFO_UA) {
    case 0: v = ua.play_underruns; info_num(c, v, ""); break;
    case 1: v = ua.play_overruns; info_num(c, v, ""); break;
    case 2: v = ua.missed_frames; info_num(c, v, ""); break;
    case 3: v = ua.bad_packets; info_num(c, v, ""); break;
    case 4: v = ua.rx_packets; info_num(c, v, ""); break;
    case 5: v = (ua_feedback() * 1000u) >> 14; info_num(c, v, " Hz"); break;
    case 6:
        v = ua.fill_lo1;
        if (v)
            info_num(c, v - 1u, " frames");
        else
            cell_val(c, "NONE YET");
        break;
    case 7: v = ua.gap_max / 24u; info_num(c, v, " us"); break;
    case 9: v = ua.cap_underruns; info_num(c, v, ""); break;
    case 10: v = ua.cap_overruns; info_num(c, v, ""); break;
    default: v = ua.gap_n; info_num(c, v, ""); break;
    }
    c->col = WARN[i % INFO_UA] && v ? C_WARN : 0;
}
#else
#define INFO_UA 0u
#endif
#if OP_BTSTATS
/* SYSTEM's BT STATS page (2026-11-01, the user: short drops on the AirPods, the link up, DROPS 0): the Bluetooth audio
 * stream since the link came up (fm1-base's plat_bta_stats, bt/br/bt_stat.h), each line one place the sound can be late.
 * DROPPED the queue full; QUEUE MAX/NOW (packets of 8.7 ms); WAIT MAX the longest a packet waited for its acknowledgment
 * (and when), then the device's answers good / bad and the BLE interrupts during it; SLOW/BLE packets over 20 ms, of them
 * with BLE meanwhile; WAIT RX AND/OR the status bits all / any of the device's answers had during that wait, LINK RX the
 * same over the link (a bit in all of the wait's and not all of the link's: its stop or NAK, rxstat bits not documented);
 * L2CAP STOPS its payloads that said stop; SINK DRAIN the most the device's buffer lost (and when); FEED GAP the longest
 * between two packets made (the render). PACKETS/S, AIR (mean slots), AFH (channels, switches) and BLE (its interval)
 * gave way to the RX lines (btdrop-2: flash; btdrop-1 on the user's FM-1 showed them normal at the drop). A table on
 * plat_bta_stats' fields (all uint32_t): up to three numbers a line "a/b/c" (decimal, or hex where BT_H), its unit, the time since ("12s ago"); amber (DROPPED red) from a line's first number at its BW */
#define INFO_BTN 10u
#define BT_F(m) ((uint8_t)(__builtin_offsetof(struct plat_bta_stats, m) / 4u))
#define BT_H 0x40u                                      /* (n's flag: the numbers in hex, 4 digits: status words) */
static const char *const BTU[] = {"", " pk", " ms"};   /* (the units: BTR's u) */
static const struct {
    const char *l;
    uint8_t n, f[3], ago, bw, u;                        /* (bw: amber from this; 0 never) */
} BTR[INFO_BTN] = {
    {"DROPPED", 1, {BT_F(dropped)}, 0, 1, 0},
    {"QUEUE MAX/NOW", 2, {BT_F(qmax), BT_F(qnow)}, 0, 11, 1},
    {"WAIT MAX", 1, {BT_F(wait_ms)}, BT_F(wait_ago), 50, 2},
    {"GOOD/BAD/BLE", 3, {BT_F(wait_good), BT_F(wait_bad), BT_F(wait_ble)}, 0, 0, 0},
    {"SLOW/BLE", 2, {BT_F(slow), BT_F(slow_ble)}, 0, 1, 0},
    {"WAIT RX AND/OR", 2 | BT_H, {BT_F(wait_and), BT_F(wait_or)}, 0, 0, 0},
    {"LINK RX AND/OR", 2 | BT_H, {BT_F(link_and), BT_F(link_or)}, 0, 0, 0},
    {"L2CAP STOPS", 1, {BT_F(flow0)}, 0, 1, 0},
    {"SINK DRAIN", 1, {BT_F(drain_ms)}, BT_F(drain_ago), 100, 2},
    {"FEED GAP", 1, {BT_F(feed_gap_ms)}, 0, 21, 2},
};
_Static_assert(sizeof(struct plat_bta_stats) % 4u == 0u && sizeof(struct plat_bta_stats) / 4u < 64u, "BTR: fields as uint32_t");
static void sys_bts_cell(uint32_t i, cell_t *c)
{
    static struct plat_bta_stats s;
    static int on;
    static char u[16];
    const uint32_t *w = (const uint32_t *)(const void *)&s;
    char *v = c->val;
    uint32_t k, x, n;
    i %= INFO_BTN;
    if (!i)
        on = plat_bta_stats(&s);                        /* (a copy a frame, at its first line) */
    cell_clear(c);
    c->kind = CK_RO;
    c->label = BTR[i].l;
    v[0] = u[0] = 0;
    c->unit = u;
    if (!on) {
        dual_cat(v, i ? "-" : "NO LINK");
        return;
    }
    n = BTR[i].n;
    for (k = 0; k < (n & 3u); k++) {
        x = w[BTR[i].f[k]];
        if (k)
            dual_cat(v, "/");
        if (n & BT_H)
            dual_chex(v, x, 4);
        else
            dual_cnum(v, x);
    }
    dual_cat(u, BTU[BTR[i].u % 3u]);
    x = BTR[i].ago ? w[BTR[i].ago] : 0u;
    if (x) {                                            /* (" 12s ago") */
        dual_cat(u, " ");
        dual_cnum(u, x / 1000u);
        dual_cat(u, "s ago");
    }
    x = BTR[i].bw;
    c->col = x && w[BTR[i].f[0]] >= x ? (i == 0u ? C_ERR : C_WARN) : 0;
}
#undef BT_F
#undef BT_H
#else
#define INFO_BTN 0u
#endif
#if defined(FELUCCA_DUAL) && FELUCCA_DUAL >= 2
/* the second core's lines on SYSTEM's INTERNALS page (2026-10-14/15: TIMEOUT on the FM-1, cause not known; dual.c, dual_mode.h;
 * plain words since 2026-10-15):
 *   CPU1 JOBS DONE   jobs joined since boot
 *   CPU1 RE-WAKES    wakes sent again for a job CPU1 had not picked up
 *   CPU1 LAST STOP   NEVER, or when it was put down (since CPU1 was started: ms under 10 s, else s) and where: IDLE (no job
 *                    posted: a boot check, a flash operation), NOT TAKEN (the job never picked up), IN JOB n (1..4 a part, 5 the
 *                    drums, 6 the reverb; 0 none begun); unit: the job's index ("job 0": the first)
 *   CPU1 GOT TO      how far CPU1 got: NONE, MAIN, HELLO, SLEEP / SPIN (its wait loop), WOKEN (its wake handler ran),
 *                    TOOK JOB (its first job), JOB DONE
 *   CPU1 HEARTBEAT   its beat (returns from `idle`, or spin passes / 256); unit: its wake handler's entries
 *   CPU1 FAULT       an exception about CPU1 (hal/fm1_dual.h fm1_dual_exc_c): "D<DBG_MSG>" or "E<EMU_MSG>", unit the core that
 *                    took it and its pc; none: NONE, unit CPU1's icfg as it slept and, once down, P1 / P0 (its wake pending) */
#define INFO_C1 6u
static void sys_c1_cell(uint32_t i, cell_t *c)
{
    static char u[16];
    char *v = c->val;
    cell_clear(c);
    c->kind = CK_RO;
    v[0] = u[0] = 0;
    c->unit = u;
    switch (i) {
    case 0:                                             /* jobs CPU1 finished */
        c->label = "CPU1 JOBS DONE";
        info_num(c, dual.jobs, "");
        break;
    case 1:                                             /* wakes sent again for a job CPU1 had not picked up */
        c->label = "CPU1 RE-WAKES";
        info_num(c, dual.rewakes, "");
        c->col = dual.rewakes ? C_WARN : 0;
        break;
    case 2: {                                           /* when it was put down and where: NEVER | <t> IDLE | <t> NOT TAKEN | <t> IN JOB n */
        const dual_stop_t *st = &dual.stop;
        c->label = "CPU1 LAST STOP";
        if (!st->stopped) {
            dual_cat(v, "NEVER");
            break;
        }
        dual_cnum(v, st->c1_ms < 10000u ? st->c1_ms : st->c1_ms / 1000u);
        dual_cat(v, st->c1_ms < 10000u ? "ms " : "s ");
        dual_cat(v, !st->in_job ? "IDLE" : !st->took ? "NOT TAKEN" : "IN JOB ");
        if (st->in_job && st->took)
            dual_cnum(v, st->at);
        if (st->in_job) {
            dual_cat(u, " job ");
            dual_cnum(u, st->job);
        }
        c->col = C_WARN;
        break;
    }
    case 3:                                             /* how far CPU1 got: MAIN, HELLO, SLEEP / SPIN, WOKEN, TOOK JOB, JOB DONE */
        c->label = "CPU1 GOT TO";
        str_cpy(v, dual_stage_name(fm1_dual_mb.stage, DUAL_SLEEPS_NOW), sizeof c->val);
        c->col = fm1_dual_mb.stage < C1S_DONE ? C_WARN : 0;
        break;
    case 4:                                             /* its beat: returns from idle (or spin passes / 256), its wake handler's entries */
        c->label = "CPU1 HEARTBEAT";
        dual_cnum(v, fm1_dual_mb.count);
        dual_cat(u, " wakes ");
        dual_cnum(u, fm1_dual_mb.wakes);
        break;
    default:                                            /* an exception about CPU1 (hal/fm1_dual.h fm1_dual_exc_c): NONE, or its code and pc */
        c->label = "CPU1 FAULT";
        if (fm1_dual_exc.count) {
            dual_cat(v, fm1_dual_exc.dbg ? "D" : "E");
            dual_chex(v, fm1_dual_exc.dbg ? fm1_dual_exc.dbg : fm1_dual_exc.emu, 8);
            dual_cat(u, fm1_dual_exc.cpu ? " cpu1 @" : " cpu0 @");
            dual_chex(u, fm1_dual_exc.pc, 7);
            c->col = C_WARN;
        } else {
            dual_cat(v, "NONE");
            dual_cat(u, " icfg ");
            dual_chex(u, fm1_dual_mb.icfg, 4);
            if (dual.stop.stopped)
                dual_cat(u, dual.down_pend ? " P1" : " P0");
        }
        break;
    }
}
#else
#define INFO_C1 0u
#endif
#if OP_DTIMES
/* SYSTEM > CPU TIMES (2026-10-30, the user's project: CPU0 over 100 % at its peaks while CPU1 sits at 25 %; system/
 * dual_load.h dual_tm): a table, a row a quantity, in % of a 32-frame mix block (726 us). Its columns, in the monospace
 * face, fixed width so that they line up under the first row and never touch the name: AVG the average over ~8 blocks,
 * MAX the peak of the last 2 s, WST its value in the worst block (CPU0's longest) since the page was opened, C the core:
 *   T1 T2 T3 DR REV  each job's render time (the parts, the drums with their bus, the reverb's kept block; BT: the
 *   BT               Bluetooth stream's SBC frame, a block in four while it streams, bt/bt_sbc_job.h); C: the core
 *                    that ran it in the last block (0 / 1, - not run); WST "-": not run in that block
 *   PRE              CPU0 alone before the fork: the events, the macros, the duck, the LFOs' draws, the split
 *   EVT, NOTE        PRE's parts (2026-11-01): EVT the sequencer's events (events_block), NOTE the note starts in them
 *                    (trk_note_on: the parts' notes, the drum hits); PRE less EVT is the rest of PRE
 *   JOBS             CPU0's own jobs
 *   WAIT             CPU0 idle at the join, waiting for CPU1
 *   POST             after the join: the merge, the USB input, mix_finish (the buses, the master chain), the macros' post
 *   CPU0 / CPU1      each core's whole block (CPU0: PRE + JOBS + WAIT + POST; CPU1: its jobs): its room is 100 less it
 *   SBC              (Bluetooth audio's frame as a job, DT_SBC; 2026-11-01) where the frames went since the page opened, in
 *                    % of them: AVG early on CPU1 (handed over before the events), MAX at the split on CPU1, WST on CPU0
 *                    (at the split, or at the platform's feed after the render); C the frames' count, its last digit
 * A job row's C is the core of the last block it ran in ("-": not yet; a frame runs in 2 blocks of 8) */
#ifdef DUAL_JOBS
#define DT_NJ DUAL_JOBS                                 /* the jobs' rows: the parts, DR, REV, BT (the SBC frame; dual.c) */
#define DT_SBC DUAL_JOB_SBC
#else
#define DT_NJ (NPART + 2u)                              /* (a host test without dual.c: the parts, DR, REV) */
#define DT_SBC 0u
#endif
#define INFO_DTN (1u + DT_NJ + 8u + (DT_SBC ? 1u : 0u))   /* the names' row, the jobs, PRE EVT NOTE JOBS WAIT POST CPU0 CPU1,
                                                                * SBC */
_Static_assert(DUAL_TM_BLK == (uint32_t)CTL * 1000000u * FM1_TICKS_PER_US / FS, "dual_load.h DUAL_TM_BLK: a mix block (CTL frames) in ticks");
_Static_assert(DT_NJ <= DUAL_TM_NJ, "dual_load.h DUAL_TM_NJ: the parts, the drums, the reverb, the SBC frame");
#define DT_COLW 3u                                      /* a column's digits: 0..999 (held there) */
static void dt_col(char *v, int32_t pct)                /* " nnn" (right-aligned in DT_COLW), "-" for pct < 0 */
{
    char t[4] = "-";
    uint32_t n, k;
    if (pct >= 0) {
        t[0] = 0;
        dual_cnum(t, (uint32_t)(pct < 999 ? pct : 999));
    }
    n = str_len(v);
    if (n)
        v[n++] = ' ';
    for (k = str_len(t); k < DT_COLW; k++)
        v[n++] = ' ';
    v[n] = 0;
    dual_cat(v, t);
}
static void sys_dt_cell(uint32_t i, cell_t *c)
{
    static const char *const NM[] = {"PRE", "EVT", "NOTE", "JOBS", "WAIT", "POST", "CPU0", "CPU1"};
    static const char *const JN[] = {"T1", "T2", "T3", "T4"};
    static uint32_t seen;
    uint32_t q, j = 0, ran = dual_tm.ran, wran = dual_tm.w_ran, jobrow = i >= 1u && i < 1u + DT_NJ;
    char *v = c->val, core;
    cell_clear(c);
    c->kind = CK_RO;
    v[0] = 0;
    c->unit = "";
    if (fm1_ms - seen > 1000u)
        dual_tm.wreset = 1;                             /* (the page opened: its worst block from now) */
    seen = fm1_ms;
    if (!i) {                                           /* the columns' names, in the columns' places */
        c->label = "% OF A BLOCK";
        dual_cat(v, "AVG MAX WST C");
        return;
    }
    if (DT_SBC && i == INFO_DTN - 1u) {                 /* SBC: where the frames went since the page opened */
        static uint32_t base[4], sseen;
        uint32_t now[4], k, tot = 0;
        now[0] = dual_tm.sbc_n[0], now[1] = dual_tm.sbc_n[1], now[2] = dual_tm.sbc_n[2], now[3] = DUAL_TM_SBC_FEED();
        if (fm1_ms - sseen > 1000u)                     /* (the page just opened: from now) */
            for (k = 0; k < 4u; k++)
                base[k] = now[k];
        sseen = fm1_ms;
        for (k = 0; k < 4u; k++)
            now[k] -= base[k], tot += now[k];
        c->label = "SBC";
        dt_col(v, tot ? (int32_t)(now[0] * 100u / tot) : -1);
        dt_col(v, tot ? (int32_t)(now[1] * 100u / tot) : -1);
        dt_col(v, tot ? (int32_t)((now[2] + now[3]) * 100u / tot) : -1);
        j = str_len(v);
        v[j] = ' ', v[j + 1u] = (char)('0' + tot % 10u), v[j + 2u] = 0;
        return;
    }
    if (jobrow) {
        j = i - 1u;
        q = DTQ_J0 + j;
        c->label = j < NPART ? JN[j % 4u] : j == NPART ? "DR" : j < DT_NJ - 1u || !DT_SBC ? "REV" : "BT";
        core = dual_tm.lastc[j] ? (char)('0' + dual_tm.lastc[j] - 1u) : '-';   /* (the core of the last block it ran in) */
        (void)ran;
    } else {
        static const uint8_t Q[] = {DTQ_PRE, DTQ_EVT, DTQ_NS, DTQ_OWN, DTQ_WAIT, DTQ_POST, DTQ_BLK, DTQ_C1};
        uint32_t r = (i - 1u - DT_NJ) % 8u;
        q = Q[r];
        c->label = NM[r];
        core = q == DTQ_C1 ? '1' : '0';
    }
    dt_col(v, (int32_t)dual_tm_pct(dual_tm.avg[q]));
    dt_col(v, (int32_t)dual_tm_pct_t(dual_tm_peak(q)));
    dt_col(v, jobrow && !((wran >> j) & 1u) ? -1 : (int32_t)dual_tm_pct_t(dual_tm.worst[q]));
    j = str_len(v);
    v[j] = ' ', v[j + 1u] = core, v[j + 2u] = 0;
    if (q == DTQ_BLK && dual_tm_pct_t(dual_tm_peak(DTQ_BLK)) > 100u)
        c->col = C_WARN;
}
#else
#define INFO_DTN 0u
#endif
#ifdef DBG_MAGIC
/* LAST RESET (2026-10-15: a boot AFTER CRASH with nothing to say why): how the boot before this one ended, from
 * what the reset keeps (core/audio.c felucca_dbg, hal/fm1_irq.h fm1_crash, hal/fm1_sys.h's reset source).
 * Value: POWER ON (nothing kept) | CRASH (the crash report reset it; CRASH1: the debug unit named CPU1) | WDT | SOFT |
 * KEY (a long press) | LOW V, then how long it ran ("12s", "850ms"); unit: " @<the crash's pc>", or " S<the step or
 * main-loop stage it was in, hex> C<CPU1's stage then>" */
#define INFO_RST 1u
static void sys_rst_cell(cell_t *c)
{
    static char u[16];
    uint32_t r = felucca_dbg.prev_rst, up = felucca_dbg.prev_alive_ms;
    char *v = c->val;
    cell_clear(c);
    c->kind = CK_RO;
    c->label = "LAST RESET";
    v[0] = u[0] = 0;
    c->unit = u;
    if ((r & 1u) && !(r & (4u | 0x40u)) && !felucca_dbg.crash_new) {
        dual_cat(v, "POWER ON");
        return;
    }
    if (felucca_dbg.crash_new)
        dual_cat(v, (fm1_crash.dbg & ((1u << 6) | (1u << 8) | (1u << 10) | (1u << 11) | (7u << 16))) ? "CRASH1 " : "CRASH ");
    else
        dual_cat(v, (r & 4u) ? "WDT " : (r & 0x40u) ? "SOFT " : (r & 0x10u) ? "KEY " : (r & 0x22u) ? "LOW V " : "? ");
    dual_cnum(v, up < 10000u ? up : up / 1000u);
    dual_cat(v, up < 10000u ? "ms" : "s");
    if (felucca_dbg.crash_new) {
        dual_cat(u, " @");
        dual_chex(u, fm1_crash.pc, 7);
    } else {
        dual_cat(u, " S");
        dual_chex(u, felucca_dbg.prev_stage, 2);
        dual_cat(u, " C");
        dual_chex(u, felucca_dbg.prev_c1, 1);
    }
    c->col = (r & 4u) || felucca_dbg.crash_new ? C_WARN : 0;
}
#else
#define INFO_RST 0u
#endif
#else
#define INFO_UA 0u                                      /* (no INTERNALS, no USB STATS) */
#define INFO_BTN 0u
#define INFO_DTN 0u
#define INFO_C1 0u
#define INFO_RST 0u
#endif
/* SYSTEM's read-out pages (op_gldraw.c gl_info_draw; the user, 2026-10-15: "the 1st system screen would gain to be split in two
 * pages with easier-to-read text"), each drawn whole, text only:
 *   INFO (page 0, what a musician reads): the clock, the CPU (a bar with its peaks), the
 *        2ND CORE (ON + load, OFF + why, NOT IN BUILD), RAM, DROPS, UNDERRUNS, GUARD (CPU_GUARD builds); the battery and
 *        whether it charges are the header's (op_draw.c head_make), not a line
 *   INTERNALS (page 1): LAST RESET, the longest flash interrupts-off time, the second core's internals (DUAL builds)
 *   USB STATS (page 2, USB audio builds): the USB audio counters */
#if FELUCCA_SYS_DEBUG
#define INFO_BASE (5u + (uint32_t)FELUCCA_CPU_GUARD + (uint32_t)PART_DROPS + (uint32_t)FMEM_ANY)   /* (no CLOCK line since 2026-11-01:
                                                         * the clock is fixed; cpu_khz stays in the console and the CPU guard) */
#else
#define INFO_BASE (3u + (uint32_t)DUAL_LOAD + (uint32_t)PART_DROPS + (uint32_t)FMEM_ANY)   /* (without the debug pages: CPU LOAD (CPU0, CPU1),
                                                         * RAM, ARENA MOVE with an arena, UNDERRUNS) */
#endif
#define INFO_N INFO_BASE                                /* INFO's lines */
#ifndef FELUCCA_STOCK_SUPPLY
#define FELUCCA_STOCK_SUPPLY 0                          /* (system/supply.c's: the host tests include this file alone) */
#endif
#define INFO_SUP ((uint32_t)FELUCCA_STOCK_SUPPLY)       /* the SUPPLY line (STOCK_SUPPLY builds) */
#define INFO_DIAG (INFO_RST + 1u + INFO_SUP + INFO_C1)  /* INTERNALS' lines */
static void sys_info_cell(uint32_t i, cell_t *c)        /* INFO's line i */
{
#if FELUCCA_SYS_DEBUG
    static const uint8_t INFO[INFO_BASE] = {SI_CPU, SI_CPU1, SI_RAM,
#if FMEM_ANY
                                            SI_RAMUSE,
#endif
#if PART_DROPS
                                            SI_ARN,
#endif
                                            SI_DROPS, SI_XRUN
#if FELUCCA_CPU_GUARD
                                            , SI_GUARD
#endif
                                            };          /* (SI_CPU1: the 2ND CORE line, in every build; DROPS, UNDERRUNS: the overload monitor) */
#else
    static const uint8_t INFO[INFO_BASE] = {SI_CPU,
#if DUAL_LOAD
                                            SI_CPU1,    /* (both cores' load: the user, 2026-10-18) */
#endif
                                            SI_RAM,     /* (the user, 2026-10-28) */
#if FMEM_ANY
                                            SI_RAMUSE,  /* (where it is: parts, radios, free; the user, 2026-10-30) */
#endif
#if PART_DROPS
                                            SI_ARN,     /* (the arena's moves: the FM-1's copy speed, 2026-10-29) */
#endif
                                            SI_XRUN};   /* (what a player wants, the user 2026-10-17; the battery is the header's, 2026-10-29) */
#endif
    sys_it_cell(INFO[i % INFO_N], c);
}
#if FELUCCA_SYS_DEBUG
static void sys_diag_cell(uint32_t i, cell_t *c)        /* INTERNALS' line i */
{
    i %= INFO_DIAG;
#if INFO_RST
    if (i == 0u) {
        sys_rst_cell(c);
        return;
    }
#endif
    if (i == INFO_RST) {                                /* the longest interrupts-off window of a flash operation (hal/fm1_flash.h) */
        cell_clear(c);
        c->kind = CK_RO;
        c->label = "FLASH IRQS OFF";
        {
            uint32_t us = (fm1_irqoff_max & 0x0FFFFFFFu) / 24u, k = fm1_irqoff_max >> 28;
            info_num(c, us, k == 3u ? " us erase" : k == 2u ? " us write" : k == 1u ? " us read" : " us");
            c->col = us > OVL_HALF_US ? C_WARN : 0;   /* (longer than an audio half: a drop-out) */
        }
        return;
    }
#if FELUCCA_STOCK_SUPPLY
    if (i == INFO_RST + 1u) {                           /* the rails after the stock start-up (supply.c): core, then RF */
        uint32_t mv;
        cell_clear(c);
        c->kind = CK_RO;
        c->label = "SUPPLY";
        if (supply.err) {
            cell_val(c, "NOT RAISED");   /* (the read-back was wrong: one core, 2ND CORE OFF SUPPLY) */
            c->unit = "";
            c->col = C_WARN;
            return;
        }
        for (mv = 0; mv < 2u; mv++) {               /* "1.38 V 1.60" V: the core rail, then the 1.4 V (RF) rail */
            uint32_t v = mv ? supply_vdc14_mv(supply.now >> 4) : supply_sysvdd_mv(supply.now), n = str_len(c->val);
            char *b = c->val + n;
            if (mv)
                str_cpy(b, " V ", sizeof c->val - n), b += 3;
            b[0] = (char)('0' + v / 1000u);
            b[1] = '.';
            b[2] = (char)('0' + v / 100u % 10u);
            b[3] = (char)('0' + v / 10u % 10u);
            b[4] = 0;
        }
        c->unit = supply_on_dcdc(supply.regs) ? "V DC-DC" : "V LDO";
        return;
    }
#endif
#if INFO_C1
    sys_c1_cell(i - INFO_RST - 1u - INFO_SUP, c);
#endif
}
#endif
#if OP_TOP
#include "op_top.c"                                    /* SYSTEM > TOP: its lines (top_lines, top_cell), SELECT's scroll (top_step) */
#endif
/* the read-out pages (op_gldraw.c gl_info): 0 INFO, 1 INTERNALS, 2 USB STATS, 3 CPU TIMES, 4 BT STATS, 5 TOP; their lines */
enum { INFO_PG_INFO, INFO_PG_DIAG, INFO_PG_USB, INFO_PG_DUAL, INFO_PG_BT, INFO_PG_TOP };
static uint32_t info_lines(uint32_t pg)
{
#if OP_TOP
    if (pg == INFO_PG_TOP)
        return top_lines();
#endif
    return !FELUCCA_SYS_DEBUG ? INFO_N : pg == INFO_PG_USB ? INFO_UA : pg == INFO_PG_DIAG ? INFO_DIAG : pg == INFO_PG_DUAL ? INFO_DTN : pg == INFO_PG_BT ? INFO_BTN : INFO_N;
}
static void info_cell(uint32_t pg, uint32_t i, cell_t *c)
{
#if FELUCCA_SYS_DEBUG
#if FELUCCA_USB_AUDIO
    if (pg == INFO_PG_USB) {
        sys_ua_cell(i % INFO_UA, c);
        return;
    }
#endif
    if (pg == INFO_PG_DIAG) {
        sys_diag_cell(i, c);
        return;
    }
#endif
#if OP_DTIMES
    if (pg == INFO_PG_DUAL) {
        sys_dt_cell(i % INFO_DTN, c);
        return;
    }
#endif
#if OP_BTSTATS
    if (pg == INFO_PG_BT) {
        sys_bts_cell(i, c);
        return;
    }
#endif
#if OP_TOP
    if (pg == INFO_PG_TOP) {
        top_cell(i, c);
        return;
    }
#endif
    (void)pg;
    sys_info_cell(i, c);
}
/* setting it: a knob turned s (> 0 right), or 0: YES (steps round, toggles) */
static void sys_set(uint32_t it, int32_t s)
{
    switch (it) {
#if USBH_UI
    case SI_UHMODE:                                     /* DEVICE / HOST, a detent each, YES round; never kept (every boot is
                                                         * DEVICE): no settings to save */
        usbh_set((uint32_t)sys_step((int32_t)usbh_mode(), s, 1));
        return;
#endif
#if FELUCCA_SD_UI
    case SI_SDVERS:
    case SI_SDSLACK:
    case SI_SDPRE:                                      /* (sd_mode.c: into the log when SYSTEM is left) */
        sdm_set(it - SI_SDVERS - (it == SI_SDPRE), s ? s : 1);
        break;
#endif
    case SI_KCOL:                                       /* (kept in the settings word: settings_word.c) */
        knob_colors = (uint8_t)sys_step(knob_colors, s, 1);
        ui.redraw = 1;                                   /* (every band's colours move: the bands again over themselves, no black fill) */
        break;
    case SI_DBGST:                                      /* (kept in the settings record: persist_rec.c rsv_pj[1]) */
        dbg_stats_set((uint32_t)sys_step(dbg_stats, s, 1));
        break;
    case SI_METERS:                                     /* (kept in the settings record: persist_rec.c rsv_pj[2]) */
        meters_mode = (uint8_t)sys_step(meters_mode, s, MTR_N - 1);
        break;
#if FELUCCA_MIDI_CLKOUT
    case SI_CLKOUT:                                     /* (kept in the settings word: settings_word.c) */
        clko_on = CKO_ORD[sys_step(cko_pos(), s, CKO_N - 1)];
        break;
#endif
#if FELUCCA_LINK
    case SI_LINK:                                       /* Ableton Link: OFF, ON (tempo and beat), ON+S/S (also play / stop) (clock_sync.c) */
        link_mode = (uint8_t)sys_step(link_mode, s, LINK_ONSS);
        break;
#endif
    case SI_HPBASS:
        settings.lowcut = oc_set_word(oc_word(spk_lc, (uint32_t)sys_step((int32_t)(hp_bass % 3u), s, 2)));   /* OFF / +3 / +6 dB */
        break;
#if FELUCCA_CDC
    case SI_USB:
        usb_serial = (uint8_t)(s > 0 ? 1u : s < 0 ? 0u : !usb_serial);
        break;
#endif
#if FELUCCA_BT_UI
    case SI_BLE:                                        /* BT MIDI's MODE: OFF, MAN, AUTO, a detent a mode (YES: round; OFF:
                                                         * a connected central is let go) */
        bt_mode_set(BT_MIDI, s);
        break;
    case SI_BTA:                                        /* BT AUDIO's MODE: the same, BT MIDI left as it is */
        bt_mode_set(BT_AUD, s);
        break;
#if WIFI_UI
    case SI_WFMODE:                                     /* WI-FI's MODE: OFF, MAN, AUTO (kept in the settings record, wn_st.mode) */
        wf_mode_set(s);
        break;
#endif
#if BLE_CENTRAL
    case SI_BLEAUTO:                                    /* (kept in the device store, saved with the settings: ble_devices.c) */
        ble_auto_set(s);
        break;
#endif
#endif
    default:
#if FELUCCA_USB_AUDIO
        if (si_ua(it)) {                                /* (kept in the settings record: persist_rec.c ua_dst) */
            ua_dst_step(it - SI_UAS0, s);
            break;
        }
#endif
        return;
    }
    sys_dirty = 1;
}
/* SHIFT held + PRESETS (or the knob) on a setting with no descriptor: its FACTORY value, what a blank FM-1 or an empty
 * settings record gives (the user, 2026-10-13): panel.c settings_init (SPK LOWCUT ON, HPHN BASS OFF), the settings word read as 0 (settings_word.c: HOLD 350 ms, KNOB COLORS, CLK OUT, SERIAL, LIGHTS,
 * KEYS OFF; 2ND CORE AUTO). BLE is never reset: a reset must not drop a live connection. 1: it had one */
static int sys_factory(uint32_t it)
{
    switch (it) {
#if USBH_UI
    case SI_UHMODE:
        usbh_set(0);                                    /* (DEVICE: the computer again) */
        return 1;
#endif
#if FELUCCA_SD_UI
    case SI_SDVERS:
    case SI_SDSLACK:
    case SI_SDPRE:
        sdm_set(it - SI_SDVERS - (it == SI_SDPRE), 0);   /* (8 versions, 100 %, 50 ms) */
        return 1;
#endif
    case SI_HPBASS:
        settings.lowcut = oc_set_word(oc_word(spk_lc, 0u));
        break;
#if FELUCCA_CDC
    case SI_USB:
        usb_serial = 0;
        break;
#endif
    case SI_KCOL:
        knob_colors = 1;                                /* (its default: ON, 2026-11-03) */
        ui.redraw = 1;
        break;
    case SI_DBGST:
        dbg_stats_set(0);
        break;
    case SI_METERS:
        meters_mode = MTR_DEFAULT;
        break;
#if FELUCCA_MIDI_CLKOUT
    case SI_CLKOUT:
        clko_on = CLKO_OFF;
        break;
#endif
#if FELUCCA_LINK
    case SI_LINK:
        link_mode = LINK_ONSS;
        break;
#endif
    default:
#if FELUCCA_USB_AUDIO
        if (si_ua(it)) {                                /* (as before the setting: T1 T2 T3 DRUMS on 1 2 3 4, the rest NONE) */
            uint32_t s = it - SI_UAS0;
            ua_dst[s] = (uint8_t)(s == UAS_T1 ? UAD_1 : s == UAS_T2 ? UAD_2 : s == UAS_T3 ? UAD_3 : s == UAS_DRUMS ? UAD_4 : UAD_NONE);
            ua_route_apply();
            break;
        }
#endif
        return 0;                                       /* (BLE, the read-outs, PANEL: nothing) */
    }
    sys_dirty = 1;
    return 1;
}
static void sys_turn(uint32_t r, uint32_t k, int32_t s, int fine)
{
    uint32_t it = sys_item(r, k);
    int16_t *vp;
    const param_desc_t *d = sys_desc(it, &vp);
#if FELUCCA_BT_UI
    if (BDV_OPEN)
        return;                                         /* (the list: a knob does nothing) */
    if ((it == SI_BTAPEER || it == SI_BLEPEER) && bt_pg_turn(it == SI_BTAPEER ? BT_AUD : BT_MIDI, s))
        return;                                         /* (a Bluetooth page's DEVICE knob: the cursor through its devices) */
#endif
#if WIFI_UI
    if (it == SI_WFNET && wf_turn(s))
        return;                                         /* (the WI-FI page's NETWORK knob: the cursor through the networks heard) */
    if (it == SI_WFSIG && (wf_grp_turn(s) || wf_ch_turn(s)))
        return;                                         /* (the JAM modes: the group; MODE AP: the channel) */
    if (it == SI_WFST && wf_st_turn(s))
        return;                                         /* (STATE: the line's next item, the cycling held 30 s) */
#endif
    if (s == OP_RESET && !d) {                          /* SHIFT held: no descriptor, the factory value */
        sys_factory(it);
        return;
    }
    if (d) {
        val_turn(d, vp, k, s, fine);
        sys_dirty |= it != SI_TUNE && it != SI_RECQ && it != SI_PUNCH;   /* (TUNE, QUANT, PUNCH: the project's, not the settings record's) */
        return;
    }
    sys_set(it, s);
}
static int sys_yes(uint32_t r, uint32_t k, uint32_t ok)
{
    uint32_t it = sys_item(r, k);
    int16_t *vp;
    const param_desc_t *d = sys_desc(it, &vp);
#if FELUCCA_BT_UI
    if (BDV_OPEN)
        return dev_yes(r, ok);
    if (it == SI_BLE || it == SI_BLEPEER || it == SI_BLEST || it == SI_BLEMIDI) {   /* the BT MIDI page, any line */
        bt_save(BT_MIDI);
        return 1;
    }
    if (it == SI_BTA || it == SI_BTAPEER || it == SI_BTAST || it == SI_BTAFMT) {   /* the BT AUDIO page, any line */
        bt_save(BT_AUD);
        return 1;
    }
#if WIFI_UI
    if (it == SI_WFMODE || it == SI_WFNET || it == SI_WFSIG || it == SI_WFST) {   /* the WI-FI page, any line */
        if (ok)
            wf_forget_do();                             /* (the FORGET question, asked by SAVE held: op_wifi.c wf_forget) */
        else
            wf_save();
        return 1;
    }
#endif
    if (it == SI_BLELIST) {                             /* DEVICES: the devices nearby */
        dev_open(BT_MIDI);
        return 1;
    }
#if BLE_CENTRAL
    if (it == SI_BLEFGT)
        return ble_forget_yes(r, k, ok);
    if (it == SI_BLEAUTO) {
        sys_set(it, 0);
        return 1;
    }
#endif
#endif
#if USBH_UI
    if (it == SI_UHMODE) {                              /* MODE: DEVICE / HOST round */
        sys_set(it, 0);
        return 1;
    }
#endif
    if (it == SI_ABOUT) {
        ui_say(FELUCCA_VERSION, " OPTIMIST UI");
        return 1;
    }
#if FELUCCA_SD_UI
    if (it == SI_SDCARD || it == SI_SDINDEX) {          /* the card mounted again (or found), its folders, the index's pass */
        sdm_again();
        return 1;
    }
    if (it == SI_SDBACK || it == SI_SDREST) {           /* the settings to the card or back (RESTORE: red, then a restart) */
        if (!ok)
            op_arm(SCR_SYSTEM, r, k, it == SI_SDBACK ? "BACKUP" : "RESTORE", "SETTINGS", it == SI_SDREST);
        else if (it == SI_SDBACK)
            sdm_backup();
        else
            sdm_restore();
        return 1;
    }
    if (it == SI_SDVERS || it == SI_SDSLACK || it == SI_SDPRE)
        return 0;                                       /* (numbers: the knob) */
#endif
    if (d) {
        if (!val_toggle(d, vp))
            return 0;
        sys_dirty |= it != SI_TUNE && it != SI_RECQ && it != SI_PUNCH;
        return 1;
    }
    if (it == SI_NONE || (it >= SI_INFO && !si_ua(it)) || it == SI_CPU || it == SI_CPU1)
        return 0;                                       /* (the read-outs; USB AUDIO's lines, last, are settings) */
    sys_set(it, 0);
    return 1;
}
static void sys_leave(void)                             /* SYSTEM left: the settings into flash (as SLOOP's menu_close) */
{
#if FELUCCA_SD_UI
    sdm_leave();                                        /* (the card's settings: the log's device record, sd_mode.c) */
#endif
#if FELUCCA_BT_UI
    dev_close();                                        /* (the devices list open: it closes, the scan stops) */
#endif
    if (!sys_dirty)
        return;
    sys_dirty = 0;
    if (SAVE_WAITS_PLAY())
        settings_later = 1;                             /* (a flash write stops the audio: once stopped) */
    else
        settings_save();
}
