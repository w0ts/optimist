/* SPDX-License-Identifier: GPL-3.0-only */
/* The polled multi-block writer (firmware/src/storage/sd/sd_mmc.c sd_wr_*; docs/SD-CARD-DESIGN.md 7) on the byte-level
 * card model (tests/sd_card_model.h: CMD25, a busy after each block, a stall once). Between the writer's calls the
 * "screen" clocks pixel bytes with the card deselected (the LCD's traffic): the card must keep the write open, and the
 * writer itself must clock nothing but 0xFF while the card is deselected (the model counts every other byte the LCD
 * sees: exactly the screen's).
 *   - 64 blocks, by bytes and by the DMA model, on SDHC (block addresses) and SDSC (byte addresses): every block on the
 *     card, one CMD25, nothing sent while busy, the screen's bytes clean;
 *   - a 150 ms stall after block 10: polled through (busy, never an error), the rest written;
 *   - a 600 ms stall: an error after 500 ms, the abort stops the write, the card reads again;
 *   - a power cut after 20 blocks: an error at the next block, the 20 on the card.
 *   sd_wr_test */
#define FELUCCA_SD_FATFS 1
#define AINL static inline
#include "sd_card_model.h"
#include "storage/sd/sd_mmc.c"

#define NSEC 4096u
static uint8_t img[NSEC * 512u];
static uint8_t src[64u * 512u] __attribute__((aligned(4)));
static long screen_bytes;

static void screen(uint32_t n)                         /* the LCD's strip: bytes with the card deselected */
{
    while (n--) {
        C.selected = 0;
        (void)xchg(0x2A);                              /* (pixels: the card ignores them while deselected) */
        screen_bytes++;
    }
}

static void fill(uint32_t seed)
{
    uint32_t i;
    for (i = 0; i < sizeof src; i++)
        src[i] = (uint8_t)(i * 31u + seed * 7u + (i >> 9));
}

/* n blocks from src to sector s0 through the writer, the screen between calls; -> the state it ended in */
static BYTE write_run(uint32_t s0, uint32_t n, long *polls)
{
    uint32_t k = 0;
    BYTE st;
    if (sd_wr_open(s0) != RES_OK)
        return SDW_ERR;
    for (;;) {
        st = sd_wr_poll();
        (*polls)++;
        if (st == SDW_ERR)
            return st;
        if (st == SDW_READY) {
            if (k == n) {
                (void)sd_wr_stop();
            } else if (sd_wr_block(src + k * 512u) == SDW_ERR) {
                return SDW_ERR;
            } else {
                k++;
            }
        }
        if (st == SDW_IDLE)
            return k == n ? SDW_IDLE : SDW_ERR;
        screen(40);                                    /* (a strip's worth of the screen between polls) */
    }
}

static int start(int hc)
{
    memset(img, 0, sizeof img);
    card(img, NSEC, 0, 1, hc, 0);
    C.crc_on = 1;
    return sd_card_init() == 0;
}

int main(void)
{
    int hc, d;
    long polls;
    for (hc = 1; hc >= 0; hc--)
        for (d = 0; d <= 1; d++) {
            const char *w = hc ? "SDHC" : "SDSC";
            CHECK(start(hc), "%s: the card starts", w);
            dma_on = d;
            fill((uint32_t)(hc * 2 + d));
            C.wbusy = 300;                              /* (0.3 ms a block: a quick card) */
            polls = 0;
            screen_bytes = 0;
            CHECK(write_run(100, 64, &polls) == SDW_IDLE, "%s dma %d: 64 blocks written, stopped", w, d);
            CHECK(!memcmp(img + 100u * 512u, src, 64u * 512u), "%s dma %d: every block on the card", w, d);
            CHECK(C.cmd25s == 1 && C.wblocks == 64 && C.wr_misuse == 0 && C.crc_refused == 0,
                  "%s dma %d: one CMD25, 64 blocks, nothing sent while busy, no CRC refused (%ld %ld %ld)", w, d,
                  C.cmd25s, C.wr_misuse, C.crc_refused);
            CHECK(C.lcd_bad == screen_bytes && screen_bytes > 0,
                  "%s dma %d: the screen's bytes between blocks (%ld), none of the writer's own reached the LCD", w, d,
                  screen_bytes);
        }

    CHECK(start(1), "stall: the card starts");
    dma_on = 1;
    fill(9);
    C.wbusy = 300;
    C.wstall_at = 10;
    C.wstall = 150000;                                 /* 150 ms once */
    polls = 0;
    {
        uint32_t c0 = clk;
        CHECK(write_run(200, 64, &polls) == SDW_IDLE && !memcmp(img + 200u * 512u, src, 64u * 512u),
              "a 150 ms stall after block 10: polled through, every block written");
        CHECK(clk - c0 >= 150000u && C.wr_misuse == 0, "... it took the stall's time, nothing sent while busy");
    }

    CHECK(start(1), "timeout: the card starts");
    fill(11);
    C.wbusy = 300;
    C.wstall_at = 5;
    C.wstall = 600000;                                 /* 600 ms: past the writer's 500 */
    polls = 0;
    CHECK(write_run(300, 64, &polls) == SDW_ERR, "a 600 ms stall: an error");
    sd_wr_abort();
    {
        uint8_t b[512] __attribute__((aligned(4)));
        CHECK(sd_wr.state == SDW_IDLE && disk_readblocks(b, 300, 1) == RES_OK && !memcmp(b, src, 512),
              "... aborted, the card reads again (the first block there)");
    }

    CHECK(start(1), "cut: the card starts");
    fill(13);
    C.wbusy = 300;
    C.cut_at = 20;
    polls = 0;
    CHECK(write_run(400, 64, &polls) == SDW_ERR, "a power cut after 20 blocks: an error");
    CHECK(!memcmp(img + 400u * 512u, src, 20u * 512u) && C.wblocks == 20, "... the 20 blocks before it on the card");

    printf("sd writer test: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
