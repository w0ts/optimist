/* SPDX-License-Identifier: GPL-3.0-only */
/* The streams' memory per project (firmware/src/storage/sd/sd_stream_plan.c) on the card model (tests/sd_card_model.h)
 * with full FatFs, the card's layout and its sample index, and the arena (core/arena.c: three parts and the streams'
 * client, placed by need):
 *   - each part's CARD sample by its hash: found in the index, one source a sample (two parts on one sample share it),
 *     the rings and the heads one block of the arena, every head the file's first bytes, the rings in the block;
 *   - the setting (the pre-buffer) changed: the voices let go first (the sources not ready at once), placed again, its
 *     KB as the setting's page shows it;
 *   - no room: refused, the part whose memory would make room proposed (one at a time, as model.c mem_offer), the longest pre-buffer that fits proposed, the
 *     samples silent; placed by itself once the part's memory is given back;
 *   - a hash not on the card: that part MISSING, the others play;
 *   - a placed sample streams: its frames through the rings as the file holds them;
 *   - a part on SLICE CARD: its sample whole in the block (sd_slice.c), no ring and no head for it; off it, streamed.
 *   sd_stream_plan_test FAT16.img FAT32.img */
#define FELUCCA_SD_FATFS 1
#define FELUCCA_SD_RING_KB 4
#define AINL static inline
#include "sd_card_model.h"
#include "storage/sd/sd_mmc.c"
#include "storage/sd/sd_diskio.c"
#include "storage/sd/sd_layout.c"
#include "storage/sd/sd_hash.c"
#include "storage/sd/sd_alloc.c"
#include "storage/sd/sd_index.c"
#include "storage/sd/sd_stream.c"
#define ARN_PARTS 4u                                 /* three parts, then the streams' block */
#define ARN_BYTES (72u * 1024u)
#define ARN_TOPS 8u
#include "core/arena.c"
#define SST_ARN_K 3u
static uint32_t part_voices[3] = {8, 8, 8};
#define SST_PART_VOICES(p) part_voices[p]
static int irq_depth;
static void fm1_irq_off(void) { irq_depth++; }
static void fm1_irq_on(void) { irq_depth--; }
/* SLICE on CARD (sd_slice.c in the firmware): a part on it, its sample whole in the block (a stand-in size) */
static uint32_t part_slice;
#define SST_PART_SLICE(p) ((part_slice >> (p)) & 1u)
static const void *slc_tab[3];
static char ssl_path[64];
static FRESULT ssl_need(const char *path, uint32_t *bytes)
{
    FIL f;
    FRESULT r = f_open(&f, path, FA_READ);
    if (r != FR_OK)
        return r;
    *bytes = (uint32_t)f_size(&f) / 4u + 1001u;
    (void)f_close(&f);
    return FR_OK;
}
static const void *ssl_load(const char *path, uint8_t *mem, uint32_t n, uint32_t *used)
{
    if (ssl_need(path, used) != FR_OK || *used > n)
        return 0;
    snprintf(ssl_path, sizeof ssl_path, "%s", path);
    memset(mem, 0xA5, *used);
    return mem;
}
static void ssl_set(uint32_t p, const void *t) { slc_tab[p] = t; }
#include "storage/sd/sd_stream_plan.c"

static FATFS fs __attribute__((aligned(4)));
static uint8_t buf[512] __attribute__((aligned(4)));
static uint32_t work[64];

static int16_t pat16(uint32_t j, uint32_t seed) { return (int16_t)(j * 7919u + seed * 40503u + (j >> 7) * 3u); }

static void le16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void le32(uint8_t *p, uint32_t v) { le16(p, v); le16(p + 2, v >> 16); }

static int put_wav(const char *path, uint32_t frames, uint32_t seed)   /* 44.1 kHz 16-bit mono */
{
    static uint8_t d[4096];
    uint8_t h[44];
    FIL f;
    UINT k;
    uint32_t i, at = 0, n;
    memcpy(h, "RIFF", 4); le32(h + 4, 36u + frames * 2u); memcpy(h + 8, "WAVEfmt ", 8); le32(h + 16, 16);
    le16(h + 20, 1); le16(h + 22, 1); le32(h + 24, 44100); le32(h + 28, 88200); le16(h + 32, 2); le16(h + 34, 16);
    memcpy(h + 36, "data", 4); le32(h + 40, frames * 2u);
    if (f_open(&f, path, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK || f_write(&f, h, 44, &k) != FR_OK)
        return 0;
    while (at < frames) {
        n = frames - at < 2048u ? frames - at : 2048u;
        for (i = 0; i < n; i++)
            le16(d + 2u * i, (uint16_t)pat16(at + i, seed));
        if (f_write(&f, d, n * 2u, &k) != FR_OK || k != n * 2u)
            return 0;
        at += n;
    }
    return f_close(&f) == FR_OK;
}

static void settle(void)                             /* the gate's wait, then the plan */
{
    int i;
    for (i = 0; i < 4 && sstp.state == SSTP_GATED; i++) {
        clk += SST_GATE_MS * 1000u + 1000u;
        sst_plan_poll();
    }
}

static int head_ok(uint32_t si, uint32_t seed)
{
    const sst_src_t *s = &sst_src[si];
    uint32_t i;
    for (i = 0; i + 1u < s->hlen; i += 2u)
        if ((int16_t)(s->head[i] | s->head[i + 1u] << 8) != pat16(i / 2u, seed))
            return 0;
    return s->ready && s->hlen > 0;
}

static void run_card(const char *img_path)
{
    uint32_t sectors, i, k;
    uint8_t *img = load(img_path, &sectors), *blk;
    const char *what = img_path;
    sdx_rec rec;
    uint64_t h0, h1;
    int32_t x;
    card(img, sectors, 0, 1, 1, 0);
    dma_on = 1;
    memset(&fs, 0, sizeof fs);
    memset(&arn_cur, 0, sizeof arn_cur);
    memset(&arn_has_next, 0, sizeof arn_has_next);
    memset(&sstp, 0, sizeof sstp);
    memset(sst_part_hash, 0, sizeof sst_part_hash);
    part_voices[0] = part_voices[1] = part_voices[2] = 8;
    sst_pre_ms = 50;
    sd_gone = 0;
    sd_dstat = STA_NOINIT;
    CHECK(f_mount(&fs, "", 1) == FR_OK && sdl_prepare() == FR_OK, "%s: mount, the folders", what);
    CHECK(put_wav("OPTIMIST/SAMPLES/kick.wav", 44100, 1) && f_mkdir("OPTIMIST/SAMPLES/loops") == FR_OK &&
          put_wav("OPTIMIST/SAMPLES/loops/pad.wav", 66150, 2),
          "%s: two samples", what);
    CHECK(sdx_update(buf, sizeof buf, work, 64) == FR_OK && sdx_open() == FR_OK, "%s: the index", what);
    CHECK(sdx_find_path("SAMPLES/kick.wav", &rec) >= 0, "%s: kick indexed", what);
    h0 = rec.hash;
    CHECK(sdx_find_path("SAMPLES/loops/pad.wav", &rec) >= 0, "%s: pad indexed", what);
    h1 = rec.hash;
    CHECK(h0 && h1 && h0 != h1, "%s: two hashes", what);

    fm1_irq_off();                                   /* part 0's engine: 20 KB of the arena */
    CHECK(arn_reserve(0, 20u * 1024u) == 0 && arn_take(0), "%s: part 0's block", what);
    fm1_irq_on();

    /* the parts' samples placed: one source a sample, the rings and heads one block */
    sst_part_set(0, h0);
    sst_part_set(1, h1);
    sst_part_set(2, h0);
    sst_plan_poll();
    CHECK(sstp.state == SSTP_GATED, "%s: gated first", what);
    settle();
    CHECK(sstp.state == SSTP_PLACED && sstp.nsrc == 2 && !sstp.missing, "%s: placed (%u, %u sources, missing %x)",
          what, sstp.state, sstp.nsrc, sstp.missing);
    CHECK(sst_part_src[0] == 0 && sst_part_src[1] == 1 && sst_part_src[2] == 0, "%s: the parts to their sources (%u %u %u)",
          what, sst_part_src[0], sst_part_src[1], sst_part_src[2]);
    blk = (uint8_t *)arn_ptr(SST_ARN_K);
    CHECK(blk && arn_cur[SST_ARN_K].len >= sstp.need, "%s: one block of the arena (%u B)", what, sstp.need);
    CHECK(sstp.need == SST_N * SST_K * 512u + 12u * (sst_src[0].nrun + sst_src[1].nrun) + 4412u * 2u,
          "%s: the need: the rings, the runs, two 50 ms heads (%u)", what, sstp.need);
    CHECK(head_ok(0, 1) && head_ok(1, 2), "%s: the heads are the files' first bytes", what);
    for (i = 0, k = 1; i < SST_N; i++)
        k &= sst[i].ring >= blk && sst[i].ring + SST_K * 512u <= blk + sstp.need;
    CHECK(k, "%s: the rings in the block", what);
    CHECK(irq_depth == 0, "%s: the interrupts back on", what);
    CHECK(sst_pre_kb(100) == (2u * 8820u + 12u * (sst_src[0].nrun + sst_src[1].nrun) + 1023u) / 1024u,
          "%s: the setting's KB at 100 ms (%u)", what, sst_pre_kb(100));

    /* a placed sample streams through the rings */
    i = (uint32_t)sst_start(sst_part_src[1], 0, 0, 0, sst_rate(&sst_src[1], 65536u), 0);
    for (k = 0; k < 40; k++)
        (void)sst_service();
    for (k = 0, x = 0; k < 3000u && sst_frame(&sst[i], &sst_src[1], k, &x) && x == pat16(k, 2); k++)
        ;
    CHECK(k == 3000u, "%s: pad's first 3000 frames through head and ring (%u)", what, k);
    sst_stop(i);

    /* the setting changed: the voices let go at once, placed again */
    sst_pre_set(100);
    sst_plan_poll();
    CHECK(!sst_src[0].ready && !sst_src[1].ready && sst_part_src[0] == 0xFF, "%s: gated: the sources not ready", what);
    settle();
    CHECK(sstp.state == SSTP_PLACED && sst_src[0].hlen == 8820u, "%s: 100 ms heads (%u)", what, sst_src[0].hlen);

    /* no room: 200 ms needs more than the arena has beside part 0 */
    sst_pre_set(200);
    sst_plan_poll();
    settle();
    CHECK(sstp.state == SSTP_REFUSED, "%s: 200 ms refused (%u B)", what, sstp.need);
    CHECK(sstp.drop == 1 && sstp.drop_all, "%s: part 0's memory proposed (%x)", what, sstp.drop);
    CHECK(sstp.fit_ms == 100, "%s: 100 ms proposed (%u)", what, sstp.fit_ms);
    CHECK(sst_part_src[0] == 0xFF && sst_part_src[1] == 0xFF && !arn_ptr(SST_ARN_K), "%s: silent meanwhile", what);
    clk += SST_RETRY_MS * 1000u + 1000u;
    sst_plan_poll();
    CHECK(sstp.state == SSTP_REFUSED, "%s: still no room", what);
    fm1_irq_off();
    arn_give(0);                                     /* part 0's engine let go */
    fm1_irq_on();
    clk += SST_RETRY_MS * 1000u + 1000u;
    sst_plan_poll();
    CHECK(sstp.state == SSTP_PLACED && sst_part_src[1] == 1 && sst_src[1].hlen == 17640u && head_ok(1, 2),
          "%s: placed by itself once there is room (%u)", what, sstp.state);

    /* the rings as many as the CARD parts can sound at once: three one-voice parts, three rings */
    part_voices[0] = part_voices[1] = part_voices[2] = 1;
    sst_plan_poll();
    settle();
    CHECK(sstp.state == SSTP_PLACED && sstp.nring == 3u && sst[2].ring && !sst[3].ring,
          "%s: three one-voice parts, three rings (%u)", what, sstp.nring);
    CHECK(sstp.need == 3u * SST_K * 512u + 12u * (sst_src[0].nrun + sst_src[1].nrun) + 2u * 17640u,
          "%s: their need (%u)", what, sstp.need);
    part_voices[0] = part_voices[1] = part_voices[2] = 8;

    /* part 1 on SLICE CARD: its sample whole in the block, no ring and no head for it; off again: streamed (one-voice
     * parts: the arena's 72 KB) */
    part_voices[0] = part_voices[1] = part_voices[2] = 1;
    part_slice = 2u;
    sst_plan_poll();
    CHECK(sstp.state == SSTP_GATED && !slc_tab[1], "%s: SLICE CARD: placed again", what);
    settle();
    blk = (uint8_t *)arn_ptr(SST_ARN_K);
    k = (66150u * 2u + 44u) / 4u + 1001u;
    CHECK(sstp.state == SSTP_PLACED && slc_tab[1] && (const uint8_t *)slc_tab[1] >= blk &&
          (const uint8_t *)slc_tab[1] + k <= blk + sstp.need && strstr(ssl_path, "pad.wav"),
          "%s: part 1's sample whole in the block (%u, %s)", what, sstp.state, ssl_path);
    CHECK(sst_part_src[1] == 0xFF && !sst_src[1].ready && sst_part_src[0] == 0 && sst_part_src[2] == 0,
          "%s: not streamed; parts 0 and 2 stream (%u %u %u)", what, sst_part_src[0], sst_part_src[1], sst_part_src[2]);
    CHECK(sstp.nring == 2u && sstp.need == 2u * SST_K * 512u + 12u * sst_src[0].nrun + 17640u + ((k + 3u) & ~3u),
          "%s: the need: two rings (parts 0 and 2), kick's head, pad whole (%u rings, %u B)", what, sstp.nring, sstp.need);
    part_slice = 0;
    sst_plan_poll();
    CHECK(!slc_tab[1], "%s: off SLICE CARD: its material let go at once", what);
    settle();
    CHECK(sstp.state == SSTP_PLACED && sstp.nring == 3u && sst_part_src[1] == 1 && head_ok(1, 2) && !slc_tab[1],
          "%s: off SLICE CARD: streamed again", what);
    part_voices[0] = part_voices[1] = part_voices[2] = 8;

    /* a sample not on the card: MISSING, the others play */
    sst_part_set(1, 0x123456789ull);
    sst_plan_poll();
    settle();
    CHECK(sstp.state == SSTP_PLACED && sstp.missing == 2u && sst_part_src[1] == 0xFF && sst_part_src[0] == 0,
          "%s: part 1 missing (%x), part 0 plays", what, sstp.missing);
    sst_part_set(0, 0);
    sst_part_set(1, 0);
    sst_part_set(2, 0);
    sst_plan_poll();
    settle();
    CHECK(sstp.state == SSTP_IDLE && !arn_ptr(SST_ARN_K), "%s: no card samples: nothing held", what);
    free(img);
}

int main(int argc, char **argv)
{
    int i;
    if (argc < 3) {
        printf("usage: sd_stream_plan_test FAT16.img FAT32.img\n");
        return 2;
    }
    for (i = 1; i < argc; i++)
        run_card(argv[i]);
    printf("sd_stream_plan_test: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
