/* SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0 */
/* EXPERIMENTAL Bluetooth Classic probe (BT_AUDIO): the console side of bt_inq.c, split out of it with no change
 * of behaviour: the options (bti_opts), the printing of what the interrupts copied into the rings (bti_print_rx,
 * bti_print_sec, btclk's result) and the commands btclk, btinq / btscan, bttrace, btrx. Included by bt_inq.c after its
 * run start (bti_run_start), before bt_page.c. No vendor code. */
static void bti_kv(const char *k, uint32_t v)
{
    con_puts(k);
    con_dec((int32_t)v);
}

#if FELUCCA_CDC && FELUCCA_BT_DIAG
static void bti_clk_print(void)
{
    uint32_t ms = bti_st.ms1 - bti_st.ms0, slots = (bti_st.clk1 - bti_st.clk0) & BTI_CLK;
    bti_kv("btclk: phase ", bti_st.phase);
    if (bti_st.phase == BTI_NO_TRIMS)
        con_puts(" (no stored RF trims in the VM: the radio is not started)");
    con_puts("\r\nbtclk: clk 0x");
    con_hex(bti_st.clk0, 7);
    con_puts(" -> 0x");
    con_hex(bti_st.clk1, 7);
    bti_kv(" in ", ms);
    bti_kv(" ms = ", ms ? slots * 1000u / ms : 0);
    bti_kv(" slots/s (expect 1600)\r\nbtclk: irq40 ", bti_st.irq40);
    bti_kv(" alarms ", bti_st.alarms);
    bti_kv(" (expect ~", ms * 16u / 180u);
    bti_kv(") late<=", bti_st.alarm_late);
    bti_kv(" rx ", bti_st.rx_irqs);
    bti_kv(" other ", bti_st.other40);
    con_puts(" unknown 0x");
    con_hex(bti_st.unknown40, 8);
    bti_kv("\r\nbtclk: irq41 ", bti_st.irq41);
    bti_kv(" timer0 ", bti_st.slot0);
    bti_kv(" (expect ~", ms / 1000u);
    bti_kv(") latch spins<=", bti_st.latch_spins);
    bti_kv(" fine<=", bti_st.fine_max);
    con_puts("\r\n");
}

/* ---- the options */
static int bti_opt_num(const char **p, const char *key, int32_t *v)   /* "key=N" or "key=-N" */
{
    const char *s = *p, *k = key;
    int ok = 0, neg = 0;
    uint32_t n;
    while (*s == ' ')
        s++;
    while (*k && *s == *k)
        s++, k++;
    if (*k || *s != '=')
        return 0;
    s++;
    if (*s == '-' || *s == '+')
        neg = *s++ == '-';
    n = con_num(&s, &ok);
    if (!ok)
        return 0;
    *v = neg ? -(int32_t)n : (int32_t)n;
    *p = s;
    return 1;
}

static void bti_opts(const char *p, uint32_t *seconds)
{
    int32_t v;
    int ok = 0;
    uint32_t s;
    bti_defaults();                                /* (sweep by default: BR §14; pll=N alone fixes the offset) */
    s = con_num(&p, &ok);
    if (ok && s >= 1u && s <= 120u)
        *seconds = s;
    for (;;) {
        if (con_word(&p, "even"))
            bti.o_pat = 0;
        else if (con_word(&p, "odd"))
            bti.o_pat = 1;
        else if (con_word(&p, "all"))
            bti.o_pat = 2;
        else if (con_word(&p, "sweep"))
            bti.o_sweep = 2;
        else if (con_word(&p, "nosweep"))
            bti.o_sweep = 0;
        else if (con_word(&p, "ble"))              /* (the default now: kept for the old command lines) */
            bti.o_ble = 1;
        else if (con_word(&p, "noble"))
            bti.o_ble = 0;
        else if (con_word(&p, "noclk"))            /* btpage: page without the last inquiry's clock estimate */
            bti.o_noclk = 1;
        else if (bti_opt_num(&p, "pcon", &v))      /* btpage: the piconet after the FHS (bt_page.c bp_page_rx) */
            bti.o_pcon = (uint8_t)(v & 3);
        else if (bti_opt_num(&p, "pclk", &v))      /* btpage: the piconet's clock offset after the FHS: N slots */
            bti.o_pclk = (int16_t)v, bti.o_pcon = 4;
        else if (bti_opt_num(&p, "padj", &v))      /* btpage: N slots added to the clock estimate */
            bti.o_padj = (int16_t)v;
        else if (bti_opt_num(&p, "ppar", &v))      /* btpage: the entries' parity after the FHS */
            bti.o_ppar = (uint8_t)(v & 1);
        else if (bti_opt_num(&p, "prio", &v))
            bti.o_prio = (uint8_t)(v < 1 ? 1 : v > 6 ? 6 : v);
        else if (bti_opt_num(&p, "agc", &v))
            bti.o_agc = (uint8_t)(v < 0 ? 0 : v > 15 ? 15 : v);
        else if (bti_opt_num(&p, "win", &v))
            bti.o_win = (uint16_t)v;
        else if (bti_opt_num(&p, "frq", &v))
            bti.o_frq = (int8_t)(v < -10 ? -10 : v > 10 ? 10 : v);
        else if (bti_opt_num(&p, "pll", &v)) {
            bti.o_pll = (int16_t)(v < -1000 ? -1000 : v > 1000 ? 1000 : v);
            if (bti.o_sweep == 1)
                bti.o_sweep = 0;                   /* (a fixed offset, unless `sweep` is given too: around it) */
        }
        else if (bti_opt_num(&p, "pwr", &v))
            bti.o_pwr = (uint8_t)(v < 0 ? 0 : v > 11 ? 11 : v);
        else
            break;
    }
}

static void bti_opts_print(void)
{
    static const char *const pat[3] = {"even", "odd", "all"};
    con_puts(" [");
    con_puts(pat[bti.o_pat]);
    if (bti.o_agc == 0xFFu)
        con_puts(" agc=follow");
    else
        bti_kv(" agc=", bti.o_agc);
    bti_kv(" win=", fm1_br_frame(bti_tab, 0)->wincntl);
    con_puts(" frq=");
    con_dec(bti.o_frq);
    con_puts(" pll=");
    con_dec(bti.o_pll);
    con_puts(bti.o_sweep ? " sweep" : "");
    bti_kv(" pwr=", bti.o_pwr);
    bti_kv(" prio=", bti.o_prio);
    con_puts(bti.le_up ? " le=on]" : " le=off]");
}

#endif

/* ---- the main loop: decode the ring, print */
static void bti_addr(const uint8_t a[6])
{
    uint32_t i;
    for (i = 6; i-- > 0;) {
        con_hex(a[i], 2);
        if (i)
            con_putc(':');
    }
}

static int bti_new(const uint8_t a[6], int32_t clk)   /* a device not yet printed in this inquiry: kept with its clock */
{
    uint32_t i;
    for (i = 0; i < bti.nseen; i++)
        if (ble_eq(bti.seen[i], a, 6)) {
            bti.seen_clk[i] = clk;
            return 0;
        }
    if (bti.nseen < 16u) {
        bti.seen_clk[bti.nseen] = clk;
        ble_cpy(bti.seen[bti.nseen++], a, 6);
    }
    return 1;
}

static int bti_seen_clk(const uint8_t a[6], int32_t *clk)   /* the last inquiry's clock offset for a -> 1 known */
{
    uint32_t i;
    for (i = 0; i < bti.nseen; i++)
        if (ble_eq(bti.seen[i], a, 6)) {
            *clk = bti.seen_clk[i];
            return 1;
        }
    return 0;
}

static void bti_rssi(const struct bti_rx *r)       /* BR §10.3: -1 - bits 5:0, the gain index (table not transcribed) */
{
    con_puts(" rssi_raw=0x");
    con_hex(r->agc & 0x3FFFu, 4);
    con_puts(" (");
    con_dec(-1 - (int32_t)(r->agc & 63u));
    bti_kv(" g", (r->agc >> 8) & 63u);
    bti_kv(") rssi1=", r->rssi1);
    bti_kv(" rxf=", r->rxfreq);
}

static const char *bti_tag(void)
{
    return bti.mode == BTI_M_SCAN ? "btscan: " : bti.mode >= BTI_M_PAGE ? "btpage: " : "btinq: ";
}

static void bp_print_rx(const struct bti_rx *r);

static void bti_print_rx(const struct bti_rx *r)
{
    if (r->kind >= BTI_K_PSTEP) {
        bp_print_rx(r);
        return;
    }
    if (r->kind == BTI_K_FHS) {
        struct bti_fhs fh;
        bti_fhs_parse(r->data, &fh);
        int32_t dclk = (int32_t)(((fh.clk << 1) - r->clkn) << 5) >> 5;   /* remote CLK - our RX CLKN: 27 bits (BR §6.1) */
        ble_cpy(bti.last_addr, fh.addr, 6);
#if FELUCCA_BT_AUDIO
        bta_saw(fh.addr, fh.cod);                  /* (the screen's list: bt_audio.c) */
#endif
        if (!bti_new(fh.addr, dclk))
            return;
        con_puts("btinq: FHS ");
        bti_addr(fh.addr);
        con_puts(" cod=0x");
        con_hex(fh.cod, 6);
        bti_kv(" sr=", fh.sr);
        con_puts(" clk=0x");
        con_hex(fh.clk, 7);
        con_puts(" dclk=");
        con_dec(dclk);
        bti_rssi(r);
        bti_kv(" eir=", fh.eir);
        con_puts("\r\n");
    } else if (r->kind == BTI_K_EIR) {
        char name[33];
        uint32_t n = bti_eir_name(r->data, r->len, name, sizeof name);
#if FELUCCA_BT_AUDIO
        if (n)
            bta_named(bti.last_addr, name);
#endif
        con_puts("btinq: EIR ");
        bti_addr(bti.last_addr);
        bti_kv(" len=", r->len);
        bti_kv(" type=", r->type);
        if (n) {
            con_puts(" name=\"");
            con_puts(name);
            con_puts("\"");
        }
        con_puts("\r\n");
    } else {
        con_puts(bti_tag());
        con_puts("heard stat=0x");
        con_hex(r->stat, 4);
        con_puts(" clkn=0x");
        con_hex(r->clkn, 7);
        bti_rssi(r);
        con_puts("\r\n");
    }
}

#if FELUCCA_CDC && FELUCCA_BT_DIAG
static void bti_print_hist(const char *k, const uint32_t *key, const uint32_t *n, uint32_t other, uint32_t digits)
{
    uint32_t i;
    con_puts(k);
    for (i = 0; i < BTI_NH && n[i]; i++) {
        con_hex(key[i], digits);
        bti_kv(":", n[i]);
        con_putc(' ');
    }
    bti_kv("other:", other);
    con_puts("}");
}

static void bti_print_sec(void)
{
    struct fm1_br_frame *f = fm1_br_frame(bti_tab, 0);
    uint32_t now = bti_clkn();
    con_puts(bti_tag());
    bti_kv("1s clk+", (now - bti.sec_clk) & BTI_CLK);
    bti.sec_clk = now;
    bti_kv(" irq40=", bti_st.irq40);
    bti_kv(" rx=", bti_st.rx_irqs);
    bti_kv(" alm=", bti_st.alarms);
    bti_kv(" o40=", bti_st.other40);
    con_puts(" u40=0x");
    con_hex(bti_st.unknown40, 8);
    bti_print_hist(" pend{", bti_st.pd_key, bti_st.pd_n, bti_st.pd_other, 8);
    bti_print_hist(" rs{", bti_st.rs_key, bti_st.rs_n, bti_st.rs_other, 4);
    bti_kv(" fhs=", bti_st.fhs);
    bti_kv(" eir=", bti_st.eir);
    bti_kv(" ids=", bti_st.ids);
    bti_kv(" drop=", bti_st.dropped);
    bti_kv(" txf=", f->txfreq);
    bti_kv(" rxf=", f->rxfreq);
    bti_kv(" agc=", f->rxgain0);
    con_puts(" pll=");
    con_dec(bti.pll_now);
    con_puts(" train=");
    con_putc(bti.train ? 'B' : 'A');
    con_puts("\r\n");
}


#endif
#if FELUCCA_BT_DIAG
static void con_btrx(const char *p)                /* every reception of the last run (the first BTI_RXLOG) */
{
    static const char *const mode[4] = {"inq", "scan", "page", "conn"};
    uint32_t i, n, t0;
    int ok = 0;
    n = con_num(&p, &ok);
    if (!ok || n > bti_rxn)
        n = bti_rxn;
    t0 = n ? bti_rxlog_buf[0].t : 0;
    bti_kv("btrx: ", bti_rxn);
    con_puts(" kept: us mode buf stat hdr(llid,len) rxbit rxfreq pll rxclk\r\n");
    for (i = 0; i < n; i++) {
        const struct bti_rxl *l = &bti_rxlog_buf[i];
        bti_kv("", (l->t - t0) / FM1_TICKS_PER_US);
        con_puts(" ");
        con_puts(mode[l->mode & 3u]);
        bti_kv(" ", l->idx);
        con_puts(" ");
        con_hex(l->stat, 4);
        bti_kv(" ", l->hdr & 3u);
        bti_kv(",", (l->hdr >> 3) & 0x3FFu);
        bti_kv(" ", l->rxbit & 0x3FFu);
        bti_kv(" ", l->rxfreq);
        con_puts(" ");
        con_dec(l->pll);
        con_puts(" ");
        con_hex(l->clkn, 4);
        con_puts("\r\n");
    }
}
#endif

#if FELUCCA_CDC && FELUCCA_BT_DIAG
/* ---- the console: btclk [run], btinq / btscan [SECONDS] [OPTIONS] | stop, bttrace [N] */
static void con_btclk(const char *p)
{
    if (con_word(&p, "run")) {
        con_puts(bti_clk_start() ? "btclk: running 3 s\r\n" : "btclk: not started (busy, or no RF trims: 'btclk')\r\n");
        return;
    }
    if (bti_st.phase == BTI_CLK_RUN)
        con_puts("btclk: running\r\n");
    else
        bti_clk_print();
}

static void con_btrun(const char *p, uint32_t mode)
{
    uint32_t s = mode == BTI_M_INQ ? 0 : 30u;
    if (con_word(&p, "stop")) {
        fm1_br_irqs_hold(1);
        bti_run_stop();
        fm1_br_irqs_hold(0);
        return;
    }
    bti_opts(p, &s);
    if (!bti_run_start(mode, s ? s * 1600u : 16384u)) {   /* the inquiry's default: 10.24 s (the spec's) */
        con_puts("not started (busy, or no RF trims: 'btclk')\r\n");
        return;
    }
    con_puts(bti_tag());
    if (mode == BTI_M_INQ) {
        bti_kv("inquiry (GIAC) for ", bti.inq_slots * 5u / 8u);
        con_puts(" ms: make a device discoverable");
    } else {
        bti_kv("inquiry scan for ", bti.inq_slots * 5u / 8u);
        con_puts(" ms as \"OPTIMIST BT\" ");
        bti_addr(bti.own);
        con_puts(": search from a phone or computer");
    }
    bti_opts_print();
    con_puts("\r\n");
}

#endif
#if FELUCCA_BT_DIAG
static void con_bttrace(const char *p)
{
    uint32_t i, n, k, t0;
    int ok = 0;
    n = con_num(&p, &ok);
    if (!ok || n > bti_tn)
        n = bti_tn;
    t0 = n ? bti_trace[0].t : 0;
    bti_kv("bttrace: ", bti_tn);
    con_puts(" kept: us pend tog(r/t) f(t/r) rxstat0/1 rxphdr0/1 linkcntl0/1 txbuf rxbuf rssi0-3 rxclk rxbit\r\n");
    for (i = 0; i < n; i++) {
        const struct bti_tr *t = &bti_trace[i];
        bti_kv("", (t->t - t0) / FM1_TICKS_PER_US);
        con_puts(" ");
        con_hex(t->pend, 8);
        bti_kv(" ", t->rxtog);
        bti_kv("/", t->txtog);
        bti_kv(" ", t->txfreq);
        bti_kv("/", t->rxfreq);
        for (k = 0; k < 2u; k++) {
            con_puts(k ? "/" : " ");
            con_hex(t->rxstat[k], 4);
        }
        for (k = 0; k < 2u; k++) {
            con_puts(k ? "/" : " ");
            con_hex(t->rxphdr[k], 4);
        }
        for (k = 0; k < 2u; k++) {
            con_puts(k ? "/" : " ");
            con_hex(t->linkcntl[k], 4);
        }
        con_puts(" ");
        con_hex(t->txbufcntl[0], 2);
        con_hex(t->txbufcntl[1], 2);
        con_puts(" ");
        con_hex(t->rxbufcntl[0], 2);
        con_hex(t->rxbufcntl[1], 2);
        for (k = 0; k < 4u; k++) {
            con_puts(k ? "/" : " ");
            con_hex(t->rssi[k], 4);
        }
        con_puts(" ");
        con_hex(t->rx_clkn_lo, 4);
        con_puts(" ");
        con_hex(t->rxbit, 4);
        con_puts("\r\n");
    }
}
#endif
