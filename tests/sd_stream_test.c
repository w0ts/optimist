/* SPDX-License-Identifier: GPL-3.0-only */
/* Samples streamed from the SD card (firmware/src/storage/sd/sd_stream.c, sd_stream.h; docs/SD-CARD-DESIGN.md 7) on the
 * byte-level card model (tests/sd_card_model.h: a tick a byte, 1 us, so its bus is about 1 MB/s, slower than the FM-1's
 * 2 MB/s by DMA at 20 MHz: every margin here is a pessimist's) with full FatFs on the FAT16 and FAT32 images. The audio
 * is an interrupt: the model calls it at every tick, so it runs in the middle of a run, of a block's DMA, of a FatFs
 * call, as the FM-1's does; every 726 ticks (32 samples at 44.1 kHz) each voice reads its samples through sst_frame,
 * stepping by its pitch, and checks every one against the pattern its file was written with: GARBAGE (a sample not the
 * file's) is a failure everywhere. The main loop draws a screen of strips each frame with sst_strip between them and
 * calls sst_service in its idle wait, as main.c does.
 *   - voices at their rates (8 / 16 bits, mono / stereo, 22.05 / 44.1 / 48 kHz), files contiguous and fragmented: every
 *     sample right, none dry, the bus's share and its longest run;
 *   - a card busy 150 ms as a note starts: a 50 ms pre-buffer runs dry and fades (counted), 200 ms covers it;
 *   - a stall while playing: the ring covers 60 ms, not 250 (dry, faded, counted), the screen draws through both;
 *   - loops: back into the head, past it, inside one sector; a sample whole in its head reads no block;
 *   - notes started again and again from the interrupt in the middle of runs: never a stale block;
 *   - a block's CRC failing once: read again; the card pulled: every voice dry, the card gone, no wait;
 *   - a voice no longer rendered: its stream taken back; more voices than rings: the rest silent, counted;
 *   - too much for the bus (8 voices an octave up): dry voices, never garbage.
 *   sd_stream_test FAT16.img FAT32.img */
#define FELUCCA_SD_FATFS 1
#define FELUCCA_SD_RING_KB 8
#define AINL static inline
#define IN_POOL
#include "sd_card_model.h"
#include "storage/sd/sd_mmc.c"
#include "storage/sd/sd_diskio.c"
#include "storage/sd/sd_hash.c"
#include "storage/sd/sd_stream.c"

static FATFS fs __attribute__((aligned(4)));
static int dma_mode = 1;
static uint8_t ring_mem[SST_N * SST_K * 512u] __attribute__((aligned(4)));
static uint8_t src_mem[600u * 1024u] __attribute__((aligned(4)));
static uint32_t src_used;

/* ---- the files: a pattern every sample can be checked against ---- */
static int16_t pat16(uint32_t j, uint32_t seed) { return (int16_t)(j * 7919u + seed * 40503u + (j >> 7) * 3u); }
static uint8_t pat8(uint32_t j, uint32_t seed) { return (uint8_t)(j * 13u + seed * 101u + (j >> 9)); }

typedef struct {
    const char *path;
    uint32_t rate, bits, ch, frames, seed, pad;      /* pad: an extra chunk before the data (its offset in the file) */
} wav_t;

static void le16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void le32(uint8_t *p, uint32_t v) { le16(p, v); le16(p + 2, v >> 16); }

static uint32_t wav_header(const wav_t *w, uint8_t *h)   /* RIFF, fmt, a LIST of pad bytes, the data's header */
{
    uint32_t fb = w->bits / 8u * w->ch, dl = w->frames * fb, n = 0;
    memcpy(h, "RIFF", 4); le32(h + 4, 4u + 24u + (w->pad ? 8u + w->pad : 0u) + 8u + dl); memcpy(h + 8, "WAVE", 4);
    memcpy(h + 12, "fmt ", 4); le32(h + 16, 16); le16(h + 20, 1); le16(h + 22, w->ch); le32(h + 24, w->rate);
    le32(h + 28, w->rate * fb); le16(h + 32, fb); le16(h + 34, w->bits);
    n = 36;
    if (w->pad) {
        memcpy(h + n, "LIST", 4); le32(h + n + 4, w->pad); memset(h + n + 8, 0, w->pad);
        n += 8u + w->pad;
    }
    memcpy(h + n, "data", 4); le32(h + n + 4, dl);
    return n + 8u;
}

static uint8_t wav_byte(const wav_t *w, uint32_t b)  /* the data's byte b */
{
    if (w->bits == 8u)
        return pat8(b, w->seed);
    return (uint8_t)((uint16_t)pat16(b >> 1, w->seed) >> (8u * (b & 1u)));
}

static int32_t wav_mono(const wav_t *w, uint32_t f)  /* what sst_frame gives for frame f */
{
    int32_t a, b;
    if (w->bits == 8u) {
        a = ((int32_t)pat8(f * w->ch, w->seed) - 128) << 8;
        b = w->ch == 2u ? ((int32_t)pat8(f * 2u + 1u, w->seed) - 128) << 8 : a;
    } else {
        a = pat16(f * w->ch, w->seed);
        b = w->ch == 2u ? pat16(f * 2u + 1u, w->seed) : a;
    }
    return w->ch == 2u ? (a + b) >> 1 : a;
}

/* the files written together, chunk bytes of each in turn (0: one after the other): their clusters interleave */
static int write_wavs(const wav_t *w, uint32_t nw, uint32_t chunk)
{
    static FIL f[8];
    static uint8_t buf[4096];
    uint32_t off[8], tot[8], i, k, n, left = 0;
    UINT put;
    for (i = 0; i < nw; i++) {
        uint8_t h[600];
        uint32_t hn = wav_header(&w[i], h);
        if (f_open(&f[i], w[i].path, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK || f_write(&f[i], h, hn, &put) != FR_OK)
            return 0;
        off[i] = 0;
        tot[i] = w[i].frames * (w[i].bits / 8u) * w[i].ch;
        left += tot[i];
        if (!chunk) {
            while (off[i] < tot[i]) {
                n = tot[i] - off[i] < sizeof buf ? tot[i] - off[i] : (uint32_t)sizeof buf;
                for (k = 0; k < n; k++) buf[k] = wav_byte(&w[i], off[i] + k);
                if (f_write(&f[i], buf, n, &put) != FR_OK || put != n) return 0;
                off[i] += n;
                left -= n;
            }
            if (f_close(&f[i]) != FR_OK) return 0;
        }
    }
    while (chunk && left)
        for (i = 0; i < nw; i++) {
            n = tot[i] - off[i] < chunk ? tot[i] - off[i] : chunk;
            for (k = 0; k < n; k += (uint32_t)sizeof buf) {
                uint32_t m = n - k < sizeof buf ? n - k : (uint32_t)sizeof buf, j;
                for (j = 0; j < m; j++) buf[j] = wav_byte(&w[i], off[i] + k + j);
                if (f_write(&f[i], buf, m, &put) != FR_OK || put != m) return 0;
            }
            off[i] += n;
            left -= n;
        }
    for (i = 0; chunk && i < nw; i++)
        if (f_close(&f[i]) != FR_OK) return 0;
    return 1;
}

/* ---- the audio: voices, an interrupt every 32 samples ---- */
#define BLOCK_US 726u                                /* 32 samples at 44.1 kHz */
typedef struct {
    uint8_t on;
    int st, frozen;
    const wav_t *w;
    uint32_t si, pos, frac, stepq, looped, ls, le;
    long reads, bad, dried, ended;
} tv_t;
static tv_t V[12];
static uint32_t next_block, blocks_run;
static int retrigger_every;                          /* > 0: voice 0 starts again every that many blocks, in the audio */
static uint32_t retrig_a = 6, retrig_b = 7;         /* (with the two sources in turn) */
static const wav_t *W[SST_NSRC];
static uint32_t step_of(const wav_t *w, double pitch);

static void tv_note(uint32_t v, uint32_t si, uint32_t stepq, uint32_t looped, uint32_t ls, uint32_t le)
{
    tv_t *t = &V[v];
    int32_t x;
    if (t->on && t->st >= 0)
        sst_stop((uint32_t)t->st);
    memset(t, 0, sizeof *t);
    t->w = W[si];
    t->si = si;
    t->stepq = stepq;
    t->looped = looped;
    t->ls = ls;
    t->le = le;
    t->st = sst_start(si, looped, ls, le, sst_rate(&sst_src[si], stepq), &t->on);
    t->on = t->st >= 0;
    if (t->on) {                                     /* (the first sample at once: from the head) */
        if (!sst_frame(&sst[t->st], &sst_src[si], 0, &x))
            t->dried++, t->on = 0, sst_ran_dry((uint32_t)t->st);
        else if (x != wav_mono(t->w, 0))
            t->bad++;
    }
}

static void tv_render(tv_t *t)
{
    uint32_t i;
    int32_t x;
    const sst_src_t *s = &sst_src[t->si];
    if (!t->frozen)
        sst[t->st].beat++;
    else
        return;
    for (i = 0; i < 32u; i++) {
        t->frac += t->stepq;
        while (t->frac >= 65536u) {
            t->frac -= 65536u;
            t->pos++;
            if (t->looped && t->pos > t->le)
                t->pos = t->ls;
            if (t->pos >= s->frames) {
                t->ended++;
                t->on = 0;
                sst_stop((uint32_t)t->st);
                return;
            }
            if (!sst_frame(&sst[t->st], s, t->pos, &x)) {
                t->dried++;
                t->on = 0;
                sst_ran_dry((uint32_t)t->st);
                return;
            }
            t->reads++;
            if (x != wav_mono(t->w, t->pos)) {
                if (!t->bad)
                    printf("  garbage: voice %d frame %u: %d, the file's %d\n", (int)(t - V), t->pos, x,
                           wav_mono(t->w, t->pos));
                t->bad++;
            }
        }
    }
}

static void audio(void)
{
    uint32_t v;
    if (clk < next_block)
        return;
    next_block += BLOCK_US;
    blocks_run++;
    if (retrigger_every > 0 && blocks_run % (uint32_t)retrigger_every == 0 && V[0].w) {
        uint32_t si = V[0].si == retrig_a ? retrig_b : retrig_a;   /* (the other file: a stale block is the wrong one's) */
        tv_note(0, si, step_of(W[si], 1.5), 0, 0, 0);
    }
    for (v = 0; v < sizeof V / sizeof V[0]; v++)
        if (V[v].on)
            tv_render(&V[v]);
}

static void wait_us(uint32_t us)
{
    uint32_t end = clk + us;
    while (clk < end) {
        clk++;
        audio();
    }
}

/* ---- the main loop: a frame of strips (sst_strip between), then the idle wait (sst_service) ---- */
static uint32_t strips = 4, strip_us = 1500, gap_max;   /* (a 240 x 240 redraw at 30 MHz: about 4 x 7.7 ms) */
static void main_loop(uint32_t ms)
{
    uint32_t end = clk + ms * 1000u, t0, s, g;
    while (clk < end) {
        t0 = clk;
        for (s = 0; s < strips; s++) {
            g = clk;
            (void)sst_strip();
            g = clk - g;
            if (g > gap_max)
                gap_max = g;                         /* (the screen's wait for the card between two strips) */
            wait_us(strip_us);
        }
        while (clk - t0 < 15000u && clk < end)
            if (!sst_service())
                wait_us(100);
    }
}

static int all_ok(const char *what, long want_dry)
{
    long bad = 0, dried = 0;
    uint32_t v;
    for (v = 0; v < sizeof V / sizeof V[0]; v++)
        bad += V[v].bad, dried += V[v].dried;
    CHECK(bad == 0, "%s: %ld samples not the file's (garbage)", what, bad);
    if (want_dry >= 0)
        CHECK(dried == want_dry, "%s: %ld voices dry (want %ld)", what, dried, want_dry);
    if (!retrigger_every)                            /* (a voice started again forgets its own count) */
        CHECK((long)sst_dry == dried, "%s: sst_dry %u, the voices %ld", what, sst_dry, dried);
    return bad == 0;
}

static void quiet(void)                              /* every voice off, the streams given back */
{
    uint32_t v;
    for (v = 0; v < sizeof V / sizeof V[0]; v++) {
        if (V[v].on)
            sst_stop((uint32_t)V[v].st);
        memset(&V[v], 0, sizeof V[v]);
    }
    retrigger_every = 0;
    C.stall_until = 0;
    main_loop(5);
    sst_stats_reset();
    gap_max = 0;
}

static void load_src(uint32_t si, const wav_t *w, uint32_t pre_ms, const char *what)
{
    uint32_t need = 0, used = 0;
    FRESULT r = sst_src_need(w->path, pre_ms, &need);
    CHECK(r == FR_OK && need, "%s: %s's need (%d)", what, w->path, (int)r);
    src_used = (src_used + 3u) & ~3u;
    r = sst_src_load(si, w->path, pre_ms, src_mem + src_used, sizeof src_mem - src_used, &used);
    CHECK(r == FR_OK && used == need, "%s: %s loaded (%d, %u of %u)", what, w->path, (int)r, used, need);
    src_used += used;
    W[si] = w;
}

/* a mono 16-bit WAV with a "smpl" chunk (unity note, one loop ls .. le) before or after its data; nl 0: no loop */
static int put_smpl_wav(const char *path, uint32_t frames, uint32_t unity, uint32_t nl, uint32_t ls, uint32_t le, int after)
{
    uint8_t h[44], sm[8 + 60], d[2];
    uint32_t i, smn = nl ? 60u : 36u;
    FIL f;
    UINT k;
    memset(sm, 0, sizeof sm);
    memcpy(sm, "smpl", 4); le32(sm + 4, smn); le32(sm + 8 + 12, unity); le32(sm + 8 + 28, nl);
    le32(sm + 8 + 36 + 8, ls); le32(sm + 8 + 36 + 12, le);
    memcpy(h, "RIFF", 4); le32(h + 4, 36u + 8u + smn + frames * 2u); memcpy(h + 8, "WAVEfmt ", 8); le32(h + 16, 16);
    le16(h + 20, 1); le16(h + 22, 1); le32(h + 24, 44100); le32(h + 28, 88200); le16(h + 32, 2); le16(h + 34, 16);
    memcpy(h + 36, "data", 4); le32(h + 40, frames * 2u);
    if (f_open(&f, path, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK || f_write(&f, h, 36, &k) != FR_OK)
        return 0;
    if (!after && f_write(&f, sm, 8u + smn, &k) != FR_OK)
        return 0;
    if (f_write(&f, h + 36, 8, &k) != FR_OK)
        return 0;
    for (i = 0; i < frames; i++) {
        le16(d, (uint16_t)pat16(i, 9));
        if (f_write(&f, d, 2, &k) != FR_OK)
            return 0;
    }
    if (after && f_write(&f, sm, 8u + smn, &k) != FR_OK)
        return 0;
    return f_close(&f) == FR_OK;
}

/* "smpl": the unity note and the first loop, before or after the data; out of the data or none: C4, no loop */
static void smpl_tests(const char *what)
{
    static const struct { const char *path; uint32_t unity, nl, ls, le; int after; int8_t root; uint32_t lps, lpe; } T[] = {
        {"m0.wav", 48, 1, 1000, 3999, 1, -12, 1000, 3999},   /* after the data: C3, a loop */
        {"m1.wav", 72, 1, 200, 4400, 0, 12, 200, 4400},      /* before the data: C5 */
        {"m2.wav", 60, 0, 0, 0, 1, 0, 0, 0},                 /* no loop */
        {"m3.wav", 64, 1, 100, 9000, 0, 4, 0, 0},            /* its end past the data: no loop */
        {"m4.wav", 200, 1, 300, 100, 1, 0, 0, 0},            /* a note past 127, an end before the start */
    };
    uint32_t i, used, f;
    for (i = 0; i < sizeof T / sizeof T[0]; i++) {
        sst_src_t *s = &sst_src[8u + i];
        int32_t x;
        CHECK(put_smpl_wav(T[i].path, 5000, T[i].unity, T[i].nl, T[i].ls, T[i].le, T[i].after), "%s: %s written", what,
              T[i].path);
        src_used = (src_used + 3u) & ~3u;
        CHECK(sst_src_load(8u + i, T[i].path, 50, src_mem + src_used, sizeof src_mem - src_used, &used) == FR_OK,
              "%s: %s loaded", what, T[i].path);
        src_used += used;
        CHECK(s->frames == 5000u && s->root == T[i].root && s->lps == T[i].lps && s->lpe == T[i].lpe,
              "%s: %s: %u frames, root %d, loop %u..%u", what, T[i].path, s->frames, s->root, s->lps, s->lpe);
        for (f = 0, x = 0; f < 5000u; f++) {             /* (the head: the data's first frames, not the smpl's bytes) */
            const uint8_t *q = f * 2u < s->hlen ? s->head + f * 2u : 0;
            if (!q)
                break;
            x = (int16_t)(q[0] | q[1] << 8);
            if (x != pat16(f, 9))
                break;
        }
        CHECK(f * 2u >= s->hlen, "%s: %s: its head is the data (frame %u)", what, T[i].path, f);
    }
}

static uint32_t step_of(const wav_t *w, double pitch) { return (uint32_t)((double)w->rate / 44100.0 * pitch * 65536.0); }

static const wav_t WAVS[] = {
    {"s0.wav", 44100, 16, 1, 88200, 1, 0},           /* 2 s mono 16 */
    {"s1.wav", 48000, 16, 2, 48000, 2, 30},          /* 1 s stereo 16, the data at 44 + 38 */
    {"s2.wav", 22050, 8, 1, 44100, 3, 0},            /* 2 s mono 8 */
    {"s3.wav", 44100, 16, 1, 88200, 4, 2},           /* 2 s mono 16, the data at 46 */
    {"s4.wav", 44100, 16, 1, 1000, 5, 0},            /* 23 ms: whole in a head */
    {"s5.wav", 44100, 8, 2, 66150, 6, 0},            /* 1.5 s stereo 8 */
};
static const wav_t FRAG[] = {                        /* written together, 16 KB of each in turn: in runs */
    {"f0.wav", 44100, 16, 1, 88200, 7, 0},
    {"f1.wav", 44100, 16, 1, 88200, 8, 4},
};

static void run_card(const char *img_path)
{
    uint32_t sectors, i;
    uint8_t *img = load(img_path, &sectors);
    const char *what = img_path;
    FRESULT r;
    card(img, sectors, 0, 1, 1, 0);
    dma_on = dma_mode;
    memset(&fs, 0, sizeof fs);
    sd_gone = 0;
    sd_dstat = STA_NOINIT;
    r = f_mount(&fs, "", 1);
    CHECK(r == FR_OK, "%s: mount (%d)", what, (int)r);
    CHECK(write_wavs(WAVS, 6, 0) && write_wavs(FRAG, 2, 16384), "%s: the samples written", what);
    src_used = 0;
    for (i = 0; i < 6; i++)
        load_src(i, &WAVS[i], 50, what);
    load_src(6, &FRAG[0], 50, what);
    load_src(7, &FRAG[1], 50, what);
    CHECK(sst_src[6].nrun > 3 && sst_src[7].nrun > 3, "%s: the fragmented files in runs (%u, %u)", what,
          sst_src[6].nrun, sst_src[7].nrun);
    CHECK(sst_src[0].hlen == 4412u && sst_src[1].hlen >= 9592u && sst_src[1].hlen <= 9600u && sst_src[2].hlen == 1104u,
          "%s: 50 ms heads (%u %u %u)", what, sst_src[0].hlen, sst_src[1].hlen, sst_src[2].hlen);
    CHECK(sst_src[4].hlen == 2000u, "%s: a short sample whole in its head (%u)", what, sst_src[4].hlen);
    smpl_tests(what);
    sst_rings(ring_mem, SST_N);
    clk_hook = audio;
    next_block = clk + BLOCK_US;
    quiet();

    /* voices at their rates, contiguous and fragmented files: right, none dry */
    tv_note(0, 0, step_of(&WAVS[0], 1.0), 0, 0, 0);
    tv_note(1, 1, step_of(&WAVS[1], 1.0), 0, 0, 0);
    tv_note(2, 2, step_of(&WAVS[2], 1.0), 0, 0, 0);
    tv_note(3, 6, step_of(&FRAG[0], 1.0), 0, 0, 0);
    tv_note(4, 7, step_of(&FRAG[1], 1.0), 0, 0, 0);
    tv_note(5, 5, step_of(&WAVS[5], 1.0), 0, 0, 0);
    main_loop(2100);
    all_ok("six voices", 0);
    for (i = 0; i < 6; i++)
        CHECK(V[i].ended == 1 && !V[i].on, "%s: voice %u played to its end (%ld)", what, i, V[i].ended);
    CHECK(V[3].reads == 88199 && V[0].reads == 88199, "%s: every frame read (%ld %ld)", what, V[0].reads, V[3].reads);
    printf("  %s: six voices: %u KB/s, %u runs, longest %u us, the screen's longest wait %u us, least lead %u ms\n",
           what, sst_kbs(), sst_st.runs, sst_st.run_max_us, gap_max, sst_st.lead_min_ms);
    CHECK(sst_st.run_max_us < SST_RUN * 560u + 200u, "%s: a run at most %u blocks (%u us)", what, SST_RUN,
          sst_st.run_max_us);
    CHECK(sst_st.errors == 0 && sst_st.stalls == 0, "%s: no errors (%u), no stalls (%u)", what, sst_st.errors,
          sst_st.stalls);
    quiet();

    /* a card busy 150 ms as a note starts: 50 ms of head runs dry (faded), 200 ms covers it */
    C.stall_until = clk + 150000u;
    tv_note(0, 0, step_of(&WAVS[0], 1.0), 0, 0, 0);
    main_loop(400);
    all_ok("stall at the start, 50 ms", 1);
    CHECK(V[0].reads > 2000 && V[0].reads < 2300, "%s: dry at the head's end (%ld frames)", what, V[0].reads);
    CHECK(sst_st.stalls >= 5, "%s: the stall let go and asked again (%u)", what, sst_st.stalls);
    CHECK(gap_max < SST_WAIT_US + 600u, "%s: the screen drew through the stall (longest wait %u us)", what, gap_max);
    quiet();
    load_src(0, &WAVS[0], 200, what);                /* (after the others: theirs stay where they are) */
    C.stall_until = clk + 150000u;
    tv_note(0, 0, step_of(&WAVS[0], 1.0), 0, 0, 0);
    main_loop(2100);
    all_ok("stall at the start, 200 ms", 0);
    CHECK(V[0].ended == 1, "%s: 200 ms: played to its end", what);
    quiet();

    /* a stall while playing: the ring (8 KB, 93 ms of 44.1 kHz 16-bit mono) covers 60 ms, not 250 */
    tv_note(0, 3, step_of(&WAVS[3], 1.0), 0, 0, 0);
    main_loop(300);
    C.stall_until = clk + 60000u;
    main_loop(1800);
    all_ok("a 60 ms stall", 0);
    CHECK(V[0].ended == 1, "%s: 60 ms: played to its end", what);
    quiet();
    tv_note(0, 3, step_of(&WAVS[3], 1.0), 0, 0, 0);
    main_loop(300);
    C.stall_until = clk + 250000u;
    main_loop(600);
    all_ok("a 250 ms stall", 1);
    CHECK(gap_max < SST_WAIT_US + 600u, "%s: the screen drew through it (%u us)", what, gap_max);
    quiet();

    /* loops: into the head (from 0), past it, inside one sector; 3 s each */
    tv_note(0, 3, step_of(&WAVS[3], 1.0), 1, 0, 20000);
    tv_note(1, 3, step_of(&WAVS[3], 1.3), 1, 30000, 50000);
    tv_note(2, 6, step_of(&FRAG[0], 0.7), 1, 40000, 40100);
    tv_note(3, 1, step_of(&WAVS[1], 1.0), 1, 1000, 47999);
    main_loop(3000);
    all_ok("loops", 0);
    for (i = 0; i < 4; i++)
        CHECK(V[i].on && V[i].reads > 60000, "%s: loop %u still playing (%ld frames)", what, i, V[i].reads);
    quiet();
    tv_note(0, 4, step_of(&WAVS[4], 1.0), 0, 0, 0);  /* whole in its head */
    main_loop(100);
    all_ok("a sample in its head", 0);
    CHECK(sst_st.blocks == 0 && V[0].ended == 1, "%s: no block read for it (%u)", what, sst_st.blocks);
    quiet();

    /* notes started again from the interrupt every 7 blocks (5 ms), in the middle of runs, the two fragmented files in turn */
    tv_note(0, 6, step_of(&FRAG[0], 1.5), 0, 0, 0);
    tv_note(1, 0, step_of(&WAVS[0], 1.0), 0, 0, 0);
    retrigger_every = 7;
    main_loop(1500);
    all_ok("started again and again", -1);
    CHECK(V[1].dried == 0, "%s: the other voice never dry", what);
    CHECK(sst_st.aborted > 0, "%s: some runs given up for a new note (%u)", what, sst_st.aborted);
    quiet();
    load_src(8, &FRAG[0], 1, what);                  /* the same with 1 ms heads: the ring's first slot wanted at once */
    load_src(9, &FRAG[1], 1, what);
    retrig_a = 8;
    retrig_b = 9;
    tv_note(0, 8, step_of(&FRAG[0], 1.5), 0, 0, 0);
    retrigger_every = 3;
    main_loop(1500);
    all_ok("started again with 1 ms heads", -1);
    retrig_a = 6;
    retrig_b = 7;
    quiet();

    /* a block's CRC failing once: read again */
    tv_note(0, 0, step_of(&WAVS[0], 1.0), 0, 0, 0);
    main_loop(100);
    C.corrupt_once = sst_src[0].run[0].lba + 60u;
    main_loop(2000);
    all_ok("a CRC failing once", 0);
    CHECK(sst_st.crc == 1 && V[0].ended == 1, "%s: read again once (%u)", what, sst_st.crc);
    quiet();

    /* a voice no longer rendered: its stream taken back; more voices than rings: silent, counted */
    tv_note(0, 0, step_of(&WAVS[0], 1.0), 0, 0, 0);
    V[0].frozen = 1;
    main_loop(100);
    CHECK(sst_st.reaped == 1 && !sst[V[0].st].on, "%s: a frozen voice's stream taken back (%u)", what, sst_st.reaped);
    quiet();
    for (i = 0; i < SST_N; i++)                      /* voices that end without a word (a release's end): rings free */
        tv_note(i, 2, step_of(&WAVS[2], 1.0), 0, 0, 0);
    main_loop(20);
    for (i = 0; i < SST_N; i++)
        V[i].on = 0;
    for (i = 0; i < SST_N; i++)
        tv_note(i, 0, step_of(&WAVS[0], 1.0), 0, 0, 0);
    CHECK(sst_nostream == 0, "%s: the ended voices' rings taken at once (%u without)", what, sst_nostream);
    main_loop(100);
    all_ok("rings of ended voices", 0);
    quiet();
    for (i = 0; i < SST_N + 2u; i++)
        tv_note(i, 2, step_of(&WAVS[2], 1.0), 0, 0, 0);
    CHECK(sst_nostream == 2 && !V[SST_N].on, "%s: two voices with no ring (%u)", what, sst_nostream);
    main_loop(100);
    all_ok("all rings", 0);
    quiet();

    /* too much for the bus: 8 voices an octave up (8 x 176 KB/s): dry ones fade, never garbage */
    for (i = 0; i < SST_N; i++)
        tv_note(i, i & 1u ? 3u : 0u, step_of(&WAVS[0], 2.0), 0, 0, 0);
    main_loop(1200);
    all_ok("over the bus", -1);
    CHECK(sst_dry > 0, "%s: some ran dry (%u)", what, sst_dry);
    printf("  %s: over the bus: %u of 8 dry, %u KB/s\n", what, sst_dry, sst_kbs());
    quiet();

    /* the card pulled: every voice dry, the card gone, no wait */
    tv_note(0, 0, step_of(&WAVS[0], 1.0), 0, 0, 0);
    tv_note(1, 6, step_of(&FRAG[0], 1.0), 0, 0, 0);
    main_loop(200);
    C.present = 0;
    main_loop(400);
    all_ok("the card pulled", 2);
    CHECK(sd_gone == 1, "%s: the card taken as gone", what);
    CHECK(gap_max < SST_WAIT_US + 600u, "%s: no long wait on it (%u us)", what, gap_max);
    C.present = 1;
    clk_hook = 0;
    free(img);
}

int main(int argc, char **argv)
{
    int i;
    if (argc < 3) {
        printf("usage: sd_stream_test FAT16.img FAT32.img\n");
        return 2;
    }
    for (i = 1; i < argc; i++) {
        dma_mode = i > 1;                            /* (the first card's blocks by bytes, the second's by the DMA) */
        run_card(argv[i]);
    }
    printf("sd_stream_test: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
