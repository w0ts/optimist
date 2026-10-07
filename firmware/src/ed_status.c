/* SPDX-License-Identifier: GPL-3.0-only */
/* Editor protocol: the transport and what plays, for the editor's transport bar and mixer (included by editor.c;
 * web/EDITOR_PROTOCOL.md "Transport and meters").
 *   53 STATUS  [op]  -> flags, BPM v14, section, then per track: step, 0, 0
 *              op 1 PLAY, 2 STOP (as the PLAY button: transport_req; recording, count-in and an external clock follow
 *              their own rules), none: only read. flags bit 0 playing, bit 1 a track armed for recording, bit 2 an
 *              external clock is followed; section: the live section playing (0..), 127 none; step: the index in the
 *              track's pattern of the step playing, 127 stopped; then two 0 bytes, where the track's peak was (the
 *              editor's meters are gone: the bytes stay so every editor reads the same reply) */
enum { ED_STATUS = 53 };

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
        ed_b(song.playing && trk[c].seq_abs != SEQ_NONE ? trk[c].seq_idx & 0x7Fu : 127u);
        ed_b(0u);                                       /* (the peak, 2 x 7 bit: 0, no meters) */
        ed_b(0u);
    }
    return 1;
}
