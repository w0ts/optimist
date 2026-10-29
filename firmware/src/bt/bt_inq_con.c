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


