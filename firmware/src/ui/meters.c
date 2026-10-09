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
    int32_t lane[DRUM_LANES];           /* for the TRACKS screen: each drum lane (drum_mix.c dlm_pk) */
#if FELUCCA_MASTER_COMP
    uint8_t gr[NTRK + DRUM_LANES];      /* for the TRACKS screen: the COMP inserts' largest reduction, dB x 4: the
                                         * parts', the drum bus's, each lane's (its voices': fx.c dins_comp) */
#endif
#if FELUCCA_MASTER_COMP
    uint8_t grc, grl;                   /* for the editor: the COMP's and the LIMIT's largest reduction, quarter dB */
#endif
} mt __attribute__((section(".bss.meters")));   /* (its own section: not merged with the audio path's globals, which would move their addressing in the RAM code) */

#if FELUCCA_MASTER_COMP
static uint32_t mt_gr_q4(int32_t gr16)  /* a reduction, log2 Q16 -> dB x 4 (master_comp.c mc_take_gr's scale), 0..127 */
{
    int32_t c = (gr16 * 3083) >> 23;
    return (uint32_t)(c < 0 ? 0 : c > 127 ? 127 : c);
}
#endif
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
    for (c = 0; c < DRUM_LANES; c++) {  /* the drum lanes (drum_mix.c dlm_pk), as the tracks */
        int32_t pk;
        fm1_irq_off();
        pk = dlm_pk[c];
        dlm_pk[c] = 0;
        fm1_irq_on();
        if (pk > mt.lane[c])
            mt.lane[c] = pk;
    }
#if FELUCCA_MASTER_COMP
    {   /* the COMP inserts' gain reduction now (a word each, read only), the largest since the screen took it */
        uint32_t g;
        for (c = 0; c < NTRK; c++) {
            g = mt_gr_q4(c < NPART ? tcomp[c].gr16 : dbus_comp.gr16);
            mt.gr[c] = (uint8_t)(g > mt.gr[c] ? g : mt.gr[c]);
        }
#if DINS
        for (c = 0; c < NDRUM; c++)     /* a sound's COMP runs per voice: its lane's */
            if (drums.v[c].active && (g = mt_gr_q4(dins_comp[c].gr16)) != 0u) {
                uint32_t l = dlm_lane(drums.v[c].note);
                if (l < DRUM_LANES)
                    mt.gr[NTRK + l] = (uint8_t)(g > mt.gr[NTRK + l] ? g : mt.gr[NTRK + l]);
            }
#endif
    }
#endif
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
#if FELUCCA_MASTER_COMP
    {   /* the master's gain reduction (master_comp.c): the editor's largest since its frame, LIMIT > GR this half's */
        uint32_t c, l;
        mc_take_gr(&c, &l);
        mt.grc = (uint8_t)(c > mt.grc ? c : mt.grc);
        mt.grl = (uint8_t)(l > mt.grl ? l : mt.grl);
        mc_gr_view = (int16_t)-(int32_t)((c + l + 2u) >> 2);   /* whole dB, negative */
    }
#endif
    mt.half = h;
}

/* the TRACKS screen: track c's largest |output| since its last look */
static int32_t meter_ui_take(uint32_t c)
{
    int32_t pk = mt.ui[c];
    mt.ui[c] = 0;
    return pk;
}
/* the TRACKS screen: drum lane l's largest |output| since its last look */
static int32_t meter_lane_take(uint32_t l)
{
    int32_t pk = mt.lane[l & 15u];
    mt.lane[l & 15u] = 0;
    return pk;
}
/* the TRACKS screen: row r's COMP reduction since its last look, dB x 4 (r: the tracks, then the 16 lanes); 0: none */
static uint32_t meter_gr_take(uint32_t r)
{
#if FELUCCA_MASTER_COMP
    uint32_t g = r < NTRK + DRUM_LANES ? mt.gr[r] : 0u;
    if (r < NTRK + DRUM_LANES)
        mt.gr[r] = 0;
    return g;
#else
    (void)r;
    return 0;
#endif
}
