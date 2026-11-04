/* SPDX-License-Identifier: GPL-3.0-only */
/* The SD card's clock detection (firmware/src/storage/sd/sd_mmc.c sd_clock_probe; hal/fm1_sd_clock.h's steps 30 20 15 12
 * 10 8.57 7.5 6.67 6 MHz; docs/SD-CARD-DESIGN.md "The clock") on the byte-level card model (tests/sd_card_model.h: a
 * card whose reads go wrong above a given clock, time out there, read zeros with a right CRC, fail now and then, fail at
 * one step alone, or stop answering for good):
 *   - every step reads: 30 MHz passes (CMD6 took high speed), 20 kept (the margin step below the fastest);
 *   - no high speed: 30 skipped, 20 passes, 15 kept; SDv1 and MMC: the floor, nothing tried;
 *   - reads wrong above each step in turn: that step the fastest, the next kept (the floor when that is the floor);
 *   - timeouts above 10 MHz: the first ends the walk at the floor, the card reads there;
 *   - the black FM-1's failure: noise past the start-up above 15 MHz: mounts at the floor; CMD6 switched only when its
 *     status (CRC right) says high speed is supported;
 *   - zeros with a right CRC16 above 12 MHz: caught by the floor's fingerprint (SDK_DIFF);
 *   - a block in 30 wrong above 8.57 MHz: the second pass catches what the first missed; 6.67 kept;
 *   - the margin step failing alone: the walk goes on after it;
 *   - the card gone at a step: the probe ends at the floor, the card type cleared (the mount says no card);
 *   - every probe: nothing written, nothing but 0xFF to the LCD, no command refused for its CRC;
 *   - at run time, by bytes and by DMA: a CRC failure above the floor drops one step and the read is redone (the data
 *     right), counted; failing all the way down: at the floor one more try, then an error.
 *   sd_clock_test */
#define FELUCCA_SD_FATFS 1
#define AINL static inline
#include "sd_card_model.h"
#include "storage/sd/sd_mmc.c"

#define NSEC 8192u
static uint8_t img[NSEC * 512u];
static uint8_t buf[16u * 512u] __attribute__((aligned(4)));
static const char *const RES_NAME[6] = {"-", "pass", "crc", "diff", "err", "nohs"};

static void fresh(int v2, int hc, int mmc)             /* a new card, started at 400 kHz then the floor */
{
    card(img, NSEC, 0, v2, hc, mmc);
    sd_step = SD_FLOOR;
    CHECK(sd_card_init() == 0 && CardType, "card start-up (v2 %d hc %d mmc %d)", v2, hc, mmc);
}

static int same(uint32_t s, uint32_t n)
{
    uint8_t scratch[512];
    uint32_t i;
    for (i = 0; i < n; i++)
        if (memcmp(buf + i * 512u, sector_ptr(s + i, scratch), 512)) return 0;
    return 1;
}

static void steps_line(const char *what)               /* (a failure's context: what each step did) */
{
    uint32_t k;
    printf("  %s:", what);
    for (k = 0; k < SD_NSTEP; k++)
        printf(" %u=%s", (unsigned)(FM1_SD_STEP_KHZ(k) / 100u), RES_NAME[sd_clk_res[k] % 6u]);
    printf("  kept %u, top %u, ms %u\n", (unsigned)sd_step, (unsigned)sd_clk_top, (unsigned)sd_clk_ms);
}

/* the probe on the card as set; checks the clean-up rules; -> the steps that failed */
static UINT probe(const char *what)
{
    long w0 = C.writes, r0 = C.crc_refused, l0 = C.lcd_bad;
    UINT bad = sd_clock_probe(buf, 16, 0);
    CHECK(C.writes == w0 && C.cmd25s == 0, "%s: the probe writes nothing", what);
    CHECK(C.lcd_bad == l0, "%s: only 0xFF reached the LCD", what);
    CHECK(C.crc_refused == r0, "%s: no command refused for its CRC", what);
    CHECK(t_step == (int)sd_step, "%s: the bus left at the clock kept (step %d, kept %u)", what, t_step,
          (unsigned)sd_step);
    return bad;
}

static void expect(const char *what, BYTE top, BYTE kept)
{
    int ok = sd_clk_top == top && sd_step == kept;
    CHECK(ok, "%s: fastest step %u (want %u), kept %u (want %u)", what, (unsigned)sd_clk_top, (unsigned)top,
          (unsigned)sd_step, (unsigned)kept);
    if (!ok) steps_line(what);
    CHECK(disk_readblocks(buf, 100, 16) == RES_OK && same(100, 16), "%s: reads right at the clock kept", what);
}

static void walks(void)
{
    int k;
    char w[80];

    fresh(1, 1, 0);                                    /* every step reads */
    CHECK(probe("all good") == 0, "all good: no step failed");
    expect("all good", 0, 1);
    CHECK(C.hs && sd_clk_res[0] == SDK_PASS && sd_clk_res[1] == SDK_PASS && sd_clk_res[SD_FLOOR] == SDK_PASS &&
          sd_clk_res[2] == SDK_UNTRIED, "all good: 30 and 20 passed (CMD6 high speed), the floor's reference, 15 untried");
    CHECK(SD_CLOCK_KHZ() == 20000u, "all good: 20000 kHz shown, %u", (unsigned)SD_CLOCK_KHZ());

    fresh(1, 1, 0);                                    /* CMD6 refused high speed: 30 not tried */
    C.no_hs = 1;
    (void)probe("no high speed");
    expect("no high speed", 1, 2);
    CHECK(sd_clk_res[0] == SDK_NOHS, "no high speed: 30 skipped (%s)", RES_NAME[sd_clk_res[0] % 6u]);

    fresh(1, 0, 0);                                    /* SDSC v2 (byte addresses): the same walk */
    (void)probe("SDSC v2");
    expect("SDSC v2", 0, 1);

    fresh(0, 0, 0);                                    /* SDv1, MMC: the floor, nothing tried */
    CHECK(probe("SDv1") == 0 && sd_step == SD_FLOOR && sd_clk_res[SD_FLOOR] == SDK_UNTRIED, "SDv1: the floor, untried");
    fresh(0, 0, 1);
    CHECK(probe("MMC") == 0 && sd_step == SD_FLOOR && sd_clk_res[0] == SDK_UNTRIED, "MMC: the floor, untried");

    for (k = 1; k <= (int)SD_FLOOR; k++) {             /* wrong above step k: k the fastest, k + 1 kept */
        fresh(1, 1, 0);
        C.ok_from = k;
        snprintf(w, sizeof w, "wrong above %u kHz", (unsigned)FM1_SD_STEP_KHZ(k));
        CHECK(probe(w) == (UINT)k, "%s: %d steps failed", w, k);
        if (k < (int)SD_FLOOR)
            expect(w, (BYTE)k, (BYTE)(k + 1));
        else
            expect(w, SD_FLOOR, SD_FLOOR);
        CHECK(sd_clk_res[k - 1] == SDK_CRC, "%s: the step above failed on a CRC (%s)", w,
              RES_NAME[sd_clk_res[k - 1] % 6u]);
    }

    fresh(1, 1, 0);                                    /* the line dead above 10 MHz: timeouts */
    C.dead_from = 5;
    {
        uint32_t t0 = clk;
        (void)probe("timeouts");
        expect("timeouts", SD_FLOOR, SD_FLOOR);
        CHECK(sd_clk_res[0] == SDK_ERR && sd_clk_res[1] == SDK_UNTRIED, "timeouts: the first ends the walk at the floor");
        CHECK(clk - t0 < 1000u * 1500u, "timeouts: the probe took %u ms (under 1.5 s)", (unsigned)((clk - t0) / 1000u));
        printf("sd_clock_test: the model's probe times (a byte a us): timeouts above 10 MHz %u ms\n",
               (unsigned)sd_clk_ms);
    }

    fresh(1, 1, 0);                                    /* noise past the start-up above 15 MHz */
    C.garbage_from = 3;
    (void)probe("noise");
    expect("noise", SD_FLOOR, SD_FLOOR);
    CHECK(CardType && sd_clk_ms < 5500u, "noise: mounted, the probe under 5.5 s (%u ms)", (unsigned)sd_clk_ms);

    fresh(1, 1, 0);                                    /* CMD6's status wrong: no switch */
    C.cmd6_crc_bad = 1;
    (void)probe("CMD6 CRC");
    CHECK(!C.cmd6_switches && sd_clk_res[0] == SDK_NOHS, "CMD6's status CRC wrong: not switched, 30 skipped");
    fresh(1, 1, 0);
    C.no_hs = 1;
    (void)probe("CMD6 unsupported");
    CHECK(!C.cmd6_switches, "CMD6's status says no high speed: not switched");

    fresh(1, 1, 0);                                    /* zeros with a right CRC16 above 12 MHz */
    C.zero_from = 3;
    (void)probe("zeros");
    expect("zeros", 3, 4);
    CHECK(sd_clk_res[2] == SDK_DIFF, "zeros: 15 MHz differs from the floor's fingerprint (%s)",
          RES_NAME[sd_clk_res[2] % 6u]);

    fresh(1, 1, 0);                                    /* a block in 30 wrong above 8.57 MHz */
    C.flaky_from = 6;
    C.flaky_every = 30;
    (void)probe("now and then");
    expect("now and then", 6, 7);

    fresh(1, 1, 0);                                    /* 30 wrong, 15 alone wrong: 20 passes, its margin fails */
    C.ok_from = 1;
    C.bad_only = 3;
    (void)probe("margin fails");
    expect("margin fails", 3, 4);
    CHECK(sd_clk_res[1] == SDK_PASS && sd_clk_res[2] == SDK_CRC, "margin fails: 20 passed, 15 failed");

    fresh(1, 1, 0);                                    /* the card gone at 30 MHz */
    C.die_from = 1;
    (void)sd_clock_probe(buf, 16, 0);
    CHECK(sd_step == SD_FLOOR && !CardType, "card gone: the floor, the card type cleared (%u)", (unsigned)CardType);
    C.die_from = 0;
}

/* at run time: the clock kept, then the card worse (wrong at and above the step kept) */
static void run_time(int dma)
{
    long d0, f0;
    uint8_t part[8], scratch[512];
    dma_on = dma;
    memset(&D, 0, sizeof D);

    fresh(1, 1, 0);
    C.ok_from = 2;
    (void)probe("run time");
    CHECK(sd_step == 3, "run time dma %d: 12 MHz kept", dma);
    C.ok_from = 4;                                     /* 12 MHz goes wrong (and 10 reads) */
    d0 = sd_demotions;
    f0 = (long)sd_crc_fails;
    CHECK(disk_readblocks(buf, 200, 16) == RES_OK && same(200, 16), "run time dma %d: CMD18 read right", dma);
    CHECK(sd_step == 4 && t_step == 4 && sd_demotions == d0 + 1 && (long)sd_crc_fails == f0 + 1,
          "run time dma %d: one step down (to 10 MHz), counted (step %u, downs %ld, fails %ld)", dma, (unsigned)sd_step,
          (long)sd_demotions - d0, (long)sd_crc_fails - f0);
    C.ok_from = 5;
    CHECK(disk_readblocks(buf, 300, 1) == RES_OK && same(300, 1) && sd_step == 5, "run time dma %d: CMD17 too", dma);
    C.ok_from = 6;
    CHECK(sd_readp(part, 301, 100, 8) == RES_OK && !memcmp(part, sector_ptr(301, scratch) + 100, 8) && sd_step == 6,
          "run time dma %d: sd_readp too", dma);
    C.ok_from = (int)SD_FLOOR;                         /* wrong everywhere above the floor: down to it, then read */
    CHECK(disk_readblocks(buf, 400, 8) == RES_OK && same(400, 8) && sd_step == SD_FLOOR,
          "run time dma %d: stepped down to the floor, read right", dma);
    C.ok_from = 0;
    C.corrupt = 402;                                   /* at the floor a block that keeps failing: an error */
    CHECK(disk_readblocks(buf, 400, 8) == RES_ERROR && sd_step == SD_FLOOR, "run time dma %d: at the floor: an error", dma);
    C.corrupt = 0xFFFFFFFFu;
    CHECK(D.clash == 0 && D.misaligned == 0, "run time dma %d: no CPU byte during a DMA", dma);
    dma_on = 0;
}

int main(void)
{
    uint32_t i;
    for (i = 0; i < sizeof img; i++)
        img[i] = (uint8_t)(i * 2654435761u >> 13);
    walks();
    run_time(0);
    run_time(1);
    printf("sd_clock_test: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
