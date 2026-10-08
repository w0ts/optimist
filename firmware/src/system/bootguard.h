/* SPDX-License-Identifier: GPL-3.0-only */
/* Warm-reset guard, retained in RAM only. Never writes the bootloader/flash.
 * This cannot rescue an image that fails before reaching the application. */
#ifndef FELUCCA_BOOTGUARD_H
#define FELUCCA_BOOTGUARD_H
#include <stdint.h>
#define BOOTGUARD_MAGIC 0x42475232u
typedef struct { uint32_t magic, failed, pending; } bootguard_t;
enum { BOOT_NORMAL, BOOT_RECOVERY, BOOT_ROM };
static void bootguard_clear(bootguard_t *b)
{
    b->magic = BOOTGUARD_MAGIC;
    b->failed = b->pending = 0;
}
static uint32_t bootguard_begin(bootguard_t *b)
{
    if (b->magic != BOOTGUARD_MAGIC || b->failed > 2u || b->pending > 2u)
        bootguard_clear(b);
    if (b->pending == 2u) {           /* recovery itself reset: keep ROM fallback */
        bootguard_clear(b);
        return BOOT_ROM;
    }
    if (b->pending && b->failed < 2u) b->failed++;
    b->pending = b->failed >= 2u ? 2u : 1u;
    return b->pending == 2u ? BOOT_RECOVERY : BOOT_NORMAL;
}
static int bootguard_manual(uint32_t buttons)
{
    return (buttons & 3u) == 1u;     /* physical OCT- alone; both keep calibration */
}
/* OCT- + OCT+ held at power-on, read off the bare matrix on the first lines of fm1_cstart (before the
 * vectors, .data/.bss or anything a build can break): held BOOTGUARD_HOLD_MS -> the chip's own UBOOT;
 * let go sooner -> HARDWARE CALIBRATION, as before. One call a millisecond, `both` = both closed */
#define BOOTGUARD_HOLD_MS 3000u
#define BOOTGUARD_HOLD_SURE 3u       /* reads in a row that agree: a bounce decides nothing */
enum { HOLD_NONE, HOLD_WAIT, HOLD_CAL, HOLD_UBOOT };
typedef struct { uint32_t ms, run, held; } bootguard_hold_t;
static uint32_t bootguard_hold_step(bootguard_hold_t *h, uint32_t both)
{
    if (!h->held) {                  /* the start: both closed from the first read, or no hold */
        if (!both)
            return HOLD_NONE;
        if (++h->run < BOOTGUARD_HOLD_SURE)
            return HOLD_WAIT;
        h->held = 1;
        h->run = h->ms = 0;
        return HOLD_WAIT;
    }
    if (both) {
        h->run = 0;
        return ++h->ms >= BOOTGUARD_HOLD_MS ? HOLD_UBOOT : HOLD_WAIT;
    }
    return ++h->run >= BOOTGUARD_HOLD_SURE ? HOLD_CAL : HOLD_WAIT;
}
#endif
