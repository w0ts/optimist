/* SPDX-License-Identifier: GPL-3.0-only */
/* HOLD (HOME menu > SYSTEM): how long a layer button (FX, SEQ, ENV, ..) is held before its map shows, and the longest press
 * that still counts as a tap (ui_layers.c TAP_MS / SHOW_MS, ui_input.c layers_input). The layer itself is active from the
 * press: a key touched while it is held acts at once, whatever the map shows. hold_sel 0 = 350 ms (the default: a settings
 * word without the field reads it), 1 = 250, 2 = 500. Kept in the settings word, bits 21..22 (storage/settings_word.c).
 * Included by ui/panel.c and storage/settings_word.c (and so by the host tests). */
#ifndef FELUCCA_HOLD_H
#define FELUCCA_HOLD_H
#include <stdint.h>
#define HOLD_N 3u
static uint8_t hold_sel;
static const uint16_t HOLD_T[HOLD_N] = {350, 250, 500};
#define HOLD_MS ((uint32_t)HOLD_T[hold_sel % HOLD_N])
#endif
