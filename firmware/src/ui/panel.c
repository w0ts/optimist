/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Physical panel: which matrix button / encoder carries which printed label.
 * The default table can be overridden by HARDWARE CALIBRATION (hold OCT- and
 * OCT+ while powering on), which asks for each label in turn. The learned
 * table lives in .noinit and, with FELUCCA_FLASH, in flash with the settings
 * (project.c). */
enum { B_FX, B_SCL, B_ENV, B_LFO, B_EDIT, B_GLO, B_HOME, B_SAVE, B_ARP, B_SEQ, B_PLAY, B_REC,
       B_OCTDN, B_OCTUP, NB };
enum { EN_SELECT, EN_ALGO, EN_PRESET, EN_K1, EN_K2, EN_K3, EN_K4, NE };
static const char *const B_NAME[NB] = {"FX", "SCL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE",
                                        "ARP", "SEQ", "PLAY", "REC", "OCT-", "OCT+"};
static const char *const E_NAME[NE] = {"SELECT", "ALGORITHM", "PRESETS", "KNOB 1", "KNOB 2",
                                        "KNOB 3", "KNOB 4"};
#define PANEL_MAGIC 0x50414E35u          /* "PAN5": bump when PANEL_DEFAULT changes */

typedef struct {
    uint32_t magic;
    uint8_t btn[NB];             /* matrix button id (0..13) per label */
    uint8_t enc[NE];             /* matrix encoder (0..6) per role */
    int8_t dir[NE];              /* +1 / -1 so that clockwise is + */
} panel_t;
panel_t panel __attribute__((section(".noinit")));

static const panel_t PANEL_DEFAULT = {
    PANEL_MAGIC,
    {2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 0, 1},   /* PLAY = 12, REC = 13 */
    {0, 1, 6, 2, 3, 4, 5},                           /* SELECT = enc 0, ALGORITHM = enc 1, PRESETS = enc 6 */
    {1, 1, 1, 1, 1, 1, 1},
};

static void panel_init(void)                     /* also after a flash load: ids are used as array indexes and shifts */
{
    uint32_t i, ok = panel.magic == PANEL_MAGIC;
    for (i = 0; ok && i < NB; i++)
        ok = panel.btn[i] < 14u;
    for (i = 0; ok && i < NE; i++)
        ok = panel.enc[i] < 7u && (panel.dir[i] == 1 || panel.dir[i] == -1);
#if FELUCCA_ST_STRICT
    {   /* a permutation: no two labels on one button or knob, else a label could never be reached
         * (SLOOP 2.3 panel_valid) */
        uint32_t b = 0, e = 0;
        for (i = 0; ok && i < NB; i++) {
            ok = !((b >> panel.btn[i]) & 1u);
            b |= 1u << panel.btn[i];
        }
        for (i = 0; ok && i < NE; i++) {
            ok = !((e >> panel.enc[i]) & 1u);
            e |= 1u << panel.enc[i];
        }
    }
#endif
    if (!ok)
        panel = PANEL_DEFAULT;
}

static uint32_t panel_btn_of(uint32_t matrix_id)        /* label of a matrix button, NB if none */
{
    uint32_t b;
    for (b = 0; b < NB; b++)
        if (panel.btn[b] == matrix_id)
            return b;
    return NB;
}

static void panel_led(uint32_t label, int on) { fm1_led_key(panel.btn[label], on); }

/* steps of a role, + = clockwise */
static uint32_t ui_input_ms;                    /* the last button, key or knob turn (ui_input.c; autosave) */
/* the knobs (roles, a bit per EN_*) not to be read again in this UI pass: a layer took them (Felucca #102, as in
 * SLOOP 2.4 panel.c enc_hold, isod89/sloop-fm1 v2.4 8d3823f, GPL-3.0-only): panel_enc returns 0 and leaves their
 * detents for the next pass (ui_input.c clears it at the start of each pass) */
static uint32_t enc_hold;
static int32_t panel_enc(uint32_t role)
{
    int32_t s;
    if ((enc_hold >> role) & 1u)
        return 0;
    s = fm1_enc_take(panel.enc[role]) * panel.dir[role];
    if (s)
        ui_input_ms = fm1_ms;                      /* a knob turning is not idle either: autosave waits */
    return s;
}

/* user settings that survive a reset */
#define SETTINGS_MAGIC 0x53455434u              /* "SET4": + view */
/* view: the pages, 1 = ALL (a family at once, ui_overview.c), 0 = PAGE (one page); GLO > SYSTEM VIEW */
struct { uint32_t magic, palette, lowcut, zoom, view; } settings __attribute__((section(".noinit")));

#include "../core/hold.h"          /* HOLD: the layer buttons' tap / hold threshold, a setting (HOME menu > SYSTEM) */
#include "../core/knobcol.h"       /* KNOB COLORS: the coloured knob caps, a setting (HOME menu > SYSTEM) */

static void settings_save(void);              /* project.c: flash copy (FELUCCA_FLASH) */
static uint8_t settings_later;                 /* changed while playing: saved once stopped (project.c) */
#if FELUCCA_LIGHTS
#include "lights.c"            /* menu LIGHTS / KEYS / NOTES (SLOOP 2.3) */
#endif
#if FELUCCA_VIS
static uint8_t vis_style;                       /* the visualiser's style, 0..11 (ui_vis.c); kept with the settings (bits 17-20) */
static uint8_t vis_on, vis_shown_last, vis_name_t;   /* on, drawn last frame, its name's time (ui_vis.c, ui_draw.c) */
#endif

static void settings_init(void)
{
    if (settings.magic != SETTINGS_MAGIC || settings.palette >= NPALETTES) {
        settings.magic = SETTINGS_MAGIC;
        settings.palette = 4;                  /* MONO (default) */
        settings.lowcut = 0;
        settings.zoom = 0;                     /* large readout of the touched value: off */
        settings.view = 1;                     /* the overview pages (ALL) */
    }
    if (settings.view > 1u)
        settings.view = 1;
    palette_set(settings.palette);
#if FELUCCA_BASSPLUS
    if (settings.lowcut > 2u)
        settings.lowcut = 1;
    fx_lowcut = (uint8_t)settings.lowcut;      /* 0 OFF, 1 LOWCUT, 2 BASS+ (bassplus.c) */
#else
    fx_lowcut = (uint8_t)(settings.lowcut != 0);
#endif
}
