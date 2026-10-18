/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * FM6 engine: Kerem Kilic (Melodee, github.com/keremimo/melodee, fm6_store.c at e459da5), GPL-3.0-only;
 * ported to SLOOP; cut down to receiving DX7 SysEx 2026-10-18 */
/* DX7 SysEx into FM6 (FELUCCA_FM6_SYSEX; the frames are collected by eng_fm6.c fm6_sx_byte, in the USB ISR):
 *   F0 43 0n 00 01 1B <155 bytes> <checksum> F7   a voice (VCED) replaces the voice of the FM6 track (the user then
 *                                                 SAVEs it as a user preset, or the project keeps it)
 *   F0 43 0n 09 20 00 <4096 bytes> <checksum> F7  32 voices (VMEM): a pick list at the end of the FM6 track's
 *                                                 PRESETS (nbank.c), in RAM until power-off, to audition and SAVE AS
 * on any channel n. The FM6 track: the selected one when it plays FM6, else track n + 1, else the first FM6 track.
 * Nothing is sent back, nothing is stored (the user bank U01..U32, STORE, SEND and the parameter-change messages
 * went 2026-10-18). */
static int fm6_is(uint32_t k) { return k < NPART && ENG_IS(ENGINES[trk[k].eng_req % NENGINES], FM6); }

static int fm6_part(uint32_t ch)                         /* the FM6 part a DX7 message on channel ch is for, -1 */
{
    uint32_t k;
    if (fm6_is(song.sel))
        return (int)song.sel;
    if (fm6_is(ch))
        return (int)ch;
    for (k = 0; k < NPART; k++)
        if (fm6_is(k))
            return (int)k;
    return -1;
}

#if FELUCCA_FM6_SYSEX
static uint8_t fm6_chk(const uint8_t *p, uint32_t n)    /* DX7 checksum: data + it = 0 mod 128 */
{
    uint32_t s = 0;
    while (n--)
        s += *p++;
    return (uint8_t)(-s & 0x7Fu);
}

static void fm6_service(void)                            /* main loop: a DX7 frame from USB-MIDI */
{
    uint32_t i, kind = fm6_rx_ready;
    char nm[12];
    int p;
    if (!kind)
        return;
    RING_PUBLISH();                                      /* read the frame only after the flag */
    if (kind == 1u) {                                    /* one voice */
        p = fm6_part(fm6_vox[2] & 15u);
        if (fm6_chk(fm6_vox + 6, 155) != fm6_vox[161]) {
            ui_message("FM6: BAD CHECKSUM");
        } else if (p < 0) {
            ui_message("FM6: NO FM6 TRACK");
        } else {
            fm1_irq_off();
            for (i = 0; i < 155u; i++)
                fm6_set(fm6_ed[p], i, fm6_vox[6 + i]);
            for (i = 0; i < 6u; i++)
                fm6_ed[p][FV_ON + i] = 1;
            fm6_pt[p].panic = 1;                         /* a new voice: the notes stop, as in Dexed */
            fm1_irq_on();
            fm6_name(nm, fm6_ed[p]);
            ui_say("FM6 VOICE ", nm);
        }
    } else if (fm6_chk(fm6_pick, 4096) == fm6_pick[4096]) {   /* 32 voices */
        for (i = 0; i < 4096u; i++)
            fm6_pick[i] &= 0x7Fu;
        fm6_pick_ok = 1;
        ui_message("FM6: 32 VOICES IN PRESETS");
    } else {
        ui_message("FM6: BAD CHECKSUM");
    }
    ui.force = 1;
    RING_PUBLISH();
    fm6_rx_ready = 0;
}
#else
static void fm6_service(void) {}
#endif
