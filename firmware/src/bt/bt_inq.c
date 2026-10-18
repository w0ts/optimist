/* SPDX-License-Identifier: GPL-3.0-only */
/* EXPERIMENTAL Bluetooth Classic (BR/EDR) inquiry probe (builder item BT_INQ_PROBE, FELUCCA_BT_INQ_PROBE): stage 1 of
 * our own Classic stack, docs/BT-CLASSIC-HW-FACTS.md §14. Written only from that fact sheet (cited as "BR §n") and
 * the Core specification; no vendor code. Included by io/console.c (it needs the CDC console, USB_MODE 1) in a build
 * without BLE (one engine on the radio at a time). The registers are hal/fm1_bredr.h's.
 *
 *   1a  the BR/EDR block's clock and interrupts: run once ~1.5 s after boot (and by `btclk run`): the radio's start-up
 *       with the stored trims (hal/fm1_ble.h, the VM only), the BR/EDR init (BR §10.2), then 3 s of the slot clock
 *       (expect +1,600 a second), the 18-slot alarm (IRQ 40 bit 9) and slot timer 0 every 1,600 slots (IRQ 41).
 *       Nothing is transmitted. `btclk` prints the result (bti_st: the emulator check reads it)
 *   1b  `btinq [SECONDS]`: an inquiry with the GIAC (BR §8.1; default 10.24 s): one frame, procedure 8, ID trains by
 *       the engine, FHS and EIR responses printed as `btinq:` lines, a status line a second, an RX status histogram
 *
 * The interrupts copy what came into a ring and re-arm; the main loop (bti_poll) decodes and prints. Nothing here
 * writes flash. */
#include "fm1_ble.h"                               /* the radio's start-up (fm1_ble_rf_init) */
#include "fm1_bredr.h"                             /* the BR/EDR block */
#include "../ble/ble_vm.c"                         /* the stored trims in stock V15's VM (read only) */
#include "bt_inq_core.c"

#if !FELUCCA_FLASH
#error "BT_INQ_PROBE reads the RF trims from flash (FELUCCA_FLASH)"
#endif
#if FELUCCA_BLE
#error "BT_INQ_PROBE: build without BLE (one engine on the radio at a time)"
#endif

/* fm1_ble.h carries the BLE driver's two interrupt entries; this build never attaches them */
void ble_wl82_event_irq(void) {}
void ble_wl82_rx_irq(void) {}

#define BTI_IRQ_PRIO 2u                            /* the library's (BR §9.3): below the audio (3) */
#define BTI_CLK FM1_BR_CLK_MASK

static uint8_t bti_tab[FM1_BR_TAB_SIZE] IN_POOL __attribute__((aligned(4)));   /* zeroed (the pool); the engine's DMA */

/* ---- what the probe saw (the emulator check reads it: tests/bt_inq_emu_test.py) */
enum { BTI_IDLE, BTI_CLK_RUN, BTI_CLK_DONE, BTI_INQ_RUN, BTI_INQ_DONE, BTI_NO_TRIMS = 0xE1, BTI_LATCH_HUNG = 0xE2 };
#define BTI_MAGIC 0x31495442u                      /* "BTI1" */
struct bti_stat {
    uint32_t magic, phase, rf_up;
    uint32_t clk0, clk1, ms0, ms1, fine_max;       /* 1a: CLKN and ms at the start and the end */
    uint32_t irq40, alarms, rx_irqs, other40;      /* IRQ 40: entries, bit 9, bit 1, the other known bits */
    uint32_t unknown40, irq41, slot0, latch_spins; /* unknown pending bits seen (OR), IRQ 41 entries, timer 0 */
    uint32_t alarm_late;                           /* the largest CLKN at the alarm minus its slot */
    uint32_t fhs, eir, dropped, trains;            /* 1b */
    uint32_t hist_key[8], hist_n[8], hist_other;   /* 1b: RX status high bytes seen and their counts */
};
static struct bti_stat bti_st = {BTI_MAGIC};

/* the interrupts' ring: one FHS or EIR each */
#define BTI_RING 8u
struct bti_rx {
    uint8_t kind, type, idx, pad;                  /* kind 1 FHS, 2 EIR */
    uint32_t clkn, stat, agc, len;
    uint8_t data[240];
};
static struct bti_rx bti_ring[BTI_RING] IN_POOL;
static volatile uint32_t bti_rw, bti_rr;

static struct {
    uint32_t end_clkn, next_slot0, train_clkn, last_rx_clkn, inq_slots, sec_ms, sec_clk, alarm_at;
    uint8_t bredr_up, want_eir, train, nseen;
    uint8_t seen[16][6], last_addr[6];
} bti;

static uint32_t bti_clkn(void)
{
    uint32_t spins, c = fm1_br_clkn(&spins);
    if (spins > bti_st.latch_spins)
        bti_st.latch_spins = spins;
    if (c == 0xFFFFFFFFu)
        bti_st.phase = BTI_LATCH_HUNG;             /* (never seen; a stuck latch must not hang the main loop) */
    return c;
}

static void bti_alarm_arm(uint32_t slots)          /* BR §6.4 (its slot kept: no readback in the emulator) */
{
    bti.alarm_at = (bti_clkn() + slots) & BTI_CLK;
    fm1_br_alarm_at(bti.alarm_at);
}

/* ---- the radio and the BR/EDR block */
static int bti_vm_rd(void *ctx, uint32_t off, uint8_t *dst, uint32_t n)   /* SPI reads, no XIP decryption */
{
    (void)ctx;
    return flash_ok ? st_read(off, dst, n) : -1;
}

static int bti_radio_up(void)                      /* the radio's start-up, once (the stored trims: VM only) */
{
    struct ble_vm_info info;
    struct ble_rf_trims t;
    if (bti_st.rf_up)
        return 1;
    if (!ble_vm_scan(bti_vm_rd, 0, &info, &t, 0, 0)) {
        bti_st.phase = BTI_NO_TRIMS;
        return 0;
    }
    fm1_ble_rf_init(t.x106, t.x107, t.x108, t.x187);
    bti_st.rf_up = 1;
    return 1;
}

static void bti_bredr_up(void)
{
    uint32_t i;
    if (bti.bredr_up)
        return;
    for (i = 0; i < FM1_BR_TAB_SIZE; i++)
        bti_tab[i] = 0;
    fm1_br_init(bti_tab);
    fm1_br_irqs_hold(0);
    bti.bredr_up = 1;
}

/* ---- interrupts */
static void bti_hist(uint32_t hi)
{
    uint32_t i;
    for (i = 0; i < 8u; i++) {
        if (bti_st.hist_n[i] && bti_st.hist_key[i] == hi) {
            bti_st.hist_n[i]++;
            return;
        }
        if (!bti_st.hist_n[i]) {
            bti_st.hist_key[i] = hi;
            bti_st.hist_n[i] = 1;
            return;
        }
    }
    bti_st.hist_other++;
}

static void bti_push(uint32_t kind, uint32_t idx, uint32_t stat, uint32_t clkn, const uint8_t *src, uint32_t len)
{
    struct bti_rx *r;
    uint32_t i;
    if (bti_rw - bti_rr >= BTI_RING) {
        bti_st.dropped++;
        return;
    }
    r = &bti_ring[bti_rw % BTI_RING];
    r->kind = (uint8_t)kind;
    r->idx = (uint8_t)idx;
    r->stat = stat;
    r->type = (uint8_t)(stat & 15u);
    r->len = len;
    r->clkn = clkn;
    r->agc = fm1_br_agc3();
    for (i = 0; i < len; i++)
        r->data[i] = src[i];
    RING_PUBLISH();
    bti_rw++;
}

static int bti_is_data(uint32_t type)              /* DM1 DH1 DM3 DH3 DM5 DH5: an EIR's (BR §8.1) */
{
    return type == 3u || type == 4u || type == 10u || type == 11u || type == 14u || type == 15u;
}

static void bti_rx_frame(void)                     /* BR §8.1 "On IRQ 40 RX": the filled buffer is rxtog ^ 1 */
{
    struct fm1_br_frame *f = fm1_br_frame(bti_tab, 0);
    uint32_t idx = (f->rxtog & 1u) ^ 1u, stat = f->rxstat[idx], type = stat & 15u, len;
    uint32_t clkn = f->rx_clkn_lo | (uint32_t)f->rx_clkn_hi << 16;
    if (clkn == bti.last_rx_clkn)
        return;                                    /* (this frame has nothing new: BR §6.2) */
    bti.last_rx_clkn = clkn;
    bti_hist(stat >> 8);
    f->linkcntl[idx] &= (uint16_t)~(1u << 14);
    if ((stat >> 8) == 0x8Fu && type == 2u) {      /* an FHS (BR §5.4) */
        bti_push(1, idx, stat, clkn, bti_tab + FM1_BR_OFF_FHS, 18u);
        bti_st.fhs++;
        bti.want_eir = (uint8_t)((bti_tab[FM1_BR_OFF_FHS + 7] >> 2) & 1u);   /* its EIR bit (BR §8.4) */
        if (!bti.want_eir)
            fm1_br_eir_arm(0);
    } else if ((stat >> 8) == 0x8Fu && bti.want_eir && bti_is_data(type)) {
        len = (f->rxphdr[idx] >> 3) & 0x3FFu;      /* (BR §4 rxphdr: [12:3] length) */
        bti_push(2, idx, stat, clkn, bti_tab + FM1_BR_OFF_EIR, len > 240u ? 240u : len);
        bti_st.eir++;
        bti.want_eir = 0;
        fm1_br_eir_arm(0);
        fm1_br_con_or(1u << 11);
    }
    f->rxbufcntl[idx] &= (uint8_t)~1u;             /* re-armed (BR §5.5) */
    f->linkcntl[idx] |= 1u << 14;
}

static void bti_inq_stop(void)                     /* (from the alarm or the console) */
{
    uint32_t i;
    for (i = 0; i < 16u; i++)
        fm1_br_xchg_or(bti_tab, i, 1u << 12);
    fm1_br_con_or(0x80000000u);                    /* as the library's suspend (BR §2.1) */
    fm1_br_eir_arm(0);
    fm1_br_alarm_off();
    if (bti_st.phase == BTI_INQ_RUN)
        bti_st.phase = BTI_INQ_DONE;
}

static void bti_alarm(void)                        /* the 18-slot housekeeping (BR §6.4, §8.1) */
{
    uint32_t now = bti_clkn(), late = (now - bti.alarm_at) & BTI_CLK;
    if (late < 0x100000u && late > bti_st.alarm_late)
        bti_st.alarm_late = late;
    if (bti_st.phase == BTI_INQ_RUN) {
        struct fm1_br_frame *f = fm1_br_frame(bti_tab, 0);
        if (((now - bti.train_clkn) & BTI_CLK) >= 4096u) {   /* the A/B train every 2.56 s (BR §7.1) */
            bti.train_clkn = now;
            bti.train ^= 1u;
            f->format = (uint8_t)((f->format & 0x7Fu) | bti.train << 7);
            bti_st.trains++;
        }
        bti.want_eir = 0;                          /* (an EIR not come by now will not) */
        fm1_br_eir_arm(1);
        if (((bti.end_clkn - now) & BTI_CLK) > (BTI_CLK >> 1)) {   /* past the end */
            bti_inq_stop();
            return;
        }
    }
    if (bti_st.phase == BTI_CLK_RUN || bti_st.phase == BTI_INQ_RUN)
        bti_alarm_arm(18u);
}

void bti_bt_irq(void)                              /* IRQ 40 (BR §6.2), via isr_bti_bt (hal/fm1_bredr.h) */
{
    uint32_t p = fm1_br_ipnd();
    bti_st.irq40++;
    bti_st.unknown40 |= p & ~(uint32_t)FM1_BR_IPND_KNOWN;
    if (p & 1u)
        fm1_br_iack(1u);
    if (p & 2u) {
        fm1_br_iack(2u);
        bti_st.rx_irqs++;
        if (bti_st.phase == BTI_INQ_RUN)
            bti_rx_frame();
    }
    if (p & (1u << 10))
        fm1_br_iack(1u << 10);
    if (p & (1u << 9)) {
        fm1_br_iack(1u << 9);
        bti_st.alarms++;
        bti_alarm();
    }
    if (p & (1u << 11))
        fm1_br_iack(1u << 11);
    if (p & 4u)
        fm1_br_iack(4u);
    if (p & 0x0C05u)
        bti_st.other40++;
}

void bti_slot_irq(void)                            /* IRQ 41 (BR §6.3): timer 0, every 1,600 slots during 1a */
{
    uint32_t p = fm1_br_stim_pending();
    bti_st.irq41++;
    fm1_br_stim_ack(p);
    if (p & 1u) {
        bti_st.slot0++;
        bti.next_slot0 = (bti.next_slot0 + 1600u) & BTI_CLK;
        if (bti_st.phase == BTI_CLK_RUN)
            fm1_br_stim_set(0, bti.next_slot0);
    }
}

static void bti_attach(void)                       /* at boot, IRQs off (main.c) */
{
    fm1_br_irq_attach(BTI_IRQ_PRIO);
}

/* ---- 1a */
static int bti_clk_start(void)
{
    uint32_t keep_rf = bti_st.rf_up, i;
    if (bti_st.phase == BTI_CLK_RUN || bti_st.phase == BTI_INQ_RUN)
        return 0;
    for (i = 0; i < sizeof bti_st / 4u; i++)
        ((uint32_t *)&bti_st)[i] = 0;
    bti_st.magic = BTI_MAGIC;
    bti_st.rf_up = keep_rf;
    if (!bti_radio_up())
        return 0;
    bti_bredr_up();
    bti_st.phase = BTI_CLK_RUN;
    bti_st.ms0 = fm1_ms;
    bti_st.clk0 = bti_clkn();
    bti.next_slot0 = (bti_st.clk0 + 1600u) & BTI_CLK;
    fm1_br_stim_set(0, bti.next_slot0);
    bti_alarm_arm(18u);
    return 1;
}

static void bti_clk_end(void)
{
    uint32_t f;
    fm1_br_alarm_off();
    fm1_br_stim_all_off();
    bti_st.ms1 = fm1_ms;
    bti_st.clk1 = bti_clkn();
    f = fm1_br_fine();
    if (f > bti_st.fine_max)
        bti_st.fine_max = f;
    if (bti_st.phase == BTI_CLK_RUN)
        bti_st.phase = BTI_CLK_DONE;
}

static void bti_kv(const char *k, uint32_t v)
{
    con_puts(k);
    con_dec((int32_t)v);
}

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

/* ---- 1b (BR §8.1 start, §14 1b) */
static void bti_frame_defaults(struct fm1_br_frame *f)   /* BR §4 "allocator default" */
{
    fm1_br_frame_clear(f);
    f->linkcntl[0] = 0x4010u;
    f->linkcntl[1] = 0x4010u;
    f->escorxstat[0] = 0x83u;
    f->escorxstat[1] = 0x83u;
    f->rxmaxbuf = 0x300u;
    f->txphdr[0] = 0x6007u;
    f->txphdr[1] = 0x6007u;
    f->aclprio = 0x1414u;
    f->escoprio = 0x1D1Du;
    f->bufcntl = 0x202Cu;
    f->pll_comp = 0;                               /* (no stored crystal offset: BR §4, BLE-HW-FACTS §14.5) */
    f->rxgain0 = 15u;
    f->txpwer = 16u + 11u;                         /* level 11, stock V15's (BR §11) */
    f->txset = 16u + 11u;
}

static int bti_inq_start(uint32_t slots)
{
    struct fm1_br_frame *f = fm1_br_frame(bti_tab, 0);
    uint8_t bch[5];
    uint32_t i;
    if (bti_st.phase == BTI_CLK_RUN || bti_st.phase == BTI_INQ_RUN)
        return 0;
    if (!bti_radio_up())
        return 0;
    bti_bredr_up();
    fm1_br_irqs_hold(1);
    bti_frame_defaults(f);
    fm1_br_eir_rx(FM1_BR_OFF_EIR, 1);              /* BR §8.1 step 2 */
    f->wincntl = 70u;                              /* step 3 */
    f->lmprxptr[0] = FM1_BR_OFF_FHS;
    f->lmprxptr[1] = FM1_BR_OFF_FHS;
    f->rxptr[0] = FM1_BR_OFF_FHS;
    f->rxptr[1] = FM1_BR_OFF_FHS;
    f->clk_off_lo = 0;                             /* step 4 */
    f->clk_off_hi = 0;
    f->bdaddr[0] = (uint16_t)(BTI_GIAC & 0xFFFFu);
    f->bdaddr[1] = (uint16_t)(BTI_GIAC >> 16);     /* (UAP 0: the spec's DCI) */
    f->bdaddr[2] = 0;
    bti_parity(BTI_GIAC, bch);
    f->bch_lo = (uint16_t)(bch[0] | bch[1] << 8);
    f->bch_mid = (uint16_t)(bch[2] | bch[3] << 8);
    f->bch_hi = bch[4];
    bti.train = 0;
    f->format = 8u;                                /* step 5: inquiry, train A */
    f->linkcntl[f->txtog & 1u] = 0x4000u;          /* ID */
    f->rxbufcntl[0] = 0;
    f->rxbufcntl[1] = 0;
    for (i = 0; i < 8u; i++)
        bti_st.hist_key[i] = bti_st.hist_n[i] = 0;
    bti_st.hist_other = bti_st.fhs = bti_st.eir = bti_st.dropped = bti_st.trains = 0;
    bti_st.rx_irqs = bti_st.irq40 = bti_st.alarms = 0;
    bti_rr = bti_rw;
    bti.nseen = 0;
    bti.want_eir = 0;
    bti.last_rx_clkn = 0xFFFFFFFFu;
    bti.inq_slots = slots;
    bti.train_clkn = bti_clkn();
    bti.end_clkn = (bti.train_clkn + slots) & BTI_CLK;
    bti.sec_ms = fm1_ms;
    bti.sec_clk = bti.train_clkn;
    bti_st.phase = BTI_INQ_RUN;
    for (i = 0; i < 16u; i++)                      /* even entries open (BR §3.1, flag 0) */
        fm1_br_xchg(bti_tab, i, FM1_BR_OFF_FRAME | ((i & 1u) ? 1u << 12 : 0u));
    bti_alarm_arm(18u);
    fm1_br_irqs_hold(0);
    return 1;
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

static int bti_new(const uint8_t a[6])
{
    uint32_t i;
    for (i = 0; i < bti.nseen; i++)
        if (ble_eq(bti.seen[i], a, 6))
            return 0;
    if (bti.nseen < 16u)
        ble_cpy(bti.seen[bti.nseen++], a, 6);
    return 1;
}

static void bti_rssi(uint32_t agc)                 /* BR §10.3: -1 - bits 5:0, and the gain index (table not transcribed) */
{
    con_puts(" rssi_raw=0x");
    con_hex(agc & 0x3FFFu, 4);
    con_puts(" (");
    con_dec(-1 - (int32_t)(agc & 63u));
    bti_kv(" g", (agc >> 8) & 63u);
    con_puts(")");
}

static void bti_print_rx(const struct bti_rx *r)
{
    if (r->kind == 1u) {
        struct bti_fhs fh;
        bti_fhs_parse(r->data, &fh);
        ble_cpy(bti.last_addr, fh.addr, 6);
        if (!bti_new(fh.addr))
            return;
        con_puts("btinq: FHS ");
        bti_addr(fh.addr);
        con_puts(" cod=0x");
        con_hex(fh.cod, 6);
        bti_kv(" sr=", fh.sr);
        con_puts(" clk=0x");
        con_hex(fh.clk, 7);
        con_puts(" dclk=");                        /* remote CLK - our RX CLKN, slots, 28-bit signed (BR §6.1) */
        con_dec((int32_t)(((fh.clk << 1) - r->clkn) << 4) >> 4);
        bti_rssi(r->agc);
        bti_kv(" eir=", fh.eir);
        con_puts("\r\n");
    } else {
        char name[33];
        uint32_t n = bti_eir_name(r->data, r->len, name, sizeof name);
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
    }
}

static void bti_print_sec(void)
{
    uint32_t now = bti_clkn(), i;
    bti_kv("btinq: 1s clk+", (now - bti.sec_clk) & BTI_CLK);
    bti.sec_clk = now;
    bti_kv(" irq40=", bti_st.irq40);
    bti_kv(" rx=", bti_st.rx_irqs);
    bti_kv(" alm=", bti_st.alarms);
    con_puts(" stat{");
    for (i = 0; i < 8u && bti_st.hist_n[i]; i++) {
        con_hex(bti_st.hist_key[i], 2);
        bti_kv(":", bti_st.hist_n[i]);
        con_putc(' ');
    }
    bti_kv("other:", bti_st.hist_other);
    bti_kv("} fhs=", bti_st.fhs);
    bti_kv(" eir=", bti_st.eir);
    bti_kv(" drop=", bti_st.dropped);
    con_puts(" train=");
    con_putc(bti.train ? 'B' : 'A');
    con_puts("\r\n");
}

static void bti_poll(void)                         /* the main loop */
{
    static uint8_t boot_done, done_printed;
    if (!boot_done && fm1_ms > 1500u) {            /* 1a once at boot (nothing transmitted) */
        boot_done = 1;
        bti_clk_start();
    }
    if (bti_st.phase == BTI_CLK_RUN && fm1_ms - bti_st.ms0 >= 3000u) {
        bti_clk_end();
        bti_clk_print();                           /* (shown only with a terminal open: `btclk` again) */
    }
    while (bti_rr != bti_rw) {
        RING_PUBLISH();
        bti_print_rx(&bti_ring[bti_rr % BTI_RING]);
        RING_PUBLISH();
        bti_rr++;
    }
    if (bti_st.phase == BTI_INQ_RUN) {
        done_printed = 0;
        if (fm1_ms - bti.sec_ms >= 1000u) {
            bti.sec_ms += 1000u;
            bti_print_sec();
        }
    } else if (bti_st.phase == BTI_INQ_DONE && !done_printed) {
        done_printed = 1;
        bti_print_sec();
        bti_kv("btinq: done ", bti.inq_slots * 5u / 8u);
        bti_kv(" ms, devices=", bti.nseen);
        con_puts("\r\n");
    }
}

/* ---- the console: btclk [run], btinq [SECONDS | stop] */
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

static void con_btinq(const char *p)
{
    uint32_t s;
    int ok = 0;
    if (con_word(&p, "stop")) {
        fm1_br_irqs_hold(1);
        bti_inq_stop();
        fm1_br_irqs_hold(0);
        return;
    }
    s = con_num(&p, &ok);
    if (!bti_inq_start(ok && s >= 1u && s <= 120u ? s * 1600u : 16384u)) {   /* default 10.24 s (the spec's) */
        con_puts("btinq: not started (busy, or no RF trims: 'btclk')\r\n");
        return;
    }
    bti_kv("btinq: inquiry (GIAC) for ", bti.inq_slots * 5u / 8u);
    con_puts(" ms: make a device discoverable\r\n");
}
