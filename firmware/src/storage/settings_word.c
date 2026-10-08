/* SPDX-License-Identifier: GPL-3.0-only */
/* The settings word of the FM-1 (included by project.c, and by the host tests): one word of the settings record that
 * holds the settings with no field of their own. */
#if BP23_SET
/* the settings SLOOP 2.3 made settings of the FM-1 (not of a project), one word: bit 9 the REC screen's MODE
 * TEMPO, bit 10 its START COUNT (FELUCCA_REC_MODES); bits 0..3 LIGHTS, 4..7 KEYS, 8 NOTES OFF (FELUCCA_LIGHTS).
 * 0 = as before (a record without the word reads as 0). A build without a switch keeps its bits as read.
 * Bits 11..12 SYNC (G_SYNC xor SYNC_AUTO: a word with none reads AUTO, the default; the HOME menu's, device-wide, no
 * longer a project's), bit 14 MIDI OUT = SEQ, bit 15 MIDI IN = CLOCK (SLOOP 2.4's bits: FELUCCA_MIDI_OUT,
 * FELUCCA_MIDI_INCLK), bit 16 USB SERIAL (SLOOP 2.4's, FELUCCA_CDC: usb.c usb_serial, 0 = off, the console not presented),
 * bits 17..20 the visualiser's style (SLOOP 2.4's, FELUCCA_VIS), bits 21..22 the Optimist UI's drum mixer view
 * (FELUCCA_UI 1: 0 four strips, 1 eight; ui/optimist/op_dmix.c dm_view), bit 23 its cards (0 four in a line, 1
 * 2 x 2 big; op_state.c op_cards) */
static uint32_t bp23_kept;                         /* the bits this build has no switch for, as read */
static uint8_t sync_boot = SYNC_AUTO;              /* the SYNC the settings record had (main.c felucca_init applies it) */
static uint32_t bp23_word(void)
{
    uint32_t w = bp23_kept;
#if FELUCCA_REC_MODES
    w = (w & ~(3u << 9)) | (uint32_t)(rec_tempo != 0u) << 9 | (uint32_t)(rec_count != 0u) << 10;
#endif
#if FELUCCA_LIGHTS
    w = (w & ~0x1FFu) | lights_word();
#endif
#if FELUCCA_MIDI_OUT
    w = (w & ~(1u << 14)) | (uint32_t)(bp_set[BPS_MOUT] != 0) << 14;     /* (SLOOP 2.4: the same bits) */
#endif
#if FELUCCA_MIDI_INCLK
    w = (w & ~(1u << 15)) | (uint32_t)(bp_set[BPS_MIN] != 0) << 15;
#endif
#if FELUCCA_CDC
    w = (w & ~(1u << 16)) | (uint32_t)(usb_serial != 0u) << 16;
#endif
    w = (w & ~(3u << 11)) | (((uint32_t)song.g[G_SYNC] & 3u) ^ SYNC_AUTO) << 11;
#if FELUCCA_VIS
    w = (w & ~(15u << 17)) | (uint32_t)(vis_style % 12u) << 17;   /* the visualiser's style (SLOOP 2.4: the same bits) */
#endif
#if FELUCCA_UI == 1
    w = (w & ~(3u << 21)) | (uint32_t)(dm_view % DMV_N) << 21;   /* the drum mixer's strips (op_dmix.c) */
    w = (w & ~(1u << 23)) | (uint32_t)(op_cards == CARDS_2X2) << 23;   /* the cards 1x4 / 2x2 (op_state.c) */
#endif
    return w;
}
static void bp23_from_word(uint32_t w)
{
    bp23_kept = w;
#if FELUCCA_REC_MODES
    rec_tempo = (uint8_t)((w >> 9) & 1u);
    rec_count = (uint8_t)((w >> 10) & 1u);
#endif
#if FELUCCA_LIGHTS
    lights_from_word(w);
#endif
#if FELUCCA_MIDI_OUT
    bp_set[BPS_MOUT] = (int16_t)((w >> 14) & 1u);
#endif
#if FELUCCA_MIDI_INCLK
    bp_set[BPS_MIN] = (int16_t)((w >> 15) & 1u);
#endif
#if FELUCCA_CDC
    usb_serial = (uint8_t)((w >> 16) & 1u);         /* (presented from the next start: usb_start) */
#endif
#if FELUCCA_VIS
    vis_style = (uint8_t)(((w >> 17) & 15u) % 12u);    /* (0 in SLOOP 2.3 = OSCILLOSCOPE) */
#endif
#if FELUCCA_UI == 1
    dm_view = (uint8_t)(((w >> 21) & 3u) % DMV_N);      /* (a word without the bits: four strips) */
    op_cards = (uint8_t)((w >> 23) & 1u);             /* (a word without the bit: the four cards in a line) */
#endif
    sync_boot = (uint8_t)(((w >> 11) & 3u) ^ SYNC_AUTO);
    song.g[G_SYNC] = sync_boot;
}
#endif
