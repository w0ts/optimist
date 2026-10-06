/* SPDX-License-Identifier: GPL-3.0-only */
/* Fixed load scenarios for measurements in the emulator (FELUCCA_BENCH, never in a release).
 * Everything happens at fixed audio blocks (bench_block, in the audio interrupt), so two builds
 * render the same samples whatever their main loop does:
 *   1  three FM6 parts (STRINGS, TINE EP, FM GLASS), 3 + 3 + 2 notes: the 8 voices
 *   2  three ANALOG SUPER PAD parts (ANALOG 2's swarm), 3 + 3 + 2 notes
 *   3  FM6 TINE EP, ANALOG SUPER PAD, FM6 STRINGS, 3 + 3 + 2 notes
 *   4  scenario 1, and at setup a typical section (16 steps a track) encoded once and decoded BENCH_DECODES
 *      times (sections.c: the stage's cost; profile with FM1_HOT, sec_decode / BENCH_DECODES)
 *   5  ACID (FELUCCA_ENG_ACID): one part (ACID LINE) playing a 16-step line in sixteenths (accents,
 *      slides), no drums
 *   6  ACID on the three parts (ACID LINE, ACID SQR, ACID RAGE: saw, square, Soft drive), the line
 *      transposed per part, no drums
 *   7  scenario 6 with the drum groove
 *   8  the drum track alone on kit FELUCCA_BENCH_KIT (a kit UID; default the power-on kit): the groove of
 *      tests/regress.c (kick, snare, clap, hats, toms, rim, crash, ride; 16ths at 120 BPM), or with
 *      FELUCCA_BENCH_NOTE one sound (that GM note) four times a second; no synth part plays
 *   9  scenario 2's three parts with scenario 8's groove
 * (before the integration: the DX7 and SUPER engines, gone since: FM6 and ANALOG 2 take their places)
 * with a drum groove (1 to 4, 7) (kick, snare, hats in eighths at 120 BPM) and the presets' FX sends. The notes
 * start again every 2 s. FELUCCA_BENCH_SAVE=1: a project save (flash erase + program) from the main
 * loop 1.5 s in, while it all plays. */
#ifndef FELUCCA_BENCH_SAVE
#define FELUCCA_BENCH_SAVE 0
#endif
static void set_engine_of(track_t *t, uint32_t ei);        /* ui.c */
static void apply_preset_to(track_t *t, uint32_t pi);
static void project_save(uint32_t slot);                   /* project.c */
#if FELUCCA_BENCH == 4 && SEC_LOGGED
static uint32_t sec_bench(uint32_t n);                     /* sections.c */
#endif

#define BENCH_EIGHTH 172u                                  /* blocks of an eighth at 120 BPM (0.25 s) */
#define BENCH_PHRASE 2756u                                 /* blocks of 2 s */
static const uint8_t BENCH_SETUP[3][3][2] = {              /* scenario, part: engine, preset */
    {{ENG_IX_FM6, 7}, {ENG_IX_FM6, 0}, {ENG_IX_FM6, 8}},
    {{0, 17}, {0, 17}, {0, 17}},
    {{ENG_IX_FM6, 0}, {0, 17}, {ENG_IX_FM6, 7}},
};
static const uint8_t BENCH_NOTES[NPART][3] = {{48, 52, 55}, {60, 64, 67}, {72, 76, 0}};
static struct {
    uint32_t blk;
    volatile uint8_t save_req;
    uint32_t saves;
    uint32_t decoded;                                      /* (scenario 4: the record's length + decodes done) */
} bench;

#ifndef FELUCCA_BENCH_KIT
#define FELUCCA_BENCH_KIT DRUM_DEFAULT_KIT                 /* (every scenario: the drum track's kit) */
#endif
#ifndef FELUCCA_BENCH_NOTE
#define FELUCCA_BENCH_NOTE 0
#endif
#define BENCH_GROOVE16 (FELUCCA_BENCH >= 8)                /* (8, 9: the 16th groove on any kit) */
#if BENCH_GROOVE16
#define BENCH_SIXTEENTH 86u                                /* blocks of a 16th at 120 BPM (~0.125 s) */
static const uint8_t BENCH_GROOVE[16][4] = {               /* (tests/regress.c DRUM_GROOVE) */
    {36, 42, 49, 0}, {42, 0}, {42, 37, 0}, {36, 42, 0}, {38, 39, 42, 0}, {42, 0}, {46, 0}, {42, 37, 0},
    {36, 42, 51, 0}, {42, 0}, {36, 42, 0}, {44, 0}, {38, 39, 42, 0}, {43, 0}, {48, 46, 0}, {38, 0}};
#endif

#define BENCH_ACID (FELUCCA_BENCH >= 5 && FELUCCA_BENCH <= 7)
#if BENCH_ACID
#define BENCH_16TH 86u                                     /* blocks of a sixteenth at 120 BPM */
#define BENCH_ACID_PARTS (FELUCCA_BENCH == 5 ? 1u : NPART)
/* the line (tests/regress.c's ACID line): note (0 = rest), flags (1 accent, 2 slide into the next) */
static const uint8_t BENCH_ACID_N[16] = {45, 45, 57, 45, 0, 48, 45, 55, 45, 0, 57, 52, 45, 48, 0, 50};
static const uint8_t BENCH_ACID_F[16] = {1, 0, 2, 0, 0, 0, 1, 2, 0, 0, 1, 0, 0, 2, 0, 1};
static uint8_t bench_acid_held[NPART];
#endif

static void bench_setup(void)                              /* boot, after felucca_init (main loop) */
{
    uint32_t p, s = FELUCCA_BENCH == 4 ? 0u : FELUCCA_BENCH == 9 ? 1u : (FELUCCA_BENCH - 1u) % 3u;
    TDRUM->p[P_E0] = (int16_t)FELUCCA_BENCH_KIT;
    if (FELUCCA_BENCH == 8) {
        song.g[G_BPM] = 120;
        return;
    }
#if BENCH_ACID
    for (p = 0; p < NPART; p++) {                          /* ACID LINE, ACID SQR, ACID RAGE; LEGATO slides */
        set_engine_of(&trk[p], ENG_IX_ACID);
        apply_preset_to(&trk[p], p);
        trk[p].engine = trk[p].eng_req;
        trk[p].p[P_VOICE] = V_LEGATO;
    }
    song.g[G_BPM] = 120;
    (void)s;
    return;
#endif
    for (p = 0; p < NPART; p++) {
        set_engine_of(&trk[p], BENCH_SETUP[s][p][0]);
        apply_preset_to(&trk[p], BENCH_SETUP[s][p][1]);
        trk[p].engine = trk[p].eng_req;
    }
    song.g[G_BPM] = 120;
#if FELUCCA_BENCH == 4 && SEC_LOGGED
    {
        uint32_t i, k;
        for (i = 0; i < NTRK; i++)
            for (k = 0; k < 16u; k += i == TRK_DRUM ? 1u : 2u)
                if (i == TRK_DRUM)
                    dstep_set(&trk[i].dstep[k], k % 4u == 0u ? 0u : 4u, LV_NORM, 0);
                else {
                    trk[i].step[k].note[0] = (uint8_t)(48u + k + 5u * i);
                    trk[i].step[k].n = 1;
                    trk[i].step[k].time = ST_NOTE;
                }
        bench.decoded = sec_bench(100u);                   /* (sections.c: one encode, 100 decodes) */
    }
#endif
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
#if BENCH_GROOVE16
    {
        uint32_t i;
        if (FELUCCA_BENCH_NOTE && b % (BENCH_SIXTEENTH * 2u) == 0u)
            trk_note_on(TDRUM, FELUCCA_BENCH_NOTE, 100);
        if (!FELUCCA_BENCH_NOTE && b % BENCH_SIXTEENTH == 0u)
            for (i = 0; i < 4u && BENCH_GROOVE[(b / BENCH_SIXTEENTH) % 16u][i]; i++)
                trk_note_on(TDRUM, BENCH_GROOVE[(b / BENCH_SIXTEENTH) % 16u][i], i ? 90u : 110u);
        if (FELUCCA_BENCH == 9 && b % BENCH_PHRASE == 0u) {
            if (b)
                bench_notes(0);
            bench_notes(1);
        }
        (void)e;
        return;
    }
#endif
#if BENCH_ACID
    {   /* each part: a step's note on at its start, off at half the step unless it slides into the next
         * (then off after the next note's on: LEGATO) */
        uint32_t k = (b / BENCH_16TH) % 16u, ph = b % BENCH_16TH, p;
        for (p = 0; p < BENCH_ACID_PARTS; p++) {
            uint32_t n = BENCH_ACID_N[k] ? BENCH_ACID_N[k] + 7u * p : 0u, old = bench_acid_held[p];
            if (ph == 0u && n) {
                trk_note_on(&trk[p], n, BENCH_ACID_F[k] & 1u ? 127u : 100u);
                bench_acid_held[p] = (uint8_t)n;
                if (old && old != n)
                    trk_note_off(&trk[p], old);
            } else if (ph == 0u && old) {
                trk_note_off(&trk[p], old);
                bench_acid_held[p] = 0;
            }
            if (ph == BENCH_16TH / 2u && bench_acid_held[p] && !(BENCH_ACID_F[k] & 2u)) {
                trk_note_off(&trk[p], bench_acid_held[p]);
                bench_acid_held[p] = 0;
            }
        }
    }
    if (FELUCCA_BENCH != 7)
        return;                                            /* (5, 6: no drums) */
#endif
    if (!BENCH_ACID && b % BENCH_PHRASE == 0u) {
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
static volatile uint32_t bench_sigs[BENCH_NSIG][6] __attribute__((section(".pool")));   /* (volatile: only the emulator reads it; the pool: RAM is full) */
static HOT void bench_sig(uint32_t k, const int32_t *b, uint32_t n)   /* (HOT: the mix in RAM calls it) */
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
