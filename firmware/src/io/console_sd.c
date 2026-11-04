/* SPDX-License-Identifier: GPL-3.0-only */
/* sd: the SD card on the LCD's bus (storage/sd/sdcard.c; docs/SD-CARD-DESIGN.md "Bring-up"). Included by console.c
 * (console builds with SDCARD), main loop only:
 *   sd                    mount: card type, size, filesystem; the read mode that found the card; the DMA probe
 *   sd mode duplex|dirrx|bitbang   how SPI1 reads bytes (hal/fm1_sd_bus.h), then sd again
 *   sd dma on|off         block data by SPI1's DMA (sd sets it from its probe)
 *   sd clock 20|30|auto   the card's clock (auto: the fastest the card proves at the mount), then the mount again
 *   sd raw SECTOR         the sector's first 64 bytes, no filesystem (sector 0: the MBR or boot record, 55 AA at 510)
 *   sd ls [DIR]           the directory's entries
 *   sd cat FILE           the file's first 64 bytes
 *   sd bench              the read modes compared on 64 blocks of the card (CON_SD_N): one line each
 *   sd bench FILE         the whole file read: through the filesystem (512 B a call), then a run per CMD18
 *   sd write FILE         a pattern written over the file (its size, up to 64 KB) block by block (CMD24, the data by DMA
 *                         when sd dma is on), read back and compared: KB/s
 *   sd stream             the streams (SD_RING builds, storage/sd/sd_stream.c): the rings, the pre-buffer, the plan
 *                         (placed / refused: its need, what would make room), each source, each stream's fill, and since
 *                         the last reset: dry voices, notes with no ring, runs, blocks, KB/s, stalls, CRC retries,
 *                         errors, the longest run (the screen's longest wait for the card), the least lead a run began at
 *   sd stream reset       those counts from 0 (and the keys' ui gap: console keys)
 *   sd stream pre MS      the pre-buffer: 25, 50, 100 or 200 ms (placed again)
 *   sd stream part P FILE part P's CARD sample (1..3): FILE from OPTIMIST/ ("SAMPLES/kick.wav"), by its hash in the
 *                         index (until the sample picker sets it); no FILE: none
 *   sd diag               CMD0 and CMD8 by hand in every read mode, the bytes that came back and PC8's level (the line
 *                         when nothing mounts)
 *   sd pins [7|8|9|10]    the pin finder (bring-up, before the SD board: a breakout on J12, a multimeter): each of the
 *                         LCD's bus lines PC7 CS, PC8 D/C, PC9 CLK, PC10 MOSI (or the one named) held HIGH alone for
 *                         CON_SD_PIN_S seconds, the other three LOW, SPI1 off its pins; a dot a second meanwhile. The
 *                         screen does not draw while it holds (the main loop is here; the audio and USB run on, their
 *                         interrupts), then the LCD is started again and redrawn whole */
#define CON_SD_N 64u                                 /* blocks a bench row reads (32 KB) */
#define CON_SD_PIN_S 15u                             /* the pin finder: seconds a line is held */

static void con_sd_pins(uint32_t only)               /* only: 7..10 one line, else all four in turn */
{
    static const char *const NAME[4] = {"PC7 = LCD CS (chip pin 32)", "PC8 = LCD D/C, the card's DO (chip pin 31)",
                                        "PC9 = CLK (chip pin 30)", "PC10 = MOSI (chip pin 29)"};
    uint32_t k, t0, s;
    lcd_sync();
    fm1_sd_pins_begin();                             /* (hal/fm1_sd_bus.h: SPI1 off, all four driven low) */
    for (k = 0; k < 4u; k++) {
        if (only >= 7u && only <= 10u && k != only - 7u)
            continue;
        fm1_sd_pin_high(k);
        con_puts("HIGH now: ");
        con_puts(NAME[k]);
        con_puts("; the other three low. Meter J12's legs to GND: the one at 3.3 V is this line\r\n");
        for (s = 0; s < CON_SD_PIN_S; s++) {
            for (t0 = fm1_ms; fm1_ms - t0 < 1000u;)
                fm1_wdt_feed();
            con_putc('.');                           /* (the console tool listens while it hears something) */
        }
        con_puts("\r\n");
    }
    fm1_sd_pins_end();
    lcd_init();                                      /* (the panel saw stray levels: started again, the pins back) */
    ui.force = 1;
    con_puts("sd pins: done, the screen back\r\n");
}

/* sd diag: the card's first commands by hand, in every read mode at the start-up clock, to see what the line does when
 * nothing mounts (bring-up, 2026-11-04): per mode the level of PC8 (the card's DO) with the card off the bus and
 * selected, the idle bytes read, then CMD0 (40 00 00 00 00 95) and CMD8 (48 00 00 01 AA 87) with 16 bytes read after
 * each: a card in SPI mode answers 01 to CMD0 and 01 00 00 01 AA after CMD8; FF all along is a line stuck high (no
 * card, no power, CS or clock not reaching it), 00 all along stuck low */
static void con_sd_dbytes(const char *tag, const uint8_t *b, uint32_t n)
{
    uint32_t i;
    con_puts(tag);
    for (i = 0; i < n; i++) {
        con_putc(' ');
        con_hex(b[i], 2);
    }
    con_puts("\r\n");
}

static void con_sd_cmd_row(const char *tag, const uint8_t *pk)
{
    uint8_t r[16];
    uint32_t i;
    fm1_sd_desel();
    (void)fm1_sd_rx();
    fm1_sd_sel();
    (void)fm1_sd_rx();
    for (i = 0; i < 6u; i++)
        fm1_sd_tx(pk[i]);
    for (i = 0; i < 16u; i++)
        r[i] = fm1_sd_rx();
    con_sd_dbytes(tag, r, 16u);
}

static uint8_t con_sd_r1(const uint8_t *pk)         /* a command, its R1 (the first byte without the top bit; 0xFF none) */
{
    uint32_t i;
    uint8_t r = 0xFFu;
    fm1_sd_desel();
    (void)fm1_sd_rx();
    fm1_sd_sel();
    (void)fm1_sd_rx();
    for (i = 0; i < 6u; i++)
        fm1_sd_tx(pk[i]);
    for (i = 0; i < 10u; i++) {
        r = fm1_sd_rx();
        if (!(r & 0x80u))
            break;
    }
    return r;
}

/* the start-up as sd_card_init runs it after CMD8: CMD55 + ACMD41 (HCS) until the card leaves idle (1 s), then CMD58 */
static void con_sd_init_row(void)
{
    static const uint8_t C55[6] = {0x77, 0, 0, 0, 0, 0x65}, A41[6] = {0x69, 0x40, 0, 0, 0, 0x77},
                         C58[6] = {0x7A, 0, 0, 0, 0, 0xFD};
    uint32_t t0 = fm1_ms, n = 0, i;
    uint8_t r55, r41, ocr[4];
    do {
        r55 = con_sd_r1(C55);
        r41 = con_sd_r1(A41);
        n++;
        fm1_wdt_feed();
    } while (r41 != 0 && fm1_ms - t0 < 1000u);
    con_puts("  ACMD41 loop: tries ");
    con_hex(n, 4);
    con_puts(" CMD55 R1 ");
    con_hex(r55, 2);
    con_puts(" ACMD41 R1 ");
    con_hex(r41, 2);
    con_kv(" ms", (int32_t)(fm1_ms - t0));
    if (r41 == 0) {
        (void)con_sd_r1(C58);
        for (i = 0; i < 4u; i++)
            ocr[i] = fm1_sd_rx();
        con_sd_dbytes("  OCR ", ocr, 4u);
    }
}

/* sd speed: the card's CSD read at each SPI clock in the current read mode, to find the clock this wiring reads at
 * (the card's start-up already works at 400 kHz): per clock the R1, the data token (FE: right) and the CSD's first bytes
 * (a v2 card starts 40 0E 00 32 ..) */
static void con_sd_speed(void)
{
    static const uint8_t NB[] = {149, 59, 29, 19, 14, 9, 7, 5, 4, 3, 2, 1};
    uint32_t k, i;
    lcd_sync();
    fm1_sd_bus_begin(FM1_SD_BAUD_SLOW);
    con_puts("sd speed: mode ");
    con_hex(fm1_sd_mode, 1);
    con_puts(" init status ");
    con_hex(sd_card_init(), 2);
    con_puts("\r\n");
    for (k = 0; k < sizeof NB; k++) {
        uint8_t csd[16], r1, tok;
        fm1_sd_baud(NB[k]);
        r1 = send_cmd(CMD9, 0);
        tok = wait_token(100);
        for (i = 0; i < 16u; i++)
            csd[i] = fm1_sd_rx();
        release_spi();
        con_puts("  kHz ");
        con_hex((uint32_t)(60000u / (NB[k] + 1u)), 5);
        con_puts("  R1 ");
        con_hex(r1, 2);
        con_puts(" token ");
        con_hex(tok, 2);
        con_sd_dbytes(" csd", csd, 6u);
        fm1_wdt_feed();
    }
    fm1_sd_bus_end();
    lcd_init();
    ui.force = 1;
    con_puts("sd speed: done, the screen back\r\n");
}

static void con_sd_diag(void)
{
    static const char *const MN[3] = {"duplex", "dirrx", "bitbang"};
    static const uint8_t DG_CMD0[6] = {0x40, 0, 0, 0, 0, 0x95}, DG_CMD8[6] = {0x48, 0, 0, 0x01, 0xAA, 0x87};
    const uint8_t keep = fm1_sd_mode;
    uint32_t m, i;
    lcd_sync();
    for (m = 0; m < 3u; m++) {
        uint8_t idle[8];
        uint32_t off, on;
        fm1_sd_mode = (uint8_t)m;
        fm1_sd_bus_begin(FM1_SD_BAUD_SLOW);
        fm1_sd_desel();
        for (i = 0; i < 10u; i++)
            (void)fm1_sd_rx();
        off = fm1_sd_do_level();
        fm1_sd_sel();
        on = fm1_sd_do_level();
        for (i = 0; i < 8u; i++)
            idle[i] = fm1_sd_rx();
        con_puts("sd diag ");
        con_puts(MN[m]);
        con_kv("  PC8 off bus", (int32_t)off);
        con_kv("  PC8 selected", (int32_t)on);
        con_puts("\r\n");
        con_sd_dbytes("  idle ", idle, 8u);
        con_sd_cmd_row("  CMD0 ", DG_CMD0);
        con_sd_cmd_row("  CMD8 ", DG_CMD8);
        con_sd_init_row();
        fm1_sd_bus_end();
        fm1_sd_bus_begin(FM1_SD_BAUD_SLOW);              /* (the driver's own commands, with its own CRC7) */
        fm1_sd_desel();
        for (i = 0; i < 10u; i++)
            (void)fm1_sd_rx();
        con_puts("  driver CMD0 R1 ");
        con_hex(send_cmd(CMD0, 0), 2);
        con_puts(" CMD8 R1 ");
        con_hex(send_cmd(CMD8, 0x1AA), 2);
        con_puts("\r\n");
        fm1_sd_bus_end();
        fm1_sd_bus_begin(FM1_SD_BAUD_SLOW);
        con_puts("  sd_card_init status ");
        con_hex(sd_card_init(), 2);
        con_kv(" card type", (int32_t)CardType);
        con_kv("  sectors", (int32_t)sd_sectors);
        fm1_sd_bus_end();
        fm1_wdt_feed();
    }
    fm1_sd_mode = keep;
    lcd_init();
    ui.force = 1;
    con_puts("sd diag: done, the screen back\r\n");
}

static void con_sd_field(const char *k, int32_t v)   /* " key value" */
{
    con_putc(' ');
    con_puts(k);
    con_putc(' ');
    con_dec(v);
}

static void con_sd_mount(void)
{
    static const char *const ST[] = {"unmounted", "no card", "no FAT16 / FAT32 filesystem", "ready"};
    static const char *const MODE[] = {"duplex", "dirrx", "bitbang"};
    uint32_t st = sd_mount(), t = sd_card_type();
    con_puts("sd ");
    con_puts(ST[st]);
    if (t) {
        con_puts(t & CT_MMC ? "  MMC" : t & CT_BLOCK ? "  SDHC" : t & CT_SD2 ? "  SDSC v2" : "  SDSC v1");
        con_kv("  MB", (int32_t)sd_card_mb());
        con_puts("read mode ");
        con_puts(MODE[fm1_sd_mode]);
        con_puts(fm1_sd_dma ? "  dma ok" : "  dma off");
        con_sd_field("(probe: bytes crc", (int32_t)(sd_probe_seen & 1u));
        con_sd_field("dma crc", (int32_t)(sd_probe_seen >> 1 & 1u));
        con_sd_field("same", (int32_t)(sd_probe_seen >> 2 & 1u));
        con_puts(")\r\n");
        con_puts(sd_over ? "clock 7.5 MHz" : "clock 6 MHz");
        if (!sd_over && sd_clock_pick != 20u) {
            con_sd_field("(30 failed: blocks", (int32_t)sd_clock_bad);
            con_sd_field("of", (int32_t)SD_CLOCK_BLOCKS);
            con_putc(')');
        }
        con_puts(sd_clock_pick ? "  (set)" : "  (auto)");
        con_sd_field(" crc fails", (int32_t)sd_crc_fails);
        con_sd_field("drops to 20", (int32_t)sd_demotions);
        con_puts("\r\n");
    } else {
        con_puts("\r\n");
    }
    if (st == SD_READY) {
        con_kv("fat", (int32_t)sd_fat_bits());
        con_kv("cluster B", (int32_t)sd_cluster_bytes());
    }
}

static void con_sd_ls(const char *path)
{
    DIR d;
    FILINFO fi;
    if (sd_opendir(&d, path) != FR_OK) {
        con_puts("sd: no such directory (mount first: sd)\r\n");
        return;
    }
    while (sd_readdir(&d, &fi) == FR_OK && fi.fname[0]) {
        con_puts(fi.fattrib & AM_DIR ? "  <dir> " : "        ");
        con_puts(fi.fname);
        if (!(fi.fattrib & AM_DIR))
            con_kv("", (int32_t)fi.fsize);
        else
            con_puts("\r\n");
    }
}

static int32_t con_sd_kbs(uint32_t bytes, uint32_t ticks)   /* bytes a millisecond (KB/s, 1000 B) */
{
    return ticks ? (int32_t)((uint64_t)bytes * 1000u * FM1_TICKS_PER_US / ticks) : 0;
}

/* One bench row: CON_SD_N blocks from sector s, by CMD18 (multi) or a CMD17 each, through the polled reader. cpu = the
 * ticks inside the driver's calls (the commands, the token polls, the byte transfers, the DMA's start and end, the CRC
 * bytes); the DMA's own time is spent spinning outside them (what the main loop would have for other work). The CRC16
 * of each block is checked after it has been timed. Prints:
 *   NAME bB: KB/s N cpu_ns/blk N us/blk N free_us/blk N crc_bad N/64 sum XXXX */
static void con_sd_row(const char *name, uint32_t mode, uint32_t dma, uint32_t baud, uint32_t multi, uint32_t s)
{
    uint32_t m0 = fm1_sd_mode, d0 = fm1_sd_dma, i = 0, j, k, st, t, t0, cpu = 0, idle = 0, chk = 0, bad = 0, sum = 0;
    uint32_t ok = 1;
    fm1_sd_mode = (uint8_t)mode;
    fm1_sd_dma = (uint8_t)dma;
    sd_begin(baud);
    t0 = fm1_ticks();
    while (i < CON_SD_N && ok) {
        k = multi ? CON_SD_N - i : 1u;
        t = fm1_ticks();
        ok = sd_rd_open(s + i, k) == RES_OK;
        cpu += fm1_ticks() - t;
        for (j = 0; j < k && ok; j++, i++) {
            t = fm1_ticks();
            sd_rd_next(sd_blk);
            st = sd_rd_poll();
            cpu += fm1_ticks() - t;
            while (st == SDR_BUSY) {
                if (sd_rd.state == SDR_DMA) {        /* the DMA runs: the CPU's own time */
                    t = fm1_ticks();
                    while (!fm1_sd_rx_dma_done() && fm1_ticks() - t < 10u * 1000u * FM1_TICKS_PER_US)
                        ;
                    idle += fm1_ticks() - t;
                }
                t = fm1_ticks();
                st = sd_rd_poll();
                cpu += fm1_ticks() - t;
            }
            ok = st == SDR_READY;
            t = fm1_ticks();
            if (ok) {
                bad += sd_crc16(sd_blk, 512) != sd_rd.crc;
                sum = sum * 31u + sd_rd.crc;
            }
            chk += fm1_ticks() - t;                  /* (not the reader's time) */
        }
        t = fm1_ticks();
        ok = sd_rd_close() == RES_OK && ok;
        cpu += fm1_ticks() - t;
        fm1_wdt_feed();
    }
    t = fm1_ticks() - t0 - chk;
    sd_end();
    fm1_sd_mode = (uint8_t)m0;
    fm1_sd_dma = (uint8_t)d0;
    con_puts(name);
    con_puts(baud == FM1_SD_BAUD_FAST ? " 6MHz" : baud == FM1_SD_BAUD_OVER ? " 7.5MHz" : " bb");
    con_puts(multi ? " cmd18:" : " cmd17:");
    if (!i) {
        con_puts(" failed (no block read)\r\n");
        return;
    }
    con_sd_field("KB/s", con_sd_kbs(i * 512u, t));
    con_sd_field("cpu_ns/blk", (int32_t)((uint64_t)cpu * 1000u / FM1_TICKS_PER_US / i));
    con_sd_field("us/blk", (int32_t)(t / FM1_TICKS_PER_US / i));
    con_sd_field("free_us/blk", (int32_t)(idle / FM1_TICKS_PER_US / i));
    con_sd_field("crc_bad", (int32_t)bad);
    con_putc('/');
    con_dec((int32_t)i);
    con_puts(i < CON_SD_N || !ok ? " ERR" : "");
    con_puts(" sum ");
    con_hex(sum, 8);
    con_puts("\r\n");
}

static void con_sd_bench_modes(void)
{
    static const uint8_t BAUD[2] = {FM1_SD_BAUD_FAST, FM1_SD_BAUD_OVER};
    uint32_t s = sd_state == SD_READY ? sd_data_sector() : 0, b, cm = fm1_sd_mode == FM1_SD_BITBANG ? FM1_SD_DUPLEX
                                                                                                  : fm1_sd_mode;
    if (!sd_card_type()) {
        con_puts("sd: no card (mount first: sd)\r\n");
        return;
    }
    uint32_t over = sd_over;
    con_kv("sd bench: blocks from sector", (int32_t)s);
    for (b = 0; b < 2u; b++) {
        con_sd_row("duplex", FM1_SD_DUPLEX, 0, BAUD[b], 1, s);
        con_sd_row("dirrx ", FM1_SD_DIRRX, 0, BAUD[b], 1, s);
        con_sd_row("dma   ", cm, 1, BAUD[b], 1, s);
        con_sd_row("dma   ", cm, 1, BAUD[b], 0, s);
    }
    con_sd_row("bitbng", FM1_SD_BITBANG, 0, FM1_SD_BAUD_FAST, 1, s);
    sd_over = (uint8_t)over;                         /* (the rows set their own clocks: the session's back) */
    con_puts("(the same sum on every row = the same data; crc_bad 0 = read right)\r\n");
}

static void con_sd_stream(uint32_t size)             /* the open file a run per CMD18, by the set mode */
{
    uint32_t total = 0, n, j, sec, t0 = fm1_ticks(), bad = 0, st;
    while ((n = sd_file_run(&sec)) != 0) {
        sd_begin(SD_BAUD());
        if (sd_rd_open(sec, n) != RES_OK) {
            sd_end();
            break;
        }
        for (j = 0; j < n; j++) {
            sd_rd_next(sd_blk);
            while ((st = sd_rd_poll()) == SDR_BUSY)
                ;
            if (st != SDR_READY)
                break;
        }
        bad += sd_rd_close() != RES_OK || j < n;
        sd_end();
        total += n * 512u;
        fm1_wdt_feed();
        if (bad || sd_seek(total) != FR_OK)
            break;
    }
    con_kv("stream B", (int32_t)(total < size ? total : size));
    con_kv("stream KB/s", con_sd_kbs(total, fm1_ticks() - t0));
    con_kv("stream errors", (int32_t)bad);
}

static void con_sd_write(const char *path, uint32_t n)   /* the pattern by whole blocks over the file's sectors */
{
    uint32_t total = 0, got, bad = 0, sec, k, j, i, t0 = fm1_ticks(), err = 0;
    while (total < n && (k = sd_file_run(&sec)) != 0) {
        sd_begin(SD_BAUD());
        for (j = 0; j < k && total < n; j++, total += 512u) {
            for (i = 0; i < 512u; i++)
                sd_blk[i] = (uint8_t)((total + i) * 13u + 5u);
            if (disk_writeblock(sd_blk, sec + j) != RES_OK) {
                err = 1;
                break;
            }
        }
        sd_end();
        fm1_wdt_feed();
        if (err || sd_seek(total) != FR_OK)
            break;
    }
    con_kv("written B", (int32_t)total);
    con_kv("write KB/s", con_sd_kbs(total, fm1_ticks() - t0));
    sd_open(path);
    for (total = 0; total < n; total += got) {
        if (sd_read(sd_blk, 512u, &got) != FR_OK || !got)
            break;
        for (i = 0; i < got; i++)
            bad += sd_blk[i] != (uint8_t)((total + i) * 13u + 5u);
    }
    con_kv("read back B", (int32_t)total);
    con_kv("wrong B", (int32_t)bad);
}

static void con_sd_rw(const char *path, uint32_t what)   /* 0 cat, 1 bench, 2 write */
{
    uint32_t got, total = 0, t0, n;
    if (sd_open(path) != FR_OK) {
        con_puts("sd: no such file (mount first: sd)\r\n");
        return;
    }
    if (what == 0) {
        sd_read(sd_blk, 64, &got);
        con_bytes(path, sd_blk, got);
        return;
    }
    if (what == 2) {
        n = sd_file_size() > 65536u ? 65536u : sd_file_size() & ~511u;   /* (whole blocks only) */
        con_sd_write(path, n);
        return;
    }
    n = sd_file_size();
    t0 = fm1_ticks();
    for (;;) {
        if (sd_read(sd_blk, 512u, &got) != FR_OK || !got)
            break;
        total += got;
        if (!(total & 0x7FFFu))
            fm1_wdt_feed();
    }
    con_kv("read B", (int32_t)total);
    con_kv("read KB/s", con_sd_kbs(total, fm1_ticks() - t0));
    if (sd_seek(0) == FR_OK)
        con_sd_stream(n);
}

#if FELUCCA_SD_STREAM
static void con_sd_stream_part(const char *p)
{
    int ok;
    uint32_t part = con_num(&p, &ok);
    while (*p == ' ')
        p++;
    if (!ok || part < 1u || part > SST_NPART) {
        con_puts("sd stream part: 1..3 FILE\r\n");
        return;
    }
    if (!*p) {
        sst_part_set(part - 1u, 0);
        return;
    }
    if (sdx_open() != FR_OK || sdx_find_path(p, &sdx.rec) < 0) {
        con_puts("sd stream part: not in the index (SYSTEM/INDEX.BIN; the sample index pass first)\r\n");
        return;
    }
    sst_part_set(part - 1u, sdx.rec.hash);
    con_kv("part", (int32_t)part);
    con_puts(" hash ");
    con_hex((uint32_t)(sdx.rec.hash >> 32), 8);
    con_hex((uint32_t)sdx.rec.hash, 8);
    con_puts("\r\n");
}

static void con_sd_streams(void)
{
    uint32_t i, on = 0;
    con_kv("rings", SST_N);
    con_kv("ring KB", SST_K / 2u);
    con_kv("run blocks", SST_RUN);
    con_kv("pre ms", sst_pre_ms);
    con_kv("plan", sstp.state);                      /* 0 none, 1 letting go, 2 placed, 3 refused */
    con_kv("need B", (int32_t)sstp.need);
    if (sstp.state == SSTP_REFUSED) {
        con_kv("drop one of parts", sstp.drop);      /* (a mask: any one of those parts' memory would make room) */
        con_kv("drop all parts fits", sstp.drop_all);
        con_kv("fits at ms", sstp.fit_ms);
    }
    con_kv("missing parts", sstp.missing);
    for (i = 0; i < SST_NSRC; i++)
        if (sst_src[i].ready) {
            con_puts("src ");
            con_dec((int32_t)i);
            con_sd_field("rate", (int32_t)((uint64_t)sst_src[i].rate * 44100u >> 16));
            con_sd_field("bits", sst_src[i].bits == SST_F16 ? 16 : 8);
            con_sd_field("ch", sst_src[i].chans);
            con_sd_field("frames", (int32_t)sst_src[i].frames);
            con_sd_field("head_B", (int32_t)sst_src[i].hlen);
            con_sd_field("runs", sst_src[i].nrun);
            con_puts("\r\n");
        }
    for (i = 0; i < SST_N; i++)
        if (sst[i].on) {
            on++;
            con_puts("stream ");
            con_dec((int32_t)i);
            con_sd_field("src", sst[i].src);
            con_sd_field("fill", (int32_t)(sst[i].wr - sst[i].rd));
            con_putc('/');
            con_dec((int32_t)SST_K);
            con_sd_field("B/ms", (int32_t)sst[i].bpms);
            con_puts("\r\n");
        }
    con_kv("playing", (int32_t)on);
    con_kv("dry", (int32_t)sst_dry);
    con_kv("no ring", (int32_t)sst_nostream);
    con_kv("runs", (int32_t)sst_st.runs);
    con_kv("blocks", (int32_t)sst_st.blocks);
    con_kv("KB/s", (int32_t)sst_kbs());
    con_kv("stalls", (int32_t)sst_st.stalls);
    con_kv("crc retries", (int32_t)sst_st.crc);
    con_kv("errors", (int32_t)sst_st.errors);
    con_kv("taken back", (int32_t)sst_st.reaped);
    con_kv("given up (new note)", (int32_t)sst_st.aborted);
    con_kv("longest run us", (int32_t)sst_st.run_max_us);
    con_kv("least lead ms", sst_st.lead_min_ms == 0xFFFFFFFFu ? -1 : (int32_t)sst_st.lead_min_ms);
    con_kv("ui gap max ms", (int32_t)fm1_in.ui_gap_max);
    for (i = 0; i < SST_NPART; i++)                  /* the caches (GRAIN, STRETCH on a card set: sd_stream_rc.h) */
        if (sst_rc_live(&sst_rc[i])) {
            con_puts("  cache part ");
            con_dec((int32_t)i + 1);
            con_sd_field("KB", (int32_t)sst_rc[i].nk / 2);
            con_sd_field("at once", (int32_t)sst_rc[i].hit);
            con_sd_field("late", (int32_t)sst_rc[i].late);
            con_sd_field("missed", (int32_t)sst_rc[i].miss);
            con_puts("\r\n");
        }
    con_kv("cache runs", (int32_t)sst_rcs.runs);
    con_kv("cache blocks", (int32_t)sst_rcs.blocks);
    con_kv("cache idle runs", (int32_t)sst_rcs.cold_runs);
}

static void con_sd_stream_cmd(const char *p)
{
    int ok;
    if (con_word(&p, "reset")) {
        sst_stats_reset();
        fm1_in.ui_gap_max = 0;
    } else if (con_word(&p, "pre")) {
        uint32_t ms = con_num(&p, &ok);
        sst_pre_set(ok ? ms : 50u);
        con_kv("pre ms", sst_pre_ms);
        for (ms = 0; ms < 4u; ms++) {                /* (what each setting would take with the project's samples) */
            con_puts("  ");
            con_dec(SST_PRE_MS[ms]);
            con_kv(" ms KB", (int32_t)sst_pre_kb(SST_PRE_MS[ms]));
        }
    } else if (con_word(&p, "part")) {
        con_sd_stream_part(p);
    } else {
        con_sd_streams();
    }
}
#endif

#if FELUCCA_SD_REC
/* sd rec [ms]: a take of the master into OPTIMIST/RECORD/REC-nnnn.WAV (the first free name; ms: its run, else a
 * minute); sd rec stop; sd rec: the state, the blocks written, the frames lost, the ring's most blocks waiting */
static void con_sd_rec(const char *p)
{
    static const char *const ST[] = {"off", "recording", "stopping", "done", "full", "error"};
    int ok;
    if (con_word(&p, "stop")) {
        sdr_stop();
    } else {
        uint32_t ms = con_num(&p, &ok);
        if (ok || *p == 0) {
            FRESULT r = sdr_start_next(ok ? ms : 0u);
            con_kv("sd rec start", (int32_t)r);
            if (r == FR_OK) {
                con_puts(sdr_name());
                con_puts("\r\n");
            }
        }
    }
    con_kv(ST[sdr_state % 6u], (int32_t)sdr.sent);
    con_kv("  lost frames", (int32_t)sdr.lost);
    con_kv("  ring most blocks", (int32_t)sdr.lag_max);
    con_kv("  of", (int32_t)SDR_BLKS);
}
#endif

static void con_sd(const char *p)
{
    if (con_word(&p, "pins")) {
        int ok;
        uint32_t n = con_num(&p, &ok);
        con_sd_pins(ok ? n : 0u);
    } else if (con_word(&p, "diag")) {
        con_sd_diag();
    } else if (con_word(&p, "speed")) {
        con_sd_speed();
    } else if (con_word(&p, "mode")) {
        if (con_word(&p, "duplex"))
            fm1_sd_mode = FM1_SD_DUPLEX;
        else if (con_word(&p, "dirrx"))
            fm1_sd_mode = FM1_SD_DIRRX;
        else if (con_word(&p, "bitbang"))
            fm1_sd_mode = FM1_SD_BITBANG;
        con_kv("sd mode", fm1_sd_mode);
    } else if (con_word(&p, "dma")) {
        if (con_word(&p, "on"))
            fm1_sd_dma = 1;
        else if (con_word(&p, "off"))
            fm1_sd_dma = 0;
        con_kv("sd dma", fm1_sd_dma);
    } else if (con_word(&p, "clock")) {
        sd_clock_pick = con_word(&p, "20") ? 20u : con_word(&p, "30") ? 30u : 0u;   /* (auto, or anything else) */
        con_sd_mount();
    } else if (con_word(&p, "raw")) {
        int ok;
        uint32_t s = con_num(&p, &ok);
        sd_begin(SD_BAUD());
        if (sd_readp(sd_blk, (DWORD)s, 0, 64) == RES_OK)
            con_bytes("sector", sd_blk, 64);
        else
            con_puts("sd: read failed (mount first: sd)\r\n");
        sd_end();
    } else if (con_word(&p, "ls"))
        con_sd_ls(p);
    else if (con_word(&p, "cat"))
        con_sd_rw(p, 0);
    else if (con_word(&p, "bench")) {
        while (*p == ' ')
            p++;
        if (*p)
            con_sd_rw(p, 1);
        else
            con_sd_bench_modes();
    } else if (con_word(&p, "write"))
        con_sd_rw(p, 2);
#if FELUCCA_SD_STREAM
    else if (con_word(&p, "stream"))
        con_sd_stream_cmd(p);
#endif
#if FELUCCA_SD_REC
    else if (con_word(&p, "rec"))
        con_sd_rec(p);
#endif
    else
        con_sd_mount();
}
