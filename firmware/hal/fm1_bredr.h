/* SPDX-License-Identifier: GPL-3.0-only */
/* FM-1 (AC791N / WL82) BR/EDR (Bluetooth Classic) baseband: the registers, the frame control block, the slot clock,
 * the alarm, the slot timers and the interrupt lines, for the EXPERIMENTAL inquiry probe (src/bt/bt_inq.c,
 * FELUCCA_BT_INQ_PROBE). Written only from the clean-room fact sheet docs/BT-CLASSIC-HW-FACTS.md: every access names
 * its section there as "BR §n". Nothing in this file ran on a real FM-1; the emulator models only the clock, the
 * alarm and the slot timers (fm1-emulator feat/ble-engine wireless.rs), not the engine.
 *
 *   fm1_br_init(table)           the BR/EDR block (BR §10.2) with the 4,196-byte table at `table`
 *   fm1_br_clkn(&spins)          CLKN in slots (BR §6.1)
 *   fm1_br_alarm_arm(n) / _off   IRQ 40 bit 9 at CLKN + n (BR §6.4)
 *   fm1_br_stim_*                slot timer k (IRQ 41, BR §6.3)
 *   fm1_br_irq_attach(prio)      IRQ 40 and 41 to the probe's bodies bti_bt_irq / bti_slot_irq, held */
#pragma once
#include <stdint.h>
#include "fm1_cc.h"
#include "fm1_irq.h"

#define FM1_BR_REG(a) (*(volatile uint32_t *)(a))
#define FM1_BR_CON     FM1_BR_REG(0x20000u)   /* bit8 run; bit31 stop request [I]; bit11 after an EIR (BR §2.1) */
#define FM1_BR_IEN     FM1_BR_REG(0x2000Cu)   /* IRQ 40 enables: bit1 RX, bit9 alarm */
#define FM1_BR_IPND    FM1_BR_REG(0x20010u)   /* IRQ 40 pending */
#define FM1_BR_IACK    FM1_BR_REG(0x20018u)   /* write 1 to acknowledge */
#define FM1_BR_LATCH   FM1_BR_REG(0x2001Cu)   /* write 1, poll until 0, then read FM1_BR_CLKN_R */
#define FM1_BR_CLKN_R  FM1_BR_REG(0x20020u)   /* CLKN[27:1]: slots */
#define FM1_BR_FINE    FM1_BR_REG(0x20024u)   /* us inside the slot */
#define FM1_BR_EIRRX   FM1_BR_REG(0x200C4u)   /* [15:0] EIR RX buffer offset, bit31 armed (BR §2.2) */
#define FM1_BR_ALARM   FM1_BR_REG(0x200E4u)   /* the slot of IRQ 40 bit 9 (write only in the emulator) */
#define FM1_BR_TBASE   FM1_BR_REG(0x2FC44u)   /* the BR/EDR table's address (BR §2.4) */
#define FM1_BR_BTEN    FM1_BR_REG(0x2FC40u)   /* bit10 the BR/EDR block */
#define FM1_BR_STIM    FM1_BR_REG(0x2FD40u)   /* slot timers: [7:0] enable, [15:8] acknowledge, [23:16] pending */
#define FM1_BR_STIM_AT(k) FM1_BR_REG(0x2FD44u + 4u * (k))
#define FM1_BR_AGC3    FM1_BR_REG(0x2FD8Cu)   /* the real-time RSSI readback (BR §10.3) */
#define FM1_BR_CLK_MASK 0x07FFFFFFu           /* 27 bits of slots */
enum { FM1_IRQ_BREDR = 40, FM1_IRQ_BT_SLOT = 41 };   /* BR §9.3 */
#define FM1_BR_IPND_KNOWN 0x0E07u            /* pending bits 0, 1, 2, 9, 10, 11 (BR §6.2) */

/* the table (BR §3): offsets from its base */
#define FM1_BR_TAB_SIZE 4196u
#define FM1_BR_OFF_FRQ 32u                    /* frq_table[0] */
#define FM1_BR_OFF_FRAME 368u                 /* frame n at 368 + 180 n (below 0x800) */
#define FM1_BR_OFF_ESCO 3808u
#define FM1_BR_OFF_FHS 3936u                  /* the 20-byte FHS RX buffer */
#define FM1_BR_OFF_EIR 3956u                  /* the 240-byte EIR RX buffer */

/* the frame control block (BR §4), 180 bytes, read and written by the engine's DMA */
struct fm1_br_frame {
    volatile uint16_t bdaddr[3], bch_lo, bch_mid;
    volatile uint8_t bch_hi, cx, miscntl, format, rxtog, txtog;
    volatile uint16_t wincntl, rxmaxbuf;
    volatile uint8_t txpwr, txfreq, rxrssi, rxfreq;
    volatile uint16_t txptr[2], rxptr[2], linkcntl[2], txphdr[2], rxstat[2], rxphdr[2];
    volatile uint16_t clk_off_lo, clk_off_hi, rx_clkn_lo, rx_clkn_hi, bitoff, rxbit;
    volatile uint16_t cryptk[8], escorxstat[2], lmprxptr[2], aclprio, escoprio, bufcntl;
    volatile uint8_t txbufcntl[2], rxbufcntl[2], rxbufstat[2];
    volatile uint16_t tws[5];
    volatile uint8_t twssta[2];
    volatile uint32_t txpwer, rxgain0, txset, rxset, pll_comp, mdm_set;
    volatile uint16_t rssi[4];
    volatile uint32_t anl_out;
    volatile uint8_t tws_copy[36];
};
_Static_assert(sizeof(struct fm1_br_frame) == 180u && __builtin_offsetof(struct fm1_br_frame, linkcntl) == 32u &&
                   __builtin_offsetof(struct fm1_br_frame, clk_off_lo) == 48u &&
                   __builtin_offsetof(struct fm1_br_frame, aclprio) == 84u &&
                   __builtin_offsetof(struct fm1_br_frame, txbufcntl) == 90u &&
                   __builtin_offsetof(struct fm1_br_frame, txpwer) == 108u &&
                   __builtin_offsetof(struct fm1_br_frame, rssi) == 132u,
               "fm1_br_frame: BR §4's offsets");

FM1_INLINE struct fm1_br_frame *fm1_br_frame(uint8_t *table, uint32_t n)
{
    return (struct fm1_br_frame *)(void *)(table + FM1_BR_OFF_FRAME + 180u * n);
}

FM1_INLINE void fm1_br_frame_clear(struct fm1_br_frame *f)
{
    volatile uint8_t *b = (volatile uint8_t *)(void *)f;
    uint32_t i;
    for (i = 0; i < sizeof *f; i++)
        b[i] = 0;
}

/* exchange table entry i (BR §3.1): [10:0] frame offset, bit12 inhibit */
FM1_INLINE void fm1_br_xchg(uint8_t *table, uint32_t i, uint32_t v)
{
    ((volatile uint16_t *)(void *)table)[i] = (uint16_t)v;
}
FM1_INLINE void fm1_br_xchg_or(uint8_t *table, uint32_t i, uint32_t v)
{
    ((volatile uint16_t *)(void *)table)[i] |= (uint16_t)v;
}

/* BR §6.1: CLKN in slots; *spins the latch polls it took (bounded: a stuck latch returns 0xFFFFFFFF) */
static uint32_t fm1_br_clkn(uint32_t *spins)
{
    uint32_t n = 0;
    FM1_BR_LATCH = 1u;
    while (FM1_BR_LATCH != 0) {
        if (++n > 100000u) {
            *spins = n;
            return 0xFFFFFFFFu;
        }
    }
    *spins = n;
    return FM1_BR_CLKN_R;
}

FM1_INLINE uint32_t fm1_br_ipnd(void) { return FM1_BR_IPND; }               /* BR §6.2 */
FM1_INLINE void fm1_br_iack(uint32_t bits) { FM1_BR_IACK = bits & 0xFFFFu; }
FM1_INLINE uint32_t fm1_br_fine(void) { return FM1_BR_FINE; }
FM1_INLINE uint32_t fm1_br_agc3(void) { return FM1_BR_AGC3; }
FM1_INLINE void fm1_br_con_or(uint32_t bits) { FM1_BR_CON |= bits; }
/* BR §2.2, §8.1: the EIR receive buffer at table offset off, armed (1) or not (0) */
FM1_INLINE void fm1_br_eir_rx(uint32_t off, int armed) { FM1_BR_EIRRX = (off & 0xFFFFu) | (armed ? 0x80000000u : 0u); }
/* BR §2.2, §8.2: the EIR an inquiry scan sends after its FHS: len bytes of packet type `type` at table offset off */
FM1_INLINE void fm1_br_eir_tx(uint32_t len, uint32_t type, uint32_t off)
{
    FM1_BR_REG(0x200C0u) = (len & 0xFFFu) << 20 | (type & 15u) << 16 | (off & 0xFFFFu);
}
FM1_INLINE void fm1_br_eir_arm(int armed)
{
    if (armed)
        FM1_BR_EIRRX |= 0x80000000u;
    else
        FM1_BR_EIRRX &= ~0x80000000u;
}

/* BR §6.4: IRQ 40 bit 9 at slot `at` */
static void fm1_br_alarm_at(uint32_t at)
{
    FM1_BR_IACK = 1u << 9;
    FM1_BR_ALARM = at & FM1_BR_CLK_MASK;
    FM1_BR_IEN |= 1u << 9;
}

static void fm1_br_alarm_off(void)
{
    FM1_BR_IACK = 1u << 9;
    FM1_BR_IEN &= ~(1u << 9);
}

/* BR §6.3: slot timer k at slot `at`, enabled; the others' enables kept */
static void fm1_br_stim_set(uint32_t k, uint32_t at)
{
    FM1_BR_STIM_AT(k) = at & FM1_BR_CLK_MASK;
    FM1_BR_STIM = (FM1_BR_STIM & 0xFFu) | (0x100u << k) | (1u << k);
}
FM1_INLINE uint32_t fm1_br_stim_pending(void) { return (FM1_BR_STIM >> 16) & 0xFFu; }
FM1_INLINE void fm1_br_stim_ack(uint32_t mask) { FM1_BR_STIM = (FM1_BR_STIM & 0xFFu) | (mask & 0xFFu) << 8; }
FM1_INLINE void fm1_br_stim_all_off(void) { FM1_BR_STIM = 0xFF00u; }

/* BR §10.2 (after the radio's start-up, hal/fm1_ble.h fm1_ble_rf_init), in order, with the table zeroed by the caller.
 * The library reads 0x1400C at step 7 (meaning unknown): not done. */
/* BR §10.2's register writes in order (after the table address; the eSCO port groups of BR §2.3 with them), as a list:
 * one loop of plain word stores */
static const uint32_t fm1_br_init_seq[][2] = {
    {0x2000Cu, 0}, {0x2000Cu, 0xFFFF0802u}, {0x20018u, 0xFFFFu}, {0x2002Cu, 0x140u}, {0x20058u, 0x4000u},
    {0x2005Cu, 0}, {0x200F0u, 0}, {0x200F4u, 0},
    {0x20120u, 0x06000000u}, {0x20128u, 0xC0u}, {0x20134u, 8u}, {0x20120u, 0x06000000u}, {0x20124u, 0x8015u},
    {0x20150u, 0x06000000u}, {0x20158u, 0xC0u}, {0x20164u, 8u}, {0x20150u, 0x06000000u}, {0x20154u, 0x8015u},
    {0x20000u, 0x107u}, {0x20080u, 0x11480A48u}, {0x2FC48u, 0xADu}, {0x20084u, 0}, {0x2007Cu, 0xA04u},
    {0x20138u, 0x0280A550u}, {0x20168u, 0x0280A550u},   /* the SDK's eSCO RX defaults (V15's differ; unused here) */
    {0x20160u, FM1_BR_OFF_ESCO * 0x10001u}, {0x2015Cu, FM1_BR_OFF_ESCO * 0x10001u},
    {0x20130u, FM1_BR_OFF_ESCO * 0x10001u}, {0x2012Cu, FM1_BR_OFF_ESCO * 0x10001u},
};

/* BR §10.2 (after the radio's start-up, hal/fm1_ble.h fm1_ble_rf_init), in order, with the table zeroed by the caller.
 * (0x20120 / 0x20150 are written again with bit 16 clear where the library clears it by a read-modify-write; the
 * library reads 0x1400C at step 7, meaning unknown: not done.) */
static void fm1_br_init(uint8_t *table)
{
    uint32_t i;
    FM1_BR_BTEN |= 1u << 10;
    FM1_BR_TBASE = (uint32_t)(uintptr_t)table;
    for (i = 0; i < sizeof fm1_br_init_seq / sizeof fm1_br_init_seq[0]; i++)
        FM1_BR_REG(fm1_br_init_seq[i][0]) = fm1_br_init_seq[i][1];
    for (i = 0; i < 40u; i++) {                    /* frq_table[0]: even channels, then odd (BR §7.1) */
        table[FM1_BR_OFF_FRQ + i] = (uint8_t)(2u * i);
        if (i < 39u)
            table[FM1_BR_OFF_FRQ + 40u + i] = (uint8_t)(2u * i + 1u);
    }
    for (i = 0; i < 16u; i++)                      /* the exchange table: all closed */
        fm1_br_xchg(table, i, 1u << 12);
    FM1_BR_CON |= 0x80000000u;
    FM1_BR_CON |= 0x100u;
    fm1_br_stim_all_off();
}

/* the interrupt entries (as fm1_isr.S), here so a build without the probe carries none of it. The C bodies are the
 * probe's (src/bt/bt_inq.c). */
void bti_bt_irq(void);
void bti_slot_irq(void);
__asm__(".section .text.isr_bti_bt,\"ax\",@progbits\n"
        "\t.globl isr_bti_bt\n"
        "isr_bti_bt:\n"
        "\t[--sp] = {psr, rets, reti}\n"
        "\t[--sp] = {r3-r0}\n"
        "\tcall bti_bt_irq\n"
        "\t{r3-r0} = [sp++]\n"
        "\t{psr, rets, reti} = [sp++]\n"
        "\tcsync\n"
        "\trti\n"
        "\t.previous\n");
__asm__(".section .text.isr_bti_slot,\"ax\",@progbits\n"
        "\t.globl isr_bti_slot\n"
        "isr_bti_slot:\n"
        "\t[--sp] = {psr, rets, reti}\n"
        "\t[--sp] = {r3-r0}\n"
        "\tcall bti_slot_irq\n"
        "\t{r3-r0} = [sp++]\n"
        "\t{psr, rets, reti} = [sp++]\n"
        "\tcsync\n"
        "\trti\n"
        "\t.previous\n");
extern void isr_bti_bt(void);
extern void isr_bti_slot(void);

/* IRQs off (boot): both vectors at prio, held in the interrupt controller until the block is up */
static void fm1_br_irq_attach(uint32_t prio)
{
    fm1_irq_attach(FM1_IRQ_BREDR, isr_bti_bt, prio);
    fm1_irq_attach(FM1_IRQ_BT_SLOT, isr_bti_slot, prio);
    fm1_irq_hold(FM1_IRQ_BREDR, 1);
    fm1_irq_hold(FM1_IRQ_BT_SLOT, 1);
}
FM1_INLINE void fm1_br_irqs_hold(int hold)
{
    fm1_irq_hold(FM1_IRQ_BREDR, hold);
    fm1_irq_hold(FM1_IRQ_BT_SLOT, hold);
}
