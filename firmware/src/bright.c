/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Charles Vestal (X0X: charlesvestal/fm1-x0x 61654ba, the backlight PWM and its 8 steps) */
/* MENU > BRIGHT (FELUCCA_BRIGHT, backports.h): the screen's backlight, 1..8 (8 = full, as before). The HAL
 * PWMs PA2 from the 10 kHz TIMER5 ISR (hal/fm1_lcd_hw.h fm1_lcd_bl_tick, main.c). X0X keeps the level in a spare
 * settings byte, 0 = full so older saves are unchanged; here it is appended to the settings record
 * (project.c persist_t.bright, the same 0 = full): an older record reads as full, a build without the switch
 * reads the record without it. Included by ui_menu.c. */
static uint8_t bl_dim;                          /* 0 = 8 (full) .. 7 = 1 */
static const uint8_t BL_DUTY[8] = {16, 13, 10, 7, 5, 3, 2, 1};   /* of 16 (X0X: roughly even steps to the eye) */
static uint32_t bright_level(void) { return 8u - (bl_dim & 7u); }
static void bright_set(uint32_t level) { bl_dim = (uint8_t)(8u - (level < 1u ? 1u : level > 8u ? 8u : level)); }
