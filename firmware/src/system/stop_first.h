/* SPDX-License-Identifier: GPL-3.0-only */
/* An update (or UBOOT from the host) starts: the transport stops first, as the STOP key does (seq_stop in the audio
 * block: song stop, notes off, MIDI OUT all-notes-off, MIDI STOP when the clock out is on). Asks for it and waits
 * for the audio block to carry it out (at most 100 ms), before anything is written. Needs song, transport_req,
 * fm1_delay_ms and fm1_wdt_feed. Also the release's: fm1-base's main loop, APP_UPDATE_BEGIN (fm1base/optimist/ui/
 * opt_update.h). */
static void stop_first(void)
{
    uint32_t i;
    if (!song.playing && !transport_req)
        return;
    transport_req = 2;
    for (i = 0; i < 100u && (song.playing || transport_req); i++) {
        fm1_wdt_feed();
        fm1_delay_ms(1);
    }
}
