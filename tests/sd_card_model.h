/* SPDX-License-Identifier: GPL-3.0-only */
/* The SD card model the SD tests share (tests/sd_test.c: the driver and Petit FatFs; tests/sd_fs_test.c: full FatFs
 * and the card's layout on it). A byte-level SPI-mode card: R1 a byte after the command, CMD8 / ACMD41 / CMD58 for SDv2
 * (block or byte addressed), SDv1 and MMC start-ups, CSD v1 / v2, data tokens with the block's real CRC16, CMD18's
 * endless stream of blocks (gaps of varying length before each token) stopped by CMD12 (stuff byte, R1, busy), a
 * write's response and busy bytes, CMD59 (the commands' CRC7 and the written blocks' CRC16 checked). It also checks the
 * FM-1's bus rule: every byte clocked while the card is deselected reaches the LCD, so each must be 0xFF. A DMA model
 * (SD_RXDMA / SD_TXDMA) moves a block only after a few polls of SD_DMA_DONE, and counts any byte the CPU clocks while a
 * DMA is running (the bus is the DMA's then). A power cut (C.cut_at): the card takes that many block writes, then
 * loses its power before the next one lands (a block is written whole or not at all: the model's sectors are atomic,
 * as a card's are meant to be) and answers nothing until card_power_on. CMD25 (the recorder's writer): blocks after
 * 0xFC tokens, each followed by a busy of C.wbusy ticks (DO low while selected), a longer one once (C.wstall_at: the
 * block after which the card stalls C.wstall ticks), 0xFD ends it (a busy of C.wbusy too); the card may be deselected
 * between blocks and during busy (the write stays open); a token or a command sent while busy is counted
 * (C.wr_misuse). Include after setting FELUCCA_SD_FATFS; then
 * include storage/sd/sd_mmc.c. */
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
    long cut_at;                               /* >0: a power cut once that many blocks are written (C.writes) */
    int cut;                                   /* the cut happened: the card answers nothing */
    uint32_t stall_until;                      /* busy until clk reaches this: a read's next token waits (a card's
                                                * 100 ms and more now and then); 0 never */
    int wmulti;                                /* CMD25 open: the blocks go on at waddr until 0xFD */
    uint32_t busy_until;                       /* a write's busy: DO low while selected until clk reaches it */
    uint32_t wbusy;                            /* ticks of busy after each CMD25 block (and its stop) */
    long wstall_at;                            /* >0: after that many CMD25 blocks the card is busy wstall ticks once */
    uint32_t wstall;
    long wblocks, cmd25s, wr_misuse;           /* CMD25 blocks written, CMD25s, tokens / commands sent while busy */
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
    if (clk < C.stall_until) {                 /* busy: no token yet */
        q(0xFF);
        return;
    }
    if (C.rsec >= card_sectors()) {            /* past the end: an error token, the stream over */
        q(0xFF); q(0x08);
        C.rs = 0;
        return;
    }
    q_block(C.rsec, C.nac_long && C.rsec % 3 == 0 ? 3 + (int)(C.rsec % 7) * 5 : 1 + (int)(C.rsec % 3));
    C.rsec++;
    if (C.rs == 2) C.rs = 0;                   /* (a CMD17 that waited out a stall: its one block) */
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
    case 24:
    case 25: {
        uint32_t s;
        if (C.idle) { q(0x05); return; }
        if (!C.hc && (arg & 511u)) { q(0x40); return; }   /* a byte address off a block */
        s = C.hc ? arg : arg / 512u;
        if (s >= card_sectors()) { q(0x40); return; }
        q(0);
        if (idx == 24 || idx == 25) { C.wstate = 1; C.waddr = s; C.wmulti = idx == 25; C.cmd25s += idx == 25; return; }
        if (C.no_token) return;
        if (idx == 18) { C.cmd18s++; C.rs = 1; C.rsec = s; return; }
        C.cmd17s++;
        if (clk < C.stall_until) { C.rs = 2; C.rsec = s; return; }   /* (busy: the block once the stall is over) */
        q_block(s, 2);
        return;
    }
    default:
        q(0x04);
    }
}

static void (*clk_hook)(void);                 /* called at every tick (a test's interrupt: tests/sd_stream_test.c's audio) */

static uint8_t xchg(uint8_t in)
{
    int rs0;
    clk++;
    if (clk_hook) clk_hook();
    if (!C.present)
        return 0xFF;
    if (!C.selected) {                         /* the LCD gets this byte */
        C.lcd_bytes++;
        if (in != 0xFF) C.lcd_bad++;
        return 0xFF;
    }
    if (clk < C.busy_until && C.outpos >= C.outlen) {   /* a write's busy: DO low; anything sent but 0xFF is misuse */
        if (in != 0xFF) C.wr_misuse++;
        return 0x00;
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
        if (C.wmulti && in == 0xFD) {          /* the multi-block write's stop: busy, then idle */
            C.wstate = 0; C.wmulti = 0;
            C.busy_until = clk + 2u + C.wbusy;
            return 0xFF;
        }
        if (in == (C.wmulti ? 0xFC : 0xFE)) { C.wstate = 2; C.wpos = 0; }
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
            if (C.cut_at > 0 && C.writes >= C.cut_at) {   /* the power goes before this block lands */
                C.cut = 1;
                C.present = 0;
                return 0xFF;
            }
            memcpy(p, C.wbuf, 512);
            C.writes++;
            if (C.wmulti) {                    /* accepted; busy for the block (once longer), then the next token */
                C.wblocks++;
                q(0xE5);
                C.busy_until = clk + 2u + C.wbusy + (C.wstall_at > 0 && C.wblocks == C.wstall_at ? C.wstall : 0u);
                C.wstate = 1;
                C.waddr++;
                return 0xFF;
            }
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
#define SD_DESEL() (C.selected = 0, C.cmdlen = 0, C.outlen = C.outpos = 0, C.rs = C.rs == 2 ? 0 : C.rs)
#define SD_FAST() ((void)0)
#define SD_SET_OVER(on) (t_over = (on))
#define SD_NOW() clk
#define SD_TICKS_MS 1000u

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


static int same_as_card(const uint8_t *buf, uint32_t s, uint32_t n)
{
    uint8_t scratch[512];
    for (; n; n--, s++, buf += 512)
        if (memcmp(buf, sector_ptr(s, scratch), 512)) return 0;
    return 1;
}

static void card_power_on(void)                /* the power back after a cut: a card in its native mode, idle */
{
    C.present = 1;
    C.cut = 0;
    C.cut_at = 0;
    C.selected = C.app = C.crc_on = C.hs = C.rs = C.wstate = 0;
    C.idle = 1;
    C.cmdlen = C.outlen = C.outpos = 0;
    C.acmd41_busy = 20;
    t_over = 0;
}
