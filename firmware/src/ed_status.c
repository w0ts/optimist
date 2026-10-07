/* SPDX-License-Identifier: GPL-3.0-only */
/* Editor protocol: the transport and what plays, for the editor's transport bar and mixer (included by editor.c;
 * web/EDITOR_PROTOCOL.md "Transport and meters").
 *   53 STATUS  [op]  -> flags, BPM v14, section, then per track: step, 0, 0
 *              op 1 PLAY, 2 STOP (as the PLAY button: transport_req; recording, count-in and an external clock follow
 *              their own rules), none: only read. flags bit 0 playing, bit 1 a track armed for recording, bit 2 an
 *              external clock is followed; section: the live section playing (0..), 127 none; step: the index in the
 *              track's pattern of the step playing, 127 stopped; then two 0 bytes, where the track's peak was (a polled
 *              peak is the v8 fallback, which has no meters: the bytes stay so every editor reads the same reply)
 *   58 STREAM  (v9 push, WATCH bit 3) the same bytes with the peaks, then the master's peak (2 x 7 bit): at most every
 *              ED_STREAM_MS while watched, and only when it differs from the frame before or something sounds. A peak
 *              is the largest |output| since the frame before (meters.c: no peak falls between two frames), >> 2:
 *              0..16383, 8192 = 0 dBFS; the editor draws the meters' fall and hold (web/EDITOR_PROTOCOL.md "v9") */
enum { ED_STATUS = 53, ED_STREAM = 58 };
#define ED_STREAM_MS 40u                                /* 25 Hz */
#define ED_STREAM_N (4u + 3u * NTRK + 2u)
static struct {
    uint8_t last[ED_STREAM_N];                          /* the frame sent last */
    uint32_t ms, frames;
} est;

static uint32_t ed_pk14(int32_t pk) { pk >>= 2; return (uint32_t)(pk > 16383 ? 16383 : pk < 0 ? 0 : pk); }
/* flags, BPM, section, per track the step and (peaks) its peak, else 0 */
static void ed_status_out(int peaks)
{
    uint32_t c;
    ed_b((song.playing ? 1u : 0u) | (song.rec ? 2u : 0u) | (sy.src ? 4u : 0u));
    ed_v(song.g[G_BPM]);
#if FELUCCA_ARRANGER
    ed_b(live_sec < 0 ? 127u : (uint32_t)live_sec);
#else
    ed_b(127u);
#endif
    for (c = 0; c < NTRK; c++) {
        uint32_t pk = peaks ? ed_pk14(mt.ed[c]) : 0u;
        ed_b(song.playing && trk[c].seq_abs != SEQ_NONE ? trk[c].seq_idx & 0x7Fu : 127u);
        ed_b(pk);
        ed_b(pk >> 7);
    }
}

static int ed_status(uint32_t cmd, const uint8_t *a, uint32_t na)
{
    if (cmd != ED_STATUS)
        return 0;
    if (na >= 1u && (a[0] == 1u || a[0] == 2u))
        transport_req = a[0];
    ed_status_out(0);
    return 1;
}

/* main loop, while the stream is on; room: a push of this size fits now (ed_sync9.c ed9_room); 1 = sent */
static int ed_stream(uint32_t now, int room)
{
    uint32_t c, pk, live = 0;
    if (now - est.ms < ED_STREAM_MS || !room)
        return 0;
    est.ms = now;
    ed_begin(ED_STREAM);
    ed_status_out(1);
    pk = ed_pk14(mt.ed[NTRK] >> OUT_SHIFT);             /* the output: Q15 << OUT_SHIFT in abuf */
    ed_b(pk);
    ed_b(pk >> 7);
    for (c = 0; c < NTRK; c++)                          /* a peak byte not 0: something sounds */
        live |= (uint32_t)ed_out[10u + 3u * c] | ed_out[11u + 3u * c];
    live |= (uint32_t)ed_out[9u + 3u * NTRK] | ed_out[10u + 3u * NTRK];   /* (the master's) */
    if (!live && !memcmp(est.last, ed_out + 5, ED_STREAM_N))
        return 0;                                       /* silent and as before: nothing to say */
    memcpy(est.last, ed_out + 5, ED_STREAM_N);
    for (c = 0; c <= NTRK; c++)
        mt.ed[c] = 0;                                   /* the next frame: the peaks from now */
    est.frames++;
    ed_send();
    return 1;
}
