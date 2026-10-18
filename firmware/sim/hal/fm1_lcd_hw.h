/* SPDX-License-Identifier: GPL-3.0-only */
/* The simulator's LCD wiring (firmware/hal/fm1_lcd_hw.h): the bytes src/display/lcd.c sends go to a model of the
 * ST7789's column / row address set and memory write (sim.c sim_lcd_*), which fills a 240 x 240 RGB565 buffer. */
#pragma once
#include <stdint.h>
#include "fm1_cc.h"

static uint32_t fm1_lcd_timeouts;
static uint8_t sim_lcd_bl = 16;                   /* the backlight's duty (0..16), as the PWM last set it */

static void sim_lcd_cmd(uint8_t c);               /* sim.c */
static void sim_lcd_data(const uint8_t *p, uint32_t n);

FM1_INLINE void fm1_lcd_hw_init(void) {}
FM1_INLINE void fm1_lcd_baud(uint32_t b) { (void)b; }
FM1_INLINE void fm1_lcd_bl_scan(void) {}
FM1_INLINE void fm1_lcd_bl_tick(uint32_t duty) { sim_lcd_bl = (uint8_t)(duty > 16u ? 16u : duty); }
static void fm1_lcd_wait(void) {}
FM1_INLINE void fm1_lcd_deselect(void) {}
FM1_INLINE void fm1_lcd_send_cmd(uint8_t c) { sim_lcd_cmd(c); }
FM1_INLINE void fm1_lcd_send_data(const void *p, uint32_t n) { sim_lcd_data((const uint8_t *)p, n); }
