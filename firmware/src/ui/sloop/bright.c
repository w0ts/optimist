/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Charles Vestal (X0X: charlesvestal/fm1-x0x 61654ba, the backlight PWM and its 8 steps) */
/* MENU > BRIGHT (FELUCCA_BRIGHT, backports.h): the screen's backlight, 1..8 (8 = full, as before). The HAL
 * PWMs PA2 from the 10 kHz TIMER5 ISR (hal/fm1_lcd_hw.h fm1_lcd_bl_tick, main.c). Included by ui_menu.c.
 *
 * EXPERIMENTAL, not tried on a real FM-1 here. On X0X a real FM-1 froze at level 1 (duty 1/16) and, the
 * level being saved, froze again at every boot until reflashed (X0X issue #2; X0X removed the feature in
 * c6f2bf6; the cause was never found, the PWM is suspected by elimination). So here:
 *   - every boot starts at full (bright_boot, from project.c persist_boot): the level is never restored, the
 *     settings record stores 0 (= full) in its persist_t.bright slot, so an older build reading it is full too;
 *   - the lowest step is 4/16, not 1/16 (BL_DUTY), until a hardware test clears the low duties.
 * A level chosen in the MENU lasts until the next power-off or reset. */
static uint8_t bl_dim;                          /* 0 = 8 (full) .. 7 = 1 */
static const uint8_t BL_DUTY[8] = {16, 14, 12, 10, 8, 6, 5, 4};  /* of 16; X0X went down to 1, see above */
static uint32_t bright_level(void) { return 8u - (bl_dim & 7u); }
static void bright_set(uint32_t level) { bl_dim = (uint8_t)(8u - (level < 1u ? 1u : level > 8u ? 8u : level)); }
static void bright_boot(void) { bl_dim = 0; }   /* full, whatever was saved (see above) */
