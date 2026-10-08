/* SPDX-License-Identifier: GPL-3.0-only */
/* FM-1 (AC791N / WL82) radio start-up, the part before the BT block: the shared Wi-Fi front end that the BT RF
 * init runs first (docs/BLE-HW-FACTS.md §5.2, §16; branch feat/ble-facts d907ce3, "HW §n" below). Included by
 * hal/fm1_ble.h in a BLE build with the real driver.
 *
 * The long constant parts (§16.1 groups 2-6, 8-13: about 34,000 register words on a second boot) are not in this
 * repository. tools/ble_rf_capture.py observes the user's own stock V15 doing them in the emulator and writes
 * build/gen/ble_rf_tables.h (git-ignored): a small program this file runs, in boot-2 order:
 *
 *   register write           §16.1 groups 2, 3, 5, 6, 8, 9, 12 (0x11900-0x11964, 0x14040-0x1405C, the MAC window);
 *                            bit 14 of 0x11900 kept as found (§16.2: a periodic routine's bit, not a trim)
 *   BBP window write / read  §16.3: entry address, data, commit 1 / address, commit 2, the read-back register;
 *                            direct BBP registers; the port 0x3101C (§5.2: bit 17 start, bit 16 read)
 *   RF-die LUT words         §5.2 SPI port 0x14028 / 0x1402C, the five-high five-low kick (§16.1 group 11)
 *   trim field               §14.5, §16.4: the next write takes a field from VM 106 / 107 / 108 / 187 (the bytes
 *                            given to fm1_ble_rf_run); a read-modify-write field goes into the last read-back
 *   delay                    the gaps of 20 us or more in stock's trace
 *   VCO scan                 §16.1 group 7: our own search (fm1_ble_vco_scan below), not stock's
 *   replay                   §16.1 group 10: the BBP reload, the same as the first load (group 4)
 *   skipped                  §16.2: the window-D read-back loop (DC / IQ [I]), TODO(hardware)
 *
 * TODO(hardware), with the experiments of HW §16.5 / §18.3:
 *   - the window-D read-back loop (§16.2, 3,537 transactions in stock's boot 2): left out; its results depend on
 *     read-backs the emulator returns as 0 (U17). §18.3 step 3, the read-back build: write a window entry and read
 *     it back through 0xD3 (console 'blerf'), then decide how to measure and write those entries;
 *   - the RF-die LUT words a stored trim changes (VM 107 -> entries 0xE0-0xFF of both words, VM 187 bytes 48-63, the
 *     scan -> word-1 entries 0x00-0x7F): written as captured (the emulator's calibration), their mapping unknown.
 *     §16.5 row 1: after the scan, read the LUT back (SPI command 0x6);
 *   - the VCO band and the final 0x11934-0x11940: the scan below finds an in-range band, but stock's final writes
 *     (constants of the capture) are kept; compare with the emulator's on hardware (§18.3 step 3);
 *   - the BT TX trims (0x2FC08, 0x2FC10: §16.2, live from BBP read-backs): hal/fm1_ble.h keeps stock's first values;
 *   - delays: stock's gaps in the emulator, not a datasheet's settle times. */
#pragma once
#include <stdint.h>
#include "fm1_cc.h"
#include "fm1_time.h"
#include "ble_rf_tables.h"   /* build/gen: tools/ble_rf_capture.py (tools/build.py runs it or says how) */

#if !defined(BLE_RF_TABLES_FORMAT) || BLE_RF_TABLES_FORMAT != 1
#error "build/gen/ble_rf_tables.h is from another tools/ble_rf_capture.py: run it again"
#endif

#define FM1_RF_REG(a)     (*(volatile uint32_t *)(uintptr_t)(a))
#define FM1_BBP_PORT      (*(volatile uint32_t *)0x3101Cu)   /* HW §5.2: bit17 start, bit16 read, [15:8] reg, [7:0] data */
#define FM1_RFSPI_CTL     (*(volatile uint32_t *)0x14028u)   /* HW §5.2: [22:20] busy, 0x10000 | address << 8 | command */
#define FM1_RFSPI_DATA    (*(volatile uint32_t *)0x1402Cu)
#define FM1_RFSPI_WAIT    (*(volatile uint32_t *)0x14034u)   /* written 0 while waiting (HW §5.2) */
#define FM1_WL_A00        0x11900u                          /* HW §16.2: bit 14 kept */
#define FM1_WL_VCO_EN     (*(volatile uint32_t *)0x11934u)   /* HW §16.1 group 7: bit 24 pulsed per step */
#define FM1_WL_VCO_BAND   (*(volatile uint32_t *)0x11938u)   /* [25:19] the band; bits 16, 18 pulsed per step */
#define FM1_WL_VCO_FINE   (*(volatile uint32_t *)0x1193Cu)
#define FM1_WL_CAL_START  (*(volatile uint32_t *)0x11968u)   /* bit 28: start a comparator measurement */
#define FM1_WL_CAL_STROBE (*(volatile uint32_t *)0x11978u)   /* strobe 1 x 8 then 0; read: bit 17 low, bit 18 high */

/* values the sheet does not settle, named so nobody mistakes them for measurements */
#define FM1_BBP_POLLS 1000u               /* the BBP start bit polled to 0 (stock reads it once; the bound is ours) */
#define FM1_RFSPI_POLLS 1000u             /* HW §5.2 busy [22:20]; the bound is ours */
#define FM1_VCO_BANDS 64u                 /* stock's scan used bands 0..63 (§16.1 group 7, 0x11938 [25:19]) */
#define FM1_VCO_EDGE_US 2u                /* between the step's pulse edges (stock: ~2 us in the emulator) [I] */
#define FM1_VCO_SETTLE_US 50u             /* before a measurement (stock: ~45 us in the emulator) [I] */

struct fm1_ble_rf_stat {                  /* what the start-up did, for the console (io/console.c 'blerf') */
    uint32_t ops, bbp_timeouts, spi_timeouts, lut_words, trims, skipped, delay_us;
    uint32_t scan_result;                 /* the last comparator word */
    uint8_t ran, scan_found, scan_band, scan_steps, bad_op;
};
static struct fm1_ble_rf_stat fm1_ble_rf_stat;

static uint32_t fm1_bbp(uint32_t reg, uint32_t data, uint32_t rd)   /* one BBP transaction -> the port's [7:0] */
{
    uint32_t c = BLE_RF_BBP_FLAGS | rd << 16 | reg << 8 | data, i;
    FM1_BBP_PORT = c;
    FM1_BBP_PORT = c | 1u << 17;                            /* HW §5.2: the command, then with the start bit */
    for (i = 0; i < FM1_BBP_POLLS && (FM1_BBP_PORT & 1u << 17); i++)
        ;
    if (i == FM1_BBP_POLLS)
        fm1_ble_rf_stat.bbp_timeouts++;
    return FM1_BBP_PORT & 0xFFu;
}

/* the BBP windows (HW §16.3): window w's commit, address, data, read-back = 0xC8 + 4w .. 0xCB + 4w */
static void fm1_bbp_win_wr(uint32_t w, uint32_t entry, uint32_t v)
{
    fm1_bbp(0xC9u + 4u * w, entry, 0);
    fm1_bbp(0xCAu + 4u * w, v & 0xFFu, 0);
    fm1_bbp(0xC8u + 4u * w, 1, 0);
}
static uint32_t fm1_bbp_win_rd(uint32_t w, uint32_t entry)   /* [I] that it returns the entry: U17 */
{
    fm1_bbp(0xC9u + 4u * w, entry, 0);
    fm1_bbp(0xC8u + 4u * w, 2, 0);
    return fm1_bbp(0xCBu + 4u * w, 0, 1);
}

static void fm1_rfspi(uint32_t addr, uint32_t cmd, uint32_t data)   /* HW §5.2: one RF-die SPI command */
{
    uint32_t i;
    for (i = 0; i < FM1_RFSPI_POLLS && (FM1_RFSPI_CTL >> 20 & 7u); i++)
        FM1_RFSPI_WAIT = 0;
    if (i == FM1_RFSPI_POLLS)
        fm1_ble_rf_stat.spi_timeouts++;
    FM1_RFSPI_DATA = data;
    FM1_RFSPI_CTL = 0x10000u | (addr & 0xFFu) << 8 | (cmd & 0xFu);
    for (i = 0; i < 5u; i++)                                /* the kick: bit 4 five times high, five times low */
        FM1_RFSPI_CTL |= 0x10u;
    for (i = 0; i < 5u; i++)
        FM1_RFSPI_CTL &= ~0x10u;
}

/* §16.1 group 7, our own search (not stock's): with the fine code stock starts from, find a band whose comparator
 * reads neither low (bit 17) nor high (bit 18): bisection over FM1_VCO_BANDS, a higher band when low. The step:
 * the band into 0x11938 [25:19], 0x11938 bits 16 / 18 and 0x11934 bit 24 pulsed low then high, 0x11968 bit 28,
 * eight strobes of 0x11978 then 0, the comparator in 0x11978 (the registers and the order as stock's boot-2 trace
 * shows them: BLE-STACK.md §12). Its result is reported only: stock's final band and fine code follow as constants
 * (TODO(hardware) above). */
static void fm1_ble_vco_scan(uint32_t fine0)
{
    uint32_t lo = 0, hi = FM1_VCO_BANDS - 1u, b, r, i;
    FM1_WL_VCO_FINE = fine0;
    fm1_ble_rf_stat.scan_found = 0;
    fm1_ble_rf_stat.scan_steps = 0;
    while (lo <= hi) {
        b = (lo + hi) / 2u;
        FM1_WL_VCO_BAND = (FM1_WL_VCO_BAND & ~(0x7Fu << 19)) | b << 19;
        FM1_WL_VCO_BAND &= ~(1u << 16);
        FM1_WL_VCO_BAND &= ~(1u << 18);
        FM1_WL_VCO_EN &= ~(1u << 24);
        fm1_delay_us(FM1_VCO_EDGE_US);
        FM1_WL_VCO_BAND |= 1u << 16;
        fm1_delay_us(FM1_VCO_EDGE_US);
        FM1_WL_VCO_EN |= 1u << 24;
        FM1_WL_VCO_BAND |= 1u << 18;
        fm1_delay_us(FM1_VCO_SETTLE_US);
        FM1_WL_CAL_START |= 1u << 28;
        for (i = 0; i < 8u; i++)
            FM1_WL_CAL_STROBE = 1;
        FM1_WL_CAL_STROBE = 0;
        r = FM1_WL_CAL_STROBE;
        fm1_ble_rf_stat.scan_steps++;
        fm1_ble_rf_stat.scan_result = r;
        fm1_ble_rf_stat.scan_band = (uint8_t)b;
        if (r & 1u << 17)
            lo = b + 1u;                                    /* low: a higher band */
        else if (r & 1u << 18) {
            if (!b)
                break;
            hi = b - 1u;                                    /* high: a lower band */
        } else {
            fm1_ble_rf_stat.scan_found = 1;
            break;
        }
    }
}

static uint32_t fm1_rf_field(uint32_t v, uint32_t f, const uint8_t *const rec[4])   /* §14.5 / §16.4 */
{
    const uint8_t *d = &ble_rf_fields[4u * f];
    uint32_t x = rec[d[0] & 3u][d[1]], s = d[2], w = d[3] & 0x7Fu, m;
    if (d[3] & 0x80u)                                       /* VM 108: a non-zero byte replaces the default */
        return x ? x : v;
    m = ((1u << w) - 1u) << s;
    return (v & ~m) | (x << s & m);
}

static uint32_t fm1_rf_u32(const uint8_t *p) { return p[0] | p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

/* run ble_rf_prog[pc, end) (one level of replay); rec = the data of VM 106, 107, 108, 187 */
static void fm1_ble_rf_exec(uint32_t pc, uint32_t end, const uint8_t *const rec[4], int depth)
{
    const uint8_t *p = ble_rf_prog;
    uint8_t trim[8], rmw = 0, nt = 0;
    uint32_t last = 0, v, a, i, n;
    while (pc < end) {
        uint32_t op = p[pc];
        fm1_ble_rf_stat.ops++;
        if (op < 0x80u) {                                   /* a register write */
            a = ble_rf_addr[op];
            v = fm1_rf_u32(p + pc + 1);
            if (a == FM1_WL_A00)
                v = (v & ~(1u << 14)) | (FM1_RF_REG(a) & 1u << 14);
            for (i = 0; i < nt; i++)
                v = fm1_rf_field(v, trim[i], rec);
            FM1_RF_REG(a) = v;
            nt = rmw = 0;
            pc += 5;
        } else if ((op & 0xFCu) == 0x80u) {                 /* a window write */
            v = rmw ? last : p[pc + 2];
            for (i = 0; i < nt; i++)
                v = fm1_rf_field(v, trim[i], rec);
            fm1_bbp_win_wr(op & 3u, p[pc + 1], v);
            nt = rmw = 0;
            pc += 3;
        } else if ((op & 0xFCu) == 0x84u) {                 /* a window read */
            last = fm1_bbp_win_rd(op & 3u, p[pc + 1]);
            pc += 2;
        } else if (op == 0x88u) {
            fm1_bbp(p[pc + 1], p[pc + 2], 0);
            pc += 3;
        } else if (op == 0x89u) {
            last = fm1_bbp(p[pc + 1], 0, 1);
            pc += 2;
        } else if (op == 0x8Au) {                           /* LUT words: command, first entry, count, words */
            n = p[pc + 3];
            for (i = 0; i < n; i++)
                fm1_rfspi(p[pc + 2] + i, p[pc + 1], fm1_rf_u32(p + pc + 4 + 4u * i));
            fm1_ble_rf_stat.lut_words += n;
            pc += 4u + 4u * n;
        } else if (op == 0x8Bu) {                           /* a trim field for the next write */
            if (nt < sizeof trim && p[pc + 1] < BLE_RF_NFIELDS)
                trim[nt++] = p[pc + 1];
            else
                fm1_ble_rf_stat.bad_op = (uint8_t)op;
            rmw |= p[pc + 2] & 1u;
            fm1_ble_rf_stat.trims++;
            pc += 3;
        } else if (op == 0x8Cu) {
            n = p[pc + 1] | p[pc + 2] << 8;
            fm1_delay_us(n);
            fm1_ble_rf_stat.delay_us += n;
            pc += 3;
        } else if (op == 0x8Du) {
            fm1_ble_vco_scan(fm1_rf_u32(p + pc + 1));
            pc += 5;
        } else if (op == 0x8Eu) {
            a = p[pc + 1] | p[pc + 2] << 8;
            n = p[pc + 3] | p[pc + 4] << 8;
            if (!depth && a + n <= sizeof ble_rf_prog)
                fm1_ble_rf_exec(a, a + n, rec, 1);
            else
                fm1_ble_rf_stat.bad_op = (uint8_t)op;
            pc += 5;
        } else if (op == 0x8Fu) {
            fm1_ble_rf_stat.skipped += p[pc + 1] | p[pc + 2] << 8;   /* TODO(hardware): the read-back loop */
            pc += 3;
        } else {                                            /* 0xFF: the end (anything else: stop there) */
            if (op != 0xFFu)
                fm1_ble_rf_stat.bad_op = (uint8_t)op;
            return;
        }
    }
}

/* HW §16.1 groups 2-13 with the stored trims (the data of VM 106, 107, 108, 187, in that order) */
static void fm1_ble_rf_run(const uint8_t *x106, const uint8_t *x107, const uint8_t *x108, const uint8_t *x187)
{
    const uint8_t *const rec[4] = {x106, x107, x108, x187};
    fm1_ble_rf_exec(0, sizeof ble_rf_prog, rec, 0);
    fm1_ble_rf_stat.ran = 1;
}
