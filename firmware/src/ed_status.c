/* SPDX-License-Identifier: GPL-3.0-only */
/* Editor protocol: the transport and what plays, for the editor's transport bar and mixer (included by editor.c;
 * web/EDITOR_PROTOCOL.md "Transport and meters").
 *   53 STATUS  [op]  -> flags, BPM v14, section, then per track: step, peak (2 x 7 bit)
 *              op 1 PLAY, 2 STOP (as the PLAY button: transport_req; recording, count-in and an external clock follow
 *              their own rules), none: only read. flags bit 0 playing, bit 1 a track armed for recording, bit 2 an
 *              external clock is followed; section: the live section playing (0..), 127 none; step: the index in the
 *              track's pattern of the step playing, 127 stopped; peak: the largest |output| since the last STATUS
 *              (>> 2, 0..16383; ed_peaks keeps it while the TRACKS screen takes its own) */
enum { ED_STATUS = 53 };
static int32_t ed_pk[NTRK];

/* main loop, every pass: the tracks' peaks before the TRACKS screen clears them */
static void ed_peaks(void)
{
    uint32_t c;
    for (c = 0; c < NTRK; c++) {
        int32_t pk = c == TRK_DRUM ? drums.peak : trk[c].peak;
        if (pk > ed_pk[c])
            ed_pk[c] = pk;
    }
}

static int ed_status(uint32_t cmd, const uint8_t *a, uint32_t na)
{
    uint32_t c;
    if (cmd != ED_STATUS)
        return 0;
    if (na >= 1u && (a[0] == 1u || a[0] == 2u))
        transport_req = a[0];
    ed_b((song.playing ? 1u : 0u) | (song.rec ? 2u : 0u) | (sy.src ? 4u : 0u));
    ed_v(song.g[G_BPM]);
#if FELUCCA_ARRANGER
    ed_b(live_sec < 0 ? 127u : (uint32_t)live_sec);
#else
    ed_b(127u);
#endif
    for (c = 0; c < NTRK; c++) {
        int32_t pk = ed_pk[c] >> 2;
        ed_b(song.playing && trk[c].seq_abs != SEQ_NONE ? trk[c].seq_idx & 0x7Fu : 127u);
        pk = pk > 16383 ? 16383 : pk;
        ed_b((uint32_t)pk);
        ed_b((uint32_t)pk >> 7);
        ed_pk[c] = 0;
    }
    return 1;
}
