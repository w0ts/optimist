/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 isod89 (SLOOP 2.3, github.com/isod89/sloop-fm1 d691ba7: menu LIGHTS / KEYS / NOTES, after
 * Felucca 1.0.1 #35 by Leo Kuroshita; NOTES by renebohne, PR #11) */
/* The keys' lights (always built; included by panel.c). HOME held, the menu:
 *   NOTES   ON / OFF : the keys of what the selected track sounds (keylit.c: its steps, its
 *           ARP, its held voices; the drum hits) light, and glow under the layers whose keys are tiles. Here it
 *           is KEYLIT's switch at run time (on: as KEYLIT always was), not a second way of lighting the notes.
 * The buttons' and keys' backlight (SLOOP 2.3's LIGHTS and KEYS) is gone (the user, 2026-10-29): always off. Settings of the
 * FM-1, not of a project: the settings record's SLOOP 2.3 word (project.c bp23_word): bit 8 NOTES OFF; bits 0..7 (LIGHTS,
 * KEYS) are read and ignored, written as 0. */
static uint8_t lights_notes_off;               /* NOTES OFF: the keys do not show what sounds */

static uint32_t lights_word(void)
{
    return (uint32_t)(lights_notes_off != 0u) << 8;
}
static void lights_from_word(uint32_t w)
{
    lights_notes_off = (uint8_t)((w >> 8) & 1u);
}
