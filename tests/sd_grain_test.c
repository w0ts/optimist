/* SPDX-License-Identifier: GPL-3.0-only */
/* GRAIN and STRETCH on SD card mode's sets (the user, 2026-10-29: they stream card sets; the caches:
 * firmware/src/storage/sd/sd_stream_rc.h / .c, eng_grain.c, eng_stretch.c) in the host's render (tests/hostsim.c) with
 * the card itself: the byte-level card model (tests/sd_card_model.h, its bus at 1.9 MB/s: the FM-1's CMD18 by DMA at
 * 20 MHz; the pessimist's 1 MB/s too), full FatFs on the FAT32 image, the zones' WAVs written on it as gen_samples.py
 * --card wrote them, their runs from FatFs (sst_src_load), the real scheduler (sst_strip between the screen's strips,
 * sst_service in the idle wait, as sd_stream_test.c's main loop) and the audio an interrupt of 8 blocks (an audio half,
 * 5.8 ms) in the middle of the runs. Each render forked from one state, the flash's (every set in this host build's
 * flash; SMP_CARD_SET the test's switch) beside the card's:
 *   - an ideal card (the main loop runs dry between two blocks: every want served before the block renders): every
 *     GRAIN factory preset on PIANO / VIBES / FLUTE and every STRETCH preset bit-identical to the same render with each
 *     zone's whole file in RAM (its head: the engines' own reads of the files' samples, no cache; the card's files are
 *     the sources at their own rate since 2026-11-04, no longer the flash zones' samples), no grain or window late or
 *     missed;
 *   - the card in time (the audio interrupting the runs): the reads counted (at once, late, missed), the render's
 *     largest sample-to-sample step no more than the whole files' by a margin (a grain never starts or stops on a step);
 *   - the card stalling 100 ms every 600 ms (a card busy writing): misses counted, still no step;
 *   - SAMPLE streaming a set beside a GRAIN part: SAMPLE never dry, the bus shared;
 *   - the cache taken away mid-phrase (a plan's gate): the grains fade, no step;
 *   - the numbers: hit / late / miss, CPU ns a sample (the render, card against flash), the cache's KB, the bus's KB/s;
 *   - a part's own WAV (SRC CARD, the user, 2026-11-05: a 60 s file, 44.1 kHz mono, a 20 s stereo one, a 0.5 s one) at the
 *     black FM-1's 840 KB/s (8.57 MHz) with the real scheduler: GRAIN frozen, moving, spread over the whole file, at
 *     its densest, STRETCH at 1x and slow, each against the same render on the whole file in RAM: the reads counted, no
 *     step over the whole file's, never garbage (an ideal card's render the whole file's where nothing was late).
 *   sd_grain_test FAT32.img DIR (gen_samples.py --card DIR) */
#define FELUCCA_SD_STREAM 1
#define FELUCCA_SD_FATFS 1
#ifndef FELUCCA_SD_RING_KB
#define FELUCCA_SD_RING_KB 4
#endif
static unsigned card_mask;                           /* the sets the test plays from the card */
#define SMP_CARD_SET(s) ((card_mask >> (s)) & 1u)
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <ctype.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "sd_card_model.h"
#undef SD_TICKS_MS
static uint32_t tick_ms = 1900u;                     /* the model's bytes a millisecond: its bus, 1.9 MB/s */
#define SD_TICKS_MS tick_ms
#include "storage/sd/sd_mmc.c"
#include "storage/sd/sd_diskio.c"
#include "storage/sd/sd_hash.c"
#include "storage/sd/sd_stream.c"

static FATFS fs __attribute__((aligned(4)));
static uint8_t src_mem[64u * 1024u] __attribute__((aligned(4)));
static uint8_t whole_mem[8u * 1024u * 1024u] __attribute__((aligned(4)));   /* (the reference: each zone whole) */
static uint8_t rc_mem[SST_NPART][SST_RC_BYTES(SST_RAND_KB)] __attribute__((aligned(4)));
static uint32_t rc_kb = SST_RAND_KB;                 /* (the part's cache: GRAIN's, STRETCH's) */
static uint8_t ring_mem[SST_N * SST_K * 512u] __attribute__((aligned(4)));
static uint32_t src_used;

/* ---- the sets on the card: PIANO, VIBES, FLUTE (the GRAIN presets' sets), their zones one source each ---- */
static const char *const SETS[3] = {"PIANO", "VIBES", "FLUTE"};
static uint32_t set_no[3], set_src[3], set_nz[3];

static uint32_t set_of(const char *name)
{
    uint32_t s;
    for (s = 0; s < SMP_NSETS && strcmp(SMP_SETS[s].name, name); s++)
        ;
    return s;
}

static int put_file(const char *from, const char *to)   /* a host file onto the card image */
{
    static uint8_t b[8192];
    FILE *f = fopen(from, "rb");
    FIL o;
    UINT k;
    size_t n;
    if (!f || f_open(&o, to, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK)
        return 0;
    while ((n = fread(b, 1, sizeof b, f)) > 0)
        if (f_write(&o, b, (UINT)n, &k) != FR_OK || k != n)
            return 0;
    fclose(f);
    return f_close(&o) == FR_OK;
}

static const char *set_dir(const char *s)        /* a set's folder on the card: PIANO's Piano (gen_samples.py card_set_dir) */
{
    static char d[16];
    uint32_t i;
    for (i = 0; s[i] && i + 1u < sizeof d; i++)
        d[i] = (char)(i ? tolower((unsigned char)s[i]) : s[i]);
    d[i] = 0;
    return d;
}

/* set k's zones: written, loaded as sources (pre_ms: their heads; GRAIN's and STRETCH's need none) */
static void load_set(uint32_t k, const char *dir, uint32_t si0, uint32_t pre_ms)
{
    char from[512], to[32];
    uint32_t z, used;
    set_no[k] = set_of(SETS[k]);
    set_src[k] = si0;
    set_nz[k] = SMP_SETS[set_no[k]].nz;
    for (z = 0; z < set_nz[k]; z++) {
        snprintf(from, sizeof from, "%s/Optimist/Samples/%s/z%u.wav", dir, set_dir(SETS[k]), z);
        snprintf(to, sizeof to, "%.1s%uZ%u.WAV", SETS[k], pre_ms, z);
        CHECK(put_file(from, to), "%s written", to);
        src_used = (src_used + 3u) & ~3u;
        CHECK(sst_src_load(si0 + z, to, pre_ms * 4410u / 50u, src_mem + src_used, sizeof src_mem - src_used, &used) == FR_OK,   /* (the
                                                                                     * head in bytes: pre_ms at 44.1 kHz 16-bit mono) */
              "%s loaded", to);
        src_used += used;
        {
            const smp_zone_t *fz = &SMP_ZONES[SMP_SETS[set_no[k]].z0 + z];
            const sst_src_t *s = &sst_src[si0 + z];
            CHECK(s->rate == fz->rate ? s->frames == fz->n : s->rate == 2u * fz->rate && s->frames + 2u >= 2u * fz->n,
                  "%s Z%u: the flash zone's frames at the source's rate (%u at %u, the flash's %u at %u)", SETS[k], z,
                  s->frames, s->rate, fz->n, fz->rate);
        }
    }
}

/* the reference (a forked child's): set k's zones again, each whole in its head (every read from RAM) */
static int32_t free_si = -1;                         /* the part's own WAV: this source, this file, these EDIT values */
static const char *free_name;
static const int16_t *free_e;
static uint32_t free_kb;
static void load_whole(uint32_t k)
{
    char to[32];
    uint32_t z, used, at = 0;
    for (z = 0; z < set_nz[k]; z++) {
        snprintf(to, sizeof to, "%.1s%uZ%u.WAV", SETS[k], 0u, z);
        CHECK(sst_src_load(set_src[k] + z, to, 0x7FFFFFFFu, whole_mem + at, sizeof whole_mem - at, &used) == FR_OK &&   /* (a head
                                                                                     * longer than any file: all its data) */
                  sst_src[set_src[k] + z].hlen + 4u > sst_src[set_src[k] + z].dlen, "%s whole", to);
        at += (used + 3u) & ~3u;
    }
}

/* ---- the audio: a part's notes on a schedule, a half (8 blocks) an interrupt ---- */
typedef struct {
    uint32_t at;                                     /* the sample it comes at */
    uint8_t part, note, on;
} ev_t;
static ev_t EV[128];
static uint32_t nev, evi, out_n, total;
static int32_t *OUT;                                 /* (the render: mmap, shared with the forked children) */
static uint32_t next_half, half_ticks, stall_every, stall_ms, next_stall, gate_at;
static double cpu_ns;

static void ev_add(uint32_t at, uint32_t part, int note, int on)
{
    if (nev < sizeof EV / sizeof EV[0] && note >= 0 && note <= 127) {
        EV[nev].at = at, EV[nev].part = (uint8_t)part, EV[nev].note = (uint8_t)note, EV[nev].on = (uint8_t)on;
        nev++;
    }
}

static void events(void)
{
    while (evi < nev && EV[evi].at <= out_n) {
        if (EV[evi].on)
            trk_note_on(&trk[EV[evi].part], EV[evi].note, 100);
        else
            trk_note_off(&trk[EV[evi].part], EV[evi].note);
        evi++;
    }
}

static void mix(void)
{
    int32_t o[2 * CTL];
    uint32_t i;
    struct timespec a, b;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &a);
    mix_block(o, CTL);
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &b);
    cpu_ns += (double)(b.tv_sec - a.tv_sec) * 1e9 + (double)(b.tv_nsec - a.tv_nsec);
    for (i = 0; i < CTL && out_n + i < total; i++)
        OUT[out_n + i] = o[2 * i];
    out_n += CTL;
    if (gate_at && out_n >= gate_at) {               /* the plan's gate: the cache let go (eng_grain.c: the grains fade) */
        gate_at = 0;
        for (i = 0; i < SST_NPART; i++)
            sst_rc[i].on = 0;
    }
}

static void audio(void)                              /* the interrupt: a half, at its time */
{
    uint32_t b;
    if (clk < next_half)
        return;
    next_half += half_ticks;
    if (stall_every && out_n >= next_stall) {        /* the card busy (writing): no token for stall_ms */
        C.stall_until = clk + stall_ms * tick_ms;
        next_stall += stall_every;
    }
    for (b = 0; b < 8u && out_n < total; b++) {
        events();
        mix();
    }
}

static void wait_ticks(uint32_t n)
{
    uint32_t end = clk + n;
    while (clk < end) {
        clk++;
        audio();
    }
}

/* the main loop as main.c's: a frame of 4 strips (sst_strip between them), then the idle wait (sst_service) */
static void run_real(void)
{
    uint32_t s, t0, strip = 1500u * tick_ms / 1000u;
    clk_hook = audio;
    next_half = clk + half_ticks;
    while (out_n < total) {
        t0 = clk;
        for (s = 0; s < 4u; s++) {
            (void)sst_strip();
            wait_ticks(strip);
        }
        while (clk - t0 < 15u * tick_ms && out_n < total)
            if (!sst_service())
                wait_ticks(tick_ms / 10u);
    }
    clk_hook = 0;
}

static void run_ideal(void)                          /* every want served before its block */
{
    clk_hook = 0;
    while (out_n < total) {
        events();
        while (sst_service())
            ;
        mix();
    }
}

/* ---- one render, forked: the flash's (card 0) or the card's ---- */
enum { M_IDEAL, M_REAL, M_STALL, M_GATE, M_WHOLE };
typedef struct {
    uint32_t hit, late, miss, dry, nostream, blocks, rcblocks, ms;
    double ns;
} st_t;
static st_t *ST;

static uint32_t eng_no(const char *name)
{
    uint32_t e;
    for (e = 0; e < NENGINES && strcmp(ENGINES[e]->name, name); e++)
        ;
    return e;
}

/* the part on a set: on the card (its zones the sources at set_src, its cache placed) or the flash's */
static void part_on(uint32_t part, uint32_t k, int card)
{
    sst_bank_src[part] = (uint8_t)set_src[k];
    sst_bank_n[part] = (uint8_t)(card ? set_nz[k] : 0u);
    sst_rc_place(part, card ? rc_mem[part] : 0, rc_kb);
    if (card)
        card_mask |= 1u << set_no[k];
    sst_bank_gen++;
}

static void render(int32_t *L, uint32_t eng, uint32_t pi, uint32_t k, int card, int mode, int with_sample)
{
    pid_t pid;
    fflush(stdout);
    pid = fork();
    int stt;
    if (pid == 0) {
        track_t *t = &trk[0];
        uint32_t i, t0;
        OUT = L;
        memset(L, 0, total * sizeof *L);
        card_mask = 0;
        host_tracks_init();
        for (i = 0; i < SST_NPART; i++)
            sst_rc_place(i, 0, 0);
        sst_rings(ring_mem, SST_N);
        host_preset(t, eng, pi);
        t->p[P_DIST] = t->p[P_CHOR] = t->p[P_DLY] = t->p[P_REV] = 0;
        if (free_si >= 0) {
            uint32_t used = 0;
            if (mode == M_WHOLE)
                CHECK(sst_src_load((uint32_t)free_si, free_name, SST_PRE_WHOLE, whole_mem, sizeof whole_mem, &used) == FR_OK,
                      "%s whole", free_name);
            for (i = 0; i < 8u; i++)
                t->p[P_E0 + i] = free_e[i];
            t->p[P_E0] = (int16_t)SMP_CARD;
            sst_bank_src[0] = (uint8_t)free_si;
            sst_bank_n[0] = 1;
            sst_bank_free = 1;
            sst_rc_place(0, rc_mem[0], free_kb);
            sst_bank_gen++;
        } else {
            if (mode == M_WHOLE)
                load_whole(k);
            part_on(0, k, card);
        }
        if (with_sample) {                           /* part 2 on SAMPLE, VIBES from the card (streamed) */
            uint32_t se = eng_no("SAMPLE"), p2;
            for (p2 = 0; p2 < ENGINES[se]->npresets && (uint32_t)ENGINES[se]->presets[p2].e[0] != set_no[1]; p2++)
                ;
            host_preset(&trk[1], se, p2);
            trk[1].p[P_DIST] = trk[1].p[P_CHOR] = trk[1].p[P_DLY] = trk[1].p[P_REV] = 0;
            sst_bank_src[1] = (uint8_t)with_sample;  /* (the VIBES sources with heads: the second load's) */
            sst_bank_n[1] = (uint8_t)set_nz[1];
            card_mask |= 1u << set_no[1];
        }
        evi = out_n = 0;
        cpu_ns = 0;
        sst_stats_reset();
        stall_every = mode == M_STALL ? 600u * 441u / 10u : 0u;
        stall_ms = 100u;
        next_stall = stall_every;
        gate_at = mode == M_GATE ? total / 2u : 0u;
        t0 = clk;
        if (mode == M_IDEAL || mode == M_WHOLE)
            run_ideal();
        else
            run_real();
        ST->hit = sst_rc[0].hit, ST->late = sst_rc[0].late, ST->miss = sst_rc[0].miss;
        ST->dry = sst_dry, ST->nostream = sst_nostream, ST->blocks = sst_st.blocks, ST->rcblocks = sst_rcs.blocks;
        ST->ms = (clk - t0) / tick_ms;
        ST->ns = cpu_ns / total;
        fflush(stdout);
        _exit(0);
    }
    waitpid(pid, &stt, 0);
    CHECK(pid > 0 && WIFEXITED(stt) && WEXITSTATUS(stt) == 0, "the render ran");
}

/* a click's measure: the largest jump off the waveform's own slope (the second difference; a step of s in a smooth
 * signal shows as s, a loud smooth passage as little) */
static uint32_t max_step(const int32_t *L)
{
    uint32_t i, m = 0, d;
    for (i = 2; i < total; i++) {
        d = (uint32_t)abs(L[i] - 2 * L[i - 1] + L[i - 2]);
        if (d > m)
            m = d;
    }
    return m;
}

static uint32_t differ(const int32_t *A, const int32_t *B, uint32_t *first)
{
    uint32_t i, n = 0;
    *first = total;
    for (i = 0; i < total; i++)
        if (A[i] != B[i] && !n++)
            *first = i;
    return n;
}

static uint32_t sounding(const int32_t *L)
{
    uint32_t i, n = 0;
    for (i = 0; i < total; i++)
        n += L[i] != 0;
    return n;
}

/* the phrases: GRAIN each zone's root held 0.8 s, a chord, an octave up; STRETCH a note, a chord, frames long */
static void phrase(uint32_t k, int stretch)
{
    const smp_set_t *ss = &SMP_SETS[set_no[k]];
    uint32_t z, at = 2205u;
    int r;
    nev = 0;
    if (stretch) {
        r = SMP_ZONES[ss->z0].root16 / 16;
        ev_add(at, 0, r, 1), ev_add(at + 88200u, 0, r, 0);
        at += 132300u;
        ev_add(at, 0, r + 4, 1), ev_add(at, 0, r + 7, 1);
        ev_add(at + 88200u, 0, r + 4, 0), ev_add(at + 88200u, 0, r + 7, 0);
        total = at + 88200u + 66150u;
        return;
    }
    for (z = 0; z < ss->nz; z++) {
        r = SMP_ZONES[ss->z0 + z].root16 / 16;
        ev_add(at, 0, r, 1), ev_add(at + 35280u, 0, r, 0);
        at += 48510u;
    }
    r = SMP_ZONES[ss->z0].root16 / 16;
    ev_add(at, 0, r, 1), ev_add(at, 0, r + 4, 1), ev_add(at, 0, r + 7, 1);
    ev_add(at + 52920u, 0, r, 0), ev_add(at + 52920u, 0, r + 4, 0), ev_add(at + 52920u, 0, r + 7, 0);
    at += 61740u;
    ev_add(at, 0, r + 12, 1), ev_add(at + 44100u, 0, r + 12, 0);
    total = at + 44100u + 44100u;
}

static int32_t *A, *B;

/* engine eng's presets on set k: ideal bit-identical, in time, stalling, gated */
static void presets(uint32_t eng, uint32_t k, int stretch, uint32_t *played)
{
    uint32_t pi, d, first, sa, sb, x2;
    st_t fl, c;
    for (pi = 0; pi < ENGINES[eng]->npresets; pi++) {
        const char *nm = ENGINES[eng]->presets[pi].name;
        if ((uint32_t)ENGINES[eng]->presets[pi].e[0] != set_no[k])
            continue;
        phrase(k, stretch);
        render(A, eng, pi, k, 1, M_WHOLE, 0);
        fl = *ST;
        render(B, eng, pi, k, 1, M_IDEAL, 0);
        c = *ST;
        d = differ(A, B, &first);
        sa = max_step(A);
        /* a 22.05 kHz set (VIBES) and STRETCH: every read in time, the whole files' render. GRAIN on a 44.1 kHz set
         * (PIANO, FLUTE: since 2026-11-04 at their sources' rate) spreads its grains over twice the bytes, more than its
         * cache holds even on an ideal card: a grain whose block is not in yet is skipped (counted late / missed, eng_grain.c,
         * no click), the rest the whole files' */
        x2 = sst_src[set_src[k]].rate > 40000u && !stretch;
        CHECK((x2 || (d == 0 && c.late == 0 && c.miss == 0)) && c.hit > 0 && sounding(A) > 44100u &&
                  max_step(B) <= sa + sa / 4u + 64u,
              "%s %s on %s, an ideal card against the whole files: %u samples differ (first at %u), %u hit %u late %u "
              "missed, %u sounding, step %u (whole %u)",
              ENGINES[eng]->name, nm, SETS[k], d, first, c.hit, c.late, c.miss, sounding(A), max_step(B), sa);
        printf("  %-7s %-11s %s  ideal: identical %s, %u reads at once %u late %u missed; CPU %.1f ns a sample (whole "
               "%.1f, %+.0f %%)\n", ENGINES[eng]->name, nm, SETS[k], d ? "NO" : "yes", c.hit, c.late, c.miss, c.ns, fl.ns,
               fl.ns > 0 ? 100.0 * (c.ns - fl.ns) / fl.ns : 0.0);
        render(B, eng, pi, k, 1, M_REAL, 0);
        c = *ST;
        sb = max_step(B);
        d = differ(A, B, &first);
        CHECK(sb <= sa + sa / 4u + 64u, "%s %s, the card in time: its largest step %u, the whole files' %u", ENGINES[eng]->name,
              nm, sb, sa);
        printf("  %22s in time: %u at once %u late %u missed; %u samples differ; step %u (whole %u); %u blocks, "
               "%u KB/s (%.1f %% of the bus)\n", "", c.hit, c.late, c.miss, d, sb, sa, c.rcblocks,
               c.ms ? c.rcblocks * 512u / c.ms : 0u, c.ms ? 100.0 * c.rcblocks * 512.0 / (c.ms * (double)tick_ms) : 0.0);
        render(B, eng, pi, k, 1, M_STALL, 0);
        c = *ST;
        sb = max_step(B);
        CHECK(sb <= sa + sa / 4u + 64u, "%s %s, the card stalling: its largest step %u, the whole files' %u",
              ENGINES[eng]->name, nm, sb, sa);
        CHECK(sounding(B) > sounding(A) / 3u, "%s %s, the card stalling: it still sounds (%u of %u)", ENGINES[eng]->name,
              nm, sounding(B), sounding(A));
        printf("  %22s stalls 100 ms / 600 ms: %u at once %u late %u missed; step %u\n", "", c.hit, c.late, c.miss, sb);
        render(B, eng, pi, k, 1, M_GATE, 0);
        sb = max_step(B);
        CHECK(sb <= sa + sa / 4u + 64u, "%s %s, the cache let go mid-phrase: its largest step %u, the whole files' %u",
              ENGINES[eng]->name, nm, sb, sa);
        for (d = 0, first = total / 2u + 13230u; first < total; first++)   /* (0.3 s on: silent) */
            if (abs(B[first]) > 64)          /* (-54 dBFS: the master's DC blocker settling) */
                d++;
        CHECK(d == 0, "%s %s, the cache let go: silent from 0.3 s after (%u samples over 64)", ENGINES[eng]->name, nm, d);
        (*played)++;
    }
}

/* ---- a part's own WAV ---- */
static int put_free(const char *name, uint32_t frames, uint32_t rate, uint32_t ch, uint32_t seed)
{
    static int16_t b[2048];
    uint8_t h[44];
    uint32_t dl = frames * 2u * ch, at = 0, i, n, c;
    FIL o;
    UINT k;
    memset(h, 0, sizeof h);
    memcpy(h, "RIFF", 4); h[4] = (uint8_t)(36u + dl); h[5] = (uint8_t)((36u + dl) >> 8); h[6] = (uint8_t)((36u + dl) >> 16);
    h[7] = (uint8_t)((36u + dl) >> 24); memcpy(h + 8, "WAVEfmt ", 8); h[16] = 16; h[20] = 1; h[22] = (uint8_t)ch;
    h[24] = (uint8_t)rate; h[25] = (uint8_t)(rate >> 8); h[26] = (uint8_t)(rate >> 16);
    n = rate * 2u * ch; h[28] = (uint8_t)n; h[29] = (uint8_t)(n >> 8); h[30] = (uint8_t)(n >> 16);
    h[32] = (uint8_t)(2u * ch); h[34] = 16; memcpy(h + 36, "data", 4);
    h[40] = (uint8_t)dl; h[41] = (uint8_t)(dl >> 8); h[42] = (uint8_t)(dl >> 16); h[43] = (uint8_t)(dl >> 24);
    if (f_open(&o, name, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK || f_write(&o, h, 44, &k) != FR_OK)
        return 0;
    while (at < frames) {
        n = frames - at < 1024u / ch ? frames - at : 1024u / ch;
        for (i = 0; i < n; i++)
            for (c = 0; c < ch; c++)
                b[i * ch + c] = (int16_t)(9000.0 * sin((at + i) * (0.03 + 0.002 * seed + 0.001 * c)) +
                                          7000.0 * sin((at + i) * 0.0011 + c) * sin((at + i) * 0.11));
        if (f_write(&o, b, n * 2u * ch, &k) != FR_OK || k != n * 2u * ch)
            return 0;
        at += n;
    }
    return f_close(&o) == FR_OK;
}

static void free_phrase(void)                        /* a note held 3 s, then a chord 1 s */
{
    nev = 0;
    ev_add(2205u, 0, 60, 1), ev_add(134505u, 0, 60, 0);
    ev_add(136710u, 0, 64, 1), ev_add(136710u, 0, 67, 1), ev_add(180810u, 0, 64, 0), ev_add(180810u, 0, 67, 0);
    total = 198450u;
}

/* a grain that loses its sector over the bus holds its sample and fades: a corner, under -36 dBFS (a click is a step) */
#define FREE_STEP 512u
/* one scenario: the whole file in RAM, an ideal card, the card in time, the card stalling; the numbers */
static void free_case(const char *what, uint32_t eng, const char *file, uint32_t si, uint32_t kb, const int16_t *e)
{
    uint32_t d, first, sa, sb, ms;
    st_t c;
    free_si = (int32_t)si, free_name = file, free_e = e, free_kb = kb;
    free_phrase();
    render(A, eng, 0, 0, 1, M_WHOLE, 0);
    sa = max_step(A);
    CHECK(sounding(A) > 44100u, "%s: the whole file sounds (%u)", what, sounding(A));
    render(B, eng, 0, 0, 1, M_IDEAL, 0);
    c = *ST;
    d = differ(A, B, &first);
    CHECK(max_step(B) <= sa + FREE_STEP, "%s, an ideal card: its largest step %u, the whole file's %u", what, max_step(B), sa);
    CHECK(c.late + c.miss || !d, "%s, an ideal card, none late: %u samples differ from the whole file's (garbage)", what, d);
    printf("  %-34s %2u KB ideal: %u at once %u late %u missed, %u samples differ\n", what, kb, c.hit, c.late, c.miss, d);
    render(B, eng, 0, 0, 1, M_REAL, 0);
    c = *ST;
    sb = max_step(B);
    ms = c.ms ? c.ms : 1u;
    CHECK(sb <= sa + FREE_STEP, "%s, in time: its largest step %u, the whole file's %u", what, sb, sa);
    CHECK(sounding(B) > sounding(A) / 4u, "%s, in time: it sounds (%u of %u)", what, sounding(B), sounding(A));
    printf("  %36s in time: %u at once %u late %u missed; %u KB/s of the bus (%u %%), step %u (whole %u)\n", "", c.hit, c.late,
           c.miss, c.rcblocks * 512u / ms, c.rcblocks * 512u / ms * 100u / tick_ms, sb, sa);
    render(B, eng, 0, 0, 1, M_STALL, 0);
    c = *ST;
    CHECK(max_step(B) <= sa + FREE_STEP, "%s, stalling: its largest step %u, the whole file's %u", what, max_step(B), sa);
    printf("  %36s stalls: %u at once %u late %u missed\n", "", c.hit, c.late, c.miss);
    free_si = -1;
}

/* the numbers at 840 KB/s: the file lengths and settings a part's own WAV takes */
static void free_tests(uint32_t ge, uint32_t se)
{
    /* SRC, POS, SIZE, DENS, PTCH, SPRD, RAND, TONE */
    static const int16_t FROZEN[8] = {0, 45, 95, 85, 0, 0, 0, 127}, CLOUD[8] = {0, 45, 95, 85, 0, 35, 12, 80},
                         WIDE[8] = {0, 64, 60, 85, 0, 127, 10, 127}, DENSE[8] = {0, 45, 95, 127, 0, 127, 10, 127},
                         SMALL[8] = {0, 60, 40, 100, 0, 8, 0, 127};
    /* SRC, POS, STRCH, SMEAR, TONE ... */
    static const int16_t ST1[8] = {0, 30, 0, 100, 127, 0, 0, 0}, ST8[8] = {0, 90, 60, 100, 127, 0, 0, 0};
    uint32_t used, at = 0;
    tick_ms = 840u;
    half_ticks = 256u * tick_ms * 10u / 441u;
    CHECK(put_free("L60.WAV", 60u * 44100u, 44100u, 1u, 1u), "a 60 s file");
    CHECK(put_free("S20.WAV", 20u * 44100u, 44100u, 2u, 2u), "a 20 s stereo file");
    CHECK(put_free("H30.WAV", 30u * 22050u, 22050u, 1u, 3u), "a 30 s 22.05 kHz file");
    CHECK(sst_src_load(10u, "L60.WAV", 0, src_mem + 60000u, 4000u, &used) == FR_OK, "the 60 s file loaded");
    at = 60000u + ((used + 3u) & ~3u);
    CHECK(sst_src_load(11u, "S20.WAV", 0, src_mem + at, 2000u, &used) == FR_OK, "the stereo file loaded");
    at += (used + 3u) & ~3u;
    CHECK(sst_src_load(12u, "H30.WAV", 0, src_mem + at, 1000u, &used) == FR_OK, "the 22.05 kHz file loaded");
    printf("sd_grain_test: a part's own WAV at %u KB/s (the black FM-1's 8.57 MHz), the screen's strips drawing\n", tick_ms);
    free_case("GRAIN 60 s, frozen (SPRD 0)", ge, "L60.WAV", 10u, SST_RAND_KB, FROZEN);
    free_case("GRAIN 60 s, SPRD 35", ge, "L60.WAV", 10u, SST_RAND_KB, CLOUD);
    free_case("GRAIN 60 s, SPRD 8, short grains", ge, "L60.WAV", 10u, SST_RAND_KB, SMALL);
    free_case("GRAIN 60 s, SPRD 127", ge, "L60.WAV", 10u, SST_RAND_KB, WIDE);
    free_case("GRAIN 60 s, SPRD 127, DENS 127", ge, "L60.WAV", 10u, SST_RAND_KB, DENSE);
    free_case("GRAIN 20 s stereo, SPRD 35", ge, "S20.WAV", 11u, SST_RAND_KB, CLOUD);
    free_case("GRAIN 30 s 22.05 kHz, SPRD 35", ge, "H30.WAV", 12u, SST_RAND_KB, CLOUD);
    free_case("STRETCH 60 s, 1x", se, "L60.WAV", 10u, SST_STRETCH_KB, ST1);
    free_case("STRETCH 60 s, slow", se, "L60.WAV", 10u, SST_STRETCH_KB, ST8);
    free_case("STRETCH 20 s stereo, 1x", se, "S20.WAV", 11u, 2u * SST_STRETCH_KB, ST1);
    free_case("STRETCH 20 s stereo, slow", se, "S20.WAV", 11u, 2u * SST_STRETCH_KB, ST8);
}

int main(int argc, char **argv)
{
    uint32_t sectors, k, played = 0, ge = eng_no("GRAIN"), se = eng_no("STRETCH"), pi, d, first, i;
    uint8_t *img;
    st_t c;
    if (argc != 3) {
        printf("usage: sd_grain_test FAT32.img DIR (gen_samples.py --card DIR)\n");
        return 2;
    }
    CHECK(ge < NENGINES && se < NENGINES, "GRAIN and STRETCH built");
    A = mmap(0, 2u * 30u * 44100u * sizeof *A + 4096u, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0);
    if (A == MAP_FAILED || ge >= NENGINES || se >= NENGINES)
        return 2;
    B = A + 30u * 44100u;
    ST = (st_t *)(void *)(B + 30u * 44100u);
    img = load(argv[1], &sectors);
    card(img, sectors, 0, 1, 1, 0);
    dma_on = 1;
    memset(&fs, 0, sizeof fs);
    sd_gone = 0;
    sd_dstat = STA_NOINIT;
    CHECK(f_mount(&fs, "", 1) == FR_OK, "mount");
    load_set(0, argv[2], 0, 0);
    load_set(1, argv[2], set_nz[0], 0);
    load_set(2, argv[2], set_nz[0] + set_nz[1], 0);
    i = set_nz[0] + set_nz[1] + set_nz[2];           /* (VIBES again with 50 ms heads: SAMPLE's streams beside) */
    {
        uint32_t keep = set_src[1];
        load_set(1, argv[2], i, 50);
        set_src[1] = keep;
    }
    half_ticks = 256u * tick_ms * 10u / 441u;
    printf("sd_grain_test: a cache %u KB (%u slots; SD_RING %u KB), %u B in the arena with its tags; the bus %u KB/s\n",
           SST_RAND_KB, SST_RAND_KB * 2u, (unsigned)FELUCCA_SD_RING_KB, (unsigned)SST_RC_BYTES(SST_RAND_KB), tick_ms);
    for (k = 0; k < 3; k++)
        presets(ge, k, 0, &played);
    rc_kb = SST_STRETCH_KB;                          /* (STRETCH's cache) */
    printf("sd_grain_test: STRETCH's cache %u KB, %u B in the arena\n", SST_STRETCH_KB,
           (unsigned)SST_RC_BYTES(SST_STRETCH_KB));
    presets(se, 0, 1, &played);
    rc_kb = SST_RAND_KB;
    {
        uint32_t want = ENGINES[se]->npresets, q, j;    /* (GRAIN's presets on PIANO / VIBES / FLUTE: several each since issue 147) */
        for (q = 0; q < ENGINES[ge]->npresets; q++)
            for (j = 0; j < 3u; j++)
                want += (uint32_t)ENGINES[ge]->presets[q].e[0] == set_no[j];
        CHECK(played == want, "every GRAIN and STRETCH preset on its set (%u of %u)", played, want);
    }

    /* SAMPLE streaming VIBES on part 2 beside GRAIN on PIANO: SAMPLE never dry, the bus shared */
    for (pi = 0; pi < ENGINES[ge]->npresets && (uint32_t)ENGINES[ge]->presets[pi].e[0] != set_no[0]; pi++)
        ;
    phrase(0, 0);
    {                                                /* (part 2's notes: round VIBES' first root, 0.4 s each) */
        uint32_t at;
        for (at = 4410u; at + 30000u < total && nev + 2u <= sizeof EV / sizeof EV[0]; at += 22050u) {
            ev_add(at, 1, SMP_ZONES[SMP_SETS[set_no[1]].z0].root16 / 16 + (int)((at / 22050u) % 5u), 1);
            ev_add(at + 17640u, 1, SMP_ZONES[SMP_SETS[set_no[1]].z0].root16 / 16 + (int)((at / 22050u) % 5u), 0);
        }
        for (k = 1; k < nev; k++) {                  /* (in time order) */
            ev_t e = EV[k];
            for (i = k; i > 0 && EV[i - 1].at > e.at; i--)
                EV[i] = EV[i - 1];
            EV[i] = e;
        }
    }
    render(B, ge, pi, 0, 1, M_REAL, (int)(set_nz[0] + set_nz[1] + set_nz[2]));
    c = *ST;
    CHECK(c.dry == 0 && c.nostream == 0, "SAMPLE beside GRAIN: SAMPLE never dry (%u dry, %u with no ring)", c.dry,
          c.nostream);
    printf("  SAMPLE (VIBES, streamed) beside GRAIN (PIANO): SAMPLE dry %u; grains %u at once %u late %u missed; "
           "the bus %u KB/s, the grains' %u KB/s\n", c.dry, c.hit, c.late, c.miss, c.ms ? c.blocks * 512u / c.ms : 0u,
           c.ms ? c.rcblocks * 512u / c.ms : 0u);

    /* the pessimist's bus (1 MB/s): LOFI CLOUD in time */
    tick_ms = 1000u;
    half_ticks = 256u * tick_ms * 10u / 441u;
    phrase(0, 0);
    render(A, ge, pi, 0, 1, M_WHOLE, 0);
    render(B, ge, pi, 0, 1, M_REAL, 0);
    c = *ST;
    d = differ(A, B, &first);
    CHECK(max_step(B) <= max_step(A) + max_step(A) / 4u + 64u, "LOFI CLOUD at 1 MB/s: its largest step %u, the whole files' %u",
          max_step(B), max_step(A));
    printf("  GRAIN LOFI CLOUD at 1 MB/s: %u at once %u late %u missed; %u samples differ\n", c.hit, c.late, c.miss, d);
    free_tests(ge, se);
    printf("%s: %d checks, %d failed\n", fails ? "FAIL" : "ok", checks, fails);
    return fails ? 1 : 0;
}
