/* SPDX-License-Identifier: GPL-3.0-only */
/* Application-level recovery, not a second firmware bank. The intact app's
 * startup, RAM flash driver and USB stack are still required. TIMER5, audio,
 * settings, samples, projects, editor and normal UI are never started here. */
static uint32_t recovery_last, recovery_fraction, recovery_usb_last;

static void recovery_poll(void)
{
    uint32_t now = fm1_ticks(), elapsed = now - recovery_last;
    recovery_last = now;
    /* Split before adding: also safe across the 32-bit TIMER4 wrap. */
    fm1_ms += elapsed / (1000u * FM1_TICKS_PER_US);
    recovery_fraction += elapsed % (1000u * FM1_TICKS_PER_US);
    fm1_ms += recovery_fraction / (1000u * FM1_TICKS_PER_US);
    recovery_fraction %= 1000u * FM1_TICKS_PER_US;
    fm1_wdt_feed();
    if (now - recovery_usb_last >= 500u * FM1_TICKS_PER_US) {
        recovery_usb_last = now;
        usb_poll();
    }
}

static void recovery_step(void)
{
    recovery_poll();
    usb_retry(fm1_ms);
    if (usb.uboot_req) {
        usb_detach();
        fm1_delay_ms(30);
        bootguard_clear(&bootguard);
        fm1_enter_uboot();
        return;
    }
    ota_service();
    if (usb.ota_req) {
        usb.ota_req = 0;
        if (flash_ok) ota_session();         /* same validated updater as normal mode */
    }
}

static int recovery_key(void)
{
    uint32_t start = fm1_ticks();
    fm1_input_init();
    do {
        fm1_wdt_feed();
        fm1_input_scan();
    } while ((uint32_t)(fm1_ticks() - start) < 50u * 1000u * FM1_TICKS_PER_US);
    return bootguard_manual(fm1_in.buttons);
}

/* "TEXT" then 8 hex digits per word, into out (room for 30 characters: the screen's width in FONT_S) */
static void recovery_hex(char *out, const char *text, const uint32_t *w, uint32_t n)
{
    uint32_t i = 0, k, b;
    while (*text && i < 12u)
        out[i++] = *text++;
    for (k = 0; k < n && i + 9u < 31u; k++) {
        out[i++] = ' ';
        for (b = 0; b < 8u; b++) {
            uint32_t d = w[k] >> (28u - 4u * b) & 0xFu;
            out[i++] = (char)(d < 10u ? '0' + d : 'A' + d - 10u);
        }
    }
    out[i] = 0;
}

/* what the failed start-ups left (RAM only: the guard sends a unit here after two warm resets, so it is still
 * there): the start-up step or main-loop stage of the last two (main.c BOOT_STAGE), the BLE breadcrumb and the
 * last link stop (hal/fm1_ble_rf.h), the last CPU exception (count, vector, PC). Grey, under the instructions */
static void recovery_crumbs(void)
{
    char t[32];
    uint32_t w[2];
    if (felucca_dbg.magic == DBG_MAGIC) {
        w[0] = felucca_dbg.stage << 16 | (felucca_dbg.prev_stage & 0xFFFFu), w[1] = felucca_dbg.ui_frames;
        recovery_hex(t, "BOOT", w, 2);         /* (last stage, the one before; the last start-up's UI frames) */
        draw_text_box(0, 190, 240, &FONT_S, t, RGB(140, 140, 140), 1);
    }
#if FELUCCA_BLE && BLE_HW_WL82
    w[0] = fm1_ble_bc.now, w[1] = fm1_ble_bc.stop;
    recovery_hex(t, "BLE", w, 2);
    draw_text_box(0, 206, 240, &FONT_S, t, RGB(140, 140, 140), 1);
#endif
    if (fm1_crash.magic == FM1_CRASH_MAGIC) {
        w[0] = fm1_crash.count << 16 | (fm1_crash.vec & 0xFFFFu), w[1] = fm1_crash.pc;
        recovery_hex(t, "CRASH", w, 2);
        draw_text_box(0, 222, 240, &FONT_S, t, RGB(140, 140, 140), 1);
    }
}

static BOOT_ORDER void recovery_main(void)
{
    recovery_active = 1;
    bootguard.pending = 2;                   /* WDT/exception here falls back to ROM */
    recovery_last = recovery_usb_last = fm1_ticks();
    recovery_fraction = fm1_ms = 0;
    /* No persist_boot or OTA cleanup: no flash write just for entering rescue. */
    flash_ok = FL_FAR(fl_jedec_ram)() == 0x856014u;
    lcd_init();
    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_text_box(0, 64, 240, &FONT_S, "OPTIMIST USB RESCUE", C_WHITE, 1);
    draw_text_box(0, 96, 240, &FONT_S, "CONNECT USB", C_WHITE, 1);
    draw_text_box(0, 124, 240, &FONT_S,
                  flash_ok ? "OPEN THE INSTALLER" : "UNKNOWN FLASH", C_WHITE, 1);
    draw_text_box(0, 164, 240, &FONT_S, "AUDIO OFF", C_WHITE, 1);
    recovery_crumbs();
    lcd_sync();
    usb_start();
    for (;;) recovery_step();
}
