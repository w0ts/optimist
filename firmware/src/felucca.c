/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* SLOOP (based on FELUCCA): one compilation unit (the HAL is header-only). Order matters. */
#include <stdint.h>
#ifndef FELUCCA_DUAL
#define FELUCCA_DUAL 0           /* EXPERIMENTAL second core (dual.c, docs/DUAL-CORE.md): 0 off, 1 stage 1
                                  * (CPU1 counts, nothing else), 2 CPU1 renders parts 2 and 3 */
#endif
#ifndef FELUCCA_BENCH
#define FELUCCA_BENCH 0          /* emulator measurement scenarios (bench.c), 0 = none */
#endif
#if FELUCCA_DUAL
static void dual_flash_enter(void);
#define FM1_FLASH_ENTER() dual_flash_enter()
#endif
#include "fm1_time.h"
#include "fm1_clock.h"
#include "fm1_sys.h"
#include "fm1_irq.h"
#include "fm1_guard.h"
#include "core/backports.h"          /* (before the HAL: FELUCCA_KEYS_FAST, hal/fm1_input.h) */
#include "fm1_input.h"
#include "fm1_timer.h"
#include "fm1_audio.h"
#include "fm1_adc.h"
#include "fm1_lcd_hw.h"
#include "felucca_tables.h"
#if FELUCCA_DUAL
#include "fm1_dual.h"
#endif

#ifndef FELUCCA_USB_AUDIO
#define FELUCCA_USB_AUDIO 1      /* EXPERIMENTAL USB audio (from Melodee): UAC1 + MIDI; replaces CDC */
#endif

#include "system/libc.c"
#include "display/lcd.c"
#include "display/gfx.c"
#include "core/core.h"
#include "engines/engines.c"
#include "drums/drums.c"
#include "core/params.c"
#include "core/voice.c"
#include "fx/slicer/slicer.c"          /* per-track SLICER insert, used by fx.c */
#if FELUCCA_MACROS
#include "core/macro.c"           /* GLO > MACRO: the performance macros, used by fx.c and seq.c */
#endif
#include "fx/fx.c"
#if FELUCCA_BENCH
#include "system/bench.c"           /* fixed scenarios for measurements (emulator) */
#endif
#include "system/dual.c"            /* the second core (FELUCCA_DUAL) */
#ifndef FELUCCA_OTA
#define FELUCCA_OTA 1            /* M-UPGRADE update entry; needs FELUCCA_FLASH */
#endif
#ifndef FELUCCA_OTA_DRYRUN
#define FELUCCA_OTA_DRYRUN 0     /* 1 = stage, ask "success", then undo: no record, no reset */
#endif
#ifndef FELUCCA_ID
#define FELUCCA_ID "FM-1_700"    /* package identity (build.py: the .fwsc marker string; Optimist 7XY) */
#endif
#ifndef FELUCCA_IDLE
#define FELUCCA_IDLE 1           /* main loop: wait for an interrupt (idle) between UI frames */
#endif
#ifndef FELUCCA_UNDO_HISTORY
#define FELUCCA_UNDO_HISTORY 1   /* undo / redo: a history of many levels in the memory left over (undo.c,
                                  * app.ld _undo_*; FELUCCA_UNDO_CAP caps it); 0 = the single level */
#endif
#ifndef FELUCCA_CDC
#define FELUCCA_CDC (!FELUCCA_USB_AUDIO) /* USB CDC-ACM serial console */
#endif
#if FELUCCA_USB_AUDIO && FELUCCA_CDC
#error "USB audio uses the CDC endpoints; build with FELUCCA_CDC=0"
#endif
#include "io/usb/usb.c"
#ifndef FELUCCA_UART
#define FELUCCA_UART 1           /* TRS MIDI IN on UART1 (notes, clock); 0 = none (SYNC TRS then hears nothing) */
#endif
#if FELUCCA_UART
#include "io/midi/midi_uart.c"
#endif
#define FELUCCA_ARRANGER 1
#include "seq/arranger.c"
#include "seq/seq.c"
#if FELUCCA_CPU_GUARD
#include "system/cpuguard.c"        /* the predictive CPU guard (audio.c calls it every half) */
#endif
#include "core/audio.c"
#include "ui/meters.c"          /* the level meters: the tracks' peaks and the output, main loop */
#include "ui/panel.c"
#ifndef FELUCCA_KNOB_ACCEL
#define FELUCCA_KNOB_ACCEL 1     /* knobs: more steps a detent when turned fast (knob_accel.h, from X0X); 0 = one */
#endif
#include "ui/knob_accel.h"
#include "core/model.c"             /* the model operations both UIs and the core call (apply_preset_to, BANK...) */
#include "drums/dsnd_desc.c"        /* the drum lanes' SOUND page values and the kit list (both UIs, ed_dsrc.c) */
#ifndef FELUCCA_UI
#define FELUCCA_UI 0             /* the user interface, chosen at build time: 0 SLOOP's (ui/sloop: pages and held
                                  * layers), 1 the Optimist UI (ui/optimist: rows and cards, docs/UI-OPTIMIST-DESIGN.md) */
#endif
#if FELUCCA_UI == 1
#include "ui/optimist/optimist.c"     /* the Optimist UI: its files, in order */
#else
#include "ui/sloop/ui.c"
#include "ui/sloop/ui_drums.c"          /* the drum track's SOUND pages, the kit list with the user kits */
#include "ui/sloop/ui_colors.c"         /* the colour language: engine, drum-kind, status colours (tools/colors.json) */
#include "ui/sloop/ui_song.c"
#include "ui/sloop/ui_studio.c"
#include "ui/sloop/ui_tempo.c"          /* the TEMPO page: PLAY held (BPM, swing, sync, nudge) */
#include "ui/sloop/ui_fm6.c"          /* the FM6 operator editor: ENV held on an FM6 track */
#include "ui/sloop/icons.c"           /* parameter icons (FELUCCA_ICONS), used by ui_draw.c */
#if FELUCCA_MISSING_WARN
#include "storage/miss.c"            /* "MISSING: PHYS T2": what a load uses and this build lacks; TOOLS > MISS */
#endif
#include "ui/sloop/ui_draw.c"
#if FELUCCA_VIS
#include "ui/sloop/ui_vis.c"          /* the full-screen visualiser, 12 styles (SLOOP 2.4; FELUCCA_VIS) */
#endif
#if FELUCCA_SCOPE
#include "ui/sloop/ui_scope.c"        /* the scope screen: HOME on TRACKS (FELUCCA_SCOPE; the visualiser's tap, slim) */
#endif
#include "ui/sloop/ui_overview.c"       /* VIEW ALL: a page family at once (GLO > SYSTEM VIEW) */
#if FELUCCA_DRUM_STEP
#include "ui/sloop/ui_drumstep.c"     /* the drum track's SEQ layer as a TR step sequencer (FELUCCA_DRUM_STEP) */
#endif
#include "ui/sloop/ui_layers.c"       /* hold a function button: what the keys and knobs do (TE style) */
#include "ui/sloop/ui_menu.c"
#if FELUCCA_MACROS
#include "ui/sloop/macro_ui.c"        /* GLO > MACRO's knobs: motion recording (macro.c) */
#endif
#include "ui/sloop/ui_input.c"
#endif
#ifndef FELUCCA_FLASH
#define FELUCCA_FLASH 1          /* flash driver + storage.c */
#endif
#if FELUCCA_OTA && !FELUCCA_FLASH
#error "FELUCCA_OTA needs FELUCCA_FLASH"
#endif
#if FELUCCA_FLASH
#include "fm1_flash.h"
static uint8_t flash_ok;                 /* JEDEC id matched at boot (persist_boot) */
static int st_read(uint32_t off, void *dst, uint32_t n)   /* 256-byte IRQ-off windows: audio keeps up */
{
    uint8_t *d = dst;
    while (n) {
        uint32_t k = n > 256u ? 256u : n, f = irq_save();
        int rc = FL_FAR(fl_read_ram)(off, d, k);
        irq_restore(f);
        if (rc)
            return rc;
        off += k;
        d += k;
        n -= k;
    }
    return 0;
}
static void audio_silence(void)                 /* IRQs off: the DMA would loop stale audio (buzz) */
{
    uint32_t i;
    for (i = 0; i < sizeof abuf / sizeof abuf[0]; i++)
        abuf[i] = 0;
}
/* a sector erase with the audio silenced inside the same interrupts-off window (silenced before it, the
 * audio interrupt could render a fresh half that the DMA then loops for the whole erase: a buzz) */
static int fl_erase4k_quiet(uint32_t off, uint32_t *took)
{
    uint32_t f = irq_save();
    int rc;
    audio_silence();
    rc = FL_FAR(fl_erase4k_ram)(off, took);
    irq_restore(f);
    return rc;
}
static int st_erase(uint32_t off)
{
    uint32_t took;
    if (!FL_STORE_OK(off, 0x1000u))
        return -8;
    return fl_erase4k_quiet(off, &took);
}
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    if (!FL_STORE_OK(off, n))
        return -8;
    return fl_write(off, src, n);
}
#include "storage/storage.c"
#endif
#include "storage/upreset.c"          /* user presets (RAM mirror; flash with FELUCCA_FLASH) */
#include "storage/project.c"
#if FELUCCA_DRUM_KITS
#if !FELUCCA_FLASH
#error "FELUCCA_DRUM_KITS needs FELUCCA_FLASH (the kit bank is in the data flash)"
#endif
#include "drums/drum_kits.c"         /* user drum kits: the bank of 16 (ui_drums.c, ed_drums.c) */
#endif
#include "engines/fm6/fm6_store.c"         /* FM6 user bank, DX7 SysEx, STORE (eng_fm6.c) */
#if FELUCCA_NATIVE_BANKS
#include "storage/nbank.c"             /* the FM6 / CZ native collections in PRESETS, the CZ store (after Melodee 0.12) */
#endif
#if FELUCCA_OTA
static uint8_t recovery_active;
#define OTA_IDENTITY (recovery_active ? "FM-1_000" : FELUCCA_ID)
#include "system/ota.c"
static void recovery_poll(void);
static uint32_t ota_now_ms(void) { return fm1_ms; }
static void ota_idle(void)
{
    fm1_wdt_feed();
    if (recovery_active) recovery_poll();
}
static int ota_in_area(uint32_t off, uint32_t n) { return FL_IN(off, n, OTA_AREA, OTA_AREA + OTA_AREA_LEN); }
static int ota_erase(uint32_t off)
{
    uint32_t took;
    if (!ota_in_area(off, 0x1000u) || (off & 0xFFFu))
        return -8;
    return fl_erase4k_quiet(off, &took);
}
static int ota_prog(uint32_t off, const void *p, uint32_t n)
{
    if (!ota_in_area(off, n))
        return -8;
    return fl_write(off, p, n);
}
static int ota_fread(uint32_t off, void *p, uint32_t n) { return st_read(off, p, n); }
static void ota_show(uint32_t step, int32_t code)
{
    static const char *const STEP[] = {"", "PACKAGE", "CHECK HEAD", "LOADER", "CONFIRM", "RESTART"};
    char b[24];
    if (recovery_active) return;             /* keep polled USB alive during OTA */
    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_text_box(0, 92, 240, &FONT_S, "UPDATE", C_WHITE, 1);
    if (step < 9u) {
        draw_text_box(0, 124, 240, &FONT_S, STEP[step < 6u ? step : 0], C_HI, 1);
        return;
    }
    if (code == 1)
        str_cpy(b, "DRY RUN OK", sizeof b);
    else {
        uint32_t k = 0, v = (uint32_t)-code;
        str_cpy(b, "FAILED  -", sizeof b);
        while (b[k])
            k++;
        if (v >= 10u)
            b[k++] = (char)('0' + v / 10u);
        b[k++] = (char)('0' + v % 10u);
        b[k] = 0;
    }
    draw_text_box(0, 124, 240, &FONT_S, b, C_HI, 1);
    fm1_delay_ms(1500);
}
static void ota_commit(const uint8_t *parm)
{
    bootguard_clear(&bootguard);                        /* intentional update reset */
    usb_detach();
    fm1_delay_ms(30);
    fm1_enter_update(parm);                             /* record into RAM, core reset (fm1_sys.h) */
}
#endif
#if FELUCCA_OTA
#include "io/editor/editor.c"          /* web editor SysEx (needs the OTA SysEx plumbing) */
#endif
#if FELUCCA_CDC
#include "io/console.c"
#endif
#if FELUCCA_OTA
#include "system/recovery.c"        /* early, polled USB updater; no synth or settings */
#endif
#ifndef FELUCCA_SPLASH
#define FELUCCA_SPLASH 1         /* the boot logo (splash.c, ~0.4-0.6 KB of flash); 0 = a dark screen, then the UI */
#endif
#if FELUCCA_SPLASH
#include "ui/splash.c"          /* the Optimist boot screen: the logo drawn from its geometry, the version */
#endif
#if FELUCCA_SIMD_PROBE
#include "system/simd_probe.c"      /* EXPERIMENTAL: the boot test of the packed 16-bit forms (hal/fm1_simd.h) */
#endif
#include "system/main.c"
