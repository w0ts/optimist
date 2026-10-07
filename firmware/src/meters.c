/* SPDX-License-Identifier: GPL-3.0-only */
/* The level meters, taken in the main loop (never in the audio ISR). The ISR keeps each track's largest |output| in
 * trk[c].peak / drums.peak (fx.c mix_part, drums.c drums_mix: a running maximum it never lowers). Two readers want
 * "the largest since I last looked": the TRACKS screen (ui_draw.c draw_tracks, every UI frame) and the editor's
 * status stream (ed_status.c, 25 Hz). One tap empties the ISR's values once per audio half (5.8 ms) and keeps a
 * maximum for each reader, so neither steals the other's peaks.
 *
 * Before (Optimist 0.1, STATUS 53): the editor copied trk.peak every pass but only the TRACKS screen ever cleared
 * it, so with any other page on the device the editor read the loudest value since boot: the meters stood still
 * while playing and after STOP (measured over the emulator bridge, protocol v9 notes).
 *
 * Master: the output half the ISR rendered last (abuf, audio.c), scanned here while the editor streams; a half the
 * main loop does not see in time (a long screen draw) is counted in mt.missed. Scale of every peak: 32767 = 0 dBFS
 * (a track: before the master volume; the master: the output). */
#define METERS_C 1                      /* (ui_draw.c: the TRACKS screen takes its peaks from here) */
static struct {
    int32_t ui[NTRK];                   /* for the TRACKS screen */
    int32_t ed[NTRK + 1u];              /* for the editor: the tracks, then the master */
    uint32_t half;                      /* audio_halves at the last tap */
    uint32_t seen, missed;              /* master: halves scanned / gone by unscanned (while on) */
    uint8_t master;                     /* the editor streams: scan the output too */
} mt __attribute__((section(".bss.meters")));   /* (its own section: not merged with the audio path's globals, which would move their addressing in the RAM code) */

/* main loop, as often as it comes round: once per new audio half */
static void meter_tap(void)
{
    uint32_t c, h = audio_halves;
    if (h == mt.half)
        return;
    for (c = 0; c < NTRK; c++) {
        int32_t *src = c == TRK_DRUM ? &drums.peak : &trk[c].peak, pk;
        fm1_irq_off();                  /* (the ISR's read-modify-write of it may not fall in between) */
        pk = *src;
        *src = 0;
        fm1_irq_on();
        if (pk > mt.ui[c])
            mt.ui[c] = pk;
        if (pk > mt.ed[c])
            mt.ed[c] = pk;
    }
    if (mt.master) {
        const int32_t *o = &abuf[fm1_audio_free_half() * HALF_WORDS];   /* the half rendered last */
        int32_t pk = mt.ed[NTRK];
        for (c = 0; c < HALF_WORDS; c++) {
            int32_t a = o[c] < 0 ? -o[c] : o[c];
            if (a > pk)
                pk = a;
        }
        mt.ed[NTRK] = pk;               /* (abuf holds Q15 << OUT_SHIFT: shifted back where it is read) */
        mt.seen++;
        mt.missed += h - mt.half - 1u;
    }
    mt.half = h;
}

/* the TRACKS screen: track c's largest |output| since its last look */
static int32_t meter_ui_take(uint32_t c)
{
    int32_t pk = mt.ui[c];
    mt.ui[c] = 0;
    return pk;
}
