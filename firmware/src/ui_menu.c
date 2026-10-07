/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* SLOOP menu (HOME held): COLOR, LOWCUT, ZOOM, HARDWARE CALIBRATION, ABOUT. */
/* ------------------------------------------------------------ menu --- */
#if FELUCCA_BRIGHT
#include "bright.c"            /* MENU > BRIGHT: the backlight level (after X0X) */
#endif
#if FELUCCA_LIGHTS
/* with LIGHTS / KEYS / NOTES (SLOOP 2.3): the items as the build has them, closer rows */
enum { MI_COLOR, MI_LOWCUT, MI_ZOOM,
#if FELUCCA_BRIGHT
       MI_BRIGHT,
#endif
       MI_LIGHTS, MI_KEYS,
#if FELUCCA_KEYLIT
       MI_NOTES,
#endif
#if FELUCCA_CDC
       MI_USB,
#endif
       MI_PANEL, MI_ABOUT, MI_BACK, MI_COUNT };
static const char *const MI_NAME[MI_COUNT] = {
    [MI_COLOR] = "COLOR", [MI_LOWCUT] = "LOWCUT", [MI_ZOOM] = "ZOOM",
#if FELUCCA_BRIGHT
    [MI_BRIGHT] = "BRIGHT",
#endif
    [MI_LIGHTS] = "LIGHTS", [MI_KEYS] = "KEYS",
#if FELUCCA_KEYLIT
    [MI_NOTES] = "NOTES",
#endif
#if FELUCCA_CDC
    [MI_USB] = "USB SERIAL",
#endif
    [MI_PANEL] = "HARDWARE CALIBRATION", [MI_ABOUT] = "ABOUT", [MI_BACK] = "BACK"};
static const char *const LIGHTS_NAME[LIGHTS_N] = {"OFF", "LOW", "MID", "HIGH"};   /* every button lit, the labels readable */
static const char *const KEYS_NAME[KEYS_N] = {"OFF", "C KEYS", "WHITE KEYS"};      /* keys lit too, at the LIGHTS level */
#define MI_DY (MI_COUNT > 10 ? 16 : FELUCCA_BRIGHT ? 17 : 18)   /* rows between two menu lines (ten with BRIGHT) */
#elif FELUCCA_BRIGHT
enum { MI_COLOR, MI_LOWCUT, MI_ZOOM, MI_BRIGHT,
#if FELUCCA_CDC
       MI_USB,
#endif
       MI_PANEL, MI_ABOUT, MI_BACK, MI_COUNT };
static const char *const MI_NAME[MI_COUNT] = {[MI_COLOR] = "COLOR", [MI_LOWCUT] = "LOWCUT", [MI_ZOOM] = "ZOOM",
                                              [MI_BRIGHT] = "BRIGHT",
#if FELUCCA_CDC
                                              [MI_USB] = "USB SERIAL",
#endif
                                              [MI_PANEL] = "HARDWARE CALIBRATION", [MI_ABOUT] = "ABOUT",
                                              [MI_BACK] = "BACK"};
#else
enum { MI_COLOR, MI_LOWCUT, MI_ZOOM,
#if FELUCCA_CDC
       MI_USB,
#endif
       MI_PANEL, MI_ABOUT, MI_BACK, MI_COUNT };
static const char *const MI_NAME[MI_COUNT] = {[MI_COLOR] = "COLOR", [MI_LOWCUT] = "LOWCUT", [MI_ZOOM] = "ZOOM",
#if FELUCCA_CDC
                                              [MI_USB] = "USB SERIAL",
#endif
                                              [MI_PANEL] = "HARDWARE CALIBRATION", [MI_ABOUT] = "ABOUT",
                                              [MI_BACK] = "BACK"};
#endif
#if FELUCCA_BASSPLUS
static const char *const LOWCUT_N[3] = {"OFF", "LOWCUT", "BASS+"};   /* settings.lowcut (fx.c, bassplus.c) */
#endif

static void draw_menu(void)
{
    uint32_t i, pass, sig = ui.menu * 7u + ui.menu_sel * 131u + settings.palette * 1009u + settings.lowcut * 7919u +
                            settings.zoom * 104729u;
#if FELUCCA_BRIGHT
    sig += bl_dim * 15485863u;
#endif
#if FELUCCA_LIGHTS
    sig += lights_lvl * 1299709u + lights_keys * 32452843u + lights_notes_off * 49979687u;
#endif
#if FELUCCA_CDC
    sig += usb_serial * 86028121u;
#endif
    if (!ui.force && sig == ui.menu_sig)
        return;
    ui.menu_sig = sig;
    if (ui.force)                                   /* head + rule + two bands cover rows 0..229 */
        lcd_fill(0, H_HEAD + 1 + 124 + 95, 240, 240 - (H_HEAD + 1 + 124 + 95), C_BLACK);
    cv_begin(240, H_HEAD, C_BLACK);
    cv_text(4, 1, &FONT_S, ui.menu == 2 ? "ABOUT" : "MENU", C_HI);
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
            for (i = 0; i < MI_COUNT; i++) {
#if FELUCCA_LIGHTS
                int32_t y = 4 + (int32_t)i * MI_DY;
#else
                int32_t y = 4 + (int32_t)i * (MI_COUNT > 6 ? 20 : 24);   /* (above the help at 170) */
#endif
                int sel = i == ui.menu_sel;
                if (sel)
                    cv_rect(4, y + 6, 3, 3, C_WHITE);
                cv_text(14, y, &FONT_S, MI_NAME[i], sel ? C_WHITE : C_GRAY);
#if FELUCCA_BASSPLUS
                if (i == MI_LOWCUT)
                    cv_text(90, y, &FONT_S, LOWCUT_N[settings.lowcut % 3u], C_HI);
                if (i == MI_ZOOM)
                    cv_text(90, y, &FONT_S, settings.zoom ? "ON" : "OFF", C_HI);
#else
                if (i == MI_LOWCUT || i == MI_ZOOM)
                    cv_text(90, y, &FONT_S, (i == MI_LOWCUT ? settings.lowcut : settings.zoom) ? "ON" : "OFF", C_HI);
#endif
#if FELUCCA_BRIGHT
                if (i == MI_BRIGHT) {
                    char b[4] = {(char)('0' + bright_level()), 0};
                    cv_text(90, y, &FONT_S, b, C_HI);
                }
#endif
#if FELUCCA_LIGHTS
                if (i == MI_LIGHTS)
                    cv_text(90, y, &FONT_S, LIGHTS_NAME[lights_lvl % LIGHTS_N], C_HI);
                if (i == MI_KEYS)
                    cv_text(90, y, &FONT_S, KEYS_NAME[lights_keys % KEYS_N], lights_lvl ? C_HI : C_DIM);   /* (needs LIGHTS) */
#if FELUCCA_KEYLIT
                if (i == MI_NOTES)
                    cv_text(90, y, &FONT_S, lights_notes_off ? "OFF" : "ON", C_HI);
#endif
#endif
#if FELUCCA_CDC
                if (i == MI_USB)                       /* (a change: from the next start) */
                    cv_text(104, y, &FONT_S, usb_serial == usb_cdc_on ? (usb_serial ? "ON" : "OFF")
                                            : usb_serial ? "ON: RESTART" : "OFF: RESTART", usb_serial == usb_cdc_on ? C_HI : C_AMB);
#endif
                if (i == MI_COLOR) {
                    uint32_t k;
                    cv_text(90, y, &FONT_S, PALETTES[settings.palette].name, C_HI);
                    for (k = 0; k < 5u; k++)
                        cv_rect(160 + (int32_t)k * 14, y + 3, 10, 10, pal[k]);
                }
            }
#if FELUCCA_LIGHTS
            cv_text(4, 4 + MI_COUNT * MI_DY + 4, &FONT_S, "PRESETS MOVE  KNOB 1 SET", C_DIM);
            cv_text(4, 4 + MI_COUNT * MI_DY + 20 - FELUCCA_BRIGHT * 2, &FONT_S, "OCT+ OK   OCT- BACK", C_DIM);
#else
            cv_text(4, 170, &FONT_S, "PRESETS MOVE", C_DIM);
            cv_text(4, 188, &FONT_S, "OCT+ OK   OCT- BACK", C_DIM);
#endif
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

/* menu: PRESETS moves, OCT+ confirms, OCT- cancels (ABOUT -> list -> close) */
static void menu_input(uint32_t pressed)
{
    int32_t s;
    uint32_t ok = (pressed >> panel.btn[B_OCTUP]) & 1u, back = (pressed >> panel.btn[B_OCTDN]) & 1u;
    if (back) {
        if (ui.menu == 2)
            ui.menu = 1, ui.force = 1;
        else
            menu_close();
        return;
    }
    if ((s = panel_enc(EN_PRESET)) != 0 && ui.menu == 1)
        ui.menu_sel = (uint8_t)((ui.menu_sel + (s > 0 ? 1u : MI_COUNT - 1u)) % MI_COUNT);
    s = panel_enc(EN_K1);
    if (s != 0 && ui.menu == 1 && ui.menu_sel == MI_COLOR) {
        settings.palette = (settings.palette + (s > 0 ? 1u : NPALETTES - 1u)) % NPALETTES;
        palette_set(settings.palette);              /* (the menu signature redraws) */
    }
#if FELUCCA_BASSPLUS
    if ((s != 0 || ok) && ui.menu == 1 && ui.menu_sel == MI_LOWCUT) {   /* OFF / LOWCUT / BASS+: KNOB 1, OCT+ steps */
        settings.lowcut = s ? (uint32_t)clamp((int32_t)(settings.lowcut % 3u) + (s > 0 ? 1 : -1), 0, 2)
                            : (settings.lowcut + 1u) % 3u;
        fx_lowcut = (uint8_t)settings.lowcut;
        ok = 0;
    }
#endif
#if FELUCCA_LIGHTS
    if ((s != 0 || ok) && ui.menu == 1 && (ui.menu_sel == MI_LIGHTS || ui.menu_sel == MI_KEYS)) {
        /* KNOB 1: brighter / dimmer (stops at the ends); OCT+ steps round */
        uint8_t *v = ui.menu_sel == MI_LIGHTS ? &lights_lvl : &lights_keys;
        uint32_t n = ui.menu_sel == MI_LIGHTS ? LIGHTS_N : KEYS_N;
        if (s > 0 && *v + 1u < n)
            (*v)++;
        else if (s < 0 && *v > 0u)
            (*v)--;
        else if (!s)
            *v = (uint8_t)((*v + 1u) % n);
        if (ui.menu_sel == MI_KEYS && lights_keys && !lights_lvl)
            lights_lvl = LIGHTS_LOW;                   /* keys lit need a level: the lowest */
        ok = 0;
    }
#if FELUCCA_KEYLIT
    if ((s != 0 || ok) && ui.menu == 1 && ui.menu_sel == MI_NOTES) {   /* right ON, left OFF; OCT+ toggles */
        lights_notes_off = (uint8_t)(s > 0 ? 0u : s < 0 ? 1u : !lights_notes_off);
        ok = 0;
    }
#endif
#endif
#if FELUCCA_CDC
    if ((s != 0 || ok) && ui.menu == 1 && ui.menu_sel == MI_USB) {   /* right ON, left OFF; OCT+ toggles */
        usb_serial = (uint8_t)(s > 0 ? 1u : s < 0 ? 0u : !usb_serial);
        ok = 0;
    }
#endif
#if FELUCCA_BRIGHT
    if ((s != 0 || ok) && ui.menu == 1 && ui.menu_sel == MI_BRIGHT) {   /* 1..8: KNOB 1, OCT+ steps round */
        bright_set(s ? (uint32_t)clamp((int32_t)bright_level() + s, 1, 8) : bright_level() % 8u + 1u);
        ok = 0;
    }
#endif
#if FELUCCA_BASSPLUS
    if ((s != 0 || ok) && ui.menu == 1 && ui.menu_sel == MI_ZOOM) {
#else
    if ((s != 0 || ok) && ui.menu == 1 && (ui.menu_sel == MI_LOWCUT || ui.menu_sel == MI_ZOOM)) {
#endif
        /* KNOB 1: right = ON, left = OFF; OCT+ toggles */
#if FELUCCA_BASSPLUS
        settings.zoom = s > 0 ? 1u : s < 0 ? 0u : !settings.zoom;   /* (LOWCUT has its own, above) */
#else
        uint32_t *v = ui.menu_sel == MI_LOWCUT ? &settings.lowcut : &settings.zoom;
        *v = s > 0 ? 1u : s < 0 ? 0u : !*v;
        fx_lowcut = (uint8_t)(settings.lowcut != 0);
#endif
        ok = 0;
    }
    if (ok && ui.menu == 1) {
        switch (ui.menu_sel) {
        case MI_COLOR:                                 /* OCT+ steps through the palettes too */
            settings.palette = (settings.palette + 1u) % NPALETTES;
            palette_set(settings.palette);
            break;
        case MI_PANEL:
            panel_setup();
            ui.force = 1;
            break;
        case MI_ABOUT:
            ui.menu = 2;
            ui.force = 1;
            break;
        default:
            menu_close();
            break;
        }
    }
    enc_drop();                                        /* swallow the rest while the menu is up */
}

