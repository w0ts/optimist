/* SPDX-License-Identifier: GPL-3.0-only */
/* KNOB COLORS (HOME menu > SYSTEM, both UIs): the user prints coloured knob caps (knob 1 blue, 2 yellow, 3 pink,
 * 4 orange). With the option ON every UI element driven by knob k (its dial and arc or value bar, its label, its
 * page card, a held step's edit) takes knob k's colour. OFF (the default) leaves every colour as it was.
 * knob_col(k, def) is the one switch: def when OFF (or k is not a knob), the cap's colour when ON. Kept in the
 * settings word, bit 25 (storage/settings_word.c); not carried into a SLOOP 2.4 export (sl24_word's mask).
 * Included by ui/panel.c and storage/settings_word.c (and so by the host tests). */
#ifndef FELUCCA_KNOBCOL_H
#define FELUCCA_KNOBCOL_H
#include <stdint.h>
#define KNOBCOL_N 4u
static uint8_t knob_colors;                                   /* 0 OFF, 1 ON */
/* RGB565, bright enough on the dark panel: blue #1E88E5, yellow #F4C430, pink #E0218A, orange #FF5A1F */
static const uint16_t KNOB_COL[KNOBCOL_N] = {0x1C5Cu, 0xF626u, 0xE111u, 0xFAC3u};
static uint16_t knob_col(uint32_t k, uint16_t def)
{
    return knob_colors && k < KNOBCOL_N ? KNOB_COL[k] : def;
}
#endif
