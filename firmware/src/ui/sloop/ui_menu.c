/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Copyright (C) 2026 isod89 (SLOOP 2.4, github.com/isod89/sloop-fm1 v2.4 8d3823f, ui_menu.c: the menu in sections,
 * SELECT turns them, the knobs set their rows, the values in large type) */
/* The HOME-held menu, in sections as SLOOP 2.4 groups it: SCREEN (COLOR, ZOOM, BRIGHT, VIEW), LIGHTS (LIGHTS, KEYS,
 * NOTES), AUDIO (LOWCUT) and SYSTEM, which has three screens: the MIDI and the clock (OUT, IN, SYNC, CLK), the MIDI
 * channels of the tracks (TRK 1..3, DRUMS), and USB SERIAL, CPU, HARDWARE CALIBRATION, ABOUT. Only what the build has
 * is shown. SELECT goes to the screen before / after (stopping at the ends), KNOB 1..4 set the rows in order (the
 * cursor follows the knob turned), PRESETS moves the cursor, OCT+ steps the cursor's setting round or opens it
 * (CALIBRATION, ABOUT), OCT- closes (from ABOUT: back to the menu). All of it is settings of the FM-1 (kept over
 * project loads), but the MIDI channels: those are the project's. */
/* ------------------------------------------------------------ menu --- */
#if FELUCCA_BRIGHT
#include "bright.c"            /* MENU > BRIGHT: the backlight level (after X0X) */
#endif
#ifndef FELUCCA_BLE
#define FELUCCA_BLE 0                              /* (a host test without the BLE code) */
#endif
enum { MI_COLOR, MI_ZOOM, MI_BRIGHT, MI_VIEW, MI_LIGHTS, MI_KEYS, MI_NOTES, MI_LOWCUT, MI_HOLD, MI_OUT, MI_IN, MI_SYNC, MI_CLK,
       MI_CH1, MI_CH2, MI_CH3, MI_CHD, MI_USB, MI_CPU, MI_PANEL, MI_ABOUT, MI_KCOL,
#if FELUCCA_BLE
       MI_BLE,                                     /* BLUETOOTH: with the radio built in (FELUCCA_BLE) */
       MI_BLEDEV,                                  /* DEVICES: NONE / LAST / nearby (io/midi/ble_devices.c) */
#endif
       MI_COUNT };
static const char *const MI_NAME[MI_COUNT] = {
    [MI_COLOR] = "COLOR", [MI_ZOOM] = "ZOOM", [MI_BRIGHT] = "BRIGHT", [MI_VIEW] = "VIEW", [MI_LIGHTS] = "LIGHTS",
    [MI_KEYS] = "KEYS", [MI_NOTES] = "NOTES", [MI_LOWCUT] = "LOWCUT", [MI_HOLD] = "HOLD", [MI_OUT] = "MIDI OUT", [MI_IN] = "MIDI IN",
    [MI_SYNC] = "SYNC", [MI_CLK] = "CLOCK", [MI_CH1] = "TRACK 1", [MI_CH2] = "TRACK 2", [MI_CH3] = "TRACK 3",
    [MI_CHD] = "DRUMS", [MI_USB] = "USB SERIAL", [MI_CPU] = "CPU", [MI_PANEL] = "CALIBRATION", [MI_ABOUT] = "ABOUT",
    [MI_KCOL] = "KNOB COLORS",
#if FELUCCA_BLE
    [MI_BLE] = "BLUETOOTH", [MI_BLEDEV] = "DEVICES",
#endif
};
#ifndef FELUCCA_CDC
#define FELUCCA_CDC 0                              /* (a host test without the USB code) */
#endif
#define MI_NONE 0xFFu
/* the screens: their section (tab) and their rows (an item the build lacks is MI_NONE: the row is not there) */
enum { MS_SCREEN, MS_LIGHTS, MS_AUDIO, MS_SYSTEM, MS_COUNT };
static const char *const MS_NAME[MS_COUNT] = {"SCREEN", "LIGHTS", "AUDIO", "SYSTEM"};
#define MI_IF(c, i) ((c) ? (uint8_t)(i) : MI_NONE)
#define MI_NSCR (7 + FELUCCA_BLE)
static const struct { uint8_t sec, item[4]; } MI_SCR[MI_NSCR] = {
    {MS_SCREEN, {MI_COLOR, MI_ZOOM, MI_IF(FELUCCA_BRIGHT, MI_BRIGHT), MI_IF(FELUCCA_OVERVIEW, MI_VIEW)}},
    {MS_LIGHTS, {MI_IF(FELUCCA_LIGHTS, MI_LIGHTS), MI_IF(FELUCCA_LIGHTS, MI_KEYS),
                 MI_IF(FELUCCA_LIGHTS && FELUCCA_KEYLIT, MI_NOTES), MI_NONE}},
    {MS_AUDIO, {MI_LOWCUT, MI_NONE, MI_NONE, MI_NONE}},
    {MS_SYSTEM, {MI_IF(FELUCCA_MIDI_OUT, MI_OUT), MI_IF(FELUCCA_MIDI_INCLK, MI_IN), MI_IF(FELUCCA_MIDI_CLOCK, MI_SYNC),
                 MI_IF(FELUCCA_MIDI_CLOCK, MI_CLK)}},
    {MS_SYSTEM, {MI_IF(FELUCCA_MIDI_CH, MI_CH1), MI_IF(FELUCCA_MIDI_CH, MI_CH2), MI_IF(FELUCCA_MIDI_CH, MI_CH3),
                 MI_IF(FELUCCA_MIDI_CH, MI_CHD)}},
    {MS_SYSTEM, {MI_HOLD, MI_KCOL, MI_NONE, MI_NONE}},   /* (KNOB COLORS: core/knobcol.h) */
    {MS_SYSTEM, {MI_IF(FELUCCA_CDC, MI_USB), MI_CPU, MI_PANEL, MI_ABOUT}},
#if FELUCCA_BLE
    {MS_SYSTEM, {MI_BLE, MI_BLEDEV, MI_NONE, MI_NONE}},    /* (the last screen: the radio, OFF by default; the list) */
#endif
};
#if FELUCCA_LIGHTS
static const char *const LIGHTS_NAME[LIGHTS_N] = {"OFF", "LOW", "MID", "HIGH"};   /* every button lit, the labels readable */
static const char *const KEYS_NAME[KEYS_N] = {"OFF", "C KEYS", "WHITE KEYS", "ALL KEYS"};      /* keys lit too, at the LIGHTS level */
#endif
#if FELUCCA_BASSPLUS
static const char *const LOWCUT_N[3] = {"OFF", "LOWCUT", "BASS+"};   /* settings.lowcut (fx.c, bassplus.c) */
#endif
#if FELUCCA_BLE
#define BLE_ST_N 9u
static const char *const BLE_STATUS_NAME[BLE_ST_N] = {"", "VISIBLE", "CONNECTED", "NO RF CAL", "SCANNING",
                                                      "CONNECTING", "SEARCHING", "PAIRING", "FAILED"};
static uint32_t ble_status(void)                   /* 0 off (or ON but not started this boot: midi_ble.c ble_up), 1
                                                    * advertising, 2 a link is up (either role; ours once ready), 3 no
                                                    * stored RF trims: the radio never starts (midi_ble.c ble_radio_ok),
                                                    * 4 scanning (the DEVICES list open), 5 connecting (to a pick, or
                                                    * our link setting up), 6 searching for LAST, 7 pairing, 8 the last
                                                    * attempt failed (kept until the user acts) (BLE_CENTRAL:
                                                    * ble_devices.c ble_seeking, ble_connect.c ble_connect_ui) */
{
    int s = ble_seeking();
    uint32_t u = 0;
#if BLE_CENTRAL
    u = ble_on && ble_up ? ble_connect_ui() : 0u;
#endif
    return !ble_radio_ok() ? 3u : !ble_on || !ble_up ? 0u : u == 2u ? 7u : u == 1u ? 5u : ble_connected() ? 2u :
           s ? (uint32_t)(4 + s) : u == 3u ? 8u : ble_scanning() ? 4u : 1u;
}
#endif
#define MI_Y0 26                                   /* the first row, under the section tabs */
#define MI_DY 38                                   /* a row: its label left, its value in large type right */

static uint32_t mi_rows(uint32_t scr, uint8_t *it)    /* the rows present on a screen: items into it[4] */
{
    uint32_t k, n = 0;
    for (k = 0; k < 4u; k++)
        if (MI_SCR[scr].item[k] != MI_NONE)
            it[n++] = MI_SCR[scr].item[k];
    return n;
}
static uint32_t mi_screen_of(uint32_t item)        /* the screen an item is on (the first one when on none) */
{
    uint32_t s, k;
    for (s = 0; s < MI_NSCR; s++)
        for (k = 0; k < 4u; k++)
            if (MI_SCR[s].item[k] == item)
                return s;
    return 0;
}
/* the row of an item on its screen (the knob 1 + row sets it) */
static uint32_t mi_row(uint32_t item)
{
    uint8_t it[4];
    uint32_t n = mi_rows(mi_screen_of(item), it), k;
    for (k = 0; k < n; k++)
        if (it[k] == item)
            return k;
    return 0;
}
/* the value of a MIDI channel row: 0 OFF, 1..16 */
static int16_t *mi_chan(uint32_t item)
{
#if FELUCCA_MIDI_CH
    return item == MI_CHD ? &song.g[G_DRCH] : &bp_set[BPS_CH0 + (item - MI_CH1)];
#else
    (void)item;
    return 0;
#endif
}

/* item i's value as shown (v: room for 12 characters), and its colour */
static const char *mi_value(uint32_t i, char *v, uint16_t *c)
{
    const char *unit;
    *c = C_HI;
    switch (i) {
    case MI_COLOR: return PALETTES[settings.palette].name;
    case MI_ZOOM: return settings.zoom ? "ON" : "OFF";
    case MI_VIEW: return N_VIEW[settings.view ? 1 : 0];
    case MI_LIGHTS:
#if FELUCCA_LIGHTS
        return LIGHTS_NAME[lights_lvl % LIGHTS_N];
    case MI_KEYS:
        *c = lights_lvl ? C_HI : C_DIM;               /* (needs LIGHTS) */
        return KEYS_NAME[lights_keys % KEYS_N];
    case MI_NOTES: return lights_notes_off ? "OFF" : "ON";
#else
    case MI_KEYS:
    case MI_NOTES: return "";
#endif
    case MI_LOWCUT:
#if FELUCCA_BASSPLUS
        return LOWCUT_N[settings.lowcut % 3u];
#else
        return settings.lowcut ? "ON" : "OFF";
#endif
    case MI_KCOL: return knob_colors ? "ON" : "OFF";
    case MI_HOLD:                                     /* ms: 250 / 350 / 500 */
        v[0] = (char)('0' + HOLD_MS / 100u);
        v[1] = (char)('0' + HOLD_MS / 10u % 10u);
        v[2] = (char)('0' + HOLD_MS % 10u);
        v[3] = 0;
        return v;
    case MI_BRIGHT:
#if FELUCCA_BRIGHT
        v[0] = (char)('0' + bright_level());
        v[1] = 0;
        return v;
#else
        return "";
#endif
#if FELUCCA_MIDI_OUT
    case MI_OUT: return bp_set[BPS_MOUT] ? "SEQ" : "KEYS";
#endif
#if FELUCCA_MIDI_INCLK
    case MI_IN: return bp_set[BPS_MIN] ? "CLOCK" : "NOTES";
#endif
    case MI_SYNC: return N_SYNC[(uint32_t)song.g[G_SYNC] & 3u];
    case MI_CLK:
        *c = C_AMB;                                   /* (read-only: the clock followed now) */
        return N_CLKSRC[(uint32_t)song.g[G_MIDI] % 3u];
#if FELUCCA_MIDI_CH
    case MI_CH1: case MI_CH2: case MI_CH3: case MI_CHD:
        return N_MCH[(uint32_t)clamp(*mi_chan(i), 0, 16)];
#endif
    case MI_USB:
#if FELUCCA_CDC
        *c = usb_serial == usb_cdc_on ? C_HI : C_AMB;
        return usb_serial ? "ON" : "OFF";
#else
        return "";
#endif
#if FELUCCA_BLE
    case MI_BLE:
        *c = ble_on ? C_HI : C_AMB;
        return ble_on ? "ON" : "OFF";
#endif
    case MI_CPU:
        cpu_info(v, &unit);
        str_cpy(v + str_len(v), unit, 6);
        return v;
    default:
        *c = C_DIM;
        return "";                                    /* (an action: OCT+ opens it) */
    }
}

static void enc_drop(void);

#if FELUCCA_BLE
/* ---- DEVICES (ui.menu 3, docs/BLE-DEVICES-DESIGN.md §2.3): NONE, LAST, the nearby BLE-MIDI devices. PRESETS or SELECT
 * move the cursor (stopping at the ends), OCT+ picks the row (ble_devices.c ble_dev_pick), KNOB 4 turned on LAST arms
 * FORGET (OCT+ within 3 s does it; SLOOP's menu has no modal), OCT- back to the menu (the scan stops). */
static uint8_t mdev_cur;                           /* the cursor row */
static uint32_t mdev_forget_ms;                    /* FORGET armed at (0: not) */
#define MDEV_ROWS 8u                               /* rows shown (the list scrolls under the cursor) */
#define MDEV_Y0 4
#define MDEV_DY 18
#define MDEV_FORGET_MS 3000u

static int mdev_forget_armed(void) { return mdev_forget_ms && fm1_ms - mdev_forget_ms < MDEV_FORGET_MS; }

static const char *mdev_state(uint16_t *c)         /* the header's right: what the radio does */
{
    uint32_t st = ble_status();
    *c = st == 0u || st == 3u || st == 8u ? C_AMB : st == 1u || st >= 5u ? C_DIM : C_HI;
    return st ? BLE_STATUS_NAME[st % BLE_ST_N] : "OFF";
}

/* the signal of a nearby row: relative bars, dim (no RSSI gain table yet: U9), "--" when not heard in this scan */
static void mdev_bars(int32_t x, int32_t y, uint32_t n)
{
    int32_t k;
    if (!n) {
        cv_text(x, y, &FONT_S, "--", C_DIM);
        return;
    }
    for (k = 0; k < 3; k++)
        cv_rect(x + k * 6, y + 12 - 4 * k, 4, 4 + 4 * k, (uint32_t)k < n ? C_GRAY : C_LINE);
}

#if BLE_CENTRAL
static int mdev_found_at(const uint8_t a[6], uint8_t rnd)   /* the nearby entry with this address, else -1 */
{
    uint32_t i;
    for (i = 0; i < BLE_SCAN_N; i++)
        if (ble_found.e[i].used && ble_found.e[i].midi && ble_found.e[i].addr_rand == rnd &&
            !memcmp(ble_found.e[i].addr, a, 6))
            return (int)i;
    return -1;
}
#endif

/* row r's text, its tag (LAST, or CONNECTED / CONNECTING / PAIRING: a long tag takes the bars' place), whether it is
 * the choice, its bars */
static void mdev_row(uint32_t r, int last, const uint8_t *near, char *nm, const char **tag, int *chosen,
                     uint32_t *bars)
{
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
        if (ble_connect_last_up())
            *tag = "CONNECTED";           /* (our link to it is up) */
#endif
        *chosen = ble_store.sel == BLE_SEL_LAST;
#if BLE_CENTRAL
        {
            int f = mdev_found_at(ble_store.dev.addr, ble_store.dev.info & BLE_DEV_RANDOM);
            *bars = f >= 0 ? ble_scan_bars(&ble_found, (uint32_t)f) : 0u;
        }
#endif
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
            *tag = ble_status() == 7u ? "PAIRING" : "CONNECTING";
    }
#else
    (void)near;
#endif
}

static uint32_t mdev_sig(void)                     /* what the list shows: it redraws when this changes */
{
    uint32_t sig = mdev_cur * 7u + ble_store.sel * 13u + (uint32_t)ble_store_has_last(&ble_store) * 17u +
                   ble_status() * 19u + (uint32_t)mdev_forget_armed() * 23u + (uint32_t)ble_on * 29u;
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

static void draw_devices(void)                     /* the list, in the menu's body coordinates (both bands) */
{
    int last, chosen;
    uint8_t near[BLE_SCAN_N];
    uint32_t n_near, n = ble_dev_rows(&last, near, &n_near), r, first, bars;
    char nm[BLE_NAME_MAX + 4u], st[32];
    const char *tag, *m;
    uint16_t sc = C_DIM;
#if BLE_CENTRAL
    uint32_t k, pk;
#endif
    if (mdev_cur >= n)
        mdev_cur = (uint8_t)(n - 1u);
    first = mdev_cur >= MDEV_ROWS ? mdev_cur - (MDEV_ROWS - 1u) : 0u;
    for (r = first; r < n && r < first + MDEV_ROWS; r++) {
        int32_t y = MDEV_Y0 + (int32_t)(r - first) * MDEV_DY;
        int cur = r == mdev_cur;
        mdev_row(r, last, near, nm, &tag, &chosen, &bars);
        cv_rect(4, y + 1, 3, 14, cur ? C_WHITE : C_BLACK);
        cv_text(12, y, &FONT_S, nm, cur ? C_WHITE : chosen ? C_HI : C_GRAY);
        if (chosen)
            cv_rect(143, y + 5, 6, 6, C_HI);      /* the choice (NONE or LAST) */
        if (tag[0])
            cv_text(152, y, &FONT_S, tag, C_AMB);
        if (r && str_len(tag) <= 4u)               /* (CONNECTED / CONNECTING / PAIRING take the bars' place) */
            mdev_bars(212, y, bars);
    }
    cv_rect(0, 150, 240, 1, C_LINE);
    if (mdev_forget_armed()) {
        str_cpy(st, "OCT+ FORGETS ", sizeof st);
        ble_store_name(&ble_store, nm);
        str_cpy(st + str_len(st), nm, sizeof st - str_len(st));
        str_cpy(st + str_len(st), "?", sizeof st - str_len(st));
        sc = C_AMB;
    } else if ((m = ble_dev_message()) != 0) {
        str_cpy(st, m, sizeof st);
        sc = C_HI;
    } else if (!ble_on) {
        str_cpy(st, "BLUETOOTH IS OFF", sizeof st);
        sc = C_AMB;
#if BLE_CENTRAL
    } else if ((k = ble_connect_status(st, sizeof st)) != RCS_NONE) {   /* connecting out: under way, CONNECTED <name>,
                                                    * FAILED: <why> (kept until the user acts) */
        sc = k == RCS_GOOD || ble_connect_passkey() != BLE_NO_PASSKEY ? C_HI : k == RCS_BAD ? C_AMB : C_DIM;
    } else if (ble_connect_last_up()) {            /* our link: to LAST (a pick that became LAST) */
        str_cpy(st, "CONNECTED ", sizeof st);
        ble_store_name(&ble_store, nm);
        str_cpy(st + str_len(st), nm, sizeof st - str_len(st));
        sc = C_HI;
    } else if (ble_seeking()) {
        str_cpy(st, ble_seeking() == 1 ? "CONNECTING ..." : "SEARCHING FOR LAST", sizeof st);
#endif
    } else if (ble_connected()) {
        str_cpy(st, "CONNECTED: NO SCAN", sizeof st);
        sc = C_HI;
    } else if (ble_scanning()) {
        str_cpy(st, "SCANNING  ", sizeof st);
        fmt_int(st + str_len(st), (int32_t)n_near);
        str_cpy(st + str_len(st), " FOUND", sizeof st - str_len(st));
    } else
        str_cpy(st, BLE_CENTRAL ? "VISIBLE" : "VISIBLE (NO SCAN BUILT IN)", sizeof st);
    cv_text(4, 154, &FONT_S, st, sc);
#if BLE_CENTRAL
    if ((pk = ble_connect_passkey()) != BLE_NO_PASSKEY) {   /* the passkey, large, in place of the keys' help */
        char d[7];
        int i;
        for (i = 5; i >= 0; i--, pk /= 10u)
            d[i] = (char)('0' + pk % 10u);
        d[6] = 0;
        cv_text((240 - text_w(&FONT_L, d)) / 2, 172, &FONT_L, d, C_HI);
        return;
    }
#endif
    cv_text(4, 172, &FONT_S, "PRESETS MOVE   OCT+ PICK", C_DIM);
    cv_text(4, 188, &FONT_S, "K4 FORGET      OCT- BACK", C_DIM);
}

static void devices_input(uint32_t ok)
{
    int last;
    int32_t s;
    uint8_t near[BLE_SCAN_N];
    uint32_t n_near, n = ble_dev_rows(&last, near, &n_near);
    if ((s = panel_enc(EN_PRESET)) != 0 || (s = panel_enc(EN_SELECT)) != 0) {
        mdev_cur = (uint8_t)clamp((int32_t)mdev_cur + (s > 0 ? 1 : -1), 0, (int32_t)n - 1);
        mdev_forget_ms = 0;
    }
    if ((s = panel_enc(EN_K1 + 3u)) != 0 && (int)mdev_cur == last)
        mdev_forget_ms = fm1_ms | 1u;             /* KNOB 4 on LAST: FORGET armed */
    if (ok) {
        if (mdev_forget_armed() && (int)mdev_cur == last) {
            ble_dev_forget();
            mdev_cur = 0;
        } else
            ble_dev_pick(mdev_cur);
        mdev_forget_ms = 0;
    }
    enc_drop();
}
#endif

static void draw_menu(void)
{
    uint32_t i, pass, scr = mi_screen_of(ui.menu_sel % MI_COUNT), sec = MI_SCR[scr].sec, n;
    uint32_t sig = ui.menu * 7u + ui.menu_sel * 131u;
    uint8_t it[4];
    char vb[4][14];
    uint16_t vcol[4];
    const char *vs[4];
    n = mi_rows(scr, it);
    for (i = 0; i < n; i++) {                       /* what shows: the menu redraws when any of it changes */
        const char *p;
        vs[i] = mi_value(it[i], vb[i], &vcol[i]);
        for (p = vs[i]; *p; p++)
            sig = sig * 31u + (uint8_t)*p;
        sig = sig * 31u + vcol[i];
    }
    sig += settings.palette * 1009u + hold_sel * 7919u + knob_colors * 104729u;
#if FELUCCA_CDC
    sig += (usb_serial != usb_cdc_on) * 86028121u;
#endif
#if FELUCCA_BLE
    sig += (uint32_t)ble_status() * 179424673u;
    if (ui.menu == 3)
        sig = sig * 31u + mdev_sig();
#endif
    if (!ui.force && sig == ui.menu_sig)
        return;
    ui.menu_sig = sig;
    if (ui.force)                                   /* head + rule + two bands cover rows 0..229 */
        lcd_fill(0, H_HEAD + 1 + 124 + 95, 240, 240 - (H_HEAD + 1 + 124 + 95), C_BLACK);
    cv_begin(240, H_HEAD, C_BLACK);
    cv_text(4, 1, &FONT_S, ui.menu == 2 ? "ABOUT" : ui.menu == 3 ? "DEVICES" : "MENU", C_HI);
#if FELUCCA_BLE
    if (ui.menu == 3) {                             /* the list: what the radio does, at the right */
        uint16_t c;
        const char *t = mdev_state(&c);
        cv_text(236 - text_w(&FONT_S, t), 1, &FONT_S, t, c);
    } else
#endif
    if (ui.menu != 2) {                             /* the section, and its screen when it has several (as "ENV DEST 2/2") */
        char t[20];
        uint32_t first = 0, cnt = 0, k;
        for (k = 0; k < MI_NSCR; k++)
            if (MI_SCR[k].sec == sec) {
                uint8_t tmp[4];
                if (mi_rows(k, tmp)) {
                    if (!cnt)
                        first = k;
                    cnt++;
                }
            }
        str_cpy(t, MS_NAME[sec], sizeof t);
        if (cnt > 1u) {
            str_cpy(t + str_len(t), " ", 2);
            fmt_int(t + str_len(t), (int32_t)(scr - first) + 1);
            str_cpy(t + str_len(t), "/", 2);
            fmt_int(t + str_len(t), (int32_t)cnt);
        }
        cv_text(236 - text_w(&FONT_S, t), 1, &FONT_S, t, C_GRAY);
    }
    cv_blit(0, Y_HEAD);
    lcd_fill(0, H_HEAD, 240, 1, C_LINE);
    for (pass = 0; pass < 2u; pass++) {             /* the canvas holds 124 rows: draw in two bands */
        cv_begin(240, pass ? 95u : 124u, C_BLACK);
        cv_oy = pass ? -124 : 0;
        if (ui.menu == 2) {
            cv_text(4, 4, &FONT_L, "OPTIMIST", C_WHITE);
            cv_rect(140, 10, 8, 4, C_DIM), cv_rect(140, 16, 12, 4, C_GRAY);   /* the sail (the palette's steps) */
            cv_rect(140, 22, 16, 4, C_AMB), cv_rect(140, 28, 20, 4, C_HI);
            cv_text(4, 36, &FONT_S, "BASED ON SLOOP + FELUCCA", C_AMB);
            cv_text(4, 54, &FONT_S, FELUCCA_VERSION, C_HI);
            cv_text(236 - text_w(&FONT_S, __DATE__), 54, &FONT_S, __DATE__, C_GRAY);   /* build date */
            cv_text(cv_text(4, 72, &FONT_S, "LEO KUROSHITA", C_HI) + 8, 72, &FONT_S, "@KUROGEDELIC", C_AMB);
            cv_text(4, 88, &FONT_S, "H\xDCGELTON INSTRUMENTS", C_HI);   /* Latin-1 U-umlaut */
            cv_text(4, 104, &FONT_S, "HUGELTON.COM", C_AMB);
            cv_text(4, 119, &FONT_S, "GPL-3.0, NO WARRANTY", C_HI);
            cv_text(4, 132, &FONT_S, "SLOOP: GITHUB.COM/ISOD89", C_AMB);   /* (where it comes from) */
            cv_text(4, 146, &FONT_S, "FONT: TERMINUS (OFL)", C_DIM);
            cv_text(4, 159, &FONT_S, "SAMPLES: VERSILIAN (CC0)", C_DIM);
            cv_text(4, 172, &FONT_S, "+ SONIC PI (CC0)", C_DIM);
            cv_text(4, 185, &FONT_S, "PHASE: CRISPYZEBRA (GPL)", C_DIM);
            cv_text(4, 198, &FONT_S, "VOICE: REF. KLATTSCH (MIT)", C_DIM);
#if FELUCCA_BLE
        } else if (ui.menu == 3) {
            draw_devices();
#endif
        } else {
            int32_t x = 4;
            for (i = 0; i < MS_COUNT; i++) {        /* the sections as tabs: this one lit */
                int32_t w = text_w(&FONT_S, MS_NAME[i]);
                if (i == sec)
                    cv_rect(x - 2, 3, w + 4, 18, C_LINE);
                cv_text(x, 4, &FONT_S, MS_NAME[i], i == sec ? C_WHITE : C_DIM);
                x += w + 10;
            }
            for (i = 0; i < n; i++) {
                int32_t y = MI_Y0 + (int32_t)i * MI_DY, xv;
                int cur = it[i] == ui.menu_sel;
                char k[3] = {'K', (char)('1' + i), 0};
                uint16_t vc = cur && vcol[i] == C_HI ? C_WHITE : vcol[i];
                cv_rect(4, y + 2, 3, MI_DY - 8, cur ? C_WHITE : C_DIM);   /* the cursor / the row's knob */
                xv = cv_text(14, y, &FONT_S, k, C_AMB);
                cv_text(xv + 6, y, &FONT_S, MI_NAME[it[i]], cur ? C_WHITE : C_GRAY);
                if (vs[i][0] && text_w(&FONT_L, vs[i]) <= 130) {
                    cv_text(232 - text_w(&FONT_L, vs[i]), y - 4, &FONT_L, vs[i], vc);
                } else if (vs[i][0]) {              /* (too wide for the large type: WHITE KEYS) */
                    cv_text(232 - text_w(&FONT_S, vs[i]), y + 10, &FONT_S, vs[i], vc);
                } else {
                    cv_text(14, y + 18, &FONT_S, "OCT+ OPENS", cur ? C_WHITE : C_DIM);
                }
#if FELUCCA_CDC
                if (it[i] == MI_USB && usb_serial != usb_cdc_on)
                    cv_text(14, y + 18, &FONT_S, "RESTART", C_AMB);   /* (usb.c: at the next start) */
#endif
#if FELUCCA_BLE
                if (it[i] == MI_BLE)                /* what the radio is doing, under the setting */
                    cv_text(14, y + 18, &FONT_S, BLE_STATUS_NAME[ble_status()],
                            ble_status() == 2 ? C_HI : ble_status() == 3 ? C_AMB : C_DIM);
#endif
                if (it[i] == MI_COLOR) {            /* the palette's colours */
                    uint32_t q;
                    for (q = 0; q < 5u; q++)
                        cv_rect(14 + (int32_t)q * 14, y + 20, 10, 10, pal[q]);
                }
            }
            cv_text(4, MI_Y0 + 4 * MI_DY + 4, &FONT_S, "SELECT SECTION  KNOB SETS", C_DIM);
            cv_text(4, MI_Y0 + 4 * MI_DY + 20, &FONT_S, "OCT+ OK   OCT- CLOSE", C_DIM);   /* (rows .. 218) */
        }
        cv_oy = 0;
        cv_blit(0, H_HEAD + 1 + pass * 124u);
    }
}

static void enc_drop(void)                             /* knob turns nobody takes */
{
    uint32_t k;
    for (k = 0; k < NE; k++)
        panel_enc(k);
}

static void menu_close(void)
{
#if FELUCCA_BLE
    if (ui.menu == 3)
        ble_devices_open(0);                           /* (the scan stops, advertising goes on) */
#endif
    if (song.playing || transport_req)
        settings_later = 1;                            /* (a flash write stops the audio: once stopped) */
    else
        settings_save();                               /* palette / panel table, if changed */
    ui.menu = 0;
    ui.force = 1;
    go_home();
}

/* v moved by a knob turn of s detents (> 0 right, < 0 left; 0: OCT+) in 0..max: a knob stops at the ends, OCT+ goes round */
static int32_t mi_step(int32_t v, int32_t s, int32_t max)
{
    return s ? clamp(v + s, 0, max) : (v >= max ? 0 : v + 1);
}
/* item i: a knob turned s (> 0 right, < 0 left), or 0: OCT+ (steps round, toggles, or opens) */
static void mi_set(uint32_t i, int32_t s)
{
    switch (i) {
    case MI_COLOR:                                     /* both ways round; OCT+ the next */
        settings.palette = (settings.palette + (s < 0 ? NPALETTES - 1u : 1u)) % NPALETTES;
        palette_set(settings.palette);
        break;
    case MI_ZOOM:
        settings.zoom = s > 0 ? 1u : s < 0 ? 0u : !settings.zoom;
        break;
    case MI_VIEW:                                      /* PAGE / ALL: the setting and the editor's G_VIEW */
        settings.view = s > 0 ? 1u : s < 0 ? 0u : !settings.view;
        song.g[G_VIEW] = (int16_t)settings.view;
        break;
    case MI_LOWCUT:
#if FELUCCA_BASSPLUS
        settings.lowcut = (uint32_t)mi_step((int32_t)(settings.lowcut % 3u), s, 2);   /* OFF / LOWCUT / BASS+ */
        fx_lowcut = (uint8_t)settings.lowcut;
#else
        settings.lowcut = s > 0 ? 1u : s < 0 ? 0u : !settings.lowcut;
        fx_lowcut = (uint8_t)(settings.lowcut != 0);
#endif
        break;
    case MI_KCOL:                                      /* right ON, left OFF (the caps' colours: core/knobcol.h) */
        knob_colors = (uint8_t)(s > 0 ? 1u : s < 0 ? 0u : !knob_colors);
        break;
    case MI_HOLD: {                                    /* 250 / 350 / 500 ms, in order (hold_sel 1, 0, 2) */
        static const uint8_t ORD[HOLD_N] = {1, 0, 2}, POS[HOLD_N] = {1, 0, 2};
        hold_sel = ORD[(uint32_t)mi_step((int32_t)POS[hold_sel % HOLD_N], s, (int32_t)HOLD_N - 1)];
        break;
    }
#if FELUCCA_BRIGHT
    case MI_BRIGHT:                                    /* 1..8 */
        bright_set(s ? (uint32_t)clamp((int32_t)bright_level() + s, 1, 8) : bright_level() % 8u + 1u);
        break;
#endif
#if FELUCCA_LIGHTS
    case MI_LIGHTS:
    case MI_KEYS: {                                    /* brighter / dimmer (stops at the ends); OCT+ steps round */
        uint8_t *v = i == MI_LIGHTS ? &lights_lvl : &lights_keys;
        *v = (uint8_t)mi_step(*v, s, (int32_t)(i == MI_LIGHTS ? LIGHTS_N : KEYS_N) - 1);
        if (i == MI_KEYS && lights_keys && !lights_lvl)
            lights_lvl = LIGHTS_LOW;                   /* keys lit need a level: the lowest */
        break;
    }
    case MI_NOTES:                                     /* right ON, left OFF */
        lights_notes_off = (uint8_t)(s > 0 ? 0u : s < 0 ? 1u : !lights_notes_off);
        break;
#endif
#if FELUCCA_MIDI_OUT
    case MI_OUT: bp_set[BPS_MOUT] = (int16_t)(s > 0 ? 1 : s < 0 ? 0 : !bp_set[BPS_MOUT]); break;
#endif
#if FELUCCA_MIDI_INCLK
    case MI_IN: bp_set[BPS_MIN] = (int16_t)(s > 0 ? 1 : s < 0 ? 0 : !bp_set[BPS_MIN]); break;
#endif
#if FELUCCA_MIDI_CLOCK
    case MI_SYNC: song.g[G_SYNC] = (int16_t)mi_step(song.g[G_SYNC], s, SYNC_AUTO); break;
#endif
#if FELUCCA_MIDI_CH
    case MI_CH1: case MI_CH2: case MI_CH3: case MI_CHD: {      /* OFF, 1..16 */
        int16_t *p = mi_chan(i);
        *p = (int16_t)mi_step(*p, s, 16);
        break;
    }
#endif
#if FELUCCA_CDC
    case MI_USB:
        usb_serial = (uint8_t)(s > 0 ? 1u : s < 0 ? 0u : !usb_serial);
        break;
#endif
#if FELUCCA_BLE
    case MI_BLE:                                       /* right ON, left OFF (OFF: a connected central is let go) */
        ble_midi_set((uint8_t)(s > 0 ? 1u : s < 0 ? 0u : !ble_on));
        break;
    case MI_BLEDEV:                                    /* an action: OCT+ opens the list (and the scan) */
        if (!s) {
            mdev_cur = 0;
            mdev_forget_ms = 0;
            ui.menu = 3;
            ui.force = 1;
            ble_devices_open(1);
        }
        break;
#endif
    case MI_PANEL:                                     /* actions: OCT+ only */
        if (!s) {
            panel_setup();
            ui.force = 1;
        }
        break;
    case MI_ABOUT:
        if (!s) {
            ui.menu = 2;
            ui.force = 1;
        }
        break;
    default:
        break;
    }
}

/* menu: SELECT the section, KNOB 1..4 its rows, PRESETS the cursor, OCT+ ok, OCT- close (ABOUT -> the menu) */
static void menu_input(uint32_t pressed)
{
    int32_t s;
    uint32_t k, ok = (pressed >> panel.btn[B_OCTUP]) & 1u, back = (pressed >> panel.btn[B_OCTDN]) & 1u;
    uint32_t sel = ui.menu_sel % MI_COUNT, scr = mi_screen_of(sel), n;
    uint8_t it[4];
    if (back) {
#if FELUCCA_BLE
        if (ui.menu == 3) {
            ble_devices_open(0);
            ui.menu = 1, ui.force = 1;
            return;
        }
#endif
        if (ui.menu == 2)
            ui.menu = 1, ui.force = 1;
        else
            menu_close();
        return;
    }
#if FELUCCA_BLE
    if (ui.menu == 3) {
        devices_input(ok);
        return;
    }
#endif
    if (ui.menu != 1) {
        enc_drop();
        return;
    }
    n = mi_rows(scr, it);
    if ((s = panel_enc(EN_SELECT)) != 0) {             /* the screen before / after (stopping at the ends) */
        int32_t to = (int32_t)scr;
        for (;;) {
            uint8_t tmp[4];
            to += s > 0 ? 1 : -1;
            if (to < 0 || to >= MI_NSCR) {
                to = -1;
                break;
            }
            if (mi_rows((uint32_t)to, tmp))
                break;
        }
        if (to >= 0) {
            mi_rows((uint32_t)to, it);
            ui.menu_sel = it[0];
            ui.force = 1;
        }
        enc_drop();
        return;
    }
    if ((s = panel_enc(EN_PRESET)) != 0) {             /* the cursor, round the screen's rows */
        uint32_t r = mi_row(sel);
        ui.menu_sel = it[(r + (s > 0 ? 1u : n - 1u)) % n];
    }
    for (k = 0; k < 4u; k++)
        if ((s = panel_enc(EN_K1 + k)) != 0 && k < n) {
            ui.menu_sel = it[k];                       /* (the cursor follows the knob turned) */
            mi_set(it[k], s);
        }
    if (ok)
        mi_set(ui.menu_sel % MI_COUNT, 0);
    enc_drop();                                        /* swallow the rest while the menu is up */
}
