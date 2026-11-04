/* SPDX-License-Identifier: GPL-3.0-only */
/* The streams' budget at 44.1 kHz (the user, 2026-11-04: "Highest for all now": SD card mode's streams play the card's
 * samples at their own rate, 44.1 kHz where the source is; the bus speed is another agent's, SYSTEM > SD CARD) on the
 * byte-level card model (tests/sd_card_model.h, a tick a byte; SD_TICKS_MS its bytes a millisecond: the bus) with full
 * FatFs on the FAT32 image, the real scheduler (sd_stream.c: sst_strip between the screen's 4 strips of 1.5 ms a 15 ms
 * frame, sst_service in the idle wait, as main.c) and the audio an interrupt every 32 samples reading each voice's frames
 * through sst_frame, each checked against its file (GARBAGE fails everywhere). sd-card-mots' rings: SD_RING 4 (4 KB a
 * stream, 46 ms of 44.1 kHz 16-bit mono), 50 ms heads.
 *   - the table: 1 .. 8 voices of 44.1 kHz 16-bit mono at their pitch for 2.5 s at 1.9 MB/s (20 MHz by DMA), 937 KB/s
 *     (7.5 MHz's raw bytes) and 733 KB/s (measured at 7.5 MHz): the voices that ran dry, the bus read, the least lead;
 *   - a voice's need: 88.2 KB/s (44,100 x 2 B) at its pitch, twice an octave up; 8 at 1.9 MB/s: none dry;
 *   - over the bus (8 voices at 733 KB/s): every underrun counted (sst_dry: the voices that ran dry, each faded out by
 *     its engine, eng_sample.c), never garbage, the streams given back; then RECOVERED: as many voices as that bus
 *     carries play to their end, none dry, and the next 8 again drop and count, never a hang.
 *   - the pages on fm-1base's main loop at 840 KB/s (8.57 MHz; fm1b_loop, ui/opt_sd.h and device/main_fm1.c as they are: a frame
 *     every 16 ms, passes between only when it took less): the strips and bytes as the host simulator counted them on
 *     profile sd-card-mots (FM1B_SD_TIME=1, "sdstat"): SYSTEM > INFO before (its percent moves each frame: 8 strips, 113,760 B,
 *     every frame, one run a frame), SD CARD's page 2 before (13 blocks and commands a frame in the draw, one sample in the
 *     plan), then each now (INFO: at most 4 times a second, the changed lines' rows, 195,906 B in 27 strips in 2 s; reads
 *     between the strips, the rings filled after a frame).
 *   sd_stream_bus_test FAT32.img */
#define FELUCCA_SD_FATFS 1
#define FELUCCA_SD_RING_KB 4
#define AINL static inline
#define IN_POOL
#include "sd_card_model.h"
#undef SD_TICKS_MS
static uint32_t tick_ms = 1900u;                     /* the bus: bytes a millisecond */
#define SD_TICKS_MS tick_ms
#include "storage/sd/sd_mmc.c"
#include "storage/sd/sd_diskio.c"
#include "storage/sd/sd_hash.c"
#include "storage/sd/sd_stream.c"

#define NV 8u
#define FRAMES (3u * 44100u)                         /* 3 s each */
static FATFS fs __attribute__((aligned(4)));
static uint8_t ring_mem[SST_N * SST_K * 512u] __attribute__((aligned(4)));
static uint8_t src_mem[64u * 1024u] __attribute__((aligned(4)));

static int16_t pat(uint32_t f, uint32_t seed) { return (int16_t)(f * 7919u + seed * 40503u + (f >> 7) * 3u); }

static int write_wav(const char *path, uint32_t seed)
{
    static uint8_t buf[4096];
    uint8_t h[44];
    uint32_t dl = FRAMES * 2u, off = 0, k, n;
    FIL f;
    UINT put;
    memcpy(h, "RIFF", 4); h[4] = (uint8_t)(36u + dl); h[5] = (uint8_t)((36u + dl) >> 8); h[6] = (uint8_t)((36u + dl) >> 16);
    h[7] = 0; memcpy(h + 8, "WAVEfmt ", 8); h[16] = 16; h[17] = h[18] = h[19] = 0; h[20] = 1; h[21] = 0; h[22] = 1;
    h[23] = 0; h[24] = 0x44; h[25] = 0xAC; h[26] = h[27] = 0; h[28] = 0x88; h[29] = 0x58; h[30] = 1; h[31] = 0; h[32] = 2;
    h[33] = 0; h[34] = 16; h[35] = 0; memcpy(h + 36, "data", 4); h[40] = (uint8_t)dl; h[41] = (uint8_t)(dl >> 8);
    h[42] = (uint8_t)(dl >> 16); h[43] = 0;
    if (f_open(&f, path, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK || f_write(&f, h, 44, &put) != FR_OK)
        return 0;
    while (off < dl) {
        n = dl - off < sizeof buf ? dl - off : (uint32_t)sizeof buf;
        for (k = 0; k < n; k++)
            buf[k] = (uint8_t)((uint16_t)pat((off + k) >> 1, seed) >> (8u * ((off + k) & 1u)));
        if (f_write(&f, buf, n, &put) != FR_OK || put != n)
            return 0;
        off += n;
    }
    return f_close(&f) == FR_OK;
}

/* ---- the voices: an interrupt every 32 samples ---- */
typedef struct {
    uint8_t on;
    int st;
    uint32_t si, pos, frac, stepq;
    long bad, dried, ended;
} tv_t;
static tv_t V[NV];
static uint64_t next_block_x10;                      /* (the next interrupt, ticks x 441: 32 samples exactly) */

static void tv_note(uint32_t v, uint32_t si, double pitch)
{
    tv_t *t = &V[v];
    memset(t, 0, sizeof *t);
    t->si = si;
    t->stepq = (uint32_t)(pitch * 65536.0);
    t->st = sst_start(si, 0, 0, 0, sst_rate(&sst_src[si], t->stepq), &t->on);
    t->on = t->st >= 0;
}

static void tv_render(tv_t *t)
{
    uint32_t i;
    int32_t x;
    sst[t->st].beat++;
    for (i = 0; i < 32u; i++) {
        t->frac += t->stepq;
        while (t->frac >= 65536u) {
            t->frac -= 65536u;
            if (++t->pos >= FRAMES) {
                t->ended++;
                t->on = 0;
                sst_stop((uint32_t)t->st);
                return;
            }
            if (!sst_frame(&sst[t->st], &sst_src[t->si], t->pos, &x)) {
                t->dried++;
                t->on = 0;
                sst_ran_dry((uint32_t)t->st);
                return;
            }
            if (x != pat(t->pos, t->si + 1u))
                t->bad++;
        }
    }
}

static void audio(void)
{
    uint32_t v;
    if ((uint64_t)clk * 441u < next_block_x10)
        return;
    next_block_x10 += 32u * 10u * tick_ms;           /* (32 samples: 32 / 44.1 ms of ticks, x 441) */
    for (v = 0; v < NV; v++)
        if (V[v].on)
            tv_render(&V[v]);
}

static void wait_ticks(uint32_t n)
{
    uint32_t end = clk + n;
    while (clk < end) {
        clk++;
        audio();
    }
}

/* a page on fm-1base's loop */
typedef struct {
    const char *name;
    uint32_t strips, strip_b, every_ms;              /* a paint's strips and bytes each, its period (0: every frame) */
    uint32_t draw_blocks;                            /* the card's blocks read in the draw, a frame (each a command too) */
    uint8_t hook, fill;                              /* sst_strip between strips; the rings filled after a frame */
} page_t;
#define STRIP_CPU_US 1000u                           /* (assumed, not measured) a strip's canvas and text, XIP code */
#define STRIP_US(b) ((b) * 18u / 100u)               /* (an LCD byte: 0.18 us, 44 MHz effective) */
#define DRAW_BLOCK_US 1010u                          /* 610 + a command's 400 (the host's model) */
static const page_t *page;

static void fm1b_loop(const page_t *P, uint32_t ms)
{
    uint32_t end = clk + ms * tick_ms, next = clk, t0, t1, s, k;
    while (clk < end) {
        t0 = clk;
        wait_ticks(P->draw_blocks * DRAW_BLOCK_US * tick_ms / 1000u);
        if (P->strips && (int32_t)(clk - next) >= 0) {
            next = t0 + P->every_ms * tick_ms;
            for (s = 0; s < P->strips; s++) {
                if (P->hook)
                    (void)sst_strip();
                wait_ticks((STRIP_CPU_US + STRIP_US(P->strip_b)) * tick_ms / 1000u);
            }
        }
        t1 = clk;
        if (P->fill) {
            for (k = 0; k < 16u && sst_service() && clk - t1 < 12u * tick_ms; k++)
                ;
        } else {
            (void)sst_service();
        }
        while (clk - t0 < 16u * tick_ms && clk < end)
            if (!sst_service())
                wait_ticks(tick_ms / 10u);
    }
}

static void main_loop(uint32_t ms)
{
    uint32_t end = clk + ms * tick_ms, t0, s;
    if (page) {
        fm1b_loop(page, ms);
        return;
    }
    while (clk < end) {
        t0 = clk;
        for (s = 0; s < 4u; s++) {
            (void)sst_strip();
            wait_ticks(1500u * tick_ms / 1000u);
        }
        while (clk - t0 < 15u * tick_ms && clk < end)
            if (!sst_service())
                wait_ticks(tick_ms / 10u);
    }
}

static void quiet(void)
{
    uint32_t v;
    for (v = 0; v < NV; v++) {
        if (V[v].on)
            sst_stop((uint32_t)V[v].st);
        memset(&V[v], 0, sizeof V[v]);
    }
    main_loop(60);                                   /* (the streams given back, the card idle) */
    sst_stats_reset();
}

typedef struct {
    long bad, dried, ended;
    uint32_t kbs, lead;
} res_t;

static res_t play(uint32_t n, double pitch, uint32_t ms)
{
    res_t r = {0, 0, 0, 0, 0};
    uint32_t v;
    quiet();
    next_block_x10 = (uint64_t)clk * 441u;
    for (v = 0; v < n; v++)
        tv_note(v, v, pitch);
    main_loop(ms);
    for (v = 0; v < NV; v++)
        r.bad += V[v].bad, r.dried += V[v].dried, r.ended += V[v].ended;
    r.kbs = sst_kbs();
    r.lead = sst_st.lead_min_ms;
    return r;
}

static uint32_t page_fit(const page_t *P)            /* the most voices at 44.1 kHz mono with none dry, printing the table */
{
    uint32_t n, fit = 0;
    res_t r;
    page = P;
    printf("  %-24s", P->name);
    for (n = 1; n <= NV; n++) {
        r = play(n, 1.0, 2500);
        CHECK(r.bad == 0 && (long)sst_dry == r.dried, "%s, %u voices: garbage %ld, dry %ld counted %u", P->name, n, r.bad,
              r.dried, sst_dry);
        if (!r.dried && fit == n - 1u)
            fit = n;
        printf(" %ld", r.dried);
    }
    printf("   %u none dry\n", fit);
    page = 0;
    return fit;
}

int main(int argc, char **argv)
{
    static const uint32_t BUS[3] = {1900u, 937u, 733u};
    static const page_t QUIET = {"a quiet page", 0, 0, 0, 0, 1, 1};
    static const page_t INFO_OLD = {"INFO before", 8, 14220, 0, 0, 0, 0};
    static const page_t P2_OLD = {"SD CARD page 2 before", 0, 0, 0, 13, 0, 0};
    static const page_t INFO_LOOP = {"INFO, its paint as before", 8, 14220, 0, 0, 1, 1};
    static const page_t INFO_NEW = {"INFO now", 6, 7256, 250, 0, 1, 1};
    uint32_t fit_q, fit_i0, fit_il, fit_i, fit_p0;
    uint32_t sectors, i, n, b, used, at = 0, fit[3] = {0, 0, 0};
    char path[16];
    uint8_t *img;
    res_t r;
    if (argc != 2) {
        printf("usage: sd_stream_bus_test FAT32.img\n");
        return 2;
    }
    img = load(argv[1], &sectors);
    card(img, sectors, 0, 1, 1, 0);
    dma_on = 1;
    memset(&fs, 0, sizeof fs);
    sd_gone = 0;
    sd_dstat = STA_NOINIT;
    CHECK(f_mount(&fs, "", 1) == FR_OK, "mount");
    for (i = 0; i < NV; i++) {
        snprintf(path, sizeof path, "b%u.wav", i);
        CHECK(write_wav(path, i + 1u), "%s written", path);
        CHECK(sst_src_load(i, path, 50, src_mem + at, sizeof src_mem - at, &used) == FR_OK, "%s loaded", path);
        at += (used + 3u) & ~3u;
    }
    sst_rings(ring_mem, SST_N);
    clk_hook = audio;
    printf("  a voice at 44.1 kHz 16-bit mono: %u B/ms read (sst_rate) = 88.2 KB/s at its pitch, 176.4 an octave up; "
           "its ring %u KB (%u ms ahead), its head %u B (50 ms); 8 voices: %u KB of rings, %u KB of heads a source\n",
           sst_rate(&sst_src[0], 65536u), SST_K / 2u, SST_K * 512u * 10u / 882u, sst_src[0].hlen, SST_N * SST_K / 2u,
           sst_src[0].hlen / 1024u);
    printf("  voices at their pitch for 2.5 s: dry / KB/s read / least lead ms\n");
    for (b = 0; b < 3u; b++) {
        tick_ms = BUS[b];
        printf("  %4u KB/s:", tick_ms);
        for (n = 1; n <= NV; n++) {
            r = play(n, 1.0, 2500);
            CHECK(r.bad == 0, "%u KB/s, %u voices: %ld samples not the file's (garbage)", tick_ms, n, r.bad);
            CHECK((long)sst_dry == r.dried, "%u KB/s, %u voices: every underrun counted (sst_dry %u, %ld dry)", tick_ms, n,
                  sst_dry, r.dried);
            if (!r.dried && fit[b] == n - 1u)
                fit[b] = n;
            printf("  %u: %ld/%u/%u", n, r.dried, r.kbs, r.lead == SST_NONE ? 0u : r.lead);
        }
        printf("\n");
    }
    printf("  voices at 44.1 kHz with none dry: %u at 1.9 MB/s, %u at 937 KB/s, %u at 733 KB/s\n", fit[0], fit[1],
           fit[2]);
    CHECK(fit[0] == NV, "1.9 MB/s: 8 voices at 44.1 kHz, none dry (%u)", fit[0]);
    CHECK(fit[2] >= 2u, "733 KB/s: two voices at least (%u)", fit[2]);

    /* over the bus at 733 KB/s, then recovered */
    tick_ms = 733u;
    r = play(NV, 1.0, 2500);
    CHECK(r.dried > 0 && r.bad == 0 && (long)sst_dry == r.dried, "733 KB/s, 8 voices: underruns (%ld), counted (%u), "
          "never garbage (%ld)", r.dried, sst_dry, r.bad);
    main_loop(800);                                  /* (the rest play to their ends: 3 s files) */
    for (i = 0, n = 0; i < SST_N; i++)
        n += sst[i].on;
    for (i = 0, r.ended = 0, r.bad = 0; i < NV; i++)
        r.ended += V[i].ended, r.bad += V[i].bad;
    CHECK(n == 0 && r.ended + r.dried == (long)NV && r.bad == 0, "733 KB/s, 8 voices: the others played to their end "
          "(%ld + %ld dry), every stream given back (%u still on)", r.ended, r.dried, n);
    printf("  733 KB/s, 8 voices: %ld ran dry (faded out, counted), %ld played to their end\n", r.dried, r.ended);
    r = play(fit[2], 1.0, 3200);
    CHECK(r.dried == 0 && r.bad == 0 && r.ended == (long)fit[2], "733 KB/s, recovered: %u voices to their end (%ld), none "
          "dry (%ld)", fit[2], r.ended, r.dried);
    r = play(NV, 2.0, 1500);
    CHECK(r.dried > 0 && r.bad == 0 && (long)sst_dry == r.dried, "733 KB/s, 8 voices an octave up: underruns counted "
          "(%ld / %u), never garbage", r.dried, sst_dry);
    r = play(1, 1.0, 3200);
    CHECK(r.dried == 0 && r.ended == 1, "733 KB/s: then one voice to its end again (%ld dry)", r.dried);

    /* the pages at 840 KB/s */
    tick_ms = 840u;
    printf("  840 KB/s, the voices that ran dry of 1 .. 8 at their pitch for 2.5 s, fm-1base's loop:\n");
    fit_q = page_fit(&QUIET);
    fit_i0 = page_fit(&INFO_OLD);
    fit_p0 = page_fit(&P2_OLD);
    fit_il = page_fit(&INFO_LOOP);
    fit_i = page_fit(&INFO_NEW);
    printf("  voices none dry at 840 KB/s: a quiet page %u; INFO %u before, %u now (%u with only the reads between strips); "
           "SD CARD page 2 %u before, %u now (no card in its draw: a quiet page)\n", fit_q, fit_i0, fit_i, fit_il, fit_p0, fit_q);
    CHECK(fit_i >= fit_q, "840 KB/s: INFO costs no voice against a quiet page (%u, %u)", fit_i, fit_q);
    CHECK(fit_i > fit_i0, "840 KB/s: INFO carries more than before (%u, %u)", fit_i, fit_i0);
    clk_hook = 0;
    free(img);
    printf("sd_stream_bus_test: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
