/* SPDX-License-Identifier: GPL-3.0-only */
/* SYSTEM (docs/UI-OPTIMIST-DESIGN.md section 4.7); PROJECT moved to op_prjflow.c (section 4.6). An action cell does
 * nothing when turned: its knob makes it the hot cell, YES does it, and what destroys asks first (op_state.c op_arm). */

#if FELUCCA_BLE
/* ---- BLE devices: SYSTEM > BLUETOOTH, YES on the BLE row opens the list (the same one SLOOP's menu has: NONE, LAST, the
 * BLE-MIDI devices nearby, io/midi/ble_devices.c; the rows, their tags, the status line and the connecting logic are
 * its, shared). While it is open SYSTEM's rows ARE the list: SELECT moves the cursor (it stops at the ends), SAVE picks
 * the row (NONE, LAST) or connects to it, HOME goes back (the scan stops, the BLUETOOTH row is where it was), SHIFT + a
 * button asks FORGET LAST (the modal; SAVE yes). The panel is drawn by dev_draw (op_draw.c calls it), the cards show the
 * row's action and its signal. The words are in sentence case, as the rest of the UI; a device's name is shown as it
 * advertises itself ("KeyStep 37", "MPC Live"), never recased. */
#define DEV_ROW_H 20
#define BLE_ACT_MS 300u                                 /* SYSTEM > Bluetooth's MIDI IN / OUT lit this long after a message */
#define DEV_STATUS_H 24                                 /* the status line under the rows */
#define DEV_LINE_W 232                                  /* the status line's room: 4 px in from each side */
static struct {
    uint8_t open, sys_row;                              /* the list is open; SYSTEM's row to come back to */
    uint8_t first;                                      /* the first row shown: kept, it moves only to follow the cursor */
    uint8_t key_ok, key_row;                            /* the row drawn highlighted, and which device it is */
    struct ble_dev_key key;                             /* (YES picks that device, wherever the list moved it) */
} bdv;
static struct {
    int last;                                           /* what dev_paint draws */
    uint8_t near[BLE_SCAN_N];
    uint32_t n, cur, first, shown, h, tone;
    uint16_t bar;
    union {
        char st[40];                                    /* the status line (dev_draw, while the list is open) */
        struct {
            uint32_t i, o, ti, to;                      /* the Bluetooth page's MIDI line: midi_ble.c's ring counts seen, and when
                                                         * they moved (only while the list is closed: no RAM of its own) */
        } act;
    };
#if BLE_CENTRAL
    uint32_t key;                                       /* the passkey (BLE_NO_PASSKEY: none) */
#endif
} bdp;
static void cv_tall(uint32_t y, uint32_t h, uint16_t bg, void (*fn)(void));   /* op_draw.c */
static uint32_t hs(uint32_t h, const char *s);          /* (signatures: op_draw.c) */
static uint32_t hu(uint32_t h, uint32_t v);
static const char *cut(char *b, const char *s, uint32_t n);
static const felucca_font_t *font_big(void);            /* op_graph.c */

static int dev_listing(void) { return bdv.open != 0u && ui.scr == SCR_SYSTEM; }
static uint32_t dev_rows(void)
{
    int last;
    uint32_t nn;
    uint8_t near[BLE_SCAN_N];
    return ble_dev_rows(&last, near, &nn);
}
static void dev_open(void)
{
    bdv.open = 1;
    bdv.first = 0;
    bdv.key_ok = 0;
    bdv.sys_row = ui.row[SCR_SYSTEM];
    ui.row[SCR_SYSTEM] = 0;                             /* (the cursor on NONE) */
    ui.hot = 0;
    ui.hot_lit = 0;
    op_disarm();
    ble_devices_open(1);                                /* (the scan starts) */
    ui.force = 1;
}
static void dev_close(void)
{
    if (!bdv.open)
        return;
    bdv.open = 0;
    ble_devices_open(0);                                /* (the scan stops, advertising goes on) */
    bdp.act.i = bmi_w, bdp.act.o = bmo_w;               /* (the MIDI line's counters take the status line's place: dark) */
    bdp.act.ti = bdp.act.to = fm1_ms - BLE_ACT_MS;
    ui.row[SCR_SYSTEM] = bdv.sys_row;
    ui.hot = 0;
    ui.hot_lit = 0;
    op_disarm();
    ui.force = 1;
}
static void dev_cell(uint32_t r, uint32_t k, cell_t *c)   /* the cards: K1 what YES does here, K2 the signal */
{
    int last;
    uint8_t near[BLE_SCAN_N];
    uint32_t nn, bars;
    const char *tag;
    cell_clear(c);
    if (r >= ble_dev_rows(&last, near, &nn))
        return;
    ble_dev_row(r, last, near, 0, &tag, 0, &bars);
    if (k == 0u) {                                      /* (what YES does: the tag says what the row is doing now) */
        cell_act(c, "DEVICE", r == 0u ? "CHOOSE" : str_eq(tag, "CONNECTED") ? "RECONNECT" : str_len(tag) > 4u ? "RETRY" : "CONNECT", 0, 0);
    } else if (k == 1u && r && str_len(tag) <= 4u) {   /* (the signal: none for a link up or being made, "" / LAST) */
        c->kind = CK_RO;
        c->label = "SIGNAL";
        fmt_int(c->val, (int32_t)bars);
        c->unit = "/3";
        cell_gauge(c, 1, 0, 3, (int32_t)bars);
    }
}
static int dev_yes(uint32_t r, uint32_t ok)             /* SAVE: pick / connect; confirmed (the modal): FORGET LAST */
{
    if (!ok) {                                          /* an action card: asks first (the user 2026-10-13) */
        cell_t c;
        char nm[BLE_NAME_MAX + 1u];
        int last;
        uint8_t near[BLE_SCAN_N];
        uint32_t nn, bars;
        const char *tag, *verb;
        dev_cell(r, 0, &c);
        verb = c.label ? c.label : "CHOOSE";
        nm[0] = 0;
        {
            int nr = bdv.key_ok ? ble_dev_key_row(&bdv.key) : (int)r;   /* (the device the pick will take: its row now) */
            if (nr > 0 && (uint32_t)nr < ble_dev_rows(&last, near, &nn))
                ble_dev_row((uint32_t)nr, last, near, nm, &tag, 0, &bars);
        }
        op_arm(SCR_SYSTEM, r, 0, verb, nm[0] ? nm : "DEVICE", 0);   /* ("Connect?" over the name as advertised, the modal sizes it) */
        ui.arm_raw = nm[0] != 0;
        return 1;
    }
    if (str_eq(ui.arm_verb, "FORGET?")) {                                   /* (SHIFT + a button: FORGET LAST) */
        ble_dev_forget();
        ui.row[SCR_SYSTEM] = 0;                         /* (NONE is the choice again) */
        return 1;
    }
    if (bdv.key_ok && (r == bdv.key_row || (r < bdv.key_row && r + 1u == dev_rows())))   /* (a shrunk list clamps the row) */
        ble_dev_pick_key(&bdv.key);                     /* (the row the user saw: that device, not that place) */
    else {                                              /* (the cursor moved since the draw: the key of this row, now) */
        struct ble_dev_key k;
        ble_dev_key_of(r, &k);
        ble_dev_pick_key(&k);
    }
    return 1;
}
static int dev_home_combo(void)                         /* SHIFT + a button on the list: FORGET LAST, asked (op_combos.c) */
{
    char nm[BLE_NAME_MAX + 1u];
    if (!dev_listing())
        return 0;
    if (op_armed())
        return 1;                                       /* (asked already: the 3 s run on, as SAVE's combos wait) */
    if (!ble_store_has_last(&ble_store)) {
        ui_message("NO LAST DEVICE");
        return 1;
    }
    ble_store_name(&ble_store, nm);
    op_arm(SCR_SYSTEM, ui.row[SCR_SYSTEM], 0, "FORGET", nm, 1);
    ui.arm_raw = 1;                                     /* (the name as advertised) */
    return 1;
}
/* the signal of a nearby row: three bars rising, as many lit as the scan heard (ble_dev_row: relative, no RSSI gain
 * table yet); none lit: not heard in this scan */
static void dev_bars(int32_t x, int32_t y, uint32_t n, int on)
{
    int32_t k;
    for (k = 0; k < 3; k++)
        if ((uint32_t)k >= n && !on)                    /* (unlit: outlined on black) */
            cv_frame(x + k * 6, y + 16 - 4 * (k + 1), 4, 4 * (k + 1), C_LINE);
        else
            cv_rect(x + k * 6, y + 16 - 4 * (k + 1), 4, 4 * (k + 1), (uint32_t)k < n ? (on ? C_WHITE : C_GRAY) : cur_trough(bdp.bar));
}
/* a row's text: NONE's is our words (sentence case), a device's name stays as it advertises itself */
static void dev_row_text(char *out, uint32_t room, uint32_t row, const char *nm)
{
    if (row == 0u)
        op_case(out, nm, room);
    else
        str_cpy(out, nm, room);
}
static void dev_paint(void)                             /* the panel's band in its own coordinates (cv_tall) */
{
    uint32_t i, rows_h = bdp.h - DEV_STATUS_H;
    char nm[BLE_NAME_MAX + 4u], b[BLE_NAME_MAX + 4u];
    int32_t wide = bdp.n > bdp.shown ? 236 : 240;
#if BLE_CENTRAL
    if (bdp.key != BLE_NO_PASSKEY) {                    /* pairing with a code: the phone's user types it */
        char d[7];
        int32_t k;
        uint32_t pk = bdp.key;
        for (k = 5; k >= 0; k--, pk /= 10u)
            d[k] = (char)('0' + pk % 10u);
        d[6] = 0;
        cv_text(120 - text_w(&FONT_S, bdp.st) / 2, (int32_t)bdp.h / 2 - 30, &FONT_S, bdp.st, C_HI);
        cv_text(120 - text_w(font_big(), d) / 2, (int32_t)bdp.h / 2 - 8, font_big(), d, C_WHITE);
        return;
    }
#endif
    for (i = bdp.first; i < bdp.n && i < bdp.first + bdp.shown; i++) {
        int32_t y = (int32_t)(i - bdp.first) * DEV_ROW_H, right = wide - 4, room;
        int chosen, on = i == bdp.cur;
        const char *tag;
        uint32_t bars;
        ble_dev_row(i, bdp.last, bdp.near, nm, &tag, &chosen, &bars);
        if (on)
            cv_rect(0, y, wide, DEV_ROW_H - 1, cur_bg(bdp.bar));   /* the cursor row: the track's colour, text white */
        if (chosen)
            cv_rect(4, y + 7, 6, 6, on ? C_WHITE : C_HI);   /* the choice (NONE or LAST) */
        if (i && (!tag[0] || str_len(tag) <= 4u)) {      /* (CONNECTED / CONNECTING / PAIRING take the bars' place) */
            dev_bars(wide - 22, y, bars, on);
            right = wide - 28;
        }
        if ((int)i == bdp.last && str_eq(tag, "LAST") && (ble_store.dev.info & BLE_DEV_BONDED))
            tag = "PAIRED";                           /* (LAST bonded: its keys kept, it reconnects encrypted) */
        if (tag[0]) {
            op_case(b, tag, sizeof b);
            right -= text_w(&FONT_S, b);
            cv_text(right, y + 1, &FONT_S, b, on ? C_WHITE : C_GRAY);
            right -= 8;
        }
        room = right - 16;
        dev_row_text(b, sizeof b, i, nm);
        cv_text(16, y + 1, &FONT_S, cut(nm, b, room > 8 ? (uint32_t)room / 8u : 1u), on ? C_WHITE : chosen ? C_HI : C_GRAY);
    }
    if (bdp.n > bdp.shown) {                            /* where the window is in the list */
        cv_rect(238, 0, 1, (int32_t)rows_h, C_LINE);
        cv_rect(237, (int32_t)(rows_h * bdp.first / bdp.n), 3, (int32_t)(rows_h * bdp.shown / bdp.n), C_GRAY);
    }
    cv_rect(0, (int32_t)rows_h, 240, 1, C_LINE);
    cv_text(4, (int32_t)rows_h + 5, &FONT_S, bdp.st, bdp.tone == BDL_GOOD ? C_OK : bdp.tone == BDL_WARN ? C_WARN : bdp.tone == BDL_BAD ? C_ERR : C_DIM);
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
    while (out[0] && text_w(&FONT_S, out) > DEV_LINE_W)
        out[str_len(out) - 1u] = 0;
}
/* the list in the panel y, h tall (op_draw.c, when dev_listing): lazy, redrawn when what it shows changes */
static void dev_draw(uint32_t y, uint32_t h)
{
    uint32_t nn, sig;
    char b[sizeof bdp.st];
    bdp.n = ble_dev_rows(&bdp.last, bdp.near, &nn);
    bdp.cur = ui.row[SCR_SYSTEM] < bdp.n ? ui.row[SCR_SYSTEM] : bdp.n - 1u;
    bdp.shown = (h - DEV_STATUS_H) / DEV_ROW_H;
    bdp.first = bdv.first;                              /* (kept: the window moves only when the cursor leaves it) */
    if (bdp.cur < bdp.first)
        bdp.first = bdp.cur;
    else if (bdp.cur >= bdp.first + bdp.shown)
        bdp.first = bdp.cur - (bdp.shown - 1u);
    if (bdp.first + bdp.shown > bdp.n)                  /* (the list shrank: no empty rows under the last one) */
        bdp.first = bdp.n > bdp.shown ? bdp.n - bdp.shown : 0u;
    bdv.first = (uint8_t)bdp.first;
    ble_dev_key_of(bdp.cur, &bdv.key);                  /* (what is highlighted: YES picks it by what it is) */
    bdv.key_row = (uint8_t)bdp.cur;
    bdv.key_ok = 1;
    bdp.h = h;
    bdp.bar = trk_col(song.sel);
#if BLE_CENTRAL
    bdp.key = ble_connect_passkey();
#endif
    bdp.tone = ble_dev_line(b, sizeof b, nn);
    dev_line_text(bdp.st, sizeof bdp.st, b, ble_dev_line_name_at());
    sig = hu(hs(hu(hu(hu(ble_dev_sig(), bdp.cur), bdp.first), bdp.bar), bdp.st), h) | 1u;
    if (sig == ui.sig[2])
        return;
    ui.sig[2] = sig;
    cv_tall(y, h, C_BLACK, dev_paint);
}
/* ---- SYSTEM > Bluetooth and Devices, the lines (docs/UI-OPTIMIST-DESIGN.md section 39): STATE in words, the DEVICE the
 * link is with, MIDI activity, RECONNECT (the list's NONE / LAST) and FORGET LAST. Read from ble_devices.c, nothing new kept
 * but the activity's two counters */
#define bact bdp.act                                    /* (in the list's drawing state: the list and this line are never both shown) */
static const char *const BLE_ST[9] = {"OFF", "ADVERTISING", "CONNECTED", "NO CAL", "SCANNING", "CONNECTING", "SEARCHING",
                                      "PAIRING", "FAILED"};   /* (ble_dev_status's codes) */
static void ble_state_cell(cell_t *c)
{
    uint32_t s = ble_dev_status();
    c->kind = CK_RO;
    c->label = "STATE";
    c->big = 1;
    str_cpy(c->val, ble_on && !ble_up && s == 0u ? "STARTING" : BLE_ST[s % 9u], sizeof c->val);
    c->col = s == 2u ? C_OK : s == 3u ? C_WARN : s == 8u ? C_ERR : 0;
}
/* the device the link is with (or being made with, or searched for): its name as advertised; HOST, a computer or phone that
 * connected to the FM-1 (its name is not known); NONE */
static void ble_peer_cell(cell_t *c)
{
    char nm[40];
    const char *v = "NONE";
    c->kind = CK_RO;
    c->label = "DEVICE";
    c->big = 1;
#if BLE_CENTRAL
    if (ble_on && ble_up && ble_connect_status(nm, sizeof nm) != RCS_NONE && rc_nm_at < str_len(nm))
        v = nm + rc_nm_at;                              /* (connecting / pairing / connected to a pick) */
    else if (ble_connect_last_up() || ble_seeking() == 2)
        ble_store_name(&ble_store, nm), v = nm;         /* (our link to LAST, or the search for it) */
    else
#endif
        if (ble_on && ble_connected())
        v = "HOST";
    str_cpy(c->val, v, sizeof c->val);
    c->col = ble_connected() ? C_OK : 0;
}
static void ble_midi_cell(cell_t *c)
{
    uint32_t in, out;
    if (bmi_w != bact.i)
        bact.i = bmi_w, bact.ti = fm1_ms;
    if (bmo_w != bact.o)
        bact.o = bmo_w, bact.to = fm1_ms;
    in = fm1_ms - bact.ti < BLE_ACT_MS;
    out = fm1_ms - bact.to < BLE_ACT_MS;
    c->kind = CK_RO;
    c->label = "MIDI";
    c->big = 1;
    str_cpy(c->val, in && out ? "IN OUT" : in ? "IN" : out ? "OUT" : "--", sizeof c->val);
    c->col = in || out ? C_OK : 0;
}
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
        str_cpy(c->val, nm, sizeof c->val);
    } else
        str_cpy(c->val, "OFF", sizeof c->val);
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

#include "../../system/dual_mode.h"                     /* 2ND CORE: DUALM_*, dual_why_name, and the INFO text helpers dual_cat / dual_cnum / dual_chex (LAST RESET: every UI=1 build; pure, no state) */
/* ---- SYSTEM: SLOOP's HOME-held menu as rows (ui/sloop/ui_menu.c) */
enum { SI_NONE, SI_COLOR, SI_BRIGHT, SI_LIGHTS, SI_KEYS, SI_LOWCUT, SI_OUT, SI_SYNC, SI_CH1, SI_CH2,
       SI_CH3, SI_CHD, SI_USB, SI_CPU, SI_MHZ, SI_CALIB, SI_ABOUT, SI_HOLD, SI_BLE, SI_KCOL, SI_CLKOUT, SI_CPU1, SI_INFO, SI_BATT, SI_BUILD, SI_REPO,
       SI_DROPS, SI_GUARD, SI_UAINFO, SI_XRUN, SI_DIAG, SI_CREDITS, SI_BLEST, SI_BLEPEER, SI_BLEMIDI, SI_BLELIST, SI_BLEAUTO,
       SI_BLEFGT,
       SI_UAS0 };                                       /* SI_UAS0 + s: USB AUDIO's line of source s (ua_tap.h UAS_*), last */
/* a core's load number (% of the audio budget) in its colour: green under 60, amber 60..85, red over 85 (the predictive guard's
 * ceiling, system/cpuguard.c CG_CEIL = 85 %: above it the guard sheds quality / voices). Text only (INFO draws no bars). */
#define LOAD_AMBER 60
#define LOAD_RED 85
static uint16_t load_col(int32_t pct) { return pct > LOAD_RED ? C_ERR : pct >= LOAD_AMBER ? C_WARN : C_OK; }
/* INFO's overload lines (system/overload.h, cpuguard.c): the CPU lines' measure (KNOB 2: NOW, MAX 5S, MAX), DROPS, UNDERRUNS,
 * GUARD; DROPS, UNDERRUNS, MAX and the voices shed counted from a baseline SHIFT + YES on INFO sets (op_global.c info_clear;
 * the counters themselves go on: the console still reads them since boot) */
#define DROPS_RED_MS 5000u                              /* DROPS red while a late half came within this long, else amber */
static uint32_t info_shed0;
static uint8_t info_mode;                               /* the CPU lines' measure: OVL_NOW, OVL_MAX5, OVL_MAXALL (INFO's KNOB 2) */
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
/* a CPU line's value in the measure chosen: NOW (now: the smoothed load given), MAX 5S, MAX (ring's maxima); its label's suffix */
static int32_t info_load(uint32_t ring, int32_t now)
{
    return info_mode == OVL_MAX5 ? (int32_t)ovl_max5_pct(ring, fm1_ms) : info_mode == OVL_MAXALL ? (int32_t)ovl_maxall_pct(ring) : now;
}
static const char *const INFO_MODE_SFX[OVL_NMODE] = {"", " MAX 5S", " MAX"};
static void info_label(cell_t *c, const char *base)     /* "CPU0" + the measure's suffix ("CPU0 MAX 5S"), in a buffer of its own */
{
    static char b[2][16];
    char *t = b[base[3] == '1'];
    str_cpy(t, base, sizeof b[0]);
    str_app(t, INFO_MODE_SFX[info_mode % OVL_NMODE], sizeof b[0]);
    c->label = t;
}
#if FELUCCA_CPU_GUARD && FELUCCA_SYS_DEBUG
static const char *const CG_NAME[CG_TOP + 1] = {"OFF", "QUALITY", "UNISON", "REVERB", "NOTES"};
#endif
#define SI_IF(c, i) ((c) ? (uint8_t)(i) : (uint8_t)SI_NONE)
#if FELUCCA_USB_AUDIO
#define UA_IT(s) SI_IF((s) < UAS_N, SI_UAS0 + (s))      /* USB AUDIO's line of source s, none past the last */
_Static_assert(UAS_N <= 16u, "USB AUDIO: four rows of four sources");
static int si_ua(uint32_t it) { return it >= SI_UAS0 && it < SI_UAS0 + UAS_N; }
#else
#define si_ua(it) 0
#endif
static const struct { const char *name; uint8_t it[4]; } SYS[] = {
    {"INFO", {SI_INFO, SI_NONE, SI_NONE, SI_NONE}},      /* first: one dense read-only page, not 4 lines (op_gldraw.c gl_info_draw): the clock, the
                                                         * load of each core, the memory (the user, 2026-10-14). The second core is always on:
                                                         * no 2ND CORE line; dual.c's guard still parks it and the page says why */
#if FELUCCA_SYS_DEBUG                                   /* (the debug pages: builder item SYS_DEBUG, the user 2026-10-17) */
    {"INTERNALS", {SI_DIAG, SI_NONE, SI_NONE, SI_NONE}},   /* INFO split in two (the user, 2026-10-15): what a musician reads on INFO,
                                                         * the internals here (the last reset, flash, the second core) */
#if FELUCCA_USB_AUDIO
    {"USB STATS", {SI_UAINFO, SI_NONE, SI_NONE, SI_NONE}},   /* the USB audio glitch counters */
#endif
#endif
    {"SCREEN", {SI_COLOR, SI_IF(FELUCCA_BRIGHT, SI_BRIGHT), SI_NONE, SI_KCOL}},   /* (SI_KCOL: KNOB COLORS; KNOB 3 was CARDS, gone 2026-10-12) */
    {"AUDIO", {SI_LOWCUT, SI_NONE, SI_NONE, SI_NONE}},
#if FELUCCA_LIGHTS
    {"KEYS", {SI_LIGHTS, SI_KEYS, SI_NONE, SI_NONE}},
#endif
#if FELUCCA_CDC
    {"USB", {SI_USB, SI_NONE, SI_NONE, SI_NONE}},
#endif
#if FELUCCA_BLE
    {"BLUETOOTH", {SI_BLE, SI_BLEST, SI_BLEPEER, SI_BLEMIDI}},   /* the radio ON / OFF, its STATE in words, the DEVICE it is
                                                         * connected to, MIDI in / out activity (ble_*_cell above) */
    {"DEVICES", {SI_BLELIST, SI_IF(BLE_CENTRAL, SI_BLEAUTO), SI_IF(BLE_CENTRAL, SI_BLEFGT), SI_NONE}},   /* the list (YES), RECONNECT
                                                         * (NONE / LAST), FORGET LAST (asked): LAST exists only with BLE_CENTRAL */
#endif
#if FELUCCA_USB_AUDIO
    /* where each source goes (the user, 2026-10-17: "each track, including master, can be assigned to none, 1, 2, 1+2, 3, 4,
     * 3+4", and the drum lanes; ua_tap.h): a line a source, MASTER T1 T2 T3 | DRUMS, DRUM BUS, DR1 DR2 | DR3..DR6 | DR7..DR10;
     * the rows one section over their pages (op_global.c gl_sid: a row named as the one before it goes on with it) */
    {"USB AUDIO", {UA_IT(0), UA_IT(1), UA_IT(2), UA_IT(3)}},
    {"USB AUDIO", {UA_IT(4), UA_IT(5), UA_IT(6), UA_IT(7)}},
    {"USB AUDIO", {UA_IT(8), UA_IT(9), UA_IT(10), UA_IT(11)}},
    {"USB AUDIO", {UA_IT(12), UA_IT(13), UA_IT(14), UA_IT(15)}},
#endif
    {"CALIBRATE", {SI_CALIB, SI_HOLD, SI_NONE, SI_NONE}},   /* (SI_HOLD: a button's hold, 250 / 350 / 500 ms) */
    {"ABOUT", {SI_ABOUT, SI_BUILD, SI_REPO, SI_NONE}},
    {"CREDITS", {SI_CREDITS, SI_NONE, SI_NONE, SI_NONE}},   /* after ABOUT: one text page (op_gldraw.c gl_cred_draw: CRED) */
    /* the global rows, last: GLOBAL's (GLO tapped, op_global.c), not SYSTEM's (the user, 2026-10-12: GLO "the page
     * where we have bpm, nudge, midi track settings etc"); defined here once, SYSTEM shows the rows above them */
    {"MIDI", {SI_IF(FELUCCA_MIDI_CLOCK, SI_SYNC), SI_IF(FELUCCA_MIDI_CLKOUT, SI_CLKOUT), SI_IF(FELUCCA_MIDI_OUT, SI_OUT),
              SI_NONE}},   /* (CLK IN, CLK OUT, MIDI OUT: no MIDI IN since 2026-10-14, the user: notes come in through the track channels,
                            * the clock through CLK IN; settings word bit 15 is free; CLKOUT: the clock sent, io/midi/clock_out.c) */
    {"CHANNELS", {SI_CH1, SI_CH2, SI_CH3, SI_CHD}},
};
#define NSYS (sizeof SYS / sizeof SYS[0])
#define NSYS_GLO 2u                                     /* the global rows at SYS's end: MIDI, CHANNELS */
#define NSYS_SETUP (NSYS - NSYS_GLO)                    /* SYSTEM's rows: the setup (SCREEN, AUDIO, BLUETOOTH ...) */
static uint8_t sys_dirty;                               /* a setting changed: saved when SYSTEM or GLOBAL is left */
static uint32_t sys_rows(void)
{
#if FELUCCA_BLE
    if (bdv.open)
        return dev_rows();                              /* (the BLE devices list: its rows) */
#endif
    return NSYS_SETUP;
}
static void sys_name(uint32_t r, char *b)
{
#if FELUCCA_BLE
    if (bdv.open) {
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
    if (it == SI_SYNC) {
        *vp = &song.g[G_SYNC];
        return &GP[G_SYNC];
    }
    if (it >= SI_OUT && it <= SI_CHD)
        return bps_desc(BPS[it - SI_OUT], vp);
    return 0;
}
static int32_t sys_step(int32_t v, int32_t s, int32_t max)   /* a knob stops at the ends; YES (s 0) goes round */
{
    return s ? clamp(v + (s > 0 ? 1 : -1), 0, max) : (v >= max ? 0 : v + 1);
}
#ifndef FELUCCA_COMMIT
#define FELUCCA_COMMIT ""
#endif
/* the battery, from the ADC (main.c reads ch3, the divider on PB1, smoothed into song.batt_raw, 0 = not read yet). Only the raw 10 bit count
 * is known: no volts, no percentage (the divider's ratio and the cell's curve are not measured), so none is shown. The 3 bars are
 * SLOOP's own thresholds (ui/sloop/ui_draw.c batt_level: 531 / 561 / 591); with a USB host present the line says USB (whether that shifts the reading is not measured) */
static uint32_t sys_batt_bars(void)
{
    return song.batt_raw >= 591 ? 3u : song.batt_raw >= 561 ? 2u : song.batt_raw >= 531 ? 1u : 0u;
}
static void sys_ro(cell_t *c, const char *label)    /* a read-out line */
{
    c->label = label;
    c->kind = CK_RO;
}
static void sys_onoff(cell_t *c, const char *label, uint32_t on)   /* an ON / OFF line */
{
    cell_list(c, label, on ? "ON" : "OFF", 1, on != 0u);
}
static void sys_it_cell(uint32_t it, cell_t *c)     /* the cell of setting it */
{
    static const char *const LV[4] = {"OFF", "LOW", "MID", "HIGH"};
    static const char *const KY[4] = {"OFF", "C", "WHITE", "ALL"};
    static const char *const LC[3] = {"OFF", "LOWCUT", "BASS+"};
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
        str_cpy(c->val, ua_dst_name(ua_dst[it - SI_UAS0]), sizeof c->val);
        return;
    }
#endif
    switch (it) {
    case SI_COLOR:
        cell_list(c, "COLOR", PALETTES[settings.palette % NPALETTES].name, NPALETTES - 1, (int32_t)(settings.palette % NPALETTES));
        break;
#if FELUCCA_BRIGHT
    case SI_BRIGHT:
        c->label = "BRIGHT";
        fmt_int(c->val, (int32_t)bright_level());
        cell_gauge(c, 0, 1, 8, (int32_t)bright_level());
        break;
#endif
#if FELUCCA_LIGHTS
    case SI_LIGHTS:
        cell_list(c, "BUTTONS", LV[lights_lvl & 3u], LIGHTS_N - 1, lights_lvl);
        break;
    case SI_KEYS:
        cell_list(c, "KEYS", KY[lights_keys & 3u], KEYS_N - 1, lights_keys);
        break;
#endif
    case SI_LOWCUT:
        cell_list(c, "LOWCUT", LC[FELUCCA_BASSPLUS ? settings.lowcut % 3u : settings.lowcut ? 1u : 0u], FELUCCA_BASSPLUS ? 2 : 1, FELUCCA_BASSPLUS ? (int32_t)(settings.lowcut % 3u) : settings.lowcut != 0u);
        break;
#if FELUCCA_CDC
    case SI_USB:
        sys_onoff(c, "SERIAL", usb_serial);
        c->col = usb_serial == usb_cdc_on ? 0 : C_WARN;  /* (amber: a restart applies it) */
        break;
#endif
#if FELUCCA_BLE
    case SI_BLE:
        cell_list(c, "BLUETOOTH", !ble_on ? "OFF" : !ble_radio_ok() ? "NO CAL" : "ON", 1, ble_on != 0u);   /* (a link: STATE says it) */
        c->col = ble_on && !ble_radio_ok() ? C_WARN : 0;      /* (NO CAL: no stored RF trims, the radio never starts: midi_ble.c) */
        break;
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
            c->kind = CK_RO, c->label = "LAST DEVICE", str_cpy(c->val, "NONE", sizeof c->val);
        break;
#endif
#endif
    case SI_CPU: {                                      /* (DUAL: its wait for CPU1 counts in it); KNOB 2: NOW, MAX 5S, MAX (over 100: late) */
        int32_t pct = info_load(OVL_CPU0, (int32_t)(song.cpu_q8 * 100u / 256u));
        const char *nm = !FELUCCA_SYS_DEBUG ? "CPU LOAD" : DUAL_LOAD ? "CPU0" : "LOAD";   /* (without the debug pages: one
                                                         * load, CPU0's, which waits for CPU1) */
        if (info_mode == OVL_NOW)
            c->label = nm;
        else
            info_label(c, nm);
        c->kind = CK_RO;
        fmt_int(c->val, pct);
        c->col = load_col(pct);
        c->unit = "%";
        cell_gauge(c, 0, 0, 100, pct < 100 ? pct : 100);
        break;
    }
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
                fmt_int(u + str_len(u), (int32_t)(nu < 9999u ? nu : 9999u));
            }
            if (mi) {
                str_app(u, " MIDI", sizeof u);
                fmt_int(u + str_len(u), (int32_t)(mi < 9999u ? mi : 9999u));
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
        str_cpy(c->val, CG_NAME[l], sizeof c->val);
        if (cg.vcap) {                                  /* (reacting at the top: the voices capped) */
            str_app(c->val, " MAX", sizeof c->val);
            fmt_int(c->val + str_len(c->val), cg.vcap);
        }
        if (n && str_len(c->val) + 5u < sizeof c->val) {
            str_app(c->val, " -", sizeof c->val);
            fmt_int(c->val + str_len(c->val), (int32_t)(n < 999u ? n : 999u));
        }
        c->unit = n ? "V" : "";
        c->col = !l ? C_OK : l < CG_L_REVERB ? C_WARN : C_ERR;
        cell_gauge(c, 0, 0, CG_TOP, (int32_t)l);
        break;
    }
#endif
#if FELUCCA_SYS_DEBUG
    case SI_CPU1: {                                     /* the second core: ON and its share of each audio half (dual_load.h), OFF and why, or not in the build */
#if DUAL_LOAD
        int32_t pct = dual_c1_pct();
        c->kind = CK_RO;
        if (pct < 0) {                                  /* (down: CPU0 renders everything, dual.c; why it is down, else NO REASON) */
            c->label = "2ND CORE OFF";
            str_cpy(c->val, dual.why ? dual_why_name(dual.why) : "NO REASON", sizeof c->val);
            c->unit = "";
            c->col = C_WARN;
            cell_gauge(c, 0, 0, 100, 0);
        } else {                                        /* (KNOB 2: NOW, MAX 5S, MAX: its jobs' longest half) */
            pct = info_load(OVL_CPU1, pct);
            if (info_mode == OVL_NOW)
                c->label = "2ND CORE ON";
            else
                info_label(c, "CPU1");
            fmt_int(c->val, pct);
            c->unit = "%";
            c->col = load_col(pct);
            cell_gauge(c, 0, 0, 100, pct < 100 ? pct : 100);
        }
#else
        sys_ro(c, "2ND CORE");
        str_cpy(c->val, "NOT IN BUILD", sizeof c->val);
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
    case SI_CREDITS:                                    /* the credits page's one line (op_gldraw.c gl_cred_draw draws the page) */
        sys_ro(c, "CREDITS");
        break;
    case SI_BATT: {                                     /* BATTERY: the bars out of 3 and the raw ADC count, no invented percentage */
        uint32_t bars = sys_batt_bars();
        sys_ro(c, "BATTERY");
        c->unit = "";
        if (!song.batt_raw) {
            str_cpy(c->val, "NO READING", sizeof c->val);
            c->col = C_DIM;
            cell_gauge(c, 0, 0, 3, 0);
            break;
        }
        fmt_int(c->val, (int32_t)bars);
        str_app(c->val, "/3 ", sizeof c->val);
        fmt_int(c->val + str_len(c->val), song.batt_raw);
        c->unit = usb.config && !usb.suspended ? "USB" : "";
        c->col = bars == 0u ? C_WARN : 0;
        cell_gauge(c, 0, 0, 3, (int32_t)bars);
        break;
    }
    case SI_BUILD:                                      /* the build's date and time (the compiler's) */
        sys_ro(c, "BUILD");
        str_cpy(c->val, __DATE__, sizeof c->val);
        c->unit = __TIME__;
        break;
    case SI_REPO:                                       /* the source, as text: the link right-aligned in the unit */
        c->label = " ";                                 /* (blank: the link takes the line's width, the label must not be empty for the help) */
        c->kind = CK_RO;
        c->unit = "github.com/w0ts/optimist";
        break;
#if FELUCCA_SYS_DEBUG
    case SI_MHZ:
        sys_ro(c, "CLOCK");
        fmt_int(c->val, (int32_t)((cpu_khz + 500u) / 1000u));
        c->unit = "MHz";
        cell_gauge(c, 0, 0, 240, (int32_t)((cpu_khz + 500u) / 1000u));
        break;
#endif
    case SI_HOLD:                                       /* a button held this long is a hold (op_state.c op_hold_ms) */
        c->label = "HOLD";
        fmt_int(c->val, (int32_t)op_hold_ms());
        c->unit = "ms";
        cell_gauge(c, 1, 0, HOLD_N - 1, OP_HOLD_POS[hold_sel % HOLD_N]);
        break;
    case SI_KCOL:                                       /* the knob caps' colours on dials, labels, cards (core/knobcol.h) */
        sys_onoff(c, "KNOB COLORS", knob_colors);       /* (the settings list has the room: op_gldraw.c) */
        break;
#if FELUCCA_MIDI_CLKOUT
    case SI_CLKOUT:                                     /* MIDI clock out (io/midi/clock_out.c) */
        sys_onoff(c, "CLK OUT", clko_on);
        c->col = clko_blocked() ? C_WARN : 0;            /* (amber: ON, but USB is the clock followed: nothing sent) */
        break;
#endif
    case SI_CALIB:
        cell_act(c, 0, "PANEL", "CALIBRATE", 0);    /* (the action's word, the user 2026-10-13; the list's small face) */
        break;
    case SI_ABOUT: {                                    /* a read-out: the version itself (YES still says it whole) */
        const char *v = FELUCCA_VERSION;
        sys_ro(c, "VERSION");
        str_cpy(c->val, v + (str_len(v) > 9u && !memcmp(v, "OPTIMIST ", 9) ? 9u : 0u), sizeof c->val);  /* "OPTIMIST 0.1": 0.1 */
        c->unit = FELUCCA_COMMIT;                       /* (the commit it was built from, "" without) */
        break;
    }
    default:
        c->kind = CK_NONE;
        break;
    }
    (void)LV, (void)KY;
}
static void sys_cell(uint32_t r, uint32_t k, cell_t *c)
{
#if FELUCCA_BLE
    if (bdv.open) {
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
#define INFO_UA 9u
static void sys_ua_cell(uint32_t i, cell_t *c)
{
    static const char *const L[INFO_UA] = {"USB UNDERRUNS", "USB OVERRUNS", "MISSED FRAMES", "BAD PACKETS", "PACKETS IN",
                                           "RATE ASKED", "LOWEST BUFFER", "LONGEST GAP", "GAPS > 0.9 MS"};
    static const uint8_t WARN[INFO_UA] = {1, 1, 1, 1, 0, 0, 0, 0, 1};   /* (a counter of trouble: amber once not 0) */
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
            str_cpy(c->val, "NONE YET", sizeof c->val);
        break;
    case 7: v = ua.gap_max / 24u; info_num(c, v, " us"); break;
    default: v = ua.gap_n; info_num(c, v, ""); break;
    }
    c->col = WARN[i % INFO_UA] && v ? C_WARN : 0;
}
#else
#define INFO_UA 0u
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
#define INFO_C1 0u
#define INFO_RST 0u
#endif
/* SYSTEM's read-out pages (op_gldraw.c gl_info_draw; the user, 2026-10-15: "the 1st system screen would gain to be split in two
 * pages with easier-to-read text"), each drawn whole, text only:
 *   INFO (page 0, what a musician reads): the clock, the CPU (KNOB 2: now, max 5 s, max), the 2ND CORE (ON + load, OFF + why, NOT IN
 *        BUILD), DROPS, UNDERRUNS, GUARD (CPU_GUARD builds), the battery
 *   INTERNALS (page 1): LAST RESET, the longest flash interrupts-off time, the second core's internals (DUAL builds)
 *   USB STATS (page 2, USB audio builds): the USB audio counters */
#if FELUCCA_SYS_DEBUG
#define INFO_BASE (6u + (uint32_t)FELUCCA_CPU_GUARD)
#else
#define INFO_BASE 3u                                    /* (without the debug pages: CPU LOAD, UNDERRUNS, BATTERY) */
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
    static const uint8_t INFO[INFO_BASE] = {SI_MHZ, SI_CPU, SI_CPU1, SI_DROPS, SI_XRUN,
#if FELUCCA_CPU_GUARD
                                            SI_GUARD,
#endif
                                            SI_BATT};   /* (SI_CPU1: the 2ND CORE line, in every build; DROPS, UNDERRUNS: the overload monitor) */
#else
    static const uint8_t INFO[INFO_BASE] = {SI_CPU, SI_XRUN, SI_BATT};   /* (what a player wants, the user 2026-10-17) */
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
            str_cpy(c->val, "NOT RAISED", sizeof c->val);   /* (the read-back was wrong: one core, 2ND CORE OFF SUPPLY) */
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
/* the read-out pages (op_gldraw.c gl_info): 0 INFO, 1 INTERNALS, 2 USB STATS; their lines */
enum { INFO_PG_INFO, INFO_PG_DIAG, INFO_PG_USB };
static uint32_t info_lines(uint32_t pg)
{
    return !FELUCCA_SYS_DEBUG ? INFO_N : pg == INFO_PG_USB ? INFO_UA : pg == INFO_PG_DIAG ? INFO_DIAG : INFO_N;
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
    (void)pg;
    sys_info_cell(i, c);
}
/* setting it: a knob turned s (> 0 right), or 0: YES (steps round, toggles) */
static void sys_set(uint32_t it, int32_t s)
{
    switch (it) {
    case SI_COLOR:
        settings.palette = (settings.palette + (s < 0 ? NPALETTES - 1u : 1u)) % NPALETTES;
        palette_set(settings.palette);
        ui.force = 1;
        break;
#if FELUCCA_BRIGHT
    case SI_BRIGHT:
        bright_set(s ? (uint32_t)clamp((int32_t)bright_level() + (s > 0 ? 1 : -1), 1, 8) : bright_level() % 8u + 1u);
        break;
#endif
#if FELUCCA_LIGHTS
    case SI_LIGHTS:
    case SI_KEYS: {
        uint8_t *v = it == SI_LIGHTS ? &lights_lvl : &lights_keys;
        *v = (uint8_t)sys_step(*v, s, (int32_t)(it == SI_LIGHTS ? LIGHTS_N : KEYS_N) - 1);
        if (it == SI_KEYS && lights_keys && !lights_lvl)
            lights_lvl = LIGHTS_LOW;                    /* keys lit need a level: the lowest */
        break;
    }
#endif
    case SI_HOLD:                                       /* (kept in the settings word: settings_word.c) */
        hold_sel = OP_HOLD_SEL[sys_step(OP_HOLD_POS[hold_sel % HOLD_N], s, HOLD_N - 1)];
        break;
    case SI_KCOL:                                       /* (kept in the settings word: settings_word.c) */
        knob_colors = (uint8_t)sys_step(knob_colors, s, 1);
        ui.force = 1;                                   /* (every band's colours move) */
        break;
#if FELUCCA_MIDI_CLKOUT
    case SI_CLKOUT:                                     /* (kept in the settings word: settings_word.c) */
        clko_on = (uint8_t)sys_step(clko_on, s, 1);
        break;
#endif
    case SI_LOWCUT:
#if FELUCCA_BASSPLUS
        settings.lowcut = (uint32_t)sys_step((int32_t)(settings.lowcut % 3u), s, 2);   /* OFF / LOWCUT / BASS+ */
        fx_lowcut = (uint8_t)settings.lowcut;
#else
        settings.lowcut = s > 0 ? 1u : s < 0 ? 0u : !settings.lowcut;
        fx_lowcut = (uint8_t)(settings.lowcut != 0);
#endif
        break;
#if FELUCCA_CDC
    case SI_USB:
        usb_serial = (uint8_t)(s > 0 ? 1u : s < 0 ? 0u : !usb_serial);
        break;
#endif
#if FELUCCA_BLE
    case SI_BLE:                                        /* right ON, left OFF (OFF: a connected central is let go) */
        ble_midi_set((uint8_t)(s > 0 ? 1u : s < 0 ? 0u : !ble_on));
        break;
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
 * settings record gives (the user, 2026-10-13): panel.c settings_init (COLOR 4 MONO, LOWCUT OFF), bright.c bright_boot
 * (BRIGHT 8, full), the settings word read as 0 (settings_word.c: HOLD 350 ms, KNOB COLORS, CLK OUT, SERIAL, LIGHTS,
 * KEYS OFF; 2ND CORE AUTO). BLE is never reset: a reset must not drop a live connection. 1: it had one */
static int sys_factory(uint32_t it)
{
    switch (it) {
    case SI_COLOR:
        settings.palette = 4u;                          /* (panel.c settings_init: MONO) */
        palette_set(settings.palette);
        ui.force = 1;
        break;
#if FELUCCA_BRIGHT
    case SI_BRIGHT:
        bright_set(8u);
        break;
#endif
#if FELUCCA_LIGHTS
    case SI_LIGHTS:
        lights_lvl = LIGHTS_OFF;
        break;
    case SI_KEYS:
        lights_keys = KEYS_OFF;
        break;
#endif
    case SI_LOWCUT:
        settings.lowcut = 0;
        fx_lowcut = 0;
        break;
#if FELUCCA_CDC
    case SI_USB:
        usb_serial = 0;
        break;
#endif
    case SI_HOLD:
        hold_sel = 0;                                   /* (core/hold.h: 0 is 350 ms) */
        break;
    case SI_KCOL:
        knob_colors = 0;
        ui.force = 1;
        break;
#if FELUCCA_MIDI_CLKOUT
    case SI_CLKOUT:
        clko_on = 0;
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
#if FELUCCA_BLE
    if (bdv.open)
        return;                                         /* (the list: a knob does nothing) */
#endif
    if (s == OP_RESET && !d) {                          /* SHIFT held: no descriptor, the factory value */
        sys_factory(it);
        return;
    }
    if (d) {
        val_turn(d, vp, k, s, fine);
        sys_dirty = 1;
        return;
    }
    sys_set(it, s);
}
static int sys_yes(uint32_t r, uint32_t k, uint32_t ok)
{
    uint32_t it = sys_item(r, k);
    int16_t *vp;
    const param_desc_t *d = sys_desc(it, &vp);
#if FELUCCA_BLE
    if (bdv.open)
        return dev_yes(r, ok);
    if (it == SI_BLELIST) {                             /* YES on DEVICES: the list (BLUETOOTH's YES toggles it, as every on / off line) */
        dev_open();
        return 1;
    }
    if (it == SI_BLEST || it == SI_BLEPEER || it == SI_BLEMIDI)
        return 0;                                       /* (read-outs) */
#if BLE_CENTRAL
    if (it == SI_BLEFGT)
        return ble_forget_yes(r, k, ok);
    if (it == SI_BLEAUTO) {
        sys_set(it, 0);
        return 1;
    }
#endif
#endif
    if (it == SI_CALIB) {                               /* PANEL: asked first (it takes every button until the end) */
        if (!ok)
            op_arm(SCR_SYSTEM, r, k, "CALIBRATE", "PANEL", 0);
        else
            panel_setup();                              /* (op_input.c: teach each button and knob) */
        return 1;
    }
    if (it == SI_ABOUT) {
        ui_say(FELUCCA_VERSION, " OPTIMIST UI");
        return 1;
    }
    if (d) {
        if (!val_toggle(d, vp))
            return 0;
        sys_dirty = 1;
        return 1;
    }
    if (it == SI_NONE || (it >= SI_INFO && !si_ua(it)) || it == SI_CPU || it == SI_MHZ || it == SI_CPU1)
        return 0;                                       /* (the read-outs; USB AUDIO's lines, last, are settings) */
    sys_set(it, 0);
    return 1;
}
static void sys_leave(void)                             /* SYSTEM left: the settings into flash (as SLOOP's menu_close) */
{
#if FELUCCA_BLE
    dev_close();                                        /* (the devices list open: it closes, the scan stops) */
#endif
    if (!sys_dirty)
        return;
    sys_dirty = 0;
    if (song.playing || transport_req)
        settings_later = 1;                             /* (a flash write stops the audio: once stopped) */
    else
        settings_save();
}
