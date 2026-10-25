/* SPDX-License-Identifier: GPL-3.0-only */
/* The SD card (builder item SDCARD): ChaN's SPI-mode driver (firmware/src/storage/sd/sd_mmc.c) and Petit FatFs (pff.c)
 * against a byte-level card model, on FAT16 and FAT32 images made by a real formatter (tests/fixtures/sd/make_images.sh).
 *   sd_test FAT16.img FAT32.img
 * The model answers like an SPI-mode card: R1 a byte after the command, CMD8 / ACMD41 / CMD58 for SDv2 (block or byte
 * addressed), SDv1 and MMC start-ups, CSD v1 / v2, data tokens with the block's real CRC16, CMD18's endless stream of
 * blocks (gaps of varying length before each token) stopped by CMD12 (stuff byte, R1, busy), a write's response and
 * busy bytes. It also checks the FM-1's bus rule: every byte clocked while the card is deselected reaches the LCD, so
 * each must be 0xFF. A DMA model (SD_RXDMA / SD_TXDMA) moves a block only after a few polls of SD_DMA_DONE, and counts
 * any byte the CPU clocks while a DMA is running (the bus is the DMA's then). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t clk;                           /* the fake clock: one tick a byte, 1000 a millisecond */

typedef struct {
    uint8_t *img;                              /* the filesystem's sectors */
    uint32_t sectors;                          /* of the image */
    uint32_t base;                             /* where the image starts on the card (>0: an MBR before it) */
    int present, v2, hc, mmc;
    int acmd41_busy;                           /* idle answers before ready (-1: never ready) */
    int no_token;                              /* CMD17 never sends its data token */
    int selected, app, idle;
    int crc_on;                                /* CMD59: commands' CRC7 and written blocks' CRC16 checked */
    long crc_refused;                          /* commands and blocks refused for their CRC */
    uint8_t cmd[6];
    int cmdlen;
    uint8_t out[600];
    int outlen, outpos;
    int wstate;                                /* 0, 1 waiting for 0xFE, 2 data */
    uint32_t waddr;
    uint8_t wbuf[514];
    int wpos;
    long lcd_bytes, lcd_bad;                   /* bytes clocked while deselected; those not 0xFF */
    long writes;
    int rs;                                    /* CMD18 streaming */
    uint32_t rsec;                             /* the next block it sends */
    uint32_t corrupt;                          /* a sector whose data comes with one byte flipped (its CRC unchanged) */
    uint32_t corrupt_once;                     /* the same, the first time it is read only (a glitch) */
    int bad30;                                 /* every block's data wrong at the faster clock (a card that cannot) */
    int hs;                                    /* CMD6 switched it to high speed */
    int nac_long;                              /* 1: some gaps before a token longer than a poll's tries */
    long cmd18s, cmd12s, cmd17s;
} card_t;
static card_t C;
static int t_over;                             /* the host's clock: 1 the faster one (SD_SET_OVER) */

static void q(uint8_t b) { if (C.outlen < (int)sizeof C.out) C.out[C.outlen++] = b; }

static uint8_t *sector_ptr(uint32_t s, uint8_t *scratch)
{
    if (s >= C.base && s - C.base < C.sectors)
        return C.img + (size_t)(s - C.base) * 512u;
    memset(scratch, 0, 512);
    if (s == 0 && C.base) {                    /* the MBR: one partition at base */
        uint8_t *e = scratch + 446;
        e[4] = C.sectors > 65536u ? 0x0C : 0x06;
        e[8] = (uint8_t)C.base; e[9] = (uint8_t)(C.base >> 8); e[10] = (uint8_t)(C.base >> 16);
        e[12] = (uint8_t)C.sectors; e[13] = (uint8_t)(C.sectors >> 8); e[14] = (uint8_t)(C.sectors >> 16);
        scratch[510] = 0x55; scratch[511] = 0xAA;
    }
    return scratch;
}

static uint32_t card_sectors(void) { return C.base + C.sectors; }

static uint16_t crc16(const uint8_t *p, int n)  /* the model's own (CRC16-CCITT from 0) */
{
    uint16_t c = 0;
    int i;
    while (n--) {
        c ^= (uint16_t)(*p++ << 8);
        for (i = 0; i < 8; i++) c = (uint16_t)(c & 0x8000 ? (c << 1) ^ 0x1021 : c << 1);
    }
    return c;
}

static void q_block(uint32_t s, int gap)       /* gap 0xFF bytes, the token, the data, its CRC */
{
    uint8_t scratch[512], d[512];
    uint16_t c;
    int i;
    memcpy(d, sector_ptr(s, scratch), 512);
    c = crc16(d, 512);
    if (s == C.corrupt || (t_over && C.bad30)) d[100] ^= 0x10;
    if (s == C.corrupt_once) d[200] ^= 0x01, C.corrupt_once = 0xFFFFFFFFu;
    while (gap--) q(0xFF);
    q(0xFE);
    for (i = 0; i < 512; i++) q(d[i]);
    q((uint8_t)(c >> 8)); q((uint8_t)c);
}

static void stream_more(void)                  /* CMD18: the next block when the last has gone out */
{
    if (C.rsec >= card_sectors()) {            /* past the end: an error token, the stream over */
        q(0xFF); q(0x08);
        C.rs = 0;
        return;
    }
    q_block(C.rsec, C.nac_long && C.rsec % 3 == 0 ? 3 + (int)(C.rsec % 7) * 5 : 1 + (int)(C.rsec % 3));
    C.rsec++;
}

static void csd(uint8_t *c)
{
    memset(c, 0, 16);
    if (C.hc) {                                /* CSD v2: (C_SIZE + 1) x 512 KB */
        uint32_t cs = card_sectors() / 1024u - 1u;
        c[0] = 0x40; c[7] = (uint8_t)(cs >> 16 & 0x3F); c[8] = (uint8_t)(cs >> 8); c[9] = (uint8_t)cs;
    } else {                                   /* CSD v1: READ_BL_LEN 9, C_SIZE_MULT 7: (C_SIZE + 1) x 512 blocks */
        uint32_t cs = card_sectors() / 512u - 1u;
        c[5] = 9; c[6] = (uint8_t)(cs >> 10 & 3); c[7] = (uint8_t)(cs >> 2); c[8] = (uint8_t)(cs << 6);
        c[9] = 3; c[10] = 0x80;
    }
}

static void command(void)
{
    uint32_t idx = C.cmd[0] & 0x3Fu, arg = (uint32_t)C.cmd[1] << 24 | (uint32_t)C.cmd[2] << 16 | (uint32_t)C.cmd[3] << 8 | C.cmd[4];
    uint8_t c16[16];
    int app = C.app, i;
    C.app = 0;
    q(0xFF);                                   /* NCR: one byte before the answer */
    if (C.crc_on || idx == 0 || idx == 8) {    /* (CMD0 and CMD8 are always checked) */
        uint8_t cc = 0, b;
        int n, k;
        for (n = 0; n < 5; n++)
            for (b = C.cmd[n], k = 0; k < 8; k++, b <<= 1) {
                cc <<= 1;
                if ((b ^ cc) & 0x80) cc ^= 0x09;
            }
        if ((uint8_t)(cc << 1 | 1) != C.cmd[5]) {
            C.crc_refused++;
            q((uint8_t)(C.idle | 0x08));       /* the command CRC error bit: not executed */
            return;
        }
    }
    switch (idx) {
    case 59:
        C.crc_on = arg & 1u;
        q(C.idle);
        return;
    case 6: {                                  /* SWITCH_FUNC (SDv2): a 64-byte status block */
        uint8_t st[64];
        uint16_t c;
        if (!C.v2 || C.mmc || C.idle) { q(0x05); return; }
        memset(st, 0, 64);
        C.hs = (arg >> 31) && (arg & 15u) == 1u;
        st[16] = (uint8_t)(arg & 15u);         /* bits 379:376: group 1's function now */
        c = crc16(st, 64);
        q(0); q(0xFF); q(0xFE);
        for (i = 0; i < 64; i++) q(st[i]);
        q((uint8_t)(c >> 8)); q((uint8_t)c);
        return;
    }
    case 0:
        C.idle = 1;
        q(C.cmd[5] == 0x95 ? 0x01 : 0x09);     /* (a bad CRC: the CRC error bit) */
        return;
    case 8:
        if (!C.v2) { q(0x05); return; }        /* illegal command: SDv1 or MMC */
        q(C.idle); q(0); q(0); q((uint8_t)(arg >> 8 & 15)); q((uint8_t)arg);
        return;
    case 55:
        C.app = 1;
        q(C.idle);
        return;
    case 41:
    case 1:
        if ((idx == 41 && (!app || C.mmc)) || (idx == 1 && !C.mmc)) { q(0x05); return; }
        if (C.acmd41_busy < 0) { q(0x01); return; }
        if (C.acmd41_busy > 0) { C.acmd41_busy--; q(0x01); return; }
        C.idle = 0;
        q(0x00);
        return;
    case 58:
        q(C.idle); q((uint8_t)(0x80 | (C.hc ? 0x40 : 0))); q(0xFF); q(0x80); q(0);
        return;
    case 16:
        q(arg == 512 ? 0 : 0x40);
        return;
    case 9:
        q(0); q(0xFF); q(0xFE); csd(c16);
        for (i = 0; i < 16; i++) q(c16[i]);
        q(0); q(0);
        return;
    case 12:
        C.rs = 0;
        C.cmd12s++;
        q(0x00); q(0x00); q(0x00);             /* (after the stuff byte above) R1, then busy */
        return;
    case 17:
    case 18:
    case 24: {
        uint32_t s;
        if (C.idle) { q(0x05); return; }
        if (!C.hc && (arg & 511u)) { q(0x40); return; }   /* a byte address off a block */
        s = C.hc ? arg : arg / 512u;
        if (s >= card_sectors()) { q(0x40); return; }
        q(0);
        if (idx == 24) { C.wstate = 1; C.waddr = s; return; }
        if (C.no_token) return;
        if (idx == 18) { C.cmd18s++; C.rs = 1; C.rsec = s; return; }
        C.cmd17s++;
        q_block(s, 2);
        return;
    }
    default:
        q(0x04);
    }
}

static uint8_t xchg(uint8_t in)
{
    int rs0;
    clk++;
    if (!C.present)
        return 0xFF;
    if (!C.selected) {                         /* the LCD gets this byte */
        C.lcd_bytes++;
        if (in != 0xFF) C.lcd_bad++;
        return 0xFF;
    }
    rs0 = C.rs;                                /* (a byte the stream's listener took is not taken again below) */
    if (C.rs) {                                /* streaming: the card listens for CMD12 while it sends */
        if (C.cmdlen || (in & 0xC0) == 0x40) {
            C.cmd[C.cmdlen++] = in;
            if (C.cmdlen == 6) {
                C.cmdlen = 0;
                C.outlen = C.outpos = 0;
                command();                     /* (CMD12 ends the stream; any other: an answer, the stream goes on) */
                return 0xFF;
            }
        }
        if (C.outpos >= C.outlen) {
            C.outlen = C.outpos = 0;
            stream_more();
        }
    }
    if (!rs0 && !C.wstate && (C.cmdlen || (in & 0xC0) == 0x40)) {   /* a command coming in (DI is read while DO sends: */
        C.outlen = C.outpos = 0;               /* what the card had left to say, a stopped stream's end, is dropped) */
        C.cmd[C.cmdlen++] = in;
        if (C.cmdlen == 6) { C.cmdlen = 0; command(); }
        return 0xFF;
    }
    if (C.outpos < C.outlen) {
        uint8_t b = C.out[C.outpos++];
        if (C.outpos == C.outlen) C.outlen = C.outpos = 0;
        return b;
    }
    if (C.wstate == 1) {
        if (in == 0xFE) { C.wstate = 2; C.wpos = 0; }
        return 0xFF;
    }
    if (C.wstate == 2) {
        C.wbuf[C.wpos++] = in;
        if (C.wpos == 514) {
            uint8_t scratch[512];
            uint8_t *p = sector_ptr(C.waddr, scratch);
            C.wstate = 0; C.outlen = C.outpos = 0;
            if (C.crc_on && crc16(C.wbuf, 512) != (uint16_t)(C.wbuf[512] << 8 | C.wbuf[513])) {
                C.crc_refused++;
                q(0xEB);                       /* data rejected: a CRC error (nothing written) */
                return 0xFF;
            }
            memcpy(p, C.wbuf, 512);
            C.writes++;
            q(0xE5); q(0); q(0); q(0);         /* data accepted, then busy */
        }
        return 0xFF;
    }
    if (C.cmdlen == 0 && (in & 0xC0) != 0x40)
        return 0xFF;
    C.cmd[C.cmdlen++] = in;
    if (C.cmdlen == 6) { C.cmdlen = 0; command(); }
    return 0xFF;
}

static struct {
    uint8_t *p;
    const uint8_t *tp;
    uint32_t n;
    int active, running, polls, tx;
    long ops, clash, misaligned, busy_polls;
} D;                                           /* the DMA model */
static int dma_on;

static uint8_t cpu_xchg(uint8_t in)
{
    if (D.active && !D.running) D.clash++;     /* the CPU on the bus while the DMA has it */
    return xchg(in);
}

static void dma_run(void)
{
    uint32_t i;
    D.running = 1;
    for (i = 0; i < D.n; i++) {
        if (D.tx) (void)xchg(D.tp[i]);
        else D.p[i] = xchg(0xFF);
    }
    D.running = 0;
    D.active = 0;
}

static void dma_start(const void *p, uint32_t n, int tx)
{
    if ((uintptr_t)p & 3u) D.misaligned++;
    if (!n) D.misaligned++;                    /* (SPI_CNT 0 never completes on the chip) */
    D.p = (uint8_t *)p; D.tp = (const uint8_t *)p; D.n = n; D.tx = tx;
    D.active = 1; D.polls = 3; D.ops++;
}

static int dma_done(void)
{
    if (!D.active) return 1;
    if (--D.polls > 0) { D.busy_polls++; return 0; }
    dma_run();
    return 1;
}

static int dma_end(void)
{
    if (D.active) dma_run();
    return 1;
}

static void rx_block(uint8_t *p, unsigned n)
{
    while (n--) *p++ = cpu_xchg(0xFF);
}

#define SD_TX(b) ((void)cpu_xchg((uint8_t)(b)))
#define SD_RX() cpu_xchg(0xFF)
#define SD_RXBLK(p, n) rx_block(p, n)
#define SD_DMA_OK() dma_on
#define SD_RXDMA(p, n) dma_start(p, n, 0)
#define SD_TXDMA(p, n) dma_start(p, n, 1)
#define SD_DMA_DONE() dma_done()
#define SD_DMA_END() dma_end()
#define SD_RD_CRC 1
#define SD_SEL() (C.selected = 1)
#define SD_DESEL() (C.selected = 0, C.cmdlen = 0, C.outlen = C.outpos = 0)
#define SD_FAST() ((void)0)
#define SD_SET_OVER(on) (t_over = (on))
#define SD_NOW() clk
#define SD_TICKS_MS 1000u
#include "storage/sd/sd_mmc.c"
#include "storage/sd/pff.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint8_t *load(const char *path, uint32_t *sectors)
{
    FILE *f = fopen(path, "rb");
    long n;
    uint8_t *p;
    if (!f) { printf("cannot open %s\n", path); exit(2); }
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    p = malloc((size_t)n);
    if (!p || fread(p, 1, (size_t)n, f) != (size_t)n) { printf("cannot read %s\n", path); exit(2); }
    fclose(f);
    *sectors = (uint32_t)(n / 512);
    return p;
}

static void card(uint8_t *img, uint32_t sectors, uint32_t base, int v2, int hc, int mmc)
{
    memset(&C, 0, sizeof C);
    C.img = img; C.sectors = sectors; C.base = base;
    C.present = 1; C.v2 = v2; C.hc = hc; C.mmc = mmc;
    C.acmd41_busy = 20;
    C.corrupt = C.corrupt_once = 0xFFFFFFFFu;
    t_over = 0;
}

static uint8_t kick(uint32_t i) { return (uint8_t)((i * 7 + (i >> 8)) & 255); }

static void files(const char *what, BYTE fat)
{
    FATFS fs;
    DIR d;
    FILINFO fi;
    UINT got, put;
    static uint8_t buf[4096] __attribute__((aligned(4))), pat[4096];
    DWORD sec;
    UINT n, off;
    int seen_hello = 0, seen_data = 0, seen_samples = 0, seen_long = 0;
    uint32_t i;
    long lcd0;

    CHECK(pf_mount(&fs) == FR_OK, "%s: mount", what);
    CHECK(fs.fs_type == fat, "%s: FAT type %u, want %u", what, fs.fs_type, fat);
    CHECK(sd_sectors == card_sectors(), "%s: capacity %lu sectors, want %lu", what, (unsigned long)sd_sectors,
          (unsigned long)card_sectors());

    CHECK(pf_open("HELLO.TXT") == FR_OK, "%s: open HELLO.TXT", what);
    memset(buf, 0, sizeof buf);
    CHECK(pf_read(buf, 100, &got) == FR_OK && got == 28, "%s: HELLO.TXT %u bytes", what, got);
    CHECK(!memcmp(buf, "Hello from the FM-1 SD card\n", 28), "%s: HELLO.TXT contents", what);

    CHECK(pf_open("SAMPLES/KICK.RAW") == FR_OK, "%s: open SAMPLES/KICK.RAW", what);
    CHECK(pf_read(buf, sizeof buf, &got) == FR_OK && got == 3000, "%s: KICK.RAW %u bytes", what, got);
    for (i = 0; i < 3000 && buf[i] == kick(i); i++)
        ;
    CHECK(i == 3000, "%s: KICK.RAW differs at %u", what, (unsigned)i);
    CHECK(pf_lseek(2500) == FR_OK && pf_read(buf, 16, &got) == FR_OK && got == 16 && buf[0] == kick(2500)
          && buf[15] == kick(2515), "%s: seek 2500 then read", what);

    CHECK(pf_opendir(&d, "") == FR_OK, "%s: open the root", what);
    while (pf_readdir(&d, &fi) == FR_OK && fi.fname[0]) {
        seen_hello |= !strcmp(fi.fname, "HELLO.TXT") && fi.fsize == 28;
        seen_data |= !strcmp(fi.fname, "DATA.BIN") && fi.fsize == 4096;
        seen_samples |= !strcmp(fi.fname, "SAMPLES") && (fi.fattrib & AM_DIR);
        seen_long |= !strncmp(fi.fname, "LONGFI", 6);
    }
    CHECK(seen_hello && seen_data && seen_samples && seen_long, "%s: root listing (hello %d data %d samples %d long %d)",
          what, seen_hello, seen_data, seen_samples, seen_long);

    for (i = 0; i < sizeof pat; i++) pat[i] = (uint8_t)(i * 13 + 5);
    CHECK(pf_open("DATA.BIN") == FR_OK, "%s: open DATA.BIN", what);
    CHECK(pf_write(pat, sizeof pat, &put) == FR_OK && put == sizeof pat, "%s: write %u", what, put);
    CHECK(pf_write(0, 0, &put) == FR_OK, "%s: write finalized", what);
    CHECK(C.writes == 8, "%s: %ld sectors written, want 8", what, C.writes);
    CHECK(pf_open("DATA.BIN") == FR_OK && pf_read(buf, sizeof buf, &got) == FR_OK && got == sizeof pat
          && !memcmp(buf, pat, sizeof pat), "%s: DATA.BIN read back", what);
    CHECK(pf_write(pat, 16, &put) == FR_OK && put == 0, "%s: no write past the file's end (%u)", what, put);

    CHECK(pf_open("SAMPLES/KICK.RAW") == FR_OK, "%s: open KICK.RAW for its runs", what);
    memset(buf, 0, sizeof buf);
    for (off = 0; off < sizeof buf && (n = pf_run(&sec)) != 0; off += n * 512u) {
        CHECK(n >= 1 && n <= fs.csize && off + n * 512u <= sizeof buf, "%s: run of %u at %u", what, n, off);
        CHECK(disk_readblocks(buf + off, sec, n) == RES_OK, "%s: the run at %u read", what, off);
        CHECK(pf_lseek(off + n * 512u) == FR_OK, "%s: seek past the run", what);
    }
    for (i = 0; i < 3000 && buf[i] == kick(i); i++)
        ;
    CHECK(off == 3072 && i == 3000, "%s: KICK.RAW by its runs: %u B, differs at %u", what, off, (unsigned)i);
    CHECK(pf_lseek(100) == FR_OK && pf_run(&sec) == 0, "%s: no run off a sector boundary", what);

    CHECK(pf_open("NOPE.TXT") == FR_NO_FILE, "%s: a missing file", what);
    lcd0 = C.lcd_bytes;
    CHECK(C.lcd_bad == 0, "%s: %ld bytes not 0xFF reached the LCD", what, C.lcd_bad);
    CHECK(lcd0 > 0, "%s: the deselect clocks went to the LCD side", what);
}

static int same_as_card(const uint8_t *buf, uint32_t s, uint32_t n)
{
    uint8_t scratch[512];
    for (; n; n--, s++, buf += 512)
        if (memcmp(buf, sector_ptr(s, scratch), 512)) return 0;
    return 1;
}

/* whole blocks: CMD18 + CMD12 and CMD17 through the polled reader, by bytes and by DMA; CRC errors, the card's end, no
 * token, block writes */
static void blocks(const char *what)
{
    static uint8_t buf[48 * 512] __attribute__((aligned(4))), pat[512] __attribute__((aligned(4)));
    uint8_t scratch[512], one[4];
    uint32_t s0 = C.base + 3, n = 40, i, t0;          /* (inside the image: writes land there) */
    long c18, c12, c17, w0;
    int d, busy, tok_busy, st;

    for (d = 0; d < 2; d++) {
        dma_on = d;
        memset(&D, 0, sizeof D);
        c18 = C.cmd18s; c12 = C.cmd12s; c17 = C.cmd17s;
        memset(buf, 0xA5, sizeof buf);
        CHECK(disk_readblocks(buf, s0, n) == RES_OK, "%s dma %d: %u blocks by CMD18", what, d, n);
        CHECK(same_as_card(buf, s0, n), "%s dma %d: the blocks' data", what, d);
        CHECK(C.cmd18s == c18 + 1 && C.cmd12s == c12 + 1 && C.cmd17s == c17, "%s dma %d: one CMD18, one CMD12", what, d);
        CHECK(!C.rs, "%s dma %d: the stream stopped", what, d);
        CHECK(D.ops == (d ? (long)n : 0) && D.clash == 0 && D.misaligned == 0,
              "%s dma %d: %ld DMAs, %ld CPU bytes during one, %ld misaligned", what, d, D.ops, D.clash, D.misaligned);
        CHECK(disk_readp(one, 0, 510, 2) == RES_OK && !memcmp(one, sector_ptr(0, scratch) + 510, 2),
              "%s dma %d: the card answers after CMD12", what, d);
        CHECK(disk_readblocks(buf, s0 + 5, 1) == RES_OK && same_as_card(buf, s0 + 5, 1) && C.cmd17s == c17 + 2,
              "%s dma %d: one block by CMD17", what, d);
        CHECK(disk_readp(buf, s0 + 7, 0, 512) == RES_OK && same_as_card(buf, s0 + 7, 1),
              "%s dma %d: a whole sector by disk_readp", what, d);
        CHECK(C.lcd_bad == 0, "%s dma %d: %ld bytes not 0xFF reached the LCD", what, d, C.lcd_bad);
    }

    dma_on = 1;                                /* polled: the reader hands back while the token or the DMA is due */
    memset(&D, 0, sizeof D);
    C.nac_long = 1;
    busy = tok_busy = 0;
    CHECK(sd_rd_open(s0, 12) == RES_OK, "%s: open 12", what);
    for (i = 0; i < 12; i++) {
        sd_rd_next(buf + i * 512);
        while ((st = sd_rd_poll()) == SDR_BUSY) {
            busy++;
            tok_busy += sd_rd.state == SDR_TOKEN;
        }
        CHECK(st == SDR_READY && sd_rd.crc_ok, "%s: polled block %u", what, (unsigned)i);
    }
    CHECK(sd_rd_close() == RES_OK && same_as_card(buf, s0, 12), "%s: polled blocks' data", what);
    CHECK(tok_busy > 0 && busy - tok_busy >= 12 && D.busy_polls >= 12 && D.clash == 0,
          "%s: busy polls %d (token %d, DMA %ld), clashes %ld", what, busy, tok_busy, D.busy_polls, D.clash);
    C.nac_long = 0;

    for (d = 0; d < 2; d++) {
        dma_on = d;
        C.corrupt = s0 + 2;                    /* a block whose CRC does not match: an error, the stream stopped */
        CHECK(disk_readblocks(buf, s0, 5) == RES_ERROR && !C.rs, "%s dma %d: a CRC error", what, d);
        CHECK(disk_readp(one, s0 + 2, 100, 4) == RES_ERROR && disk_readp(buf, s0 + 2, 0, 512) == RES_ERROR,
              "%s dma %d: disk_readp checks the block's CRC too (a part, a whole sector)", what, d);
        C.corrupt = 0xFFFFFFFFu;
        CHECK(disk_readp(one, s0 + 2, 100, 4) == RES_OK && !memcmp(one, sector_ptr(s0 + 2, scratch) + 100, 4),
              "%s dma %d: disk_readp of a part, CRC right", what, d);
        CHECK(disk_readblocks(buf, s0, 5) == RES_OK && same_as_card(buf, s0, 5), "%s dma %d: read again", what, d);
        CHECK(disk_readblocks(buf, card_sectors() - 2, 4) == RES_ERROR, "%s dma %d: past the card's end", what, d);
        CHECK(disk_readblocks(buf, card_sectors() - 2, 2) == RES_OK && same_as_card(buf, card_sectors() - 2, 2),
              "%s dma %d: the card's last two blocks", what, d);
        CHECK(disk_readblocks(buf, card_sectors(), 2) == RES_ERROR, "%s dma %d: CMD18 at the end refused", what, d);
        C.no_token = 1;
        t0 = clk;
        CHECK(disk_readblocks(buf, s0, 3) == RES_ERROR, "%s dma %d: no data token", what, d);
        CHECK(clk - t0 >= 1000u * 100u && clk - t0 < 1000u * 120u, "%s dma %d: gave up after %u ms", what, d,
              (unsigned)((clk - t0) / 1000u));
        C.no_token = 0;
        CHECK(disk_readblocks(buf, s0, 2) == RES_OK, "%s dma %d: reads after the timeout", what, d);

        memset(&D, 0, sizeof D);
        for (i = 0; i < 512; i++) pat[i] = (uint8_t)(i * 3 + d);
        w0 = C.writes;
        CHECK(disk_writeblock(pat, s0 + 9) == RES_OK && C.writes == w0 + 1, "%s dma %d: a block written", what, d);
        CHECK(!memcmp(sector_ptr(s0 + 9, scratch), pat, 512), "%s dma %d: the block's data on the card", what, d);
        CHECK(D.ops == d && D.clash == 0, "%s dma %d: write DMAs %ld, clashes %ld", what, d, D.ops, D.clash);
        CHECK(disk_readblocks(buf, s0 + 9, 1) == RES_OK && !memcmp(buf, pat, 512), "%s dma %d: read back", what, d);
    }
    CHECK(C.lcd_bad == 0, "%s: %ld bytes not 0xFF reached the LCD", what, C.lcd_bad);
    CHECK(C.crc_on && sd_crc_on, "%s: CMD59: the card checks the CRCs", what);
    CHECK(C.crc_refused == 0, "%s: %ld commands or blocks refused for their CRC (each CRC7 and CRC16 sent right)", what,
          C.crc_refused);
    {   /* a command with a wrong CRC7 (a garbled one, or a stray CMD12 on DI during a CMD18 run): refused, not executed */
        static const uint8_t bad[6] = {0x40 | 12, 0, 0, 0, 0, 0x01};
        long r0 = C.crc_refused;
        C.rs = 1;
        memcpy(C.cmd, bad, 6);
        C.outlen = C.outpos = 0;
        command();
        CHECK(C.crc_refused == r0 + 1 && C.rs == 1, "%s: a CMD12 with a wrong CRC7: refused, the stream goes on", what);
        C.rs = 0;
        C.outlen = C.outpos = 0;
    }
    dma_on = 0;
}

/* the clock the card proves (sd_clock_probe) and the drop to the floor on CRC failures (sd_crc_retry) */
static void clocks(const char *what)
{
    static uint8_t buf[8 * 512] __attribute__((aligned(4)));
    uint8_t one[4], scratch[512];
    uint32_t s0 = C.base + 3, n = card_sectors() < 64u ? card_sectors() : 64u;
    UINT bad;
    long d0, r0 = C.crc_refused;
    int sd2 = C.v2 && !C.mmc;

    C.bad30 = 0;
    bad = sd_clock_probe(buf, n, 0);
    if (sd2) {
        CHECK(bad == 0 && sd_over && t_over && C.hs, "%s: a card that reads right at 30 MHz: kept there (CMD6 switched)",
              what);
    } else {
        CHECK(sd_over == 0 && !t_over && bad == n, "%s: SDv1 / MMC: the floor, 20 MHz, nothing tried", what);
        return;
    }
    C.bad30 = 1;
    bad = sd_clock_probe(buf, n, 0);
    CHECK(bad == n && !sd_over && !t_over, "%s: a card that fails at 30 MHz: %u of %u blocks bad, back to 20", what,
          (unsigned)bad, (unsigned)n);
    CHECK(sd_clock_probe(buf, n, 20) == n && !sd_over && !t_over, "%s: sd clock 20: the floor, nothing tried", what);
    CHECK(sd_clock_probe(buf, n, 30) == n && sd_over && t_over, "%s: sd clock 30: forced", what);

    d0 = sd_demotions;                         /* at 30, the card starts failing: the read goes on at 20 */
    CHECK(disk_readblocks(buf, s0, 8) == RES_OK && same_as_card(buf, s0, 8), "%s: CRC failures at 30: read right", what);
    CHECK(sd_demotions == d0 + 1 && !sd_over && !t_over, "%s: ... the session dropped to 20 MHz once", what);
    SD_SET_OVER(1), sd_over = 1;
    CHECK(disk_readp(one, s0 + 1, 40, 4) == RES_OK && !memcmp(one, sector_ptr(s0 + 1, scratch) + 40, 4) &&
          sd_demotions == d0 + 2 && !sd_over, "%s: disk_readp at 30 failing: read at 20, dropped", what);
    C.bad30 = 0;

    d0 = sd_demotions;                         /* at 20, a glitch: read again, no drop, no error */
    C.corrupt_once = s0 + 4;
    CHECK(disk_readblocks(buf, s0, 8) == RES_OK && same_as_card(buf, s0, 8) && sd_demotions == d0,
          "%s: one CRC glitch at 20: the block read again, right", what);
    C.corrupt_once = s0 + 2;
    CHECK(disk_readp(buf, s0 + 2, 0, 512) == RES_OK && same_as_card(buf, s0 + 2, 1), "%s: ... disk_readp too", what);
    C.corrupt = s0 + 4;                        /* at 20, a block that keeps failing: an error */
    CHECK(disk_readblocks(buf, s0, 8) == RES_ERROR, "%s: a block failing twice at 20: an error", what);
    C.corrupt = 0xFFFFFFFFu;
    CHECK(C.crc_refused == r0, "%s: no command refused for its CRC through the probes", what);
}

int main(int argc, char **argv)
{
    uint32_t n16, n32;
    uint8_t *i16, *i32, *copy;
    FATFS fs;
    uint32_t t0;

    if (argc < 3) { printf("usage: sd_test FAT16.img FAT32.img\n"); return 2; }
    i16 = load(argv[1], &n16);
    i32 = load(argv[2], &n32);
    copy = malloc((size_t)n32 * 512u);

    memcpy(copy, i32, (size_t)n32 * 512u);
    card(copy, n32, 0, 1, 1, 0);               /* SDHC, FAT32 on the whole card (no MBR) */
    files("SDHC FAT32", FS_FAT32);
    CHECK(CardType == (CT_SD2 | CT_BLOCK), "SDHC: card type %u", CardType);

    memcpy(copy, i32, (size_t)n32 * 512u);
    card(copy, n32, 2048, 1, 1, 0);            /* SDHC, FAT32 in a partition at 2048 */
    files("SDHC FAT32 + MBR", FS_FAT32);

    memcpy(copy, i32, (size_t)n32 * 512u);     /* the same with the sectors by DMA (pf_read's whole sectors) */
    card(copy, n32, 2048, 1, 1, 0);
    dma_on = 1;
    memset(&D, 0, sizeof D);
    files("SDHC FAT32 + MBR, DMA", FS_FAT32);
    CHECK(D.ops > 0 && D.clash == 0 && D.misaligned == 0, "DMA files: %ld DMAs, %ld clashes, %ld misaligned", D.ops,
          D.clash, D.misaligned);
    dma_on = 0;
    blocks("SDHC + MBR");
    clocks("SDHC + MBR");

    memcpy(copy, i16, (size_t)n16 * 512u);
    card(copy, n16, 2048, 1, 0, 0);            /* SDv2 byte-addressed (SDSC), FAT16 in a partition */
    files("SDSC v2 FAT16 + MBR", FS_FAT16);
    CHECK(CardType == CT_SD2, "SDSC v2: card type %u", CardType);
    blocks("SDSC v2 (byte addresses)");
    clocks("SDSC v2 (byte addresses)");

    memcpy(copy, i16, (size_t)n16 * 512u);
    card(copy, n16, 0, 0, 0, 0);               /* SDv1 */
    files("SDv1 FAT16", FS_FAT16);
    CHECK(CardType == CT_SD1, "SDv1: card type %u", CardType);

    memcpy(copy, i16, (size_t)n16 * 512u);
    card(copy, n16, 0, 0, 0, 1);               /* MMCv3 */
    files("MMC FAT16", FS_FAT16);
    CHECK(CardType == CT_MMC, "MMC: card type %u", CardType);
    blocks("MMC");
    clocks("MMC");

    card(copy, n16, 0, 1, 1, 0);
    C.present = 0;                             /* no card: every byte 0xFF */
    t0 = clk;
    CHECK(pf_mount(&fs) == FR_NOT_READY, "no card: not ready");
    CHECK(clk - t0 < 1000u * 50u, "no card: gave up in %u ms", (unsigned)((clk - t0) / 1000u));

    card(copy, n16, 0, 1, 1, 0);
    C.acmd41_busy = -1;                        /* never leaves idle: the 1 s start-up limit */
    t0 = clk;
    CHECK(pf_mount(&fs) == FR_NOT_READY, "stuck card: not ready");
    CHECK(clk - t0 >= 1000u * 1000u && clk - t0 < 1000u * 1100u, "stuck card: gave up after %u ms",
          (unsigned)((clk - t0) / 1000u));

    memset(copy, 0, (size_t)n16 * 512u);
    card(copy, n16, 0, 1, 1, 0);               /* a card without a filesystem */
    CHECK(pf_mount(&fs) == FR_NO_FILESYSTEM, "blank card: no filesystem");

    memcpy(copy, i16, (size_t)n16 * 512u);
    card(copy, n16, 0, 1, 1, 0);
    CHECK(pf_mount(&fs) == FR_OK, "token test: mount");
    C.no_token = 1;                            /* a read whose data never comes: the 100 ms limit */
    t0 = clk;
    CHECK(disk_readp(copy, 0, 0, 16) == RES_ERROR, "no data token: error");
    CHECK(clk - t0 >= 1000u * 100u && clk - t0 < 1000u * 110u, "no data token: gave up after %u ms",
          (unsigned)((clk - t0) / 1000u));

    printf("sd_test: %d checks, %d failed\n", checks, fails);
    free(copy); free(i16); free(i32);
    return fails ? 1 : 0;
}
