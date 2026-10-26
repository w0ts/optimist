/* SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0 */
/* EXPERIMENTAL Bluetooth Classic (BR/EDR) inquiry (builder item BT_AUDIO, FELUCCA_BT_AUDIO): stage 1 of our own
 * Classic stack, docs/BT-CLASSIC.md §14. Written only from that fact sheet (cited as "BR §n") and
 * the Core specification; no vendor code. Included by io/console.c with the CDC console (USB_CONSOLE), else by
 * felucca.c; the radio carries BLE and BR/EDR at the same time (one start-up, ble/rf_share.h; the arbitration: §9.1 and
 * BTI_ACL_PRIO below). The registers are hal/fm1_bredr.h's.
 *
 *   1a  the BR/EDR block's clock and interrupts, by `btclk run` (the console): the radio's start-up
 *       with the stored trims (hal/fm1_ble.h, the VM only), the BR/EDR init (BR §10.2), then 3 s of the slot clock
 *       (expect +1,600 a second), the 18-slot alarm (IRQ 40 bit 9) and slot timer 0 every 1,600 slots (IRQ 41).
 *       Nothing is transmitted. `btclk` prints the result (bti_st: the emulator check reads it)
 *   1b  `btinq [SECONDS] [OPTIONS]`: an inquiry with the GIAC (BR §8.1; default 10.24 s): one frame, procedure 8, ID
 *       trains by the engine, FHS and EIR responses printed as `btinq:` lines, a status line a second
 *   1c  `btscan [SECONDS] [OPTIONS]`: an inquiry scan as stock V15 runs it (BR §8.2, its frame as measured): a phone or
 *       a computer searching finds "OPTIMIST BT" (a random address each boot); the IDs heard are counted
 *
 *   OPTIONS (the hardware's unknowns, BR §12): even | odd | all (the exchange entries open), agc=N (a fixed RX gain
 *   step 0-15; default: start at 8 and follow each packet's RSSI, as the library), win=N (the receive window),
 *   frq=+-N (every hop table entry moved by N channels), pll=+-N (the carrier offset, in the library's crystal units),
 *   sweep (pll through the library's 9 steps, one a 1.28 s), pwr=N (TX level 0-11), ble (the LE block's enable too, as
 *   stock V15 runs it beside the BR/EDR one)
 *   `bttrace [N]` prints the first interrupts of the last run (pending bits, buffers, hop indexes, RX status, RSSI).
 *
 * The interrupts copy what came into rings and re-arm; the main loop (bti_poll) decodes and prints (bt_inq_con.c:
 * the options, the printing and the console commands). Nothing here
 * writes flash. */
#include "fm1_ble.h"                               /* the radio's start-up (fm1_ble_rf_init), the LE block's init */
#include "fm1_bredr.h"                             /* the BR/EDR block */
#include "../ble/rf_share.h"                       /* the radio started once, shared with BLE (ble/ble_hw_wl82.c) */
#if !FELUCCA_BLE
#include "../ble/ble_vm.c"                         /* the stored trims in stock V15's VM (read only) */
#endif
#include "bt_inq_core.c"

#if !FELUCCA_FLASH
#error "BT_AUDIO reads the RF trims from flash (FELUCCA_FLASH)"
#endif

#if !FELUCCA_BLE
/* fm1_ble.h carries the BLE driver's two interrupt entries; this build never attaches them */
void ble_wl82_event_irq(void) {}
void ble_wl82_rx_irq(void) {}
#endif

#define BTI_IRQ_PRIO 2u                            /* at boot: the library's (BR §9.3), below the audio (3) */
#define BTI_RUN_PRIO 5u                            /* during a run (prio=N): above the audio (3) and TIMER5 (4), so a
                                                    * page answer is reprogrammed within the slot (BR §6.5) */
#define BTI_CLK FM1_BR_CLK_MASK
/* the radio's arbitration between BLE and BR/EDR (docs/BT-CLASSIC.md §9.1): each engine asks for the radio with a priority
 * word, the higher one wins a slot both want [I: stock V15's values, never measured against each other]. Stock's order is
 * kept, so the two stacks run side by side as stock's would: eSCO 29 > a BLE connection event 28 (30 around an instant)
 * > BLE advertising 26 > BR/EDR ACL (A2DP) 20 > BLE advertising in sniff 19 > BLE scanning 17 (ble_hw_wl82.c cb_rfprio).
 * A BLE connection event is short (a packet or two each 7.5-30 ms) and has no second chance before the next interval;
 * an ACL packet lost to it is sent again by the baseband's ARQ and the SBC ring has room, so BLE MIDI goes first. The
 * scan for MIDI devices (17) gives way to the audio. */
#define BTI_ACL_PRIO 20u                           /* (a byte each for the two buffers: 0x1414) */
#define BTI_ESCO_PRIO 29u                          /* (unused: no SCO / eSCO link) */

/* the table (BR §3), then our own TX buffers inside the engine's 32 KB window */
#define BTI_OFF_FHS_TX 4200u                       /* the inquiry scan's FHS payload (18 B) */
#define BTI_OFF_EIR_TX 4224u                       /* its EIR (25 B as stock's) */
#define BTI_TAB_ALLOC (4480u + 2u * 344u)          /* (+ bt_page.c's two DH5 TX buffers, BP_OFF_TXB0 / 1) */
static uint8_t bti_tab[BTI_TAB_ALLOC] IN_POOL __attribute__((aligned(4)));   /* zeroed (the pool); the engine's DMA */
static uint8_t bti_le_bb[64] IN_POOL __attribute__((aligned(4)));    /* the LE block's RAM window, no links (`ble`) */

/* ---- what the probe saw (the emulator check reads the first 17 words: tests/bt_inq_emu_test.py) */
enum { BTI_IDLE, BTI_CLK_RUN, BTI_CLK_DONE, BTI_INQ_RUN, BTI_INQ_DONE, BTI_NO_TRIMS = 0xE1, BTI_LATCH_HUNG = 0xE2 };
#define BTI_MAGIC 0x31495442u                      /* "BTI1" */
#define BTI_NH 8u
struct bti_stat {
    uint32_t magic, phase, rf_up;
    uint32_t clk0, clk1, ms0, ms1, fine_max;       /* 1a: CLKN and ms at the start and the end */
    uint32_t irq40, alarms, rx_irqs, other40;      /* IRQ 40: entries, bit 9, bit 1, the other known bits */
    uint32_t unknown40, irq41, slot0, latch_spins; /* unknown pending bits seen (OR), IRQ 41 entries, timer 0 */
    uint32_t alarm_late;                           /* the largest CLKN at the alarm minus its slot */
    uint32_t fhs, eir, dropped, trains, ids;       /* 1b / 1c */
    uint32_t rs_key[BTI_NH], rs_n[BTI_NH], rs_other;   /* RX status words (all 16 bits) seen and their counts */
    uint32_t pd_key[BTI_NH], pd_n[BTI_NH], pd_other;   /* IRQ 40 pending words seen and their counts */
};
static struct bti_stat bti_st = {BTI_MAGIC};

/* the interrupts' ring: one FHS, EIR or ID each */
#define BTI_RING 32u                               /* (8 overflowed: the AirPods' SDP burst while the console
                                                    * printed; a packet acknowledged on air and dropped here is lost for
                                                    * good, bti_st.dropped, 2026-10-20) */
enum { BTI_K_FHS = 1, BTI_K_EIR = 2, BTI_K_ID = 3, BTI_K_PSTEP = 4, BTI_K_LMP = 5, BTI_K_CONN = 6, BTI_K_ACL = 7 };
struct bti_rx {
    uint8_t kind, type, idx, rxfreq;
    uint32_t clkn, stat, agc, len, rssi1;
    uint8_t data[340];                             /* (a DH5's 339 B whole: our features say 5-slot, bt_lmp.c) */
};
static struct bti_rx bti_ring[BTI_RING] IN_POOL;
static volatile uint32_t bti_rw, bti_rr;

/* the first interrupts of a run (bttrace) */
#define BTI_TRACE 96u
struct bti_tr {
    uint32_t t, pend;                              /* TIMER4 ticks, IRQ 40 pending */
    uint16_t rxstat[2], rxphdr[2], linkcntl[2], rssi[4], rx_clkn_lo, rxbit;
    uint8_t rxtog, txtog, txfreq, rxfreq, txbufcntl[2], rxbufcntl[2];
};
#if FELUCCA_BT_DIAG
static struct bti_tr bti_trace[BTI_TRACE] IN_POOL;
#endif
static volatile uint32_t bti_tn;

enum { BTI_M_INQ, BTI_M_SCAN, BTI_M_PAGE, BTI_M_CONN };

/* every reception of a run (btrx): the first BTI_RXLOG */
#define BTI_RXLOG 128u
struct bti_rxl {
    uint32_t t;                                    /* TIMER4 ticks */
    uint16_t stat, hdr, rxbit, clkn;
    int16_t pll;
    uint8_t mode, sub, idx, rxfreq;
};
#if FELUCCA_BT_DIAG
static struct bti_rxl bti_rxlog_buf[BTI_RXLOG] IN_POOL;
#endif
static volatile uint32_t bti_rxn;
static struct {
    uint32_t end_clkn, next_slot0, train_clkn, last_rx_clkn, inq_slots, sec_ms, sec_clk, alarm_at, sweep_clkn;
    uint8_t bredr_up, want_eir, train, nseen, mode, agc, sweep_i, le_up;
    uint8_t seen[16][6], last_addr[6], own[6];
    int32_t seen_clk[16];                          /* their clock offsets (remote CLK - our CLKN, slots): btpage */
    /* options */
    uint8_t o_pat, o_agc, o_sweep, o_pwr, o_ble, o_prio, cur_prio, o_noclk, o_pcon, o_ppar;
    int16_t o_pclk, o_padj;
    int8_t o_frq;
    int16_t o_pll, pll_now;
    uint16_t o_win;
} bti;
/* the run's options as the console has them by default (bt_inq_con.c bti_opts starts from these). The screen's scan and
 * connect (bt_audio.c) set them too: before, they ran on the zeroed struct, TX power 0, the gain fixed at 0, the
 * interrupt at priority 0 under the audio's (the user, 2026-10-20: the AirPods answered the page, never our FHS) */
static void bti_defaults(void)
{
    bti.o_pat = 0, bti.o_agc = 0xFFu, bti.o_win = 0, bti.o_frq = 0, bti.o_pll = 0, bti.o_sweep = 1, bti.o_pwr = 11u;
    bti.o_ble = 1, bti.o_prio = BTI_RUN_PRIO, bti.o_noclk = 0, bti.o_pcon = 0, bti.o_pclk = 0, bti.o_ppar = 0xFFu, bti.o_padj = 0;
}
static const int8_t bti_sweep_tb[9] = {-120, -80, -40, -20, 0, 20, 40, 80, -120};   /* BR §7.1 (the library's steps) */

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

static void bti_count(uint32_t *key, uint32_t *n, uint32_t *other, uint32_t v)   /* (BT_DIAG: the status histograms) */
{
    uint32_t i;
    if (!FELUCCA_BT_DIAG)
        return;
    for (i = 0; i < BTI_NH; i++) {
        if (n[i] && key[i] == v) {
            n[i]++;
            return;
        }
        if (!n[i]) {
            key[i] = v;
            n[i] = 1;
            return;
        }
    }
    (*other)++;
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
    if (rfs_is_up()) {                             /* (BLE started it, with the same stored trims: not started again
                                                    * under BLE's link; rf_share.h) */
        bti_st.rf_up = 1;
        return 1;
    }
    if (!ble_vm_scan(bti_vm_rd, 0, &info, &t, 0, 0)) {
        bti_st.phase = BTI_NO_TRIMS;
        return 0;
    }
#if FELUCCA_DUAL >= 2
    dual_radio_hold();                             /* (CPU1 not running while the radio's start-up program runs, as BLE's:
                                                    * system/dual.c; unheld, CPU1 did not come back, 2026-10-20) */
#endif
    (void)rfs_rf_up(t.x106, t.x107, t.x108, t.x187);
#if FELUCCA_DUAL >= 2
    dual_radio_release();
#endif
    bti_st.rf_up = 1;
    return 1;
}

static void bti_bredr_up(void)
{
    uint32_t i;
    if (bti.bredr_up)
        return;
    for (i = 0; i < BTI_TAB_ALLOC; i++)
        bti_tab[i] = 0;
    fm1_br_init(bti_tab);
    fm1_br_irqs_hold(0);
    bti.bredr_up = 1;
}

static void bti_le_up(void)                        /* the LE block enabled as stock V15 does after BR/EDR (BLE-HW-FACTS §5.5), no link;
                                                    * with BLE up it is BLE's (enabled, its window, its interrupts): left as it is */
{
    uint32_t i;
    if (bti.le_up)
        return;
    for (i = 0; i < sizeof bti_le_bb; i++)
        bti_le_bb[i] = 0;
    (void)rfs_le_br((uint32_t)(uintptr_t)bti_le_bb, sizeof bti_le_bb);
    bti.le_up = 1;
}

static void bti_frq_table(void)                    /* frq_table[0] (BR §7.1), moved by frq=N */
{
    uint32_t i;
    for (i = 0; i < 79u; i++) {
        int32_t ch = (int32_t)(i < 40u ? 2u * i : 2u * (i - 40u) + 1u) + bti.o_frq;
        bti_tab[FM1_BR_OFF_FRQ + i] = (uint8_t)(ch < 0 ? 0 : ch > 80 ? 80 : ch);
    }
}

/* ---- interrupts */
static void bti_trace_add(uint32_t pend)
{
#if FELUCCA_BT_DIAG
    struct fm1_br_frame *f = fm1_br_frame(bti_tab, 0);
    struct bti_tr *t;
    uint32_t i;
    if (bti_tn >= BTI_TRACE)
        return;
    t = &bti_trace[bti_tn];
    t->t = fm1_ticks();
    t->pend = pend;
    for (i = 0; i < 2u; i++) {
        t->rxstat[i] = f->rxstat[i];
        t->rxphdr[i] = f->rxphdr[i];
        t->linkcntl[i] = f->linkcntl[i];
        t->txbufcntl[i] = f->txbufcntl[i];
        t->rxbufcntl[i] = f->rxbufcntl[i];
    }
    for (i = 0; i < 4u; i++)
        t->rssi[i] = f->rssi[i];
    t->rx_clkn_lo = f->rx_clkn_lo;
    t->rxbit = f->rxbit;
    t->rxtog = f->rxtog;
    t->txtog = f->txtog;
    t->txfreq = f->txfreq;
    t->rxfreq = f->rxfreq;
    bti_tn++;
#else
    (void)pend;
#endif
}

static void bti_push(uint32_t kind, uint32_t idx, uint32_t stat, uint32_t clkn, const uint8_t *src, uint32_t len)
{
    struct fm1_br_frame *f = fm1_br_frame(bti_tab, 0);
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
    r->rssi1 = f->rssi[1];
    r->rxfreq = f->rxfreq;
    for (i = 0; i < len; i++)
        r->data[i] = src[i];
    RING_PUBLISH();
    bti_rw++;
}

static int bti_is_data(uint32_t type)              /* DM1 DH1 DM3 DH3 DM5 DH5: an EIR's (BR §8.1) */
{
    return type == 3u || type == 4u || type == 10u || type == 11u || type == 14u || type == 15u;
}

static void bti_agc_follow(struct fm1_br_frame *f)   /* BR §4 RSSI1: step up below 800, down above 1,100 */
{
    uint32_t r = f->rssi[1];
    if (bti.o_agc != 0xFFu)
        return;
    if (r < 800u && bti.agc < 15u)
        bti.agc++;
    else if (r > 1100u && bti.agc > 0)
        bti.agc--;
    f->rxgain0 = bti.agc;
}

static void bti_rxlog(uint32_t idx, uint32_t stat)   /* one reception into btrx's log (BT_DIAG) */
{
#if FELUCCA_BT_DIAG
    struct fm1_br_frame *f = fm1_br_frame(bti_tab, 0);
    struct bti_rxl *l;
    if (bti_rxn >= BTI_RXLOG)
        return;
    l = &bti_rxlog_buf[bti_rxn++];
    l->t = fm1_ticks();
    l->stat = (uint16_t)stat;
    l->hdr = f->rxphdr[idx];
    l->rxbit = f->rxbit;
    l->clkn = f->rx_clkn_lo;
    l->pll = bti.pll_now;
    l->mode = bti.mode;
    l->sub = 0;
    l->idx = (uint8_t)idx;
    l->rxfreq = f->rxfreq;
#else
    (void)idx, (void)stat;
#endif
}

static void bp_rx(void);
static int bp_alarm(uint32_t now);

static void bti_rx_frame(void)                     /* BR §8.1 "On IRQ 40 RX": the filled buffer is rxtog ^ 1 */
{
    struct fm1_br_frame *f = fm1_br_frame(bti_tab, 0);
    uint32_t idx = (f->rxtog & 1u) ^ 1u, stat = f->rxstat[idx], type = stat & 15u, len;
    uint32_t clkn = f->rx_clkn_lo | (uint32_t)f->rx_clkn_hi << 16;
    if (clkn == bti.last_rx_clkn)
        return;                                    /* (this frame has nothing new: BR §6.2) */
    bti.last_rx_clkn = clkn;
    bti_count(bti_st.rs_key, bti_st.rs_n, &bti_st.rs_other, stat);
    bti_rxlog(idx, stat);
    if ((stat >> 8) == 0x8Fu)
        bti_agc_follow(f);
    if (bti.mode == BTI_M_SCAN) {                  /* an inquiry scan hears IDs (BR §8.2); the engine answers */
        if ((stat >> 8) == 0x8Fu && type == 0) {
            bti_st.ids++;
            if (bti_st.ids <= 8u)
                bti_push(BTI_K_ID, idx, stat, clkn, 0, 0);
        }
        return;
    }
    f->linkcntl[idx] &= (uint16_t)~(1u << 14);
    if ((stat >> 8) == 0x8Fu && type == 2u) {      /* an FHS (BR §5.4) */
        bti_push(BTI_K_FHS, idx, stat, clkn, bti_tab + FM1_BR_OFF_FHS, 18u);
        bti_st.fhs++;
        bti.want_eir = (uint8_t)((bti_tab[FM1_BR_OFF_FHS + 7] >> 2) & 1u);   /* its EIR bit (BR §8.4) */
        if (!bti.want_eir)
            fm1_br_eir_arm(0);
    } else if ((stat >> 8) == 0x8Fu && bti.want_eir && bti_is_data(type)) {
        len = (f->rxphdr[idx] >> 3) & 0x3FFu;      /* (BR §4 rxphdr: [12:3] length) */
        bti_push(BTI_K_EIR, idx, stat, clkn, bti_tab + FM1_BR_OFF_EIR, len > 240u ? 240u : len);
        bti_st.eir++;
        bti.want_eir = 0;
        fm1_br_eir_arm(0);
        fm1_br_con_or(1u << 11);
    } else if ((stat >> 8) != 0x8Fu) {
        bti_push(BTI_K_ID, idx, stat, clkn, 0, 0);   /* (anything else: shown as heard, with its status) */
    }
    f->rxbufcntl[idx] &= (uint8_t)~1u;             /* re-armed (BR §5.5) */
    f->linkcntl[idx] |= 1u << 14;
}

static void bti_run_stop(void)                     /* (from the alarm or the console) */
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

static void bti_pll(int32_t units)                 /* BR §4 PLL_COMP = units x 2^20 / 24,000 */
{
    struct fm1_br_frame *f = fm1_br_frame(bti_tab, 0);
    bti.pll_now = (int16_t)units;
    f->pll_comp = (uint32_t)(units * 1048576 / 24000);
}

static void bti_alarm(void)                        /* the 18-slot housekeeping (BR §6.4, §8.1) */
{
    uint32_t now = bti_clkn(), late = (now - bti.alarm_at) & BTI_CLK;
    if (late < 0x100000u && late > bti_st.alarm_late)
        bti_st.alarm_late = late;
    if (bti_st.phase == BTI_INQ_RUN) {
        struct fm1_br_frame *f = fm1_br_frame(bti_tab, 0);
        if (bti.o_sweep) {                         /* the library's sweep: the next step at every 18-slot alarm (BR §7.1) */
            bti.sweep_i = (uint8_t)((bti.sweep_i + 1u) % 9u);
            bti_pll(bti.o_pll + bti_sweep_tb[bti.sweep_i]);
        }
        if (bti.mode >= BTI_M_PAGE) {
            bp_alarm(now);
            return;
        }
        if (bti.mode == BTI_M_INQ && ((now - bti.train_clkn) & BTI_CLK) >= 4096u) {   /* A/B every 2.56 s (BR §7.1) */
            bti.train_clkn = now;
            bti.train ^= 1u;
            f->format = (uint8_t)((f->format & 0x7Fu) | bti.train << 7);
            bti_st.trains++;
        }
        if (bti.mode == BTI_M_INQ) {
            bti.want_eir = 0;                      /* (an EIR not come by now will not) */
            fm1_br_eir_arm(1);
        }
        if (((bti.end_clkn - now) & BTI_CLK) > (BTI_CLK >> 1)) {   /* past the end */
            bti_run_stop();
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
    if (bti_st.phase == BTI_INQ_RUN) {
        bti_count(bti_st.pd_key, bti_st.pd_n, &bti_st.pd_other, p);
        if (p != (1u << 9))
            bti_trace_add(p);
    }
    bti_st.unknown40 |= p & ~(uint32_t)FM1_BR_IPND_KNOWN;
    if (p & ~(uint32_t)FM1_BR_IPND_KNOWN)          /* (acknowledged too: an unacknowledged bit would hold the line) */
        fm1_br_iack(p & ~(uint32_t)FM1_BR_IPND_KNOWN & 0xFFFFu);
    if (p & 1u)
        fm1_br_iack(1u);
    if (p & 2u) {
        fm1_br_iack(2u);
        bti_st.rx_irqs++;
        if (bti_st.phase == BTI_INQ_RUN) {
            if (bti.mode >= BTI_M_PAGE)
                bp_rx();
            else
                bti_rx_frame();
        }
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
    bti.cur_prio = BTI_IRQ_PRIO;
}

/* ---- 1a */
static int bti_busy(void) { return bti_st.phase == BTI_CLK_RUN || bti_st.phase == BTI_INQ_RUN; }

static int bti_clk_start(void)
{
    uint32_t keep_rf = bti_st.rf_up, i;
    if (bti_busy())
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

/* ---- 1b, 1c (BR §8.1, §8.2, §14) */
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
    f->aclprio = (uint16_t)(BTI_ACL_PRIO * 0x101u);  /* 0x1414 */
    f->escoprio = (uint16_t)(BTI_ESCO_PRIO * 0x101u);   /* 0x1D1D */
    f->bufcntl = 0x202Cu;
    bti.agc = bti.o_agc == 0xFFu ? 8u : bti.o_agc;   /* (the library's first step: 8) */
    f->rxgain0 = bti.agc;
    f->txpwer = 16u + bti.o_pwr;                   /* (level 11: stock V15's, BR §11) */
    f->txset = 16u + bti.o_pwr;
}

static void bti_giac(struct fm1_br_frame *f)       /* the GIAC's access code (BR §5.3), UAP 0 (the spec's DCI) */
{
    uint8_t bch[5];
    f->bdaddr[0] = (uint16_t)(BTI_GIAC & 0xFFFFu);
    f->bdaddr[1] = (uint16_t)(BTI_GIAC >> 16);
    f->bdaddr[2] = 0;
    bti_parity(BTI_GIAC, bch);
    f->bch_lo = (uint16_t)(bch[0] | bch[1] << 8);
    f->bch_mid = (uint16_t)(bch[2] | bch[3] << 8);
    f->bch_hi = bch[4];
}

static const uint8_t bti_eir_tx[25] = {12, 0x09, 'O', 'P', 'T', 'I', 'M', 'I', 'S', 'T', ' ', 'B', 'T'};   /* + zeros */

static void bti_own_addr(void)                     /* our address: made once (LAP outside 0x9E8B00-3F), then kept in the
                                                    * settings record (bta.own: a bonded device knows us by it) */
{
    uint32_t r, i;
    if (bti.own[0] | bti.own[1] | bti.own[2])
        return;
    if (bta.own_set) {
        for (i = 0; i < 6u; i++)
            bti.own[i] = bta.own[i];
        return;
    }
    do
        r = fm1_ble_rng32();
    while (((r & 0xFFFFC0u) == 0x9E8B00u) || (r & 0xFFFFFFu) == 0);
    bti.own[0] = (uint8_t)r, bti.own[1] = (uint8_t)(r >> 8), bti.own[2] = (uint8_t)(r >> 16);
    r = fm1_ble_rng32();
    bti.own[3] = (uint8_t)r, bti.own[4] = (uint8_t)(r >> 8), bti.own[5] = (uint8_t)((r >> 16) & 0xFCu);
    for (i = 0; i < 6u; i++)
        bta.own[i] = bti.own[i];
    bta.own_set = 1;
    bta.key_new = 1;                               /* (kept from now: the settings record) */
}

/* the common start of a run (the interrupts held when it returns 1): the radio, the block, the priority, the frame */
static int bti_prepare(uint32_t mode)
{
    struct fm1_br_frame *f = fm1_br_frame(bti_tab, 0);
    if (bti_busy())
        return 0;
    if (!bti_radio_up())
        return 0;
    bti_bredr_up();
    if (bti.o_ble)                                 /* the LE block's enable (BR §14: RX much better with it; stock V15
                                                    * writes it after the BR/EDR init). Once on, on until the next boot:
                                                    * `noble` on the first run after a boot measures without it */
        bti_le_up();
    fm1_br_irqs_hold(1);
    if (bti.cur_prio != bti.o_prio) {              /* (the priority field only, the interrupts off) */
        fm1_irq_off();
        fm1_br_irq_prio(bti.o_prio);
        fm1_irq_on();
        bti.cur_prio = bti.o_prio;
    }
    bti_frq_table();
    bti_frame_defaults(f);
    bti.mode = (uint8_t)mode;
    f->clk_off_lo = 0;
    f->clk_off_hi = 0;
    f->rxbufcntl[0] = 0;
    f->rxbufcntl[1] = 0;
    return 1;
}

/* the common end of a start: the counters, the offset, the clock marks, the exchange entries, the alarm, go */
static void bti_go(uint32_t slots)
{
    uint32_t i, open;
    for (i = 0; i < sizeof bti_st / 4u; i++)       /* (keep the 1a figures: the first 17 words) */
        if (i >= 17u)
            ((uint32_t *)&bti_st)[i] = 0;
    bti_st.rx_irqs = bti_st.irq40 = bti_st.alarms = bti_st.other40 = bti_st.unknown40 = 0;
    bti_tn = 0;
    bti_rxn = 0;
    bti_rr = bti_rw;
    if (bti.mode == BTI_M_INQ)
        bti.nseen = 0;
    bti.want_eir = 0;
    bti.train = 0;
    bti.sweep_i = 0;
    bti_pll(bti.o_pll + (bti.o_sweep ? bti_sweep_tb[0] : 0));
    bti.last_rx_clkn = 0xFFFFFFFFu;
    bti.inq_slots = slots;
    bti.train_clkn = bti_clkn();
    bti.sweep_clkn = bti.train_clkn;
    bti.end_clkn = (bti.train_clkn + slots) & BTI_CLK;
    bti.sec_ms = fm1_ms;
    bti.sec_clk = bti.train_clkn;
    bti_st.phase = BTI_INQ_RUN;
    for (i = 0; i < 16u; i++) {                    /* BR §3.1: even (flag 0), odd (1) or all (2) entries open */
        open = bti.o_pat == 2u || (i & 1u) == bti.o_pat;
        fm1_br_xchg(bti_tab, i, FM1_BR_OFF_FRAME | (open ? 0u : 1u << 12));
    }
    bti_alarm_arm(18u);
    fm1_br_irqs_hold(0);
}

static int bti_run_start(uint32_t mode, uint32_t slots)
{
    struct fm1_br_frame *f = fm1_br_frame(bti_tab, 0);
    uint32_t i;
    if (!bti_prepare(mode))
        return 0;
    bti_giac(f);
    if (mode == BTI_M_INQ) {                       /* BR §8.1 steps 2-5 */
        fm1_br_eir_rx(FM1_BR_OFF_EIR, 1);
        f->wincntl = bti.o_win ? bti.o_win : 70u;
        f->lmprxptr[0] = FM1_BR_OFF_FHS;
        f->lmprxptr[1] = FM1_BR_OFF_FHS;
        f->rxptr[0] = FM1_BR_OFF_FHS;
        f->rxptr[1] = FM1_BR_OFF_FHS;
        f->format = 8u;                            /* inquiry, train A */
        f->linkcntl[f->txtog & 1u] = 0x4000u;      /* ID */
    } else {                                       /* BR §8.2: stock V15's inquiry-scan frame as measured */
        struct bti_fhs fh;
        bti_own_addr();
        for (i = 0; i < 6u; i++)
            fh.addr[i] = bti.own[i];
        fh.eir = 1, fh.sr = 1, fh.cod = 0x240404u, fh.lt_addr = 0, fh.clk = 0, fh.psm = 0;
        bti_fhs_build(&fh, bti_tab + BTI_OFF_FHS_TX);
        for (i = 0; i < sizeof bti_eir_tx; i++)
            bti_tab[BTI_OFF_EIR_TX + i] = bti_eir_tx[i];
        fm1_br_eir_tx(sizeof bti_eir_tx, 10u, BTI_OFF_EIR_TX);   /* 25 bytes, DM3, as stock */
        f->txphdr[0] = (uint16_t)(sizeof bti_eir_tx << 3 | 3u);
        f->txphdr[1] = (uint16_t)(sizeof bti_eir_tx << 3 | 3u);
        f->txptr[0] = BTI_OFF_FHS_TX;
        f->txptr[1] = BTI_OFF_FHS_TX;
        f->wincntl = bti.o_win ? bti.o_win : 0x8022u;
        f->format = 11u;                           /* inquiry scan */
        f->linkcntl[0] = 0x4102u;                  /* FHS, an EIR follows */
        f->linkcntl[1] = 0x4112u;
    }
    bti_go(slots);
    return 1;
}

#if FELUCCA_BT_AUDIO
static void bta_saw(const uint8_t a[6], uint32_t cod);   /* bt_audio.c: the screen's device list, fed by the scan */
static void bta_named(const uint8_t a[6], const char *nm);
#endif
#include "bt_inq_con.c"                            /* the options, the printing, the console commands */

#include "bt_page.c"                               /* stage 2: btpage (the page, the link, the minimal LMP) */
#if FELUCCA_BT_AUDIO
#include "bt_audio.c"                              /* the screen's side: scan, pick, reconnect (SYSTEM > Bluetooth AUDIO) */
#endif

static void bti_poll(void)                         /* the main loop */
{
#if FELUCCA_CDC && FELUCCA_BT_DIAG
    static uint8_t done_printed;
#endif
    if (bti_st.phase == BTI_CLK_RUN && fm1_ms - bti_st.ms0 >= 3000u) {
        bti_clk_end();
#if FELUCCA_CDC && FELUCCA_BT_DIAG
        bti_clk_print();                           /* (shown only with a terminal open: `btclk` again) */
#endif
    }
    while (bti_rr != bti_rw) {
        RING_PUBLISH();
        bti_print_rx(&bti_ring[bti_rr % BTI_RING]);
        RING_PUBLISH();
        bti_rr++;
    }
    if (bti.mode >= BTI_M_PAGE)
        bp_poll();
    if (bti_st.phase == BTI_INQ_RUN && fm1_ms - bti.sec_ms >= 1000u) {   /* once a second */
        bti.sec_ms += 1000u;
#if FELUCCA_CDC && FELUCCA_BT_DIAG               /* (the console's lines: with no console they print nothing) */
        if (bti.mode >= BTI_M_PAGE)
            bp_print_sec();
        else
            bti_print_sec();
#endif
        if (bti.mode >= BTI_M_PAGE)
            bp_sec_tick();                         /* (the link's channel assessment, bt_afh.c; its channel line) */
    }
#if FELUCCA_CDC && FELUCCA_BT_DIAG
    if (bti_st.phase == BTI_INQ_RUN) {
        done_printed = 0;
    } else if (bti_st.phase == BTI_INQ_DONE && !done_printed) {
        done_printed = 1;
        if (bti.mode >= BTI_M_PAGE) {
            bp_print_sec();
            con_puts(bti.mode == BTI_M_CONN ? "btpage: the link is closed" : "btpage: page timeout, no answer");
            con_puts(" ('btrx' for every reception)\r\n");
            return;
        }
        bti_print_sec();
        con_puts(bti_tag());
        bti_kv("done ", bti.inq_slots * 5u / 8u);
        if (bti.mode == BTI_M_INQ)
            bti_kv(" ms, devices=", bti.nseen);
        else
            bti_kv(" ms, IDs heard=", bti_st.ids);
        con_puts(" ('bttrace' for the first interrupts, 'btrx' for every reception)\r\n");
    }
#endif
}
