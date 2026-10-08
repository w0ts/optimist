/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Serial console on the CDC-ACM function (FELUCCA_CDC=1): read-only
 * diagnostics. Runs in the main loop (cdc_task); usb_poll moves the bytes.
 * The baud rate is ignored. Nothing here writes memory or flash; `uboot`
 * does what the SysEx soft key does. */
#define CON_LINE 64u

static struct {
    char line[CON_LINE];
    uint32_t len;
    uint8_t dtr_seen, stalled;   /* stalled: the host stopped reading, drop the rest of this reply */
} con;

static void con_putc(char c)
{
    uint32_t t0 = fm1_ms;
    while (co_w - co_r >= CO_N) {                      /* full: wait for usb_poll, briefly */
        if (!cdc.dtr || con.stalled || fm1_ms - t0 > 20u) {
            con.stalled = 1;
            return;
        }
        fm1_wdt_feed();
    }
    cdc_out[co_w % CO_N] = (uint8_t)c;
    RING_PUBLISH();
    co_w++;
}

static void con_puts(const char *s)
{
    while (*s)
        con_putc(*s++);
}

static void con_hex(uint32_t v, uint32_t digits)
{
    while (digits--)
        con_putc("0123456789ABCDEF"[(v >> (digits * 4u)) & 15u]);
}

static void con_dec(int32_t v)
{
    char b[12];
    uint32_t n = 0, u = v < 0 ? (uint32_t)-v : (uint32_t)v;
    if (v < 0)
        con_putc('-');
    do
        b[n++] = (char)('0' + u % 10u);
    while ((u /= 10u) != 0 && n < sizeof b);
    while (n)
        con_putc(b[--n]);
}

static void con_kv(const char *k, int32_t v)            /* "key value\r\n" */
{
    con_puts(k);
    con_putc(' ');
    con_dec(v);
    con_puts("\r\n");
}

static void con_kx(const char *k, uint32_t v)
{
    con_puts(k);
    con_puts(" 0x");
    con_hex(v, 8);
    con_puts("\r\n");
}

static uint32_t con_num(const char **p, int *ok)        /* decimal or 0x hex */
{
    const char *s = *p;
    uint32_t v = 0, base = 10, d, n = 0;
    while (*s == ' ')
        s++;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        s += 2, base = 16;
    for (;; s++, n++) {
        char c = *s;
        if (c >= '0' && c <= '9')
            d = (uint32_t)(c - '0');
        else if (base == 16 && c >= 'a' && c <= 'f')
            d = (uint32_t)(c - 'a' + 10);
        else if (base == 16 && c >= 'A' && c <= 'F')
            d = (uint32_t)(c - 'A' + 10);
        else
            break;
        v = v * base + d;
    }
    *ok = n > 0;
    *p = s;
    return v;
}

static int con_word(const char **p, const char *w)       /* match a whole word */
{
    const char *s = *p;
    while (*s == ' ')
        s++;
    while (*w && *s == *w)
        s++, w++;
    if (*w || (*s && *s != ' '))
        return 0;
    *p = s;
    return 1;
}

static void con_memr(const char *p)
{
    int ok, ok2;
    uint32_t a = con_num(&p, &ok), n = con_num(&p, &ok2), i;
    if (!ok) {
        con_puts("usage: memr ADDR [LEN<=256]\r\n");
        return;
    }
    if (!ok2 || !n)
        n = 64;
    if (n > 256u)
        n = 256;
    if (!fm1_mem_readable(a, n)) {
        con_puts("only RAM 01C00000-01C80000 and XIP 02000000-02100000\r\n");
        return;
    }
    for (i = 0; i < n; i++) {
        if (i % 16u == 0) {
            con_hex(a + i, 8);
            con_putc(':');
        }
        con_putc(' ');
        con_hex(fm1_peek8(a + i), 2);
        if (i % 16u == 15u || i + 1u == n)
            con_puts("\r\n");
    }
}

#if FELUCCA_FLASH
static void con_flr(const char *p)                  /* flash read over SPI (no XIP decryption) */
{
    static uint8_t b[256];
    int ok, ok2;
    uint32_t a = con_num(&p, &ok), n = con_num(&p, &ok2), i;
    if (!ok || a < 0x93000u || a >= 0x100000u) {
        con_puts("usage: flr OFFSET [LEN<=256]   (0x93000..0xFFFFF)\r\n");
        return;
    }
    if (!ok2 || !n)
        n = 64;
    if (n > 256u)
        n = 256;
    if (a + n > 0x100000u)
        n = 0x100000u - a;
    if (!flash_ok || st_read(a, b, n)) {
        con_puts("flash not available\r\n");
        return;
    }
    for (i = 0; i < n; i++) {
        if (i % 16u == 0) {
            con_hex(a + i, 6);
            con_putc(':');
        }
        con_putc(' ');
        con_hex(b[i], 2);
        if (i % 16u == 15u || i + 1u == n)
            con_puts("\r\n");
    }
}
#endif

#if FELUCCA_BLE
/* the radio's stored calibration (midi_ble.c, ble/ble_vm.c; docs/BLE-HW-FACTS.md §14, §18): read only */
static void con_bytes(const char *k, const uint8_t *d, uint32_t n)   /* "key: XX XX ..", 16 a line */
{
    uint32_t i;
    con_puts(k);
    for (i = 0; i < n; i++) {
        if (i && i % 16u == 0)
            con_puts("\r\n    ");
        con_putc(' ');
        con_hex(d[i], 2);
    }
    con_puts("\r\n");
}

static void con_vm_rec(void *ctx, uint32_t off, uint32_t id, uint32_t len)
{
    (void)ctx;
    con_putc('@');
    con_hex(off, 6);
    con_puts(" id ");
    con_dec((int32_t)id);
    con_puts(" len ");
    con_dec((int32_t)len);
    con_puts("\r\n");
}

static void con_trims(const struct ble_rf_trims *t, uint8_t have)
{
    if (have & 1u)
        con_bytes("106:", t->x106, sizeof t->x106);
    if (have & 2u)
        con_bytes("107:", t->x107, sizeof t->x107);
    if (have & 4u)
        con_bytes("108:", t->x108, sizeof t->x108);
    if (have & 8u)
        con_bytes("187:", t->x187, sizeof t->x187);
}

static void con_blevm(void)                         /* stock V15's VM where the firmware finds it (ble_vm.h's candidates) */
{
    struct ble_vm_info in;
    struct ble_rf_trims t;
    uint8_t m[4];
    uint32_t a, i, size;
    int ok;
    for (i = 0; (a = ble_vm_cand(i, &size)) != 0; i++) {   /* every candidate's first word, in the order tried */
        con_puts("area ");
        con_hex(a, 6);
        con_putc(':');
        if (ble_vm_rd(0, a, m, 4)) {
            con_puts(" flash not available\r\n");
            return;
        }
        con_putc(' ');
        con_hex((uint32_t)m[0] << 24 | (uint32_t)m[1] << 16 | (uint32_t)m[2] << 8 | m[3], 8);
        con_puts("\r\n");
    }
    ok = ble_vm_scan(ble_vm_rd, 0, &in, &t, con_vm_rec, 0);
    if (!in.area) {
        con_puts("no VM (no candidate with 55AAAA55 and a valid first record)\r\n");
        return;
    }
    con_kx("live", in.area);                        /* the candidate used */
    con_kx("size", in.size);
    con_kx("log_end", in.end);                      /* (the emulator's V15 compacts its 8 KiB areas past 60 %, HW §14.4) */
    con_kv("records", in.nrec);
    con_kx("have", in.have);                        /* bits: 106, 107, 108, 187 */
    con_kx("wrong_len", in.wrong_len);
    con_kv("crc187", in.ok187);
    con_kv("read_err", in.read_err);
    con_kv("complete", ok);
    con_trims(&t, in.have);
}

#if FELUCCA_FLASH
/* every candidate area of the VM (ble_vm.h: 0x0E8000 and 0x0E7000, 4 KiB each, then the emulator's 0x093000-0x096FFF,
 * 24 KiB in all), raw, in flr's line format: tools/ble_vm.py decodes a log of it (or of 'flr' reads in a build without
 * BLE). About 85 KB of text: the main loop (the panel, not the audio) waits while the host reads it. */
static int con_dump_flash(uint32_t base, uint32_t size)   /* -> 0 done, -1 stopped (no flash, or the host stalled) */
{
    static uint8_t b[256];
    uint32_t a, i;
    for (a = base; a < base + size; a += sizeof b) {
        if (!flash_ok || st_read(a, b, sizeof b)) {
            con_puts("flash not available\r\n");
            return -1;
        }
        for (i = 0; i < sizeof b; i++) {
            if (i % 16u == 0) {
                con_hex(a + i, 6);
                con_putc(':');
            }
            con_putc(' ');
            con_hex(b[i], 2);
            if (i % 16u == 15u)
                con_puts("\r\n");
        }
        if (con.stalled)
            return -1;
    }
    return 0;
}

static void con_blevmdump(void)
{
    uint32_t k, size, base;
    for (k = 0; (base = ble_vm_cand(k, &size)) != 0; k++)
        if (con_dump_flash(base, size))
            return;
    con_puts("end\r\n");
}
#endif

static void con_bletrim(void)                       /* what the radio uses, Optimist's copy, what rf_init did */
{
    static const char *const SRC[3] = {"none (the radio stays off)", "VM", "Optimist's copy"};
    struct ble_rf_trims t;
    con_puts("source ");
    con_puts(SRC[ble_rf_src % 3u]);
    con_puts("\r\n");
    con_kx("vm_area", ble_vm_seen.area);
    con_kx("vm_have", ble_vm_seen.have);
    con_kv("vm_crc187", ble_vm_seen.ok187);
    con_kv("copy_ok", ble_rf_copy_ok(ble_rf_kept));
    con_kv("bluetooth_on", ble_on);
    con_kv("radio_started", ble_up);
    con_kv("boot_failed", (int32_t)bootguard.failed);   /* > 0: ON saved, this boot left the radio off (ble_boot_radio) */
    con_bytes("copy_raw:", ble_rf_kept, sizeof ble_rf_kept);    /* (the 100 bytes as kept: mark, data, CRC) */
    if (ble_rf_copy_ok(ble_rf_kept)) {
        con_kx("copy_from", ble_rf_copy_area(ble_rf_kept[0]));   /* the VM area it was taken from */
        ble_rf_copy_get(ble_rf_kept, &t);
        con_trims(&t, BLE_VM_ALL);
    }
#if BLE_HW_WL82
    con_puts("tables ");
    con_puts(BLE_RF_SHA256);
    con_puts("\r\n");
    con_kv("rf_ran", fm1_ble_rf_stat.ran);
    con_kv("rf_ops", (int32_t)fm1_ble_rf_stat.ops);
    con_kv("rf_trims", (int32_t)fm1_ble_rf_stat.trims);
    con_kv("rf_lut_words", (int32_t)fm1_ble_rf_stat.lut_words);
    con_kv("rf_delay_us", (int32_t)fm1_ble_rf_stat.delay_us);
    con_kv("rf_skipped", (int32_t)fm1_ble_rf_stat.skipped);
    con_kv("rf_bbp_timeouts", (int32_t)fm1_ble_rf_stat.bbp_timeouts);
    con_kv("rf_spi_timeouts", (int32_t)fm1_ble_rf_stat.spi_timeouts);
    con_kx("rf_bad_op", fm1_ble_rf_stat.bad_op);
    con_kv("vco_found", fm1_ble_rf_stat.scan_found);
    con_kv("vco_band", fm1_ble_rf_stat.scan_band);
    con_kv("vco_steps", fm1_ble_rf_stat.scan_steps);
    con_kx("vco_result", fm1_ble_rf_stat.scan_result);
    con_kv("rf_section", fm1_ble_rf_stat.section);           /* the §16.1 group last entered (15: done) */
    con_kx("rf_sections", fm1_ble_rf_stat.sections);
#endif
}
#endif

/* the clock registers as the SPL left them (hal/fm1_clock.h): sys_div clk_con0..3, pll pll_con0/1 pll2_con0/1 */
static void con_clock_regs(void)
{
    uint32_t i;
    con_puts("clk");
    for (i = 0; i < 9u; i++) {
        con_putc(' ');
        con_hex(fm1_clk_reg(i), 8);
    }
    con_puts("\r\n");
}

static void con_status(void)
{
    const engine_t *e = ENGINES[TSEL->eng_req % NENGINES];
    con_puts("felucca ");
    con_puts(FELUCCA_VERSION);
    con_puts("\r\n");
    con_kv("uptime_ms", (int32_t)fm1_ms);
    con_kv("cpu_pct", (int32_t)(song.cpu_q8 * 100u / 256u));
    con_kv("cpu_khz", (int32_t)cpu_khz);                /* measured at boot (hal/fm1_clock.h) */
    con_clock_regs();
    con_kv("audio_max_us", (int32_t)felucca_dbg.max_us);
    con_kv("voices_shed", (int32_t)shed_count);
    con_kv("voices_given_up", (int32_t)voice_kills);
#if FELUCCA_CPU_GUARD
    con_kv("cpu_guard", (int32_t)cg.level);
    con_kv("cpu_guard_steps", (int32_t)cg.steps);
    con_kv("cpu_guard_shed", (int32_t)cg.sheds);
#endif
#if FELUCCA_DUAL
    con_kv("cpu1_up", dual.up);
    con_kv("cpu1_why", dual.why);
    con_kv("cpu1_jobs", (int32_t)dual.jobs);
    con_kv("cpu1_wait_max_us", (int32_t)(dual.wait_max / FM1_TICKS_PER_US));
#endif
    con_kv("track", (int32_t)song.sel + 1);
    con_kv("batt_raw", song.batt_raw);
    con_puts("engine ");
    con_puts(e->name);
    con_puts("\r\n");
    con_puts("preset ");
    con_puts(TSEL->preset < e->npresets ? e->presets[TSEL->preset].name : "-");
    con_puts("\r\n");
    con_kv("bpm", song.g[G_BPM]);
    con_kv("playing", song.playing);
    con_kv("boots", (int32_t)felucca_dbg.boots);
    con_kv("usb_resets", (int32_t)usb.resets);
    con_kv("usb_sof", (int32_t)usb.sof_seen);
    con_kv("usb_suspends", (int32_t)usb.suspends);
    con_kv("usb_retries", (int32_t)usb.retries);
    con_kv("usb_frame", (int32_t)usb.frame);
    con_kv("usb_frame_stalls", (int32_t)usb.frame_stalls);
    con_kv("usb_max_gap_polls", (int32_t)usb.max_gap);
    con_kv("midi_rx_pkts", (int32_t)usb.rx_pkts);
    con_kv("midi_tx_pkts", (int32_t)usb.tx_pkts);
#if FELUCCA_FLASH
    con_kv("flash", flash_ok);
#endif
}

static void con_dbg(void)
{
    const uint32_t *w = (const uint32_t *)&felucca_dbg;
    static const char *const NAMES[] = {"magic", "halves", "max_us", "nested", "in_audio", "late",
                                        "timer_irqs", "ui_frames", "last_us", "cpu_q8", "boots",
                                        "stage", "page", "home", "prev_stage", "prev_page",
                                        "prev_home", "prev_rst", "prev_frames"};
    uint32_t i;
    for (i = 0; i < sizeof NAMES / sizeof NAMES[0] && i < sizeof felucca_dbg / 4u; i++)
        con_kx(NAMES[i], w[i]);
}

static void con_crash(void)
{
    if (fm1_crash.magic != FM1_CRASH_MAGIC) {
        con_puts("no crash record\r\n");
        return;
    }
    con_kv("count", (int32_t)fm1_crash.count);
    con_kx("vec", fm1_crash.vec);
    con_kx("pc", fm1_crash.pc);
    con_kx("rets", fm1_crash.rets);
    con_kx("emu", fm1_crash.emu);
    con_kx("sp", fm1_crash.sp);
    con_kx("psr", fm1_crash.psr);
    con_kv("uptime_ms", (int32_t)fm1_crash.uptime_ms);
    con_kv("early", (int32_t)fm1_crash.early);
}

static void con_params(void)
{
    uint32_t i;
    for (i = 0; i < P_COUNT; i++) {
        con_dec((int32_t)i);
        con_putc('=');
        con_dec(TSEL->p[i]);
        con_puts(i % 8u == 7u || i + 1u == P_COUNT ? "\r\n" : " ");
    }
}

static void con_exec(const char *p)
{
    if (con_word(&p, "help") || con_word(&p, "?"))
        con_puts("status  dbg  crash  params  memr ADDR [LEN]  flr OFF [LEN]"
#if FELUCCA_BLE
                 "  blevm  blevmdump  bletrim"
#endif
                 "  uboot yes\r\n");
    else if (con_word(&p, "status"))
        con_status();
    else if (con_word(&p, "dbg"))
        con_dbg();
    else if (con_word(&p, "crash"))
        con_crash();
    else if (con_word(&p, "params"))
        con_params();
    else if (con_word(&p, "memr"))
        con_memr(p);
#if FELUCCA_FLASH
    else if (con_word(&p, "flr"))
        con_flr(p);
#endif
#if FELUCCA_BLE
    else if (con_word(&p, "blevmdump"))
#if FELUCCA_FLASH
        con_blevmdump();
#else
        con_puts("flash not available\r\n");
#endif
    else if (con_word(&p, "blevm"))
        con_blevm();
    else if (con_word(&p, "bletrim"))
        con_bletrim();
#endif
    else if (con_word(&p, "uboot")) {
        if (con_word(&p, "yes")) {
            con_puts("entering UBOOT\r\n");
            usb.uboot_req = 1;                         /* main loop: same path as the SysEx key */
        } else {
            con_puts("type 'uboot yes'\r\n");
        }
    } else if (*p)
        con_puts("? (help)\r\n");
}

static void cdc_task(void)                              /* main loop */
{
    if (cdc.dtr && !con.dtr_seen) {
        con_puts("\r\nOptimist (Felucca) ");
        con_puts(FELUCCA_VERSION);
        con_puts(" console - 'help'\r\n> ");
    }
    con.dtr_seen = cdc.dtr;
    while (ci_r != ci_w) {
        char c;
        RING_PUBLISH();                                /* usb_poll (producer) can preempt us */
        c = (char)cdc_in[ci_r % CI_N];
        RING_PUBLISH();
        ci_r++;
        if (c == '\r' || c == '\n') {
            if (c == '\n' && con.len == 0)
                continue;                              /* the LF of a CR LF */
            con_puts("\r\n");
            con.line[con.len] = 0;
            con.stalled = 0;
            con_exec(con.line);
            con.len = 0;
            con_puts("> ");
        } else if (c == 0x7F || c == 0x08) {
            if (con.len) {
                con.len--;
                con_puts("\b \b");
            }
        } else if (c >= ' ' && c < 0x7F && con.len + 1u < CON_LINE) {
            con.line[con.len++] = c;
            con_putc(c);
        }
    }
}
