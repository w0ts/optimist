/* SPDX-License-Identifier: GPL-3.0-only */
/* Recording to the SD card (FELUCCA_SD_REC; docs/SD-CARD-DESIGN.md 7): the master output, 16-bit stereo at 44.1 kHz
 * (176 KB/s), into a WAV on the card. The audio interrupt puts each block's frames in a ring (sdr_feed); the main loop
 * sends the ring's whole blocks to the card by the polled CMD25 writer (sd_mmc.c sd_wr_*), one block a call, never
 * waiting on the card's busy: the ring covers the card's stalls (FELUCCA_SD_REC_KB: 32 KB is 186 ms; real cards stall
 * 100-250 ms now and then). A frame the ring has no room for is dropped and counted (sdr.lost), never a click of
 * garbage. The file is one contiguous run (sd_alloc.c sda_create, SDA_RECORD: the asked length, else a minute), never
 * grown: a take that fills it stops there (FULL). Block 0 is the WAV's header (RIFF, fmt, a JUNK chunk to 512 B, data),
 * written first with no length and again at the end with the take's: the data starts at the file's byte 512, the
 * run's slack after it stays (the file's size on the card is the run's; RIFF's and data's sizes are the take's).
 * While a take writes (CMD25 open) the card takes no other command: FatFs' reads and writes are refused (sd_diskio.c:
 * RES_NOTRDY) and the sample streams wait (sd_stream.c).
 *   sdr_start(path, ms)   a new file (never over one), ms the length asked (0: a minute); 0 ok, else a FRESULT
 *   sdr_feed(q15, n)      (the audio interrupt, core/audio.c) n stereo frames, Q15 in int32 pairs
 *   sdr_stop()            the take ends: what the ring holds is written, the header made whole, the file closed
 *   sdr_service()         (the main loop) a block to the card when it takes one; 1 = it did something
 *   sdr_state             RC_OFF, RC_REC, RC_STOP (writing out), RC_DONE, RC_FULL (the run ended the take),
 *                         RC_ERR (the card failed: the take kept up to its last header, if any) */
#ifndef FELUCCA_SD_REC_KB
#define FELUCCA_SD_REC_KB 32                            /* (the builder's SD_REC: registry.h) */
#endif
#define SDR_BLKS (FELUCCA_SD_REC_KB * 2u)               /* the ring's blocks (512 B) */
#define SDR_RATE 44100u
#define SDR_FB 4u                                       /* bytes a frame: 16-bit stereo */
enum { RC_OFF = 0, RC_REC, RC_STOP, RC_DONE, RC_FULL, RC_ERR };

static uint8_t sdr_ring[SDR_BLKS * 512u] __attribute__((aligned(4))) IN_POOL;
static uint8_t sdr_hdr[512] __attribute__((aligned(4)));
static FIL sdr_f;
static struct {
    volatile uint32_t w;                                /* bytes the interrupt put in (a count, the ring's place mod) */
    uint32_t r;                                         /* blocks sent to the card */
    volatile uint32_t lost;                             /* frames dropped: the ring full */
    uint32_t lba, run;                                  /* the file's first sector, its run in blocks */
    uint32_t sent;                                      /* blocks sent, the header's included */
    uint32_t lag_max;                                   /* the ring's most blocks waiting (how near a loss came) */
    volatile uint8_t state;
    uint8_t hdr_due;                                    /* the header's block goes first */
    uint8_t tail_sent;                                  /* the last partial block sent (zeros after the frames) */
    uint8_t full;                                       /* the run ended the take */
    uint8_t news;                                       /* (the UI's, once: 1 the run ended a take, 2 the card failed) */
} sdr;
#define sdr_state sdr.state

static void sdr_le16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void sdr_le32(uint8_t *p, uint32_t v) { sdr_le16(p, v); sdr_le16(p + 2, v >> 16); }

static void sdr_header(uint32_t data)                   /* the 512 B header, data the take's bytes */
{
    uint8_t *h = sdr_hdr;
    memset(h, 0, 512);
    memcpy(h, "RIFF", 4);
    sdr_le32(h + 4, 504u + data);                       /* (the file's bytes after this field: 512 - 8 + data) */
    memcpy(h + 8, "WAVEfmt ", 8);
    sdr_le32(h + 16, 16);
    sdr_le16(h + 20, 1);                                /* PCM */
    sdr_le16(h + 22, 2);
    sdr_le32(h + 24, SDR_RATE);
    sdr_le32(h + 28, SDR_RATE * SDR_FB);
    sdr_le16(h + 32, SDR_FB);
    sdr_le16(h + 34, 16);
    memcpy(h + 36, "JUNK", 4);
    sdr_le32(h + 40, 504u - 44u);                       /* (its payload to byte 504: the data's header at 504..511) */
    memcpy(h + 504, "data", 4);
    sdr_le32(h + 508, data);
}

static int sdr_busy(void) { return sdr.state == RC_REC || sdr.state == RC_STOP; }

static FRESULT sdr_start(const char *path, uint32_t ms)
{
    FRESULT r;
    FATFS *fs;
    if (sdr_busy())
        return FR_LOCKED;
    r = sda_create(&sdr_f, path, SDA_RECORD, 512u, 512u + sda_rec_bytes(ms, 0, SDR_RATE, SDR_FB));
    if (r != FR_OK)
        return r;
    fs = sdr_f.obj.fs;
    sdr.lba = fs->database + (sdr_f.obj.sclust - 2u) * fs->csize;   /* (one run: f_expand's) */
    sdr.run = (uint32_t)(sdr_f.obj.objsize / 512u);
    sdr_header(0);
    SD_SESSION();
    r = sd_wr_open(sdr.lba) == RES_OK ? FR_OK : FR_DISK_ERR;
    SD_SESSION_END();
    if (r != FR_OK) {
        (void)f_close(&sdr_f);
        sdr.state = RC_ERR;
        return r;
    }
    sdr.r = sdr.sent = sdr.lost = sdr.lag_max = 0;
    sdr.w = 0;
    sdr.hdr_due = 1;
    sdr.tail_sent = sdr.full = 0;
    sdr.state = RC_REC;                                /* (the interrupt feeds from here) */
    return FR_OK;
}

static void sdr_feed(const int32_t *q15, uint32_t n)
{
    uint32_t w = sdr.w, i, room;
    if (sdr.state != RC_REC)
        return;
    room = SDR_BLKS * 512u - (w - sdr.r * 512u);
    if (n * SDR_FB > room) {                            /* the card behind by the whole ring: these frames dropped */
        sdr.lost += n;
        return;
    }
    for (i = 0; i < 2u * n; i++) {                      /* (Q15 at the DAC's scale: saturated to 16 bits) */
        int32_t v = q15[i];
        uint8_t *p = sdr_ring + (w % (SDR_BLKS * 512u));
        v = v > 32767 ? 32767 : v < -32768 ? -32768 : v;
        p[0] = (uint8_t)v;
        p[1] = (uint8_t)((uint32_t)v >> 8);
        w += 2u;
    }
    sdr.w = w;
}

static void sdr_stop(void)
{
    if (sdr.state == RC_REC)
        sdr.state = RC_STOP;
}

static void sdr_fail(void)
{
    SD_SESSION();
    sd_wr_abort();
    SD_SESSION_END();
    (void)f_close(&sdr_f);
    sdr.state = RC_ERR;
    sdr.news = 2;
}

static void sdr_finish(uint8_t end)                     /* the write stopped: the header with the take's length */
{
    uint32_t data = (sdr.sent - 1u) * 512u;             /* (the blocks after the header; the last one's zeros out) */
    uint32_t bytes = sdr.w, keep = bytes < data ? bytes : data;
    DRESULT r;
    sdr_header(keep);
    SD_SESSION();
    r = disk_writeblock(sdr_hdr, sdr.lba);
    SD_SESSION_END();
    (void)f_close(&sdr_f);
    sdr.state = r == RES_OK ? end : RC_ERR;
    sdr.news = sdr.state == RC_ERR ? 2u : end == RC_FULL ? 1u : 0u;
}

static int sdr_service(void)
{
    uint32_t have, lag;
    const uint8_t *b;
    BYTE st;
    if (!sdr_busy())
        return 0;
    SD_SESSION();
    st = sd_wr_poll();
    SD_SESSION_END();
    if (st == SDW_ERR) {
        sdr_fail();
        return 1;
    }
    if (st == SDW_IDLE) {                               /* the stop programmed: the header with the length */
        sdr_finish(sdr.full ? RC_FULL : RC_DONE);
        return 1;
    }
    if (st != SDW_READY)
        return 0;
    have = sdr.tail_sent ? 0u : sdr.w - sdr.r * 512u;
    lag = have / 512u;
    if (lag > sdr.lag_max)
        sdr.lag_max = lag;
    if (sdr.hdr_due) {
        b = sdr_hdr;
    } else if (sdr.sent >= sdr.run || (sdr.state == RC_STOP && have == 0)) {
        sdr.full = sdr.sent >= sdr.run && sdr.state == RC_REC;   /* (the run full: the take ends here) */
        sdr.state = RC_STOP;
        SD_SESSION();
        st = sd_wr_stop();
        SD_SESSION_END();
        if (st == SDW_ERR)
            sdr_fail();
        return 1;
    } else if (have >= 512u) {
        b = sdr_ring + (sdr.r % SDR_BLKS) * 512u;
    } else if (sdr.state == RC_STOP) {                 /* the last frames: a block with zeros after them */
        uint8_t *t = sdr_ring + (sdr.r % SDR_BLKS) * 512u;
        memset(t + have, 0, 512u - have);
        b = t;
        sdr.tail_sent = 1;
    } else {
        return 0;
    }
    SD_SESSION();
    st = sd_wr_block(b);
    SD_SESSION_END();
    if (st == SDW_ERR) {
        sdr_fail();
        return 1;
    }
    if (sdr.hdr_due)
        sdr.hdr_due = 0;
    else
        sdr.r++;
    sdr.sent++;
    return 1;
}

#ifdef SDL_ROOT                                         /* (the card's layout: sd_layout.c; the device's build) */
/* a take into OPTIMIST/RECORD/REC-nnnn.WAV, the first free name (a minute's run, or ms); its name in sdr_file */
static char sdr_file[16];
static FRESULT sdr_start_next(uint32_t ms)
{
    char path[48];
    FRESULT r = FR_EXIST;
    uint32_t k;
    if (sdr_busy())
        return FR_LOCKED;
    if (!sdl_path(path, sizeof path, SDL_RECORD, 0))
        return FR_INVALID_NAME;
    (void)f_mkdir(path);
    for (k = 1; k < 10000u && r == FR_EXIST; k++) {
        str_cpy(sdr_file, "REC-0000.WAV", sizeof sdr_file);
        sdr_file[4] = (char)('0' + k / 1000u), sdr_file[5] = (char)('0' + k / 100u % 10u);
        sdr_file[6] = (char)('0' + k / 10u % 10u), sdr_file[7] = (char)('0' + k % 10u);
        if (!sdl_path(path, sizeof path, SDL_RECORD, sdr_file))
            return FR_INVALID_NAME;
        r = sdr_start(path, ms);
    }
    return r;
}
/* REC tapped then held (op_input.c), the console: a take started (1) or stopped (0); -1 refused (no card, a failure) */
static int sdr_toggle(void)
{
    if (sdr_busy()) {
        sdr_stop();
        return 0;
    }
    return sdr_start_next(0) == FR_OK ? 1 : -1;      /* (no card, none mounted: FatFs says so) */
}
static const char *sdr_name(void) { return sdr_file; }
static int sdr_news(void)                               /* once: 1 a take ended by its run, 2 the card failed; 0 none */
{
    int n = sdr.news;
    sdr.news = 0;
    return n;
}
#endif
