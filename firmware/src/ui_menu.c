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
enum { MI_COLOR, MI_ZOOM, MI_BRIGHT, MI_VIEW, MI_LIGHTS, MI_KEYS, MI_NOTES, MI_LOWCUT, MI_OUT, MI_IN, MI_SYNC, MI_CLK,
       MI_CH1, MI_CH2, MI_CH3, MI_CHD, MI_USB, MI_CPU, MI_PANEL, MI_ABOUT, MI_COUNT };
static const char *const MI_NAME[MI_COUNT] = {
    [MI_COLOR] = "COLOR", [MI_ZOOM] = "ZOOM", [MI_BRIGHT] = "BRIGHT", [MI_VIEW] = "VIEW", [MI_LIGHTS] = "LIGHTS",
    [MI_KEYS] = "KEYS", [MI_NOTES] = "NOTES", [MI_LOWCUT] = "LOWCUT", [MI_OUT] = "MIDI OUT", [MI_IN] = "MIDI IN",
    [MI_SYNC] = "SYNC", [MI_CLK] = "CLOCK", [MI_CH1] = "TRACK 1", [MI_CH2] = "TRACK 2", [MI_CH3] = "TRACK 3",
    [MI_CHD] = "DRUMS", [MI_USB] = "USB SERIAL", [MI_CPU] = "CPU", [MI_PANEL] = "CALIBRATION", [MI_ABOUT] = "ABOUT"};
#define MI_NONE 0xFFu
/* the screens: their section (tab) and their rows (an item the build lacks is MI_NONE: the row is not there) */
enum { MS_SCREEN, MS_LIGHTS, MS_AUDIO, MS_SYSTEM, MS_COUNT };
static const char *const MS_NAME[MS_COUNT] = {"SCREEN", "LIGHTS", "AUDIO", "SYSTEM"};
#define MI_IF(c, i) ((c) ? (uint8_t)(i) : MI_NONE)
#define MI_NSCR 6
static const struct { uint8_t sec, item[4]; } MI_SCR[MI_NSCR] = {
    {MS_SCREEN, {MI_COLOR, MI_ZOOM, MI_IF(FELUCCA_BRIGHT, MI_BRIGHT), MI_IF(FELUCCA_OVERVIEW, MI_VIEW)}},
    {MS_LIGHTS, {MI_IF(FELUCCA_LIGHTS, MI_LIGHTS), MI_IF(FELUCCA_LIGHTS, MI_KEYS),
                 MI_IF(FELUCCA_LIGHTS && FELUCCA_KEYLIT, MI_NOTES), MI_NONE}},
    {MS_AUDIO, {MI_LOWCUT, MI_NONE, MI_NONE, MI_NONE}},
    {MS_SYSTEM, {MI_IF(FELUCCA_MIDI_OUT, MI_OUT), MI_IF(FELUCCA_MIDI_INCLK, MI_IN), MI_IF(FELUCCA_MIDI_CLOCK, MI_SYNC),
                 MI_IF(FELUCCA_MIDI_CLOCK, MI_CLK)}},
    {MS_SYSTEM, {MI_IF(FELUCCA_MIDI_CH, MI_CH1), MI_IF(FELUCCA_MIDI_CH, MI_CH2), MI_IF(FELUCCA_MIDI_CH, MI_CH3),
                 MI_IF(FELUCCA_MIDI_CH, MI_CHD)}},
    {MS_SYSTEM, {MI_IF(FELUCCA_CDC, MI_USB), MI_CPU, MI_PANEL, MI_ABOUT}},
};
#if FELUCCA_LIGHTS
static const char *const LIGHTS_NAME[LIGHTS_N] = {"OFF", "LOW", "MID", "HIGH"};   /* every button lit, the labels readable */
static const char *const KEYS_NAME[KEYS_N] = {"OFF", "C KEYS", "WHITE KEYS"};      /* keys lit too, at the LIGHTS level */
#endif
#if FELUCCA_BASSPLUS
static const char *const LOWCUT_N[3] = {"OFF", "LOWCUT", "BASS+"};   /* settings.lowcut (fx.c, bassplus.c) */
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
    case MI_CPU:
        cpu_info(v, &unit);
        str_cpy(v + str_len(v), unit, 6);
        return v;
    default:
        *c = C_DIM;
        return "";                                    /* (an action: OCT+ opens it) */
    }
}

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
    sig += settings.palette * 1009u;
#if FELUCCA_CDC
    sig += (usb_serial != usb_cdc_on) * 86028121u;
#endif
    if (!ui.force && sig == ui.menu_sig)
        return;
    ui.menu_sig = sig;
    if (ui.force)                                   /* head + rule + two bands cover rows 0..229 */
        lcd_fill(0, H_HEAD + 1 + 124 + 95, 240, 240 - (H_HEAD + 1 + 124 + 95), C_BLACK);
    cv_begin(240, H_HEAD, C_BLACK);
    cv_text(4, 1, &FONT_S, ui.menu == 2 ? "ABOUT" : "MENU", C_HI);
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
        if (ui.menu == 2)
            ui.menu = 1, ui.force = 1;
        else
            menu_close();
        return;
    }
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
