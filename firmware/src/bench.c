/* SPDX-License-Identifier: GPL-3.0-only */
/* Fixed load scenarios for measurements in the emulator (FELUCCA_BENCH, never in a release).
 * Everything happens at fixed audio blocks (bench_block, in the audio interrupt), so two builds
 * render the same samples whatever their main loop does:
 *   1  three DX7 parts (DX STRINGS, DX 8OP KEY, DX PAD), 3 + 3 + 2 notes: the 8 voices
 *   2  three SUPER PAD parts, 3 + 3 + 2 notes
 *   3  DX 8OP KEY, SUPER PAD, DX STRINGS, 3 + 3 + 2 notes
 * with a drum groove (kick, snare, hats in eighths at 120 BPM) and the presets' FX sends. The notes
 * start again every 2 s. FELUCCA_BENCH_SAVE=1: a project save (flash erase + program) from the main
 * loop 1.5 s in, while it all plays. */
#ifndef FELUCCA_BENCH_SAVE
#define FELUCCA_BENCH_SAVE 0
#endif
static void set_engine_of(track_t *t, uint32_t ei);        /* ui.c */
static void apply_preset_to(track_t *t, uint32_t pi);
static void project_save(uint32_t slot);                   /* project.c */

#define BENCH_EIGHTH 172u                                  /* blocks of an eighth at 120 BPM (0.25 s) */
#define BENCH_PHRASE 2756u                                 /* blocks of 2 s */
static const uint8_t BENCH_SETUP[3][3][2] = {              /* scenario, part: engine, preset */
    {{10, 20}, {10, 27}, {10, 25}},
    {{9, 1}, {9, 1}, {9, 1}},
    {{10, 27}, {9, 1}, {10, 20}},
};
static const uint8_t BENCH_NOTES[NPART][3] = {{48, 52, 55}, {60, 64, 67}, {72, 76, 0}};
static struct {
    uint32_t blk;
    volatile uint8_t save_req;
    uint32_t saves;
} bench;

static void bench_setup(void)                              /* boot, after felucca_init (main loop) */
{

    uint32_t p, s = (FELUCCA_BENCH - 1u) % 3u;
    for (p = 0; p < NPART; p++) {
        set_engine_of(&trk[p], BENCH_SETUP[s][p][0]);
        apply_preset_to(&trk[p], BENCH_SETUP[s][p][1]);
        trk[p].engine = trk[p].eng_req;
    }
    song.g[G_BPM] = 120;
}

static void bench_notes(int on)
{
    uint32_t p, i;
    for (p = 0; p < NPART; p++)
        for (i = 0; i < 3u; i++)
            if (BENCH_NOTES[p][i]) {
                if (on)
                    trk_note_on(&trk[p], BENCH_NOTES[p][i], 100);
                else
                    trk_note_off(&trk[p], BENCH_NOTES[p][i]);
            }
}

static void bench_block(void)                              /* audio interrupt, after the block's events */
{
    uint32_t b = bench.blk++, e = b / BENCH_EIGHTH;
    if (b % BENCH_PHRASE == 0u) {
        if (b)
            bench_notes(0);
        bench_notes(1);
    }
    if (b % BENCH_EIGHTH == 0u) {
        trk_note_on(TDRUM, 42, 90);                        /* hat */
        if (e % 4u == 0u)
            trk_note_on(TDRUM, 36, 110);                   /* kick on the beat */
        if (e % 4u == 2u)
            trk_note_on(TDRUM, 38, 100);                   /* snare on 2 and 4 */
    }
#if FELUCCA_BENCH_SAVE
    if (b == 2067u)
        bench.save_req = 1;
#endif
}

/* per block, the first BENCH_NSIG blocks: a signature of each part's render (0..2), the dry mix and the
 * reverb send before the buses (4, 5): two builds' first difference (words:ADDR:N in play_check) */
#define BENCH_NSIG 256u
static volatile uint32_t bench_sigs[BENCH_NSIG][6];   /* (volatile: only the emulator reads it) */
static void bench_sig(uint32_t k, const int32_t *b, uint32_t n)
{
    uint32_t i, h = 0x811C9DC5u, blk = bench.blk - 1u;     /* (bench_block already counted this block) */
    if (blk >= BENCH_NSIG || k >= 6u)
        return;
    for (i = 0; i < n; i++)
        h = (h ^ (uint32_t)b[i]) * 16777619u;
    bench_sigs[blk][k] = h;
}

static void bench_frame(void)                              /* main loop */
{
    if (bench.save_req) {
        bench.save_req = 0;
        project_save(0);
        bench.saves++;
    }
}
