/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 isod89 (SLOOP 2.3, github.com/isod89/sloop-fm1 d691ba7: menu LIGHTS / KEYS / NOTES, after
 * Felucca 1.0.1 #35 by Leo Kuroshita; NOTES by renebohne, PR #11) */
/* Lights for playing in the dark (FELUCCA_LIGHTS, backports23.h; included by panel.c). HOME held, the menu:
 *   LIGHTS  OFF / LOW / MID / HIGH: every button glows at that level (hal/fm1_input.h fm1_led_bg: a pulse a scan
 *           frame), so the labels read on a black FM-1. What is active (the page, PLAY, REC, the octave) stays at
 *           full light and still blinks as before.
 *   KEYS    OFF / C KEYS / WHITE KEYS: the C keys, or every white key, glow too (at the LIGHTS level).
 *   NOTES   ON / OFF (with FELUCCA_KEYLIT): the keys of what the selected track sounds (keylit.c: its steps, its
 *           ARP, its held voices; the drum hits) light, and glow under the layers whose keys are tiles. Here it
 *           is KEYLIT's switch at run time (on: as KEYLIT always was), not a second way of lighting the notes.
 * Settings of the FM-1, not of a project: the settings record's SLOOP 2.3 word (project.c bp23_word): bits 0..3
 * LIGHTS, 4..7 KEYS, 8 NOTES OFF. */
enum { LIGHTS_OFF, LIGHTS_LOW, LIGHTS_MID, LIGHTS_HIGH, LIGHTS_N };
enum { KEYS_OFF, KEYS_C, KEYS_WHITE, KEYS_N };
static uint8_t lights_lvl, lights_keys;
static uint8_t lights_notes_off;               /* NOTES OFF: the keys do not show what sounds */
static const uint16_t LIGHTS_NS[LIGHTS_N] = {0u, 500u, 1000u, 2000u};   /* the backlight pulse a frame (ns): a lit
                                                * LED ~95 us, the glow (landmarks) 4 us (fm1_input.h) */

static uint32_t lights_word(void)
{
    return (uint32_t)lights_lvl | (uint32_t)lights_keys << 4 | (uint32_t)(lights_notes_off != 0u) << 8;
}
static void lights_from_word(uint32_t w)
{
    lights_lvl = (uint8_t)((w & 15u) < LIGHTS_N ? (w & 15u) : LIGHTS_OFF);
    lights_keys = (uint8_t)(((w >> 4) & 15u) < KEYS_N ? ((w >> 4) & 15u) : KEYS_OFF);
    lights_notes_off = (uint8_t)((w >> 8) & 1u);
}

/* the keys the backlight lights (menu KEYS): the Cs, or every white key; bit k = key k (0 = F3) */
static uint32_t lights_keys_mask(void)
{
    uint32_t k, m = 0;
    if (!lights_lvl || !lights_keys)
        return 0u;
    for (k = 0; k < 27u; k++) {
        uint32_t pc = (53u + k) % 12u;             /* key 0 = F3 (53) */
        if (lights_keys == KEYS_C ? pc == 0u : ((0xAB5u >> pc) & 1u) != 0u)   /* 0xAB5: C D E F G A B */
            m |= 1u << k;
    }
    return m;
}
