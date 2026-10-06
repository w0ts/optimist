/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* SLOOP menu (HOME held): COLOR, LOWCUT, ZOOM, HARDWARE CALIBRATION, ABOUT. */
/* ------------------------------------------------------------ menu --- */
#if FELUCCA_BRIGHT
#include "bright.c"            /* MENU > BRIGHT: the backlight level (after X0X) */
enum { MI_COLOR, MI_LOWCUT, MI_ZOOM, MI_BRIGHT, MI_PANEL, MI_ABOUT, MI_BACK, MI_COUNT };
static const char *const MI_NAME[MI_COUNT] = {"COLOR", "LOWCUT", "ZOOM", "BRIGHT", "HARDWARE CALIBRATION", "ABOUT",
                                              "BACK"};
#else
enum { MI_COLOR, MI_LOWCUT, MI_ZOOM, MI_PANEL, MI_ABOUT, MI_BACK, MI_COUNT };
static const char *const MI_NAME[MI_COUNT] = {"COLOR", "LOWCUT", "ZOOM", "HARDWARE CALIBRATION", "ABOUT", "BACK"};
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
            cv_text(4, 4, &FONT_L, "SLOOP", C_WHITE);
            cv_rect(96, 10, 8, 4, TE_COL[0]), cv_rect(96, 16, 12, 4, TE_COL[1]);   /* the sail */
            cv_rect(96, 22, 16, 4, TE_COL[2]), cv_rect(96, 28, 20, 4, TE_COL[3]);
            cv_text(4, 36, &FONT_S, "BASED ON FELUCCA", C_AMB);
            cv_text(4, 54, &FONT_S, FELUCCA_VERSION, C_HI);
            cv_text(236 - text_w(&FONT_S, __DATE__), 54, &FONT_S, __DATE__, C_GRAY);   /* build date */
            cv_text(cv_text(4, 72, &FONT_S, "LEO KUROSHITA", C_HI) + 8, 72, &FONT_S, "@KUROGEDELIC", C_AMB);
            cv_text(4, 88, &FONT_S, "H\xDCGELTON INSTRUMENTS", C_HI);   /* Latin-1 U-umlaut */
            cv_text(4, 104, &FONT_S, "HUGELTON.COM", C_AMB);
            cv_text(4, 119, &FONT_S, "GPL-3.0, NO WARRANTY", C_HI);
            cv_text(4, 132, &FONT_S, "GITHUB.COM/ISOD89/SLOOP-FM1", C_AMB);   /* (the source of this firmware) */
            cv_text(4, 146, &FONT_S, "FONT: TERMINUS (OFL)", C_DIM);
            cv_text(4, 159, &FONT_S, "SAMPLES: VERSILIAN (CC0)", C_DIM);
            cv_text(4, 172, &FONT_S, "+ SONIC PI (CC0)", C_DIM);
            cv_text(4, 185, &FONT_S, "PHASE: CRISPYZEBRA (GPL)", C_DIM);
            cv_text(4, 198, &FONT_S, "VOICE: REF. KLATTSCH (MIT)", C_DIM);
        } else {
            for (i = 0; i < MI_COUNT; i++) {
                int32_t y = 4 + (int32_t)i * 24;
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
                if (i == MI_COLOR) {
                    uint32_t k;
                    cv_text(90, y, &FONT_S, PALETTES[settings.palette].name, C_HI);
                    for (k = 0; k < 5u; k++)
                        cv_rect(160 + (int32_t)k * 14, y + 3, 10, 10, pal[k]);
                }
            }
            cv_text(4, 170, &FONT_S, "PRESETS MOVE", C_DIM);
            cv_text(4, 188, &FONT_S, "OCT+ OK   OCT- BACK", C_DIM);
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

