/* SPDX-License-Identifier: GPL-3.0-only */
/* Recording to the SD card (firmware/src/storage/sd/sd_rec.c; docs/SD-CARD-DESIGN.md 7) on the byte-level card model
 * (tests/sd_card_model.h: CMD25 with a busy after each block, a stall once) with full FatFs on the FAT16 and FAT32
 * images. The audio is an interrupt: every 726 ticks (32 frames at 44.1 kHz) it feeds the recorder a block of a known
 * pattern; the main loop draws a screen of strips (bytes with the card deselected) and calls sdr_service between them
 * and in its idle wait, as main.c does. After each take the file is read back through FatFs and checked frame by frame.
 *   - 2 s, a quick card: the WAV's header (sizes, format, the data at 512), every frame, none lost;
 *   - a 150 ms stall: the 32 KB ring (186 ms) covers it, none lost;
 *   - a 300 ms stall: frames dropped and counted, never garbage: the file holds the frames before, then those after;
 *   - a take of 100 ms asked and recorded on: the run full, the take ends there (FULL), its length the run's;
 *   - while a take writes, FatFs' reads are refused (the card is in CMD25), and work again after it.
 *   sd_rec_test FAT16.img FAT32.img */
#define FELUCCA_SD_FATFS 1
#define FELUCCA_SD_REC_KB 32
#define AINL static inline
#define IN_POOL
#include "sd_card_model.h"
#include "storage/sd/sd_mmc.c"
#include "storage/sd/sd_diskio.c"
#include "storage/sd/sd_alloc.c"
#include "storage/sd/sd_rec.c"

static FATFS fs __attribute__((aligned(4)));
static uint32_t frames_fed, next_block, skip_from = 0xFFFFFFFFu, skip_n;

static int16_t patv(uint32_t i) { return (int16_t)(i * 2971u + (i >> 11) * 7u); }   /* sample i (L, R interleaved) */

#define BLOCK_US 726u
static void audio(void)                                  /* the audio interrupt: 32 frames each block */
{
    int32_t q[64];
    uint32_t i;
    if (clk < next_block)
        return;
    next_block += BLOCK_US;
    for (i = 0; i < 64u; i++)
        q[i] = patv(frames_fed * 2u + i);
    sdr_feed(q, 32);
    frames_fed += 32;
}

static void screen(uint32_t n)
{
    while (n--) {
        C.selected = 0;
        (void)xchg(0x2A);
    }
}

static void main_loop(uint32_t ms)
{
    uint32_t end = clk + ms * 1000u, t0, s;
    while (clk < end) {
        t0 = clk;
        for (s = 0; s < 4u; s++) {
            (void)sdr_service();
            screen(1500);                                /* (a strip: 1.5 ms of pixels) */
        }
        while (clk - t0 < 15000u && clk < end)
            if (!sdr_service())
                screen(100);
    }
}

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

/* the file back: its header, then frames; lost frames counted out where the stall was -> the frames that match */
static uint32_t check_file(const char *path, const char *what, uint32_t *data_out)
{
    FIL f;
    uint8_t h[512], b[512];
    UINT got;
    uint32_t data, i = 0, ok = 0, n, k;
    CHECK(f_open(&f, path, FA_READ) == FR_OK, "%s: %s opens", what, path);
    CHECK(f_read(&f, h, 512, &got) == FR_OK && got == 512, "%s: the header block", what);
    data = rd32(h + 508);
    *data_out = data;
    CHECK(!memcmp(h, "RIFF", 4) && rd32(h + 4) == 504u + data && !memcmp(h + 8, "WAVEfmt ", 8) && rd32(h + 24) == 44100u &&
          h[22] == 2 && h[34] == 16 && !memcmp(h + 36, "JUNK", 4) && rd32(h + 40) == 460u && !memcmp(h + 504, "data", 4),
          "%s: RIFF / fmt / JUNK / data, 44.1 kHz 16-bit stereo, data at 512 (%u B)", what, data);
    for (n = 0; n < data; n += 512u) {
        UINT want = data - n < 512u ? data - n : 512u;
        if (f_read(&f, b, want, &got) != FR_OK || got != want)
            break;
        for (k = 0; k < want; k += 2u, i++) {
            uint32_t s = i >= skip_from * 2u ? i + skip_n * 2u : i;   /* (after the frames dropped) */
            int16_t v = (int16_t)(b[k] | b[k + 1u] << 8);
            ok += v == patv(s);
        }
    }
    (void)f_close(&f);
    return ok;
}

static void take(const char *path, uint32_t ask_ms, uint32_t rec_ms)
{
    CHECK(sdr_start(path, ask_ms) == FR_OK, "%s: started", path);
    frames_fed = 0;
    next_block = clk + BLOCK_US;
    clk_hook = audio;
    main_loop(rec_ms);
    sdr_stop();
    clk_hook = 0;
    main_loop(300);
}

static void run_card(const char *img_path)
{
    uint32_t sectors, data, ok;
    uint8_t *img = load(img_path, &sectors);
    const char *what = img_path;
    card(img, sectors, 0, 1, 1, 0);
    C.crc_on = 1;
    dma_on = 1;
    memset(&fs, 0, sizeof fs);
    sd_gone = 0;
    sd_dstat = STA_NOINIT;
    CHECK(f_mount(&fs, "", 1) == FR_OK, "%s: mount", what);

    C.wbusy = 300;
    skip_from = 0xFFFFFFFFu;
    take("t1.wav", 3000, 2000);
    CHECK(sdr_state == RC_DONE && sdr.lost == 0, "%s: 2 s: done, none lost (state %u, lost %u, the ring's most %u blocks)", what,
          sdr_state, sdr.lost, sdr.lag_max);
    ok = check_file("t1.wav", what, &data);
    CHECK(data == frames_fed * 4u && ok == data / 2u, "%s: 2 s: every frame (%u B of %u fed, %u samples right)", what, data,
          frames_fed * 4u, ok);
    CHECK(C.wr_misuse == 0 && C.cmd25s == 1, "%s: one CMD25, nothing sent while busy", what);

    C.wstall_at = C.wblocks + 40;
    C.wstall = 150000;
    take("t2.wav", 3000, 2000);
    ok = check_file("t2.wav", what, &data);
    CHECK(sdr_state == RC_DONE && sdr.lost == 0 && data == frames_fed * 4u && ok == data / 2u,
          "%s: a 150 ms stall: covered, none lost, every frame (lost %u, the ring's most %u of %u blocks)", what, sdr.lost,
          sdr.lag_max, SDR_BLKS);

    C.wstall_at = C.wblocks + 40;
    C.wstall = 300000;
    take("t3.wav", 3000, 2000);
    CHECK(sdr_state == RC_DONE && sdr.lost > 0 && sdr.lost % 32u == 0, "%s: a 300 ms stall: frames dropped and counted (%u)",
          what, sdr.lost);
    {
        FIL f;
        uint8_t h[512], b[4];
        UINT got;
        uint32_t i, first_bad = 0xFFFFFFFFu;
        CHECK(f_open(&f, "t3.wav", FA_READ) == FR_OK && f_read(&f, h, 512, &got) == FR_OK, "%s: t3 opens", what);
        data = rd32(h + 508);
        for (i = 0; i < data / 4u; i++) {                /* (the frames up to the stall's loss, then a jump: never garbage) */
            if (f_read(&f, b, 4, &got) != FR_OK || got != 4)
                break;
            if ((int16_t)(b[0] | b[1] << 8) != patv(2u * i)) {
                first_bad = i;
                break;
            }
        }
        if (first_bad != 0xFFFFFFFFu) {                  /* from there on: the pattern sdr.lost frames later */
            uint32_t j = first_bad + sdr.lost, bad = 0;
            (void)f_lseek(&f, 512u + first_bad * 4u);
            for (i = first_bad; i < data / 4u; i++, j++) {
                if (f_read(&f, b, 4, &got) != FR_OK || got != 4)
                    break;
                bad += (int16_t)(b[0] | b[1] << 8) != patv(2u * j) || (int16_t)(b[2] | b[3] << 8) != patv(2u * j + 1u);
            }
            CHECK(bad == 0 && data == (frames_fed - sdr.lost) * 4u,
                  "%s: the frames before the loss, then those after it, nothing else (bad %u)", what, bad);
        } else {
            CHECK(0, "%s: a loss counted but no jump in the file", what);
        }
        (void)f_close(&f);
    }

    C.wstall_at = 0;
    take("t4.wav", 100, 1000);
    ok = check_file("t4.wav", what, &data);
    CHECK(sdr_state == RC_FULL && data == (sdr.run - 1u) * 512u && ok == data / 2u,
          "%s: 100 ms asked, 1 s played: the run full, the take ends there (state %u, %u B, run %u blocks)", what, sdr_state,
          data, sdr.run);

    CHECK(sdr_start("t5.wav", 3000) == FR_OK, "%s: t5 started", what);
    {
        uint8_t b[512] __attribute__((aligned(4)));
        CHECK(disk_read(0, b, 0, 1) == RES_NOTRDY, "%s: a read while it writes: refused", what);
        sdr_stop();
        main_loop(100);
        CHECK(sdr_state == RC_DONE && disk_read(0, b, 0, 1) == RES_OK, "%s: after it: reads again", what);
    }
    free(img);
}

int main(int argc, char **argv)
{
    int i;
    if (argc < 2) {
        printf("usage: sd_rec_test FAT16.img [FAT32.img]\n");
        return 2;
    }
    for (i = 1; i < argc; i++)
        run_card(argv[i]);
    printf("sd recorder test: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
