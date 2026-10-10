/* SPDX-License-Identifier: GPL-3.0-only */
/* Real recovery control flow with hardware hooks mocked. No claim of USB
 * enumeration on hardware: the separate OTA suite exercises the real protocol. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include "../firmware/src/system/bootguard.h"
static bootguard_t bootguard;
static uint32_t ticks, fm1_ms, polls, feeds, retries, services, sessions, rom, detached, scans;
static uint32_t jedec = 0x856014u, jedec_reads, lcd_calls, font;
static uint8_t recovery_active, flash_ok;
static struct { uint8_t ota_req, uboot_req; } usb;
static struct { uint32_t buttons; } fm1_in;
static uint32_t input_buttons, stop_at_retry;
static jmp_buf exit_loop;
#define FM1_TICKS_PER_US 24u
#define FL_FAR(x) x
#define C_BLACK 0
#define C_WHITE 65535
#define FONT_S font
static uint32_t fm1_ticks(void) { return ticks; }
static void fm1_wdt_feed(void) { feeds++; }
static void usb_poll(void) { polls++; }
static void usb_retry(uint32_t t)
{
    (void)t; retries++;
    if (stop_at_retry && retries >= stop_at_retry) longjmp(exit_loop, 1);
}
static void usb_detach(void) { detached++; }
static void fm1_delay_ms(uint32_t n) { ticks += n * 24000u; }
static void fm1_enter_uboot(void) { rom++; }
static void ota_service(void) { services++; }
static void recovery_poll(void);
static int ota_session(void)
{
    unsigned i;
    sessions++;
    for (i = 0; i < 200; i++) { ticks += 12000u; recovery_poll(); }
    return -13;  /* failed update must leave rescue available for another try */
}
static void fm1_input_init(void) { fm1_in.buttons = 0; }
static void fm1_input_scan(void) { scans++; ticks += 24000u; fm1_in.buttons = input_buttons; }
static uint32_t fl_jedec_ram(void) { jedec_reads++; return jedec; }
static void lcd_init(void) { lcd_calls++; }
static void lcd_fill(int x, int y, int w, int h, int c) { (void)x;(void)y;(void)w;(void)h;(void)c; }
static char drawn[8][32];                    /* the rescue screen's lines at y 190 / 206 / 222 (recovery_crumbs) */
static void draw_text_box(int x, int y, int w, const void *f, const char *s, int c, int a)
{
    (void)x;(void)w;(void)f;(void)c;(void)a;
    if (y >= 190 && (y - 190) % 16 == 0 && (y - 190) / 16 < 8) snprintf(drawn[(y - 190) / 16], 32, "%s", s);
}
#define RGB(r, g, b) ((int)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))
#define DBG_MAGIC 0x44424731u
static struct { uint32_t magic, halves, max_us, nested, in_audio, late, timer_irqs, ui_frames, last_us, cpu_q8, boots,
                stage, page, home, prev_stage, prev_page, prev_home, prev_rst, prev_frames; } felucca_dbg;
#define FM1_CRASH_MAGIC 0x43525348u
static struct { uint32_t magic, count, vec, pc; } fm1_crash;
static void lcd_sync(void) {}
static void usb_start(void) {}
#include "../firmware/src/system/recovery.c"

int main(void)
{
    bootguard_t b = {0};
    assert(bootguard_begin(&b) == BOOT_NORMAL);
    assert(bootguard_begin(&b) == BOOT_NORMAL && b.failed == 1);
    assert(bootguard_begin(&b) == BOOT_RECOVERY && b.pending == 2);
    assert(bootguard_begin(&b) == BOOT_ROM && b.pending == 0);
    assert(bootguard_begin(&b) == BOOT_NORMAL);
    bootguard_clear(&b);                     /* healthy boot / intentional update */
    assert(bootguard_begin(&b) == BOOT_NORMAL && b.failed == 0);
    b.pending = 2; bootguard_clear(&b);
    assert(bootguard_begin(&b) == BOOT_NORMAL);
    b.failed = 0xFFFFFFFFu;
    assert(bootguard_begin(&b) == BOOT_NORMAL && b.failed == 0);
    assert(bootguard_manual(1) && !bootguard_manual(0) && !bootguard_manual(2) && !bootguard_manual(3));
    {   /* OCT- + OCT+ at power-on: 3 s -> UBOOT, sooner -> calibration, a bounce decides nothing */
        bootguard_hold_t h = {0, 0, 0};
        uint32_t i, r = HOLD_WAIT;
        assert(bootguard_hold_step(&h, 0) == HOLD_NONE);
        h = (bootguard_hold_t){0, 0, 0};
        assert(bootguard_hold_step(&h, 1) == HOLD_WAIT && bootguard_hold_step(&h, 0) == HOLD_NONE);
        h = (bootguard_hold_t){0, 0, 0};
        for (i = 0; r == HOLD_WAIT; i++) r = bootguard_hold_step(&h, 1);
        assert(r == HOLD_UBOOT && i == BOOTGUARD_HOLD_SURE + BOOTGUARD_HOLD_MS);
        h = (bootguard_hold_t){0, 0, 0}; r = HOLD_WAIT;
        for (i = 0; i < 1000u; i++) assert(bootguard_hold_step(&h, 1) == HOLD_WAIT);
        assert(bootguard_hold_step(&h, 0) == HOLD_WAIT && bootguard_hold_step(&h, 0) == HOLD_WAIT);
        assert(bootguard_hold_step(&h, 1) == HOLD_WAIT);          /* a bounce: still held */
        for (i = 0; i < BOOTGUARD_HOLD_SURE - 1u; i++) assert(bootguard_hold_step(&h, 0) == HOLD_WAIT);
        assert(bootguard_hold_step(&h, 0) == HOLD_CAL);
        h = (bootguard_hold_t){0, 0, 0}; r = HOLD_WAIT;
        for (i = 0; i < 2999u; i++) r = bootguard_hold_step(&h, 1);
        assert(r == HOLD_WAIT);                                      /* 3 reads + 2996 ms */
        for (i = 0; i < 3u; i++) r = bootguard_hold_step(&h, 1);
        assert(r == HOLD_WAIT && bootguard_hold_step(&h, 1) == HOLD_UBOOT);
    }

    input_buttons = 1; assert(recovery_key());
    input_buttons = 3; assert(!recovery_key());
    input_buttons = 0; assert(!recovery_key());
    assert(scans >= 150);

    fm1_ms = 0; recovery_fraction = 0;
    recovery_last = recovery_usb_last = ticks = 0xFFFFF000u;
    ticks += 12000u; recovery_poll();         /* wrap + half a millisecond */
    assert(fm1_ms == 0 && polls == 1);
    ticks += 12000u; recovery_poll();
    assert(fm1_ms == 1 && polls == 2);
    recovery_poll(); assert(polls == 2);     /* never busy-poll USB faster than 2 kHz */

    flash_ok = 0; usb.ota_req = 1; recovery_step();
    assert(sessions == 0 && usb.ota_req == 0);
    flash_ok = 1; usb.ota_req = 1; recovery_step();
    assert(sessions == 1 && fm1_ms == 101 && polls == 202);
    usb.ota_req = 1; recovery_step();
    assert(sessions == 2 && fm1_ms == 201 && polls == 402);
    assert(feeds > 400 && services == 3);
    usb.uboot_req = 1; bootguard.pending = 2; recovery_step();
    assert(rom == 1 && detached == 1 && bootguard.pending == 0);
    usb.uboot_req = 0;

    retries = 0; stop_at_retry = 5;
    felucca_dbg.magic = DBG_MAGIC;               /* what the failed start-ups left: the rescue screen prints it */
    felucca_dbg.stage = 0x49; felucca_dbg.prev_stage = 0x4A; felucca_dbg.ui_frames = 0;
    fm1_crash.magic = FM1_CRASH_MAGIC; fm1_crash.count = 2; fm1_crash.vec = 1; fm1_crash.pc = 0x0201ABCDu;
    if (!setjmp(exit_loop)) recovery_main();
    assert(!strcmp(drawn[0], "BOOT 0049004A 00000000"));
    assert(!strcmp(drawn[2], "CRASH 00020001 0201ABCD"));
    assert(strlen(drawn[0]) <= 30 && strlen(drawn[2]) <= 30);   /* (30 characters: the screen's width in FONT_S) */
    assert(recovery_active && bootguard.pending == 2 && flash_ok);
    assert(jedec_reads == 1 && lcd_calls == 1);
    assert(services == 7 && sessions == 2);  /* idle startup starts no update */
    assert(bootguard_begin(&bootguard) == BOOT_ROM);
    puts("recovery: boot failures, manual key, timer wrap, polled USB during OTA, retry, flash refusal, ROM fallback and the failed start-ups' breadcrumbs on the screen passed");
    return 0;
}
