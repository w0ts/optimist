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
 *   - the card before the screen (the user, 2026-11-05: "sd card should have prio over screen"): 8 voices at 1,147 KB/s, the REC
 *     ring, page changes and the whole screen every frame, before (one run a strip, none in a clear: voices dry) and now (none
 *     dry), the paints a second; over the bus the screen goes on; a card busy or gone costs the screen no wait.
 *   sd_stream_bus_test FAT32.img (FELUCCA_SD_RING_KB: the rings' KB, run_tests.sh 4 and 8) */
#define FELUCCA_SD_FATFS 1
#ifndef FELUCCA_SD_RING_KB
#define FELUCCA_SD_RING_KB 4
#endif
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
    uint8_t hook, fill;                              /* the card between strips (PG_*); the rings filled after a frame */
    uint32_t clear_b;                                /* a paint's whole-screen clear first, bytes (a page change: 115,200) */
    uint32_t cpu_us;                                 /* a strip's drawing, 0 STRIP_CPU_US (the audio's share of the CPU in it) */
} page_t;
enum { PG_NONE, PG_ONE_RUN, PG_FIRST };              /* no run; one run a strip and none in a clear (as until 2026-11-05);
                                                      * the card first: sst_strip, a clear in strips, a frame held back */
#define STRIP_CPU_US 1000u                           /* (assumed, not measured) a strip's canvas and text, XIP code */
#define LCD_CLEAR_B 7680u                            /* a clear between the card's turns: 16 rows (1.4 ms) */
#define STRIP_US(b) ((b) * 18u / 100u)               /* (an LCD byte: 0.18 us, 44 MHz effective) */
#define DRAW_BLOCK_US 1010u                          /* 610 + a command's 400 (the host's model) */
static const page_t *page;
static uint32_t paints, paint_max;                   /* the paints done, the longest (ticks) */

static void pg_card(const page_t *P)                 /* the card's turn before a strip */
{
    if (P->hook == PG_FIRST)
        (void)sst_strip();
    else if (P->hook == PG_ONE_RUN)
        (void)sst_go(SST_URGENT_MS);
}

static void fm1b_loop(const page_t *P, uint32_t ms)
{
    uint32_t end = clk + ms * tick_ms, next = clk, t0, t1, s, k;
    while (clk < end) {
        t0 = clk;
        wait_ticks(P->draw_blocks * DRAW_BLOCK_US * tick_ms / 1000u);
        if (P->strips && (int32_t)(clk - next) >= 0 && !(P->hook == PG_FIRST && sst_frame_late())) {
            next = t0 + P->every_ms * tick_ms;
            if (P->clear_b && P->hook == PG_FIRST) {
                for (k = 0; k < P->clear_b; k += LCD_CLEAR_B) {
                    pg_card(P);
                    wait_ticks(STRIP_US(LCD_CLEAR_B) * tick_ms / 1000u);
                }
            } else if (P->clear_b) {
                wait_ticks(STRIP_US(P->clear_b) * tick_ms / 1000u);
            }
            for (s = 0; s < P->strips; s++) {
                pg_card(P);
                wait_ticks(((P->cpu_us ? P->cpu_us : STRIP_CPU_US) + STRIP_US(P->strip_b)) * tick_ms / 1000u);
            }
            paints++;
            if (clk - t0 > paint_max)
                paint_max = clk - t0;
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

/* the card first (the user, 2026-11-05: "sd card should have prio over screen"): the figures from tests/ui_optimist_buscost.h
 * (BUS_COST=1, 16 canvas rows: a page change a clear of 115,200 B and 16 strips of 7,170 B, whichever the page; the REC ring
 * 8 strips of 7,380 B every frame until the fix), the bus the card's 12 MHz (1,147 KB/s measured on two FM-1s). A strip's
 * drawing at 1 ms and at 2.5 ms (the audio holding 60 % of the CPU): assumed, not measured. */
static void screen_row(const page_t *P, long *dried)
{
    res_t r;
    paints = paint_max = 0;
    page = P;
    r = play(NV, 1.0, 2500);
    page = 0;
    CHECK(r.bad == 0 && (long)sst_dry == r.dried, "%s: garbage %ld, dry %ld counted %u", P->name, r.bad, r.dried, sst_dry);
    printf("  %-38s dry %ld of 8, %2u.%u paints a second, the longest %3u ms, least lead %u ms\n", P->name, r.dried, paints / 2u,
           paints * 5u % 10u, paint_max / tick_ms, r.lead == SST_NONE ? 0u : r.lead);
    *dried = r.dried;
}

/* rings shortened by the plan (sst_place_rings, a build's rings of more than 4 KB): 5 hits at once on a kit's 25 ms heads */
static void short_ring_tests(void)
{
    static const page_t CHG = {"a page change a 300 ms", 16, 7170, 300, 0, PG_FIRST, 1, 115200, 0};
    static const page_t CHG_B = {"the same, busy audio", 16, 7170, 300, 0, PG_FIRST, 1, 115200, 2500};
    static const page_t FULL_B = {"the whole screen every frame, busy audio", 16, 7170, 0, 0, PG_FIRST, 1, 0, 2500};
    const page_t *P[3] = {&CHG, &CHG_B, &FULL_B};
    static const uint32_t KB[3] = {4u, 6u, FELUCCA_SD_RING_KB};
    uint32_t i, k, at = 0, used;
    char path[16];
    res_t r;
    tick_ms = 1147u;
    for (i = 0; i < NV; i++) {
        snprintf(path, sizeof path, "b%u.wav", i);
        CHECK(sst_src_load(i, path, 25, src_mem + at, sizeof src_mem - at, &used) == FR_OK, "%s: a 25 ms head", path);
        at += (used + 3u) & ~3u;
    }
    for (k = 0; k < 3u; k++) {
        if (KB[k] > FELUCCA_SD_RING_KB)
            continue;                                /* (a ring is at most the build's) */
        quiet();
        sst_k = KB[k] * 2u;
        sst_rings(ring_mem, SST_N);
        for (i = 0; i < 3u; i++) {
            paints = paint_max = 0;
            page = P[i];
            r = play(5u, 1.0, 2500);
            page = 0;
            printf("  rings of %u KB (urgent under %u ms), 5 hits on 25 ms heads, %-40s dry %ld, %u paints, least lead %u ms\n", KB[k],
                   SST_URGENT_MS, P[i]->name, r.dried, paints, r.lead == SST_NONE ? 0u : r.lead);
            CHECK(r.dried == 0, "rings of %u KB, %s: none dry (%ld)", KB[k], P[i]->name, r.dried);
        }
    }
    quiet();
    sst_k = SST_K;
    sst_rings(ring_mem, SST_N);
}

static void screen_first_tests(void)
{
    static const page_t RING_OLD = {"the REC ring as it was", 8, 7380, 0, 0, PG_ONE_RUN, 1, 0, 0};
    static const page_t RING_OLD_B = {"the REC ring as it was, busy audio", 8, 7380, 0, 0, PG_ONE_RUN, 1, 0, 2500};
    static const page_t CHG_OLD = {"a page change a 300 ms, as it was", 16, 7170, 300, 0, PG_ONE_RUN, 1, 115200, 0};
    static const page_t CHG_OLD_B = {"a page change, as it was, busy audio", 16, 7170, 300, 0, PG_ONE_RUN, 1, 115200, 2500};
    static const page_t RING = {"the REC ring's whole panel, card first", 8, 7380, 0, 0, PG_FIRST, 1, 0, 0};
    static const page_t RING_B = {"the same, busy audio", 8, 7380, 0, 0, PG_FIRST, 1, 0, 2500};
    static const page_t CHG = {"a page change a 300 ms, card first", 16, 7170, 300, 0, PG_FIRST, 1, 115200, 0};
    static const page_t CHG_B = {"the same, busy audio", 16, 7170, 300, 0, PG_FIRST, 1, 115200, 2500};
    static const page_t FULL = {"the whole screen every frame", 16, 7170, 0, 0, PG_FIRST, 1, 0, 0};
    static const page_t FULL_B = {"the same, busy audio", 16, 7170, 0, 0, PG_FIRST, 1, 0, 2500};
    long d_ring_old, d_chg_old, d;
    uint32_t i;
    tick_ms = 1147u;
    printf("  1,147 KB/s, 8 voices at their pitch, rings of %u KB, 2.5 s:\n", SST_K / 2u);
    screen_row(&RING_OLD, &d_ring_old);
    screen_row(&RING_OLD_B, &d);
    d_ring_old += d;
    screen_row(&CHG_OLD, &d_chg_old);
    screen_row(&CHG_OLD_B, &d);
    d_chg_old += d;
    printf("  as it was: %ld dry on the ring, %ld on page changes\n", d_ring_old, d_chg_old);
    screen_row(&RING, &d);
    CHECK(d == 0, "the REC ring, card first: none dry (%ld)", d);
    screen_row(&RING_B, &d);
    CHECK(d == 0, "the REC ring, card first, busy audio: none dry (%ld)", d);
    screen_row(&CHG, &d);
    CHECK(d == 0, "page changes, card first: none dry (%ld)", d);
    CHECK(paints >= 6u, "page changes, card first: the pages still come (%u paints in 2.5 s)", paints);
    screen_row(&CHG_B, &d);
    CHECK(d == 0, "page changes, card first, busy audio: none dry (%ld)", d);
    screen_row(&FULL, &d);
    CHECK(d == 0, "the whole screen every frame, card first: none dry (%ld)", d);
    CHECK(paints >= 10u, "the screen goes on (%u paints in 2.5 s)", paints);
    screen_row(&FULL_B, &d);
    CHECK(d == 0, "the whole screen every frame, card first, busy audio: none dry (%ld)", d);
    CHECK(paints >= 5u, "the screen goes on, busy audio (%u paints in 2.5 s)", paints);

    /* the limits: a bus too slow for the voices, a card that stops */
    tick_ms = 300u;
    screen_row(&FULL, &d);
    CHECK(d > 0 && paints >= 10u, "300 KB/s, over the bus: voices dry, counted, and the screen at its floor (%ld dry, %u paints)", d,
          paints);
    tick_ms = 1147u;
    quiet();
    next_block_x10 = (uint64_t)clk * 441u;
    for (i = 0; i < NV; i++)
        tv_note(i, i, 1.0);
    (void)sst_service();
    wait_ticks(20u * tick_ms);                       /* (a 50 ms head: 30 ms left, under any ring's urgency) */
    {
        uint32_t t0;
        sst_st.drawn = clk;
        CHECK(sst_frame_late() == 1u, "a stream short of data: the frame held back");
        sst_st.drawn = clk - SST_HOLD_MS * tick_ms - tick_ms;
        CHECK(sst_frame_late() == 0u, "a frame held back %u ms at most", SST_HOLD_MS);
        sst_st.backoff = 1;                          /* (the card busy: the streams wait, the screen does not) */
        sst_st.backoff_t0 = clk;
        t0 = clk;
        CHECK(sst_strip() == 0u && clk == t0, "the card busy: no wait before a strip");
        wait_ticks(0u);
        sst_st.drawn = clk;
        CHECK(!sst_frame_late(), "the card busy: the frame drawn");
        sst_st.backoff = 0;
        sd_gone = 1;                                 /* (a card pulled: every later call fails at once) */
        t0 = clk;
        CHECK(sst_strip() == 0u && clk == t0, "a card gone: no wait before a strip");
        sst_st.drawn = clk;
        CHECK(!sst_frame_late(), "a card gone: the frame drawn");
        sd_gone = 0;
    }
    quiet();
}

int main(int argc, char **argv)
{
    static const uint32_t BUS[3] = {1900u, 937u, 733u};
    static const page_t QUIET = {"a quiet page", 0, 0, 0, 0, PG_FIRST, 1};
    static const page_t INFO_OLD = {"INFO before", 8, 14220, 0, 0, PG_NONE, 0};
    static const page_t P2_OLD = {"SD CARD page 2 before", 0, 0, 0, 13, PG_NONE, 0};
    static const page_t INFO_LOOP = {"INFO, its paint as before", 8, 14220, 0, 0, PG_ONE_RUN, 1};
    static const page_t INFO_NEW = {"INFO now", 6, 7256, 250, 0, PG_FIRST, 1};
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
    screen_first_tests();
    if (SST_K >= 8u)
        short_ring_tests();
    clk_hook = 0;
    free(img);
    printf("sd_stream_bus_test: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
