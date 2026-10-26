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
        con_puts(sd_over ? "clock 30 MHz" : "clock 20 MHz");
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
    con_puts(baud == FM1_SD_BAUD_FAST ? " 20MHz" : baud == FM1_SD_BAUD_OVER ? " 30MHz" : " bb");
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

static void con_sd(const char *p)
{
    if (con_word(&p, "pins")) {
        int ok;
        uint32_t n = con_num(&p, &ok);
        con_sd_pins(ok ? n : 0u);
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
    else
        con_sd_mount();
}
